//===-- BreakpointResolverScripted.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverScripted.h"


#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Core/StructuredDataImpl.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

// BreakpointResolverScripted:
BreakpointResolverScripted::BreakpointResolverScripted(
    const BreakpointSP &bkpt, const llvm::StringRef class_name,
    lldb::SearchDepth depth, const StructuredDataImpl &args_data)
    : BreakpointResolver(bkpt, BreakpointResolver::PythonResolver),
      m_class_name(std::string(class_name)), m_depth(depth), m_args(args_data) {
  CreateImplementationIfNeeded(bkpt);
}

void BreakpointResolverScripted::CreateImplementationIfNeeded(
    BreakpointSP breakpoint_sp) {
  if (m_implementation_sp)
    return;

  if (m_class_name.empty())
    return;

  if (!breakpoint_sp)
    return;

  TargetSP target_sp = breakpoint_sp->GetTargetSP();
  ScriptInterpreter *script_interp = target_sp->GetDebugger()
                                              .GetScriptInterpreter();
  if (!script_interp)
    return;

  m_implementation_sp = script_interp->CreateScriptedBreakpointResolver(
      m_class_name.c_str(), m_args, breakpoint_sp);
}

void BreakpointResolverScripted::NotifyBreakpointSet() {
  CreateImplementationIfNeeded(GetBreakpoint());
}

BreakpointResolverSP BreakpointResolverScripted::CreateFromStructuredData(
    const StructuredData::Dictionary &options_dict, Status &error) {
  llvm::StringRef class_name;
  bool success;

  success = options_dict.GetValueForKeyAsString(
      GetKey(OptionNames::PythonClassName), class_name);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find class name entry.");
    return nullptr;
  }
  // The Python function will actually provide the search depth, this is a
  // placeholder.
  lldb::SearchDepth depth = lldb::eSearchDepthTarget;

  StructuredDataImpl args_data_impl;
  StructuredData::Dictionary *args_dict = nullptr;
  if (options_dict.GetValueForKeyAsDictionary(GetKey(OptionNames::ScriptArgs),
                                              args_dict))
    args_data_impl.SetObjectSP(args_dict->shared_from_this());
  return std::make_shared<BreakpointResolverScripted>(nullptr, class_name,
                                                      depth, args_data_impl);
}

StructuredData::ObjectSP
BreakpointResolverScripted::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());

  options_dict_sp->AddStringItem(GetKey(OptionNames::PythonClassName),
                                   m_class_name);
  if (m_args.IsValid())
    options_dict_sp->AddItem(GetKey(OptionNames::ScriptArgs),
                             m_args.GetObjectSP());

  return WrapOptionsDict(options_dict_sp);
}

ScriptInterpreter *BreakpointResolverScripted::GetScriptInterpreter() {
  return GetBreakpoint()->GetTarget().GetDebugger().GetScriptInterpreter();
}

Searcher::CallbackReturn BreakpointResolverScripted::SearchCallback(
    SearchFilter &filter, SymbolContext &context, Address *addr) {
  bool should_continue = true;
  if (!m_implementation_sp)
    return Searcher::eCallbackReturnStop;

  ScriptInterpreter *interp = GetScriptInterpreter();
  should_continue = interp->ScriptedBreakpointResolverSearchCallback(
      m_implementation_sp,
      &context);
  if (should_continue)
    return Searcher::eCallbackReturnContinue;

  return Searcher::eCallbackReturnStop;
}

lldb::SearchDepth
BreakpointResolverScripted::GetDepth() {
  lldb::SearchDepth depth = lldb::eSearchDepthModule;
  if (m_implementation_sp) {
    ScriptInterpreter *interp = GetScriptInterpreter();
    depth = interp->ScriptedBreakpointResolverSearchDepth(
        m_implementation_sp);
  }
  return depth;
}

void BreakpointResolverScripted::GetDescription(Stream *s) {
  StructuredData::GenericSP generic_sp;
  std::string short_help;

  if (m_implementation_sp) {
    ScriptInterpreter *interp = GetScriptInterpreter();
    interp->GetShortHelpForCommandObject(m_implementation_sp,
                                         short_help);
  }
  if (!short_help.empty())
    s->PutCString(short_help.c_str());
  else
    s->Printf("python class = %s", m_class_name.c_str());
}

void BreakpointResolverScripted::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverScripted::CopyForBreakpoint(BreakpointSP &breakpoint) {
  return std::make_shared<BreakpointResolverScripted>(breakpoint, m_class_name,
                                                      m_depth, m_args);
}
