//===-- ItaniumABILanguageRuntime.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ItaniumABILanguageRuntime.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectMemory.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Interpreter/CommandObject.h"
#include "lldb/Interpreter/CommandObjectMultiword.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StopInfo.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Status.h"

#include <vector>

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ItaniumABILanguageRuntime, CXXItaniumABI)

static const char *vtable_demangled_prefix = "vtable for ";

char ItaniumABILanguageRuntime::ID = 0;

bool ItaniumABILanguageRuntime::CouldHaveDynamicValue(ValueObject &in_value) {
  const bool check_cxx = true;
  const bool check_objc = false;
  return in_value.GetCompilerType().IsPossibleDynamicType(nullptr, check_cxx,
                                                          check_objc);
}

TypeAndOrName ItaniumABILanguageRuntime::GetTypeInfo(
    ValueObject &in_value, const VTableInfo &vtable_info) {
  if (vtable_info.addr.IsSectionOffset()) {
    // See if we have cached info for this type already
    TypeAndOrName type_info = GetDynamicTypeInfo(vtable_info.addr);
    if (type_info)
      return type_info;

    if (vtable_info.symbol) {
      Log *log = GetLog(LLDBLog::Object);
      llvm::StringRef symbol_name =
          vtable_info.symbol->GetMangled().GetDemangledName().GetStringRef();
      LLDB_LOGF(log,
                "0x%16.16" PRIx64
                ": static-type = '%s' has vtable symbol '%s'\n",
                in_value.GetPointerValue(),
                in_value.GetTypeName().GetCString(),
                symbol_name.str().c_str());
      // We are a C++ class, that's good.  Get the class name and look it
      // up:
      llvm::StringRef class_name = symbol_name;
      class_name.consume_front(vtable_demangled_prefix);
      // We know the class name is absolute, so tell FindTypes that by
      // prefixing it with the root namespace:
      std::string lookup_name("::");
      lookup_name.append(class_name.data(), class_name.size());

      type_info.SetName(class_name);
      ConstString const_lookup_name(lookup_name);
      TypeList class_types;
      ModuleSP module_sp = vtable_info.symbol->CalculateSymbolContextModule();
      // First look in the module that the vtable symbol came from and
      // look for a single exact match.
      TypeResults results;
      TypeQuery query(const_lookup_name.GetStringRef(),
                      TypeQueryOptions::e_exact_match |
                          TypeQueryOptions::e_find_one);
      if (module_sp) {
        module_sp->FindTypes(query, results);
        TypeSP type_sp = results.GetFirstType();
        if (type_sp)
          class_types.Insert(type_sp);
      }

      // If we didn't find a symbol, then move on to the entire module
      // list in the target and get as many unique matches as possible
      if (class_types.Empty()) {
        query.SetFindOne(false);
        m_process->GetTarget().GetImages().FindTypes(nullptr, query, results);
        for (const auto &type_sp : results.GetTypeMap().Types())
          class_types.Insert(type_sp);
      }

      lldb::TypeSP type_sp;
      if (class_types.Empty()) {
        LLDB_LOGF(log, "0x%16.16" PRIx64 ": is not dynamic\n",
                  in_value.GetPointerValue());
        return TypeAndOrName();
      }
      if (class_types.GetSize() == 1) {
        type_sp = class_types.GetTypeAtIndex(0);
        if (type_sp) {
          if (TypeSystemClang::IsCXXClassType(
                  type_sp->GetForwardCompilerType())) {
            LLDB_LOGF(
                log,
                "0x%16.16" PRIx64
                ": static-type = '%s' has dynamic type: uid={0x%" PRIx64
                "}, type-name='%s'\n",
                in_value.GetPointerValue(), in_value.GetTypeName().AsCString(),
                type_sp->GetID(), type_sp->GetName().GetCString());
            type_info.SetTypeSP(type_sp);
          }
        }
      } else {
        size_t i;
        if (log) {
          for (i = 0; i < class_types.GetSize(); i++) {
            type_sp = class_types.GetTypeAtIndex(i);
            if (type_sp) {
              LLDB_LOGF(
                  log,
                  "0x%16.16" PRIx64
                  ": static-type = '%s' has multiple matching dynamic "
                  "types: uid={0x%" PRIx64 "}, type-name='%s'\n",
                  in_value.GetPointerValue(),
                  in_value.GetTypeName().AsCString(),
                  type_sp->GetID(), type_sp->GetName().GetCString());
            }
          }
        }

        for (i = 0; i < class_types.GetSize(); i++) {
          type_sp = class_types.GetTypeAtIndex(i);
          if (type_sp) {
            if (TypeSystemClang::IsCXXClassType(
                    type_sp->GetForwardCompilerType())) {
              LLDB_LOGF(
                  log,
                  "0x%16.16" PRIx64 ": static-type = '%s' has multiple "
                  "matching dynamic types, picking "
                  "this one: uid={0x%" PRIx64 "}, type-name='%s'\n",
                  in_value.GetPointerValue(),
                  in_value.GetTypeName().AsCString(),
                  type_sp->GetID(), type_sp->GetName().GetCString());
              type_info.SetTypeSP(type_sp);
            }
          }
        }

        if (log) {
          LLDB_LOGF(log,
                    "0x%16.16" PRIx64
                    ": static-type = '%s' has multiple matching dynamic "
                    "types, didn't find a C++ match\n",
                    in_value.GetPointerValue(),
                    in_value.GetTypeName().AsCString());
        }
      }
      if (type_info)
        SetDynamicTypeInfo(vtable_info.addr, type_info);
      return type_info;
    }
  }
  return TypeAndOrName();
}

