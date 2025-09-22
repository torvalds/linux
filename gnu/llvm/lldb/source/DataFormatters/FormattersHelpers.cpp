//===-- FormattersHelpers.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//




#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Core/Module.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RegularExpression.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

void lldb_private::formatters::AddFormat(
    TypeCategoryImpl::SharedPointer category_sp, lldb::Format format,
    llvm::StringRef type_name, TypeFormatImpl::Flags flags, bool regex) {
  lldb::TypeFormatImplSP format_sp(new TypeFormatImpl_Format(format, flags));

  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeFormat(type_name, match_type, format_sp);
}

void lldb_private::formatters::AddSummary(
    TypeCategoryImpl::SharedPointer category_sp, TypeSummaryImplSP summary_sp,
    llvm::StringRef type_name, bool regex) {
  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeSummary(type_name, match_type, summary_sp);
}

void lldb_private::formatters::AddStringSummary(
    TypeCategoryImpl::SharedPointer category_sp, const char *string,
    llvm::StringRef type_name, TypeSummaryImpl::Flags flags, bool regex) {
  lldb::TypeSummaryImplSP summary_sp(new StringSummaryFormat(flags, string));

  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeSummary(type_name, match_type, summary_sp);
}

void lldb_private::formatters::AddOneLineSummary(
    TypeCategoryImpl::SharedPointer category_sp, llvm::StringRef type_name,
    TypeSummaryImpl::Flags flags, bool regex) {
  flags.SetShowMembersOneLiner(true);
  lldb::TypeSummaryImplSP summary_sp(new StringSummaryFormat(flags, ""));

  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeSummary(type_name, match_type, summary_sp);
}

void lldb_private::formatters::AddCXXSummary(
    TypeCategoryImpl::SharedPointer category_sp,
    CXXFunctionSummaryFormat::Callback funct, const char *description,
    llvm::StringRef type_name, TypeSummaryImpl::Flags flags, bool regex) {
  lldb::TypeSummaryImplSP summary_sp(
      new CXXFunctionSummaryFormat(flags, funct, description));

  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeSummary(type_name, match_type, summary_sp);
}

void lldb_private::formatters::AddCXXSynthetic(
    TypeCategoryImpl::SharedPointer category_sp,
    CXXSyntheticChildren::CreateFrontEndCallback generator,
    const char *description, llvm::StringRef type_name,
    ScriptedSyntheticChildren::Flags flags, bool regex) {
  lldb::SyntheticChildrenSP synth_sp(
      new CXXSyntheticChildren(flags, description, generator));
  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeSynthetic(type_name, match_type, synth_sp);
}

void lldb_private::formatters::AddFilter(
    TypeCategoryImpl::SharedPointer category_sp,
    std::vector<std::string> children, const char *description,
    llvm::StringRef type_name, ScriptedSyntheticChildren::Flags flags,
    bool regex) {
  TypeFilterImplSP filter_sp(new TypeFilterImpl(flags));
  for (auto child : children)
    filter_sp->AddExpressionPath(child);
  FormatterMatchType match_type =
      regex ? eFormatterMatchRegex : eFormatterMatchExact;
  category_sp->AddTypeFilter(type_name, match_type, filter_sp);
}

size_t lldb_private::formatters::ExtractIndexFromString(const char *item_name) {
  if (!item_name || !*item_name)
    return UINT32_MAX;
  if (*item_name != '[')
    return UINT32_MAX;
  item_name++;
  char *endptr = nullptr;
  unsigned long int idx = ::strtoul(item_name, &endptr, 0);
  if (idx == 0 && endptr == item_name)
    return UINT32_MAX;
  if (idx == ULONG_MAX)
    return UINT32_MAX;
  return idx;
}

Address
lldb_private::formatters::GetArrayAddressOrPointerValue(ValueObject &valobj) {
  lldb::addr_t data_addr = LLDB_INVALID_ADDRESS;
  AddressType type;

  if (valobj.IsPointerType())
    data_addr = valobj.GetPointerValue(&type);
  else if (valobj.IsArrayType())
    data_addr = valobj.GetAddressOf(/*scalar_is_load_address=*/true, &type);
  if (data_addr != LLDB_INVALID_ADDRESS && type == eAddressTypeFile)
    return Address(data_addr, valobj.GetModule()->GetSectionList());

  return data_addr;
}
