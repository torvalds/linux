//===-- IRInterpreter.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_EXPRESSION_IRINTERPRETER_H
#define LLDB_EXPRESSION_IRINTERPRETER_H

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Stream.h"
#include "lldb/Utility/Timeout.h"
#include "lldb/lldb-public.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Pass.h"

namespace llvm {
class Function;
class Module;
}

namespace lldb_private {

class IRMemoryMap;
}

/// \class IRInterpreter IRInterpreter.h "lldb/Expression/IRInterpreter.h"
/// Attempt to interpret the function's code if it does not require
///        running the target.
///
/// In some cases, the IR for an expression can be evaluated entirely in the
/// debugger, manipulating variables but not executing any code in the target.
/// The IRInterpreter attempts to do this.
class IRInterpreter {
public:
  static bool CanInterpret(llvm::Module &module, llvm::Function &function,
                           lldb_private::Status &error,
                           const bool support_function_calls);

  static bool Interpret(llvm::Module &module, llvm::Function &function,
                        llvm::ArrayRef<lldb::addr_t> args,
                        lldb_private::IRExecutionUnit &execution_unit,
                        lldb_private::Status &error,
                        lldb::addr_t stack_frame_bottom,
                        lldb::addr_t stack_frame_top,
                        lldb_private::ExecutionContext &exe_ctx,
                        lldb_private::Timeout<std::micro> timeout);

private:
  static bool supportsFunction(llvm::Function &llvm_function,
                               lldb_private::Status &err);
};

#endif
