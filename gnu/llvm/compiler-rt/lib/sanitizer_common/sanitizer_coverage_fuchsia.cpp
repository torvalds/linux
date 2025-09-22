//===-- sanitizer_coverage_fuchsia.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Sanitizer Coverage Controller for Trace PC Guard, Fuchsia-specific version.
//
// This Fuchsia-specific implementation uses the same basic scheme and the
// same simple '.sancov' file format as the generic implementation.  The
// difference is that we just produce a single blob of output for the whole
// program, not a separate one per DSO.  We do not sort the PC table and do
// not prune the zeros, so the resulting file is always as large as it
// would be to report 100% coverage.  Implicit tracing information about
// the address ranges of DSOs allows offline tools to split the one big
// blob into separate files that the 'sancov' tool can understand.
//
// Unlike the traditional implementation that uses an atexit hook to write
// out data files at the end, the results on Fuchsia do not go into a file
// per se.  The 'coverage_dir' option is ignored.  Instead, they are stored
// directly into a shared memory object (a Zircon VMO).  At exit, that VMO
// is handed over to a system service that's responsible for getting the
// data out to somewhere that it can be fed into the sancov tool (where and
// how is not our problem).

#include "sanitizer_platform.h"
#if SANITIZER_FUCHSIA
#include <zircon/process.h>
#include <zircon/sanitizer.h>
#include <zircon/syscalls.h>

#include "sanitizer_atomic.h"
#include "sanitizer_common.h"
#include "sanitizer_interface_internal.h"
#include "sanitizer_internal_defs.h"
#  include "sanitizer_symbolizer_markup_constants.h"

using namespace __sanitizer;

namespace __sancov {
namespace {

// TODO(mcgrathr): Move the constant into a header shared with other impls.
constexpr u64 Magic64 = 0xC0BFFFFFFFFFFF64ULL;
static_assert(SANITIZER_WORDSIZE == 64, "Fuchsia is always LP64");

constexpr const char kSancovSinkName[] = "sancov";

// Collects trace-pc guard coverage.
// This class relies on zero-initialization.
class TracePcGuardController final {
 public:
  constexpr TracePcGuardController() {}

  // For each PC location being tracked, there is a u32 reserved in global
  // data called the "guard".  At startup, we assign each guard slot a
  // unique index into the big results array.  Later during runtime, the
  // first call to TracePcGuard (below) will store the corresponding PC at
  // that index in the array.  (Each later call with the same guard slot is
  // presumed to be from the same PC.)  Then it clears the guard slot back
  // to zero, which tells the compiler not to bother calling in again.  At
  // the end of the run, we have a big array where each element is either
  // zero or is a tracked PC location that was hit in the trace.

  // This is called from global constructors.  Each translation unit has a
  // contiguous array of guard slots, and a constructor that calls here
  // with the bounds of its array.  Those constructors are allowed to call
  // here more than once for the same array.  Usually all of these
  // constructors run in the initial thread, but it's possible that a
  // dlopen call on a secondary thread will run constructors that get here.
  void InitTracePcGuard(u32 *start, u32 *end) {
    if (end > start && *start == 0 && common_flags()->coverage) {
      // Complete the setup before filling in any guards with indices.
      // This avoids the possibility of code called from Setup reentering
      // TracePcGuard.
      u32 idx = Setup(end - start);
      for (u32 *p = start; p < end; ++p) {
        *p = idx++;
      }
    }
  }

  void TracePcGuard(u32 *guard, uptr pc) {
    atomic_uint32_t *guard_ptr = reinterpret_cast<atomic_uint32_t *>(guard);
    u32 idx = atomic_exchange(guard_ptr, 0, memory_order_relaxed);
    if (idx > 0)
      array_[idx] = pc;
  }

  void Dump() {
    Lock locked(&setup_lock_);
    if (array_) {
      CHECK_NE(vmo_, ZX_HANDLE_INVALID);

      // Publish the VMO to the system, where it can be collected and
      // analyzed after this process exits.  This always consumes the VMO
      // handle.  Any failure is just logged and not indicated to us.
      __sanitizer_publish_data(kSancovSinkName, vmo_);
      vmo_ = ZX_HANDLE_INVALID;

      // This will route to __sanitizer_log_write, which will ensure that
      // information about shared libraries is written out.  This message
      // uses the `dumpfile` symbolizer markup element to highlight the
      // dump.  See the explanation for this in:
      // https://fuchsia.googlesource.com/zircon/+/master/docs/symbolizer_markup.md
      Printf("SanitizerCoverage: " FORMAT_DUMPFILE " with up to %u PCs\n",
             kSancovSinkName, vmo_name_, next_index_ - 1);
    }
  }

