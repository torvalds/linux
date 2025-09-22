//===-- TypeSynthetic.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_DATAFORMATTERS_TYPESYNTHETIC_H
#define LLDB_DATAFORMATTERS_TYPESYNTHETIC_H

#include <cstdint>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Utility/StructuredData.h"

namespace lldb_private {
class SyntheticChildrenFrontEnd {
protected:
  ValueObject &m_backend;

  void SetValid(bool valid) { m_valid = valid; }

  bool IsValid() { return m_valid; }

public:
  SyntheticChildrenFrontEnd(ValueObject &backend)
      : m_backend(backend), m_valid(true) {}

  virtual ~SyntheticChildrenFrontEnd() = default;

  virtual llvm::Expected<uint32_t> CalculateNumChildren() = 0;

  virtual llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) {
    auto count = CalculateNumChildren();
    if (!count)
      return count;
    return *count <= max ? *count : max;
  }

  uint32_t CalculateNumChildrenIgnoringErrors(uint32_t max = UINT32_MAX);

  virtual lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) = 0;

  virtual size_t GetIndexOfChildWithName(ConstString name) = 0;

  /// This function is assumed to always succeed and if it fails, the front-end
  /// should know to deal with it in the correct way (most probably, by refusing
  /// to return any children). The return value of \ref Update should actually
  /// be interpreted as "ValueObjectSyntheticFilter cache is good/bad". If this
  /// function returns \ref lldb::ChildCacheState::eReuse, \ref
  /// ValueObjectSyntheticFilter is allowed to use the children it fetched
  /// previously and cached. Otherwise, \ref ValueObjectSyntheticFilter must
  /// throw away its cache, and query again for children.
  virtual lldb::ChildCacheState Update() = 0;

  // if this function returns false, then CalculateNumChildren() MUST return 0
  // since UI frontends might validly decide not to inquire for children given
  // a false return value from this call if it returns true, then
  // CalculateNumChildren() can return any number >= 0 (0 being valid) it
  // should if at all possible be more efficient than CalculateNumChildren()
  virtual bool MightHaveChildren() = 0;

  // if this function returns a non-null ValueObject, then the returned
  // ValueObject will stand for this ValueObject whenever a "value" request is
  // made to this ValueObject
  virtual lldb::ValueObjectSP GetSyntheticValue() { return nullptr; }

  // if this function returns a non-empty ConstString, then clients are
  // expected to use the return as the name of the type of this ValueObject for
  // display purposes
  virtual ConstString GetSyntheticTypeName() { return ConstString(); }

  typedef std::shared_ptr<SyntheticChildrenFrontEnd> SharedPointer;
  typedef std::unique_ptr<SyntheticChildrenFrontEnd> AutoPointer;

protected:
  lldb::ValueObjectSP
  CreateValueObjectFromExpression(llvm::StringRef name,
                                  llvm::StringRef expression,
                                  const ExecutionContext &exe_ctx);

  lldb::ValueObjectSP
  CreateValueObjectFromAddress(llvm::StringRef name, uint64_t address,
                               const ExecutionContext &exe_ctx,
                               CompilerType type);

  lldb::ValueObjectSP CreateValueObjectFromData(llvm::StringRef name,
                                                const DataExtractor &data,
                                                const ExecutionContext &exe_ctx,
                                                CompilerType type);

private:
  bool m_valid;
  SyntheticChildrenFrontEnd(const SyntheticChildrenFrontEnd &) = delete;
  const SyntheticChildrenFrontEnd &
  operator=(const SyntheticChildrenFrontEnd &) = delete;
};

class SyntheticValueProviderFrontEnd : public SyntheticChildrenFrontEnd {
public:
  SyntheticValueProviderFrontEnd(ValueObject &backend)
      : SyntheticChildrenFrontEnd(backend) {}

  ~SyntheticValueProviderFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override { return 0; }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override { return nullptr; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return UINT32_MAX;
  }

  lldb::ChildCacheState Update() override {
    return lldb::ChildCacheState::eRefetch;
  }

  bool MightHaveChildren() override { return false; }

  lldb::ValueObjectSP GetSyntheticValue() override = 0;

private:
  SyntheticValueProviderFrontEnd(const SyntheticValueProviderFrontEnd &) =
      delete;
  const SyntheticValueProviderFrontEnd &
  operator=(const SyntheticValueProviderFrontEnd &) = delete;
};

