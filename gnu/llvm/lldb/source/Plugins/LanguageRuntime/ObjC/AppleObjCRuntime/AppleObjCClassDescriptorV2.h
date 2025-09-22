//===-- AppleObjCClassDescriptorV2.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCCLASSDESCRIPTORV2_H
#define LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCCLASSDESCRIPTORV2_H

#include <mutex>

#include "AppleObjCRuntimeV2.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private.h"

#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

namespace lldb_private {

class ClassDescriptorV2 : public ObjCLanguageRuntime::ClassDescriptor {
public:
  friend class lldb_private::AppleObjCRuntimeV2;

  ~ClassDescriptorV2() override = default;

  ConstString GetClassName() override;

  ObjCLanguageRuntime::ClassDescriptorSP GetSuperclass() override;

  ObjCLanguageRuntime::ClassDescriptorSP GetMetaclass() const override;

  bool IsValid() override {
    return true; // any Objective-C v2 runtime class descriptor we vend is valid
  }

  lldb::LanguageType GetImplementationLanguage() const override;

  // a custom descriptor is used for tagged pointers
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

  uint64_t GetInstanceSize() override;

  ObjCLanguageRuntime::ObjCISA GetISA() override { return m_objc_class_ptr; }

  bool Describe(
      std::function<void(ObjCLanguageRuntime::ObjCISA)> const &superclass_func,
      std::function<bool(const char *, const char *)> const
          &instance_method_func,
      std::function<bool(const char *, const char *)> const &class_method_func,
      std::function<bool(const char *, const char *, lldb::addr_t,
                         uint64_t)> const &ivar_func) const override;

  size_t GetNumIVars() override {
    GetIVarInformation();
    return m_ivars_storage.size();
  }

  iVarDescriptor GetIVarAtIndex(size_t idx) override {
    if (idx >= GetNumIVars())
      return iVarDescriptor();
    return m_ivars_storage[idx];
  }

protected:
  void GetIVarInformation();

private:
  static const uint32_t RW_REALIZED = (1u << 31);

  struct objc_class_t {
    ObjCLanguageRuntime::ObjCISA m_isa = 0; // The class's metaclass.
    ObjCLanguageRuntime::ObjCISA m_superclass = 0;
    lldb::addr_t m_cache_ptr = 0;
    lldb::addr_t m_vtable_ptr = 0;
    lldb::addr_t m_data_ptr = 0;
    uint8_t m_flags = 0;

    objc_class_t() = default;

