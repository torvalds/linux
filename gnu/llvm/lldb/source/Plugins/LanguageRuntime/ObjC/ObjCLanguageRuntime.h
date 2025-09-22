//===-- ObjCLanguageRuntime.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_OBJCLANGUAGERUNTIME_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_OBJCLANGUAGERUNTIME_H

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_set>

#include "llvm/Support/Casting.h"

#include "lldb/Breakpoint/BreakpointPrecondition.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/ThreadSafeDenseMap.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

class CommandObjectObjC_ClassTable_Dump;

namespace lldb_private {

class TypeSystemClang;
class UtilityFunction;

class ObjCLanguageRuntime : public LanguageRuntime {
public:
  enum class ObjCRuntimeVersions {
    eObjC_VersionUnknown = 0,
    eAppleObjC_V1 = 1,
    eAppleObjC_V2 = 2,
    eGNUstep_libobjc2 = 3,
  };

  typedef lldb::addr_t ObjCISA;

  class ClassDescriptor;
  typedef std::shared_ptr<ClassDescriptor> ClassDescriptorSP;

  // the information that we want to support retrieving from an ObjC class this
  // needs to be pure virtual since there are at least 2 different
  // implementations of the runtime, and more might come
  class ClassDescriptor {
  public:
    ClassDescriptor() : m_type_wp() {}

    virtual ~ClassDescriptor() = default;

    virtual ConstString GetClassName() = 0;

    virtual ClassDescriptorSP GetSuperclass() = 0;

    virtual ClassDescriptorSP GetMetaclass() const = 0;

    // virtual if any implementation has some other version-specific rules but
    // for the known v1/v2 this is all that needs to be done
    virtual bool IsKVO() {
      if (m_is_kvo == eLazyBoolCalculate) {
        const char *class_name = GetClassName().AsCString();
        if (class_name && *class_name)
          m_is_kvo =
              (LazyBool)(strstr(class_name, "NSKVONotifying_") == class_name);
      }
      return (m_is_kvo == eLazyBoolYes);
    }

    // virtual if any implementation has some other version-specific rules but
    // for the known v1/v2 this is all that needs to be done
    virtual bool IsCFType() {
      if (m_is_cf == eLazyBoolCalculate) {
        const char *class_name = GetClassName().AsCString();
        if (class_name && *class_name)
          m_is_cf = (LazyBool)(strcmp(class_name, "__NSCFType") == 0 ||
                               strcmp(class_name, "NSCFType") == 0);
      }
      return (m_is_cf == eLazyBoolYes);
    }

    /// Determine whether this class is implemented in Swift.
    virtual lldb::LanguageType GetImplementationLanguage() const {
      return lldb::eLanguageTypeObjC;
    }

    virtual bool IsValid() = 0;

    /// There are two routines in the ObjC runtime that tagged pointer clients
    /// can call to get the value from their tagged pointer, one that retrieves
    /// it as an unsigned value and one a signed value.  These two
    /// GetTaggedPointerInfo methods mirror those two ObjC runtime calls.
    /// @{
    virtual bool GetTaggedPointerInfo(uint64_t *info_bits = nullptr,
                                      uint64_t *value_bits = nullptr,
                                      uint64_t *payload = nullptr) = 0;

    virtual bool GetTaggedPointerInfoSigned(uint64_t *info_bits = nullptr,
                                            int64_t *value_bits = nullptr,
                                            uint64_t *payload = nullptr) = 0;
    /// @}

    virtual uint64_t GetInstanceSize() = 0;

    // use to implement version-specific additional constraints on pointers
    virtual bool CheckPointer(lldb::addr_t value, uint32_t ptr_size) const {
      return true;
    }

    virtual ObjCISA GetISA() = 0;

    // This should return true iff the interface could be completed
    virtual bool
    Describe(std::function<void(ObjCISA)> const &superclass_func,
             std::function<bool(const char *, const char *)> const
                 &instance_method_func,
             std::function<bool(const char *, const char *)> const
                 &class_method_func,
             std::function<bool(const char *, const char *, lldb::addr_t,
                                uint64_t)> const &ivar_func) const {
      return false;
    }

