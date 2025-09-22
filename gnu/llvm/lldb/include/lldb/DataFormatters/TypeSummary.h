//===-- TypeSummary.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_TYPESUMMARY_H
#define LLDB_DATAFORMATTERS_TYPESUMMARY_H

#include <cstdint>

#include <functional>
#include <memory>
#include <string>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/FormatEntity.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {
class TypeSummaryOptions {
public:
  TypeSummaryOptions();

  ~TypeSummaryOptions() = default;

  lldb::LanguageType GetLanguage() const;

  lldb::TypeSummaryCapping GetCapping() const;

  TypeSummaryOptions &SetLanguage(lldb::LanguageType);

  TypeSummaryOptions &SetCapping(lldb::TypeSummaryCapping);

private:
  lldb::LanguageType m_lang = lldb::eLanguageTypeUnknown;
  lldb::TypeSummaryCapping m_capping = lldb::eTypeSummaryCapped;
};

class TypeSummaryImpl {
public:
  enum class Kind { eSummaryString, eScript, eCallback, eInternal };

  virtual ~TypeSummaryImpl() = default;

  Kind GetKind() const { return m_kind; }

  class Flags {
  public:
    Flags() = default;

    Flags(const Flags &other) : m_flags(other.m_flags) {}

    Flags(uint32_t value) : m_flags(value) {}

    Flags &operator=(const Flags &rhs) {
      if (&rhs != this)
        m_flags = rhs.m_flags;

      return *this;
    }

    Flags &operator=(const uint32_t &rhs) {
      m_flags = rhs;
      return *this;
    }

    Flags &Clear() {
      m_flags = 0;
      return *this;
    }

    bool GetCascades() const {
      return (m_flags & lldb::eTypeOptionCascade) == lldb::eTypeOptionCascade;
    }

    Flags &SetCascades(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionCascade;
      else
        m_flags &= ~lldb::eTypeOptionCascade;
      return *this;
    }

    bool GetSkipPointers() const {
      return (m_flags & lldb::eTypeOptionSkipPointers) ==
             lldb::eTypeOptionSkipPointers;
    }

    Flags &SetSkipPointers(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionSkipPointers;
      else
        m_flags &= ~lldb::eTypeOptionSkipPointers;
      return *this;
    }

    bool GetSkipReferences() const {
      return (m_flags & lldb::eTypeOptionSkipReferences) ==
             lldb::eTypeOptionSkipReferences;
    }

    Flags &SetSkipReferences(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionSkipReferences;
      else
        m_flags &= ~lldb::eTypeOptionSkipReferences;
      return *this;
    }

    bool GetDontShowChildren() const {
      return (m_flags & lldb::eTypeOptionHideChildren) ==
             lldb::eTypeOptionHideChildren;
    }

    Flags &SetDontShowChildren(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionHideChildren;
      else
        m_flags &= ~lldb::eTypeOptionHideChildren;
      return *this;
    }

    bool GetHideEmptyAggregates() const {
      return (m_flags & lldb::eTypeOptionHideEmptyAggregates) ==
             lldb::eTypeOptionHideEmptyAggregates;
    }

    Flags &SetHideEmptyAggregates(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionHideEmptyAggregates;
      else
        m_flags &= ~lldb::eTypeOptionHideEmptyAggregates;
      return *this;
    }

    bool GetDontShowValue() const {
      return (m_flags & lldb::eTypeOptionHideValue) ==
             lldb::eTypeOptionHideValue;
    }

    Flags &SetDontShowValue(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionHideValue;
      else
        m_flags &= ~lldb::eTypeOptionHideValue;
      return *this;
    }

    bool GetShowMembersOneLiner() const {
      return (m_flags & lldb::eTypeOptionShowOneLiner) ==
             lldb::eTypeOptionShowOneLiner;
    }

    Flags &SetShowMembersOneLiner(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionShowOneLiner;
      else
        m_flags &= ~lldb::eTypeOptionShowOneLiner;
      return *this;
    }

    bool GetHideItemNames() const {
      return (m_flags & lldb::eTypeOptionHideNames) ==
             lldb::eTypeOptionHideNames;
    }

    Flags &SetHideItemNames(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionHideNames;
      else
        m_flags &= ~lldb::eTypeOptionHideNames;
      return *this;
    }

    bool GetNonCacheable() const {
      return (m_flags & lldb::eTypeOptionNonCacheable) ==
             lldb::eTypeOptionNonCacheable;
    }

    Flags &SetNonCacheable(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionNonCacheable;
      else
        m_flags &= ~lldb::eTypeOptionNonCacheable;
      return *this;
    }

    uint32_t GetValue() { return m_flags; }

    void SetValue(uint32_t value) { m_flags = value; }

  private:
    uint32_t m_flags = lldb::eTypeOptionCascade;
  };

  bool Cascades() const { return m_flags.GetCascades(); }

  bool SkipsPointers() const { return m_flags.GetSkipPointers(); }

  bool SkipsReferences() const { return m_flags.GetSkipReferences(); }

  bool NonCacheable() const { return m_flags.GetNonCacheable(); }

  virtual bool DoesPrintChildren(ValueObject *valobj) const {
    return !m_flags.GetDontShowChildren();
  }

  virtual bool DoesPrintEmptyAggregates() const {
    return !m_flags.GetHideEmptyAggregates();
  }

  virtual bool DoesPrintValue(ValueObject *valobj) const {
    return !m_flags.GetDontShowValue();
  }

  bool IsOneLiner() const { return m_flags.GetShowMembersOneLiner(); }

