//===-- CPPLanguageRuntime.cpp---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstring>

#include <memory>

#include "CPPLanguageRuntime.h"

#include "llvm/ADT/StringRef.h"

#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/UniqueCStringMap.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Target/ABI.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/SectionLoadList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/ThreadPlanRunToAddress.h"
#include "lldb/Target/ThreadPlanStepInRange.h"
#include "lldb/Utility/Timer.h"

using namespace lldb;
using namespace lldb_private;

static ConstString g_this = ConstString("this");
// Artificial coroutine-related variables emitted by clang.
static ConstString g_promise = ConstString("__promise");
static ConstString g_coro_frame = ConstString("__coro_frame");

char CPPLanguageRuntime::ID = 0;

CPPLanguageRuntime::CPPLanguageRuntime(Process *process)
    : LanguageRuntime(process) {}

bool CPPLanguageRuntime::IsAllowedRuntimeValue(ConstString name) {
  return name == g_this || name == g_promise || name == g_coro_frame;
}

llvm::Error CPPLanguageRuntime::GetObjectDescription(Stream &str,
                                                     ValueObject &object) {
  // C++ has no generic way to do this.
  return llvm::createStringError("C++ does not support object descriptions");
}

llvm::Error
CPPLanguageRuntime::GetObjectDescription(Stream &str, Value &value,
                                         ExecutionContextScope *exe_scope) {
  // C++ has no generic way to do this.
  return llvm::createStringError("C++ does not support object descriptions");
}

bool contains_lambda_identifier(llvm::StringRef &str_ref) {
  return str_ref.contains("$_") || str_ref.contains("'lambda'");
}

CPPLanguageRuntime::LibCppStdFunctionCallableInfo
line_entry_helper(Target &target, const SymbolContext &sc, Symbol *symbol,
                  llvm::StringRef first_template_param_sref,
                  bool has_invoke) {

  CPPLanguageRuntime::LibCppStdFunctionCallableInfo optional_info;

  AddressRange range;
  sc.GetAddressRange(eSymbolContextEverything, 0, false, range);

  Address address = range.GetBaseAddress();

  Address addr;
  if (target.ResolveLoadAddress(address.GetCallableLoadAddress(&target),
                                addr)) {
    LineEntry line_entry;
    addr.CalculateSymbolContextLineEntry(line_entry);

    if (contains_lambda_identifier(first_template_param_sref) || has_invoke) {
      // Case 1 and 2
      optional_info.callable_case = lldb_private::CPPLanguageRuntime::
          LibCppStdFunctionCallableCase::Lambda;
    } else {
      // Case 3
      optional_info.callable_case = lldb_private::CPPLanguageRuntime::
          LibCppStdFunctionCallableCase::CallableObject;
    }

    optional_info.callable_symbol = *symbol;
    optional_info.callable_line_entry = line_entry;
    optional_info.callable_address = addr;
  }

  return optional_info;
}

