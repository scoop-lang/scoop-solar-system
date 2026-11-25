#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "render_target.h"
#include <assert.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// D3D12 counterpart of gles.c; the assertions are kept identical.
static ComPtr<ID3D12Device> device;
static ComPtr<ID3D12CommandQueue> queue;
static ComPtr<ID3D12Resource> target, readback;
static ComPtr<ID3D12Fence> fence;
static UINT64 fence_value;
static HANDLE fence_event;
static D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;

static void check(HRESULT status, const char *what) {
  if (FAILED(status)) {
    fprintf(stderr, "%s failed: 0x%08lx\n", what, (unsigned long)status);
    abort();
  }
}

static void target_create() {
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = TARGET_SIZE;
  desc.Height = TARGET_SIZE;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  // COMMON matches a Skia buffer handed to withNativeWrite after sampling.
  check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                        IID_PPV_ARGS(&target)),
        "CreateCommittedResource(target)");
  UINT64 total = 0;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                &total);
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = total;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&readback)),
        "CreateCommittedResource(readback)");
  check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
        "CreateFence");
  fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  assert(fence_event);
}

static void finish() {
  check(queue->Signal(fence.Get(), ++fence_value), "Signal");
  if (fence->GetCompletedValue() < fence_value) {
    check(fence->SetEventOnCompletion(fence_value, fence_event),
          "SetEventOnCompletion");
    WaitForSingleObject(fence_event, INFINITE);
  }
}

extern "C" void target_render(void *renderer, SolarCamera camera) {
  if (!solar_renderer_render_resource(renderer, target.Get(),
                                      D3D12_RESOURCE_STATE_COMMON, camera, 0, 0,
                                      0)) {
    fprintf(stderr, "%s\n", solar_renderer_error());
    abort();
  }
}

extern "C" void target_read(unsigned char *pixels) {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       IID_PPV_ARGS(&allocator)),
        "CreateCommandAllocator");
  check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                  allocator.Get(), nullptr,
                                  IID_PPV_ARGS(&commands)),
        "CreateCommandList");
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = target.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
  commands->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION to = {}, from = {};
  to.pResource = readback.Get();
  to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  to.PlacedFootprint = footprint;
  from.pResource = target.Get();
  from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
  commands->ResourceBarrier(1, &barrier);
  check(commands->Close(), "Close");
  ID3D12CommandList *lists[] = {commands.Get()};
  queue->ExecuteCommandLists(1, lists);
  finish();
  unsigned char *mapped = nullptr;
  check(readback->Map(0, nullptr, reinterpret_cast<void **>(&mapped)), "Map");
  // The target is BGRA; convert to the RGBA order of the shared checks.
  for (int y = 0; y < TARGET_SIZE; y++)
    for (int x = 0; x < TARGET_SIZE; x++) {
      const unsigned char *in = mapped + footprint.Offset +
                                (size_t)y * footprint.Footprint.RowPitch +
                                (size_t)x * 4;
      unsigned char *out = pixels + ((size_t)y * TARGET_SIZE + x) * 4;
      out[0] = in[2];
      out[1] = in[1];
      out[2] = in[0];
      out[3] = in[3];
    }
  D3D12_RANGE none = {0, 0};
  readback->Unmap(0, &none);
}

static double center_brightness(const unsigned char *pixels) {
  double sum = 0;
  for (int y = 96; y < 160; y++)
    for (int x = 96; x < 160; x++) {
      const unsigned char *p = pixels + (y * TARGET_SIZE + x) * 4;
      sum += p[0] + p[1] + p[2];
    }
  return sum / (64 * 64 * 3);
}

struct Map {
  char file[128];
  int id;
};

