//===-- ForwardDecl.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Windows_ForwardDecl_H_
#define liblldb_Plugins_Process_Windows_ForwardDecl_H_

#include <memory>

// ExceptionResult is returned by the debug delegate to specify how it processed
// the exception.
enum class ExceptionResult {
  BreakInDebugger,  // Break in the debugger and give the user a chance to
                    // interact with
                    // the program before continuing.
  MaskException,    // Eat the exception and don't let the application know it
                    // occurred.
  SendToApplication // Send the exception to the application to be handled as if
                    // there were
                    // no debugger attached.
};

namespace lldb_private {

class ProcessWindows;

class IDebugDelegate;
class DebuggerThread;
class ExceptionRecord;

typedef std::shared_ptr<IDebugDelegate> DebugDelegateSP;
typedef std::shared_ptr<DebuggerThread> DebuggerThreadSP;
typedef std::shared_ptr<ExceptionRecord> ExceptionRecordSP;
typedef std::unique_ptr<ExceptionRecord> ExceptionRecordUP;
}

#endif
