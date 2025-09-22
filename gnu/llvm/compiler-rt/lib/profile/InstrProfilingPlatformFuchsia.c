/*===- InstrProfilingPlatformFuchsia.c - Profile data Fuchsia platform ----===*\
|*
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
|* See https://llvm.org/LICENSE.txt for license information.
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
|*
\*===----------------------------------------------------------------------===*/
/*
 * This file implements the profiling runtime for Fuchsia and defines the
 * shared profile runtime interface. Each module (executable or DSO) statically
 * links in the whole profile runtime to satisfy the calls from its
 * instrumented code. Several modules in the same program might be separately
 * compiled and even use different versions of the instrumentation ABI and data
 * format. All they share in common is the VMO and the offset, which live in
 * exported globals so that exactly one definition will be shared across all
 * modules. Each module has its own independent runtime that registers its own
 * atexit hook to append its own data into the shared VMO which is published
 * via the data sink hook provided by Fuchsia's dynamic linker.
 */

#if defined(__Fuchsia__)

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>

#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "InstrProfiling.h"
#include "InstrProfilingInternal.h"
#include "InstrProfilingUtil.h"

/* This variable is an external reference to symbol defined by the compiler. */
COMPILER_RT_VISIBILITY extern intptr_t INSTR_PROF_PROFILE_COUNTER_BIAS_VAR;

COMPILER_RT_VISIBILITY unsigned lprofProfileDumped(void) {
  return 1;
}
COMPILER_RT_VISIBILITY void lprofSetProfileDumped(unsigned Value) {}

static const char ProfileSinkName[] = "llvm-profile";

static inline void lprofWrite(const char *fmt, ...) {
  char s[256];

  va_list ap;
  va_start(ap, fmt);
  int ret = vsnprintf(s, sizeof(s), fmt, ap);
  va_end(ap);

  __sanitizer_log_write(s, ret);
}

struct lprofVMOWriterCtx {
  /* VMO that contains the profile data for this module. */
  zx_handle_t Vmo;
  /* Current offset within the VMO where data should be written next. */
  uint64_t Offset;
};

static uint32_t lprofVMOWriter(ProfDataWriter *This, ProfDataIOVec *IOVecs,
                               uint32_t NumIOVecs) {
  struct lprofVMOWriterCtx *Ctx = (struct lprofVMOWriterCtx *)This->WriterCtx;

  /* Compute the total length of data to be written. */
  size_t Length = 0;
  for (uint32_t I = 0; I < NumIOVecs; I++)
    Length += IOVecs[I].ElmSize * IOVecs[I].NumElm;

  /* Resize the VMO to ensure there's sufficient space for the data. */
  zx_status_t Status = _zx_vmo_set_size(Ctx->Vmo, Ctx->Offset + Length);
  if (Status != ZX_OK)
    return -1;

  /* Copy the data into VMO. */
  for (uint32_t I = 0; I < NumIOVecs; I++) {
    size_t Length = IOVecs[I].ElmSize * IOVecs[I].NumElm;
    if (IOVecs[I].Data) {
      Status = _zx_vmo_write(Ctx->Vmo, IOVecs[I].Data, Ctx->Offset, Length);
      if (Status != ZX_OK)
        return -1;
    } else if (IOVecs[I].UseZeroPadding) {
      /* Resizing the VMO should zero fill. */
    }
    Ctx->Offset += Length;
  }

  /* Record the profile size as a property of the VMO. */
  _zx_object_set_property(Ctx->Vmo, ZX_PROP_VMO_CONTENT_SIZE, &Ctx->Offset,
                          sizeof(Ctx->Offset));

  return 0;
}

static void initVMOWriter(ProfDataWriter *This, struct lprofVMOWriterCtx *Ctx) {
  This->Write = lprofVMOWriter;
  This->WriterCtx = Ctx;
}

/* This method is invoked by the runtime initialization hook
 * InstrProfilingRuntime.o if it is linked in. */
COMPILER_RT_VISIBILITY
void __llvm_profile_initialize(void) {
  /* Check if there is llvm/runtime version mismatch. */
  if (GET_VERSION(__llvm_profile_get_version()) != INSTR_PROF_RAW_VERSION) {
    lprofWrite("LLVM Profile: runtime and instrumentation version mismatch: "
               "expected %d, but got %d\n",
               INSTR_PROF_RAW_VERSION,
               (int)GET_VERSION(__llvm_profile_get_version()));
    return;
  }

  const __llvm_profile_data *DataBegin = __llvm_profile_begin_data();
  const __llvm_profile_data *DataEnd = __llvm_profile_end_data();
  const char *CountersBegin = __llvm_profile_begin_counters();
  const char *CountersEnd = __llvm_profile_end_counters();
  const uint64_t DataSize = __llvm_profile_get_data_size(DataBegin, DataEnd);
  const uint64_t CountersOffset =
      sizeof(__llvm_profile_header) + __llvm_write_binary_ids(NULL) + DataSize;
  uint64_t CountersSize =
      __llvm_profile_get_counters_size(CountersBegin, CountersEnd);

  /* Don't publish a VMO if there are no counters. */
  if (!CountersSize)
    return;

  zx_status_t Status;

  /* Create a VMO to hold the profile data. */
  zx_handle_t Vmo = ZX_HANDLE_INVALID;
  Status = _zx_vmo_create(0, ZX_VMO_RESIZABLE, &Vmo);
  if (Status != ZX_OK) {
    lprofWrite("LLVM Profile: cannot create VMO: %s\n",
               _zx_status_get_string(Status));
    return;
  }

  /* Give the VMO a name that includes the module signature. */
  char VmoName[ZX_MAX_NAME_LEN];
  snprintf(VmoName, sizeof(VmoName), "%" PRIu64 ".profraw",
           lprofGetLoadModuleSignature());
  _zx_object_set_property(Vmo, ZX_PROP_NAME, VmoName, strlen(VmoName));

  /* Write the profile data into the mapped region. */
  ProfDataWriter VMOWriter;
  struct lprofVMOWriterCtx Ctx = {.Vmo = Vmo, .Offset = 0};
  initVMOWriter(&VMOWriter, &Ctx);
  if (lprofWriteData(&VMOWriter, 0, 0) != 0) {
    lprofWrite("LLVM Profile: failed to write data\n");
    _zx_handle_close(Vmo);
    return;
  }

  uint64_t Len = 0;
  Status = _zx_vmo_get_size(Vmo, &Len);
  if (Status != ZX_OK) {
    lprofWrite("LLVM Profile: failed to get the VMO size: %s\n",
               _zx_status_get_string(Status));
    _zx_handle_close(Vmo);
    return;
  }

  uintptr_t Mapping;
  Status =
      _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, 0,
                   Vmo, 0, Len, &Mapping);
  if (Status != ZX_OK) {
    lprofWrite("LLVM Profile: failed to map the VMO: %s\n",
               _zx_status_get_string(Status));
    _zx_handle_close(Vmo);
    return;
  }

  /* Publish the VMO which contains profile data to the system. Note that this
   * also consumes the VMO handle. */
  __sanitizer_publish_data(ProfileSinkName, Vmo);

  /* Update the profile fields based on the current mapping. */
  INSTR_PROF_PROFILE_COUNTER_BIAS_VAR =
      (intptr_t)Mapping - (uintptr_t)CountersBegin + CountersOffset;

  /* Return the memory allocated for counters to OS. */
  lprofReleaseMemoryPagesToOS((uintptr_t)CountersBegin, (uintptr_t)CountersEnd);
}

#endif
