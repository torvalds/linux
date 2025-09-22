//===-- FormatManager.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/FormatManager.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/DataFormatters/LanguageCategory.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "llvm/ADT/STLExtras.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

struct FormatInfo {
  Format format;
  const char format_char;  // One or more format characters that can be used for
                           // this format.
  const char *format_name; // Long format name that can be used to specify the
                           // current format
};

static constexpr FormatInfo g_format_infos[] = {
    {eFormatDefault, '\0', "default"},
    {eFormatBoolean, 'B', "boolean"},
    {eFormatBinary, 'b', "binary"},
    {eFormatBytes, 'y', "bytes"},
    {eFormatBytesWithASCII, 'Y', "bytes with ASCII"},
    {eFormatChar, 'c', "character"},
    {eFormatCharPrintable, 'C', "printable character"},
    {eFormatComplexFloat, 'F', "complex float"},
    {eFormatCString, 's', "c-string"},
    {eFormatDecimal, 'd', "decimal"},
    {eFormatEnum, 'E', "enumeration"},
    {eFormatHex, 'x', "hex"},
    {eFormatHexUppercase, 'X', "uppercase hex"},
    {eFormatFloat, 'f', "float"},
    {eFormatOctal, 'o', "octal"},
    {eFormatOSType, 'O', "OSType"},
    {eFormatUnicode16, 'U', "unicode16"},
    {eFormatUnicode32, '\0', "unicode32"},
    {eFormatUnsigned, 'u', "unsigned decimal"},
    {eFormatPointer, 'p', "pointer"},
    {eFormatVectorOfChar, '\0', "char[]"},
    {eFormatVectorOfSInt8, '\0', "int8_t[]"},
    {eFormatVectorOfUInt8, '\0', "uint8_t[]"},
    {eFormatVectorOfSInt16, '\0', "int16_t[]"},
    {eFormatVectorOfUInt16, '\0', "uint16_t[]"},
    {eFormatVectorOfSInt32, '\0', "int32_t[]"},
    {eFormatVectorOfUInt32, '\0', "uint32_t[]"},
    {eFormatVectorOfSInt64, '\0', "int64_t[]"},
    {eFormatVectorOfUInt64, '\0', "uint64_t[]"},
    {eFormatVectorOfFloat16, '\0', "float16[]"},
    {eFormatVectorOfFloat32, '\0', "float32[]"},
    {eFormatVectorOfFloat64, '\0', "float64[]"},
    {eFormatVectorOfUInt128, '\0', "uint128_t[]"},
    {eFormatComplexInteger, 'I', "complex integer"},
    {eFormatCharArray, 'a', "character array"},
    {eFormatAddressInfo, 'A', "address"},
    {eFormatHexFloat, '\0', "hex float"},
    {eFormatInstruction, 'i', "instruction"},
    {eFormatVoid, 'v', "void"},
    {eFormatUnicode8, 'u', "unicode8"},
};

static_assert((sizeof(g_format_infos) / sizeof(g_format_infos[0])) ==
                  kNumFormats,
              "All formats must have a corresponding info entry.");

static uint32_t g_num_format_infos = std::size(g_format_infos);

static bool GetFormatFromFormatChar(char format_char, Format &format) {
  for (uint32_t i = 0; i < g_num_format_infos; ++i) {
    if (g_format_infos[i].format_char == format_char) {
      format = g_format_infos[i].format;
      return true;
    }
  }
  format = eFormatInvalid;
  return false;
}

static bool GetFormatFromFormatName(llvm::StringRef format_name,
                                    Format &format) {
  uint32_t i;
  for (i = 0; i < g_num_format_infos; ++i) {
    if (format_name.equals_insensitive(g_format_infos[i].format_name)) {
      format = g_format_infos[i].format;
      return true;
    }
  }

  for (i = 0; i < g_num_format_infos; ++i) {
    if (llvm::StringRef(g_format_infos[i].format_name)
            .starts_with_insensitive(format_name)) {
      format = g_format_infos[i].format;
      return true;
    }
  }
  format = eFormatInvalid;
  return false;
}

void FormatManager::Changed() {
  ++m_last_revision;
  m_format_cache.Clear();
  std::lock_guard<std::recursive_mutex> guard(m_language_categories_mutex);
  for (auto &iter : m_language_categories_map) {
    if (iter.second)
      iter.second->GetFormatCache().Clear();
  }
}

