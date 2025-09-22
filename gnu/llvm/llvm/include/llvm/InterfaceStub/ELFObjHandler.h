//===- ELFObjHandler.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
/// \file
/// This supports reading and writing of elf dynamic shared objects.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_INTERFACESTUB_ELFOBJHANDLER_H
#define LLVM_INTERFACESTUB_ELFOBJHANDLER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <memory>

namespace llvm {

namespace ifs {
struct IFSStub;

/// Attempt to read a binary ELF file from a MemoryBuffer.
Expected<std::unique_ptr<IFSStub>> readELFFile(MemoryBufferRef Buf);

/// Attempt to write a binary ELF stub.
/// This function determines appropriate ELFType using the passed ELFTarget and
/// then writes a binary ELF stub to a specified file path.
///
/// @param FilePath File path for writing the ELF binary.
/// @param Stub Source ELFStub to generate a binary ELF stub from.
/// @param WriteIfChanged Whether or not to preserve timestamp if
///        the output stays the same.
Error writeBinaryStub(StringRef FilePath, const IFSStub &Stub,
                      bool WriteIfChanged = false);

} // end namespace ifs
} // end namespace llvm

#endif // LLVM_INTERFACESTUB_ELFOBJHANDLER_H
