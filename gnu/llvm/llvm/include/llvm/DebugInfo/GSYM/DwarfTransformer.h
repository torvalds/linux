//===- DwarfTransformer.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H
#define LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/GSYM/ExtractRanges.h"
#include "llvm/Support/Error.h"

namespace llvm {

class raw_ostream;

namespace gsym {

struct CUInfo;
struct FunctionInfo;
class GsymCreator;
class OutputAggregator;

/// A class that transforms the DWARF in a DWARFContext into GSYM information
/// by populating the GsymCreator object that it is constructed with. This
/// class supports converting all DW_TAG_subprogram DIEs into
/// gsym::FunctionInfo objects that includes line table information and inline
/// function information. Creating a separate class to transform this data
/// allows this class to be unit tested.
class DwarfTransformer {
public:

  /// Create a DWARF transformer.
  ///
  /// \param D The DWARF to use when converting to GSYM.
  ///
  /// \param G The GSYM creator to populate with the function information
  /// from the debug info.
  DwarfTransformer(DWARFContext &D, GsymCreator &G) : DICtx(D), Gsym(G) {}

  /// Extract the DWARF from the supplied object file and convert it into the
  /// Gsym format in the GsymCreator object that is passed in. Returns an
  /// error if something fatal is encountered.
  ///
  /// \param NumThreads The number of threads that the conversion process can
  ///                   use.
  ///
  /// \param OS The stream to log warnings and non fatal issues to. If NULL
  ///           then don't log.
  ///
  /// \returns An error indicating any fatal issues that happen when parsing
  /// the DWARF, or Error::success() if all goes well.
  llvm::Error convert(uint32_t NumThreads, OutputAggregator &OS);

  llvm::Error verify(StringRef GsymPath, OutputAggregator &OS);

private:

  /// Parse the DWARF in the object file and convert it into the GsymCreator.
  Error parse();

  /// Handle any DIE (debug info entry) from the DWARF.
  ///
  /// This function will find all DW_TAG_subprogram DIEs that convert them into
  /// GSYM FuntionInfo objects and add them to the GsymCreator supplied during
  /// construction. The DIE and all its children will be recursively parsed
  /// with calls to this function.
  ///
  /// \param Strm The thread specific log stream for any non fatal errors and
  /// warnings. Once a thread has finished parsing an entire compile unit, all
  /// information in this temporary stream will be forwarded to the member
  /// variable log. This keeps logging thread safe. If the value is NULL, then
  /// don't log.
  ///
  /// \param CUI The compile unit specific information that contains the DWARF
  /// line table, cached file list, and other compile unit specific
  /// information.
  ///
  /// \param Die The DWARF debug info entry to parse.
  void handleDie(OutputAggregator &Strm, CUInfo &CUI, DWARFDie Die);

  DWARFContext &DICtx;
  GsymCreator &Gsym;

  friend class DwarfTransformerTest;
};

} // namespace gsym
} // namespace llvm

#endif // LLVM_DEBUGINFO_GSYM_DWARFTRANSFORMER_H
