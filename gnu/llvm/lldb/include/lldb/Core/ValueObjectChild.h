//===-- ValueObjectChild.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTCHILD_H
#define LLDB_CORE_VALUEOBJECTCHILD_H

#include "lldb/Core/ValueObject.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-private-enumerations.h"
#include "lldb/lldb-types.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {

/// A child of another ValueObject.
class ValueObjectChild : public ValueObject {
public:
  ~ValueObjectChild() override;

  std::optional<uint64_t> GetByteSize() override { return m_byte_size; }

  lldb::offset_t GetByteOffset() override { return m_byte_offset; }

  uint32_t GetBitfieldBitSize() override { return m_bitfield_bit_size; }

  uint32_t GetBitfieldBitOffset() override { return m_bitfield_bit_offset; }

  lldb::ValueType GetValueType() const override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool IsInScope() override;

  bool IsBaseClass() override { return m_is_base_class; }

  bool IsDereferenceOfParent() override { return m_is_deref_of_parent; }

protected:
  bool UpdateValue() override;

  LazyBool CanUpdateWithInvalidExecutionContext() override;

  CompilerType GetCompilerTypeImpl() override { return m_compiler_type; }

  CompilerType m_compiler_type;
  ConstString m_type_name;
  uint64_t m_byte_size;
  int32_t m_byte_offset;
  uint8_t m_bitfield_bit_size;
  uint8_t m_bitfield_bit_offset;
  bool m_is_base_class;
  bool m_is_deref_of_parent;
  std::optional<LazyBool> m_can_update_with_invalid_exe_ctx;

  friend class ValueObject;
  friend class ValueObjectConstResult;
  friend class ValueObjectConstResultImpl;
  friend class ValueObjectVTable;

  ValueObjectChild(ValueObject &parent, const CompilerType &compiler_type,
                   ConstString name, uint64_t byte_size,
                   int32_t byte_offset, uint32_t bitfield_bit_size,
                   uint32_t bitfield_bit_offset, bool is_base_class,
                   bool is_deref_of_parent,
                   AddressType child_ptr_or_ref_addr_type,
                   uint64_t language_flags);

  ValueObjectChild(const ValueObjectChild &) = delete;
  const ValueObjectChild &operator=(const ValueObjectChild &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTCHILD_H
