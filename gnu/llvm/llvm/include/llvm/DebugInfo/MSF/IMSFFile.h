//===- IMSFFile.h - Abstract base class for an MSF file ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_MSF_IMSFFILE_H
#define LLVM_DEBUGINFO_MSF_IMSFFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
namespace msf {

class IMSFFile {
public:
  virtual ~IMSFFile() = default;

  virtual uint32_t getBlockSize() const = 0;
  virtual uint32_t getBlockCount() const = 0;

  virtual uint32_t getNumStreams() const = 0;
  virtual uint32_t getStreamByteSize(uint32_t StreamIndex) const = 0;
  virtual ArrayRef<support::ulittle32_t>
  getStreamBlockList(uint32_t StreamIndex) const = 0;

  virtual Expected<ArrayRef<uint8_t>> getBlockData(uint32_t BlockIndex,
                                                   uint32_t NumBytes) const = 0;
  virtual Error setBlockData(uint32_t BlockIndex, uint32_t Offset,
                             ArrayRef<uint8_t> Data) const = 0;
};

} // end namespace msf
} // end namespace llvm

#endif // LLVM_DEBUGINFO_MSF_IMSFFILE_H
