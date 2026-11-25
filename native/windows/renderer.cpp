#include "renderer_internal.h"
#include <d3dcompiler.h>
#include <new>
#include <stdio.h>
#include <string.h>

static thread_local char last_error[2048];

int solar_d3d_fail(const char *message, HRESULT status) {
  if (status == S_OK)
    snprintf(last_error, sizeof(last_error), "%s",
             message ? message : "D3D12 operation failed");
  else
    snprintf(last_error, sizeof(last_error), "%s (HRESULT 0x%08lx)",
             message ? message : "D3D12 operation failed",
             static_cast<unsigned long>(status));
  return 0;
}
const char *solar_renderer_error(void) { return last_error; }

int solar_d3d_wait(SolarD3DRenderer *r, UINT64 value) {
  if (r->fence->GetCompletedValue() >= value)
    return 1;
  HRESULT status = r->fence->SetEventOnCompletion(value, r->event);
  if (FAILED(status))
    return solar_d3d_fail("Unable to wait for D3D12 work", status);
  WaitForSingleObject(r->event, INFINITE);
  return 1;
}

static ComPtr<ID3DBlob> compile(const char *stage, const char *program) {
  const char *profile = strcmp(stage, "VERTEX") == 0 ? "vs_5_1" : "ps_5_1";
  const D3D_SHADER_MACRO defines[] = {
      {stage, "1"}, {program, "1"}, {nullptr, nullptr}};
  const char *source = solar_shader_source();
  ComPtr<ID3DBlob> code, errors;
  HRESULT status =
      D3DCompile(source, strlen(source), "solar.hlsl", defines, nullptr, "main",
                 profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
  if (FAILED(status)) {
    snprintf(last_error, sizeof(last_error), "Shader %s %s failed: %s", stage,
             program,
             errors ? static_cast<const char *>(errors->GetBufferPointer())
                    : "no compiler output");
    return nullptr;
  }
  return code;
}

static int root_signature(SolarD3DRenderer *r) {
  D3D12_DESCRIPTOR_RANGE ranges[6] = {};
  D3D12_ROOT_PARAMETER parameters[7] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  for (UINT i = 0; i < 6; i++) {
    ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[i].NumDescriptors = 1;
    ranges[i].BaseShaderRegister = i;
    parameters[i + 1].ParameterType =
        D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
    parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  // s0 repeats like the Metal mapSampler; s1 clamps like ringSampler.
  D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
  for (UINT i = 0; i < 2; i++) {
    D3D12_TEXTURE_ADDRESS_MODE mode = i == 0 ? D3D12_TEXTURE_ADDRESS_MODE_WRAP
                                             : D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[i].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = mode;
    samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[i].ShaderRegister = i;
    samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }
  D3D12_ROOT_SIGNATURE_DESC desc = {};
  desc.NumParameters = 7;
  desc.pParameters = parameters;
  desc.NumStaticSamplers = 2;
  desc.pStaticSamplers = samplers;
  desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  ComPtr<ID3DBlob> blob, errors;
  HRESULT status = D3D12SerializeRootSignature(
      &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errors);
  if (FAILED(status))
    return solar_d3d_fail(
        errors ? static_cast<const char *>(errors->GetBufferPointer())
               : "Unable to serialize the root signature",
        status);
  status = r->device->CreateRootSignature(0, blob->GetBufferPointer(),
                                          blob->GetBufferSize(),
                                          IID_PPV_ARGS(&r->root));
  return SUCCEEDED(status) ||
         solar_d3d_fail("Unable to create the root signature", status);
}

static int pipeline(SolarD3DRenderer *r, int index, const char *program) {
  ComPtr<ID3DBlob> vertex = compile("VERTEX", program);
  if (!vertex)
    return 0;
  ComPtr<ID3DBlob> fragment = compile("FRAGMENT", program);
  if (!fragment)
    return 0;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = r->root.Get();
  desc.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
  desc.PS = {fragment->GetBufferPointer(), fragment->GetBufferSize()};
  const D3D12_INPUT_ELEMENT_DESC line[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(SolarFloat4),
       D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};
  if (index == SOLAR_LINE)
    desc.InputLayout = {line, 2};
  // Blending matches the GLES/Metal adapters for every non-surface program.
  D3D12_RENDER_TARGET_BLEND_DESC &blend = desc.BlendState.RenderTarget[0];
  blend.BlendEnable = index != SOLAR_SURFACE;
  blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOp = D3D12_BLEND_OP_ADD;
  blend.SrcBlendAlpha = D3D12_BLEND_ONE;
  blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
  blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  blend.LogicOp = D3D12_LOGIC_OP_NOOP;
  blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  desc.RasterizerState.DepthClipEnable = TRUE;
  // Reversed [0, 1] depth: cleared to 0 and tested with GREATER_EQUAL.
  desc.DepthStencilState.DepthEnable = TRUE;
  desc.DepthStencilState.DepthWriteMask = index == SOLAR_SURFACE
                                              ? D3D12_DEPTH_WRITE_MASK_ALL
                                              : D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
  desc.PrimitiveTopologyType = index == SOLAR_LINE
                                   ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE
                                   : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  desc.NumRenderTargets = 1;
  // Matches the BGRA8 Unorm target chosen in platform/windows/backend.scoop.
  desc.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  desc.SampleDesc.Count = 1;
  HRESULT status = r->device->CreateGraphicsPipelineState(
      &desc, IID_PPV_ARGS(&r->pipelines[index]));
  if (FAILED(status)) {
    char message[128];
    snprintf(message, sizeof(message), "Unable to create the %s pipeline",
             program);
    return solar_d3d_fail(message, status);
  }
  return 1;
}

static ComPtr<ID3D12DescriptorHeap> heap(ID3D12Device *device,
                                         D3D12_DESCRIPTOR_HEAP_TYPE type,
                                         UINT count, bool visible) {
  D3D12_DESCRIPTOR_HEAP_DESC desc = {};
  desc.Type = type;
  desc.NumDescriptors = count;
  desc.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                       : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  ComPtr<ID3D12DescriptorHeap> result;
  if (FAILED(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&result))))
    return nullptr;
  return result;
}