llvm::Error ItaniumABILanguageRuntime::TypeHasVTable(CompilerType type) {
  // Check to make sure the class has a vtable.
  CompilerType original_type = type;
  if (type.IsPointerOrReferenceType()) {
    CompilerType pointee_type = type.GetPointeeType();
    if (pointee_type)
      type = pointee_type;
  }

  // Make sure this is a class or a struct first by checking the type class
  // bitfield that gets returned.
  if ((type.GetTypeClass() & (eTypeClassStruct | eTypeClassClass)) == 0) {
    return llvm::createStringError(std::errc::invalid_argument,
        "type \"%s\" is not a class or struct or a pointer to one",
        original_type.GetTypeName().AsCString("<invalid>"));
  }

  // Check if the type has virtual functions by asking it if it is polymorphic.
  if (!type.IsPolymorphicClass()) {
    return llvm::createStringError(std::errc::invalid_argument,
        "type \"%s\" doesn't have a vtable",
        type.GetTypeName().AsCString("<invalid>"));
  }
  return llvm::Error::success();
}

// This function can accept both pointers or references to classes as well as
// instances of classes. If you are using this function during dynamic type
// detection, only valid ValueObjects that return true to
// CouldHaveDynamicValue(...) should call this function and \a check_type
// should be set to false. This function is also used by ValueObjectVTable
// and is can pass in instances of classes which is not suitable for dynamic
// type detection, these cases should pass true for \a check_type.
llvm::Expected<LanguageRuntime::VTableInfo>
 ItaniumABILanguageRuntime::GetVTableInfo(ValueObject &in_value,
                                          bool check_type) {

  CompilerType type = in_value.GetCompilerType();
  if (check_type) {
    if (llvm::Error err = TypeHasVTable(type))
      return std::move(err);
  }
  ExecutionContext exe_ctx(in_value.GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (process == nullptr)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "invalid process");

  AddressType address_type;
  lldb::addr_t original_ptr = LLDB_INVALID_ADDRESS;
  if (type.IsPointerOrReferenceType())
    original_ptr = in_value.GetPointerValue(&address_type);
  else
    original_ptr = in_value.GetAddressOf(/*scalar_is_load_address=*/true,
                                         &address_type);
  if (original_ptr == LLDB_INVALID_ADDRESS || address_type != eAddressTypeLoad)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "failed to get the address of the value");

  Status error;
  lldb::addr_t vtable_load_addr =
      process->ReadPointerFromMemory(original_ptr, error);

  if (!error.Success() || vtable_load_addr == LLDB_INVALID_ADDRESS)
    return llvm::createStringError(std::errc::invalid_argument,
        "failed to read vtable pointer from memory at 0x%" PRIx64,
        original_ptr);

  // The vtable load address can have authentication bits with
  // AArch64 targets on Darwin.
  vtable_load_addr = process->FixDataAddress(vtable_load_addr);

  // Find the symbol that contains the "vtable_load_addr" address
  Address vtable_addr;
  if (!process->GetTarget().ResolveLoadAddress(vtable_load_addr, vtable_addr))
    return llvm::createStringError(std::errc::invalid_argument,
                                   "failed to resolve vtable pointer 0x%"
                                   PRIx64 "to a section", vtable_load_addr);

  // Check our cache first to see if we already have this info
  {
    std::lock_guard<std::mutex> locker(m_mutex);
    auto pos = m_vtable_info_map.find(vtable_addr);
    if (pos != m_vtable_info_map.end())
      return pos->second;
  }

  Symbol *symbol = vtable_addr.CalculateSymbolContextSymbol();
  if (symbol == nullptr)
    return llvm::createStringError(std::errc::invalid_argument,
                                   "no symbol found for 0x%" PRIx64,
                                   vtable_load_addr);
  llvm::StringRef name = symbol->GetMangled().GetDemangledName().GetStringRef();
  if (name.starts_with(vtable_demangled_prefix)) {
    VTableInfo info = {vtable_addr, symbol};
    std::lock_guard<std::mutex> locker(m_mutex);
    auto pos = m_vtable_info_map[vtable_addr] = info;
    return info;
  }
  return llvm::createStringError(std::errc::invalid_argument,
      "symbol found that contains 0x%" PRIx64 " is not a vtable symbol",
      vtable_load_addr);
}

