//===-- NSDictionary.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <mutex>

#include "clang/AST/DeclCXX.h"

#include "CFBasicHash.h"
#include "NSDictionary.h"

#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

NSDictionary_Additionals::AdditionalFormatterMatching::Prefix::Prefix(
    ConstString p)
    : m_prefix(p) {}

bool NSDictionary_Additionals::AdditionalFormatterMatching::Prefix::Match(
    ConstString class_name) {
  return class_name.GetStringRef().starts_with(m_prefix.GetStringRef());
}

NSDictionary_Additionals::AdditionalFormatterMatching::Full::Full(ConstString n)
    : m_name(n) {}

bool NSDictionary_Additionals::AdditionalFormatterMatching::Full::Match(
    ConstString class_name) {
  return (class_name == m_name);
}

NSDictionary_Additionals::AdditionalFormatters<
    CXXFunctionSummaryFormat::Callback> &
NSDictionary_Additionals::GetAdditionalSummaries() {
  static AdditionalFormatters<CXXFunctionSummaryFormat::Callback> g_map;
  return g_map;
}

NSDictionary_Additionals::AdditionalFormatters<
    CXXSyntheticChildren::CreateFrontEndCallback> &
NSDictionary_Additionals::GetAdditionalSynthetics() {
  static AdditionalFormatters<CXXSyntheticChildren::CreateFrontEndCallback>
      g_map;
  return g_map;
}

static CompilerType GetLLDBNSPairType(TargetSP target_sp) {
  CompilerType compiler_type;
  TypeSystemClangSP scratch_ts_sp =
      ScratchTypeSystemClang::GetForTarget(*target_sp);

  if (!scratch_ts_sp)
    return compiler_type;

  static constexpr llvm::StringLiteral g_lldb_autogen_nspair("__lldb_autogen_nspair");

  compiler_type = scratch_ts_sp->GetTypeForIdentifier<clang::CXXRecordDecl>(g_lldb_autogen_nspair);

  if (!compiler_type) {
    compiler_type = scratch_ts_sp->CreateRecordType(
        nullptr, OptionalClangModuleID(), lldb::eAccessPublic,
        g_lldb_autogen_nspair, llvm::to_underlying(clang::TagTypeKind::Struct),
        lldb::eLanguageTypeC);

    if (compiler_type) {
      TypeSystemClang::StartTagDeclarationDefinition(compiler_type);
      CompilerType id_compiler_type =
          scratch_ts_sp->GetBasicType(eBasicTypeObjCID);
      TypeSystemClang::AddFieldToRecordType(
          compiler_type, "key", id_compiler_type, lldb::eAccessPublic, 0);
      TypeSystemClang::AddFieldToRecordType(
          compiler_type, "value", id_compiler_type, lldb::eAccessPublic, 0);
      TypeSystemClang::CompleteTagDeclarationDefinition(compiler_type);
    }
  }
  return compiler_type;
}

namespace lldb_private {
namespace formatters {
class NSDictionaryISyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSDictionaryISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSDictionaryISyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  struct DataDescriptor_32 {
    uint32_t _used : 26;
    uint32_t _szidx : 6;
  };

  struct DataDescriptor_64 {
    uint64_t _used : 58;
    uint32_t _szidx : 6;
  };

  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size = 8;
  lldb::ByteOrder m_order = lldb::eByteOrderInvalid;
  DataDescriptor_32 *m_data_32 = nullptr;
  DataDescriptor_64 *m_data_64 = nullptr;
  lldb::addr_t m_data_ptr = LLDB_INVALID_ADDRESS;
  CompilerType m_pair_type;
  std::vector<DictionaryItemDescriptor> m_children;
};

class NSConstantDictionarySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSConstantDictionarySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  CompilerType m_pair_type;
  uint8_t m_ptr_size = 8;
  lldb::ByteOrder m_order = lldb::eByteOrderInvalid;
  unsigned int m_size = 0;
  lldb::addr_t m_keys_ptr = LLDB_INVALID_ADDRESS;
  lldb::addr_t m_objects_ptr = LLDB_INVALID_ADDRESS;

  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  std::vector<DictionaryItemDescriptor> m_children;
};

class NSCFDictionarySyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSCFDictionarySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size = 8;
  lldb::ByteOrder m_order = lldb::eByteOrderInvalid;

  CFBasicHash m_hashtable;

  CompilerType m_pair_type;
  std::vector<DictionaryItemDescriptor> m_children;
};

class NSDictionary1SyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSDictionary1SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSDictionary1SyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ValueObjectSP m_pair;
};

template <typename D32, typename D64>
class GenericNSDictionaryMSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  GenericNSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~GenericNSDictionaryMSyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  struct DictionaryItemDescriptor {
    lldb::addr_t key_ptr;
    lldb::addr_t val_ptr;
    lldb::ValueObjectSP valobj_sp;
  };

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size = 8;
  lldb::ByteOrder m_order = lldb::eByteOrderInvalid;
  D32 *m_data_32;
  D64 *m_data_64;
  CompilerType m_pair_type;
  std::vector<DictionaryItemDescriptor> m_children;
};
  
namespace Foundation1100 {
  class NSDictionaryMSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
  public:
    NSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);
    
    ~NSDictionaryMSyntheticFrontEnd() override;

    llvm::Expected<uint32_t> CalculateNumChildren() override;

    lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

    lldb::ChildCacheState Update() override;

    bool MightHaveChildren() override;
    
    size_t GetIndexOfChildWithName(ConstString name) override;
    
  private:
    struct DataDescriptor_32 {
      uint32_t _used : 26;
      uint32_t _kvo : 1;
      uint32_t _size;
      uint32_t _mutations;
      uint32_t _objs_addr;
      uint32_t _keys_addr;
    };
    
    struct DataDescriptor_64 {
      uint64_t _used : 58;
      uint32_t _kvo : 1;
      uint64_t _size;
      uint64_t _mutations;
      uint64_t _objs_addr;
      uint64_t _keys_addr;
    };
    
    struct DictionaryItemDescriptor {
      lldb::addr_t key_ptr;
      lldb::addr_t val_ptr;
      lldb::ValueObjectSP valobj_sp;
    };
    
    ExecutionContextRef m_exe_ctx_ref;
    uint8_t m_ptr_size = 8;
    lldb::ByteOrder m_order = lldb::eByteOrderInvalid;
    DataDescriptor_32 *m_data_32 = nullptr;
    DataDescriptor_64 *m_data_64 = nullptr;
    CompilerType m_pair_type;
    std::vector<DictionaryItemDescriptor> m_children;
  };
}
  
namespace Foundation1428 {
  namespace {
    struct DataDescriptor_32 {
      uint32_t _used : 26;
      uint32_t _kvo : 1;
      uint32_t _size;
      uint32_t _buffer;
      uint64_t GetSize() { return _size; }
    };
    
    struct DataDescriptor_64 {
      uint64_t _used : 58;
      uint32_t _kvo : 1;
      uint64_t _size;
      uint64_t _buffer;
      uint64_t GetSize() { return _size; }
    };
  }

  using NSDictionaryMSyntheticFrontEnd =
    GenericNSDictionaryMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
}
  
namespace Foundation1437 {
    static const uint64_t NSDictionaryCapacities[] = {
        0, 3, 7, 13, 23, 41, 71, 127, 191, 251, 383, 631, 1087, 1723,
        2803, 4523, 7351, 11959, 19447, 31231, 50683, 81919, 132607,
        214519, 346607, 561109, 907759, 1468927, 2376191, 3845119,
        6221311, 10066421, 16287743, 26354171, 42641881, 68996069,
        111638519, 180634607, 292272623, 472907251
    };
    
    static const size_t NSDictionaryNumSizeBuckets =
        sizeof(NSDictionaryCapacities) / sizeof(uint64_t);

    namespace {
    struct DataDescriptor_32 {
      uint32_t _buffer;
      uint32_t _muts;
      uint32_t _used : 25;
      uint32_t _kvo : 1;
      uint32_t _szidx : 6;

      uint64_t GetSize() {
        return (_szidx) >= NSDictionaryNumSizeBuckets ?
            0 : NSDictionaryCapacities[_szidx];
      }
    };
    
    struct DataDescriptor_64 {
      uint64_t _buffer;
      uint32_t _muts;
      uint32_t _used : 25;
      uint32_t _kvo : 1;
      uint32_t _szidx : 6;