static int objects(SolarD3DRenderer *r) {
  const char *names[SOLAR_PROGRAMS] = {"SPHERE", "RING", "LINE", "PARTICLE",
                                       "GLOW"};
  if (!root_signature(r))
    return 0;
  for (int i = 0; i < SOLAR_PROGRAMS; i++)
    if (!pipeline(r, i, names[i]))
      return 0;
  r->targets = heap(r->device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
  r->depths = heap(r->device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);
  r->maps = heap(r->device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                 SOLAR_MAX_TEXTURES + 1, true);
  if (!r->targets || !r->depths || !r->maps)
    return solar_d3d_fail("Unable to create D3D12 descriptor heaps");
  r->mapStride = r->device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC empty = {};
  empty.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  empty.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  empty.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  empty.Texture2D.MipLevels = 1;
  r->device->CreateShaderResourceView(
      nullptr, &empty, r->maps->GetCPUDescriptorHandleForHeapStart());
  HRESULT status =
      r->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r->fence));
  if (FAILED(status))
    return solar_d3d_fail("Unable to create the D3D12 fence", status);
  r->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!r->event)
    return solar_d3d_fail("Unable to create the fence event");
  D3D12_HEAP_PROPERTIES upload = {};
  upload.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = SOLAR_UPLOAD_BYTES;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  for (SolarFrame &frame : r->frames) {
    status = r->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               IID_PPV_ARGS(&frame.allocator));
    if (SUCCEEDED(status))
      status = r->device->CreateCommittedResource(
          &upload, D3D12_HEAP_FLAG_NONE, &buffer,
          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
          IID_PPV_ARGS(&frame.upload));
    D3D12_RANGE none = {0, 0};
    if (SUCCEEDED(status))
      status =
          frame.upload->Map(0, &none, reinterpret_cast<void **>(&frame.mapped));
    if (FAILED(status))
      return solar_d3d_fail("Unable to create D3D12 frame resources", status);
  }
  status = r->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        r->frames[0].allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&r->commands));
  if (SUCCEEDED(status))
    status = r->commands->Close();
  if (FAILED(status))
    return solar_d3d_fail("Unable to create the D3D12 command list", status);
  return 1;
}