class SyntheticChildren {
public:
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

    bool GetFrontEndWantsDereference() const {
      return (m_flags & lldb::eTypeOptionFrontEndWantsDereference) ==
             lldb::eTypeOptionFrontEndWantsDereference;
    }

    Flags &SetFrontEndWantsDereference(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionFrontEndWantsDereference;
      else
        m_flags &= ~lldb::eTypeOptionFrontEndWantsDereference;
      return *this;
    }

    uint32_t GetValue() { return m_flags; }

    void SetValue(uint32_t value) { m_flags = value; }

  private:
    uint32_t m_flags = lldb::eTypeOptionCascade;
  };

  SyntheticChildren(const Flags &flags);

  virtual ~SyntheticChildren();

  bool Cascades() const { return m_flags.GetCascades(); }

  bool SkipsPointers() const { return m_flags.GetSkipPointers(); }

  bool SkipsReferences() const { return m_flags.GetSkipReferences(); }

  bool NonCacheable() const { return m_flags.GetNonCacheable(); }

  bool WantsDereference() const { return m_flags.GetFrontEndWantsDereference();}

  void SetCascades(bool value) { m_flags.SetCascades(value); }

  void SetSkipsPointers(bool value) { m_flags.SetSkipPointers(value); }

  void SetSkipsReferences(bool value) { m_flags.SetSkipReferences(value); }

  void SetNonCacheable(bool value) { m_flags.SetNonCacheable(value); }

  uint32_t GetOptions() { return m_flags.GetValue(); }

  void SetOptions(uint32_t value) { m_flags.SetValue(value); }

  virtual bool IsScripted() = 0;

  virtual std::string GetDescription() = 0;

  virtual SyntheticChildrenFrontEnd::AutoPointer
  GetFrontEnd(ValueObject &backend) = 0;

  typedef std::shared_ptr<SyntheticChildren> SharedPointer;

  uint32_t &GetRevision() { return m_my_revision; }

protected:
  uint32_t m_my_revision = 0;
  Flags m_flags;

private:
  SyntheticChildren(const SyntheticChildren &) = delete;
  const SyntheticChildren &operator=(const SyntheticChildren &) = delete;
};

class TypeFilterImpl : public SyntheticChildren {
  std::vector<std::string> m_expression_paths;

public:
  TypeFilterImpl(const SyntheticChildren::Flags &flags)
      : SyntheticChildren(flags) {}

  TypeFilterImpl(const SyntheticChildren::Flags &flags,
                 const std::initializer_list<const char *> items)
      : SyntheticChildren(flags) {
    for (auto path : items)
      AddExpressionPath(path);
  }

  void AddExpressionPath(const char *path) {
    AddExpressionPath(std::string(path));
  }

  void Clear() { m_expression_paths.clear(); }

  size_t GetCount() const { return m_expression_paths.size(); }

  const char *GetExpressionPathAtIndex(size_t i) const {
    return m_expression_paths[i].c_str();
  }

  bool SetExpressionPathAtIndex(size_t i, const char *path) {
    return SetExpressionPathAtIndex(i, std::string(path));
  }

  void AddExpressionPath(const std::string &path);

  bool SetExpressionPathAtIndex(size_t i, const std::string &path);

  bool IsScripted() override { return false; }

  std::string GetDescription() override;

  class FrontEnd : public SyntheticChildrenFrontEnd {
  public:
    FrontEnd(TypeFilterImpl *flt, ValueObject &backend)
        : SyntheticChildrenFrontEnd(backend), filter(flt) {}

    ~FrontEnd() override = default;

    llvm::Expected<uint32_t> CalculateNumChildren() override {
      return filter->GetCount();
    }

    lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
      if (idx >= filter->GetCount())
        return lldb::ValueObjectSP();
      return m_backend.GetSyntheticExpressionPathChild(
          filter->GetExpressionPathAtIndex(idx), true);
    }

    lldb::ChildCacheState Update() override {
      return lldb::ChildCacheState::eRefetch;
    }

    bool MightHaveChildren() override { return filter->GetCount() > 0; }

