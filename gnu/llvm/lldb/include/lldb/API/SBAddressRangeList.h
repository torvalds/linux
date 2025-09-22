//===-- SBAddressRangeList.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBADDRESSRANGELIST_H
#define LLDB_API_SBADDRESSRANGELIST_H

#include <memory>

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class AddressRangeListImpl;
}

namespace lldb {

class LLDB_API SBAddressRangeList {
public:
  SBAddressRangeList();

  SBAddressRangeList(const lldb::SBAddressRangeList &rhs);

  ~SBAddressRangeList();

  const lldb::SBAddressRangeList &
  operator=(const lldb::SBAddressRangeList &rhs);

  uint32_t GetSize() const;

  void Clear();

  SBAddressRange GetAddressRangeAtIndex(uint64_t idx);

  void Append(const lldb::SBAddressRange &addr_range);

  void Append(const lldb::SBAddressRangeList &addr_range_list);

  bool GetDescription(lldb::SBStream &description, const SBTarget &target);

private:
  friend class SBBlock;
  friend class SBProcess;

  lldb_private::AddressRangeListImpl &ref() const;

  std::unique_ptr<lldb_private::AddressRangeListImpl> m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBADDRESSRANGELIST_H