// Reads the versioned manifest's file/renderer_id pairs in declaration order.
static int read_manifest(const char *root, Map *maps, int capacity) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/notices/textures.json", root);
  FILE *input = fopen(path, "rb");
  assert(input);
  static char text[1 << 16];
  size_t size = fread(text, 1, sizeof(text) - 1, input);
  fclose(input);
  text[size] = 0;
  int count = 0;
  for (char *at = strstr(text, "\"file\": \""); at && count < capacity;
       at = strstr(at, "\"file\": \"")) {
    at += strlen("\"file\": \"");
    char *end = strchr(at, '"');
    assert(end && end - at < (long)sizeof(maps[count].file));
    memcpy(maps[count].file, at, end - at);
    maps[count].file[end - at] = 0;
    char *id = strstr(end, "\"renderer_id\": ");
    assert(id);
    maps[count++].id = atoi(id + strlen("\"renderer_id\": "));
    at = end;
  }
  return count;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  ComPtr<IDXGIFactory6> factory;
  check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2");
  ComPtr<IDXGIAdapter1> adapter;
  check(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                            IID_PPV_ARGS(&adapter)),
        "EnumAdapterByGpuPreference");
  DXGI_ADAPTER_DESC1 adapter_desc;
  adapter->GetDesc1(&adapter_desc);
  check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                          IID_PPV_ARGS(&device)),
        "D3D12CreateDevice");
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  check(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)),
        "CreateCommandQueue");

  void *r = solar_renderer_create(device.Get(), queue.Get());
  if (!r) {
    fprintf(stderr, "%s\n", solar_renderer_error());
    return 1;
  }
  Map maps[64];
  int count = read_manifest(argv[1], maps, 64);
  assert(count == 37);
  for (int i = 0; i < count; i++) {
    char file[4096];
    snprintf(file, sizeof(file), "%s/textures/%s", argv[1], maps[i].file);
    if (!solar_renderer_load(r, maps[i].id, file)) {
      fprintf(stderr, "%s: %s\n", file, solar_renderer_error());
      return 1;
    }
  }
  target_create();
  static unsigned char pixels[TARGET_SIZE * TARGET_SIZE * 4],
      repeated[TARGET_SIZE * TARGET_SIZE * 4];
  unsigned rendered = 0;
  for (int i = 0; i < count; i++) {
    int id = maps[i].id;
    if (id < 0)
      continue;
    SolarBody sun = {};
    sun.axis_x = {1, 0, 0};
    sun.axis_y = {0, 1, 0};
    sun.axis_z = {0, 0, 1};
    sun.radii = {.45, .45, .45};
    sun.physical_radii = {696340, 696340, 696340};
    sun.body = 10;
    SolarBody body = {};
    body.position = {3, 0, 0};
    body.axis_x = {1, 0, 0};
    body.axis_y = {0, 1, 0};
    body.axis_z = {0, 0, 1};
    body.radii = {.45, .45, .45};
    body.physical_position = {3 * 149597870.7, 0, 0};
    body.physical_radii = {6371, 6371, 6371};
    body.body = id;
    body.roughness = .8;
    if (id == 10)
      body = sun;
    SolarCamera camera = {
        {body.position.x - 2.5, -.6, .65}, body.position, {0, 0, 1}, .72};
    solar_renderer_begin(r);
    if (id != 10)
      solar_renderer_body(r, sun, 0);
    solar_renderer_body(r, body, 1);
    target_render(r, camera);
    target_read(pixels);
    double brightness = center_brightness(pixels);
    if (brightness <= 8) {
      fprintf(stderr, "Body %d rendered unexpectedly dark: %.1f\n", id,
              brightness);
      return 1;
    }
    if (id == 401 || id == 402 || (id >= 701 && id <= 705)) {
      const unsigned char *center = pixels + (128 * TARGET_SIZE + 128) * 4;
      assert(abs((int)center[0] - center[1]) <= 2);
      assert(abs((int)center[1] - center[2]) <= 2);
    }
    if (id == 399) {
      double day = brightness;
      target_render(r, camera);
      target_read(repeated);
      assert(memcmp(pixels, repeated, sizeof(pixels)) == 0);
      camera.eye.x = 5.5;
      target_render(r, camera);
      target_read(repeated);
      double night = center_brightness(repeated);
      assert(day > night * 1.5);
      printf("Earth daylight %.1f, night side %.1f; identical frames match "
             "byte for byte\n",
             day, night);
    }
    rendered++;
  }
  assert(rendered == 31);
  check_shadows(r, argv[1]);
  solar_renderer_close(r);
  char name[256];
  WideCharToMultiByte(CP_UTF8, 0, adapter_desc.Description, -1, name,
                      sizeof(name), nullptr, nullptr);
  printf("D3D12: five pipelines, 37 maps and %u body frames passed on %s\n",
         rendered, name);
  CloseHandle(fence_event);
  return 0;
}
