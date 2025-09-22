//===-- ValueObjectVTable.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTVTABLE_H
#define LLDB_CORE_VALUEOBJECTVTABLE_H

#include "lldb/Core/ValueObject.h"

namespace lldb_private {

/// A class that represents a virtual function table for a C++ class.
///
/// ValueObject::GetError() will be in the success state if this value
/// represents a C++ class with a vtable, or an appropriate error describing
/// that the object isn't a C++ class with a vtable or not a C++ class.
///
/// ValueObject::GetName() will be the demangled symbol name for the virtual
/// function table like "vtable for <classname>".
///
/// ValueObject::GetValueAsCString() will be the address of the first vtable
/// entry if the current ValueObject is a class with a vtable, or nothing the
/// current ValueObject is not a C++ class or not a C++ class that has a
/// vtable.
///
/// ValueObject::GetValueAtUnsigned(...) will return the address of the first
/// vtable entry.
///
/// ValueObject::GetAddressOf() will return the address of the vtable pointer
/// found in the parent ValueObject.
///
/// ValueObject::GetNumChildren() will return the number of virtual function
/// pointers in the vtable, or zero on error.
///
/// ValueObject::GetChildAtIndex(...) will return each virtual function pointer
/// as a ValueObject object.
///
/// The child ValueObjects will have the following values:
///
/// ValueObject::GetError() will indicate success if the vtable entry was
/// successfully read from memory, or an error if not.
///
/// ValueObject::GetName() will be the vtable function index in the form "[%u]"
/// where %u is the index.
///
/// ValueObject::GetValueAsCString() will be the virtual function pointer value
///
/// ValueObject::GetValueAtUnsigned(...) will return the virtual function
/// pointer value.
///
/// ValueObject::GetAddressOf() will return the address of the virtual function
/// pointer.
///
/// ValueObject::GetNumChildren() returns 0
class ValueObjectVTable : public ValueObject {
public:
  ~ValueObjectVTable() override;

  static lldb::ValueObjectSP Create(ValueObject &parent);

  std::optional<uint64_t> GetByteSize() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueType GetValueType() const override;

  ConstString GetTypeName() override;

  ConstString GetQualifiedTypeName() override;

  ConstString GetDisplayTypeName() override;

  bool IsInScope() override;

protected:
  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  /// The symbol for the C++ virtual function table.
  const Symbol *m_vtable_symbol = nullptr;
  /// Cache the number of vtable children when we update the value.
  uint32_t m_num_vtable_entries = 0;
  /// Cache the address size in bytes to avoid checking with the process to
  /// many times.
  uint32_t m_addr_size = 0;

private:
  ValueObjectVTable(ValueObject &parent);

  ValueObject *CreateChildAtIndex(size_t idx) override;
  ValueObject *CreateSyntheticArrayMember(size_t idx) override {
    return nullptr;
  }

  // For ValueObject only
  ValueObjectVTable(const ValueObjectVTable &) = delete;
  const ValueObjectVTable &operator=(const ValueObjectVTable &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTVTABLE_H
