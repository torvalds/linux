//===-- StoppointCallbackContext.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/StoppointCallbackContext.h"

using namespace lldb_private;

StoppointCallbackContext::StoppointCallbackContext() = default;

StoppointCallbackContext::StoppointCallbackContext(
    Event *e, const ExecutionContext &exe_ctx, bool synchronously)
    : event(e), exe_ctx_ref(exe_ctx), is_synchronous(synchronously) {}

void StoppointCallbackContext::Clear() {
  event = nullptr;
  exe_ctx_ref.Clear();
  is_synchronous = false;
}
