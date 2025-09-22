//===-- OffloadDump.cpp - Offloading dumper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the offloading-specific dumper for llvm-objdump.
///
//===----------------------------------------------------------------------===//

#include "OffloadDump.h"
#include "llvm-objdump.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Alignment.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::objdump;

/// Get the printable name of the image kind.
static StringRef getImageName(const OffloadBinary &OB) {
  switch (OB.getImageKind()) {
  case IMG_Object:
    return "elf";
  case IMG_Bitcode:
    return "llvm ir";
  case IMG_Cubin:
    return "cubin";
  case IMG_Fatbinary:
    return "fatbinary";
  case IMG_PTX:
    return "ptx";
  default:
    return "<none>";
  }
}

static void printBinary(const OffloadBinary &OB, uint64_t Index) {
  outs() << "\nOFFLOADING IMAGE [" << Index << "]:\n";
  outs() << left_justify("kind", 16) << getImageName(OB) << "\n";
  outs() << left_justify("arch", 16) << OB.getArch() << "\n";
  outs() << left_justify("triple", 16) << OB.getTriple() << "\n";
  outs() << left_justify("producer", 16)
         << getOffloadKindName(OB.getOffloadKind()) << "\n";
}

/// Print the embedded offloading contents of an ObjectFile \p O.
void llvm::dumpOffloadBinary(const ObjectFile &O) {
  if (!O.isELF() && !O.isCOFF()) {
    reportWarning(
        "--offloading is currently only supported for COFF and ELF targets",
        O.getFileName());
    return;
  }

  SmallVector<OffloadFile> Binaries;
  if (Error Err = extractOffloadBinaries(O.getMemoryBufferRef(), Binaries))
    reportError(O.getFileName(), "while extracting offloading files: " +
                                     toString(std::move(Err)));

  // Print out all the binaries that are contained in this buffer.
  for (uint64_t I = 0, E = Binaries.size(); I != E; ++I)
    printBinary(*Binaries[I].getBinary(), I);
}

/// Print the contents of an offload binary file \p OB. This may contain
/// multiple binaries stored in the same buffer.
void llvm::dumpOffloadSections(const OffloadBinary &OB) {
  SmallVector<OffloadFile> Binaries;
  if (Error Err = extractOffloadBinaries(OB.getMemoryBufferRef(), Binaries))
    reportError(OB.getFileName(), "while extracting offloading files: " +
                                      toString(std::move(Err)));

  // Print out all the binaries that are contained in this buffer.
  for (uint64_t I = 0, E = Binaries.size(); I != E; ++I)
    printBinary(*Binaries[I].getBinary(), I);
}
