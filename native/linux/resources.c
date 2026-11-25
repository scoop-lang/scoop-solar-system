#define _DEFAULT_SOURCE
#include "solar_ephemeris.h"
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// scoop bundle installs the executable in usr/lib/<identifier>/bin and
// resources in usr/share/<identifier>.
#define SOLAR_BUNDLED "/../../../share/dev.scoop.solar-system/solar"

static int directory(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

const char *solar_resource_root(void) {
  static char root[PATH_MAX + 64];
  if (root[0])
    return root;
  const char *override = getenv("SCOOP_SOLAR_RESOURCES");
  if (override && override[0]) {
    snprintf(root, sizeof(root), "%s", override);
    return root;
  }
  char executable[PATH_MAX];
  ssize_t size = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
  if (size > 0) {
    executable[size] = 0;
    char *slash = strrchr(executable, '/');
    if (slash) {
      *slash = 0;
      char bundled[sizeof(root)];
      snprintf(bundled, sizeof(bundled), "%s" SOLAR_BUNDLED, executable);
      char resolved[PATH_MAX];
      if (directory(bundled) && realpath(bundled, resolved)) {
        snprintf(root, sizeof(root), "%s", resolved);
        return root;
      }
    }
  }
  char working[PATH_MAX];
  snprintf(root, sizeof(root), "%s/assets/prepared",
           getcwd(working, sizeof(working)) ? working : ".");
  return root;
}
