//===-- ThreadPlanCallFunctionUsingABI.h --------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_THREADPLANCALLFUNCTIONUSINGABI_H
#define LLDB_TARGET_THREADPLANCALLFUNCTIONUSINGABI_H

#include "lldb/Target/ABI.h"
#include "lldb/Target/Thread.h"
#include "lldb/Target/ThreadPlanCallFunction.h"
#include "lldb/lldb-private.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/IR/DerivedTypes.h"

namespace lldb_private {

class ThreadPlanCallFunctionUsingABI : public ThreadPlanCallFunction {
  // Create a thread plan to call a function at the address passed in the
  // "function" argument, this function is executed using register manipulation
  // instead of JIT. Class derives from ThreadPlanCallFunction and differs by
  // calling a alternative
  // ABI interface ABI::PrepareTrivialCall() which provides more detailed
  // information.
public:
  ThreadPlanCallFunctionUsingABI(Thread &thread,
                                 const Address &function_address,
                                 llvm::Type &function_prototype,
                                 llvm::Type &return_type,
                                 llvm::ArrayRef<ABI::CallArgument> args,
                                 const EvaluateExpressionOptions &options);

  ~ThreadPlanCallFunctionUsingABI() override;

  void GetDescription(Stream *s, lldb::DescriptionLevel level) override;

protected:
  void SetReturnValue() override;

private:
  llvm::Type &m_return_type;
  ThreadPlanCallFunctionUsingABI(const ThreadPlanCallFunctionUsingABI &) =
      delete;
  const ThreadPlanCallFunctionUsingABI &
  operator=(const ThreadPlanCallFunctionUsingABI &) = delete;
};

} // namespace lldb_private

#endif // LLDB_TARGET_THREADPLANCALLFUNCTIONUSINGABI_H