bool FormatManager::GetFormatFromCString(const char *format_cstr,
                                         lldb::Format &format) {
  bool success = false;
  if (format_cstr && format_cstr[0]) {
    if (format_cstr[1] == '\0') {
      success = GetFormatFromFormatChar(format_cstr[0], format);
      if (success)
        return true;
    }

    success = GetFormatFromFormatName(format_cstr, format);
  }
  if (!success)
    format = eFormatInvalid;
  return success;
}

char FormatManager::GetFormatAsFormatChar(lldb::Format format) {
  for (uint32_t i = 0; i < g_num_format_infos; ++i) {
    if (g_format_infos[i].format == format)
      return g_format_infos[i].format_char;
  }
  return '\0';
}

const char *FormatManager::GetFormatAsCString(Format format) {
  if (format >= eFormatDefault && format < kNumFormats)
    return g_format_infos[format].format_name;
  return nullptr;
}

void FormatManager::EnableAllCategories() {
  m_categories_map.EnableAllCategories();
  std::lock_guard<std::recursive_mutex> guard(m_language_categories_mutex);
  for (auto &iter : m_language_categories_map) {
    if (iter.second)
      iter.second->Enable();
  }
}

void FormatManager::DisableAllCategories() {
  m_categories_map.DisableAllCategories();
  std::lock_guard<std::recursive_mutex> guard(m_language_categories_mutex);
  for (auto &iter : m_language_categories_map) {
    if (iter.second)
      iter.second->Disable();
  }
}

void FormatManager::GetPossibleMatches(
    ValueObject &valobj, CompilerType compiler_type,
    lldb::DynamicValueType use_dynamic, FormattersMatchVector &entries,
    FormattersMatchCandidate::Flags current_flags, bool root_level) {
  compiler_type = compiler_type.GetTypeForFormatters();
  ConstString type_name(compiler_type.GetTypeName());
  // A ValueObject that couldn't be made correctly won't necessarily have a
  // target.  We aren't going to find a formatter in this case anyway, so we
  // should just exit.
  TargetSP target_sp = valobj.GetTargetSP();
  if (!target_sp)
    return;
  ScriptInterpreter *script_interpreter =
      target_sp->GetDebugger().GetScriptInterpreter();
  if (valobj.GetBitfieldBitSize() > 0) {
    StreamString sstring;
    sstring.Printf("%s:%d", type_name.AsCString(), valobj.GetBitfieldBitSize());
    ConstString bitfieldname(sstring.GetString());
    entries.push_back({bitfieldname, script_interpreter,
                       TypeImpl(compiler_type), current_flags});
  }

  if (!compiler_type.IsMeaninglessWithoutDynamicResolution()) {
    entries.push_back({type_name, script_interpreter, TypeImpl(compiler_type),
                       current_flags});

    ConstString display_type_name(compiler_type.GetTypeName());
    if (display_type_name != type_name)
      entries.push_back({display_type_name, script_interpreter,
                         TypeImpl(compiler_type), current_flags});
  }

  for (bool is_rvalue_ref = true, j = true;
       j && compiler_type.IsReferenceType(nullptr, &is_rvalue_ref); j = false) {
    CompilerType non_ref_type = compiler_type.GetNonReferenceType();
    GetPossibleMatches(valobj, non_ref_type, use_dynamic, entries,
                       current_flags.WithStrippedReference());
    if (non_ref_type.IsTypedefType()) {
      CompilerType deffed_referenced_type = non_ref_type.GetTypedefedType();
      deffed_referenced_type =
          is_rvalue_ref ? deffed_referenced_type.GetRValueReferenceType()
                        : deffed_referenced_type.GetLValueReferenceType();
      // this is not exactly the usual meaning of stripping typedefs
      GetPossibleMatches(
          valobj, deffed_referenced_type,
          use_dynamic, entries, current_flags.WithStrippedTypedef());
    }
  }

  if (compiler_type.IsPointerType()) {
    CompilerType non_ptr_type = compiler_type.GetPointeeType();
    GetPossibleMatches(valobj, non_ptr_type, use_dynamic, entries,
                       current_flags.WithStrippedPointer());
    if (non_ptr_type.IsTypedefType()) {
      CompilerType deffed_pointed_type =
          non_ptr_type.GetTypedefedType().GetPointerType();
      // this is not exactly the usual meaning of stripping typedefs
      GetPossibleMatches(valobj, deffed_pointed_type, use_dynamic, entries,
                         current_flags.WithStrippedTypedef());
    }
  }

  // For arrays with typedef-ed elements, we add a candidate with the typedef
  // stripped.
  uint64_t array_size;
  if (compiler_type.IsArrayType(nullptr, &array_size, nullptr)) {
    ExecutionContext exe_ctx(valobj.GetExecutionContextRef());
    CompilerType element_type = compiler_type.GetArrayElementType(
        exe_ctx.GetBestExecutionContextScope());
    if (element_type.IsTypedefType()) {
      // Get the stripped element type and compute the stripped array type
      // from it.
      CompilerType deffed_array_type =
          element_type.GetTypedefedType().GetArrayType(array_size);
      // this is not exactly the usual meaning of stripping typedefs
      GetPossibleMatches(
          valobj, deffed_array_type,
          use_dynamic, entries, current_flags.WithStrippedTypedef());
    }
  }

  for (lldb::LanguageType language_type :
       GetCandidateLanguages(valobj.GetObjectRuntimeLanguage())) {
    if (Language *language = Language::FindPlugin(language_type)) {
      for (const FormattersMatchCandidate& candidate :
           language->GetPossibleFormattersMatches(valobj, use_dynamic)) {
        entries.push_back(candidate);
      }
    }
  }

  // try to strip typedef chains
  if (compiler_type.IsTypedefType()) {
    CompilerType deffed_type = compiler_type.GetTypedefedType();
    GetPossibleMatches(valobj, deffed_type, use_dynamic, entries,
                       current_flags.WithStrippedTypedef());
  }

  if (root_level) {
    do {
      if (!compiler_type.IsValid())
        break;

      CompilerType unqual_compiler_ast_type =
          compiler_type.GetFullyUnqualifiedType();
      if (!unqual_compiler_ast_type.IsValid())
        break;
      if (unqual_compiler_ast_type.GetOpaqueQualType() !=
          compiler_type.GetOpaqueQualType())
        GetPossibleMatches(valobj, unqual_compiler_ast_type, use_dynamic,
                           entries, current_flags);
    } while (false);

    // if all else fails, go to static type
    if (valobj.IsDynamic()) {
      lldb::ValueObjectSP static_value_sp(valobj.GetStaticValue());
      if (static_value_sp)
        GetPossibleMatches(*static_value_sp.get(),
                           static_value_sp->GetCompilerType(), use_dynamic,
                           entries, current_flags, true);
    }
  }
}

