#ifndef LLDB_TEST_DYLIB_H
#define LLDB_TEST_DYLIB_H

#include <stdio.h>

#ifdef _WIN32
#include <Windows.h>

#define dylib_get_symbol(handle, name) GetProcAddress((HMODULE)handle, name)
#define dylib_close(handle) (!FreeLibrary((HMODULE)handle))
#else
#include <dlfcn.h>

#define dylib_get_symbol(handle, name) dlsym(handle, name)
#define dylib_close(handle) dlclose(handle)
#endif


inline void *dylib_open(const char *name) {
  char dylib_prefix[] =
#ifdef _WIN32
    "";
#else
    "lib";
#endif
  char dylib_suffix[] =
#ifdef _WIN32
    ".dll";
#elif defined(__APPLE__)
    ".dylib";
#else
    ".so";
#endif
  char fullname[1024];
  snprintf(fullname, sizeof(fullname), "%s%s%s", dylib_prefix, name, dylib_suffix);
#ifdef _WIN32
  return LoadLibraryA(fullname);
#else
  return dlopen(fullname, RTLD_NOW);
#endif
}

inline const char *dylib_last_error() {
#ifndef _WIN32
  return dlerror();
#else
  DWORD err = GetLastError();
  char *msg;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
      NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (char *)&msg, 0, NULL);
  return msg;
#endif
}

#endif
