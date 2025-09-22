//===-- LibCxxRangesRefView.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LibCxx.h"

#include "lldb/Core/ValueObject.h"
#include "lldb/DataFormatters/FormattersHelpers.h"
#include "lldb/Utility/ConstString.h"
#include "llvm/ADT/APSInt.h"

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::formatters;

namespace lldb_private {
namespace formatters {

class LibcxxStdRangesRefViewSyntheticFrontEnd
    : public SyntheticChildrenFrontEnd {
public:
  LibcxxStdRangesRefViewSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp);

  ~LibcxxStdRangesRefViewSyntheticFrontEnd() override = default;

  llvm::Expected<uint32_t> CalculateNumChildren() override {
    // __range_ will be the sole child of this type
    return 1;
  }

  lldb::ValueObjectSP GetChildAtIndex(uint32_t idx) override {
    // Since we only have a single child, return it
    assert(idx == 0);
    return m_range_sp;
  }

  lldb::ChildCacheState Update() override;

  bool MightHaveChildren() override { return true; }

  size_t GetIndexOfChildWithName(ConstString name) override {
    // We only have a single child
    return 0;
  }

private:
  /// Pointer to the dereferenced __range_ member
  lldb::ValueObjectSP m_range_sp = nullptr;
};

lldb_private::formatters::LibcxxStdRangesRefViewSyntheticFrontEnd::
    LibcxxStdRangesRefViewSyntheticFrontEnd(lldb::ValueObjectSP valobj_sp)
    : SyntheticChildrenFrontEnd(*valobj_sp) {
  if (valobj_sp)
    Update();
}

lldb::ChildCacheState
lldb_private::formatters::LibcxxStdRangesRefViewSyntheticFrontEnd::Update() {
  ValueObjectSP range_ptr =
      GetChildMemberWithName(m_backend, {ConstString("__range_")});
  if (!range_ptr)
    return lldb::ChildCacheState::eRefetch;

  lldb_private::Status error;
  m_range_sp = range_ptr->Dereference(error);

  return error.Success() ? lldb::ChildCacheState::eReuse
                         : lldb::ChildCacheState::eRefetch;
}

lldb_private::SyntheticChildrenFrontEnd *
LibcxxStdRangesRefViewSyntheticFrontEndCreator(CXXSyntheticChildren *,
                                               lldb::ValueObjectSP valobj_sp) {
  if (!valobj_sp)
    return nullptr;
  CompilerType type = valobj_sp->GetCompilerType();
  if (!type.IsValid())
    return nullptr;
  return new LibcxxStdRangesRefViewSyntheticFrontEnd(valobj_sp);
}

} // namespace formatters
} // namespace lldb_private
