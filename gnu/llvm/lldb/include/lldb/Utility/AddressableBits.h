//===-- AddressableBits.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_ADDRESSABLEBITS_H
#define LLDB_UTILITY_ADDRESSABLEBITS_H

#include "lldb/lldb-forward.h"
#include "lldb/lldb-public.h"

#include <cstdint>

namespace lldb_private {

/// \class AddressableBits AddressableBits.h "lldb/Core/AddressableBits.h"
/// A class which holds the metadata from a remote stub/corefile note
/// about how many bits are used for addressing on this target.
///
class AddressableBits {
public:
  AddressableBits() : m_low_memory_addr_bits(0), m_high_memory_addr_bits(0) {}

  /// When a single value is available for the number of bits.
  void SetAddressableBits(uint32_t addressing_bits);

  /// When we have separate values for low memory addresses and high memory
  /// addresses.
  void SetAddressableBits(uint32_t lowmem_addressing_bits,
                          uint32_t highmem_addressing_bits);

  void SetLowmemAddressableBits(uint32_t lowmem_addressing_bits);

  uint32_t GetLowmemAddressableBits() const;

  void SetHighmemAddressableBits(uint32_t highmem_addressing_bits);

  uint32_t GetHighmemAddressableBits() const;

  static lldb::addr_t AddressableBitToMask(uint32_t addressable_bits);

private:
  uint32_t m_low_memory_addr_bits;
  uint32_t m_high_memory_addr_bits;
};

} // namespace lldb_private

#endif // LLDB_UTILITY_ADDRESSABLEBITS_H
