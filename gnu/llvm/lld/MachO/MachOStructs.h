//===- MachOStructs.h -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines structures used in the MachO object file format. Note that
// unlike llvm/BinaryFormat/MachO.h, the structs here are defined in terms of
// endian- and alignment-compatibility wrappers.
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_MACHO_STRUCTS_H
#define LLD_MACHO_MACHO_STRUCTS_H

#include "llvm/Support/Endian.h"

namespace lld::structs {

struct nlist_64 {
  llvm::support::ulittle32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  llvm::support::ulittle16_t n_desc;
  llvm::support::ulittle64_t n_value;
};

struct nlist {
  llvm::support::ulittle32_t n_strx;
  uint8_t n_type;
  uint8_t n_sect;
  llvm::support::ulittle16_t n_desc;
  llvm::support::ulittle32_t n_value;
};

struct entry_point_command {
  llvm::support::ulittle32_t cmd;
  llvm::support::ulittle32_t cmdsize;
  llvm::support::ulittle64_t entryoff;
  llvm::support::ulittle64_t stacksize;
};

} // namespace lld::structs

#endif