    lldb::TypeSP GetType() { return m_type_wp.lock(); }

    void SetType(const lldb::TypeSP &type_sp) { m_type_wp = type_sp; }

    struct iVarDescriptor {
      ConstString m_name;
      CompilerType m_type;
      uint64_t m_size;
      int32_t m_offset;
    };

    virtual size_t GetNumIVars() { return 0; }

    virtual iVarDescriptor GetIVarAtIndex(size_t idx) {
      return iVarDescriptor();
    }

  protected:
    bool IsPointerValid(lldb::addr_t value, uint32_t ptr_size,
                        bool allow_NULLs = false, bool allow_tagged = false,
                        bool check_version_specific = false) const;

  private:
    LazyBool m_is_kvo = eLazyBoolCalculate;
    LazyBool m_is_cf = eLazyBoolCalculate;
    lldb::TypeWP m_type_wp;
  };

  class EncodingToType {
  public:
    virtual ~EncodingToType();

    virtual CompilerType RealizeType(TypeSystemClang &ast_ctx, const char *name,
                                     bool for_expression) = 0;
    virtual CompilerType RealizeType(const char *name, bool for_expression);

  protected:
    std::shared_ptr<TypeSystemClang> m_scratch_ast_ctx_sp;
  };

  class ObjCExceptionPrecondition : public BreakpointPrecondition {
  public:
    ObjCExceptionPrecondition();

    ~ObjCExceptionPrecondition() override = default;

    bool EvaluatePrecondition(StoppointCallbackContext &context) override;
    void GetDescription(Stream &stream, lldb::DescriptionLevel level) override;
    Status ConfigurePrecondition(Args &args) override;

  protected:
    void AddClassName(const char *class_name);

  private:
    std::unordered_set<std::string> m_class_names;
  };

  static lldb::BreakpointPreconditionSP
  GetBreakpointExceptionPrecondition(lldb::LanguageType language,
                                     bool throw_bp);

  class TaggedPointerVendor {
  public:
    virtual ~TaggedPointerVendor() = default;

    virtual bool IsPossibleTaggedPointer(lldb::addr_t ptr) = 0;

    virtual ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) = 0;

  protected:
    TaggedPointerVendor() = default;

  private:
    TaggedPointerVendor(const TaggedPointerVendor &) = delete;
    const TaggedPointerVendor &operator=(const TaggedPointerVendor &) = delete;
  };

  ~ObjCLanguageRuntime() override;

  static char ID;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || LanguageRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  static ObjCLanguageRuntime *Get(Process &process) {
    return llvm::cast_or_null<ObjCLanguageRuntime>(
        process.GetLanguageRuntime(lldb::eLanguageTypeObjC));
  }

  virtual TaggedPointerVendor *GetTaggedPointerVendor() { return nullptr; }

  typedef std::shared_ptr<EncodingToType> EncodingToTypeSP;

  virtual EncodingToTypeSP GetEncodingToType();

  virtual ClassDescriptorSP GetClassDescriptor(ValueObject &in_value);

  ClassDescriptorSP GetNonKVOClassDescriptor(ValueObject &in_value);

  virtual ClassDescriptorSP
  GetClassDescriptorFromClassName(ConstString class_name);

  virtual ClassDescriptorSP GetClassDescriptorFromISA(ObjCISA isa);

