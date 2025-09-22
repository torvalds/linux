//===-- asan_activation.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan activation/deactivation logic.
//===----------------------------------------------------------------------===//

#ifndef ASAN_ACTIVATION_H
#define ASAN_ACTIVATION_H

namespace __asan {
void AsanDeactivate();
void AsanActivate();
}  // namespace __asan

#endif  // ASAN_ACTIVATION_H
