//===-- UtilityFunction.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstdio>
#include <sys/types.h>

#include "lldb/Core/Module.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Expression/IRExecutionUnit.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/Host.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/Stream.h"

using namespace lldb_private;
using namespace lldb;

char UtilityFunction::ID;

/// Constructor
///
/// \param[in] text
///     The text of the function.  Must be a full translation unit.
///
/// \param[in] name
///     The name of the function, as used in the text.
UtilityFunction::UtilityFunction(ExecutionContextScope &exe_scope,
                                 std::string text, std::string name,
                                 bool enable_debugging)
    : Expression(exe_scope), m_execution_unit_sp(), m_jit_module_wp(),
      m_function_text(std::move(text)), m_function_name(std::move(name)) {}

UtilityFunction::~UtilityFunction() {
  lldb::ProcessSP process_sp(m_jit_process_wp.lock());
  if (process_sp) {
    lldb::ModuleSP jit_module_sp(m_jit_module_wp.lock());
    if (jit_module_sp)
      process_sp->GetTarget().GetImages().Remove(jit_module_sp);
  }
}

// FIXME: We should check that every time this is called it is called with the
// same return type & arguments...

FunctionCaller *UtilityFunction::MakeFunctionCaller(
    const CompilerType &return_type, const ValueList &arg_value_list,
    lldb::ThreadSP thread_to_use_sp, Status &error) {
  if (m_caller_up)
    return m_caller_up.get();

  ProcessSP process_sp = m_jit_process_wp.lock();
  if (!process_sp) {
    error.SetErrorString("Can't make a function caller without a process.");
    return nullptr;
  }
  // Since we might need to allocate memory and maybe call code to make
  // the caller, we need to be stopped.
  if (process_sp->GetState() != lldb::eStateStopped) {
    error.SetErrorStringWithFormatv(
        "Can't make a function caller while the process is {0}: the process "
        "must be stopped to allocate memory.",
        StateAsCString(process_sp->GetState()));
    return nullptr;
  }

  Address impl_code_address;
  impl_code_address.SetOffset(StartAddress());
  std::string name(m_function_name);
  name.append("-caller");

  m_caller_up.reset(process_sp->GetTarget().GetFunctionCallerForLanguage(
      Language().AsLanguageType(), return_type, impl_code_address,
      arg_value_list, name.c_str(), error));
  if (error.Fail()) {

    return nullptr;
  }
  if (m_caller_up) {
    DiagnosticManager diagnostics;

    unsigned num_errors =
        m_caller_up->CompileFunction(thread_to_use_sp, diagnostics);
    if (num_errors) {
      error.SetErrorStringWithFormat(
          "Error compiling %s caller function: \"%s\".",
          m_function_name.c_str(), diagnostics.GetString().c_str());
      m_caller_up.reset();
      return nullptr;
    }

    diagnostics.Clear();
    ExecutionContext exe_ctx(process_sp);

    if (!m_caller_up->WriteFunctionWrapper(exe_ctx, diagnostics)) {
      error.SetErrorStringWithFormat(
          "Error inserting caller function for %s: \"%s\".",
          m_function_name.c_str(), diagnostics.GetString().c_str());
      m_caller_up.reset();
      return nullptr;
    }
  }
  return m_caller_up.get();
}