lldb::TypeFormatImplSP
FormatManager::GetFormatForType(lldb::TypeNameSpecifierImplSP type_sp) {
  if (!type_sp)
    return lldb::TypeFormatImplSP();
  lldb::TypeFormatImplSP format_chosen_sp;
  uint32_t num_categories = m_categories_map.GetCount();
  lldb::TypeCategoryImplSP category_sp;
  uint32_t prio_category = UINT32_MAX;
  for (uint32_t category_id = 0; category_id < num_categories; category_id++) {
    category_sp = GetCategoryAtIndex(category_id);
    if (!category_sp->IsEnabled())
      continue;
    lldb::TypeFormatImplSP format_current_sp =
        category_sp->GetFormatForType(type_sp);
    if (format_current_sp &&
        (format_chosen_sp.get() == nullptr ||
         (prio_category > category_sp->GetEnabledPosition()))) {
      prio_category = category_sp->GetEnabledPosition();
      format_chosen_sp = format_current_sp;
    }
  }
  return format_chosen_sp;
}

lldb::TypeSummaryImplSP
FormatManager::GetSummaryForType(lldb::TypeNameSpecifierImplSP type_sp) {
  if (!type_sp)
    return lldb::TypeSummaryImplSP();
  lldb::TypeSummaryImplSP summary_chosen_sp;
  uint32_t num_categories = m_categories_map.GetCount();
  lldb::TypeCategoryImplSP category_sp;
  uint32_t prio_category = UINT32_MAX;
  for (uint32_t category_id = 0; category_id < num_categories; category_id++) {
    category_sp = GetCategoryAtIndex(category_id);
    if (!category_sp->IsEnabled())
      continue;
    lldb::TypeSummaryImplSP summary_current_sp =
        category_sp->GetSummaryForType(type_sp);
    if (summary_current_sp &&
        (summary_chosen_sp.get() == nullptr ||
         (prio_category > category_sp->GetEnabledPosition()))) {
      prio_category = category_sp->GetEnabledPosition();
      summary_chosen_sp = summary_current_sp;
    }
  }
  return summary_chosen_sp;
}

