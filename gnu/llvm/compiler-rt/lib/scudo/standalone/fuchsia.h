//===-- fuchsia.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_FUCHSIA_H_
#define SCUDO_FUCHSIA_H_

#include "platform.h"

#if SCUDO_FUCHSIA

#include <stdint.h>
#include <zircon/types.h>

namespace scudo {

struct MapPlatformData {
  zx_handle_t Vmar;
  zx_handle_t Vmo;
  uintptr_t VmarBase;
  uint64_t VmoSize;
};

} // namespace scudo

#endif // SCUDO_FUCHSIA

#endif // SCUDO_FUCHSIA_H_
