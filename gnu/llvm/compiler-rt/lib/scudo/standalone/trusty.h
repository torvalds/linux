//===-- trusty.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_TRUSTY_H_
#define SCUDO_TRUSTY_H_

#include "platform.h"

#if SCUDO_TRUSTY

namespace scudo {
// MapPlatformData is unused on Trusty, define it as a minimially sized
// structure.
struct MapPlatformData {};
} // namespace scudo

#endif // SCUDO_TRUSTY

#endif // SCUDO_TRUSTY_H_
