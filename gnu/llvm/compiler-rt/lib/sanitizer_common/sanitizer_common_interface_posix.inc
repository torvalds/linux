//===-- sanitizer_common_interface_posix.inc ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Sanitizer Common interface list only available for Posix systems.
//===----------------------------------------------------------------------===//
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_code)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_data)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_frame)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_demangle)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_flush)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_set_demangle)
INTERFACE_WEAK_FUNCTION(__sanitizer_symbolize_set_inline_frames)
