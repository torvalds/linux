//===-- TypeCategory.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/TypeCategory.h"
#include "lldb/Target/Language.h"


using namespace lldb;
using namespace lldb_private;

TypeCategoryImpl::TypeCategoryImpl(IFormatChangeListener *clist,
                                   ConstString name)
    : m_format_cont(clist), m_summary_cont(clist), m_filter_cont(clist),
      m_synth_cont(clist), m_enabled(false), m_change_listener(clist),
      m_mutex(), m_name(name), m_languages() {}

static bool IsApplicable(lldb::LanguageType category_lang,
                         lldb::LanguageType valobj_lang) {
  switch (category_lang) {
  // Unless we know better, allow only exact equality.
  default:
    return category_lang == valobj_lang;

  // the C family, we consider it as one
  case eLanguageTypeC89:
  case eLanguageTypeC:
  case eLanguageTypeC99:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99;

  // ObjC knows about C and itself
  case eLanguageTypeObjC:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 || valobj_lang == eLanguageTypeObjC;

  // C++ knows about C and C++
  case eLanguageTypeC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus;

  // ObjC++ knows about C,C++,ObjC and ObjC++
  case eLanguageTypeObjC_plus_plus:
    return valobj_lang == eLanguageTypeC89 || valobj_lang == eLanguageTypeC ||
           valobj_lang == eLanguageTypeC99 ||
           valobj_lang == eLanguageTypeC_plus_plus ||
           valobj_lang == eLanguageTypeObjC;

  // Categories with unspecified language match everything.
  case eLanguageTypeUnknown:
    return true;
  }
}

bool TypeCategoryImpl::IsApplicable(lldb::LanguageType lang) {
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType category_lang = GetLanguageAtIndex(idx);
    if (::IsApplicable(category_lang, lang))
      return true;
  }
  return false;
}

size_t TypeCategoryImpl::GetNumLanguages() {
  if (m_languages.empty())
    return 1;
  return m_languages.size();
}

lldb::LanguageType TypeCategoryImpl::GetLanguageAtIndex(size_t idx) {
  if (m_languages.empty())
    return lldb::eLanguageTypeUnknown;
  return m_languages[idx];
}

void TypeCategoryImpl::AddLanguage(lldb::LanguageType lang) {
  m_languages.push_back(lang);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::TypeFormatImplSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;
  return m_format_cont.Get(candidates, entry);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::TypeSummaryImplSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;
  return m_summary_cont.Get(candidates, entry);
}

bool TypeCategoryImpl::Get(lldb::LanguageType lang,
                           const FormattersMatchVector &candidates,
                           lldb::SyntheticChildrenSP &entry) {
  if (!IsEnabled() || !IsApplicable(lang))
    return false;

  // first find both Filter and Synth, and then check which is most recent
  bool pick_synth = false;

  TypeFilterImpl::SharedPointer filter_sp;
  m_filter_cont.Get(candidates, filter_sp);

  ScriptedSyntheticChildren::SharedPointer synth_sp;
  m_synth_cont.Get(candidates, synth_sp);

  if (!filter_sp.get() && !synth_sp.get())
    return false;
  else if (!filter_sp.get() && synth_sp.get())
    pick_synth = true;
  else if (filter_sp.get() && !synth_sp.get())
    pick_synth = false;
  else /*if (filter_sp.get() && synth_sp.get())*/
  {
    pick_synth = filter_sp->GetRevision() <= synth_sp->GetRevision();
  }

  if (pick_synth) {
    entry = synth_sp;
    return true;
  } else {
    entry = filter_sp;
    return true;
  }
  return false;
}

void TypeCategoryImpl::Clear(FormatCategoryItems items) {
  if (items & eFormatCategoryItemFormat)
    m_format_cont.Clear();

  if (items & eFormatCategoryItemSummary)
    m_summary_cont.Clear();

  if (items & eFormatCategoryItemFilter)
    m_filter_cont.Clear();

  if (items & eFormatCategoryItemSynth)
    m_synth_cont.Clear();
}

bool TypeCategoryImpl::Delete(ConstString name, FormatCategoryItems items) {
  bool success = false;

  if (items & eFormatCategoryItemFormat)
    success = m_format_cont.Delete(name) || success;

  if (items & eFormatCategoryItemSummary)
    success = m_summary_cont.Delete(name) || success;

  if (items & eFormatCategoryItemFilter)
    success = m_filter_cont.Delete(name) || success;

  if (items & eFormatCategoryItemSynth)
    success = m_synth_cont.Delete(name) || success;

  return success;
}

uint32_t TypeCategoryImpl::GetCount(FormatCategoryItems items) {
  uint32_t count = 0;

  if (items & eFormatCategoryItemFormat)
    count += m_format_cont.GetCount();

  if (items & eFormatCategoryItemSummary)
    count += m_summary_cont.GetCount();

  if (items & eFormatCategoryItemFilter)
    count += m_filter_cont.GetCount();

  if (items & eFormatCategoryItemSynth)
    count += m_synth_cont.GetCount();

  return count;
}