static void release(SolarD3DRenderer *r) {
  if (r->fence)
    solar_d3d_wait(r, r->fenceValue);
  if (r->event)
    CloseHandle(r->event);
  delete r;
}

void *solar_renderer_create(void *device, void *queue) {
  auto *r = new (std::nothrow) SolarD3DRenderer{};
  if (!r) {
    solar_d3d_fail("Unable to allocate the D3D12 renderer");
    return nullptr;
  }
  r->device = static_cast<ID3D12Device *>(device);
  r->queue = static_cast<ID3D12CommandQueue *>(queue);
  if (!r->device || !r->queue || !objects(r)) {
    if (!r->device || !r->queue)
      solar_d3d_fail("A D3D12 device and direct queue are required");
    release(r);
    return nullptr;
  }
  return r;
}

UINT solar_d3d_texture(SolarD3DRenderer *r, int32_t body) {
  for (int i = 0; i < r->textureCount; i++)
    if (r->textures[i].body == body)
      return static_cast<UINT>(i + 1);
  return 0;
}

int32_t solar_renderer_load(void *opaque, int32_t body, const char *filename) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  if (solar_d3d_texture(r, body))
    return 1;
  if (r->textureCount >= SOLAR_MAX_TEXTURES)
    return solar_d3d_fail("Too many renderer textures");
  return solar_d3d_load(r, body, filename);
}

void solar_renderer_begin(void *opaque) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  r->bodyCount = 0;
  r->orbitCount = 0;
  r->sunPosition = {0, 0, 0};
  r->sunRadius = 0;
}
void solar_renderer_body(void *opaque, SolarBody body, int32_t visible) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  if (r->bodyCount < SOLAR_MAX_BODIES) {
    r->visible[r->bodyCount] = visible;
    r->bodies[r->bodyCount++] = body;
    if (body.body == 10) {
      r->sunPosition = body.physical_position;
      r->sunRadius = body.physical_radii.x;
    }
  }
}
void solar_renderer_orbit_point(void *opaque, int32_t slot, int32_t index,
                                SolarVector point) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  if (slot >= 0 && slot < SOLAR_MAX_BODIES && index >= 0 &&
      index < SOLAR_ORBIT_POINTS) {
    r->orbits[slot].points[index] = point;
    r->orbits[slot].count = index + 1;
  }
}
void solar_renderer_orbit_place(void *opaque, int32_t slot, SolarVector center,
                                double scale, int32_t selected) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  if (slot >= 0 && slot < SOLAR_MAX_BODIES) {
    r->orbits[slot].center = center;
    r->orbits[slot].scale = scale;
    r->orbits[slot].selected = selected;
    if (slot >= r->orbitCount)
      r->orbitCount = slot + 1;
  }
}

static int depth_target(SolarD3DRenderer *r, UINT64 width, UINT height) {
  if (r->depth && r->depthWidth == width && r->depthHeight == height)
    return 1;
  // In-flight frames keep their own reference to the previous depth texture.
  D3D12_HEAP_PROPERTIES properties = {};
  properties.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = width;
  desc.Height = height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_D32_FLOAT;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
               D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  D3D12_CLEAR_VALUE clear = {};
  clear.Format = DXGI_FORMAT_D32_FLOAT;
  clear.DepthStencil.Depth = 0;
  ComPtr<ID3D12Resource> depth;
  HRESULT status = r->device->CreateCommittedResource(
      &properties, D3D12_HEAP_FLAG_NONE, &desc,
      D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&depth));
  if (FAILED(status))
    return solar_d3d_fail("Unable to create the depth target", status);
  r->depth = depth;
  r->depthWidth = width;
  r->depthHeight = height;
  return 1;
}

static D3D12_RESOURCE_BARRIER transition(ID3D12Resource *resource,
                                         D3D12_RESOURCE_STATES before,
                                         D3D12_RESOURCE_STATES after) {
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = before;
  barrier.Transition.StateAfter = after;
  return barrier;
}

