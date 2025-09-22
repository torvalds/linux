//===-- NSArray.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TargetInfo.h"

#include "Cocoa.h"

#include "Plugins/LanguageRuntime/ObjC/AppleObjCRuntime/AppleObjCRuntime.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/Core/ValueObjectConstResult.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Expression/FunctionCaller.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataBufferHeap.h"
#include "lldb/Utility/Endian.h"
#include "lldb/Utility/Status.h"
#include "lldb/Utility/Stream.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {
std::map<ConstString, CXXFunctionSummaryFormat::Callback> &
NSArray_Additionals::GetAdditionalSummaries() {
  static std::map<ConstString, CXXFunctionSummaryFormat::Callback> g_map;
  return g_map;
}

std::map<ConstString, CXXSyntheticChildren::CreateFrontEndCallback> &
NSArray_Additionals::GetAdditionalSynthetics() {
  static std::map<ConstString, CXXSyntheticChildren::CreateFrontEndCallback>
      g_map;
  return g_map;
}

class NSArrayMSyntheticFrontEndBase : public SyntheticChildrenFrontEnd {
public:
  NSArrayMSyntheticFrontEndBase(lldb::ValueObjectSP valobj_sp);

  ~NSArrayMSyntheticFrontEndBase() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override = 0;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

protected:
  virtual lldb::addr_t GetDataAddress() = 0;

  virtual uint64_t GetUsedCount() = 0;

  virtual uint64_t GetOffset() = 0;

  virtual uint64_t GetSize() = 0;

  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size = 8;
  CompilerType m_id_type;
};

template <typename D32, typename D64>
class GenericNSArrayMSyntheticFrontEnd : public NSArrayMSyntheticFrontEndBase {
public:
  GenericNSArrayMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~GenericNSArrayMSyntheticFrontEnd() override;

  lldb::ChildCacheState Update() override;

protected:
  lldb::addr_t GetDataAddress() override;

  uint64_t GetUsedCount() override;

  uint64_t GetOffset() override;

  uint64_t GetSize() override;

private:
  D32 *m_data_32;
  D64 *m_data_64;
};

namespace Foundation1010 {
  namespace {
    struct DataDescriptor_32 {
      uint32_t _used;
      uint32_t _offset;
      uint32_t _size : 28;
      uint64_t _priv1 : 4;
      uint32_t _priv2;
      uint32_t _data;
    };

    struct DataDescriptor_64 {
      uint64_t _used;
      uint64_t _offset;
      uint64_t _size : 60;
      uint64_t _priv1 : 4;
      uint32_t _priv2;
      uint64_t _data;
    };
  }

  using NSArrayMSyntheticFrontEnd =
      GenericNSArrayMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
}

namespace Foundation1428 {
  namespace {
    struct DataDescriptor_32 {
      uint32_t _used;
      uint32_t _offset;
      uint32_t _size;
      uint32_t _data;
    };

    struct DataDescriptor_64 {
      uint64_t _used;
      uint64_t _offset;
      uint64_t _size;
      uint64_t _data;
    };
  }

  using NSArrayMSyntheticFrontEnd =
      GenericNSArrayMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
}

namespace Foundation1437 {
  template <typename PtrType>
  struct DataDescriptor {
    PtrType _cow;
    // __deque
    PtrType _data;
    uint32_t _offset;
    uint32_t _size;
    uint32_t _muts;
    uint32_t _used;
  };

  using NSArrayMSyntheticFrontEnd =
     GenericNSArrayMSyntheticFrontEnd<
        DataDescriptor<uint32_t>, DataDescriptor<uint64_t>>;

  template <typename DD>
  uint64_t
  __NSArrayMSize_Impl(lldb_private::Process &process,
                      lldb::addr_t valobj_addr, Status &error) {
    const lldb::addr_t start_of_descriptor =
    valobj_addr + process.GetAddressByteSize();
    DD descriptor = DD();
    process.ReadMemory(start_of_descriptor, &descriptor,
                       sizeof(descriptor), error);
    if (error.Fail()) {
      return 0;
    }
    return descriptor._used;
  }

  uint64_t
  __NSArrayMSize(lldb_private::Process &process, lldb::addr_t valobj_addr,
                 Status &error) {
    if (process.GetAddressByteSize() == 4) {
      return __NSArrayMSize_Impl<DataDescriptor<uint32_t>>(process, valobj_addr,
                                                           error);
    } else {
      return __NSArrayMSize_Impl<DataDescriptor<uint64_t>>(process, valobj_addr,
                                                           error);
    }
  }

}

