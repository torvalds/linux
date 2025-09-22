//===-- AppleObjCRuntime.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AppleObjCRuntime.h"
#include "AppleObjCRuntimeV1.h"
#include "AppleObjCRuntimeV2.h"
#include "AppleObjCTrampolineHandler.h"
#include "Plugins/Language/ObjC/NSString.h"
#include "Plugins/LanguageRuntime/CPlusPlus/CPPLanguageRuntime.h"
#include "Plugins/Process/Utility/HistoryThread.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/ErrorMessages.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StreamString.h"
#include "clang/AST/Type.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE(AppleObjCRuntime)

char AppleObjCRuntime::ID = 0;

AppleObjCRuntime::~AppleObjCRuntime() = default;

AppleObjCRuntime::AppleObjCRuntime(Process *process)
    : ObjCLanguageRuntime(process), m_read_objc_library(false),
      m_objc_trampoline_handler_up(), m_Foundation_major() {
  ReadObjCLibraryIfNeeded(process->GetTarget().GetImages());
}

void AppleObjCRuntime::Initialize() {
  AppleObjCRuntimeV2::Initialize();
  AppleObjCRuntimeV1::Initialize();
}

void AppleObjCRuntime::Terminate() {
  AppleObjCRuntimeV2::Terminate();
  AppleObjCRuntimeV1::Terminate();
}

llvm::Error AppleObjCRuntime::GetObjectDescription(Stream &str,
                                                   ValueObject &valobj) {
  CompilerType compiler_type(valobj.GetCompilerType());
  bool is_signed;
  // ObjC objects can only be pointers (or numbers that actually represents
  // pointers but haven't been typecast, because reasons..)
  if (!compiler_type.IsIntegerType(is_signed) && !compiler_type.IsPointerType())
    return llvm::createStringError("not a pointer type");

  // Make the argument list: we pass one arg, the address of our pointer, to
  // the print function.
  Value val;

  if (!valobj.ResolveValue(val.GetScalar()))
    return llvm::createStringError("pointer value could not be resolved");

  // Value Objects may not have a process in their ExecutionContextRef.  But we
  // need to have one in the ref we pass down to eventually call description.
  // Get it from the target if it isn't present.
  ExecutionContext exe_ctx;
  if (valobj.GetProcessSP()) {
    exe_ctx = ExecutionContext(valobj.GetExecutionContextRef());
  } else {
    exe_ctx.SetContext(valobj.GetTargetSP(), true);
    if (!exe_ctx.HasProcessScope())
      return llvm::createStringError("no process");
  }
  return GetObjectDescription(str, val, exe_ctx.GetBestExecutionContextScope());
}

