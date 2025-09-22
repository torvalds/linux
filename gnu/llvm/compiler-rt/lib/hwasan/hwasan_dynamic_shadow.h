//===-- hwasan_dynamic_shadow.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is a part of HWAddressSanitizer. It reserves dynamic shadow memory
/// region.
///
//===----------------------------------------------------------------------===//

#ifndef HWASAN_PREMAP_SHADOW_H
#define HWASAN_PREMAP_SHADOW_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __hwasan {

uptr FindDynamicShadowStart(uptr shadow_size_bytes);
void InitShadowGOT();

}  // namespace __hwasan

#endif  // HWASAN_PREMAP_SHADOW_H
