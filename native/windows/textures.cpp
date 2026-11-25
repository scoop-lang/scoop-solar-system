#include "renderer_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <wincodec.h>

namespace {

struct Pixels {
  UINT width = 0, height = 0;
  std::vector<unsigned char> rgba;
};

// The calling UI thread normally has COM already; balance only our own call.
struct ComScope {
  HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  ~ComScope() {
    if (SUCCEEDED(status))
      CoUninitialize();
  }
};

int fail(const char *filename, const char *message, HRESULT status) {
  char text[1536];
  snprintf(text, sizeof(text), "%s: %s", filename, message);
  return solar_d3d_fail(text, status);
}

// Decodes any WIC-supported file to straight RGBA8; grey and grey-alpha maps
// expand to RGB(A) as with GdkPixbuf on Linux.
int decode(const char *filename, UINT limit, Pixels *pixels) {
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, filename, -1,
                                  nullptr, 0);
  if (count <= 0)
    return fail(filename, "the path is not valid UTF-8", S_OK);
  std::vector<wchar_t> path(count);
  MultiByteToWideChar(CP_UTF8, 0, filename, -1, path.data(), count);
  ComScope com;
  ComPtr<IWICImagingFactory> factory;
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  HRESULT status =
      CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&factory));
  if (FAILED(status))
    return fail(filename, "unable to create the WIC factory", status);
  status = factory->CreateDecoderFromFilename(
      path.data(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
      &decoder);
  if (SUCCEEDED(status))
    status = decoder->GetFrame(0, &frame);
  if (SUCCEEDED(status))
    status = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(status))
    status = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0,
                                   WICBitmapPaletteTypeCustom);
  if (FAILED(status))
    return fail(filename, "unable to decode image", status);
  ComPtr<IWICBitmapSource> source = converter;
  UINT width = 0, height = 0;
  source->GetSize(&width, &height);
  if (width > limit || height > limit) {
    // Keep the map's aspect ratio when the device limit is below its source.
    double factor = (double)limit / (width > height ? width : height);
    UINT scaledWidth = (UINT)(width * factor),
         scaledHeight = (UINT)(height * factor);
    ComPtr<IWICBitmapScaler> scaler;
    status = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(status))
      status = scaler->Initialize(source.Get(), scaledWidth, scaledHeight,
                                  WICBitmapInterpolationModeLinear);
    if (FAILED(status))
      return fail(filename, "unable to fit the map within the texture limit",
                  status);
    source = scaler;
    width = scaledWidth;
    height = scaledHeight;
  }
  pixels->width = width;
  pixels->height = height;
  pixels->rgba.resize((size_t)width * height * 4);
  status = source->CopyPixels(nullptr, width * 4, (UINT)pixels->rgba.size(),
                              pixels->rgba.data());
  if (FAILED(status))
    return fail(filename, "unable to read decoded pixels", status);
  return 1;
}

// 16-bit linear light per sRGB byte, and the sRGB byte for each linear value.
unsigned short to_linear[256];
unsigned char to_srgb[65536];

void tables() {
  if (to_linear[255])
    return;
  for (int i = 0; i < 256; i++) {
    float value = i / 255.0f;
    value = value <= 0.04045f ? value / 12.92f
                              : powf((value + 0.055f) / 1.055f, 2.4f);
    to_linear[i] = (unsigned short)(value * 65535.0f + 0.5f);
  }
  for (int i = 0; i < 65536; i++) {
    float value = i / 65535.0f;
    value = value <= 0.0031308f ? value * 12.92f
                                : 1.055f * powf(value, 1.0f / 2.4f) - 0.055f;
    to_srgb[i] = (unsigned char)fminf(255.0f, value * 255.0f + 0.5f);
  }
}

// Halves a level with a box filter. sRGB colour channels are averaged in
// linear light, as glGenerateMipmap does for sRGB textures; alpha and data
// maps are averaged directly.
Pixels reduce(const Pixels &level, bool srgb) {
  Pixels next;
  next.width = level.width > 1 ? level.width / 2 : 1;
  next.height = level.height > 1 ? level.height / 2 : 1;
  next.rgba.resize((size_t)next.width * next.height * 4);
  const unsigned char *in = level.rgba.data();
  unsigned char *out = next.rgba.data();
  for (UINT y = 0; y < next.height; y++) {
    const unsigned char *top = in + (size_t)(y * 2) * level.width * 4;
    const unsigned char *bottom =
        y * 2 + 1 < level.height ? top + (size_t)level.width * 4 : top;
    for (UINT x = 0; x < next.width; x++, out += 4) {
      size_t left = (size_t)x * 8,
             right = x * 2 + 1 < level.width ? left + 4 : left;
      for (int c = 0; c < 4; c++) {
        unsigned a = top[left + c], b = top[right + c], d = bottom[left + c],
                 e = bottom[right + c];
        out[c] = srgb && c < 3 ? to_srgb[(to_linear[a] + to_linear[b] +
                                          to_linear[d] + to_linear[e] + 2) >>
                                         2]
                               : (unsigned char)((a + b + d + e + 2) >> 2);
      }
    }
  }
  return next;
}

} // namespace

