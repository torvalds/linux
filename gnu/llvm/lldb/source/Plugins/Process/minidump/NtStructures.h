#ifndef LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_NTSTRUCTURES_H

#define LLDB_SOURCE_PLUGINS_PROCESS_MINIDUMP_NTSTRUCTURES_H

//===-- NtStructures.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_Plugins_Process_Minidump_NtStructures_h_
#define liblldb_Plugins_Process_Minidump_NtStructures_h_

#include "llvm/Support/Endian.h"

namespace lldb_private {

namespace minidump {

// This describes the layout of a TEB (Thread Environment Block) for a 64-bit
// process.  It's adapted from the 32-bit TEB in winternl.h.  Currently, we care
// only about the position of the tls_slots.
struct TEB64 {
  llvm::support::ulittle64_t reserved1[12];
  llvm::support::ulittle64_t process_environment_block;
  llvm::support::ulittle64_t reserved2[399];
  uint8_t reserved3[1952];
  llvm::support::ulittle64_t tls_slots[64];
  uint8_t reserved4[8];
  llvm::support::ulittle64_t reserved5[26];
  llvm::support::ulittle64_t reserved_for_ole; // Windows 2000 only
  llvm::support::ulittle64_t reserved6[4];
  llvm::support::ulittle64_t tls_expansion_slots;
};

#endif // liblldb_Plugins_Process_Minidump_NtStructures_h_
} // namespace minidump
} // namespace lldb_private

#endif