      uint64_t GetSize() {
        return (_szidx) >= NSDictionaryNumSizeBuckets ?
            0 : NSDictionaryCapacities[_szidx];
      }
    };
    } // namespace

  using NSDictionaryMSyntheticFrontEnd =
    GenericNSDictionaryMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
  
  template <typename DD>
  uint64_t
  __NSDictionaryMSize_Impl(lldb_private::Process &process,
                           lldb::addr_t valobj_addr, Status &error) {
    const lldb::addr_t start_of_descriptor =
        valobj_addr + process.GetAddressByteSize();
    DD descriptor = DD();
    process.ReadMemory(start_of_descriptor, &descriptor, sizeof(descriptor),
                       error);
    if (error.Fail()) {
      return 0;
    }
    return descriptor._used;
  }
  
  uint64_t
  __NSDictionaryMSize(lldb_private::Process &process, lldb::addr_t valobj_addr,
               Status &error) {
    if (process.GetAddressByteSize() == 4) {
      return __NSDictionaryMSize_Impl<DataDescriptor_32>(process, valobj_addr,
                                                         error);
    } else {
      return __NSDictionaryMSize_Impl<DataDescriptor_64>(process, valobj_addr,
                                                         error);
    }
  }

}
} // namespace formatters
} // namespace lldb_private

