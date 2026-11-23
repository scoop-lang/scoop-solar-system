#include "solar_renderer.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <string.h>

static void finish(id<MTLCommandQueue> queue) {
  id<MTLCommandBuffer> barrier = [queue commandBuffer];
  [barrier commit];
  [barrier waitUntilCompleted];
  assert(barrier.status == MTLCommandBufferStatusCompleted);
}

static double center_brightness(id<MTLTexture> target) {
  unsigned char pixels[64 * 64 * 4];
  [target getBytes:pixels
       bytesPerRow:64 * 4
        fromRegion:MTLRegionMake2D(96, 96, 64, 64)
       mipmapLevel:0];
  double sum = 0;
  for (int i = 0; i < 64 * 64; i++)
    sum += pixels[i * 4] + pixels[i * 4 + 1] + pixels[i * 4 + 2];
  return sum / (64 * 64 * 3);
}

int main(int argc, char **argv) {
  assert(argc == 2);
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];
    void *r =
        solar_renderer_create((__bridge void *)device, (__bridge void *)queue);
    if (!r) {
      fprintf(stderr, "%s\n", solar_renderer_error());
      return 1;
    }
    NSString *root = [NSString stringWithUTF8String:argv[1]];
    NSData *data = [NSData
        dataWithContentsOfFile:
            [root stringByAppendingPathComponent:@"notices/textures.json"]];
    NSError *error = nil;
    NSDictionary *manifest = [NSJSONSerialization JSONObjectWithData:data
                                                             options:0
                                                               error:&error];
    assert(manifest && !error);
    for (NSDictionary *entry in manifest[@"assets"]) {
      NSString *file = [[root stringByAppendingPathComponent:@"textures"]
          stringByAppendingPathComponent:entry[@"file"]];
      if (!solar_renderer_load(r, [entry[@"renderer_id"] intValue],
                               file.UTF8String)) {
        fprintf(stderr, "%s: %s\n", file.UTF8String, solar_renderer_error());
        return 1;
      }
    }
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:256
                                    height:256
                                 mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [device newTextureWithDescriptor:descriptor];
    unsigned rendered = 0;
    for (NSDictionary *entry in manifest[@"assets"]) {
      int id = [entry[@"renderer_id"] intValue];
      if (id < 0)
        continue;
      SolarBody body = {
          {3, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {.45, .45, .45},
          id,        0,         0,         .8};
      if (id == 10)
        body.position.x = 0;
      SolarCamera camera = {
          {body.position.x - 2.5, -.6, .65}, body.position, {0, 0, 1}, .72};
      solar_renderer_begin(r);
      solar_renderer_body(r, body);
      assert(
          solar_renderer_render(r, (__bridge void *)target, camera, 0, 0, 0));
      finish(queue);
      double brightness = center_brightness(target);
      if (brightness <= 8) {
        fprintf(stderr, "Body %d rendered unexpectedly dark: %.1f\n", id,
                brightness);
        return 1;
      }
      if (id == 401 || id == 402 || (id >= 701 && id <= 705)) {
        unsigned char center[4];
        [target getBytes:center
             bytesPerRow:4
              fromRegion:MTLRegionMake2D(128, 128, 1, 1)
             mipmapLevel:0];
        assert(abs((int)center[0] - center[1]) <= 2);
        assert(abs((int)center[1] - center[2]) <= 2);
      }
      if (id == 399) {
        double day = center_brightness(target);
        unsigned char first[256 * 256 * 4], repeated[256 * 256 * 4];
        [target getBytes:first
             bytesPerRow:256 * 4
              fromRegion:MTLRegionMake2D(0, 0, 256, 256)
             mipmapLevel:0];
        assert(
            solar_renderer_render(r, (__bridge void *)target, camera, 0, 0, 0));
        finish(queue);
        [target getBytes:repeated
             bytesPerRow:256 * 4
              fromRegion:MTLRegionMake2D(0, 0, 256, 256)
             mipmapLevel:0];
        assert(memcmp(first, repeated, sizeof(first)) == 0);
        camera.eye.x = 5.5;
        assert(
            solar_renderer_render(r, (__bridge void *)target, camera, 0, 0, 0));
        finish(queue);
        double night = center_brightness(target);
        assert(day > night * 1.5);
        printf("Earth daylight %.1f, night side %.1f; identical frames match "
               "byte for byte\n",
               day, night);
      }
      rendered++;
    }
    assert(rendered == 31);
    solar_renderer_close(r);
    printf("Metal: five pipelines, 37 maps and %u body frames passed on %s\n",
           rendered, device.name.UTF8String);
  }
  return 0;
}