lldb::TypeFilterImplSP
FormatManager::GetFilterForType(lldb::TypeNameSpecifierImplSP type_sp) {
  if (!type_sp)
    return lldb::TypeFilterImplSP();
  lldb::TypeFilterImplSP filter_chosen_sp;
  uint32_t num_categories = m_categories_map.GetCount();
  lldb::TypeCategoryImplSP category_sp;
  uint32_t prio_category = UINT32_MAX;
  for (uint32_t category_id = 0; category_id < num_categories; category_id++) {
    category_sp = GetCategoryAtIndex(category_id);
    if (!category_sp->IsEnabled())
      continue;
    lldb::TypeFilterImplSP filter_current_sp(
        (TypeFilterImpl *)category_sp->GetFilterForType(type_sp).get());
    if (filter_current_sp &&
        (filter_chosen_sp.get() == nullptr ||
         (prio_category > category_sp->GetEnabledPosition()))) {
      prio_category = category_sp->GetEnabledPosition();
      filter_chosen_sp = filter_current_sp;
    }
  }
  return filter_chosen_sp;
}

lldb::ScriptedSyntheticChildrenSP
FormatManager::GetSyntheticForType(lldb::TypeNameSpecifierImplSP type_sp) {
  if (!type_sp)
    return lldb::ScriptedSyntheticChildrenSP();
  lldb::ScriptedSyntheticChildrenSP synth_chosen_sp;
  uint32_t num_categories = m_categories_map.GetCount();
  lldb::TypeCategoryImplSP category_sp;
  uint32_t prio_category = UINT32_MAX;
  for (uint32_t category_id = 0; category_id < num_categories; category_id++) {
    category_sp = GetCategoryAtIndex(category_id);
    if (!category_sp->IsEnabled())
      continue;
    lldb::ScriptedSyntheticChildrenSP synth_current_sp(
        (ScriptedSyntheticChildren *)category_sp->GetSyntheticForType(type_sp)
            .get());
    if (synth_current_sp &&
        (synth_chosen_sp.get() == nullptr ||
         (prio_category > category_sp->GetEnabledPosition()))) {
      prio_category = category_sp->GetEnabledPosition();
      synth_chosen_sp = synth_current_sp;
    }
  }
  return synth_chosen_sp;
}

void FormatManager::ForEachCategory(TypeCategoryMap::ForEachCallback callback) {
  m_categories_map.ForEach(callback);
  std::lock_guard<std::recursive_mutex> guard(m_language_categories_mutex);
  for (const auto &entry : m_language_categories_map) {
    if (auto category_sp = entry.second->GetCategory()) {
      if (!callback(category_sp))
        break;
    }
  }
}

lldb::TypeCategoryImplSP
FormatManager::GetCategory(ConstString category_name, bool can_create) {
  if (!category_name)
    return GetCategory(m_default_category_name);
  lldb::TypeCategoryImplSP category;
  if (m_categories_map.Get(category_name, category))
    return category;

  if (!can_create)
    return lldb::TypeCategoryImplSP();

  m_categories_map.Add(
      category_name,
      lldb::TypeCategoryImplSP(new TypeCategoryImpl(this, category_name)));
  return GetCategory(category_name);
}

lldb::Format FormatManager::GetSingleItemFormat(lldb::Format vector_format) {
  switch (vector_format) {
  case eFormatVectorOfChar:
    return eFormatCharArray;

  case eFormatVectorOfSInt8:
  case eFormatVectorOfSInt16:
  case eFormatVectorOfSInt32:
  case eFormatVectorOfSInt64:
    return eFormatDecimal;

  case eFormatVectorOfUInt8:
  case eFormatVectorOfUInt16:
  case eFormatVectorOfUInt32:
  case eFormatVectorOfUInt64:
  case eFormatVectorOfUInt128:
    return eFormatHex;

  case eFormatVectorOfFloat16:
  case eFormatVectorOfFloat32:
  case eFormatVectorOfFloat64:
    return eFormatFloat;

  default:
    return lldb::eFormatInvalid;
  }
}

