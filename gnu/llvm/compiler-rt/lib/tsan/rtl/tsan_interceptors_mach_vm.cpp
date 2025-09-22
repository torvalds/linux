//===-- tsan_interceptors_mach_vm.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Interceptors for mach_vm_* user space memory routines on Darwin.
//===----------------------------------------------------------------------===//

#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_platform.h"

#include <mach/mach.h>

namespace __tsan {

static bool intersects_with_shadow(mach_vm_address_t address,
                                   mach_vm_size_t size, int flags) {
  // VM_FLAGS_FIXED is 0x0, so we have to test for VM_FLAGS_ANYWHERE.
  if (flags & VM_FLAGS_ANYWHERE) return false;
  return !IsAppMem(address) || !IsAppMem(address + size - 1);
}

TSAN_INTERCEPTOR(kern_return_t, mach_vm_allocate, vm_map_t target,
                 mach_vm_address_t *address, mach_vm_size_t size, int flags) {
  SCOPED_TSAN_INTERCEPTOR(mach_vm_allocate, target, address, size, flags);
  if (target != mach_task_self())
    return REAL(mach_vm_allocate)(target, address, size, flags);
  if (address && intersects_with_shadow(*address, size, flags))
    return KERN_NO_SPACE;
  kern_return_t kr = REAL(mach_vm_allocate)(target, address, size, flags);
  if (kr == KERN_SUCCESS)
    MemoryRangeImitateWriteOrResetRange(thr, pc, *address, size);
  return kr;
}

TSAN_INTERCEPTOR(kern_return_t, mach_vm_deallocate, vm_map_t target,
                 mach_vm_address_t address, mach_vm_size_t size) {
  SCOPED_TSAN_INTERCEPTOR(mach_vm_deallocate, target, address, size);
  if (target != mach_task_self())
    return REAL(mach_vm_deallocate)(target, address, size);
  kern_return_t kr = REAL(mach_vm_deallocate)(target, address, size);
  if (kr == KERN_SUCCESS && address)
    UnmapShadow(thr, address, size);
  return kr;
}

}  // namespace __tsan
