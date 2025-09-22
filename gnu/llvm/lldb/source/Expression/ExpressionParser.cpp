//===-- ExpressionParser.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/ExpressionParser.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/ThreadPlanCallFunction.h"

using namespace lldb;
using namespace lldb_private;

Status ExpressionParser::PrepareForExecution(
    addr_t &func_addr, addr_t &func_end,
    std::shared_ptr<IRExecutionUnit> &execution_unit_sp,
    ExecutionContext &exe_ctx, bool &can_interpret,
    ExecutionPolicy execution_policy) {
  Status status =
      DoPrepareForExecution(func_addr, func_end, execution_unit_sp, exe_ctx,
                            can_interpret, execution_policy);
  if (status.Success() && exe_ctx.GetProcessPtr() && exe_ctx.HasThreadScope())
    status = RunStaticInitializers(execution_unit_sp, exe_ctx);

  return status;
}

Status
ExpressionParser::RunStaticInitializers(IRExecutionUnitSP &execution_unit_sp,
                                        ExecutionContext &exe_ctx) {
  Status err;

  if (!execution_unit_sp.get()) {
    err.SetErrorString(
        "can't run static initializers for a NULL execution unit");
    return err;
  }

  if (!exe_ctx.HasThreadScope()) {
    err.SetErrorString("can't run static initializers without a thread");
    return err;
  }

  std::vector<addr_t> static_initializers;

  execution_unit_sp->GetStaticInitializers(static_initializers);

  for (addr_t static_initializer : static_initializers) {
    EvaluateExpressionOptions options;

    ThreadPlanSP call_static_initializer(new ThreadPlanCallFunction(
        exe_ctx.GetThreadRef(), Address(static_initializer), CompilerType(),
        llvm::ArrayRef<addr_t>(), options));

    DiagnosticManager execution_errors;
    ExpressionResults results =
        exe_ctx.GetThreadRef().GetProcess()->RunThreadPlan(
            exe_ctx, call_static_initializer, options, execution_errors);

    if (results != eExpressionCompleted) {
      err.SetErrorStringWithFormat("couldn't run static initializer: %s",
                                   execution_errors.GetString().c_str());
      return err;
    }
  }

  return err;
}
