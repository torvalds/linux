//===- TextAPI/DylibReader.h - TAPI MachO Dylib Reader ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Defines the MachO Dynamic Library Reader.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_DYLIBREADER_H
#define LLVM_TEXTAPI_DYLIBREADER_H

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/RecordsSlice.h"

namespace llvm::MachO::DylibReader {

struct ParseOption {
  /// Determines arch slice to parse.
  ArchitectureSet Archs = ArchitectureSet::All();
  /// Capture Mach-O header from binary, primarily load commands.
  bool MachOHeader = true;
  /// Capture defined symbols out of export trie and n-list.
  bool SymbolTable = true;
  /// Capture undefined symbols too.
  bool Undefineds = true;
};

/// Parse Mach-O dynamic libraries to extract TAPI attributes.
///
/// \param Buffer Data that points to dylib.
/// \param Options Determines which attributes to extract.
/// \return List of record slices.
Expected<Records> readFile(MemoryBufferRef Buffer, const ParseOption &Opt);

/// Get TAPI file representation of binary dylib.
///
/// \param Buffer Data that points to dylib.
Expected<std::unique_ptr<InterfaceFile>> get(MemoryBufferRef Buffer);

using SymbolToSourceLocMap = llvm::StringMap<RecordLoc>;
/// Get the source location for each symbol from dylib.
///
/// \param DSYM Path to DSYM file.
/// \param T Requested target slice for dylib.
SymbolToSourceLocMap accumulateSourceLocFromDSYM(const StringRef DSYM,
                                                 const Target &T);

} // namespace llvm::MachO::DylibReader

#endif // LLVM_TEXTAPI_DYLIBREADER_H
