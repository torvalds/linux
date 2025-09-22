//===-- AppleObjCRuntimeV1.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV1_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV1_H

#include "AppleObjCRuntime.h"
#include "lldb/lldb-private.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

namespace lldb_private {

class AppleObjCRuntimeV1 : public AppleObjCRuntime {
public:
  ~AppleObjCRuntimeV1() override = default;

  // Static Functions
  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static llvm::StringRef GetPluginNameStatic() { return "apple-objc-v1"; }

  static char ID;

  bool isA(const void *ClassID) const override {
    return ClassID == &ID || AppleObjCRuntime::isA(ClassID);
  }

  static bool classof(const LanguageRuntime *runtime) {
    return runtime->isA(&ID);
  }

  lldb::addr_t GetTaggedPointerObfuscator();

  class ClassDescriptorV1 : public ObjCLanguageRuntime::ClassDescriptor {
  public:
    ClassDescriptorV1(ValueObject &isa_pointer);
    ClassDescriptorV1(ObjCISA isa, lldb::ProcessSP process_sp);

    ~ClassDescriptorV1() override = default;

    ConstString GetClassName() override { return m_name; }

    ClassDescriptorSP GetSuperclass() override;

    ClassDescriptorSP GetMetaclass() const override;

    bool IsValid() override { return m_valid; }

    // v1 does not support tagged pointers
    bool GetTaggedPointerInfo(uint64_t *info_bits = nullptr,
                              uint64_t *value_bits = nullptr,
                              uint64_t *payload = nullptr) override {
      return false;
    }

    bool GetTaggedPointerInfoSigned(uint64_t *info_bits = nullptr,
                                    int64_t *value_bits = nullptr,
                                    uint64_t *payload = nullptr) override {
      return false;
    }

    uint64_t GetInstanceSize() override { return m_instance_size; }

    ObjCISA GetISA() override { return m_isa; }

    bool
    Describe(std::function<void(ObjCLanguageRuntime::ObjCISA)> const
                 &superclass_func,
             std::function<bool(const char *, const char *)> const
                 &instance_method_func,
             std::function<bool(const char *, const char *)> const
                 &class_method_func,
             std::function<bool(const char *, const char *, lldb::addr_t,
                                uint64_t)> const &ivar_func) const override;

  protected:
    void Initialize(ObjCISA isa, lldb::ProcessSP process_sp);

  private:
    ConstString m_name;
    ObjCISA m_isa;
    ObjCISA m_parent_isa;
    bool m_valid;
    lldb::ProcessWP m_process_wp;
    uint64_t m_instance_size;
  };

  // These are generic runtime functions:
  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  llvm::Expected<std::unique_ptr<UtilityFunction>>
  CreateObjectChecker(std::string, ExecutionContext &exe_ctx) override;

  // PluginInterface protocol
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  ObjCRuntimeVersions GetRuntimeVersion() const override {
    return ObjCRuntimeVersions::eAppleObjC_V1;
  }

  void UpdateISAToDescriptorMapIfNeeded() override;

  DeclVendor *GetDeclVendor() override;

protected:
  lldb::BreakpointResolverSP
  CreateExceptionResolver(const lldb::BreakpointSP &bkpt,
                          bool catch_bp, bool throw_bp) override;

  class HashTableSignature {
  public:
    HashTableSignature() = default;

    bool NeedsUpdate(uint32_t count, uint32_t num_buckets,
                     lldb::addr_t buckets_ptr) {
      return m_count != count || m_num_buckets != num_buckets ||
             m_buckets_ptr != buckets_ptr;
    }

    void UpdateSignature(uint32_t count, uint32_t num_buckets,
                         lldb::addr_t buckets_ptr) {
      m_count = count;
      m_num_buckets = num_buckets;
      m_buckets_ptr = buckets_ptr;
    }

  protected:
    uint32_t m_count = 0;
    uint32_t m_num_buckets = 0;
    lldb::addr_t m_buckets_ptr = LLDB_INVALID_ADDRESS;
  };

  lldb::addr_t GetISAHashTablePointer();

  HashTableSignature m_hash_signature;
  lldb::addr_t m_isa_hash_table_ptr;
  std::unique_ptr<DeclVendor> m_decl_vendor_up;

private:
  AppleObjCRuntimeV1(Process *process);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCRUNTIMEV1_H