  ClassDescriptorSP GetNonKVOClassDescriptor(ObjCISA isa);

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeObjC;
  }

  virtual bool IsModuleObjCLibrary(const lldb::ModuleSP &module_sp) = 0;

  virtual bool ReadObjCLibrary(const lldb::ModuleSP &module_sp) = 0;

  virtual bool HasReadObjCLibrary() = 0;

  // These two methods actually use different caches.  The only time we'll
  // cache a sel_str is if we found a "selector specific stub" for the selector
  // and conversely we only add to the SEL cache if we saw a regular dispatch.
  lldb::addr_t LookupInMethodCache(lldb::addr_t class_addr, lldb::addr_t sel);
  lldb::addr_t LookupInMethodCache(lldb::addr_t class_addr,
                                   llvm::StringRef sel_str);

  void AddToMethodCache(lldb::addr_t class_addr, lldb::addr_t sel,
                        lldb::addr_t impl_addr);

  void AddToMethodCache(lldb::addr_t class_addr, llvm::StringRef sel_str,
                        lldb::addr_t impl_addr);

  TypeAndOrName LookupInClassNameCache(lldb::addr_t class_addr);

  void AddToClassNameCache(lldb::addr_t class_addr, const char *name,
                           lldb::TypeSP type_sp);

  void AddToClassNameCache(lldb::addr_t class_addr,
                           const TypeAndOrName &class_or_type_name);

  lldb::TypeSP LookupInCompleteClassCache(ConstString &name);

  std::optional<CompilerType> GetRuntimeType(CompilerType base_type) override;

  virtual llvm::Expected<std::unique_ptr<UtilityFunction>>
  CreateObjectChecker(std::string name, ExecutionContext &exe_ctx) = 0;

  virtual ObjCRuntimeVersions GetRuntimeVersion() const {
    return ObjCRuntimeVersions::eObjC_VersionUnknown;
  }

  bool IsValidISA(ObjCISA isa) {
    UpdateISAToDescriptorMap();
    return m_isa_to_descriptor.count(isa) > 0;
  }

  virtual void UpdateISAToDescriptorMapIfNeeded() = 0;

  void UpdateISAToDescriptorMap() {
    if (m_process && m_process->GetStopID() != m_isa_to_descriptor_stop_id) {
      UpdateISAToDescriptorMapIfNeeded();
    }
  }

  virtual ObjCISA GetISA(ConstString name);

  virtual ObjCISA GetParentClass(ObjCISA isa);

  // Finds the byte offset of the child_type ivar in parent_type.  If it can't
  // find the offset, returns LLDB_INVALID_IVAR_OFFSET.

  virtual size_t GetByteOffsetForIvar(CompilerType &parent_qual_type,
                                      const char *ivar_name);

  bool HasNewLiteralsAndIndexing() {
    if (m_has_new_literals_and_indexing == eLazyBoolCalculate) {
      if (CalculateHasNewLiteralsAndIndexing())
        m_has_new_literals_and_indexing = eLazyBoolYes;
      else
        m_has_new_literals_and_indexing = eLazyBoolNo;
    }

    return (m_has_new_literals_and_indexing == eLazyBoolYes);
  }

  void SymbolsDidLoad(const ModuleList &module_list) override {
    m_negative_complete_class_cache.clear();
  }

  std::optional<uint64_t>
  GetTypeBitSize(const CompilerType &compiler_type) override;

  /// Check whether the name is "self" or "_cmd" and should show up in
  /// "frame variable".
  bool IsAllowedRuntimeValue(ConstString name) override;

protected:
  // Classes that inherit from ObjCLanguageRuntime can see and modify these
  ObjCLanguageRuntime(Process *process);

  virtual bool CalculateHasNewLiteralsAndIndexing() { return false; }

  bool ISAIsCached(ObjCISA isa) const {
    return m_isa_to_descriptor.find(isa) != m_isa_to_descriptor.end();
  }

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp) {
    if (isa != 0) {
      m_isa_to_descriptor[isa] = descriptor_sp;
      return true;
    }
    return false;
  }

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp,
                const char *class_name);

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp,
                uint32_t class_name_hash) {
    if (isa != 0) {
      m_isa_to_descriptor[isa] = descriptor_sp;
      m_hash_to_isa_map.insert(std::make_pair(class_name_hash, isa));
      return true;
    }
    return false;
  }