bool ItaniumABILanguageRuntime::GetDynamicTypeAndAddress(
    ValueObject &in_value, lldb::DynamicValueType use_dynamic,
    TypeAndOrName &class_type_or_name, Address &dynamic_address,
    Value::ValueType &value_type) {
  // For Itanium, if the type has a vtable pointer in the object, it will be at
  // offset 0 in the object.  That will point to the "address point" within the
  // vtable (not the beginning of the vtable.)  We can then look up the symbol
  // containing this "address point" and that symbol's name demangled will
  // contain the full class name. The second pointer above the "address point"
  // is the "offset_to_top".  We'll use that to get the start of the value
  // object which holds the dynamic type.
  //

  class_type_or_name.Clear();
  value_type = Value::ValueType::Scalar;

  if (!CouldHaveDynamicValue(in_value))
    return false;

  // Check if we have a vtable pointer in this value. If we don't it will
  // return an error, else it will return a valid resolved address. We don't
  // want GetVTableInfo to check the type since we accept void * as a possible
  // dynamic type and that won't pass the type check. We already checked the
  // type above in CouldHaveDynamicValue(...).
  llvm::Expected<VTableInfo> vtable_info_or_err =
      GetVTableInfo(in_value, /*check_type=*/false);
  if (!vtable_info_or_err) {
    llvm::consumeError(vtable_info_or_err.takeError());
    return false;
  }

  const VTableInfo &vtable_info = vtable_info_or_err.get();
  class_type_or_name = GetTypeInfo(in_value, vtable_info);

  if (!class_type_or_name)
    return false;

  CompilerType type = class_type_or_name.GetCompilerType();
  // There can only be one type with a given name, so we've just found
  // duplicate definitions, and this one will do as well as any other. We
  // don't consider something to have a dynamic type if it is the same as
  // the static type.  So compare against the value we were handed.
  if (!type)
    return true;

  if (TypeSystemClang::AreTypesSame(in_value.GetCompilerType(), type)) {
    // The dynamic type we found was the same type, so we don't have a
    // dynamic type here...
    return false;
  }

  // The offset_to_top is two pointers above the vtable pointer.
  Target &target = m_process->GetTarget();
  const addr_t vtable_load_addr = vtable_info.addr.GetLoadAddress(&target);
  if (vtable_load_addr == LLDB_INVALID_ADDRESS)
    return false;
  const uint32_t addr_byte_size = m_process->GetAddressByteSize();
  const lldb::addr_t offset_to_top_location =
      vtable_load_addr - 2 * addr_byte_size;
  // Watch for underflow, offset_to_top_location should be less than
  // vtable_load_addr
  if (offset_to_top_location >= vtable_load_addr)
    return false;
  Status error;
  const int64_t offset_to_top = m_process->ReadSignedIntegerFromMemory(
      offset_to_top_location, addr_byte_size, INT64_MIN, error);

  if (offset_to_top == INT64_MIN)
    return false;
  // So the dynamic type is a value that starts at offset_to_top above
  // the original address.
  lldb::addr_t dynamic_addr = in_value.GetPointerValue() + offset_to_top;
  if (!m_process->GetTarget().ResolveLoadAddress(
          dynamic_addr, dynamic_address)) {
    dynamic_address.SetRawAddress(dynamic_addr);
  }
  return true;
}