llvm::Error
AppleObjCRuntime::GetObjectDescription(Stream &strm, Value &value,
                                       ExecutionContextScope *exe_scope) {
  if (!m_read_objc_library)
    return llvm::createStringError("Objective-C runtime not loaded");

  ExecutionContext exe_ctx;
  exe_scope->CalculateExecutionContext(exe_ctx);
  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return llvm::createStringError("no process");

  // We need other parts of the exe_ctx, but the processes have to match.
  assert(m_process == process);

  // Get the function address for the print function.
  const Address *function_address = GetPrintForDebuggerAddr();
  if (!function_address)
    return llvm::createStringError("no print function");

  Target *target = exe_ctx.GetTargetPtr();
  CompilerType compiler_type = value.GetCompilerType();
  if (compiler_type) {
    if (!TypeSystemClang::IsObjCObjectPointerType(compiler_type))
      return llvm::createStringError(
          "Value doesn't point to an ObjC object.\n");
  } else {
    // If it is not a pointer, see if we can make it into a pointer.
    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(*target);
    if (!scratch_ts_sp)
      return llvm::createStringError("no scratch type system");

    CompilerType opaque_type = scratch_ts_sp->GetBasicType(eBasicTypeObjCID);
    if (!opaque_type)
      opaque_type =
          scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();
    // value.SetContext(Value::eContextTypeClangType, opaque_type_ptr);
    value.SetCompilerType(opaque_type);
  }

  ValueList arg_value_list;
  arg_value_list.PushValue(value);

  // This is the return value:
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*target);
  if (!scratch_ts_sp)
    return llvm::createStringError("no scratch type system");

  CompilerType return_compiler_type = scratch_ts_sp->GetCStringType(true);
  Value ret;
  //    ret.SetContext(Value::eContextTypeClangType, return_compiler_type);
  ret.SetCompilerType(return_compiler_type);

  if (!exe_ctx.GetFramePtr()) {
    Thread *thread = exe_ctx.GetThreadPtr();
    if (thread == nullptr) {
      exe_ctx.SetThreadSP(process->GetThreadList().GetSelectedThread());
      thread = exe_ctx.GetThreadPtr();
    }
    if (thread) {
      exe_ctx.SetFrameSP(thread->GetSelectedFrame(DoNoSelectMostRelevantFrame));
    }
  }

  // Now we're ready to call the function:

  DiagnosticManager diagnostics;
  lldb::addr_t wrapper_struct_addr = LLDB_INVALID_ADDRESS;

  if (!m_print_object_caller_up) {
    Status error;
    m_print_object_caller_up.reset(
        exe_scope->CalculateTarget()->GetFunctionCallerForLanguage(
            eLanguageTypeObjC, return_compiler_type, *function_address,
            arg_value_list, "objc-object-description", error));
    if (error.Fail()) {
      m_print_object_caller_up.reset();
      return llvm::createStringError(
          llvm::Twine(
              "could not get function runner to call print for debugger "
              "function: ") +
          error.AsCString());
    }
    m_print_object_caller_up->InsertFunction(exe_ctx, wrapper_struct_addr,
                                             diagnostics);
  } else {
    m_print_object_caller_up->WriteFunctionArguments(
        exe_ctx, wrapper_struct_addr, arg_value_list, diagnostics);
  }

  EvaluateExpressionOptions options;
  options.SetUnwindOnError(true);
  options.SetTryAllThreads(true);
  options.SetStopOthers(true);
  options.SetIgnoreBreakpoints(true);
  options.SetTimeout(process->GetUtilityExpressionTimeout());
  options.SetIsForUtilityExpr(true);

  ExpressionResults results = m_print_object_caller_up->ExecuteFunction(
      exe_ctx, &wrapper_struct_addr, options, diagnostics, ret);
  if (results != eExpressionCompleted)
    return llvm::createStringError(
        "could not evaluate print object function: " + toString(results));

  addr_t result_ptr = ret.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);

  char buf[512];
  size_t cstr_len = 0;
  size_t full_buffer_len = sizeof(buf) - 1;
  size_t curr_len = full_buffer_len;
  while (curr_len == full_buffer_len) {
    Status error;
    curr_len = process->ReadCStringFromMemory(result_ptr + cstr_len, buf,
                                              sizeof(buf), error);
    strm.Write(buf, curr_len);
    cstr_len += curr_len;
  }
  if (cstr_len > 0)
    return llvm::Error::success();
  return llvm::createStringError("empty object description");
}

lldb::ModuleSP AppleObjCRuntime::GetObjCModule() {
  ModuleSP module_sp(m_objc_module_wp.lock());
  if (module_sp)
    return module_sp;

  Process *process = GetProcess();
  if (process) {
    const ModuleList &modules = process->GetTarget().GetImages();
    for (uint32_t idx = 0; idx < modules.GetSize(); idx++) {
      module_sp = modules.GetModuleAtIndex(idx);
      if (AppleObjCRuntime::AppleIsModuleObjCLibrary(module_sp)) {
        m_objc_module_wp = module_sp;
        return module_sp;
      }
    }
  }
  return ModuleSP();
}

Address *AppleObjCRuntime::GetPrintForDebuggerAddr() {
  if (!m_PrintForDebugger_addr) {
    const ModuleList &modules = m_process->GetTarget().GetImages();

    SymbolContextList contexts;
    SymbolContext context;

    modules.FindSymbolsWithNameAndType(ConstString("_NSPrintForDebugger"),
                                        eSymbolTypeCode, contexts);
    if (contexts.IsEmpty()) {
      modules.FindSymbolsWithNameAndType(ConstString("_CFPrintForDebugger"),
                                         eSymbolTypeCode, contexts);
      if (contexts.IsEmpty())
        return nullptr;
    }

    contexts.GetContextAtIndex(0, context);

    m_PrintForDebugger_addr =
        std::make_unique<Address>(context.symbol->GetAddress());
  }

  return m_PrintForDebugger_addr.get();
}

bool AppleObjCRuntime::CouldHaveDynamicValue(ValueObject &in_value) {
  return in_value.GetCompilerType().IsPossibleDynamicType(
      nullptr,
      false, // do not check C++
      true); // check ObjC
}

bool AppleObjCRuntime::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &address,
    Value::ValueType &value_type) {
  return false;
}

