//===-- FormatClasses.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_FORMATCLASSES_H
#define LLDB_DATAFORMATTERS_FORMATCLASSES_H

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "lldb/DataFormatters/TypeFormat.h"
#include "lldb/DataFormatters/TypeSummary.h"
#include "lldb/DataFormatters/TypeSynthetic.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class HardcodedFormatters {
public:
  template <typename FormatterType>
  using HardcodedFormatterFinder =
      std::function<typename FormatterType::SharedPointer(
          lldb_private::ValueObject &, lldb::DynamicValueType,
          FormatManager &)>;

  template <typename FormatterType>
  using HardcodedFormatterFinders =
      std::vector<HardcodedFormatterFinder<FormatterType>>;

  typedef HardcodedFormatterFinders<TypeFormatImpl> HardcodedFormatFinder;
  typedef HardcodedFormatterFinders<TypeSummaryImpl> HardcodedSummaryFinder;
  typedef HardcodedFormatterFinders<SyntheticChildren> HardcodedSyntheticFinder;
};

class FormattersMatchCandidate {
public:
  // Contains flags to indicate how this candidate was generated (e.g. if
  // typedefs were stripped, or pointers were skipped). These are later compared
  // to flags in formatters to confirm a string match.
  struct Flags {
    bool stripped_pointer = false;
    bool stripped_reference = false;
    bool stripped_typedef = false;

    // Returns a copy of this with the "stripped pointer" flag set.
    Flags WithStrippedPointer() {
      Flags result(*this);
      result.stripped_pointer = true;
      return result;
    }

    // Returns a copy of this with the "stripped reference" flag set.
    Flags WithStrippedReference() {
      Flags result(*this);
      result.stripped_reference = true;
      return result;
    }

    // Returns a copy of this with the "stripped typedef" flag set.
    Flags WithStrippedTypedef() {
      Flags result(*this);
      result.stripped_typedef = true;
      return result;
    }
  };

  FormattersMatchCandidate(ConstString name,
                           ScriptInterpreter *script_interpreter, TypeImpl type,
                           Flags flags)
      : m_type_name(name), m_script_interpreter(script_interpreter),
        m_type(type), m_flags(flags) {}

  ~FormattersMatchCandidate() = default;

  ConstString GetTypeName() const { return m_type_name; }

  TypeImpl GetType() const { return m_type; }

  ScriptInterpreter *GetScriptInterpreter() const {
    return m_script_interpreter;
  }

  bool DidStripPointer() const { return m_flags.stripped_pointer; }

  bool DidStripReference() const { return m_flags.stripped_reference; }

  bool DidStripTypedef() const { return m_flags.stripped_typedef; }

  template <class Formatter>
  bool IsMatch(const std::shared_ptr<Formatter> &formatter_sp) const {
    if (!formatter_sp)
      return false;
    if (formatter_sp->Cascades() == false && DidStripTypedef())
      return false;
    if (formatter_sp->SkipsPointers() && DidStripPointer())
      return false;
    if (formatter_sp->SkipsReferences() && DidStripReference())
      return false;
    return true;
  }

private:
  ConstString m_type_name;
  // If a formatter provides a matching callback function, we need the script
  // interpreter and the type object (as an argument to the callback).
  ScriptInterpreter *m_script_interpreter;
  TypeImpl m_type;
  Flags m_flags;
};

typedef std::vector<FormattersMatchCandidate> FormattersMatchVector;
typedef std::vector<lldb::LanguageType> CandidateLanguagesVector;

class FormattersMatchData {
public:
  FormattersMatchData(ValueObject &, lldb::DynamicValueType);

  FormattersMatchVector GetMatchesVector();

  ConstString GetTypeForCache();

  CandidateLanguagesVector GetCandidateLanguages();

  ValueObject &GetValueObject();

  lldb::DynamicValueType GetDynamicValueType();

private:
  ValueObject &m_valobj;
  lldb::DynamicValueType m_dynamic_value_type;
  std::pair<FormattersMatchVector, bool> m_formatters_match_vector;
  ConstString m_type_for_cache;
  CandidateLanguagesVector m_candidate_languages;
};

class TypeNameSpecifierImpl {
public:
  TypeNameSpecifierImpl() = default;

  TypeNameSpecifierImpl(llvm::StringRef name,
                        lldb::FormatterMatchType match_type)
      : m_match_type(match_type) {
    m_type.m_type_name = std::string(name);
  }

  // if constructing with a given type, we consider that a case of exact match.
  TypeNameSpecifierImpl(lldb::TypeSP type)
      : m_match_type(lldb::eFormatterMatchExact) {
    if (type) {
      m_type.m_type_name = std::string(type->GetName().GetStringRef());
      m_type.m_compiler_type = type->GetForwardCompilerType();
    }
  }

  TypeNameSpecifierImpl(CompilerType type)
      : m_match_type(lldb::eFormatterMatchExact) {
    if (type.IsValid()) {
      m_type.m_type_name.assign(type.GetTypeName().GetCString());
      m_type.m_compiler_type = type;
    }
  }

  const char *GetName() {
    if (m_type.m_type_name.size())
      return m_type.m_type_name.c_str();
    return nullptr;
  }

  CompilerType GetCompilerType() {
    if (m_type.m_compiler_type.IsValid())
      return m_type.m_compiler_type;
    return CompilerType();
  }

  lldb::FormatterMatchType GetMatchType() { return m_match_type; }

  bool IsRegex() { return m_match_type == lldb::eFormatterMatchRegex; }

private:
  lldb::FormatterMatchType m_match_type = lldb::eFormatterMatchExact;
  // TODO: Replace this with TypeAndOrName.
  struct TypeOrName {
    std::string m_type_name;
    CompilerType m_compiler_type;
  };
  TypeOrName m_type;

  TypeNameSpecifierImpl(const TypeNameSpecifierImpl &) = delete;
  const TypeNameSpecifierImpl &
  operator=(const TypeNameSpecifierImpl &) = delete;
};

} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_FORMATCLASSES_H