bool FormatManager::ShouldPrintAsOneLiner(ValueObject &valobj) {
  TargetSP target_sp = valobj.GetTargetSP();
  // if settings say no oneline whatsoever
  if (target_sp && !target_sp->GetDebugger().GetAutoOneLineSummaries())
    return false; // then don't oneline

  // if this object has a summary, then ask the summary
  if (valobj.GetSummaryFormat().get() != nullptr)
    return valobj.GetSummaryFormat()->IsOneLiner();

  const size_t max_num_children =
      (target_sp ? *target_sp : Target::GetGlobalProperties())
          .GetMaximumNumberOfChildrenToDisplay();
  auto num_children = valobj.GetNumChildren(max_num_children);
  if (!num_children) {
    llvm::consumeError(num_children.takeError());
    return true;
  }
  // no children, no party
  if (*num_children == 0)
    return false;

  // ask the type if it has any opinion about this eLazyBoolCalculate == no
  // opinion; other values should be self explanatory
  CompilerType compiler_type(valobj.GetCompilerType());
  if (compiler_type.IsValid()) {
    switch (compiler_type.ShouldPrintAsOneLiner(&valobj)) {
    case eLazyBoolNo:
      return false;
    case eLazyBoolYes:
      return true;
    case eLazyBoolCalculate:
      break;
    }
  }

  size_t total_children_name_len = 0;

  for (size_t idx = 0; idx < *num_children; idx++) {
    bool is_synth_val = false;
    ValueObjectSP child_sp(valobj.GetChildAtIndex(idx));
    // something is wrong here - bail out
    if (!child_sp)
      return false;

    // also ask the child's type if it has any opinion
    CompilerType child_compiler_type(child_sp->GetCompilerType());
    if (child_compiler_type.IsValid()) {
      switch (child_compiler_type.ShouldPrintAsOneLiner(child_sp.get())) {
      case eLazyBoolYes:
      // an opinion of yes is only binding for the child, so keep going
      case eLazyBoolCalculate:
        break;
      case eLazyBoolNo:
        // but if the child says no, then it's a veto on the whole thing
        return false;
      }
    }

    // if we decided to define synthetic children for a type, we probably care
    // enough to show them, but avoid nesting children in children
    if (child_sp->GetSyntheticChildren().get() != nullptr) {
      ValueObjectSP synth_sp(child_sp->GetSyntheticValue());
      // wait.. wat? just get out of here..
      if (!synth_sp)
        return false;
      // but if we only have them to provide a value, keep going
      if (!synth_sp->MightHaveChildren() &&
          synth_sp->DoesProvideSyntheticValue())
        is_synth_val = true;
      else
        return false;
    }

    total_children_name_len += child_sp->GetName().GetLength();

    // 50 itself is a "randomly" chosen number - the idea is that
    // overly long structs should not get this treatment
    // FIXME: maybe make this a user-tweakable setting?
    if (total_children_name_len > 50)
      return false;

    // if a summary is there..
    if (child_sp->GetSummaryFormat()) {
      // and it wants children, then bail out
      if (child_sp->GetSummaryFormat()->DoesPrintChildren(child_sp.get()))
        return false;
    }

    // if this child has children..
    if (child_sp->HasChildren()) {
      // ...and no summary...
      // (if it had a summary and the summary wanted children, we would have
      // bailed out anyway
      //  so this only makes us bail out if this has no summary and we would
      //  then print children)
      if (!child_sp->GetSummaryFormat() && !is_synth_val) // but again only do
                                                          // that if not a
                                                          // synthetic valued
                                                          // child
        return false;                                     // then bail out
    }
  }
  return true;
}

ConstString FormatManager::GetTypeForCache(ValueObject &valobj,
                                           lldb::DynamicValueType use_dynamic) {
  ValueObjectSP valobj_sp = valobj.GetQualifiedRepresentationIfAvailable(
      use_dynamic, valobj.IsSynthetic());
  if (valobj_sp && valobj_sp->GetCompilerType().IsValid()) {
    if (!valobj_sp->GetCompilerType().IsMeaninglessWithoutDynamicResolution())
      return valobj_sp->GetQualifiedTypeName();
  }
  return ConstString();
}

std::vector<lldb::LanguageType>
FormatManager::GetCandidateLanguages(lldb::LanguageType lang_type) {
  switch (lang_type) {
  case lldb::eLanguageTypeC:
  case lldb::eLanguageTypeC89:
  case lldb::eLanguageTypeC99:
  case lldb::eLanguageTypeC11:
  case lldb::eLanguageTypeC_plus_plus:
  case lldb::eLanguageTypeC_plus_plus_03:
  case lldb::eLanguageTypeC_plus_plus_11:
  case lldb::eLanguageTypeC_plus_plus_14:
    return {lldb::eLanguageTypeC_plus_plus, lldb::eLanguageTypeObjC};
  default:
    return {lang_type};
  }
  llvm_unreachable("Fully covered switch");
}