private:
  // We keep two maps of <Class,Selector>->Implementation so we don't have
  // to call the resolver function over and over.
  // The first comes from regular obj_msgSend type dispatch, and maps the
  // class + uniqued SEL value to an implementation.
  // The second comes from the "selector-specific stubs", which are always
  // of the form _objc_msgSend$SelectorName, so we don't know the uniqued
  // selector, only the string name.

  // FIXME: We need to watch for the loading of Protocols, and flush the cache
  // for any
  // class that we see so changed.

  struct ClassAndSel {
    ClassAndSel() = default;

    ClassAndSel(lldb::addr_t in_class_addr, lldb::addr_t in_sel_addr)
        : class_addr(in_class_addr), sel_addr(in_sel_addr) {}

    bool operator==(const ClassAndSel &rhs) {
      if (class_addr == rhs.class_addr && sel_addr == rhs.sel_addr)
        return true;
      else
        return false;
    }

    bool operator<(const ClassAndSel &rhs) const {
      if (class_addr < rhs.class_addr)
        return true;
      else if (class_addr > rhs.class_addr)
        return false;
      else {
        if (sel_addr < rhs.sel_addr)
          return true;
        else
          return false;
      }
    }

    lldb::addr_t class_addr = LLDB_INVALID_ADDRESS;
    lldb::addr_t sel_addr = LLDB_INVALID_ADDRESS;
  };

  struct ClassAndSelStr {
    ClassAndSelStr() = default;

    ClassAndSelStr(lldb::addr_t in_class_addr, llvm::StringRef in_sel_name)
        : class_addr(in_class_addr), sel_name(in_sel_name) {}

    bool operator==(const ClassAndSelStr &rhs) {
      return class_addr == rhs.class_addr && sel_name == rhs.sel_name;
    }

    bool operator<(const ClassAndSelStr &rhs) const {
      if (class_addr < rhs.class_addr)
        return true;
      else if (class_addr > rhs.class_addr)
        return false;
      else
        return ConstString::Compare(sel_name, rhs.sel_name);
    }

    lldb::addr_t class_addr = LLDB_INVALID_ADDRESS;
    ConstString sel_name;
  };

  typedef std::map<ClassAndSel, lldb::addr_t> MsgImplMap;
  typedef std::map<ClassAndSelStr, lldb::addr_t> MsgImplStrMap;
  typedef std::map<ObjCISA, ClassDescriptorSP> ISAToDescriptorMap;
  typedef std::multimap<uint32_t, ObjCISA> HashToISAMap;
  typedef ISAToDescriptorMap::iterator ISAToDescriptorIterator;
  typedef HashToISAMap::iterator HashToISAIterator;
  typedef ThreadSafeDenseMap<void *, uint64_t> TypeSizeCache;

  MsgImplMap m_impl_cache;
  MsgImplStrMap m_impl_str_cache;
  LazyBool m_has_new_literals_and_indexing;
  ISAToDescriptorMap m_isa_to_descriptor;
  HashToISAMap m_hash_to_isa_map;
  TypeSizeCache m_type_size_cache;

protected:
  uint32_t m_isa_to_descriptor_stop_id;

  typedef std::map<ConstString, lldb::TypeWP> CompleteClassMap;
  CompleteClassMap m_complete_class_cache;

  struct ConstStringSetHelpers {
    size_t operator()(ConstString arg) const // for hashing
    {
      return (size_t)arg.GetCString();
    }
    bool operator()(ConstString arg1,
                    ConstString arg2) const // for equality
    {
      return arg1.operator==(arg2);
    }
  };
  typedef std::unordered_set<ConstString, ConstStringSetHelpers,
                             ConstStringSetHelpers>
      CompleteClassSet;
  CompleteClassSet m_negative_complete_class_cache;

  ISAToDescriptorIterator GetDescriptorIterator(ConstString name);

  friend class ::CommandObjectObjC_ClassTable_Dump;

  std::pair<ISAToDescriptorIterator, ISAToDescriptorIterator>
  GetDescriptorIteratorPair(bool update_if_needed = true);

  void ReadObjCLibraryIfNeeded(const ModuleList &module_list);

  ObjCLanguageRuntime(const ObjCLanguageRuntime &) = delete;
  const ObjCLanguageRuntime &operator=(const ObjCLanguageRuntime &) = delete;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_OBJCLANGUAGERUNTIME_H
