// N1-b packaging probe: loads the ReXGlue runtime libraries and MoltenVK that the CI build places
// in the app's Frameworks folder, and creates a Vulkan instance and device list through MoltenVK.
// It does not start the ReXGlue runtime itself (that is the next step). No game code or data.
#include "SelfTest.h"

#include <dirent.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_frameworks[1024];

void selftest_set_frameworks_dir(const char *path) {
  snprintf(g_frameworks, sizeof g_frameworks, "%s", path ? path : "");
}

static void say(selftest_log_fn log, const char *fmt, ...) {
  char buffer[600];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  if (log) log(buffer);
}

static int name_order(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int selftest_runtime_libraries(selftest_log_fn log) {
  if (!g_frameworks[0]) {
    say(log, "FAIL: Frameworks folder not set");
    return 0;
  }
  DIR *dir = opendir(g_frameworks);
  if (!dir) {
    say(log, "FAIL: cannot open Frameworks folder");
    return 0;
  }
  char *names[64];
  int count = 0;
  struct dirent *entry;
  while ((entry = readdir(dir)) && count < 64) {
    const size_t n = strlen(entry->d_name);
    if (n > 6 && strcmp(entry->d_name + n - 6, ".dylib") == 0 &&
        strncmp(entry->d_name, "librex", 6) == 0) {
      names[count++] = strdup(entry->d_name);
    }
  }
  closedir(dir);
  qsort(names, (size_t)count, sizeof names[0], name_order);
  if (count == 0) {
    say(log, "FAIL: no ReXGlue libraries in the app (this IPA was built without them)");
    return 0;
  }
  say(log, "  %d ReXGlue libraries in the app's Frameworks folder", count);

  int loaded = 0, modules = 0;
  const char *identity_seen = NULL;
  for (int i = 0; i < count; i++) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_frameworks, names[i]);
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
      const char *error = dlerror();
      say(log, "  FAIL load %s: %.400s", names[i], error ? error : "unknown error");
      continue;
    }
    loaded++;
    typedef const char *(*IdentityFn)(void);
    IdentityFn identity = (IdentityFn)dlsym(handle, "ReXBuildIdentity");
    if (identity) {
      modules++;
      identity_seen = identity();
    }
  }
  say(log, "  Loaded %d of %d libraries; %d are runtime modules (build %s)", loaded, count, modules,
      identity_seen ? identity_seen : "n/a");
  for (int i = 0; i < count; i++) free(names[i]);
  const int ok = loaded == count && modules > 0;
  say(log, "%s: ReXGlue runtime libraries load inside the app", ok ? "PASS" : "FAIL");
  return ok;
}

// ---------------------------------------------------------------------------------------------
// MoltenVK: minimal Vulkan declarations (stable C ABI), so the shell needs no Vulkan headers.
// ---------------------------------------------------------------------------------------------
typedef void (*VkVoidFn)(void);
typedef VkVoidFn (*GetInstanceProcAddrFn)(void *instance, const char *name);
typedef struct {
  int32_t sType;  // VK_STRUCTURE_TYPE_APPLICATION_INFO = 0
  const void *pNext;
  const char *pApplicationName;
  uint32_t applicationVersion;
  const char *pEngineName;
  uint32_t engineVersion;
  uint32_t apiVersion;
} VkAppInfo;
typedef struct {
  int32_t sType;  // VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO = 1
  const void *pNext;
  uint32_t flags;
  const VkAppInfo *pApplicationInfo;
  uint32_t enabledLayerCount;
  const char *const *ppEnabledLayerNames;
  uint32_t enabledExtensionCount;
  const char *const *ppEnabledExtensionNames;
} VkInstanceInfo;
typedef int32_t (*CreateInstanceFn)(const VkInstanceInfo *, const void *, void **);
typedef void (*DestroyInstanceFn)(void *, const void *);
typedef int32_t (*EnumeratePhysicalDevicesFn)(void *, uint32_t *, void **);
typedef void (*GetPhysicalDevicePropertiesFn)(void *, void *);
typedef int32_t (*EnumerateExtensionsFn)(const char *, uint32_t *, void *);