namespace CallStackArray {
struct DataDescriptor_32 {
  uint32_t _data;
  uint32_t _used;
  uint32_t _offset;
  const uint32_t _size = 0;
};

struct DataDescriptor_64 {
  uint64_t _data;
  uint64_t _used;
  uint64_t _offset;
  const uint64_t _size = 0;
};

using NSCallStackArraySyntheticFrontEnd =
    GenericNSArrayMSyntheticFrontEnd<DataDescriptor_32, DataDescriptor_64>;
} // namespace CallStackArray

template <typename D32, typename D64, bool Inline>
class GenericNSArrayISyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  GenericNSArrayISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~GenericNSArrayISyntheticFrontEnd() override;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ExecutionContextRef m_exe_ctx_ref;
  uint8_t m_ptr_size = 8;

  D32 *m_data_32;
  D64 *m_data_64;
  CompilerType m_id_type;
};

namespace Foundation1300 {
    struct IDD32 {
        uint32_t used;
        uint32_t list;
    };

    struct IDD64 {
        uint64_t used;
        uint64_t list;
    };

    using NSArrayISyntheticFrontEnd =
        GenericNSArrayISyntheticFrontEnd<IDD32, IDD64, true>;
}

namespace Foundation1430 {
    using NSArrayISyntheticFrontEnd =
        Foundation1428::NSArrayMSyntheticFrontEnd;
}

namespace Foundation1436 {
    struct IDD32 {
        uint32_t used;
        uint32_t list; // in Inline cases, this is the first element
    };

    struct IDD64 {
        uint64_t used;
        uint64_t list; // in Inline cases, this is the first element
    };

    using NSArrayI_TransferSyntheticFrontEnd =
        GenericNSArrayISyntheticFrontEnd<IDD32, IDD64, false>;

    using NSArrayISyntheticFrontEnd =
        GenericNSArrayISyntheticFrontEnd<IDD32, IDD64, true>;

    using NSFrozenArrayMSyntheticFrontEnd =
        Foundation1437::NSArrayMSyntheticFrontEnd;

    uint64_t
    __NSFrozenArrayMSize(lldb_private::Process &process, lldb::addr_t valobj_addr,
                         Status &error) {
      return Foundation1437::__NSArrayMSize(process, valobj_addr, error);
    }
}

namespace ConstantArray {

struct ConstantArray32 {
  uint64_t used;
  uint32_t list;
};

struct ConstantArray64 {
  uint64_t used;
  uint64_t list;
};

using NSConstantArraySyntheticFrontEnd =
    GenericNSArrayISyntheticFrontEnd<ConstantArray32, ConstantArray64, false>;
} // namespace ConstantArray

class NSArray0SyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSArray0SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSArray0SyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;
};

class NSArray1SyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  NSArray1SyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~NSArray1SyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;
};
} // namespace formatters
} // namespace lldb_private

