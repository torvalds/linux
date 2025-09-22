//===-- AppleObjCRuntimeV2.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV2_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV2_H

#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include "AppleObjCRuntime.h"
#include "lldb/lldb-private.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include "llvm/ADT/BitVector.h"

class RemoteNXMapTable;

namespace lldb_private {

class AppleObjCRuntimeV2 : public AppleObjCRuntime {
public:
  ~AppleObjCRuntimeV2() override = default;

  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() { return "apple-objc-v2"; }

  LanguageRuntime *GetPreferredLanguageRuntime(ValueObject &in_value) override;

  static char ID;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || AppleObjCRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  llvm::Expected<std::unique_ptr<UtilityFunction>>
  CreateObjectChecker(std::string name, ExecutionContext &exe_ctx) override;

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  ObjCRuntimeVersions GetRuntimeVersion() const override {
    return ObjCRuntimeVersions::eAppleObjC_V2;
  }

  size_t GetByteOffsetForIvar(CompilerType &parent_ast_type,
                              const char *ivar_name) override;

  void UpdateISAToDescriptorMapIfNeeded() override;

  ClassDescriptorSP GetClassDescriptor(ValueObject &valobj) override;

  ClassDescriptorSP GetClassDescriptorFromISA(ObjCISA isa) override;

  DeclVendor *GetDeclVendor() override;

  lldb::addr_t LookupRuntimeSymbol(ConstString name) override;

  EncodingToTypeSP GetEncodingToType() override;

  bool IsTaggedPointer(lldb::addr_t ptr) override;

  TaggedPointerVendor *GetTaggedPointerVendor() override {
    return m_tagged_pointer_vendor_up.get();
  }

  lldb::addr_t GetTaggedPointerObfuscator();

  /// Returns the base address for relative method list selector strings.
  lldb::addr_t GetRelativeSelectorBaseAddr() {
    return m_relative_selector_base;
  }

  void SetRelativeSelectorBaseAddr(lldb::addr_t relative_selector_base) {
    m_relative_selector_base = relative_selector_base;
  }

  void GetValuesForGlobalCFBooleans(lldb::addr_t &cf_true,
                                    lldb::addr_t &cf_false) override;

  void ModulesDidLoad(const ModuleList &module_list) override;

  bool IsSharedCacheImageLoaded(uint16_t image_index);

  std::optional<uint64_t> GetSharedCacheImageHeaderVersion();

protected:
  lldb::BreakpointResolverSP
  CreateExceptionResolver(const lldb::BreakpointSP &bkpt, bool catch_bp,
                          bool throw_bp) override;

private:
  class HashTableSignature {
  public:
    HashTableSignature();

    bool NeedsUpdate(Process *process, AppleObjCRuntimeV2 *runtime,
                     RemoteNXMapTable &hash_table);

    void UpdateSignature(const RemoteNXMapTable &hash_table);

  protected:
    uint32_t m_count = 0;
    uint32_t m_num_buckets = 0;
    lldb::addr_t m_buckets_ptr = 0;
  };

  class NonPointerISACache {
  public:
    static NonPointerISACache *
    CreateInstance(AppleObjCRuntimeV2 &runtime,
                   const lldb::ModuleSP &objc_module_sp);

    ObjCLanguageRuntime::ClassDescriptorSP GetClassDescriptor(ObjCISA isa);

  private:
    NonPointerISACache(AppleObjCRuntimeV2 &runtime,
                       const lldb::ModuleSP &objc_module_sp,
                       uint64_t objc_debug_isa_class_mask,
                       uint64_t objc_debug_isa_magic_mask,
                       uint64_t objc_debug_isa_magic_value,
                       uint64_t objc_debug_indexed_isa_magic_mask,
                       uint64_t objc_debug_indexed_isa_magic_value,
                       uint64_t objc_debug_indexed_isa_index_mask,
                       uint64_t objc_debug_indexed_isa_index_shift,
                       lldb::addr_t objc_indexed_classes);

    bool EvaluateNonPointerISA(ObjCISA isa, ObjCISA &ret_isa);

    AppleObjCRuntimeV2 &m_runtime;
    std::map<ObjCISA, ObjCLanguageRuntime::ClassDescriptorSP> m_cache;
    lldb::ModuleWP m_objc_module_wp;
    uint64_t m_objc_debug_isa_class_mask;
    uint64_t m_objc_debug_isa_magic_mask;
    uint64_t m_objc_debug_isa_magic_value;