TypeAndOrName
AppleObjCRuntime::FixUpDynamicType(const TypeAndOrName &type_and_or_name,
                                   ValueObject &static_value) {
  CompilerType static_type(static_value.GetCompilerType());
  Flags static_type_flags(static_type.GetTypeInfo());

  TypeAndOrName ret(type_and_or_name);
  if (type_and_or_name.HasType()) {
    // The type will always be the type of the dynamic object.  If our parent's
    // type was a pointer, then our type should be a pointer to the type of the
    // dynamic object.  If a reference, then the original type should be
    // okay...
    CompilerType orig_type = type_and_or_name.GetCompilerType();
    CompilerType corrected_type = orig_type;
    if (static_type_flags.AllSet(eTypeIsPointer))
      corrected_type = orig_type.GetPointerType();
    ret.SetCompilerType(corrected_type);
  } else {
    // If we are here we need to adjust our dynamic type name to include the
    // correct & or * symbol
    std::string corrected_name(type_and_or_name.GetName().GetCString());
    if (static_type_flags.AllSet(eTypeIsPointer))
      corrected_name.append(" *");
    // the parent type should be a correctly pointer'ed or referenc'ed type
    ret.SetCompilerType(static_type);
    ret.SetName(corrected_name.c_str());
  }
  return ret;
}

bool AppleObjCRuntime::AppleIsModuleObjCLibrary(const ModuleSP &module_sp) {
  if (module_sp) {
    const FileSpec &module_file_spec = module_sp->GetFileSpec();
    static ConstString ObjCName("libobjc.A.dylib");

    if (module_file_spec) {
      if (module_file_spec.GetFilename() == ObjCName)
        return true;
    }
  }
  return false;
}

// we use the version of Foundation to make assumptions about the ObjC runtime
// on a target
uint32_t AppleObjCRuntime::GetFoundationVersion() {
  if (!m_Foundation_major) {
    const ModuleList &modules = m_process->GetTarget().GetImages();
    for (uint32_t idx = 0; idx < modules.GetSize(); idx++) {
      lldb::ModuleSP module_sp = modules.GetModuleAtIndex(idx);
      if (!module_sp)
        continue;
      if (strcmp(module_sp->GetFileSpec().GetFilename().AsCString(""),
                 "Foundation") == 0) {
        m_Foundation_major = module_sp->GetVersion().getMajor();
        return *m_Foundation_major;
      }
    }
    return LLDB_INVALID_MODULE_VERSION;
  } else
    return *m_Foundation_major;
}

void AppleObjCRuntime::GetValuesForGlobalCFBooleans(lldb::addr_t &cf_true,
                                                    lldb::addr_t &cf_false) {
  cf_true = cf_false = LLDB_INVALID_ADDRESS;
}

bool AppleObjCRuntime::IsModuleObjCLibrary(const ModuleSP &module_sp) {
  return AppleIsModuleObjCLibrary(module_sp);
}

bool AppleObjCRuntime::ReadObjCLibrary(const ModuleSP &module_sp) {
  // Maybe check here and if we have a handler already, and the UUID of this
  // module is the same as the one in the current module, then we don't have to
  // reread it?
  m_objc_trampoline_handler_up = std::make_unique<AppleObjCTrampolineHandler>(
      m_process->shared_from_this(), module_sp);
  if (m_objc_trampoline_handler_up != nullptr) {
    m_read_objc_library = true;
    return true;
  } else
    return false;
}

ThreadPlanSP AppleObjCRuntime::GetStepThroughTrampolinePlan(Thread &thread,
                                                            bool stop_others) {
  ThreadPlanSP thread_plan_sp;
  if (m_objc_trampoline_handler_up)
    thread_plan_sp = m_objc_trampoline_handler_up->GetStepThroughDispatchPlan(
        thread, stop_others);
  return thread_plan_sp;
}

// Static Functions
ObjCLanguageRuntime::ObjCRuntimeVersions
AppleObjCRuntime::GetObjCVersion(Process *process, ModuleSP &objc_module_sp) {
  if (!process)
    return ObjCRuntimeVersions::eObjC_VersionUnknown;

  Target &target = process->GetTarget();
  if (target.GetArchitecture().GetTriple().getVendor() !=
      llvm::Triple::VendorType::Apple)
    return ObjCRuntimeVersions::eObjC_VersionUnknown;

  for (ModuleSP module_sp : target.GetImages().Modules()) {
    // One tricky bit here is that we might get called as part of the initial
    // module loading, but before all the pre-run libraries get winnowed from
    // the module list.  So there might actually be an old and incorrect ObjC
    // library sitting around in the list, and we don't want to look at that.
    // That's why we call IsLoadedInTarget.

    if (AppleIsModuleObjCLibrary(module_sp) &&
        module_sp->IsLoadedInTarget(&target)) {
      objc_module_sp = module_sp;
      ObjectFile *ofile = module_sp->GetObjectFile();
      if (!ofile)
        return ObjCRuntimeVersions::eObjC_VersionUnknown;

      SectionList *sections = module_sp->GetSectionList();
      if (!sections)
        return ObjCRuntimeVersions::eObjC_VersionUnknown;
      SectionSP v1_telltale_section_sp =
          sections->FindSectionByName(ConstString("__OBJC"));
      if (v1_telltale_section_sp) {
        return ObjCRuntimeVersions::eAppleObjC_V1;
      }
      return ObjCRuntimeVersions::eAppleObjC_V2;
    }
  }

  return ObjCRuntimeVersions::eObjC_VersionUnknown;
}