 private:
  // We map in the largest possible view into the VMO: one word
  // for every possible 32-bit index value.  This avoids the need
  // to change the mapping when increasing the size of the VMO.
  // We can always spare the 32G of address space.
  static constexpr size_t MappingSize = sizeof(uptr) << 32;

  Mutex setup_lock_;
  uptr *array_ = nullptr;
  u32 next_index_ = 0;
  zx_handle_t vmo_ = {};
  char vmo_name_[ZX_MAX_NAME_LEN] = {};

  size_t DataSize() const { return next_index_ * sizeof(uintptr_t); }

  u32 Setup(u32 num_guards) {
    Lock locked(&setup_lock_);
    DCHECK(common_flags()->coverage);

    if (next_index_ == 0) {
      CHECK_EQ(vmo_, ZX_HANDLE_INVALID);
      CHECK_EQ(array_, nullptr);

      // The first sample goes at [1] to reserve [0] for the magic number.
      next_index_ = 1 + num_guards;

      zx_status_t status = _zx_vmo_create(DataSize(), ZX_VMO_RESIZABLE, &vmo_);
      CHECK_EQ(status, ZX_OK);

      // Give the VMO a name including our process KOID so it's easy to spot.
      internal_snprintf(vmo_name_, sizeof(vmo_name_), "%s.%zu", kSancovSinkName,
                        internal_getpid());
      _zx_object_set_property(vmo_, ZX_PROP_NAME, vmo_name_,
                              internal_strlen(vmo_name_));
      uint64_t size = DataSize();
      status = _zx_object_set_property(vmo_, ZX_PROP_VMO_CONTENT_SIZE, &size,
                                       sizeof(size));
      CHECK_EQ(status, ZX_OK);

      // Map the largest possible view we might need into the VMO.  Later
      // we might need to increase the VMO's size before we can use larger
      // indices, but we'll never move the mapping address so we don't have
      // any multi-thread synchronization issues with that.
      uintptr_t mapping;
      status =
          _zx_vmar_map(_zx_vmar_root_self(), ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                       0, vmo_, 0, MappingSize, &mapping);
      CHECK_EQ(status, ZX_OK);

      // Hereafter other threads are free to start storing into
      // elements [1, next_index_) of the big array.
      array_ = reinterpret_cast<uptr *>(mapping);

      // Store the magic number.
      // Hereafter, the VMO serves as the contents of the '.sancov' file.
      array_[0] = Magic64;

      return 1;
    } else {
      // The VMO is already mapped in, but it's not big enough to use the
      // new indices.  So increase the size to cover the new maximum index.

      CHECK_NE(vmo_, ZX_HANDLE_INVALID);
      CHECK_NE(array_, nullptr);

      uint32_t first_index = next_index_;
      next_index_ += num_guards;

      zx_status_t status = _zx_vmo_set_size(vmo_, DataSize());
      CHECK_EQ(status, ZX_OK);
      uint64_t size = DataSize();
      status = _zx_object_set_property(vmo_, ZX_PROP_VMO_CONTENT_SIZE, &size,
                                       sizeof(size));
      CHECK_EQ(status, ZX_OK);

      return first_index;
    }
  }
};

static TracePcGuardController pc_guard_controller;

}  // namespace
}  // namespace __sancov

namespace __sanitizer {
void InitializeCoverage(bool enabled, const char *dir) {
  CHECK_EQ(enabled, common_flags()->coverage);
  CHECK_EQ(dir, common_flags()->coverage_dir);

  static bool coverage_enabled = false;
  if (!coverage_enabled) {
    coverage_enabled = enabled;
    Atexit(__sanitizer_cov_dump);
    AddDieCallback(__sanitizer_cov_dump);
  }
}
}  // namespace __sanitizer

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_coverage(const uptr *pcs,
                                                             uptr len) {
  UNIMPLEMENTED();
}

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard, u32 *guard) {
  if (!*guard)
    return;
  __sancov::pc_guard_controller.TracePcGuard(guard, GET_CALLER_PC() - 1);
}

SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_guard_init,
                             u32 *start, u32 *end) {
  if (start == end || *start)
    return;
  __sancov::pc_guard_controller.InitTracePcGuard(start, end);
}

SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_dump_trace_pc_guard_coverage() {
  __sancov::pc_guard_controller.Dump();
}
SANITIZER_INTERFACE_ATTRIBUTE void __sanitizer_cov_dump() {
  __sanitizer_dump_trace_pc_guard_coverage();
}
// Default empty implementations (weak). Users should redefine them.
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp1, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp2, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_cmp8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp1, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp2, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_const_cmp8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_switch, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_div4, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_div8, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_gep, void) {}
SANITIZER_INTERFACE_WEAK_DEF(void, __sanitizer_cov_trace_pc_indir, void) {}
}  // extern "C"

#endif  // !SANITIZER_FUCHSIA
