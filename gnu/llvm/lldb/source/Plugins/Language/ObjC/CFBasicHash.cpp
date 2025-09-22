#include "CFBasicHash.h"

#include "lldb/Utility/Endian.h"

using namespace lldb;
using namespace lldb_private;

bool CFBasicHash::IsValid() const {
  if (m_address != LLDB_INVALID_ADDRESS) {
    if (m_ptr_size == 4 && m_ht_32)
      return true;
    else if (m_ptr_size == 8 && m_ht_64)
      return true;
    else
      return false;
  }
  return false;
}

bool CFBasicHash::Update(addr_t addr, ExecutionContextRef exe_ctx_rf) {
  if (addr == LLDB_INVALID_ADDRESS || !addr)
    return false;

  m_address = addr;
  m_exe_ctx_ref = exe_ctx_rf;
  m_ptr_size =
      m_exe_ctx_ref.GetTargetSP()->GetArchitecture().GetAddressByteSize();
  m_byte_order = m_exe_ctx_ref.GetTargetSP()->GetArchitecture().GetByteOrder();

  if (m_ptr_size == 4)
    return UpdateFor(m_ht_32);
  else if (m_ptr_size == 8)
    return UpdateFor(m_ht_64);
  return false;

  llvm_unreachable(
      "Unsupported architecture. Only 32bits and 64bits supported.");
}

template <typename T>
bool CFBasicHash::UpdateFor(std::unique_ptr<__CFBasicHash<T>> &m_ht) {
  if (m_byte_order != endian::InlHostByteOrder())
    return false;
  
  Status error;
  Target *target = m_exe_ctx_ref.GetTargetSP().get();
  addr_t addr = m_address.GetLoadAddress(target);
  size_t size = sizeof(typename __CFBasicHash<T>::RuntimeBase) +
                sizeof(typename __CFBasicHash<T>::Bits);

  m_ht = std::make_unique<__CFBasicHash<T>>();
  m_exe_ctx_ref.GetProcessSP()->ReadMemory(addr, m_ht.get(),
                                           size, error);
  if (error.Fail())
    return false;

  m_mutable = !(m_ht->base.cfinfoa & (1 << 6));
  m_multi = m_ht->bits.counts_offset;
  m_type = static_cast<HashType>(m_ht->bits.keys_offset);
  addr_t ptr_offset = addr + size;
  size_t ptr_count = GetPointerCount();
  size = ptr_count * sizeof(T);

  m_exe_ctx_ref.GetProcessSP()->ReadMemory(ptr_offset, m_ht->pointers, size,
                                           error);

  if (error.Fail()) {
    m_ht = nullptr;
    return false;
  }

  return true;
}

size_t CFBasicHash::GetCount() const {
  if (!IsValid())
    return 0;

  if (!m_multi)
    return (m_ptr_size == 4) ? m_ht_32->bits.used_buckets
                             : m_ht_64->bits.used_buckets;

  //  FIXME: Add support for multi
  return 0;
}

size_t CFBasicHash::GetPointerCount() const {
  if (!IsValid())
    return 0;

  if (m_multi)
    return 3; // Bits::counts_offset;
  return (m_type == HashType::dict) + 1;
}

addr_t CFBasicHash::GetKeyPointer() const {
  if (!IsValid())
    return LLDB_INVALID_ADDRESS;

  if (m_ptr_size == 4)
    return m_ht_32->pointers[m_ht_32->bits.keys_offset];

  return m_ht_64->pointers[m_ht_64->bits.keys_offset];
}

addr_t CFBasicHash::GetValuePointer() const {
  if (!IsValid())
    return LLDB_INVALID_ADDRESS;

  if (m_ptr_size == 4)
    return m_ht_32->pointers[0];

  return m_ht_64->pointers[0];
}