void AppleObjCRuntime::SetExceptionBreakpoints() {
  const bool catch_bp = false;
  const bool throw_bp = true;
  const bool is_internal = true;

  if (!m_objc_exception_bp_sp) {
    m_objc_exception_bp_sp = LanguageRuntime::CreateExceptionBreakpoint(
        m_process->GetTarget(), GetLanguageType(), catch_bp, throw_bp,
        is_internal);
    if (m_objc_exception_bp_sp)
      m_objc_exception_bp_sp->SetBreakpointKind("ObjC exception");
  } else
    m_objc_exception_bp_sp->SetEnabled(true);
}

void AppleObjCRuntime::ClearExceptionBreakpoints() {
  if (!m_process)
    return;

  if (m_objc_exception_bp_sp.get()) {
    m_objc_exception_bp_sp->SetEnabled(false);
  }
}

bool AppleObjCRuntime::ExceptionBreakpointsAreSet() {
  return m_objc_exception_bp_sp && m_objc_exception_bp_sp->IsEnabled();
}

bool AppleObjCRuntime::ExceptionBreakpointsExplainStop(
    lldb::StopInfoSP stop_reason) {
  if (!m_process)
    return false;

  if (!stop_reason || stop_reason->GetStopReason() != eStopReasonBreakpoint)
    return false;

  uint64_t break_site_id = stop_reason->GetValue();
  return m_process->GetBreakpointSiteList().StopPointSiteContainsBreakpoint(
      break_site_id, m_objc_exception_bp_sp->GetID());
}

bool AppleObjCRuntime::CalculateHasNewLiteralsAndIndexing() {
  if (!m_process)
    return false;

  Target &target(m_process->GetTarget());

  static ConstString s_method_signature(
      "-[NSDictionary objectForKeyedSubscript:]");
  static ConstString s_arclite_method_signature(
      "__arclite_objectForKeyedSubscript");

  SymbolContextList sc_list;

  target.GetImages().FindSymbolsWithNameAndType(s_method_signature,
                                                eSymbolTypeCode, sc_list);
  if (sc_list.IsEmpty())
    target.GetImages().FindSymbolsWithNameAndType(s_arclite_method_signature,
                                                  eSymbolTypeCode, sc_list);
  return !sc_list.IsEmpty();
}

lldb::SearchFilterSP AppleObjCRuntime::CreateExceptionSearchFilter() {
  Target &target = m_process->GetTarget();

  FileSpecList filter_modules;
  if (target.GetArchitecture().GetTriple().getVendor() == llvm::Triple::Apple) {
    filter_modules.Append(std::get<0>(GetExceptionThrowLocation()));
  }
  return target.GetSearchFilterForModuleList(&filter_modules);
}

ValueObjectSP AppleObjCRuntime::GetExceptionObjectForThread(
    ThreadSP thread_sp) {
  auto *cpp_runtime = m_process->GetLanguageRuntime(eLanguageTypeC_plus_plus);
  if (!cpp_runtime) return ValueObjectSP();
  auto cpp_exception = cpp_runtime->GetExceptionObjectForThread(thread_sp);
  if (!cpp_exception) return ValueObjectSP();

  auto descriptor = GetClassDescriptor(*cpp_exception);
  if (!descriptor || !descriptor->IsValid()) return ValueObjectSP();

  while (descriptor) {
    ConstString class_name(descriptor->GetClassName());
    if (class_name == "NSException")
      return cpp_exception;
    descriptor = descriptor->GetSuperclass();
  }

  return ValueObjectSP();
}

/// Utility method for error handling in GetBacktraceThreadFromException.
/// \param msg The message to add to the log.
/// \return An invalid ThreadSP to be returned from
///         GetBacktraceThreadFromException.
[[nodiscard]]
static ThreadSP FailExceptionParsing(llvm::StringRef msg) {
  Log *log = GetLog(LLDBLog::Language);
  LLDB_LOG(log, "Failed getting backtrace from exception: {0}", msg);
  return ThreadSP();
}