template <bool name_entries>
bool lldb_private::formatters::NSDictionarySummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  static constexpr llvm::StringLiteral g_TypeHint("NSDictionary");
  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetNonKVOClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();
  bool is_64bit = (ptr_size == 8);

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  uint64_t value = 0;

  ConstString class_name(descriptor->GetClassName());

  static const ConstString g_DictionaryI("__NSDictionaryI");
  static const ConstString g_DictionaryM("__NSDictionaryM");
  static const ConstString g_DictionaryMLegacy("__NSDictionaryM_Legacy");
  static const ConstString g_DictionaryMImmutable("__NSDictionaryM_Immutable");
  static const ConstString g_DictionaryMFrozen("__NSFrozenDictionaryM");
  static const ConstString g_Dictionary1("__NSSingleEntryDictionaryI");
  static const ConstString g_Dictionary0("__NSDictionary0");
  static const ConstString g_DictionaryCF("__CFDictionary");
  static const ConstString g_DictionaryNSCF("__NSCFDictionary");
  static const ConstString g_DictionaryCFRef("CFDictionaryRef");
  static const ConstString g_ConstantDictionary("NSConstantDictionary");

  if (class_name.IsEmpty())
    return false;

  if (class_name == g_DictionaryI || class_name == g_DictionaryMImmutable) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;

    value &= (is_64bit ? ~0xFC00000000000000UL : ~0xFC000000U);
  } else if (class_name == g_ConstantDictionary) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(
        valobj_addr + 2 * ptr_size, ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_DictionaryM || class_name == g_DictionaryMLegacy ||
             class_name == g_DictionaryMFrozen) {
    AppleObjCRuntime *apple_runtime =
    llvm::dyn_cast_or_null<AppleObjCRuntime>(runtime);
    Status error;
    if (apple_runtime && apple_runtime->GetFoundationVersion() >= 1437) {
      value = Foundation1437::__NSDictionaryMSize(*process_sp, valobj_addr,
                                                  error);
    } else {
      value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                        ptr_size, 0, error);
      value &= (is_64bit ? ~0xFC00000000000000UL : ~0xFC000000U);
    }
    if (error.Fail())
      return false;
  } else if (class_name == g_Dictionary1) {
    value = 1;
  } else if (class_name == g_Dictionary0) {
    value = 0;
  } else if (class_name == g_DictionaryCF || class_name == g_DictionaryNSCF ||
             class_name == g_DictionaryCFRef) {
    ExecutionContext exe_ctx(process_sp);
    CFBasicHash cfbh;
    if (!cfbh.Update(valobj_addr, exe_ctx))
      return false;
    value = cfbh.GetCount();
  } else {
    auto &map(NSDictionary_Additionals::GetAdditionalSummaries());
    for (auto &candidate : map) {
      if (candidate.first && candidate.first->Match(class_name))
        return candidate.second(valobj, stream, options);
    }
    return false;
  }

  llvm::StringRef prefix, suffix;
  if (Language *language = Language::FindPlugin(options.GetLanguage()))
    std::tie(prefix, suffix) = language->GetFormatterPrefixSuffix(g_TypeHint);

  stream << prefix;
  stream.Printf("%" PRIu64 " %s%s", value, "key/value pair",
                value == 1 ? "" : "s");
  stream << suffix;
  return true;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::NSDictionarySyntheticFrontEndCreator(
    CXXSyntheticChildren *synth, lldb::ValueObjectSP valobj_sp) {
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return nullptr;
  AppleObjCRuntime *runtime = llvm::dyn_cast_or_null<AppleObjCRuntime>(
      ObjCLanguageRuntime::Get(*process_sp));
  if (!runtime)
    return nullptr;

  CompilerType valobj_type(valobj_sp->GetCompilerType());
  Flags flags(valobj_type.GetTypeInfo());

  if (flags.IsClear(eTypeIsPointer)) {
    Status error;
    valobj_sp = valobj_sp->AddressOf(error);
    if (error.Fail() || !valobj_sp)
      return nullptr;
  }

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(*valobj_sp));

  if (!descriptor || !descriptor->IsValid())
    return nullptr;

  ConstString class_name(descriptor->GetClassName());

  static const ConstString g_DictionaryI("__NSDictionaryI");
  static const ConstString g_DictionaryM("__NSDictionaryM");
  static const ConstString g_Dictionary1("__NSSingleEntryDictionaryI");
  static const ConstString g_DictionaryImmutable("__NSDictionaryM_Immutable");
  static const ConstString g_DictionaryMFrozen("__NSFrozenDictionaryM");
  static const ConstString g_DictionaryMLegacy("__NSDictionaryM_Legacy");
  static const ConstString g_Dictionary0("__NSDictionary0");
  static const ConstString g_DictionaryCF("__CFDictionary");
  static const ConstString g_DictionaryNSCF("__NSCFDictionary");
  static const ConstString g_DictionaryCFRef("CFDictionaryRef");
  static const ConstString g_ConstantDictionary("NSConstantDictionary");

  if (class_name.IsEmpty())
    return nullptr;

  if (class_name == g_DictionaryI) {
    return (new NSDictionaryISyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_ConstantDictionary) {
    return (new NSConstantDictionarySyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_DictionaryM || class_name == g_DictionaryMFrozen) {
    if (runtime->GetFoundationVersion() >= 1437) {
      return (new Foundation1437::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    } else if (runtime->GetFoundationVersion() >= 1428) {
      return (new Foundation1428::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    } else {
      return (new Foundation1100::NSDictionaryMSyntheticFrontEnd(valobj_sp));
    }
  } else if (class_name == g_DictionaryMLegacy) {
    return (new Foundation1100::NSDictionaryMSyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_Dictionary1) {
    return (new NSDictionary1SyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_DictionaryCF || class_name == g_DictionaryNSCF ||
             class_name == g_DictionaryCFRef) {
    return (new NSCFDictionarySyntheticFrontEnd(valobj_sp));
  } else {
    auto &map(NSDictionary_Additionals::GetAdditionalSynthetics());
    for (auto &candidate : map) {
      if (candidate.first && candidate.first->Match((class_name)))
        return candidate.second(synth, valobj_sp);
    }
  }

  return nullptr;
}

lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    NSDictionaryISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_pair_type() {}

lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    ~NSDictionaryISyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

size_t lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    NSDictionaryISyntheticFrontEnd::CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? m_data_32->_used : m_data_64->_used);
}

lldb::ChildCacheState
lldb_private::formatters::NSDictionaryISyntheticFrontEnd::Update() {
  m_children.clear();
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  m_ptr_size = 0;
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return lldb::ChildCacheState::eRefetch;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new DataDescriptor_32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(DataDescriptor_32),
                           error);
  } else {
    m_data_64 = new DataDescriptor_64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(DataDescriptor_64),
                           error);
  }
  if (error.Fail())
    return lldb::ChildCacheState::eRefetch;
  m_data_ptr = data_location + m_ptr_size;
  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSDictionaryISyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSDictionaryISyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  uint32_t num_children = CalculateNumChildrenIgnoringErrors();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_data_ptr + (2 * test_idx * m_ptr_size);
      val_at_idx = key_at_idx + m_ptr_size;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

lldb_private::formatters::NSCFDictionarySyntheticFrontEnd::
    NSCFDictionarySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_hashtable(),
      m_pair_type() {}

size_t lldb_private::formatters::NSCFDictionarySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  const uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    NSCFDictionarySyntheticFrontEnd::CalculateNumChildren() {
  if (!m_hashtable.IsValid())
    return 0;
  return m_hashtable.GetCount();
}

lldb::ChildCacheState
lldb_private::formatters::NSCFDictionarySyntheticFrontEnd::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  m_ptr_size = 0;
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();

  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return lldb::ChildCacheState::eRefetch;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  return m_hashtable.Update(valobj_sp->GetValueAsUnsigned(0), m_exe_ctx_ref)
             ? lldb::ChildCacheState::eReuse
             : lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSCFDictionarySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSCFDictionarySyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  lldb::addr_t m_keys_ptr = m_hashtable.GetKeyPointer();
  lldb::addr_t m_values_ptr = m_hashtable.GetValuePointer();

  const uint32_t num_children = CalculateNumChildrenIgnoringErrors();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
    if (!process_sp)
      return lldb::ValueObjectSP();

    Status error;
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    // Iterate over inferior memory, reading key/value pointers by shifting each
    // cursor by test_index * m_ptr_size. Returns an empty ValueObject if a read
    // fails, otherwise, continue until the number of tries matches the number
    // of childen.
    while (tries < num_children) {
      key_at_idx = m_keys_ptr + (test_idx * m_ptr_size);
      val_at_idx = m_values_ptr + (test_idx * m_ptr_size);

      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    switch (m_ptr_size) {
    case 0: // architecture has no clue - fail
      return lldb::ValueObjectSP();
    case 4: {
      uint32_t *data_ptr = reinterpret_cast<uint32_t *>(buffer_sp->GetBytes());
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } break;
    case 8: {
      uint64_t *data_ptr = reinterpret_cast<uint64_t *>(buffer_sp->GetBytes());
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } break;
    default:
      lldbassert(false && "pointer size is not 4 nor 8");
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

lldb_private::formatters::NSConstantDictionarySyntheticFrontEnd::
    NSConstantDictionarySyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {}

size_t lldb_private::formatters::NSConstantDictionarySyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    NSConstantDictionarySyntheticFrontEnd::CalculateNumChildren() {
  return m_size;
}

lldb::ChildCacheState
lldb_private::formatters::NSConstantDictionarySyntheticFrontEnd::Update() {
  ValueObjectSP valobj_sp = m_backend.GetSP();
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return lldb::ChildCacheState::eRefetch;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t valobj_addr = valobj_sp->GetValueAsUnsigned(0);
  m_size = process_sp->ReadUnsignedIntegerFromMemory(
      valobj_addr + 2 * m_ptr_size, m_ptr_size, 0, error);
  if (error.Fail())
    return lldb::ChildCacheState::eRefetch;
  m_keys_ptr =
      process_sp->ReadPointerFromMemory(valobj_addr + 3 * m_ptr_size, error);
  if (error.Fail())
    return lldb::ChildCacheState::eRefetch;
  m_objects_ptr =
      process_sp->ReadPointerFromMemory(valobj_addr + 4 * m_ptr_size, error);

  return error.Success() ? lldb::ChildCacheState::eReuse
                         : lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSConstantDictionarySyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP lldb_private::formatters::
    NSConstantDictionarySyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  uint32_t num_children = CalculateNumChildrenIgnoringErrors();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;
    ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
    if (!process_sp)
      return lldb::ValueObjectSP();

    for (unsigned int child = 0; child < num_children; ++child) {
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(
          m_keys_ptr + child * m_ptr_size, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(
          m_objects_ptr + child * m_ptr_size, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};
      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    NSDictionary1SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp.get()), m_pair(nullptr) {}

size_t lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  static const ConstString g_zero("[0]");
  return name == g_zero ? 0 : UINT32_MAX;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    NSDictionary1SyntheticFrontEnd::CalculateNumChildren() {
  return 1;
}

lldb::ChildCacheState
lldb_private::formatters::NSDictionary1SyntheticFrontEnd::Update() {
  m_pair.reset();
  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSDictionary1SyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSDictionary1SyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (idx != 0)
    return lldb::ValueObjectSP();

  if (m_pair.get())
    return m_pair;

  auto process_sp(m_backend.GetProcessSP());
  if (!process_sp)
    return nullptr;

  auto ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t key_ptr =
      m_backend.GetValueAsUnsigned(LLDB_INVALID_ADDRESS) + ptr_size;
  lldb::addr_t value_ptr = key_ptr + ptr_size;

  Status error;

  lldb::addr_t value_at_idx = process_sp->ReadPointerFromMemory(key_ptr, error);
  if (error.Fail())
    return nullptr;
  lldb::addr_t key_at_idx = process_sp->ReadPointerFromMemory(value_ptr, error);
  if (error.Fail())
    return nullptr;

  auto pair_type =
      GetLLDBNSPairType(process_sp->GetTarget().shared_from_this());

  WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * ptr_size, 0));

  if (ptr_size == 8) {
    uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
    *data_ptr = key_at_idx;
    *(data_ptr + 1) = value_at_idx;
  } else {
    uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
    *data_ptr = key_at_idx;
    *(data_ptr + 1) = value_at_idx;
  }

  DataExtractor data(buffer_sp, process_sp->GetByteOrder(), ptr_size);
  m_pair = CreateValueObjectFromData(
      "[0]", data, m_backend.GetExecutionContextRef(), pair_type);

  return m_pair;
}

template <typename D32, typename D64>
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32, D64>::
    GenericNSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(),
      m_data_32(nullptr), m_data_64(nullptr), m_pair_type() {}

template <typename D32, typename D64>
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<
    D32, D64>::GenericNSDictionaryMSyntheticFrontEnd::
    ~GenericNSDictionaryMSyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

template <typename D32, typename D64>
size_t lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<
    D32, D64>::GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

template <typename D32, typename D64>
llvm::Expected<uint32_t>
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<
    D32, D64>::CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? (uint32_t)m_data_32->_used : (uint32_t)m_data_64->_used);
}

template <typename D32, typename D64>
lldb::ChildCacheState
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,
                                                                D64>::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  m_ptr_size = 0;
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return lldb::ChildCacheState::eRefetch;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new D32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(D32),
                           error);
  } else {
    m_data_64 = new D64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(D64),
                           error);
  }

  return error.Success() ? lldb::ChildCacheState::eReuse
                         : lldb::ChildCacheState::eRefetch;
}

