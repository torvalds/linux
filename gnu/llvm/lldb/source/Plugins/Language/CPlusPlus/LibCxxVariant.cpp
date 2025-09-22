//===-- LibCxxVariant.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxxVariant.h"
#include "LibCxx.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/LLDBAssert.h"

#include "llvm/ADT/ScopeExit.h"
#include <optional>

using namespace lldb;
using namespace lldb_private;

// libc++ variant implementation contains two members that we care about both
// are contained in the __impl member.
// - __index which tells us which of the variadic template types is the active
//   type for the variant
// - __data is a variadic union which recursively contains itself as member
//   which refers to the tailing variadic types.
//   - __head which refers to the leading non pack type
//     - __value refers to the actual value contained
//   - __tail which refers to the remaining pack types
//
// e.g. given std::variant<int,double,char> v1
//
// (lldb) frame var -R v1.__impl.__data
//(... __union<... 0, int, double, char>) v1.__impl.__data = {
// ...
//  __head = {
//    __value = ...
//  }
//  __tail = {
//  ...
//    __head = {
//      __value = ...
//    }
//    __tail = {
//    ...
//      __head = {
//        __value = ...
//  ...
//
// So given
// - __index equal to 0 the active value is contained in
//
//     __data.__head.__value
//
// - __index equal to 1 the active value is contained in
//
//     __data.__tail.__head.__value
//
// - __index equal to 2 the active value is contained in
//
//      __data.__tail.__tail.__head.__value
//

namespace {
// libc++ std::variant index could have one of three states
// 1) Valid, we can obtain it and its not variant_npos
// 2) Invalid, we can't obtain it or it is not a type we expect
// 3) NPos, its value is variant_npos which means the variant has no value
enum class LibcxxVariantIndexValidity { Valid, Invalid, NPos };

uint64_t VariantNposValue(uint64_t index_byte_size) {
  switch (index_byte_size) {
  case 1:
    return static_cast<uint8_t>(-1);
  case 2:
    return static_cast<uint16_t>(-1);
  case 4:
    return static_cast<uint32_t>(-1);
  }
  lldbassert(false && "Unknown index type size");
  return static_cast<uint32_t>(-1); // Fallback to stable ABI type.
}

LibcxxVariantIndexValidity
LibcxxVariantGetIndexValidity(ValueObjectSP &impl_sp) {
  ValueObjectSP index_sp(impl_sp->GetChildMemberWithName("__index"));

  if (!index_sp)
    return LibcxxVariantIndexValidity::Invalid;

  // In the stable ABI, the type of __index is just int.
  // In the unstable ABI, where _LIBCPP_ABI_VARIANT_INDEX_TYPE_OPTIMIZATION is
  // enabled, the type can either be unsigned char/short/int depending on
  // how many variant types there are.
  // We only need to do this here when comparing against npos, because npos is
  // just `-1`, but that translates to different unsigned values depending on
  // the byte size.
  CompilerType index_type = index_sp->GetCompilerType();

  std::optional<uint64_t> index_type_bytes = index_type.GetByteSize(nullptr);
  if (!index_type_bytes)
    return LibcxxVariantIndexValidity::Invalid;

  uint64_t npos_value = VariantNposValue(*index_type_bytes);
  uint64_t index_value = index_sp->GetValueAsUnsigned(0);

  if (index_value == npos_value)
    return LibcxxVariantIndexValidity::NPos;

  return LibcxxVariantIndexValidity::Valid;
}

std::optional<uint64_t> LibcxxVariantIndexValue(ValueObjectSP &impl_sp) {
  ValueObjectSP index_sp(impl_sp->GetChildMemberWithName("__index"));

  if (!index_sp)
    return {};

  return {index_sp->GetValueAsUnsigned(0)};
}

ValueObjectSP LibcxxVariantGetNthHead(ValueObjectSP &impl_sp, uint64_t index) {
  ValueObjectSP data_sp(impl_sp->GetChildMemberWithName("__data"));

  if (!data_sp)
    return ValueObjectSP{};

  ValueObjectSP current_level = data_sp;
  for (uint64_t n = index; n != 0; --n) {
    ValueObjectSP tail_sp(current_level->GetChildMemberWithName("__tail"));

    if (!tail_sp)
      return ValueObjectSP{};

    current_level = tail_sp;
  }

  return current_level->GetChildMemberWithName("__head");
}
} // namespace

