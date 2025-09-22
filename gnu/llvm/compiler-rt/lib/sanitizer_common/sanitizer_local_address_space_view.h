//===-- sanitizer_local_address_space_view.h --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `LocalAddressSpaceView` provides the local (i.e. target and current address
// space are the same) implementation of the `AddressSpaceView` interface which
// provides a simple interface to load memory from another process (i.e.
// out-of-process)
//
// The `AddressSpaceView` interface requires that the type can be used as a
// template parameter to objects that wish to be able to operate in an
// out-of-process manner. In normal usage, objects are in-process and are thus
// instantiated with the `LocalAddressSpaceView` type. This type is used to
// load any pointers in instance methods. This implementation is effectively
// a no-op. When an object is to be used in an out-of-process manner it is
// instantiated with the `RemoteAddressSpaceView` type.
//
// By making `AddressSpaceView` a template parameter of an object, it can
// change its implementation at compile time which has no run time overhead.
// This also allows unifying in-process and out-of-process code which avoids
// code duplication.
//
//===----------------------------------------------------------------------===//
#ifndef SANITIZER_LOCAL_ADDRES_SPACE_VIEW_H
#define SANITIZER_LOCAL_ADDRES_SPACE_VIEW_H

namespace __sanitizer {
struct LocalAddressSpaceView {
  // Load memory `sizeof(T) * num_elements` bytes of memory from the target
  // process (always local for this implementation) starting at address
  // `target_address`. The local copy of this memory is returned as a pointer.
  // The caller should not write to this memory. The behaviour when doing so is
  // undefined. Callers should use `LoadWritable()` to get access to memory
  // that is writable.
  //
  // The lifetime of loaded memory is implementation defined.
  template <typename T>
  static const T *Load(const T *target_address, uptr num_elements = 1) {
    // The target address space is the local address space so
    // nothing needs to be copied. Just return the pointer.
    return target_address;
  }

  // Load memory `sizeof(T) * num_elements` bytes of memory from the target
  // process (always local for this implementation) starting at address
  // `target_address`. The local copy of this memory is returned as a pointer.
  // The memory returned may be written to.
  //
  // Writes made to the returned memory will be visible in the memory returned
  // by subsequent `Load()` or `LoadWritable()` calls provided the
  // `target_address` parameter is the same. It is not guaranteed that the
  // memory returned by previous calls to `Load()` will contain any performed
  // writes.  If two or more overlapping regions of memory are loaded via
  // separate calls to `LoadWritable()`, it is implementation defined whether
  // writes made to the region returned by one call are visible in the regions
  // returned by other calls.
  //
  // Given the above it is recommended to load the largest possible object
  // that requires modification (e.g. a class) rather than individual fields
  // from a class to avoid issues with overlapping writable regions.
  //
  // The lifetime of loaded memory is implementation defined.
  template <typename T>
  static T *LoadWritable(T *target_address, uptr num_elements = 1) {
    // The target address space is the local address space so
    // nothing needs to be copied. Just return the pointer.
    return target_address;
  }
};
}  // namespace __sanitizer

#endif
