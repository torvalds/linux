//===-- BreakpointResolverFileRegex.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointResolverFileRegex.h"

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/SourceManager.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

using namespace lldb;
using namespace lldb_private;

// BreakpointResolverFileRegex:
BreakpointResolverFileRegex::BreakpointResolverFileRegex(
    const lldb::BreakpointSP &bkpt, RegularExpression regex,
    const std::unordered_set<std::string> &func_names, bool exact_match)
    : BreakpointResolver(bkpt, BreakpointResolver::FileRegexResolver),
      m_regex(std::move(regex)), m_exact_match(exact_match),
      m_function_names(func_names) {}

BreakpointResolverSP BreakpointResolverFileRegex::CreateFromStructuredData(
    const StructuredData::Dictionary &options_dict, Status &error) {
  bool success;

  llvm::StringRef regex_string;
  success = options_dict.GetValueForKeyAsString(
      GetKey(OptionNames::RegexString), regex_string);
  if (!success) {
    error.SetErrorString("BRFR::CFSD: Couldn't find regex entry.");
    return nullptr;
  }
  RegularExpression regex(regex_string);

  bool exact_match;
  success = options_dict.GetValueForKeyAsBoolean(
      GetKey(OptionNames::ExactMatch), exact_match);
  if (!success) {
    error.SetErrorString("BRFL::CFSD: Couldn't find exact match entry.");
    return nullptr;
  }

  // The names array is optional:
  std::unordered_set<std::string> names_set;
  StructuredData::Array *names_array;
  success = options_dict.GetValueForKeyAsArray(
      GetKey(OptionNames::SymbolNameArray), names_array);
  if (success && names_array) {
    size_t num_names = names_array->GetSize();
    for (size_t i = 0; i < num_names; i++) {
      std::optional<llvm::StringRef> maybe_name =
          names_array->GetItemAtIndexAsString(i);
      if (!maybe_name) {
        error.SetErrorStringWithFormat(
            "BRFR::CFSD: Malformed element %zu in the names array.", i);
        return nullptr;
      }
      names_set.insert(std::string(*maybe_name));
    }
  }

  return std::make_shared<BreakpointResolverFileRegex>(
      nullptr, std::move(regex), names_set, exact_match);
}

StructuredData::ObjectSP
BreakpointResolverFileRegex::SerializeToStructuredData() {
  StructuredData::DictionarySP options_dict_sp(
      new StructuredData::Dictionary());

  options_dict_sp->AddStringItem(GetKey(OptionNames::RegexString),
                                 m_regex.GetText());
  options_dict_sp->AddBooleanItem(GetKey(OptionNames::ExactMatch),
                                  m_exact_match);
  if (!m_function_names.empty()) {
    StructuredData::ArraySP names_array_sp(new StructuredData::Array());
    for (std::string name : m_function_names) {
      StructuredData::StringSP item(new StructuredData::String(name));
      names_array_sp->AddItem(item);
    }
    options_dict_sp->AddItem(GetKey(OptionNames::LineNumber), names_array_sp);
  }

  return WrapOptionsDict(options_dict_sp);
}

Searcher::CallbackReturn BreakpointResolverFileRegex::SearchCallback(
    SearchFilter &filter, SymbolContext &context, Address *addr) {

  if (!context.target_sp)
    return eCallbackReturnContinue;

  CompileUnit *cu = context.comp_unit;
  FileSpec cu_file_spec = cu->GetPrimaryFile();
  std::vector<uint32_t> line_matches;
  context.target_sp->GetSourceManager().FindLinesMatchingRegex(
      cu_file_spec, m_regex, 1, UINT32_MAX, line_matches);

  uint32_t num_matches = line_matches.size();
  for (uint32_t i = 0; i < num_matches; i++) {
    SymbolContextList sc_list;
    // TODO: Handle SourceLocationSpec column information
    SourceLocationSpec location_spec(cu_file_spec, line_matches[i],
                                     /*column=*/std::nullopt,
                                     /*check_inlines=*/false, m_exact_match);
    cu->ResolveSymbolContext(location_spec, eSymbolContextEverything, sc_list);
    // Find all the function names:
    if (!m_function_names.empty()) {
      std::vector<size_t> sc_to_remove;
      for (size_t i = 0; i < sc_list.GetSize(); i++) {
        SymbolContext sc_ctx;
        sc_list.GetContextAtIndex(i, sc_ctx);
        std::string name(
            sc_ctx
                .GetFunctionName(
                    Mangled::NamePreference::ePreferDemangledWithoutArguments)
                .AsCString());
        if (!m_function_names.count(name)) {
          sc_to_remove.push_back(i);
        }
      }

      if (!sc_to_remove.empty()) {
        std::vector<size_t>::reverse_iterator iter;
        std::vector<size_t>::reverse_iterator rend = sc_to_remove.rend();
        for (iter = sc_to_remove.rbegin(); iter != rend; iter++) {
          sc_list.RemoveContextAtIndex(*iter);
        }
      }
    }

    const bool skip_prologue = true;

    BreakpointResolver::SetSCMatchesByLine(filter, sc_list, skip_prologue,
                                           m_regex.GetText());
  }

  return Searcher::eCallbackReturnContinue;
}

lldb::SearchDepth BreakpointResolverFileRegex::GetDepth() {
  return lldb::eSearchDepthCompUnit;
}

void BreakpointResolverFileRegex::GetDescription(Stream *s) {
  s->Printf("source regex = \"%s\", exact_match = %d",
            m_regex.GetText().str().c_str(), m_exact_match);
}

void BreakpointResolverFileRegex::Dump(Stream *s) const {}

lldb::BreakpointResolverSP
BreakpointResolverFileRegex::CopyForBreakpoint(BreakpointSP &breakpoint) {
  lldb::BreakpointResolverSP ret_sp(new BreakpointResolverFileRegex(
      breakpoint, m_regex, m_function_names, m_exact_match));
  return ret_sp;
}

void BreakpointResolverFileRegex::AddFunctionName(const char *func_name) {
  m_function_names.insert(func_name);
}