bool TypeCategoryImpl::AnyMatches(
    const FormattersMatchCandidate &candidate_type, FormatCategoryItems items,
    bool only_enabled, const char **matching_category,
    FormatCategoryItems *matching_type) {
  if (!IsEnabled() && only_enabled)
    return false;

  if (items & eFormatCategoryItemFormat) {
    if (m_format_cont.AnyMatches(candidate_type)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemFormat;
      return true;
    }
  }

  if (items & eFormatCategoryItemSummary) {
    if (m_summary_cont.AnyMatches(candidate_type)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSummary;
      return true;
    }
  }

  if (items & eFormatCategoryItemFilter) {
    if (m_filter_cont.AnyMatches(candidate_type)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemFilter;
      return true;
    }
  }

  if (items & eFormatCategoryItemSynth) {
    if (m_synth_cont.AnyMatches(candidate_type)) {
      if (matching_category)
        *matching_category = m_name.GetCString();
      if (matching_type)
        *matching_type = eFormatCategoryItemSynth;
      return true;
    }
  }

  return false;
}

void TypeCategoryImpl::AutoComplete(CompletionRequest &request,
                                    FormatCategoryItems items) {
  if (items & eFormatCategoryItemFormat)
    m_format_cont.AutoComplete(request);
  if (items & eFormatCategoryItemSummary)
    m_summary_cont.AutoComplete(request);
  if (items & eFormatCategoryItemFilter)
    m_filter_cont.AutoComplete(request);
  if (items & eFormatCategoryItemSynth)
    m_synth_cont.AutoComplete(request);
}

TypeCategoryImpl::FormatContainer::MapValueType
TypeCategoryImpl::GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp) {
  return m_format_cont.GetForTypeNameSpecifier(type_sp);
}

TypeCategoryImpl::SummaryContainer::MapValueType
TypeCategoryImpl::GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp) {
  return m_summary_cont.GetForTypeNameSpecifier(type_sp);
}

TypeCategoryImpl::FilterContainer::MapValueType
TypeCategoryImpl::GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp) {
  return m_filter_cont.GetForTypeNameSpecifier(type_sp);
}

TypeCategoryImpl::SynthContainer::MapValueType
TypeCategoryImpl::GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp) {
  return m_synth_cont.GetForTypeNameSpecifier(type_sp);
}

TypeCategoryImpl::FormatContainer::MapValueType
TypeCategoryImpl::GetFormatAtIndex(size_t index) {
  return m_format_cont.GetAtIndex(index);
}

TypeCategoryImpl::SummaryContainer::MapValueType
TypeCategoryImpl::GetSummaryAtIndex(size_t index) {
  return m_summary_cont.GetAtIndex(index);
}

TypeCategoryImpl::FilterContainer::MapValueType
TypeCategoryImpl::GetFilterAtIndex(size_t index) {
  return m_filter_cont.GetAtIndex(index);
}

TypeCategoryImpl::SynthContainer::MapValueType
TypeCategoryImpl::GetSyntheticAtIndex(size_t index) {
  return m_synth_cont.GetAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFormatAtIndex(size_t index) {
  return m_format_cont.GetTypeNameSpecifierAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSummaryAtIndex(size_t index) {
  return m_summary_cont.GetTypeNameSpecifierAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForFilterAtIndex(size_t index) {
  return m_filter_cont.GetTypeNameSpecifierAtIndex(index);
}

lldb::TypeNameSpecifierImplSP
TypeCategoryImpl::GetTypeNameSpecifierForSyntheticAtIndex(size_t index) {
  return m_synth_cont.GetTypeNameSpecifierAtIndex(index);
}

void TypeCategoryImpl::Enable(bool value, uint32_t position) {
  std::lock_guard<std::recursive_mutex> guard(m_mutex);
  if ((m_enabled = value))
    m_enabled_position = position;
  if (m_change_listener)
    m_change_listener->Changed();
}

std::string TypeCategoryImpl::GetDescription() {
  StreamString stream;
  stream.Printf("%s (%s", GetName(), (IsEnabled() ? "enabled" : "disabled"));
  StreamString lang_stream;
  lang_stream.Printf(", applicable for language(s): ");
  bool print_lang = false;
  for (size_t idx = 0; idx < GetNumLanguages(); idx++) {
    const lldb::LanguageType lang = GetLanguageAtIndex(idx);
    if (lang != lldb::eLanguageTypeUnknown)
      print_lang = true;
    lang_stream.Printf("%s%s", Language::GetNameForLanguageType(lang),
                       idx + 1 < GetNumLanguages() ? ", " : "");
  }
  if (print_lang)
    stream.PutCString(lang_stream.GetString());
  stream.PutChar(')');
  return std::string(stream.GetString());
}