template <typename D32, typename D64>
bool
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<D32,D64>::
    MightHaveChildren() {
  return true;
}

template <typename D32, typename D64>
lldb::ValueObjectSP
lldb_private::formatters::GenericNSDictionaryMSyntheticFrontEnd<
    D32, D64>::GetChildAtIndex(uint32_t idx) {
  lldb::addr_t m_keys_ptr;
  lldb::addr_t m_values_ptr;
  if (m_data_32) {
    uint32_t size = m_data_32->GetSize();
    m_keys_ptr = m_data_32->_buffer;
    m_values_ptr = m_data_32->_buffer + (m_ptr_size * size);
  } else {
    uint32_t size = m_data_64->GetSize();
    m_keys_ptr = m_data_64->_buffer;
    m_values_ptr = m_data_64->_buffer + (m_ptr_size * size);
  }

  uint32_t num_children = CalculateNumChildrenIgnoringErrors();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_keys_ptr + (test_idx * m_ptr_size);
      val_at_idx = m_values_ptr + (test_idx * m_ptr_size);
      ;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

lldb_private::formatters::Foundation1100::NSDictionaryMSyntheticFrontEnd::
    NSDictionaryMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_pair_type() {}

lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::~NSDictionaryMSyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

size_t
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

llvm::Expected<uint32_t> lldb_private::formatters::Foundation1100::
    NSDictionaryMSyntheticFrontEnd::CalculateNumChildren() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return (m_data_32 ? m_data_32->_used : m_data_64->_used);
}

