//===-- AppleObjCRuntime.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIME_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIME_H


#include "AppleObjCTrampolineHandler.h"
#include "AppleThreadPlanStepThroughObjCTrampoline.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include <optional>

namespace lldb_private {

class AppleObjCRuntime : public lldb_private::ObjCLanguageRuntime {
public:
  ~AppleObjCRuntime() override;

  // Static Functions
  // Note there is no CreateInstance, Initialize & Terminate functions here,
  // because
  // you can't make an instance of this generic runtime.

  static char ID;

  static void Initialize();

  static void Terminate();

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjCLanguageRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  // These are generic runtime functions:
  llvm::Error GetObjectDescription(Stream &str, Value &value,
                                   ExecutionContextScope *exe_scope) override;

  llvm::Error GetObjectDescription(Stream &str, ValueObject &object) override;

  bool CouldHaveDynamicValue(ValueObject &in_value) override;

  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  TypeAndOrName FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                 ValueObject &static_value) override;

  // These are the ObjC specific functions.

  bool IsModuleObjCLibrary(const lldb::ModuleSP &module_sp) override;

  bool ReadObjCLibrary(const lldb::ModuleSP &module_sp) override;

  bool HasReadObjCLibrary() override { return m_read_objc_library; }

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop_others) override;

  // Get the "libobjc.A.dylib" module from the current target if we can find
  // it, also cache it once it is found to ensure quick lookups.
  lldb::ModuleSP GetObjCModule();

  // Sync up with the target

  void ModulesDidLoad(const ModuleList &module_list) override;

  void SetExceptionBreakpoints() override;

  void ClearExceptionBreakpoints() override;

  bool ExceptionBreakpointsAreSet() override;

  bool ExceptionBreakpointsExplainStop(lldb::StopInfoSP stop_reason) override;

  lldb::SearchFilterSP CreateExceptionSearchFilter() override;

  static std::tuple<FileSpec, ConstString> GetExceptionThrowLocation();

  lldb::ValueObjectSP GetExceptionObjectForThread(
      lldb::ThreadSP thread_sp) override;

  lldb::ThreadSP GetBacktraceThreadFromException(
      lldb::ValueObjectSP thread_sp) override;

  uint32_t GetFoundationVersion();

  virtual void GetValuesForGlobalCFBooleans(lldb::addr_t &cf_true,
                                            lldb::addr_t &cf_false);

  virtual bool IsTaggedPointer (lldb::addr_t addr) { return false; }

protected:
  // Call CreateInstance instead.
  AppleObjCRuntime(Process *process);

  bool CalculateHasNewLiteralsAndIndexing() override;

  static bool AppleIsModuleObjCLibrary(const lldb::ModuleSP &module_sp);

  static ObjCRuntimeVersions GetObjCVersion(Process *process,
                                            lldb::ModuleSP &objc_module_sp);

  void ReadObjCLibraryIfNeeded(const ModuleList &module_list);

  Address *GetPrintForDebuggerAddr();

  std::unique_ptr<Address> m_PrintForDebugger_addr;
  bool m_read_objc_library;
  std::unique_ptr<lldb_private::AppleObjCTrampolineHandler>
      m_objc_trampoline_handler_up;
  lldb::BreakpointSP m_objc_exception_bp_sp;
  lldb::ModuleWP m_objc_module_wp;
  std::unique_ptr<FunctionCaller> m_print_object_caller_up;

  std::optional<uint32_t> m_Foundation_major;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIME_H