TypeAndOrName ItaniumABILanguageRuntime::FixUpDynamicType(
    const TypeAndOrName &type_and_or_name, ValueObject &static_value) {
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
    else if (static_type_flags.AllSet(eTypeIsReference))
      corrected_type = orig_type.GetLValueReferenceType();
    ret.SetCompilerType(corrected_type);
  } else {
    // If we are here we need to adjust our dynamic type name to include the
    // correct & or * symbol
    std::string corrected_name(type_and_or_name.GetName().GetCString());
    if (static_type_flags.AllSet(eTypeIsPointer))
      corrected_name.append(" *");
    else if (static_type_flags.AllSet(eTypeIsReference))
      corrected_name.append(" &");
    // the parent type should be a correctly pointer'ed or referenc'ed type
    ret.SetCompilerType(static_type);
    ret.SetName(corrected_name.c_str());
  }
  return ret;
}

// Static Functions
LanguageRuntime *
ItaniumABILanguageRuntime::CreateInstance(Process *process,
                                          lldb::LanguageType language) {
  // FIXME: We have to check the process and make sure we actually know that
  // this process supports
  // the Itanium ABI.
  if (language == eLanguageTypeC_plus_plus ||
      language == eLanguageTypeC_plus_plus_03 ||
      language == eLanguageTypeC_plus_plus_11 ||
      language == eLanguageTypeC_plus_plus_14)
    return new ItaniumABILanguageRuntime(process);
  else
    return nullptr;
}

class CommandObjectMultiwordItaniumABI_Demangle : public CommandObjectParsed {
public:
  CommandObjectMultiwordItaniumABI_Demangle(CommandInterpreter &interpreter)
      : CommandObjectParsed(
            interpreter, "demangle", "Demangle a C++ mangled name.",
            "language cplusplus demangle [<mangled-name> ...]") {
    AddSimpleArgumentList(eArgTypeSymbol, eArgRepeatPlus);
  }

  ~CommandObjectMultiwordItaniumABI_Demangle() override = default;

protected:
  void DoExecute(Args &command, CommandReturnObject &result) override {
    bool demangled_any = false;
    bool error_any = false;
    for (auto &entry : command.entries()) {
      if (entry.ref().empty())
        continue;

      // the actual Mangled class should be strict about this, but on the
      // command line if you're copying mangled names out of 'nm' on Darwin,
      // they will come out with an extra underscore - be willing to strip this
      // on behalf of the user.   This is the moral equivalent of the -_/-n
      // options to c++filt
      auto name = entry.ref();
      if (name.starts_with("__Z"))
        name = name.drop_front();

      Mangled mangled(name);
      if (mangled.GuessLanguage() == lldb::eLanguageTypeC_plus_plus) {
        ConstString demangled(mangled.GetDisplayDemangledName());
        demangled_any = true;
        result.AppendMessageWithFormat("%s ---> %s\n", entry.c_str(),
                                       demangled.GetCString());
      } else {
        error_any = true;
        result.AppendErrorWithFormat("%s is not a valid C++ mangled name\n",
                                     entry.ref().str().c_str());
      }
    }

    result.SetStatus(
        error_any ? lldb::eReturnStatusFailed
                  : (demangled_any ? lldb::eReturnStatusSuccessFinishResult
                                   : lldb::eReturnStatusSuccessFinishNoResult));
  }
};

class CommandObjectMultiwordItaniumABI : public CommandObjectMultiword {
public:
  CommandObjectMultiwordItaniumABI(CommandInterpreter &interpreter)
      : CommandObjectMultiword(
            interpreter, "cplusplus",
            "Commands for operating on the C++ language runtime.",
            "cplusplus <subcommand> [<subcommand-options>]") {
    LoadSubCommand(
        "demangle",
        CommandObjectSP(
            new CommandObjectMultiwordItaniumABI_Demangle(interpreter)));
  }