lldb::ChildCacheState lldb_private::formatters::Foundation1100::
    NSDictionaryMSyntheticFrontEnd::Update() {
  m_children.clear();
  ValueObjectSP valobj_sp = m_backend.GetSP();
  m_ptr_size = 0;
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
  if (!valobj_sp)
    return lldb::ChildCacheState::eRefetch;
  m_exe_ctx_ref = valobj_sp->GetExecutionContextRef();
  Status error;
  error.Clear();
  lldb::ProcessSP process_sp(valobj_sp->GetProcessSP());
  if (!process_sp)
    return lldb::ChildCacheState::eRefetch;
  m_ptr_size = process_sp->GetAddressByteSize();
  m_order = process_sp->GetByteOrder();
  uint64_t data_location = valobj_sp->GetValueAsUnsigned(0) + m_ptr_size;
  if (m_ptr_size == 4) {
    m_data_32 = new DataDescriptor_32();
    process_sp->ReadMemory(data_location, m_data_32, sizeof(DataDescriptor_32),
                           error);
  } else {
    m_data_64 = new DataDescriptor_64();
    process_sp->ReadMemory(data_location, m_data_64, sizeof(DataDescriptor_64),
                           error);
  }

  return error.Success() ? lldb::ChildCacheState::eReuse
                         : lldb::ChildCacheState::eRefetch;
}