LanguageCategory *
FormatManager::GetCategoryForLanguage(lldb::LanguageType lang_type) {
  std::lock_guard<std::recursive_mutex> guard(m_language_categories_mutex);
  auto iter = m_language_categories_map.find(lang_type),
       end = m_language_categories_map.end();
  if (iter != end)
    return iter->second.get();
  LanguageCategory *lang_category = new LanguageCategory(lang_type);
  m_language_categories_map[lang_type] =
      LanguageCategory::UniquePointer(lang_category);
  return lang_category;
}

template <typename ImplSP>
ImplSP FormatManager::GetHardcoded(FormattersMatchData &match_data) {
  ImplSP retval_sp;
  for (lldb::LanguageType lang_type : match_data.GetCandidateLanguages()) {
    if (LanguageCategory *lang_category = GetCategoryForLanguage(lang_type)) {
      if (lang_category->GetHardcoded(*this, match_data, retval_sp))
        return retval_sp;
    }
  }
  return retval_sp;
}

namespace {
template <typename ImplSP> const char *FormatterKind;
template <> const char *FormatterKind<lldb::TypeFormatImplSP> = "format";
template <> const char *FormatterKind<lldb::TypeSummaryImplSP> = "summary";
template <> const char *FormatterKind<lldb::SyntheticChildrenSP> = "synthetic";
} // namespace

#define FORMAT_LOG(Message) "[%s] " Message, FormatterKind<ImplSP>

template <typename ImplSP>
ImplSP FormatManager::Get(ValueObject &valobj,
                          lldb::DynamicValueType use_dynamic) {
  FormattersMatchData match_data(valobj, use_dynamic);
  if (ImplSP retval_sp = GetCached<ImplSP>(match_data))
    return retval_sp;

  Log *log = GetLog(LLDBLog::DataFormatters);

  LLDB_LOGF(log, FORMAT_LOG("Search failed. Giving language a chance."));
  for (lldb::LanguageType lang_type : match_data.GetCandidateLanguages()) {
    if (LanguageCategory *lang_category = GetCategoryForLanguage(lang_type)) {
      ImplSP retval_sp;
      if (lang_category->Get(match_data, retval_sp))
        if (retval_sp) {
          LLDB_LOGF(log, FORMAT_LOG("Language search success. Returning."));
          return retval_sp;
        }
    }
  }

  LLDB_LOGF(log, FORMAT_LOG("Search failed. Giving hardcoded a chance."));
  return GetHardcoded<ImplSP>(match_data);
}

template <typename ImplSP>
ImplSP FormatManager::GetCached(FormattersMatchData &match_data) {
  ImplSP retval_sp;
  Log *log = GetLog(LLDBLog::DataFormatters);
  if (match_data.GetTypeForCache()) {
    LLDB_LOGF(log, "\n\n" FORMAT_LOG("Looking into cache for type %s"),
              match_data.GetTypeForCache().AsCString("<invalid>"));
    if (m_format_cache.Get(match_data.GetTypeForCache(), retval_sp)) {
      if (log) {
        LLDB_LOGF(log, FORMAT_LOG("Cache search success. Returning."));
        LLDB_LOGV(log, "Cache hits: {0} - Cache Misses: {1}",
                  m_format_cache.GetCacheHits(),
                  m_format_cache.GetCacheMisses());
      }
      return retval_sp;
    }
    LLDB_LOGF(log, FORMAT_LOG("Cache search failed. Going normal route"));
  }

  m_categories_map.Get(match_data, retval_sp);
  if (match_data.GetTypeForCache() && (!retval_sp || !retval_sp->NonCacheable())) {
    LLDB_LOGF(log, FORMAT_LOG("Caching %p for type %s"),
              static_cast<void *>(retval_sp.get()),
              match_data.GetTypeForCache().AsCString("<invalid>"));
    m_format_cache.Set(match_data.GetTypeForCache(), retval_sp);
  }
  LLDB_LOGV(log, "Cache hits: {0} - Cache Misses: {1}",
            m_format_cache.GetCacheHits(), m_format_cache.GetCacheMisses());
  return retval_sp;
}

#undef FORMAT_LOG

