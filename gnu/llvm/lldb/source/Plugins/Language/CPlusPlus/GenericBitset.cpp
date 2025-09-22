//===-- GenericBitset.cpp //-----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"
#include "LibStdcpp.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Target/Target.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

namespace {

/// This class can be used for handling bitsets from both libcxx and libstdcpp.
class GenericBitsetFrontEnd : public SyntheticChildrenFrontEnd {
public:
  enum class StdLib {
    LibCxx,
    LibStdcpp,
  };

  GenericBitsetFrontEnd(ValueObject &valobj, StdLib stdlib);

  size_t GetIndexOfChildWithName(ConstString name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  lldb::ChildCacheState Update() override;
  llvm::Expected<uint32_t> CalculateNumChildren() override {
    return m_elements.size();
  }
  ValueObjectSP GetChildAtIndex(uint32_t idx) override;

private:
  llvm::StringRef GetDataContainerMemberName();

  // The lifetime of a ValueObject and all its derivative ValueObjects
  // (children, clones, etc.) is managed by a ClusterManager. These
  // objects are only destroyed when every shared pointer to any of them
  // is destroyed, so we must not store a shared pointer to any ValueObject
  // derived from our backend ValueObject (since we're in the same cluster).
  // Value objects created from raw data (i.e. in a different cluster) must
  // be referenced via shared pointer to keep them alive, however.
  std::vector<ValueObjectSP> m_elements;
  ValueObject *m_first = nullptr;
  CompilerType m_bool_type;
  ByteOrder m_byte_order = eByteOrderInvalid;
  uint8_t m_byte_size = 0;
  StdLib m_stdlib;
};
} // namespace

GenericBitsetFrontEnd::GenericBitsetFrontEnd(ValueObject &valobj, StdLib stdlib)
    : SyntheticChildrenFrontEnd(valobj), m_stdlib(stdlib) {
  m_bool_type = valobj.GetCompilerType().GetBasicTypeFromAST(eBasicTypeBool);
  if (auto target_sp = m_backend.GetTargetSP()) {
    m_byte_order = target_sp->GetArchitecture().GetByteOrder();
    m_byte_size = target_sp->GetArchitecture().GetAddressByteSize();
    Update();
  }
}

llvm::StringRef GenericBitsetFrontEnd::GetDataContainerMemberName() {
  static constexpr llvm::StringLiteral s_libcxx_case("__first_");
  static constexpr llvm::StringLiteral s_libstdcpp_case("_M_w");
  switch (m_stdlib) {
  case StdLib::LibCxx:
    return s_libcxx_case;
  case StdLib::LibStdcpp:
    return s_libstdcpp_case;
  }
  llvm_unreachable("Unknown StdLib enum");
}

lldb::ChildCacheState GenericBitsetFrontEnd::Update() {
  m_elements.clear();
  m_first = nullptr;

  TargetSP target_sp = m_backend.GetTargetSP();
  if (!target_sp)
    return lldb::ChildCacheState::eRefetch;

  size_t size = 0;

  if (auto arg = m_backend.GetCompilerType().GetIntegralTemplateArgument(0))
    size = arg->value.getLimitedValue();

  m_elements.assign(size, ValueObjectSP());
  m_first =
      m_backend.GetChildMemberWithName(GetDataContainerMemberName()).get();
  return lldb::ChildCacheState::eRefetch;
}

ValueObjectSP GenericBitsetFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx >= m_elements.size() || !m_first)
    return ValueObjectSP();

  if (m_elements[idx])
    return m_elements[idx];

  ExecutionContext ctx = m_backend.GetExecutionContextRef().Lock(false);
  CompilerType type;
  ValueObjectSP chunk;
  // For small bitsets __first_ is not an array, but a plain size_t.
  if (m_first->GetCompilerType().IsArrayType(&type)) {
    std::optional<uint64_t> bit_size =
        type.GetBitSize(ctx.GetBestExecutionContextScope());
    if (!bit_size || *bit_size == 0)
      return {};
    chunk = m_first->GetChildAtIndex(idx / *bit_size);
  } else {
    type = m_first->GetCompilerType();
    chunk = m_first->GetSP();
  }
  if (!type || !chunk)
    return {};

  std::optional<uint64_t> bit_size =
      type.GetBitSize(ctx.GetBestExecutionContextScope());
  if (!bit_size || *bit_size == 0)
    return {};
  size_t chunk_idx = idx % *bit_size;
  uint8_t value = !!(chunk->GetValueAsUnsigned(0) & (uint64_t(1) << chunk_idx));
  DataExtractor data(&value, sizeof(value), m_byte_order, m_byte_size);

  m_elements[idx] = CreateValueObjectFromData(llvm::formatv("[{0}]", idx).str(),
                                              data, ctx, m_bool_type);

  return m_elements[idx];
}

SyntheticChildrenFrontEnd *formatters::LibStdcppBitsetSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new GenericBitsetFrontEnd(*valobj_sp,
                                     GenericBitsetFrontEnd::StdLib::LibStdcpp);
  return nullptr;
}

SyntheticChildrenFrontEnd *formatters::LibcxxBitsetSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new GenericBitsetFrontEnd(*valobj_sp,
                                     GenericBitsetFrontEnd::StdLib::LibCxx);
  return nullptr;
}
