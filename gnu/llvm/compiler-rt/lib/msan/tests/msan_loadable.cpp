//===-- msan_loadable.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of MemorySanitizer.
//
// MemorySanitizer unit tests.
//===----------------------------------------------------------------------===//

#include "msan/msan_interface_internal.h"
#include <stdlib.h>

static void *dso_global;

// No name mangling.
extern "C" {

void **get_dso_global() {
  return &dso_global;
}

}
