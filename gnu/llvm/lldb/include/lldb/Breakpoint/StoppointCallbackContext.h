//===-- StoppointCallbackContext.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_BREAKPOINT_STOPPOINTCALLBACKCONTEXT_H
#define LLDB_BREAKPOINT_STOPPOINTCALLBACKCONTEXT_H

#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

/// \class StoppointCallbackContext StoppointCallbackContext.h
/// "lldb/Breakpoint/StoppointCallbackContext.h" Class holds the information
/// that a breakpoint callback needs to evaluate this stop.

/// General Outline:
/// When we hit a breakpoint we need to package up whatever information is
/// needed to evaluate breakpoint commands and conditions.  This class is the
/// container of that information.

class StoppointCallbackContext {
public:
  StoppointCallbackContext();

  StoppointCallbackContext(Event *event, const ExecutionContext &exe_ctx,
                           bool synchronously = false);

  /// Clear the object's state.
  ///
  /// Sets the event, process and thread to NULL, and the frame index to an
  /// invalid value.
  void Clear();

  // Member variables
  Event *event = nullptr; // This is the event, the callback can modify this to
                          // indicate the meaning of the breakpoint hit
  ExecutionContextRef
      exe_ctx_ref;     // This tells us where we have stopped, what thread.
  bool is_synchronous =
      false; // Is the callback being executed synchronously with the
             // breakpoint,
             // or asynchronously as the event is retrieved?
};

} // namespace lldb_private

#endif // LLDB_BREAKPOINT_STOPPOINTCALLBACKCONTEXT_H