    uint64_t m_objc_debug_indexed_isa_magic_mask;
    uint64_t m_objc_debug_indexed_isa_magic_value;
    uint64_t m_objc_debug_indexed_isa_index_mask;
    uint64_t m_objc_debug_indexed_isa_index_shift;
    lldb::addr_t m_objc_indexed_classes;

    std::vector<lldb::addr_t> m_indexed_isa_cache;

    friend class AppleObjCRuntimeV2;

    NonPointerISACache(const NonPointerISACache &) = delete;
    const NonPointerISACache &operator=(const NonPointerISACache &) = delete;
  };

  class TaggedPointerVendorV2
      : public ObjCLanguageRuntime::TaggedPointerVendor {
  public:
    ~TaggedPointerVendorV2() override = default;

    static TaggedPointerVendorV2 *
    CreateInstance(AppleObjCRuntimeV2 &runtime,
                   const lldb::ModuleSP &objc_module_sp);

  protected:
    AppleObjCRuntimeV2 &m_runtime;

    TaggedPointerVendorV2(AppleObjCRuntimeV2 &runtime)
        : TaggedPointerVendor(), m_runtime(runtime) {}

  private:
    TaggedPointerVendorV2(const TaggedPointerVendorV2 &) = delete;
    const TaggedPointerVendorV2 &
    operator=(const TaggedPointerVendorV2 &) = delete;
  };

  class TaggedPointerVendorRuntimeAssisted : public TaggedPointerVendorV2 {
  public:
    bool IsPossibleTaggedPointer(lldb::addr_t ptr) override;

    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorRuntimeAssisted(
        AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
        uint32_t objc_debug_taggedpointer_slot_shift,
        uint32_t objc_debug_taggedpointer_slot_mask,
        uint32_t objc_debug_taggedpointer_payload_lshift,
        uint32_t objc_debug_taggedpointer_payload_rshift,
        lldb::addr_t objc_debug_taggedpointer_classes);

    typedef std::map<uint8_t, ObjCLanguageRuntime::ClassDescriptorSP> Cache;
    typedef Cache::iterator CacheIterator;
    Cache m_cache;
    uint64_t m_objc_debug_taggedpointer_mask;
    uint32_t m_objc_debug_taggedpointer_slot_shift;
    uint32_t m_objc_debug_taggedpointer_slot_mask;
    uint32_t m_objc_debug_taggedpointer_payload_lshift;
    uint32_t m_objc_debug_taggedpointer_payload_rshift;
    lldb::addr_t m_objc_debug_taggedpointer_classes;

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    TaggedPointerVendorRuntimeAssisted(
        const TaggedPointerVendorRuntimeAssisted &) = delete;
    const TaggedPointerVendorRuntimeAssisted &
    operator=(const TaggedPointerVendorRuntimeAssisted &) = delete;
  };

  class TaggedPointerVendorExtended
      : public TaggedPointerVendorRuntimeAssisted {
  public:
    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorExtended(
        AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
        uint64_t objc_debug_taggedpointer_ext_mask,
        uint32_t objc_debug_taggedpointer_slot_shift,
        uint32_t objc_debug_taggedpointer_ext_slot_shift,
        uint32_t objc_debug_taggedpointer_slot_mask,
        uint32_t objc_debug_taggedpointer_ext_slot_mask,
        uint32_t objc_debug_taggedpointer_payload_lshift,
        uint32_t objc_debug_taggedpointer_payload_rshift,
        uint32_t objc_debug_taggedpointer_ext_payload_lshift,
        uint32_t objc_debug_taggedpointer_ext_payload_rshift,
        lldb::addr_t objc_debug_taggedpointer_classes,
        lldb::addr_t objc_debug_taggedpointer_ext_classes);

    bool IsPossibleExtendedTaggedPointer(lldb::addr_t ptr);

    typedef std::map<uint8_t, ObjCLanguageRuntime::ClassDescriptorSP> Cache;
    typedef Cache::iterator CacheIterator;
    Cache m_ext_cache;
    uint64_t m_objc_debug_taggedpointer_ext_mask;
    uint32_t m_objc_debug_taggedpointer_ext_slot_shift;
    uint32_t m_objc_debug_taggedpointer_ext_slot_mask;
    uint32_t m_objc_debug_taggedpointer_ext_payload_lshift;
    uint32_t m_objc_debug_taggedpointer_ext_payload_rshift;
    lldb::addr_t m_objc_debug_taggedpointer_ext_classes;

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    TaggedPointerVendorExtended(const TaggedPointerVendorExtended &) = delete;
    const TaggedPointerVendorExtended &
    operator=(const TaggedPointerVendorExtended &) = delete;
  };

