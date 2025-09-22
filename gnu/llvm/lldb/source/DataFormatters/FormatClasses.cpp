//===-- FormatClasses.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/DataFormatters/FormatClasses.h"

#include "lldb/DataFormatters/FormatManager.h"





using namespace lldb;
using namespace lldb_private;

FormattersMatchData::FormattersMatchData(ValueObject &valobj,
                                         lldb::DynamicValueType use_dynamic)
    : m_valobj(valobj), m_dynamic_value_type(use_dynamic),
      m_formatters_match_vector({}, false), m_type_for_cache(),
      m_candidate_languages() {
  m_type_for_cache = FormatManager::GetTypeForCache(valobj, use_dynamic);
  m_candidate_languages =
      FormatManager::GetCandidateLanguages(valobj.GetObjectRuntimeLanguage());
}

FormattersMatchVector FormattersMatchData::GetMatchesVector() {
  if (!m_formatters_match_vector.second) {
    m_formatters_match_vector.second = true;
    m_formatters_match_vector.first =
        FormatManager::GetPossibleMatches(m_valobj, m_dynamic_value_type);
  }
  return m_formatters_match_vector.first;
}

ConstString FormattersMatchData::GetTypeForCache() { return m_type_for_cache; }

CandidateLanguagesVector FormattersMatchData::GetCandidateLanguages() {
  return m_candidate_languages;
}

ValueObject &FormattersMatchData::GetValueObject() { return m_valobj; }

lldb::DynamicValueType FormattersMatchData::GetDynamicValueType() {
  return m_dynamic_value_type;
}