namespace lldb_private {
namespace formatters {
bool LibcxxVariantSummaryProvider(ValueObject &valobj, Stream &stream,
                                  const TypeSummaryOptions &options) {
  ValueObjectSP valobj_sp(valobj.GetNonSyntheticValue());
  if (!valobj_sp)
    return false;

  ValueObjectSP impl_sp = GetChildMemberWithName(
      *valobj_sp, {ConstString("__impl_"), ConstString("__impl")});

  if (!impl_sp)
    return false;

  LibcxxVariantIndexValidity validity = LibcxxVariantGetIndexValidity(impl_sp);

  if (validity == LibcxxVariantIndexValidity::Invalid)
    return false;

  if (validity == LibcxxVariantIndexValidity::NPos) {
    stream.Printf(" No Value");
    return true;
  }

  auto optional_index_value = LibcxxVariantIndexValue(impl_sp);

  if (!optional_index_value)
    return false;

  uint64_t index_value = *optional_index_value;

  ValueObjectSP nth_head = LibcxxVariantGetNthHead(impl_sp, index_value);

  if (!nth_head)
    return false;

  CompilerType head_type = nth_head->GetCompilerType();

  if (!head_type)
    return false;

  CompilerType template_type = head_type.GetTypeTemplateArgument(1);

  if (!template_type)
    return false;

  stream << " Active Type = " << template_type.GetDisplayTypeName() << " ";

  return true;
}
} // namespace formatters
} // namespace lldb_private

namespace {
class VariantFrontEnd : public SyntheticChildrenFrontEnd {
public:
  VariantFrontEnd(ValueObject &valobj) : SyntheticChildrenFrontEnd(valobj) {
    Update();
  }

  size_t GetIndexOfChildWithName(ConstString name) override {
    return formatters::ExtractIndexFromString(name.GetCString());
  }

  bool MightHaveChildren() override { return true; }
  lldb::ChildCacheState Update() override;
  llvm::Expected<uint32_t> CalculateNumChildren() override { return m_size; }
  ValueObjectSP GetChildAtIndex(uint32_t idx) override;

private:
  size_t m_size = 0;
};
} // namespace

lldb::ChildCacheState VariantFrontEnd::Update() {
  m_size = 0;
  ValueObjectSP impl_sp = formatters::GetChildMemberWithName(
      m_backend, {ConstString("__impl_"), ConstString("__impl")});
  if (!impl_sp)
    return lldb::ChildCacheState::eRefetch;

  LibcxxVariantIndexValidity validity = LibcxxVariantGetIndexValidity(impl_sp);

  if (validity == LibcxxVariantIndexValidity::Invalid)
    return lldb::ChildCacheState::eRefetch;

  if (validity == LibcxxVariantIndexValidity::NPos)
    return lldb::ChildCacheState::eReuse;

  m_size = 1;

  return lldb::ChildCacheState::eRefetch;
}

ValueObjectSP VariantFrontEnd::GetChildAtIndex(uint32_t idx) {
  if (idx >= m_size)
    return {};

  ValueObjectSP impl_sp = formatters::GetChildMemberWithName(
      m_backend, {ConstString("__impl_"), ConstString("__impl")});
  if (!impl_sp)
    return {};

  auto optional_index_value = LibcxxVariantIndexValue(impl_sp);

  if (!optional_index_value)
    return {};

  uint64_t index_value = *optional_index_value;

  ValueObjectSP nth_head = LibcxxVariantGetNthHead(impl_sp, index_value);

  if (!nth_head)
    return {};

  CompilerType head_type = nth_head->GetCompilerType();

  if (!head_type)
    return {};

  CompilerType template_type = head_type.GetTypeTemplateArgument(1);

  if (!template_type)
    return {};

  ValueObjectSP head_value(nth_head->GetChildMemberWithName("__value"));

  if (!head_value)
    return {};

  return head_value->Clone(ConstString("Value"));
}

SyntheticChildrenFrontEnd *
formatters::LibcxxVariantFrontEndCreator(CXXSyntheticChildren *,
                                         lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new VariantFrontEnd(*valobj_sp);
  return nullptr;
}