CPPLanguageRuntime::LibCppStdFunctionCallableInfo
CPPLanguageRuntime::FindLibCppStdFunctionCallableInfo(
    lldb::ValueObjectSP &valobj_sp) {
  LLDB_SCOPED_TIMER();

  LibCppStdFunctionCallableInfo optional_info;

  if (!valobj_sp)
    return optional_info;

  // Member __f_ has type __base*, the contents of which will hold:
  // 1) a vtable entry which may hold type information needed to discover the
  //    lambda being called
  // 2) possibly hold a pointer to the callable object
  // e.g.
  //
  // (lldb) frame var -R  f_display
  // (std::__1::function<void (int)>) f_display = {
  //  __buf_ = {
  //  …
  // }
  //  __f_ = 0x00007ffeefbffa00
  // }
  // (lldb) memory read -fA 0x00007ffeefbffa00
  // 0x7ffeefbffa00: ... `vtable for std::__1::__function::__func<void (*) ...
  // 0x7ffeefbffa08: ... `print_num(int) at std_function_cppreference_exam ...
  //
  // We will be handling five cases below, std::function is wrapping:
  //
  // 1) a lambda we know at compile time. We will obtain the name of the lambda
  //    from the first template pameter from __func's vtable. We will look up
  //    the lambda's operator()() and obtain the line table entry.
  // 2) a lambda we know at runtime. A pointer to the lambdas __invoke method
  //    will be stored after the vtable. We will obtain the lambdas name from
  //    this entry and lookup operator()() and obtain the line table entry.
  // 3) a callable object via operator()(). We will obtain the name of the
  //    object from the first template parameter from __func's vtable. We will
  //    look up the objects operator()() and obtain the line table entry.
  // 4) a member function. A pointer to the function will stored after the
  //    we will obtain the name from this pointer.
  // 5) a free function. A pointer to the function will stored after the vtable
  //    we will obtain the name from this pointer.
  ValueObjectSP member_f_(valobj_sp->GetChildMemberWithName("__f_"));

  if (member_f_) {
    ValueObjectSP sub_member_f_(member_f_->GetChildMemberWithName("__f_"));

    if (sub_member_f_)
        member_f_ = sub_member_f_;
  }

  if (!member_f_)
    return optional_info;

  lldb::addr_t member_f_pointer_value = member_f_->GetValueAsUnsigned(0);

  optional_info.member_f_pointer_value = member_f_pointer_value;

  if (!member_f_pointer_value)
    return optional_info;

  ExecutionContext exe_ctx(valobj_sp->GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();

  if (process == nullptr)
    return optional_info;

  uint32_t address_size = process->GetAddressByteSize();
  Status status;

  // First item pointed to by __f_ should be the pointer to the vtable for
  // a __base object.
  lldb::addr_t vtable_address =
      process->ReadPointerFromMemory(member_f_pointer_value, status);

  if (status.Fail())
    return optional_info;

  lldb::addr_t vtable_address_first_entry =
      process->ReadPointerFromMemory(vtable_address + address_size, status);

  if (status.Fail())
    return optional_info;

  lldb::addr_t address_after_vtable = member_f_pointer_value + address_size;
  // As commented above we may not have a function pointer but if we do we will
  // need it.
  lldb::addr_t possible_function_address =
      process->ReadPointerFromMemory(address_after_vtable, status);

  if (status.Fail())
    return optional_info;

  Target &target = process->GetTarget();

  if (target.GetSectionLoadList().IsEmpty())
    return optional_info;

  Address vtable_first_entry_resolved;

  if (!target.GetSectionLoadList().ResolveLoadAddress(
          vtable_address_first_entry, vtable_first_entry_resolved))
    return optional_info;

  Address vtable_addr_resolved;
  SymbolContext sc;
  Symbol *symbol = nullptr;

  if (!target.GetSectionLoadList().ResolveLoadAddress(vtable_address,
                                                      vtable_addr_resolved))
    return optional_info;

  target.GetImages().ResolveSymbolContextForAddress(
      vtable_addr_resolved, eSymbolContextEverything, sc);
  symbol = sc.symbol;

  if (symbol == nullptr)
    return optional_info;

  llvm::StringRef vtable_name(symbol->GetName().GetStringRef());
  bool found_expected_start_string =
      vtable_name.starts_with("vtable for std::__1::__function::__func<");

  if (!found_expected_start_string)
    return optional_info;

  // Given case 1 or 3 we have a vtable name, we are want to extract the first
  // template parameter
  //
  //  ... __func<main::$_0, std::__1::allocator<main::$_0> ...
  //             ^^^^^^^^^
  //
  // We could see names such as:
  //    main::$_0
  //    Bar::add_num2(int)::'lambda'(int)
  //    Bar
  //
  // We do this by find the first < and , and extracting in between.
  //
  // This covers the case of the lambda known at compile time.
  size_t first_open_angle_bracket = vtable_name.find('<') + 1;
  size_t first_comma = vtable_name.find(',');

  llvm::StringRef first_template_parameter =
      vtable_name.slice(first_open_angle_bracket, first_comma);

  Address function_address_resolved;

  // Setup for cases 2, 4 and 5 we have a pointer to a function after the
  // vtable. We will use a process of elimination to drop through each case
  // and obtain the data we need.
  if (target.GetSectionLoadList().ResolveLoadAddress(
          possible_function_address, function_address_resolved)) {
    target.GetImages().ResolveSymbolContextForAddress(
        function_address_resolved, eSymbolContextEverything, sc);
    symbol = sc.symbol;
  }

  // These conditions are used several times to simplify statements later on.
  bool has_invoke =
      (symbol ? symbol->GetName().GetStringRef().contains("__invoke") : false);
  auto calculate_symbol_context_helper = [](auto &t,
                                            SymbolContextList &sc_list) {
    SymbolContext sc;
    t->CalculateSymbolContext(&sc);
    sc_list.Append(sc);
  };

  // Case 2
  if (has_invoke) {
    SymbolContextList scl;
    calculate_symbol_context_helper(symbol, scl);

    return line_entry_helper(target, scl[0], symbol, first_template_parameter,
                             has_invoke);
  }

  // Case 4 or 5
  if (symbol && !symbol->GetName().GetStringRef().starts_with("vtable for") &&
      !contains_lambda_identifier(first_template_parameter) && !has_invoke) {
    optional_info.callable_case =
        LibCppStdFunctionCallableCase::FreeOrMemberFunction;
    optional_info.callable_address = function_address_resolved;
    optional_info.callable_symbol = *symbol;

    return optional_info;
  }

  std::string func_to_match = first_template_parameter.str();

  auto it = CallableLookupCache.find(func_to_match);
  if (it != CallableLookupCache.end())
    return it->second;

  SymbolContextList scl;

  CompileUnit *vtable_cu =
      vtable_first_entry_resolved.CalculateSymbolContextCompileUnit();
  llvm::StringRef name_to_use = func_to_match;

  // Case 3, we have a callable object instead of a lambda
  //
  // TODO
  // We currently don't support this case a callable object may have multiple
  // operator()() varying on const/non-const and number of arguments and we
  // don't have a way to currently distinguish them so we will bail out now.
  if (!contains_lambda_identifier(name_to_use))
    return optional_info;

  if (vtable_cu && !has_invoke) {
    lldb::FunctionSP func_sp =
        vtable_cu->FindFunction([name_to_use](const FunctionSP &f) {
          auto name = f->GetName().GetStringRef();
          if (name.starts_with(name_to_use) && name.contains("operator"))
            return true;

          return false;
        });

    if (func_sp) {
      calculate_symbol_context_helper(func_sp, scl);
    }
  }

  if (symbol == nullptr)
    return optional_info;

  // Case 1 or 3
  if (scl.GetSize() >= 1) {
    optional_info = line_entry_helper(target, scl[0], symbol,
                                      first_template_parameter, has_invoke);
  }

  CallableLookupCache[func_to_match] = optional_info;

  return optional_info;
}

lldb::ThreadPlanSP
CPPLanguageRuntime::GetStepThroughTrampolinePlan(Thread &thread,
                                                 bool stop_others) {
  ThreadPlanSP ret_plan_sp;

  lldb::addr_t curr_pc = thread.GetRegisterContext()->GetPC();

  TargetSP target_sp(thread.CalculateTarget());

  if (target_sp->GetSectionLoadList().IsEmpty())
    return ret_plan_sp;

  Address pc_addr_resolved;
  SymbolContext sc;
  Symbol *symbol;

  if (!target_sp->GetSectionLoadList().ResolveLoadAddress(curr_pc,
                                                          pc_addr_resolved))
    return ret_plan_sp;

  target_sp->GetImages().ResolveSymbolContextForAddress(
      pc_addr_resolved, eSymbolContextEverything, sc);
  symbol = sc.symbol;

  if (symbol == nullptr)
    return ret_plan_sp;

  llvm::StringRef function_name(symbol->GetName().GetCString());

  // Handling the case where we are attempting to step into std::function.
  // The behavior will be that we will attempt to obtain the wrapped
  // callable via FindLibCppStdFunctionCallableInfo() and if we find it we
  // will return a ThreadPlanRunToAddress to the callable. Therefore we will
  // step into the wrapped callable.
  //
  bool found_expected_start_string =
      function_name.starts_with("std::__1::function<");

  if (!found_expected_start_string)
    return ret_plan_sp;

  AddressRange range_of_curr_func;
  sc.GetAddressRange(eSymbolContextEverything, 0, false, range_of_curr_func);

  StackFrameSP frame = thread.GetStackFrameAtIndex(0);

  if (frame) {
    ValueObjectSP value_sp = frame->FindVariable(g_this);

    CPPLanguageRuntime::LibCppStdFunctionCallableInfo callable_info =
        FindLibCppStdFunctionCallableInfo(value_sp);

    if (callable_info.callable_case != LibCppStdFunctionCallableCase::Invalid &&
        value_sp->GetValueIsValid()) {
      // We found the std::function wrapped callable and we have its address.
      // We now create a ThreadPlan to run to the callable.
      ret_plan_sp = std::make_shared<ThreadPlanRunToAddress>(
          thread, callable_info.callable_address, stop_others);
      return ret_plan_sp;
    } else {
      // We are in std::function but we could not obtain the callable.
      // We create a ThreadPlan to keep stepping through using the address range
      // of the current function.
      ret_plan_sp = std::make_shared<ThreadPlanStepInRange>(
          thread, range_of_curr_func, sc, nullptr, eOnlyThisThread,
          eLazyBoolYes, eLazyBoolYes);
      return ret_plan_sp;
    }
  }

  return ret_plan_sp;
}
