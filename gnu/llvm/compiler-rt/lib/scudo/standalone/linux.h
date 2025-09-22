//===-- linux.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_LINUX_H_
#define SCUDO_LINUX_H_

#include "platform.h"

#if SCUDO_LINUX

namespace scudo {

// MapPlatformData is unused on Linux, define it as a minimally sized structure.
struct MapPlatformData {};

} // namespace scudo

#endif // SCUDO_LINUX

#endif // SCUDO_LINUX_H_
