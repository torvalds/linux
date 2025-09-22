//===-- LZMA.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_HOST_LZMA_H
#define LLDB_HOST_LZMA_H

#include "llvm/ADT/ArrayRef.h"

namespace llvm {
class Error;
} // End of namespace llvm

namespace lldb_private {

namespace lzma {
	
bool isAvailable();

llvm::Expected<uint64_t>
getUncompressedSize(llvm::ArrayRef<uint8_t> InputBuffer);

llvm::Error uncompress(llvm::ArrayRef<uint8_t> InputBuffer,
                       llvm::SmallVectorImpl<uint8_t> &Uncompressed);

} // End of namespace lzma

} // End of namespace lldb_private

#endif // LLDB_HOST_LZMA_H