  class TaggedPointerVendorLegacy : public TaggedPointerVendorV2 {
  public:
    bool IsPossibleTaggedPointer(lldb::addr_t ptr) override;

    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorLegacy(AppleObjCRuntimeV2 &runtime)
        : TaggedPointerVendorV2(runtime) {}

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    TaggedPointerVendorLegacy(const TaggedPointerVendorLegacy &) = delete;
    const TaggedPointerVendorLegacy &
    operator=(const TaggedPointerVendorLegacy &) = delete;
  };

  struct DescriptorMapUpdateResult {
    bool m_update_ran;
    bool m_retry_update;
    uint32_t m_num_found;

    DescriptorMapUpdateResult(bool ran, bool retry, uint32_t found) {
      m_update_ran = ran;

      m_retry_update = retry;

      m_num_found = found;
    }

    static DescriptorMapUpdateResult Fail() { return {false, false, 0}; }

    static DescriptorMapUpdateResult Success(uint32_t found) {
      return {true, false, found};
    }

    static DescriptorMapUpdateResult Retry() { return {false, true, 0}; }
  };

  /// Abstraction to read the Objective-C class info.
  class ClassInfoExtractor {
  public:
    ClassInfoExtractor(AppleObjCRuntimeV2 &runtime) : m_runtime(runtime) {}
    std::mutex &GetMutex() { return m_mutex; }

  protected:
    /// The lifetime of this object is tied to that of the runtime.
    AppleObjCRuntimeV2 &m_runtime;
    std::mutex m_mutex;
  };

  /// We can read the class info from the Objective-C runtime using
  /// gdb_objc_realized_classes, objc_copyRealizedClassList or
  /// objc_getRealizedClassList_trylock. The RealizedClassList variants are
  /// preferred because they include lazily named classes, but they are not
  /// always available or safe to call.
  ///
  /// We potentially need more than one helper for the same process, because we
  /// may need to use gdb_objc_realized_classes until dyld is initialized and
  /// then switch over to objc_copyRealizedClassList or
  /// objc_getRealizedClassList_trylock for lazily named classes.
  class DynamicClassInfoExtractor : public ClassInfoExtractor {
  public:
    DynamicClassInfoExtractor(AppleObjCRuntimeV2 &runtime)
        : ClassInfoExtractor(runtime) {}

    DescriptorMapUpdateResult
    UpdateISAToDescriptorMap(RemoteNXMapTable &hash_table);

  private:
    enum Helper {
      gdb_objc_realized_classes,
      objc_copyRealizedClassList,
      objc_getRealizedClassList_trylock
    };

    /// Compute which helper to use. If dyld is not yet fully initialized we
    /// must use gdb_objc_realized_classes. Otherwise, we prefer
    /// objc_getRealizedClassList_trylock and objc_copyRealizedClassList
    /// respectively, depending on availability.
    Helper ComputeHelper(ExecutionContext &exe_ctx) const;

    UtilityFunction *GetClassInfoUtilityFunction(ExecutionContext &exe_ctx,
                                                 Helper helper);
    lldb::addr_t &GetClassInfoArgs(Helper helper);

    std::unique_ptr<UtilityFunction>
    GetClassInfoUtilityFunctionImpl(ExecutionContext &exe_ctx, Helper helper,
                                    std::string code, std::string name);

    struct UtilityFunctionHelper {
      std::unique_ptr<UtilityFunction> utility_function;
      lldb::addr_t args = LLDB_INVALID_ADDRESS;
    };

    UtilityFunctionHelper m_gdb_objc_realized_classes_helper;
    UtilityFunctionHelper m_objc_copyRealizedClassList_helper;
    UtilityFunctionHelper m_objc_getRealizedClassList_trylock_helper;
  };

  /// Abstraction to read the Objective-C class info from the shared cache.
  class SharedCacheClassInfoExtractor : public ClassInfoExtractor {
  public:
    SharedCacheClassInfoExtractor(AppleObjCRuntimeV2 &runtime)
        : ClassInfoExtractor(runtime) {}

