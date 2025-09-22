//===-- GNUstepObjCRuntime.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_GNUSTEPOBJCRUNTIME_GNUSTEPOBJCRUNTIME_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_GNUSTEPOBJCRUNTIME_GNUSTEPOBJCRUNTIME_H

#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"

#include <optional>

namespace lldb_private {

class GNUstepObjCRuntime : public lldb_private::ObjCLanguageRuntime {
public:
  ~GNUstepObjCRuntime() override;

  //
  //  PluginManager, PluginInterface and LLVM RTTI implementation
  //

  static char ID;

  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() {
    return "gnustep-objc-libobjc2";
  }

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  void ModulesDidLoad(const ModuleList &module_list) override;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || ObjCLanguageRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  //
  // LanguageRuntime implementation
  //
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

  lldb::BreakpointResolverSP
  CreateExceptionResolver(const lldb::BreakpointSP &bkpt, bool catch_bp,
                          bool throw_bp) override;

  lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                  bool stop_others) override;

  //
  // ObjCLanguageRuntime implementation
  //

  bool IsModuleObjCLibrary(const lldb::ModuleSP &module_sp) override;

  bool ReadObjCLibrary(const lldb::ModuleSP &module_sp) override;

  bool HasReadObjCLibrary() override { return m_objc_module_sp != nullptr; }

  llvm::Expected<std::unique_ptr<UtilityFunction>>
  CreateObjectChecker(std::string name, ExecutionContext &exe_ctx) override;

  ObjCRuntimeVersions GetRuntimeVersion() const override {
    return ObjCRuntimeVersions::eGNUstep_libobjc2;
  }

  void UpdateISAToDescriptorMapIfNeeded() override;

protected:
  // Call CreateInstance instead.
  GNUstepObjCRuntime(Process *process);

  lldb::ModuleSP m_objc_module_sp;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_GNUSTEPOBJCRUNTIME_GNUSTEPOBJCRUNTIME_H