int32_t solar_renderer_render_resource(void *opaque, void *resource,
                                       uint32_t state, SolarCamera camera,
                                       double et, int32_t orbits,
                                       int32_t belt) {
  auto *r = static_cast<SolarD3DRenderer *>(opaque);
  auto *target = static_cast<ID3D12Resource *>(resource);
  if (!target)
    return solar_d3d_fail("A D3D12 render target is required");
  const D3D12_RESOURCE_DESC desc = target->GetDesc();
  if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
      desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))
    return solar_d3d_fail("The borrowed D3D12 texture is not a BGRA8 target");
  SolarFrame &frame = r->frames[r->frameIndex];
  if (!solar_d3d_wait(r, frame.fence) ||
      !depth_target(r, desc.Width, desc.Height))
    return 0;
  HRESULT status = frame.allocator->Reset();
  if (SUCCEEDED(status))
    status = r->commands->Reset(frame.allocator.Get(), nullptr);
  if (FAILED(status))
    return solar_d3d_fail("Unable to reset the D3D12 command list", status);
  frame.used = 0;
  frame.depth = r->depth;
  ID3D12GraphicsCommandList *c = r->commands.Get();
  const auto before = static_cast<D3D12_RESOURCE_STATES>(state);
  D3D12_RESOURCE_BARRIER barrier =
      transition(target, before, D3D12_RESOURCE_STATE_RENDER_TARGET);
  if (before != D3D12_RESOURCE_STATE_RENDER_TARGET)
    c->ResourceBarrier(1, &barrier);
  D3D12_CPU_DESCRIPTOR_HANDLE view =
      r->targets->GetCPUDescriptorHandleForHeapStart();
  D3D12_CPU_DESCRIPTOR_HANDLE depth =
      r->depths->GetCPUDescriptorHandleForHeapStart();
  r->device->CreateRenderTargetView(target, nullptr, view);
  r->device->CreateDepthStencilView(r->depth.Get(), nullptr, depth);
  c->OMSetRenderTargets(1, &view, FALSE, &depth);
  const float background[] = {0.008f, 0.014f, 0.026f, 1};
  c->ClearRenderTargetView(view, background, 0, nullptr);
  c->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 0, 0, 0, nullptr);
  const float width = static_cast<float>(desc.Width),
              height = static_cast<float>(desc.Height);
  D3D12_VIEWPORT viewport = {0, 0, width, height, 0, 1};
  D3D12_RECT scissor = {0, 0, static_cast<LONG>(desc.Width),
                        static_cast<LONG>(desc.Height)};
  c->RSSetViewports(1, &viewport);
  c->RSSetScissorRects(1, &scissor);
  c->SetGraphicsRootSignature(r->root.Get());
  ID3D12DescriptorHeap *heaps[] = {r->maps.Get()};
  c->SetDescriptorHeaps(1, heaps);
  SolarUniforms u = solar_uniforms(r, camera, width, height);
  int recorded = solar_draw_scene(r, &frame, camera, u, et, orbits, belt);
  // Return the borrowed texture to the state the Scoop buffer expects, on
  // both the normal and the failure path.
  if (before != D3D12_RESOURCE_STATE_RENDER_TARGET) {
    barrier = transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, before);
    c->ResourceBarrier(1, &barrier);
  }
  status = c->Close();
  if (FAILED(status))
    return solar_d3d_fail("Unable to close the D3D12 command list", status);
  ID3D12CommandList *lists[] = {c};
  r->queue->ExecuteCommandLists(1, lists);
  status = r->queue->Signal(r->fence.Get(), ++r->fenceValue);
  frame.fence = r->fenceValue;
  r->frameIndex = (r->frameIndex + 1) % SOLAR_FRAMES;
  if (FAILED(status))
    return solar_d3d_fail("Unable to signal the D3D12 fence", status);
  if (!recorded)
    return solar_d3d_fail("The D3D12 frame upload ring was exhausted");
  return 1;
}

void solar_renderer_close(void *opaque) {
  release(static_cast<SolarD3DRenderer *>(opaque));
}
