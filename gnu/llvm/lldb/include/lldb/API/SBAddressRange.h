//===-- SBAddressRange.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBADDRESSRANGE_H
#define LLDB_API_SBADDRESSRANGE_H

#include "lldb/API/SBDefines.h"

namespace lldb_private {
class AddressRange;
}

namespace lldb {

class LLDB_API SBAddressRange {
public:
  SBAddressRange();

  SBAddressRange(const lldb::SBAddressRange &rhs);

  SBAddressRange(lldb::SBAddress addr, lldb::addr_t byte_size);

  ~SBAddressRange();

  const lldb::SBAddressRange &operator=(const lldb::SBAddressRange &rhs);

  void Clear();

  /// Check the address range refers to a valid base address and has a byte
  /// size greater than zero.
  ///
  /// \return
  ///     True if the address range is valid, false otherwise.
  bool IsValid() const;

  /// Get the base address of the range.
  ///
  /// \return
  ///     Base address object.
  lldb::SBAddress GetBaseAddress() const;

  /// Get the byte size of this range.
  ///
  /// \return
  ///     The size in bytes of this address range.
  lldb::addr_t GetByteSize() const;

  bool operator==(const SBAddressRange &rhs);

  bool operator!=(const SBAddressRange &rhs);

  bool GetDescription(lldb::SBStream &description, const SBTarget target);

private:
  friend class SBAddressRangeList;
  friend class SBBlock;
  friend class SBFunction;
  friend class SBProcess;

  lldb_private::AddressRange &ref() const;

  AddressRangeUP m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBADDRESSRANGE_H