  virtual bool HideNames(ValueObject *valobj) const {
    return m_flags.GetHideItemNames();
  }

  void SetCascades(bool value) { m_flags.SetCascades(value); }

  void SetSkipsPointers(bool value) { m_flags.SetSkipPointers(value); }

  void SetSkipsReferences(bool value) { m_flags.SetSkipReferences(value); }

  virtual void SetDoesPrintChildren(bool value) {
    m_flags.SetDontShowChildren(!value);
  }

  virtual void SetDoesPrintValue(bool value) {
    m_flags.SetDontShowValue(!value);
  }

  void SetIsOneLiner(bool value) { m_flags.SetShowMembersOneLiner(value); }

  virtual void SetHideNames(bool value) { m_flags.SetHideItemNames(value); }

  virtual void SetNonCacheable(bool value) { m_flags.SetNonCacheable(value); }

  uint32_t GetOptions() { return m_flags.GetValue(); }

  void SetOptions(uint32_t value) { m_flags.SetValue(value); }

  // we are using a ValueObject* instead of a ValueObjectSP because we do not
  // need to hold on to this for extended periods of time and we trust the
  // ValueObject to stay around for as long as it is required for us to
  // generate its summary
  virtual bool FormatObject(ValueObject *valobj, std::string &dest,
                            const TypeSummaryOptions &options) = 0;

  virtual std::string GetDescription() = 0;

  uint32_t &GetRevision() { return m_my_revision; }

  typedef std::shared_ptr<TypeSummaryImpl> SharedPointer;

protected:
  uint32_t m_my_revision = 0;
  Flags m_flags;

  TypeSummaryImpl(Kind kind, const TypeSummaryImpl::Flags &flags);

private:
  Kind m_kind;
  TypeSummaryImpl(const TypeSummaryImpl &) = delete;
  const TypeSummaryImpl &operator=(const TypeSummaryImpl &) = delete;
};

// simple string-based summaries, using ${var to show data
struct StringSummaryFormat : public TypeSummaryImpl {
  std::string m_format_str;
  FormatEntity::Entry m_format;
  Status m_error;

  StringSummaryFormat(const TypeSummaryImpl::Flags &flags, const char *f);

  ~StringSummaryFormat() override = default;

  const char *GetSummaryString() const { return m_format_str.c_str(); }

  void SetSummaryString(const char *f);

  bool FormatObject(ValueObject *valobj, std::string &dest,
                    const TypeSummaryOptions &options) override;

  std::string GetDescription() override;

  static bool classof(const TypeSummaryImpl *S) {
    return S->GetKind() == Kind::eSummaryString;
  }

private:
  StringSummaryFormat(const StringSummaryFormat &) = delete;
  const StringSummaryFormat &operator=(const StringSummaryFormat &) = delete;
};

// summaries implemented via a C++ function
struct CXXFunctionSummaryFormat : public TypeSummaryImpl {
  // we should convert these to SBValue and SBStream if we ever cross the
  // boundary towards the external world
  typedef std::function<bool(ValueObject &, Stream &,
                             const TypeSummaryOptions &)>
      Callback;

  Callback m_impl;
  std::string m_description;

  CXXFunctionSummaryFormat(const TypeSummaryImpl::Flags &flags, Callback impl,
                           const char *description);

  ~CXXFunctionSummaryFormat() override = default;

  Callback GetBackendFunction() const { return m_impl; }

  const char *GetTextualInfo() const { return m_description.c_str(); }

  void SetBackendFunction(Callback cb_func) { m_impl = std::move(cb_func); }

  void SetTextualInfo(const char *descr) {
    if (descr)
      m_description.assign(descr);
    else
      m_description.clear();
  }

  bool FormatObject(ValueObject *valobj, std::string &dest,
                    const TypeSummaryOptions &options) override;

  std::string GetDescription() override;

  static bool classof(const TypeSummaryImpl *S) {
    return S->GetKind() == Kind::eCallback;
  }

  typedef std::shared_ptr<CXXFunctionSummaryFormat> SharedPointer;

private:
  CXXFunctionSummaryFormat(const CXXFunctionSummaryFormat &) = delete;
  const CXXFunctionSummaryFormat &
  operator=(const CXXFunctionSummaryFormat &) = delete;
};

// Python-based summaries, running script code to show data
struct ScriptSummaryFormat : public TypeSummaryImpl {
  std::string m_function_name;
  std::string m_python_script;
  StructuredData::ObjectSP m_script_function_sp;

  ScriptSummaryFormat(const TypeSummaryImpl::Flags &flags,
                      const char *function_name,
                      const char *python_script = nullptr);

  ~ScriptSummaryFormat() override = default;

  const char *GetFunctionName() const { return m_function_name.c_str(); }

  const char *GetPythonScript() const { return m_python_script.c_str(); }

  void SetFunctionName(const char *function_name) {
    if (function_name)
      m_function_name.assign(function_name);
    else
      m_function_name.clear();
    m_python_script.clear();
  }

  void SetPythonScript(const char *script) {
    if (script)
      m_python_script.assign(script);
    else
      m_python_script.clear();
  }

  bool FormatObject(ValueObject *valobj, std::string &dest,
                    const TypeSummaryOptions &options) override;

  std::string GetDescription() override;

  static bool classof(const TypeSummaryImpl *S) {
    return S->GetKind() == Kind::eScript;
  }

  typedef std::shared_ptr<ScriptSummaryFormat> SharedPointer;

private:
  ScriptSummaryFormat(const ScriptSummaryFormat &) = delete;
  const ScriptSummaryFormat &operator=(const ScriptSummaryFormat &) = delete;
};
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_TYPESUMMARY_H
