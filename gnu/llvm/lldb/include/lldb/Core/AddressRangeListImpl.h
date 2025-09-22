//===-- AddressRangeListImpl.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_CORE_ADDRESSRANGELISTIMPL_H
#define LLDB_CORE_ADDRESSRANGELISTIMPL_H

#include "lldb/Core/AddressRange.h"
#include <cstddef>

namespace lldb {
class SBAddressRangeList;
class SBBlock;
class SBProcess;
}

namespace lldb_private {

class AddressRangeListImpl {
public:
  AddressRangeListImpl();

  AddressRangeListImpl(const AddressRangeListImpl &rhs) = default;

  AddressRangeListImpl &operator=(const AddressRangeListImpl &rhs);

  size_t GetSize() const;

  void Reserve(size_t capacity);

  void Append(const AddressRange &sb_region);

  void Append(const AddressRangeListImpl &list);

  void Clear();

  lldb_private::AddressRange GetAddressRangeAtIndex(size_t index);

private:
  friend class lldb::SBAddressRangeList;
  friend class lldb::SBBlock;
  friend class lldb::SBProcess;

  AddressRanges &ref();

  AddressRanges m_ranges;
};

} // namespace lldb_private

#endif // LLDB_CORE_ADDRESSRANGE_H