lldb::TypeFormatImplSP
FormatManager::GetFormat(ValueObject &valobj,
                         lldb::DynamicValueType use_dynamic) {
  return Get<lldb::TypeFormatImplSP>(valobj, use_dynamic);
}

lldb::TypeSummaryImplSP
FormatManager::GetSummaryFormat(ValueObject &valobj,
                                lldb::DynamicValueType use_dynamic) {
  return Get<lldb::TypeSummaryImplSP>(valobj, use_dynamic);
}

lldb::SyntheticChildrenSP
FormatManager::GetSyntheticChildren(ValueObject &valobj,
                                    lldb::DynamicValueType use_dynamic) {
  return Get<lldb::SyntheticChildrenSP>(valobj, use_dynamic);
}

FormatManager::FormatManager()
    : m_last_revision(0), m_format_cache(), m_language_categories_mutex(),
      m_language_categories_map(), m_named_summaries_map(this),
      m_categories_map(this), m_default_category_name(ConstString("default")),
      m_system_category_name(ConstString("system")),
      m_vectortypes_category_name(ConstString("VectorTypes")) {
  LoadSystemFormatters();
  LoadVectorFormatters();

  EnableCategory(m_vectortypes_category_name, TypeCategoryMap::Last,
                 lldb::eLanguageTypeObjC_plus_plus);
  EnableCategory(m_system_category_name, TypeCategoryMap::Last,
                 lldb::eLanguageTypeObjC_plus_plus);
}

void FormatManager::LoadSystemFormatters() {
  TypeSummaryImpl::Flags string_flags;
  string_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(false)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  TypeSummaryImpl::Flags string_array_flags;
  string_array_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(true)
      .SetShowMembersOneLiner(false)
      .SetHideItemNames(false);

  lldb::TypeSummaryImplSP string_format(
      new StringSummaryFormat(string_flags, "${var%s}"));

  lldb::TypeSummaryImplSP string_array_format(
      new StringSummaryFormat(string_array_flags, "${var%char[]}"));

  TypeCategoryImpl::SharedPointer sys_category_sp =
      GetCategory(m_system_category_name);

  sys_category_sp->AddTypeSummary(R"(^(unsigned )?char ?(\*|\[\])$)",
                                  eFormatterMatchRegex, string_format);

  sys_category_sp->AddTypeSummary(R"(^((un)?signed )?char ?\[[0-9]+\]$)",
                                  eFormatterMatchRegex, string_array_format);

  lldb::TypeSummaryImplSP ostype_summary(
      new StringSummaryFormat(TypeSummaryImpl::Flags()
                                  .SetCascades(false)
                                  .SetSkipPointers(true)
                                  .SetSkipReferences(true)
                                  .SetDontShowChildren(true)
                                  .SetDontShowValue(false)
                                  .SetShowMembersOneLiner(false)
                                  .SetHideItemNames(false),
                              "${var%O}"));

  sys_category_sp->AddTypeSummary("OSType", eFormatterMatchExact,
                                  ostype_summary);

  TypeFormatImpl::Flags fourchar_flags;
  fourchar_flags.SetCascades(true).SetSkipPointers(true).SetSkipReferences(
      true);

  AddFormat(sys_category_sp, lldb::eFormatOSType, "FourCharCode",
            fourchar_flags);
}

void FormatManager::LoadVectorFormatters() {
  TypeCategoryImpl::SharedPointer vectors_category_sp =
      GetCategory(m_vectortypes_category_name);

  TypeSummaryImpl::Flags vector_flags;
  vector_flags.SetCascades(true)
      .SetSkipPointers(true)
      .SetSkipReferences(false)
      .SetDontShowChildren(true)
      .SetDontShowValue(false)
      .SetShowMembersOneLiner(true)
      .SetHideItemNames(true);

  AddStringSummary(vectors_category_sp, "${var.uint128}", "builtin_type_vec128",
                   vector_flags);
  AddStringSummary(vectors_category_sp, "", "float[4]", vector_flags);
  AddStringSummary(vectors_category_sp, "", "int32_t[4]", vector_flags);
  AddStringSummary(vectors_category_sp, "", "int16_t[8]", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vDouble", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vFloat", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vSInt8", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vSInt16", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vSInt32", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vUInt16", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vUInt8", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vUInt16", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vUInt32", vector_flags);
  AddStringSummary(vectors_category_sp, "", "vBool32", vector_flags);
}