bool
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::Foundation1100::
  NSDictionaryMSyntheticFrontEnd::GetChildAtIndex(uint32_t idx) {
  lldb::addr_t m_keys_ptr =
      (m_data_32 ? m_data_32->_keys_addr : m_data_64->_keys_addr);
  lldb::addr_t m_values_ptr =
      (m_data_32 ? m_data_32->_objs_addr : m_data_64->_objs_addr);

  uint32_t num_children = CalculateNumChildrenIgnoringErrors();

  if (idx >= num_children)
    return lldb::ValueObjectSP();

  if (m_children.empty()) {
    // do the scan phase
    lldb::addr_t key_at_idx = 0, val_at_idx = 0;

    uint32_t tries = 0;
    uint32_t test_idx = 0;

    while (tries < num_children) {
      key_at_idx = m_keys_ptr + (test_idx * m_ptr_size);
      val_at_idx = m_values_ptr + (test_idx * m_ptr_size);
      ;
      ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
      if (!process_sp)
        return lldb::ValueObjectSP();
      Status error;
      key_at_idx = process_sp->ReadPointerFromMemory(key_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();
      val_at_idx = process_sp->ReadPointerFromMemory(val_at_idx, error);
      if (error.Fail())
        return lldb::ValueObjectSP();

      test_idx++;

      if (!key_at_idx || !val_at_idx)
        continue;
      tries++;

      DictionaryItemDescriptor descriptor = {key_at_idx, val_at_idx,
                                             lldb::ValueObjectSP()};

      m_children.push_back(descriptor);
    }
  }

  if (idx >= m_children.size()) // should never happen
    return lldb::ValueObjectSP();

  DictionaryItemDescriptor &dict_item = m_children[idx];
  if (!dict_item.valobj_sp) {
    if (!m_pair_type.IsValid()) {
      TargetSP target_sp(m_backend.GetTargetSP());
      if (!target_sp)
        return ValueObjectSP();
      m_pair_type = GetLLDBNSPairType(target_sp);
    }
    if (!m_pair_type.IsValid())
      return ValueObjectSP();

    WritableDataBufferSP buffer_sp(new DataBufferHeap(2 * m_ptr_size, 0));

    if (m_ptr_size == 8) {
      uint64_t *data_ptr = (uint64_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    } else {
      uint32_t *data_ptr = (uint32_t *)buffer_sp->GetBytes();
      *data_ptr = dict_item.key_ptr;
      *(data_ptr + 1) = dict_item.val_ptr;
    }

    StreamString idx_name;
    idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
    DataExtractor data(buffer_sp, m_order, m_ptr_size);
    dict_item.valobj_sp = CreateValueObjectFromData(idx_name.GetString(), data,
                                                    m_exe_ctx_ref, m_pair_type);
  }
  return dict_item.valobj_sp;
}

template bool lldb_private::formatters::NSDictionarySummaryProvider<true>(
    ValueObject &, Stream &, const TypeSummaryOptions &);

template bool lldb_private::formatters::NSDictionarySummaryProvider<false>(
    ValueObject &, Stream &, const TypeSummaryOptions &);