    void Clear() {
      m_isa = 0;
      m_superclass = 0;
      m_cache_ptr = 0;
      m_vtable_ptr = 0;
      m_data_ptr = 0;
      m_flags = 0;
    }

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct class_ro_t {
    uint32_t m_flags;
    uint32_t m_instanceStart;
    uint32_t m_instanceSize;
    uint32_t m_reserved;

    lldb::addr_t m_ivarLayout_ptr;
    lldb::addr_t m_name_ptr;
    lldb::addr_t m_baseMethods_ptr;
    lldb::addr_t m_baseProtocols_ptr;
    lldb::addr_t m_ivars_ptr;

    lldb::addr_t m_weakIvarLayout_ptr;
    lldb::addr_t m_baseProperties_ptr;

    std::string m_name;

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct class_rw_t {
    uint32_t m_flags;
    uint32_t m_version;

    lldb::addr_t m_ro_ptr;
    union {
      lldb::addr_t m_method_list_ptr;
      lldb::addr_t m_method_lists_ptr;
    };
    lldb::addr_t m_properties_ptr;
    lldb::addr_t m_protocols_ptr;

    ObjCLanguageRuntime::ObjCISA m_firstSubclass;
    ObjCLanguageRuntime::ObjCISA m_nextSiblingClass;

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct method_list_t {
    uint16_t m_entsize;
    bool m_is_small;
    bool m_has_direct_selector;
    uint32_t m_count;
    lldb::addr_t m_first_ptr;

    bool Read(Process *process, lldb::addr_t addr);
  };

  std::optional<method_list_t>
  GetMethodList(Process *process, lldb::addr_t method_list_ptr) const;

  struct method_t {
    lldb::addr_t m_name_ptr;
    lldb::addr_t m_types_ptr;
    lldb::addr_t m_imp_ptr;

    std::string m_name;
    std::string m_types;

    static size_t GetSize(Process *process, bool is_small) {
      size_t field_size;
      if (is_small)
        field_size = 4; // uint32_t relative indirect fields
      else
        field_size = process->GetAddressByteSize();

      return field_size    // SEL name;
             + field_size  // const char *types;
             + field_size; // IMP imp;
    }

    bool Read(Process *process, lldb::addr_t addr,
              lldb::addr_t relative_selector_base_addr, bool is_small,
              bool has_direct_sel);
  };

  struct ivar_list_t {
    uint32_t m_entsize;
    uint32_t m_count;
    lldb::addr_t m_first_ptr;

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct ivar_t {
    lldb::addr_t m_offset_ptr;
    lldb::addr_t m_name_ptr;
    lldb::addr_t m_type_ptr;
    uint32_t m_alignment;
    uint32_t m_size;

    std::string m_name;
    std::string m_type;

    static size_t GetSize(Process *process) {
      size_t ptr_size = process->GetAddressByteSize();

      return ptr_size            // uintptr_t *offset;
             + ptr_size          // const char *name;
             + ptr_size          // const char *type;
             + sizeof(uint32_t)  // uint32_t alignment;
             + sizeof(uint32_t); // uint32_t size;
    }

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct relative_list_entry_t {
    uint16_t m_image_index;
    int64_t m_list_offset;

    bool Read(Process *process, lldb::addr_t addr);
  };

  struct relative_list_list_t {
    uint32_t m_entsize;
    uint32_t m_count;
    lldb::addr_t m_first_ptr;

    bool Read(Process *process, lldb::addr_t addr);
  };

  class iVarsStorage {
  public:
    iVarsStorage();

    size_t size();

    iVarDescriptor &operator[](size_t idx);

    void fill(AppleObjCRuntimeV2 &runtime, ClassDescriptorV2 &descriptor);

  private:
    bool m_filled = false;
    std::vector<iVarDescriptor> m_ivars;
    std::recursive_mutex m_mutex;
  };

  // The constructor should only be invoked by the runtime as it builds its
  // caches
  // or populates them.  A ClassDescriptorV2 should only ever exist in a cache.
  ClassDescriptorV2(AppleObjCRuntimeV2 &runtime,
                    ObjCLanguageRuntime::ObjCISA isa, const char *name)
      : m_runtime(runtime), m_objc_class_ptr(isa), m_name(name),
        m_ivars_storage(), m_image_to_method_lists(), m_last_version_updated() {
  }

  bool Read_objc_class(Process *process,
                       std::unique_ptr<objc_class_t> &objc_class) const;

  bool Read_class_row(Process *process, const objc_class_t &objc_class,
                      std::unique_ptr<class_ro_t> &class_ro,
                      std::unique_ptr<class_rw_t> &class_rw) const;

  bool ProcessMethodList(std::function<bool(const char *, const char *)> const
                             &instance_method_func,
                         method_list_t &method_list) const;

  bool ProcessRelativeMethodLists(
      std::function<bool(const char *, const char *)> const
          &instance_method_func,
      lldb::addr_t relative_method_list_ptr) const;

  AppleObjCRuntimeV2
      &m_runtime; // The runtime, so we can read information lazily.
  lldb::addr_t m_objc_class_ptr; // The address of the objc_class_t.  (I.e.,
                                 // objects of this class type have this as
                                 // their ISA)
  ConstString m_name;            // May be NULL
  iVarsStorage m_ivars_storage;

  mutable std::map<uint16_t, std::vector<method_list_t>>
      m_image_to_method_lists;
  mutable std::optional<uint64_t> m_last_version_updated;
};

// tagged pointer descriptor
class ClassDescriptorV2Tagged : public ObjCLanguageRuntime::ClassDescriptor {
public:
  ClassDescriptorV2Tagged(ConstString class_name, uint64_t payload) {
    m_name = class_name;
    if (!m_name) {
      m_valid = false;
      return;
    }
    m_valid = true;
    m_payload = payload;
    m_info_bits = (m_payload & 0xF0ULL) >> 4;
    m_value_bits = (m_payload & ~0x0000000000000000FFULL) >> 8;
  }

  ClassDescriptorV2Tagged(
      ObjCLanguageRuntime::ClassDescriptorSP actual_class_sp,
      uint64_t u_payload, int64_t s_payload) {
    if (!actual_class_sp) {
      m_valid = false;
      return;
    }
    m_name = actual_class_sp->GetClassName();
    if (!m_name) {
      m_valid = false;
      return;
    }
    m_valid = true;
    m_payload = u_payload;
    m_info_bits = (m_payload & 0x0FULL);
    m_value_bits = (m_payload & ~0x0FULL) >> 4;
    m_value_bits_signed = (s_payload & ~0x0FLL) >> 4;
  }

  ~ClassDescriptorV2Tagged() override = default;

  ConstString GetClassName() override { return m_name; }

  ObjCLanguageRuntime::ClassDescriptorSP GetSuperclass() override {
    // tagged pointers can represent a class that has a superclass, but since
    // that information is not
    // stored in the object itself, we would have to query the runtime to
    // discover the hierarchy
    // for the time being, we skip this step in the interest of static discovery
    return ObjCLanguageRuntime::ClassDescriptorSP();
  }

  ObjCLanguageRuntime::ClassDescriptorSP GetMetaclass() const override {
    return ObjCLanguageRuntime::ClassDescriptorSP();
  }

  bool IsValid() override { return m_valid; }

  bool IsKVO() override {
    return false; // tagged pointers are not KVO'ed
  }

  bool IsCFType() override {
    return false; // tagged pointers are not CF objects
  }

  bool GetTaggedPointerInfo(uint64_t *info_bits = nullptr,
                            uint64_t *value_bits = nullptr,
                            uint64_t *payload = nullptr) override {
    if (info_bits)
      *info_bits = GetInfoBits();
    if (value_bits)
      *value_bits = GetValueBits();
    if (payload)
      *payload = GetPayload();
    return true;
  }

  bool GetTaggedPointerInfoSigned(uint64_t *info_bits = nullptr,
                                  int64_t *value_bits = nullptr,
                                  uint64_t *payload = nullptr) override {
    if (info_bits)
      *info_bits = GetInfoBits();
    if (value_bits)
      *value_bits = GetValueBitsSigned();
    if (payload)
      *payload = GetPayload();
    return true;
  }

  uint64_t GetInstanceSize() override {
    return (IsValid() ? m_pointer_size : 0);
  }

  ObjCLanguageRuntime::ObjCISA GetISA() override {
    return 0; // tagged pointers have no ISA
  }

  // these calls are not part of any formal tagged pointers specification
  virtual uint64_t GetValueBits() { return (IsValid() ? m_value_bits : 0); }

  virtual int64_t GetValueBitsSigned() {
    return (IsValid() ? m_value_bits_signed : 0);
  }

  virtual uint64_t GetInfoBits() { return (IsValid() ? m_info_bits : 0); }

  virtual uint64_t GetPayload() { return (IsValid() ? m_payload : 0); }

private:
  ConstString m_name;
  uint8_t m_pointer_size = 0;
  bool m_valid = false;
  uint64_t m_info_bits = 0;
  uint64_t m_value_bits = 0;
  int64_t m_value_bits_signed = 0;
  uint64_t m_payload = 0;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_LANGUAGERUNTIME_OBJC_APPLEOBJCRUNTIME_APPLEOBJCCLASSDESCRIPTORV2_H