    size_t GetIndexOfChildWithName(ConstString name) override;

    typedef std::shared_ptr<SyntheticChildrenFrontEnd> SharedPointer;

  private:
    TypeFilterImpl *filter;

    FrontEnd(const FrontEnd &) = delete;
    const FrontEnd &operator=(const FrontEnd &) = delete;
  };

  SyntheticChildrenFrontEnd::AutoPointer
  GetFrontEnd(ValueObject &backend) override {
    return SyntheticChildrenFrontEnd::AutoPointer(new FrontEnd(this, backend));
  }

  typedef std::shared_ptr<TypeFilterImpl> SharedPointer;

private:
  TypeFilterImpl(const TypeFilterImpl &) = delete;
  const TypeFilterImpl &operator=(const TypeFilterImpl &) = delete;
};

class CXXSyntheticChildren : public SyntheticChildren {
public:
  typedef std::function<SyntheticChildrenFrontEnd *(CXXSyntheticChildren *,
                                                    lldb::ValueObjectSP)>
      CreateFrontEndCallback;
  CXXSyntheticChildren(const SyntheticChildren::Flags &flags,
                       const char *description, CreateFrontEndCallback callback);

  virtual ~CXXSyntheticChildren();

  bool IsScripted() override { return false; }

  std::string GetDescription() override;

  SyntheticChildrenFrontEnd::AutoPointer
  GetFrontEnd(ValueObject &backend) override {
    return SyntheticChildrenFrontEnd::AutoPointer(
        m_create_callback(this, backend.GetSP()));
  }

protected:
  CreateFrontEndCallback m_create_callback;
  std::string m_description;

private:
  CXXSyntheticChildren(const CXXSyntheticChildren &) = delete;
  const CXXSyntheticChildren &operator=(const CXXSyntheticChildren &) = delete;
};

class ScriptedSyntheticChildren : public SyntheticChildren {
  std::string m_python_class;
  std::string m_python_code;

public:
  ScriptedSyntheticChildren(const SyntheticChildren::Flags &flags,
                            const char *pclass, const char *pcode = nullptr)
      : SyntheticChildren(flags) {
    if (pclass)
      m_python_class = pclass;
    if (pcode)
      m_python_code = pcode;
  }

  const char *GetPythonClassName() { return m_python_class.c_str(); }

  const char *GetPythonCode() { return m_python_code.c_str(); }

  void SetPythonClassName(const char *fname) {
    m_python_class.assign(fname);
    m_python_code.clear();
  }

  void SetPythonCode(const char *script) { m_python_code.assign(script); }

  std::string GetDescription() override;

  bool IsScripted() override { return true; }

  class FrontEnd : public SyntheticChildrenFrontEnd {
  public:
    FrontEnd(std::string pclass, ValueObject &backend);

    ~FrontEnd() override;

    bool IsValid();

    llvm::Expected<uint32_t> CalculateNumChildren() override;

    llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

    lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

    lldb::ChildCacheState Update() override;

    bool MightHaveChildren() override;

    size_t GetIndexOfChildWithName(ConstString name) override;

    lldb::ValueObjectSP GetSyntheticValue() override;

    ConstString GetSyntheticTypeName() override;

    typedef std::shared_ptr<SyntheticChildrenFrontEnd> SharedPointer;

  private:
    std::string m_python_class;
    StructuredData::ObjectSP m_wrapper_sp;
    ScriptInterpreter *m_interpreter;

    FrontEnd(const FrontEnd &) = delete;
    const FrontEnd &operator=(const FrontEnd &) = delete;
  };

  SyntheticChildrenFrontEnd::AutoPointer
  GetFrontEnd(ValueObject &backend) override {
    auto synth_ptr = SyntheticChildrenFrontEnd::AutoPointer(
        new FrontEnd(m_python_class, backend));
    if (synth_ptr && ((FrontEnd *)synth_ptr.get())->IsValid())
      return synth_ptr;
    return nullptr;
  }

private:
  ScriptedSyntheticChildren(const ScriptedSyntheticChildren &) = delete;
  const ScriptedSyntheticChildren &
  operator=(const ScriptedSyntheticChildren &) = delete;
};
} // namespace lldb_private

#endif // LLDB_DATAFORMATTERS_TYPESYNTHETIC_H