static void *load_moltenvk(selftest_log_fn log) {
  static const char *candidates[] = {"libMoltenVK.dylib", "MoltenVK.framework/MoltenVK"};
  for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", g_frameworks, candidates[i]);
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle) {
      say(log, "  Loaded %s", candidates[i]);
      return handle;
    }
    const char *error = dlerror();
    say(log, "  %s: %.300s", candidates[i], error ? error : "not loaded");
  }
  return NULL;
}

int selftest_moltenvk(selftest_log_fn log) {
  if (!g_frameworks[0]) {
    say(log, "FAIL: Frameworks folder not set");
    return 0;
  }
  void *molten = load_moltenvk(log);
  if (!molten) {
    say(log, "FAIL: MoltenVK (Vulkan on Metal) could not be loaded");
    return 0;
  }
  GetInstanceProcAddrFn gipa = (GetInstanceProcAddrFn)dlsym(molten, "vkGetInstanceProcAddr");
  if (!gipa) {
    say(log, "FAIL: vkGetInstanceProcAddr not exported");
    return 0;
  }
  EnumerateExtensionsFn enumerate_ext =
      (EnumerateExtensionsFn)gipa(NULL, "vkEnumerateInstanceExtensionProperties");
  uint32_t extension_count = 0;
  if (enumerate_ext) enumerate_ext(NULL, &extension_count, NULL);
  say(log, "  Instance extensions available: %u", extension_count);

  say(log, "  Looking up vkCreateInstance");
  CreateInstanceFn create = (CreateInstanceFn)gipa(NULL, "vkCreateInstance");
  if (!create) {
    say(log, "FAIL: vkCreateInstance not found");
    return 0;
  }
  VkAppInfo app = {0, NULL, "NFSMWShell probe", 1, "none", 1, (1u << 22) | (2u << 12)};  // 1.2
  VkInstanceInfo info = {1, NULL, 0, &app, 0, NULL, 0, NULL};
  void *instance = NULL;
  say(log, "  Calling vkCreateInstance (Vulkan 1.2, no layers, no extensions)");
  int32_t result = create(&info, NULL, &instance);
  say(log, "  vkCreateInstance returned %d", result);
  if (result != 0 || !instance) {
    say(log, "FAIL: vkCreateInstance returned %d", result);
    return 0;
  }
  EnumeratePhysicalDevicesFn enumerate =
      (EnumeratePhysicalDevicesFn)gipa(instance, "vkEnumeratePhysicalDevices");
  GetPhysicalDevicePropertiesFn properties =
      (GetPhysicalDevicePropertiesFn)gipa(instance, "vkGetPhysicalDeviceProperties");
  DestroyInstanceFn destroy = (DestroyInstanceFn)gipa(instance, "vkDestroyInstance");
  uint32_t device_count = 0;
  void *devices[4] = {0};
  if (enumerate) {
    say(log, "  Enumerating physical devices");
    enumerate(instance, &device_count, NULL);
    if (device_count > 4) device_count = 4;
    enumerate(instance, &device_count, devices);
  }
  int ok = device_count > 0 && properties != NULL;
  for (uint32_t i = 0; ok && i < device_count; i++) {
    // VkPhysicalDeviceProperties: apiVersion, driverVersion, vendorID, deviceID, deviceType, then
    // deviceName[256] at offset 20. The buffer is larger than the whole structure.
    _Alignas(8) unsigned char buffer[4096];
    memset(buffer, 0, sizeof buffer);
    properties(devices[i], buffer);
    uint32_t api = 0;
    memcpy(&api, buffer, sizeof api);
    char name[257];
    memcpy(name, buffer + 20, 256);
    name[256] = 0;
    say(log, "  Vulkan device %u: %s, Vulkan %u.%u.%u", i, name, api >> 22, (api >> 12) & 0x3FF,
        api & 0xFFF);
  }
  if (destroy) destroy(instance, NULL);
  say(log, "%s: Vulkan on Metal through MoltenVK (%u device%s)", ok ? "PASS" : "FAIL", device_count,
      device_count == 1 ? "" : "s");
  return ok;
}