bool lldb_private::formatters::NSArraySummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {
  static constexpr llvm::StringLiteral g_TypeHint("NSArray");

  ProcessSP process_sp = valobj.GetProcessSP();
  if (!process_sp)
    return false;

  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp);

  if (!runtime)
    return false;

  ObjCLanguageRuntime::ClassDescriptorSP descriptor(
      runtime->GetClassDescriptor(valobj));

  if (!descriptor || !descriptor->IsValid())
    return false;

  uint32_t ptr_size = process_sp->GetAddressByteSize();

  lldb::addr_t valobj_addr = valobj.GetValueAsUnsigned(0);

  if (!valobj_addr)
    return false;

  uint64_t value = 0;

  ConstString class_name(descriptor->GetClassName());

  static const ConstString g_NSArrayI("__NSArrayI");
  static const ConstString g_NSArrayM("__NSArrayM");
  static const ConstString g_NSArrayI_Transfer("__NSArrayI_Transfer");
  static const ConstString g_NSFrozenArrayM("__NSFrozenArrayM");
  static const ConstString g_NSArray0("__NSArray0");
  static const ConstString g_NSArray1("__NSSingleObjectArrayI");
  static const ConstString g_NSArrayCF("__NSCFArray");
  static const ConstString g_NSArrayMLegacy("__NSArrayM_Legacy");
  static const ConstString g_NSArrayMImmutable("__NSArrayM_Immutable");
  static const ConstString g_NSCallStackArray("_NSCallStackArray");
  static const ConstString g_NSConstantArray("NSConstantArray");

  if (class_name.IsEmpty())
    return false;

  if (class_name == g_NSArrayI) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSConstantArray) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size, 8,
                                                      0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSArrayM) {
    AppleObjCRuntime *apple_runtime =
    llvm::dyn_cast_or_null<AppleObjCRuntime>(runtime);
    Status error;
    if (apple_runtime && apple_runtime->GetFoundationVersion() >= 1437) {
      value = Foundation1437::__NSArrayMSize(*process_sp, valobj_addr, error);
    } else {
      value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                        ptr_size, 0, error);
    }
    if (error.Fail())
      return false;
  } else if (class_name == g_NSArrayI_Transfer) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSFrozenArrayM) {
    Status error;
    value = Foundation1436::__NSFrozenArrayMSize(*process_sp, valobj_addr, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSArrayMLegacy) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSArrayMImmutable) {
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(valobj_addr + ptr_size,
                                                      ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else if (class_name == g_NSArray0) {
    value = 0;
  } else if (class_name == g_NSArray1) {
    value = 1;
  } else if (class_name == g_NSArrayCF || class_name == g_NSCallStackArray) {
    // __NSCFArray and _NSCallStackArray store the number of elements as a
    // pointer-sized value at offset `2 * ptr_size`.
    Status error;
    value = process_sp->ReadUnsignedIntegerFromMemory(
        valobj_addr + 2 * ptr_size, ptr_size, 0, error);
    if (error.Fail())
      return false;
  } else {
    auto &map(NSArray_Additionals::GetAdditionalSummaries());
    auto iter = map.find(class_name), end = map.end();
    if (iter != end)
      return iter->second(valobj, stream, options);
    else
      return false;
  }

  llvm::StringRef prefix, suffix;
  if (Language *language = Language::FindPlugin(options.GetLanguage()))
    std::tie(prefix, suffix) = language->GetFormatterPrefixSuffix(g_TypeHint);

  stream << prefix;
  stream.Printf("%" PRIu64 " %s%s", value, "element", value == 1 ? "" : "s");
  stream << suffix;
  return true;
}

lldb_private::formatters::NSArrayMSyntheticFrontEndBase::
    NSArrayMSyntheticFrontEndBase(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(), m_id_type() {
  if (valobj_sp) {
    TypeSystemClangSP scratch_ts_sp = ScratchTypeSystemClang::GetForTarget(
        *valobj_sp->GetExecutionContextRef().GetTargetSP());
    if (scratch_ts_sp)
      m_id_type = CompilerType(
          scratch_ts_sp->weak_from_this(),
          scratch_ts_sp->getASTContext().ObjCBuiltinIdTy.getAsOpaquePtr());
    if (valobj_sp->GetProcessSP())
      m_ptr_size = valobj_sp->GetProcessSP()->GetAddressByteSize();
  }
}

template <typename D32, typename D64>
lldb_private::formatters::
  GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : NSArrayMSyntheticFrontEndBase(valobj_sp), m_data_32(nullptr),
      m_data_64(nullptr) {}

llvm::Expected<uint32_t> lldb_private::formatters::
    NSArrayMSyntheticFrontEndBase::CalculateNumChildren() {
  return GetUsedCount();
}

lldb::ValueObjectSP
lldb_private::formatters::NSArrayMSyntheticFrontEndBase::GetChildAtIndex(
    uint32_t idx) {
  if (idx >= CalculateNumChildrenIgnoringErrors())
    return lldb::ValueObjectSP();
  lldb::addr_t object_at_idx = GetDataAddress();
  size_t pyhs_idx = idx;
  pyhs_idx += GetOffset();
  if (GetSize() <= pyhs_idx)
    pyhs_idx -= GetSize();
  object_at_idx += (pyhs_idx * m_ptr_size);
  StreamString idx_name;
  idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(idx_name.GetString(), object_at_idx,
                                      m_exe_ctx_ref, m_id_type);
}

template <typename D32, typename D64>
lldb::ChildCacheState
lldb_private::formatters::GenericNSArrayMSyntheticFrontEnd<D32, D64>::Update() {
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

bool
lldb_private::formatters::NSArrayMSyntheticFrontEndBase::MightHaveChildren() {
  return true;
}

size_t
lldb_private::formatters::NSArrayMSyntheticFrontEndBase::GetIndexOfChildWithName(
    ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

template <typename D32, typename D64>
lldb_private::formatters::GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd::~GenericNSArrayMSyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

template <typename D32, typename D64>
lldb::addr_t
lldb_private::formatters::
  GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd::GetDataAddress() {
  if (!m_data_32 && !m_data_64)
    return LLDB_INVALID_ADDRESS;
  return m_data_32 ? m_data_32->_data : m_data_64->_data;
}

template <typename D32, typename D64>
uint64_t
lldb_private::formatters::
  GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd::GetUsedCount() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return m_data_32 ? m_data_32->_used : m_data_64->_used;
}

template <typename D32, typename D64>
uint64_t
lldb_private::formatters::
  GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd::GetOffset() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return m_data_32 ? m_data_32->_offset : m_data_64->_offset;
}

template <typename D32, typename D64>
uint64_t
lldb_private::formatters::
  GenericNSArrayMSyntheticFrontEnd<D32, D64>::
    GenericNSArrayMSyntheticFrontEnd::GetSize() {
  if (!m_data_32 && !m_data_64)
    return 0;
  return m_data_32 ? m_data_32->_size : m_data_64->_size;
}

template <typename D32, typename D64, bool Inline>
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64, Inline>::
    GenericNSArrayISyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp), m_exe_ctx_ref(),
      m_data_32(nullptr), m_data_64(nullptr) {
  if (valobj_sp) {
    CompilerType type = valobj_sp->GetCompilerType();
    if (type) {
      TypeSystemClangSP scratch_ts_sp = ScratchTypeSystemClang::GetForTarget(
          *valobj_sp->GetExecutionContextRef().GetTargetSP());
      if (scratch_ts_sp)
        m_id_type = scratch_ts_sp->GetType(
            scratch_ts_sp->getASTContext().ObjCBuiltinIdTy);
    }
  }
}

template <typename D32, typename D64, bool Inline>
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64, Inline>::
    GenericNSArrayISyntheticFrontEnd::~GenericNSArrayISyntheticFrontEnd() {
  delete m_data_32;
  m_data_32 = nullptr;
  delete m_data_64;
  m_data_64 = nullptr;
}

template <typename D32, typename D64, bool Inline>
size_t
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64, Inline>::
  GetIndexOfChildWithName(ConstString name) {
  const char *item_name = name.GetCString();
  uint32_t idx = ExtractIndexFromString(item_name);
  if (idx < UINT32_MAX && idx >= CalculateNumChildrenIgnoringErrors())
    return UINT32_MAX;
  return idx;
}

template <typename D32, typename D64, bool Inline>
llvm::Expected<uint32_t>
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<
    D32, D64, Inline>::CalculateNumChildren() {
  return m_data_32 ? m_data_32->used : m_data_64->used;
}

template <typename D32, typename D64, bool Inline>
lldb::ChildCacheState
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64,
                                                           Inline>::Update() {
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

template <typename D32, typename D64, bool Inline>
bool
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64, Inline>::
  MightHaveChildren() {
  return true;
}

template <typename D32, typename D64, bool Inline>
lldb::ValueObjectSP
lldb_private::formatters::GenericNSArrayISyntheticFrontEnd<D32, D64, Inline>::
  GetChildAtIndex(uint32_t idx) {
  if (idx >= CalculateNumChildrenIgnoringErrors())
    return lldb::ValueObjectSP();
  lldb::addr_t object_at_idx;
  if (Inline) {
    object_at_idx = m_backend.GetSP()->GetValueAsUnsigned(0) + m_ptr_size;
    object_at_idx += m_ptr_size == 4 ? sizeof(D32) : sizeof(D64); // skip the data header
    object_at_idx -= m_ptr_size; // we treat the last entry in the data header as the first pointer
  } else {
    object_at_idx = m_data_32 ? m_data_32->list : m_data_64->list;
  }
  object_at_idx += (idx * m_ptr_size);

  ProcessSP process_sp = m_exe_ctx_ref.GetProcessSP();
  if (!process_sp)
    return lldb::ValueObjectSP();
  Status error;
  if (error.Fail())
    return lldb::ValueObjectSP();
  StreamString idx_name;
  idx_name.Printf("[%" PRIu64 "]", (uint64_t)idx);
  return CreateValueObjectFromAddress(idx_name.GetString(), object_at_idx,
                                      m_exe_ctx_ref, m_id_type);
}

lldb_private::formatters::NSArray0SyntheticFrontEnd::NSArray0SyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {}

size_t
lldb_private::formatters::NSArray0SyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  return UINT32_MAX;
}

llvm::Expected<uint32_t>
lldb_private::formatters::NSArray0SyntheticFrontEnd::CalculateNumChildren() {
  return 0;
}

lldb::ChildCacheState
lldb_private::formatters::NSArray0SyntheticFrontEnd::Update() {
  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSArray0SyntheticFrontEnd::MightHaveChildren() {
  return false;
}

lldb::ValueObjectSP
lldb_private::formatters::NSArray0SyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  return lldb::ValueObjectSP();
}

lldb_private::formatters::NSArray1SyntheticFrontEnd::NSArray1SyntheticFrontEnd(
    lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp.get()) {}

size_t
lldb_private::formatters::NSArray1SyntheticFrontEnd::GetIndexOfChildWithName(
    ConstString name) {
  static const ConstString g_zero("[0]");

  if (name == g_zero)
    return 0;

  return UINT32_MAX;
}

llvm::Expected<uint32_t>
lldb_private::formatters::NSArray1SyntheticFrontEnd::CalculateNumChildren() {
  return 1;
}

lldb::ChildCacheState
lldb_private::formatters::NSArray1SyntheticFrontEnd::Update() {
  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::NSArray1SyntheticFrontEnd::MightHaveChildren() {
  return true;
}

lldb::ValueObjectSP
lldb_private::formatters::NSArray1SyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  static const ConstString g_zero("[0]");

  if (idx == 0) {
    TypeSystemClangSP scratch_ts_sp =
        ScratchTypeSystemClang::GetForTarget(*m_backend.GetTargetSP());
    if (scratch_ts_sp) {
      CompilerType id_type(scratch_ts_sp->GetBasicType(lldb::eBasicTypeObjCID));
      return m_backend.GetSyntheticChildAtOffset(
          m_backend.GetProcessSP()->GetAddressByteSize(), id_type, true,
          g_zero);
    }
  }
  return lldb::ValueObjectSP();
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::NSArraySyntheticFrontEndCreator(
    CXXSyntheticChildren *synth, lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;

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

  static const ConstString g_NSArrayI("__NSArrayI");
  static const ConstString g_NSConstantArray("NSConstantArray");
  static const ConstString g_NSArrayI_Transfer("__NSArrayI_Transfer");
  static const ConstString g_NSFrozenArrayM("__NSFrozenArrayM");
  static const ConstString g_NSArrayM("__NSArrayM");
  static const ConstString g_NSArray0("__NSArray0");
  static const ConstString g_NSArray1("__NSSingleObjectArrayI");
  static const ConstString g_NSArrayMLegacy("__NSArrayM_Legacy");
  static const ConstString g_NSArrayMImmutable("__NSArrayM_Immutable");
  static const ConstString g_NSCallStackArray("_NSCallStackArray");

  if (class_name.IsEmpty())
    return nullptr;

  if (class_name == g_NSArrayI) {
    if (runtime->GetFoundationVersion() >= 1436)
      return (new Foundation1436::NSArrayISyntheticFrontEnd(valobj_sp));
    if (runtime->GetFoundationVersion() >= 1430)
      return (new Foundation1430::NSArrayISyntheticFrontEnd(valobj_sp));
    return (new Foundation1300::NSArrayISyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSArrayI_Transfer) {
      return (new Foundation1436::NSArrayI_TransferSyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSConstantArray) {
    return new ConstantArray::NSConstantArraySyntheticFrontEnd(valobj_sp);
  } else if (class_name == g_NSFrozenArrayM) {
    return (new Foundation1436::NSFrozenArrayMSyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSArray0) {
    return (new NSArray0SyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSArray1) {
    return (new NSArray1SyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSArrayM) {
    if (runtime->GetFoundationVersion() >= 1437)
      return (new Foundation1437::NSArrayMSyntheticFrontEnd(valobj_sp));
    if (runtime->GetFoundationVersion() >= 1428)
      return (new Foundation1428::NSArrayMSyntheticFrontEnd(valobj_sp));
    if (runtime->GetFoundationVersion() >= 1100)
      return (new Foundation1010::NSArrayMSyntheticFrontEnd(valobj_sp));
  } else if (class_name == g_NSCallStackArray) {
    return (new CallStackArray::NSCallStackArraySyntheticFrontEnd(valobj_sp));
  } else {
    auto &map(NSArray_Additionals::GetAdditionalSynthetics());
    auto iter = map.find(class_name), end = map.end();
    if (iter != end)
      return iter->second(synth, valobj_sp);
  }

  return nullptr;
}