  ~CommandObjectMultiwordItaniumABI() override = default;
};

void ItaniumABILanguageRuntime::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "Itanium ABI for the C++ language", CreateInstance,
      [](CommandInterpreter &interpreter) -> lldb::CommandObjectSP {
        return CommandObjectSP(
            new CommandObjectMultiwordItaniumABI(interpreter));
      });
}

void ItaniumABILanguageRuntime::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

BreakpointResolverSP ItaniumABILanguageRuntime::CreateExceptionResolver(
    const BreakpointSP &bkpt, bool catch_bp, bool throw_bp) {
  return CreateExceptionResolver(bkpt, catch_bp, throw_bp, false);
}

BreakpointResolverSP ItaniumABILanguageRuntime::CreateExceptionResolver(
    const BreakpointSP &bkpt, bool catch_bp, bool throw_bp,
    bool for_expressions) {
  // One complication here is that most users DON'T want to stop at
  // __cxa_allocate_expression, but until we can do anything better with
  // predicting unwinding the expression parser does.  So we have two forms of
  // the exception breakpoints, one for expressions that leaves out
  // __cxa_allocate_exception, and one that includes it. The
  // SetExceptionBreakpoints does the latter, the CreateExceptionBreakpoint in
  // the runtime the former.
  static const char *g_catch_name = "__cxa_begin_catch";
  static const char *g_throw_name1 = "__cxa_throw";
  static const char *g_throw_name2 = "__cxa_rethrow";
  static const char *g_exception_throw_name = "__cxa_allocate_exception";
  std::vector<const char *> exception_names;
  exception_names.reserve(4);
  if (catch_bp)
    exception_names.push_back(g_catch_name);

  if (throw_bp) {
    exception_names.push_back(g_throw_name1);
    exception_names.push_back(g_throw_name2);
  }

  if (for_expressions)
    exception_names.push_back(g_exception_throw_name);

  BreakpointResolverSP resolver_sp(new BreakpointResolverName(
      bkpt, exception_names.data(), exception_names.size(),
      eFunctionNameTypeBase, eLanguageTypeUnknown, 0, eLazyBoolNo));

  return resolver_sp;
}

lldb::SearchFilterSP ItaniumABILanguageRuntime::CreateExceptionSearchFilter() {
  Target &target = m_process->GetTarget();

  FileSpecList filter_modules;
  if (target.GetArchitecture().GetTriple().getVendor() == llvm::Triple::Apple) {
    // Limit the number of modules that are searched for these breakpoints for
    // Apple binaries.
    filter_modules.EmplaceBack("libc++abi.dylib");
    filter_modules.EmplaceBack("libSystem.B.dylib");
    filter_modules.EmplaceBack("libc++abi.1.0.dylib");
    filter_modules.EmplaceBack("libc++abi.1.dylib");
  }
  return target.GetSearchFilterForModuleList(&filter_modules);
}

lldb::BreakpointSP ItaniumABILanguageRuntime::CreateExceptionBreakpoint(
    bool catch_bp, bool throw_bp, bool for_expressions, bool is_internal) {
  Target &target = m_process->GetTarget();
  FileSpecList filter_modules;
  BreakpointResolverSP exception_resolver_sp =
      CreateExceptionResolver(nullptr, catch_bp, throw_bp, for_expressions);
  SearchFilterSP filter_sp(CreateExceptionSearchFilter());
  const bool hardware = false;
  const bool resolve_indirect_functions = false;
  return target.CreateBreakpoint(filter_sp, exception_resolver_sp, is_internal,
                                 hardware, resolve_indirect_functions);
}

void ItaniumABILanguageRuntime::SetExceptionBreakpoints() {
  if (!m_process)
    return;

  const bool catch_bp = false;
  const bool throw_bp = true;
  const bool is_internal = true;
  const bool for_expressions = true;

  // For the exception breakpoints set by the Expression parser, we'll be a
  // little more aggressive and stop at exception allocation as well.

  if (m_cxx_exception_bp_sp) {
    m_cxx_exception_bp_sp->SetEnabled(true);
  } else {
    m_cxx_exception_bp_sp = CreateExceptionBreakpoint(
        catch_bp, throw_bp, for_expressions, is_internal);
    if (m_cxx_exception_bp_sp)
      m_cxx_exception_bp_sp->SetBreakpointKind("c++ exception");
  }
}

