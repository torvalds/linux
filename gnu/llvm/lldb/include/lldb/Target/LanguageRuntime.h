//===-- LanguageRuntime.h ---------------------------------------------------*-
// C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_LANGUAGERUNTIME_H
#define LLDB_TARGET_LANGUAGERUNTIME_H

#include "lldb/Breakpoint/BreakpointResolver.h"
#include "lldb/Breakpoint/BreakpointResolverName.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/Value.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Expression/LLVMUserExpression.h"
#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Target/ExecutionContextScope.h"
#include "lldb/Target/Runtime.h"
#include "lldb/lldb-private.h"
#include "lldb/lldb-public.h"
#include <optional>

namespace lldb_private {

class ExceptionSearchFilter : public SearchFilter {
public:
  ExceptionSearchFilter(const lldb::TargetSP &target_sp,
                        lldb::LanguageType language,
                        bool update_module_list = true);

  ~ExceptionSearchFilter() override = default;

  bool ModulePasses(const lldb::ModuleSP &module_sp) override;

  bool ModulePasses(const FileSpec &spec) override;

  void Search(Searcher &searcher) override;

  void GetDescription(Stream *s) override;

  static SearchFilter *
  CreateFromStructuredData(Target &target,
                           const StructuredData::Dictionary &data_dict,
                           Status &error);

  StructuredData::ObjectSP SerializeToStructuredData() override;

protected:
  lldb::LanguageType m_language;
  LanguageRuntime *m_language_runtime;
  lldb::SearchFilterSP m_filter_sp;

  lldb::SearchFilterSP DoCreateCopy() override;

  void UpdateModuleListIfNeeded();
};

class LanguageRuntime : public Runtime, public PluginInterface {
public:
  static LanguageRuntime *FindPlugin(Process *process,
                                     lldb::LanguageType language);

  static void InitializeCommands(CommandObject *parent);

  virtual lldb::LanguageType GetLanguageType() const = 0;

  /// Return the preferred language runtime instance, which in most cases will
  /// be the current instance.
  virtual LanguageRuntime *GetPreferredLanguageRuntime(ValueObject &in_value) {
    return nullptr;
  }

  virtual llvm::Error GetObjectDescription(Stream &str,
                                           ValueObject &object) = 0;

  virtual llvm::Error
  GetObjectDescription(Stream &str, Value &value,
                       ExecutionContextScope *exe_scope) = 0;

  struct VTableInfo {
    Address addr; /// Address of the vtable's virtual function table
    Symbol *symbol; /// The vtable symbol from the symbol table
  };
  /// Get the vtable information for a given value.
  ///
  /// \param[in] in_value
  ///     The value object to try and extract the VTableInfo from.
  ///
  /// \param[in] check_type
  ///     If true, the compiler type of \a in_value will be checked to see if
  ///     it is an instance to, or pointer or reference to a class or struct
  ///     that has a vtable. If the type doesn't meet the requirements, an
  ///     error will be returned explaining why the type isn't suitable.
  ///
  /// \return
  ///     An error if anything goes wrong while trying to extract the vtable
  ///     or if \a check_type is true and the type doesn't have a vtable.
  virtual llvm::Expected<VTableInfo> GetVTableInfo(ValueObject &in_value,
                                                   bool check_type) {
    return llvm::createStringError(
        std::errc::invalid_argument,
        "language doesn't support getting vtable information");
  }

  // this call should return true if it could set the name and/or the type
  virtual bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                        lldb::DynamicValueType use_dynamic,
                                        TypeAndOrName &class_type_or_name,
                                        Address &address,
                                        Value::ValueType &value_type) = 0;

  // This call should return a CompilerType given a generic type name and an
  // ExecutionContextScope in which one can actually fetch any specialization
  // information required.
  virtual CompilerType GetConcreteType(ExecutionContextScope *exe_scope,
                                       ConstString abstract_type_name) {
    return CompilerType();
  }

  // This should be a fast test to determine whether it is likely that this
  // value would have a dynamic type.
  virtual bool CouldHaveDynamicValue(ValueObject &in_value) = 0;