ThreadSP AppleObjCRuntime::GetBacktraceThreadFromException(
    lldb::ValueObjectSP exception_sp) {
  ValueObjectSP reserved_dict =
      exception_sp->GetChildMemberWithName("reserved");
  if (!reserved_dict)
    return FailExceptionParsing("Failed to get 'reserved' member.");

  reserved_dict = reserved_dict->GetSyntheticValue();
  if (!reserved_dict)
    return FailExceptionParsing("Failed to get synthetic value.");

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*exception_sp->GetTargetSP());
  if (!scratch_ts_sp)
    return FailExceptionParsing("Failed to get scratch AST.");
  CompilerType objc_id = scratch_ts_sp->GetBasicType(lldb::eBasicTypeObjCID);
  ValueObjectSP return_addresses;

  auto objc_object_from_address = [&exception_sp, &objc_id](uint64_t addr,
                                                            const char *name) {
    Value value(addr);
    value.SetCompilerType(objc_id);
    auto object = ValueObjectConstResult::Create(
        exception_sp->GetTargetSP().get(), value, ConstString(name));
    object = object->GetDynamicValue(eDynamicDontRunTarget);
    return object;
  };

  for (size_t idx = 0; idx < reserved_dict->GetNumChildrenIgnoringErrors();
       idx++) {
    ValueObjectSP dict_entry = reserved_dict->GetChildAtIndex(idx);

    DataExtractor data;
    data.SetAddressByteSize(dict_entry->GetProcessSP()->GetAddressByteSize());
    Status error;
    dict_entry->GetData(data, error);
    if (error.Fail()) return ThreadSP();

    lldb::offset_t data_offset = 0;
    auto dict_entry_key = data.GetAddress(&data_offset);
    auto dict_entry_value = data.GetAddress(&data_offset);

    auto key_nsstring = objc_object_from_address(dict_entry_key, "key");
    StreamString key_summary;
    if (lldb_private::formatters::NSStringSummaryProvider(
            *key_nsstring, key_summary, TypeSummaryOptions()) &&
        !key_summary.Empty()) {
      if (key_summary.GetString() == "\"callStackReturnAddresses\"") {
        return_addresses = objc_object_from_address(dict_entry_value,
                                                    "callStackReturnAddresses");
        break;
      }
    }
  }

  if (!return_addresses)
    return FailExceptionParsing("Failed to get return addresses.");
  auto frames_value = return_addresses->GetChildMemberWithName("_frames");
  if (!frames_value)
    return FailExceptionParsing("Failed to get frames_value.");
  addr_t frames_addr = frames_value->GetValueAsUnsigned(0);
  auto count_value = return_addresses->GetChildMemberWithName("_cnt");
  if (!count_value)
    return FailExceptionParsing("Failed to get count_value.");
  size_t count = count_value->GetValueAsUnsigned(0);
  auto ignore_value = return_addresses->GetChildMemberWithName("_ignore");
  if (!ignore_value)
    return FailExceptionParsing("Failed to get ignore_value.");
  size_t ignore = ignore_value->GetValueAsUnsigned(0);

  size_t ptr_size = m_process->GetAddressByteSize();
  std::vector<lldb::addr_t> pcs;
  for (size_t idx = 0; idx < count; idx++) {
    Status error;
    addr_t pc = m_process->ReadPointerFromMemory(
        frames_addr + (ignore + idx) * ptr_size, error);
    pcs.push_back(pc);
  }

  if (pcs.empty())
    return FailExceptionParsing("Failed to get PC list.");

  ThreadSP new_thread_sp(new HistoryThread(*m_process, 0, pcs));
  m_process->GetExtendedThreadList().AddThread(new_thread_sp);
  return new_thread_sp;
}

std::tuple<FileSpec, ConstString>
AppleObjCRuntime::GetExceptionThrowLocation() {
  return std::make_tuple(
      FileSpec("libobjc.A.dylib"), ConstString("objc_exception_throw"));
}

void AppleObjCRuntime::ReadObjCLibraryIfNeeded(const ModuleList &module_list) {
  if (!HasReadObjCLibrary()) {
    std::lock_guard<std::recursive_mutex> guard(module_list.GetMutex());

    size_t num_modules = module_list.GetSize();
    for (size_t i = 0; i < num_modules; i++) {
      auto mod = module_list.GetModuleAtIndex(i);
      if (IsModuleObjCLibrary(mod)) {
        ReadObjCLibrary(mod);
        break;
      }
    }
  }
}

void AppleObjCRuntime::ModulesDidLoad(const ModuleList &module_list) {
  ReadObjCLibraryIfNeeded(module_list);
}