void ItaniumABILanguageRuntime::ClearExceptionBreakpoints() {
  if (!m_process)
    return;

  if (m_cxx_exception_bp_sp) {
    m_cxx_exception_bp_sp->SetEnabled(false);
  }
}

bool ItaniumABILanguageRuntime::ExceptionBreakpointsAreSet() {
  return m_cxx_exception_bp_sp && m_cxx_exception_bp_sp->IsEnabled();
}

bool ItaniumABILanguageRuntime::ExceptionBreakpointsExplainStop(
    lldb::StopInfoSP stop_reason) {
  if (!m_process)
    return false;

  if (!stop_reason || stop_reason->GetStopReason() != eStopReasonBreakpoint)
    return false;

  uint64_t break_site_id = stop_reason->GetValue();
  return m_process->GetBreakpointSiteList().StopPointSiteContainsBreakpoint(
      break_site_id, m_cxx_exception_bp_sp->GetID());
}

ValueObjectSP ItaniumABILanguageRuntime::GetExceptionObjectForThread(
    ThreadSP thread_sp) {
  if (!thread_sp->SafeToCallFunctions())
    return {};

  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(m_process->GetTarget());
  if (!scratch_ts_sp)
    return {};

  CompilerType voidstar =
      scratch_ts_sp->GetBasicType(eBasicTypeVoid).GetPointerType();

  DiagnosticManager diagnostics;
  ExecutionContext exe_ctx;
  EvaluateExpressionOptions options;

  options.SetUnwindOnError(true);
  options.SetIgnoreBreakpoints(true);
  options.SetStopOthers(true);
  options.SetTimeout(m_process->GetUtilityExpressionTimeout());
  options.SetTryAllThreads(false);
  thread_sp->CalculateExecutionContext(exe_ctx);

  const ModuleList &modules = m_process->GetTarget().GetImages();
  SymbolContextList contexts;
  SymbolContext context;

  modules.FindSymbolsWithNameAndType(
      ConstString("__cxa_current_exception_type"), eSymbolTypeCode, contexts);
  contexts.GetContextAtIndex(0, context);
  if (!context.symbol) {
    return {};
  }
  Address addr = context.symbol->GetAddress();

  Status error;
  FunctionCaller *function_caller =
      m_process->GetTarget().GetFunctionCallerForLanguage(
          eLanguageTypeC, voidstar, addr, ValueList(), "caller", error);

  ExpressionResults func_call_ret;
  Value results;
  func_call_ret = function_caller->ExecuteFunction(exe_ctx, nullptr, options,
                                                   diagnostics, results);
  if (func_call_ret != eExpressionCompleted || !error.Success()) {
    return ValueObjectSP();
  }

  size_t ptr_size = m_process->GetAddressByteSize();
  addr_t result_ptr = results.GetScalar().ULongLong(LLDB_INVALID_ADDRESS);
  addr_t exception_addr =
      m_process->ReadPointerFromMemory(result_ptr - ptr_size, error);

  if (!error.Success()) {
    return ValueObjectSP();
  }

  lldb_private::formatters::InferiorSizedWord exception_isw(exception_addr,
                                                            *m_process);
  ValueObjectSP exception = ValueObject::CreateValueObjectFromData(
      "exception", exception_isw.GetAsData(m_process->GetByteOrder()), exe_ctx,
      voidstar);
  ValueObjectSP dyn_exception
      = exception->GetDynamicValue(eDynamicDontRunTarget);
  // If we succeed in making a dynamic value, return that:
  if (dyn_exception)
     return dyn_exception;

  return exception;
}

TypeAndOrName ItaniumABILanguageRuntime::GetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr) {
  std::lock_guard<std::mutex> locker(m_mutex);
  DynamicTypeCache::const_iterator pos = m_dynamic_type_map.find(vtable_addr);
  if (pos == m_dynamic_type_map.end())
    return TypeAndOrName();
  else
    return pos->second;
}

void ItaniumABILanguageRuntime::SetDynamicTypeInfo(
    const lldb_private::Address &vtable_addr, const TypeAndOrName &type_info) {
  std::lock_guard<std::mutex> locker(m_mutex);
  m_dynamic_type_map[vtable_addr] = type_info;
}