  // The contract for GetDynamicTypeAndAddress() is to return a "bare-bones"
  // dynamic type For instance, given a Base* pointer,
  // GetDynamicTypeAndAddress() will return the type of Derived, not Derived*.
  // The job of this API is to correct this misalignment between the static
  // type and the discovered dynamic type
  virtual TypeAndOrName FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                         ValueObject &static_value) = 0;

  virtual void SetExceptionBreakpoints() {}

  virtual void ClearExceptionBreakpoints() {}

  virtual bool ExceptionBreakpointsAreSet() { return false; }

  virtual bool ExceptionBreakpointsExplainStop(lldb::StopInfoSP stop_reason) {
    return false;
  }

  static lldb::BreakpointSP
  CreateExceptionBreakpoint(Target &target, lldb::LanguageType language,
                            bool catch_bp, bool throw_bp,
                            bool is_internal = false);

  static lldb::BreakpointPreconditionSP
  GetExceptionPrecondition(lldb::LanguageType language, bool throw_bp);

  virtual lldb::ValueObjectSP GetExceptionObjectForThread(
      lldb::ThreadSP thread_sp) {
    return lldb::ValueObjectSP();
  }

  virtual lldb::ThreadSP GetBacktraceThreadFromException(
      lldb::ValueObjectSP thread_sp) {
    return lldb::ThreadSP();
  }

  virtual DeclVendor *GetDeclVendor() { return nullptr; }

  virtual lldb::BreakpointResolverSP
  CreateExceptionResolver(const lldb::BreakpointSP &bkpt,
                          bool catch_bp, bool throw_bp) = 0;

  virtual lldb::SearchFilterSP CreateExceptionSearchFilter() {
    return m_process->GetTarget().GetSearchFilterForModule(nullptr);
  }

  virtual std::optional<uint64_t>
  GetTypeBitSize(const CompilerType &compiler_type) {
    return {};
  }

  virtual void SymbolsDidLoad(const ModuleList &module_list) {}

  virtual lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                          bool stop_others) = 0;

  /// Identify whether a name is a runtime value that should not be hidden by
  /// from the user interface.
  virtual bool IsAllowedRuntimeValue(ConstString name) { return false; }

  virtual std::optional<CompilerType> GetRuntimeType(CompilerType base_type) {
    return std::nullopt;
  }

  void ModulesDidLoad(const ModuleList &module_list) override {}

  // Called by ClangExpressionParser::PrepareForExecution to query for any
  // custom LLVM IR passes that need to be run before an expression is
  // assembled and run.
  virtual bool GetIRPasses(LLVMUserExpression::IRPasses &custom_passes) {
    return false;
  }

  // Given the name of a runtime symbol (e.g. in Objective-C, an ivar offset
  // symbol), try to determine from the runtime what the value of that symbol
  // would be. Useful when the underlying binary is stripped.
  virtual lldb::addr_t LookupRuntimeSymbol(ConstString name) {
    return LLDB_INVALID_ADDRESS;
  }

  virtual bool isA(const void *ClassID) const { return ClassID == &ID; }
  static char ID;

  /// A language runtime may be able to provide a special UnwindPlan for
  /// the frame represented by the register contents \a regctx when that
  /// frame is not following the normal ABI conventions.
  /// Instead of using the normal UnwindPlan for the function, we will use
  /// this special UnwindPlan for this one backtrace.
  /// One example of this would be a language that has asynchronous functions,
  /// functions that may not be currently-executing, while waiting on other
  /// asynchronous calls they made, but are part of a logical backtrace that
  /// we want to show the developer because that's how they think of the
  /// program flow.
  ///
  /// \param[in] thread
  ///     The thread that the unwind is happening on.
  ///
  /// \param[in] regctx
  ///     The RegisterContext for the frame we need to create an UnwindPlan.
  ///     We don't yet have a StackFrame when we're selecting the UnwindPlan.
  ///
  /// \param[out] behaves_like_zeroth_frame
  ///     With normal ABI calls, all stack frames except the zeroth frame need
  ///     to have the return-pc value backed up by 1 for symbolication purposes.
  ///     For these LanguageRuntime unwind plans, they may not follow normal ABI
  ///     calling conventions and the return pc may need to be symbolicated
  ///     as-is.
  ///
  /// \return
  ///     Returns an UnwindPlan to find the caller frame if it should be used,
  ///     instead of the UnwindPlan that would normally be used for this
  ///     function.
  static lldb::UnwindPlanSP
  GetRuntimeUnwindPlan(lldb_private::Thread &thread,
                       lldb_private::RegisterContext *regctx,
                       bool &behaves_like_zeroth_frame);

protected:
  // The static GetRuntimeUnwindPlan method above is only implemented in the
  // base class; subclasses may override this protected member if they can
  // provide one of these UnwindPlans.
  virtual lldb::UnwindPlanSP
  GetRuntimeUnwindPlan(lldb::ProcessSP process_sp,
                       lldb_private::RegisterContext *regctx,
                       bool &behaves_like_zeroth_frame) {
    return lldb::UnwindPlanSP();
  }

  LanguageRuntime(Process *process);
};

} // namespace lldb_private

#endif // LLDB_TARGET_LANGUAGERUNTIME_H