    DescriptorMapUpdateResult UpdateISAToDescriptorMap();

  private:
    UtilityFunction *GetClassInfoUtilityFunction(ExecutionContext &exe_ctx);

    std::unique_ptr<UtilityFunction>
    GetClassInfoUtilityFunctionImpl(ExecutionContext &exe_ctx);

    std::unique_ptr<UtilityFunction> m_utility_function;
    lldb::addr_t m_args = LLDB_INVALID_ADDRESS;
  };

  class SharedCacheImageHeaders {
  public:
    static std::unique_ptr<SharedCacheImageHeaders>
    CreateSharedCacheImageHeaders(AppleObjCRuntimeV2 &runtime);

    void SetNeedsUpdate() { m_needs_update = true; }

    bool IsImageLoaded(uint16_t image_index);

    uint64_t GetVersion();

  private:
    SharedCacheImageHeaders(AppleObjCRuntimeV2 &runtime,
                            lldb::addr_t headerInfoRWs_ptr, uint32_t count,
                            uint32_t entsize)
        : m_runtime(runtime), m_headerInfoRWs_ptr(headerInfoRWs_ptr),
          m_loaded_images(count, false), m_version(0), m_count(count),
          m_entsize(entsize), m_needs_update(true) {}
    llvm::Error UpdateIfNeeded();

    AppleObjCRuntimeV2 &m_runtime;
    lldb::addr_t m_headerInfoRWs_ptr;
    llvm::BitVector m_loaded_images;
    uint64_t m_version;
    uint32_t m_count;
    uint32_t m_entsize;
    bool m_needs_update;
  };

  AppleObjCRuntimeV2(Process *process, const lldb::ModuleSP &objc_module_sp);

  ObjCISA GetPointerISA(ObjCISA isa);

  lldb::addr_t GetISAHashTablePointer();

  /// Update the generation count of realized classes. This is not an exact
  /// count but rather a value that is incremented when new classes are realized
  /// or destroyed. Unlike the count in gdb_objc_realized_classes, it will
  /// change when lazily named classes get realized.
  bool RealizedClassGenerationCountChanged();

  uint32_t ParseClassInfoArray(const lldb_private::DataExtractor &data,
                               uint32_t num_class_infos);

  enum class SharedCacheWarningReason {
    eExpressionUnableToRun,
    eExpressionExecutionFailure,
    eNotEnoughClassesRead
  };

  void WarnIfNoClassesCached(SharedCacheWarningReason reason);
  void WarnIfNoExpandedSharedCache();

  lldb::addr_t GetSharedCacheReadOnlyAddress();
  lldb::addr_t GetSharedCacheBaseAddress();

  bool GetCFBooleanValuesIfNeeded();

  bool HasSymbol(ConstString Name);

  NonPointerISACache *GetNonPointerIsaCache() {
    if (!m_non_pointer_isa_cache_up)
      m_non_pointer_isa_cache_up.reset(
          NonPointerISACache::CreateInstance(*this, m_objc_module_sp));
    return m_non_pointer_isa_cache_up.get();
  }

  friend class ClassDescriptorV2;

  lldb::ModuleSP m_objc_module_sp;

  DynamicClassInfoExtractor m_dynamic_class_info_extractor;
  SharedCacheClassInfoExtractor m_shared_cache_class_info_extractor;

  std::unique_ptr<DeclVendor> m_decl_vendor_up;
  lldb::addr_t m_tagged_pointer_obfuscator;
  lldb::addr_t m_isa_hash_table_ptr;
  lldb::addr_t m_relative_selector_base;
  HashTableSignature m_hash_signature;
  bool m_has_object_getClass;
  bool m_has_objc_copyRealizedClassList;
  bool m_has_objc_getRealizedClassList_trylock;
  bool m_loaded_objc_opt;
  std::unique_ptr<NonPointerISACache> m_non_pointer_isa_cache_up;
  std::unique_ptr<TaggedPointerVendor> m_tagged_pointer_vendor_up;
  EncodingToTypeSP m_encoding_to_type_sp;
  std::once_flag m_no_classes_cached_warning;
  std::once_flag m_no_expanded_cache_warning;
  std::optional<std::pair<lldb::addr_t, lldb::addr_t>> m_CFBoolean_values;
  uint64_t m_realized_class_generation_count;
  std::unique_ptr<SharedCacheImageHeaders> m_shared_cache_image_headers_up;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV2_H
