//===-- CPPLanguageRuntime.h
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_CPLUSPLUS_CPPLANGUAGERUNTIME_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_CPLUSPLUS_CPPLANGUAGERUNTIME_H

#include <vector>

#include "llvm/ADT/StringMap.h"

#include "lldb/Core/PluginInterface.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

namespace lldb_private {

class CPPLanguageRuntime : public LanguageRuntime {
public:
  enum class LibCppStdFunctionCallableCase {
    Lambda = 0,
    CallableObject,
    FreeOrMemberFunction,
    Invalid
  };

  struct LibCppStdFunctionCallableInfo {
    Symbol callable_symbol;
    Address callable_address;
    LineEntry callable_line_entry;
    lldb::addr_t member_f_pointer_value = 0u;
    LibCppStdFunctionCallableCase callable_case =
        LibCppStdFunctionCallableCase::Invalid;
  };

  LibCppStdFunctionCallableInfo
  FindLibCppStdFunctionCallableInfo(lldb::ValueObjectSP &valobj_sp);

  static char ID;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || LanguageRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeC_plus_plus;
  }

  static CPPLanguageRuntime *Get(Process &process) {
    return llvm::cast_or_null<CPPLanguageRuntime>(
        process.GetLanguageRuntime(lldb::eLanguageTypeC_plus_plus));
  }

  llvm::Error GetObjectDescription(Stream &str, ValueObject &object) override;

  llvm::Error GetObjectDescription(Stream &str, Value &value,
                                   ExecutionContextScope *exe_scope) override;

  /// Obtain a ThreadPlan to get us into C++ constructs such as std::function.
  ///
  /// \param[in] thread
  ///     Current thrad of execution.
  ///
  /// \param[in] stop_others
  ///     True if other threads should pause during execution.
  ///
  /// \return
  ///      A ThreadPlan Shared pointer
  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop_others) override;

  bool IsAllowedRuntimeValue(ConstString name) override;
protected:
  // Classes that inherit from CPPLanguageRuntime can see and modify these
  CPPLanguageRuntime(Process *process);

private:
  using OperatorStringToCallableInfoMap =
    llvm::StringMap<CPPLanguageRuntime::LibCppStdFunctionCallableInfo>;

  OperatorStringToCallableInfoMap CallableLookupCache;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_CPLUSPLUS_CPPLANGUAGERUNTIME_H