int solar_d3d_load(SolarD3DRenderer *r, int32_t body, const char *filename) {
  tables();
  Pixels base;
  if (!decode(filename, D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION, &base))
    return 0;
  bool srgb = body != -1 && body != -2 && body != -3;
  std::vector<Pixels> levels;
  levels.push_back(std::move(base));
  while (levels.back().width > 1 || levels.back().height > 1)
    levels.push_back(reduce(levels.back(), srgb));

  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = levels[0].width;
  desc.Height = levels[0].height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = (UINT16)levels.size();
  desc.Format =
      srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  ComPtr<ID3D12Resource> texture;
  HRESULT status = r->device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
      nullptr, IID_PPV_ARGS(&texture));
  if (FAILED(status))
    return fail(filename, "unable to create the GPU texture", status);

  std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(levels.size());
  UINT64 total = 0;
  r->device->GetCopyableFootprints(&desc, 0, (UINT)levels.size(), 0,
                                   layouts.data(), nullptr, nullptr, &total);
  heap.Type = D3D12_HEAP_TYPE_UPLOAD;
  D3D12_RESOURCE_DESC buffer = {};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = total;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> staging;
  status = r->device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr, IID_PPV_ARGS(&staging));
  unsigned char *mapped = nullptr;
  D3D12_RANGE none = {0, 0};
  if (SUCCEEDED(status))
    status = staging->Map(0, &none, reinterpret_cast<void **>(&mapped));
  if (FAILED(status))
    return fail(filename, "unable to stage map pixels", status);
  for (size_t i = 0; i < levels.size(); i++)
    for (UINT y = 0; y < levels[i].height; y++)
      memcpy(mapped + layouts[i].Offset +
                 (size_t)y * layouts[i].Footprint.RowPitch,
             levels[i].rgba.data() + (size_t)y * levels[i].width * 4,
             (size_t)levels[i].width * 4);
  staging->Unmap(0, nullptr);

  // Loading runs inside a device borrow, so the copy is ordered on the shared
  // queue; wait once so the staging memory can be released immediately.
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commands;
  status = r->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             IID_PPV_ARGS(&allocator));
  if (SUCCEEDED(status))
    status = r->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                          allocator.Get(), nullptr,
                                          IID_PPV_ARGS(&commands));
  if (FAILED(status))
    return fail(filename, "unable to record the map upload", status);
  for (size_t i = 0; i < levels.size(); i++) {
    D3D12_TEXTURE_COPY_LOCATION to = {};
    to.pResource = texture.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    to.SubresourceIndex = (UINT)i;
    D3D12_TEXTURE_COPY_LOCATION from = {};
    from.pResource = staging.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = layouts[i];
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  }
  D3D12_RESOURCE_BARRIER barrier = {};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition.pResource = texture.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  commands->ResourceBarrier(1, &barrier);
  status = commands->Close();
  if (FAILED(status))
    return fail(filename, "unable to close the map upload", status);
  ID3D12CommandList *lists[] = {commands.Get()};
  r->queue->ExecuteCommandLists(1, lists);
  status = r->queue->Signal(r->fence.Get(), ++r->fenceValue);
  if (FAILED(status))
    return fail(filename, "unable to signal the map upload", status);
  if (!solar_d3d_wait(r, r->fenceValue))
    return 0;

  D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
  view.Format = desc.Format;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = desc.MipLevels;
  D3D12_CPU_DESCRIPTOR_HANDLE handle =
      r->maps->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += (SIZE_T)(r->textureCount + 1) * r->mapStride;
  r->device->CreateShaderResourceView(texture.Get(), &view, handle);
  r->textures[r->textureCount].body = body;
  r->textures[r->textureCount].resource = texture;
  r->textureCount++;
  return 1;
}
