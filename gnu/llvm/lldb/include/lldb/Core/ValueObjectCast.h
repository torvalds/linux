//===-- ValueObjectCast.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_VALUEOBJECTCAST_H
#define LLDB_CORE_VALUEOBJECTCAST_H

#include "lldb/Core/ValueObject.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/lldb-defines.h"
#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-forward.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace lldb_private {
class ConstString;

/// A ValueObject that represents a given value represented as a different type.
class ValueObjectCast : public ValueObject {
public:
  ~ValueObjectCast() override;

  static lldb::ValueObjectSP Create(ValueObject &parent,
                                    ConstString name,
                                    const CompilerType &cast_type);

  std::optional<uint64_t> GetByteSize() override;

  llvm::Expected<uint32_t> CalculateNumChildren(uint32_t max) override;

  lldb::ValueType GetValueType() const override;

  bool IsInScope() override;

  ValueObject *GetParent() override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

  const ValueObject *GetParent() const override {
    return ((m_parent != nullptr) ? m_parent->GetParent() : nullptr);
  }

protected:
  ValueObjectCast(ValueObject &parent, ConstString name,
                  const CompilerType &cast_type);

  bool UpdateValue() override;

  CompilerType GetCompilerTypeImpl() override;

  CompilerType m_cast_type;

private:
  ValueObjectCast(const ValueObjectCast &) = delete;
  const ValueObjectCast &operator=(const ValueObjectCast &) = delete;
};

} // namespace lldb_private

#endif // LLDB_CORE_VALUEOBJECTCAST_H
