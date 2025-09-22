//===-- LibCxxAtomic.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxxAtomic.h"
#include "lldb/DataFormatters/FormattersHelpers.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

//
// We are supporting two versions of libc++ std::atomic
//
// Given std::atomic<int> i;
//
// The previous version of std::atomic was laid out like this
//
// (lldb) frame var -L -R i
// 0x00007ffeefbff9a0: (std::__1::atomic<int>) i = {
// 0x00007ffeefbff9a0:   std::__1::__atomic_base<int, true> = {
// 0x00007ffeefbff9a0:     std::__1::__atomic_base<int, false> = {
// 0x00007ffeefbff9a0:       __a_ = 5
//        }
//    }
// }
//
// In this case we need to obtain __a_ and the current version is laid out as so
//
// (lldb) frame var -L -R i
// 0x00007ffeefbff9b0: (std::__1::atomic<int>) i = {
// 0x00007ffeefbff9b0:   std::__1::__atomic_base<int, true> = {
// 0x00007ffeefbff9b0:     std::__1::__atomic_base<int, false> = {
// 0x00007ffeefbff9b0:       __a_ = {
// 0x00007ffeefbff9b0:         std::__1::__cxx_atomic_base_impl<int> = {
// 0x00007ffeefbff9b0:           __a_value = 5
//                }
//          }
//       }
//    }
//}
//
// In this case we need to obtain __a_value
//
// The below method covers both cases and returns the relevant member as a
// ValueObjectSP
//
ValueObjectSP
lldb_private::formatters::GetLibCxxAtomicValue(ValueObject &valobj) {
  ValueObjectSP non_sythetic = valobj.GetNonSyntheticValue();
  if (!non_sythetic)
    return {};

  ValueObjectSP member__a_ = non_sythetic->GetChildMemberWithName("__a_");
  if (!member__a_)
    return {};

  ValueObjectSP member__a_value =
      member__a_->GetChildMemberWithName("__a_value");
  if (!member__a_value)
    return member__a_;

  return member__a_value;
}

bool lldb_private::formatters::LibCxxAtomicSummaryProvider(
    ValueObject &valobj, Stream &stream, const TypeSummaryOptions &options) {

  if (ValueObjectSP atomic_value = GetLibCxxAtomicValue(valobj)) {
    std::string summary;
    if (atomic_value->GetSummaryAsCString(summary, options) &&
        summary.size() > 0) {
      stream.Printf("%s", summary.c_str());
      return true;
    }
  }

  return false;
}

namespace lldb_private {
namespace formatters {
class LibcxxStdAtomicSyntheticFrontEnd : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdAtomicSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdAtomicSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override;

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override;

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override;

  size_t GetIndexOfChildWithName(ConstString name) override;

private:
  ValueObject *m_real_child = nullptr;
};
} // namespace formatters
} // namespace lldb_private

lldb_private::formatters::LibcxxStdAtomicSyntheticFrontEnd::
    LibcxxStdAtomicSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdAtomicSyntheticFrontEnd::Update() {
  ValueObjectSP atomic_value = GetLibCxxAtomicValue(m_backend);
  if (atomic_value)
    m_real_child = GetLibCxxAtomicValue(m_backend).get();

  return lldb::ChildCacheState::eRefetch;
}

bool lldb_private::formatters::LibcxxStdAtomicSyntheticFrontEnd::
    MightHaveChildren() {
  return true;
}

llvm::Expected<uint32_t> lldb_private::formatters::
    LibcxxStdAtomicSyntheticFrontEnd::CalculateNumChildren() {
  return m_real_child ? 1 : 0;
}

lldb::ValueObjectSP
lldb_private::formatters::LibcxxStdAtomicSyntheticFrontEnd::GetChildAtIndex(
    uint32_t idx) {
  if (idx == 0)
    return m_real_child->GetSP()->Clone(ConstString("Value"));
  return nullptr;
}

size_t lldb_private::formatters::LibcxxStdAtomicSyntheticFrontEnd::
    GetIndexOfChildWithName(ConstString name) {
  return name == "Value" ? 0 : UINT32_MAX;
}

SyntheticChildrenFrontEnd *
lldb_private::formatters::LibcxxAtomicSyntheticFrontEndCreator(
    CXXSyntheticChildren *, lldb::ValueObjectSP valobj_sp) {
  if (valobj_sp)
    return new LibcxxStdAtomicSyntheticFrontEnd(valobj_sp);
  return nullptr;
}
