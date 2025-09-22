//===-- sync_fetch_and_add_4.S - ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the __sync_fetch_and_add_4 function for the ARM
// architecture.
//
//===----------------------------------------------------------------------===//

#include "sync-ops.h"

// "adds" is 2 bytes shorter than "add".
#define add_4(rD, rN, rM)  add rD, rN, rM

SYNC_OP_4(add_4)

NO_EXEC_STACK_DIRECTIVE

