#include "solar_ephemeris.h"
#import <Foundation/Foundation.h>

const char *solar_resource_root(void) {
  static NSString *root;
  if (!root) {
    NSString *override =
        NSProcessInfo.processInfo.environment[@"SCOOP_SOLAR_RESOURCES"];
    NSString *bundled = [NSBundle.mainBundle.resourcePath
        stringByAppendingPathComponent:@"solar"];
    if (override.length)
      root = override;
    else if ([NSFileManager.defaultManager fileExistsAtPath:bundled])
      root = bundled;
    else
      root = [NSFileManager.defaultManager.currentDirectoryPath
          stringByAppendingPathComponent:@"assets/prepared"];
  }
  return root.UTF8String;
}
