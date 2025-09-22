//===-- sanitizer_symbolizer_libbacktrace.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is shared between AddressSanitizer and ThreadSanitizer
// run-time libraries.
// Libbacktrace implementation of symbolizer parts.
//===----------------------------------------------------------------------===//

#include "sanitizer_symbolizer_libbacktrace.h"

#include "sanitizer_internal_defs.h"
#include "sanitizer_platform.h"
#include "sanitizer_symbolizer.h"

#if SANITIZER_LIBBACKTRACE
# include "backtrace-supported.h"
# if SANITIZER_POSIX && BACKTRACE_SUPPORTED && !BACKTRACE_USES_MALLOC
#  include "backtrace.h"
#  if SANITIZER_CP_DEMANGLE
#   undef ARRAY_SIZE
#   include "demangle.h"
#  endif
# else
#  define SANITIZER_LIBBACKTRACE 0
# endif
#endif

namespace __sanitizer {

static char *DemangleAlloc(const char *name, bool always_alloc);

#if SANITIZER_LIBBACKTRACE

namespace {

# if SANITIZER_CP_DEMANGLE
struct CplusV3DemangleData {
  char *buf;
  uptr size, allocated;
};

extern "C" {
static void CplusV3DemangleCallback(const char *s, size_t l, void *vdata) {
  CplusV3DemangleData *data = (CplusV3DemangleData *)vdata;
  uptr needed = data->size + l + 1;
  if (needed > data->allocated) {
    data->allocated *= 2;
    if (needed > data->allocated)
      data->allocated = needed;
    char *buf = (char *)InternalAlloc(data->allocated);
    if (data->buf) {
      internal_memcpy(buf, data->buf, data->size);
      InternalFree(data->buf);
    }
    data->buf = buf;
  }
  internal_memcpy(data->buf + data->size, s, l);
  data->buf[data->size + l] = '\0';
  data->size += l;
}
}  // extern "C"

char *CplusV3Demangle(const char *name) {
  CplusV3DemangleData data;
  data.buf = 0;
  data.size = 0;
  data.allocated = 0;
  if (cplus_demangle_v3_callback(name, DMGL_PARAMS | DMGL_ANSI,
                                 CplusV3DemangleCallback, &data)) {
    if (data.size + 64 > data.allocated)
      return data.buf;
    char *buf = internal_strdup(data.buf);
    InternalFree(data.buf);
    return buf;
  }
  if (data.buf)
    InternalFree(data.buf);
  return 0;
}
# endif  // SANITIZER_CP_DEMANGLE

struct SymbolizeCodeCallbackArg {
  SymbolizedStack *first;
  SymbolizedStack *last;
  uptr frames_symbolized;

  AddressInfo *get_new_frame(uintptr_t addr) {
    CHECK(last);
    if (frames_symbolized > 0) {
      SymbolizedStack *cur = SymbolizedStack::New(addr);
      AddressInfo *info = &cur->info;
      info->FillModuleInfo(first->info.module, first->info.module_offset,
                           first->info.module_arch);
      last->next = cur;
      last = cur;
    }
    CHECK_EQ(addr, first->info.address);
    CHECK_EQ(addr, last->info.address);
    return &last->info;
  }
};

extern "C" {
static int SymbolizeCodePCInfoCallback(void *vdata, uintptr_t addr,
                                       const char *filename, int lineno,
                                       const char *function) {
  SymbolizeCodeCallbackArg *cdata = (SymbolizeCodeCallbackArg *)vdata;
  if (function) {
    AddressInfo *info = cdata->get_new_frame(addr);
    info->function = DemangleAlloc(function, /*always_alloc*/ true);
    if (filename)
      info->file = internal_strdup(filename);
    info->line = lineno;
    cdata->frames_symbolized++;
  }
  return 0;
}

static void SymbolizeCodeCallback(void *vdata, uintptr_t addr,
                                  const char *symname, uintptr_t, uintptr_t) {
  SymbolizeCodeCallbackArg *cdata = (SymbolizeCodeCallbackArg *)vdata;
  if (symname) {
    AddressInfo *info = cdata->get_new_frame(addr);
    info->function = DemangleAlloc(symname, /*always_alloc*/ true);
    cdata->frames_symbolized++;
  }
}

static void SymbolizeDataCallback(void *vdata, uintptr_t, const char *symname,
                                  uintptr_t symval, uintptr_t symsize) {
  DataInfo *info = (DataInfo *)vdata;
  if (symname && symval) {
    info->name = DemangleAlloc(symname, /*always_alloc*/ true);
    info->start = symval;
    info->size = symsize;
  }
}

static void ErrorCallback(void *, const char *, int) {}
}  // extern "C"

}  // namespace

LibbacktraceSymbolizer *LibbacktraceSymbolizer::get(LowLevelAllocator *alloc) {
  // State created in backtrace_create_state is leaked.
  void *state = (void *)(backtrace_create_state("/proc/self/exe", 0,
                                                ErrorCallback, NULL));
  if (!state)
    return 0;
  return new(*alloc) LibbacktraceSymbolizer(state);
}

bool LibbacktraceSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  SymbolizeCodeCallbackArg data;
  data.first = stack;
  data.last = stack;
  data.frames_symbolized = 0;
  backtrace_pcinfo((backtrace_state *)state_, addr, SymbolizeCodePCInfoCallback,
                   ErrorCallback, &data);
  if (data.frames_symbolized > 0)
    return true;
  backtrace_syminfo((backtrace_state *)state_, addr, SymbolizeCodeCallback,
                    ErrorCallback, &data);
  return (data.frames_symbolized > 0);
}

bool LibbacktraceSymbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  backtrace_syminfo((backtrace_state *)state_, addr, SymbolizeDataCallback,
                    ErrorCallback, info);
  return true;
}

#else  // SANITIZER_LIBBACKTRACE

LibbacktraceSymbolizer *LibbacktraceSymbolizer::get(LowLevelAllocator *alloc) {
  return 0;
}

bool LibbacktraceSymbolizer::SymbolizePC(uptr addr, SymbolizedStack *stack) {
  (void)state_;
  return false;
}

bool LibbacktraceSymbolizer::SymbolizeData(uptr addr, DataInfo *info) {
  return false;
}

#endif  // SANITIZER_LIBBACKTRACE

static char *DemangleAlloc(const char *name, bool always_alloc) {
#if SANITIZER_LIBBACKTRACE && SANITIZER_CP_DEMANGLE
  if (char *demangled = CplusV3Demangle(name))
    return demangled;
#endif
  if (always_alloc)
    return internal_strdup(name);
  return nullptr;
}

const char *LibbacktraceSymbolizer::Demangle(const char *name) {
  return DemangleAlloc(name, /*always_alloc*/ false);
}

}  // namespace __sanitizer
