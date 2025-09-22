//===- DWARFLinker.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DWARFLINKER_PARALLEL_DWARFLINKER_H
#define LLVM_DWARFLINKER_PARALLEL_DWARFLINKER_H

#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/DWARFLinker/DWARFFile.h"
#include "llvm/DWARFLinker/DWARFLinkerBase.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/TargetParser/Triple.h"

/// ------------------------------------------------------------------
/// The core of the Dwarf linking logic.
///
/// The generation of the dwarf information from the object files will be
/// driven by the selection of 'root DIEs', which are DIEs that
/// describe variables or functions that resolves to the corresponding
/// code section(and thus have entries in the Addresses map). All the debug
/// information that will be generated(the DIEs, but also the line
/// tables, ranges, ...) is derived from that set of root DIEs.
///
/// The root DIEs are identified because they contain relocations that
/// points to code section(the low_pc for a function, the location for
/// a variable). These relocations are gathered as a very first step
/// when we start processing a object file by AddressesMap.
///
/// The overall linking process looks like this:
///
/// parrallel_for_each(ObjectFile) {
///   for_each (Compile Unit) {
///     1. Load Clang modules.
///   }
///
///   parrallel_for_each(Compile Unit) {
///     1. Load input DWARF for Compile Unit.
///     2. Report warnings for Clang modules.
///     3. Analyze live DIEs and type names(if ODR deduplication is requested).
///     4. Clone DIEs(Generate output DIEs and resulting DWARF tables).
///        The result is in an OutDebugInfoBytes, which is an ELF file
///        containing DWARF tables corresponding to the current compile unit.
///     5. Cleanup Input and Output DIEs.
///   }
///
///   Deallocate loaded Object file.
/// }
///
/// if (ODR deduplication is requested)
///   Generate an artificial compilation unit ("Type Table": used to partially
///   generate DIEs at the clone stage).
///
/// for_each (ObjectFile) {
///   for_each (Compile Unit) {
///     1. Set offsets to Compile Units DWARF tables.
///     2. Sort offsets/attributes/patches to have a predictable result.
///     3. Patch size/offsets fields.
///     4. Generate index tables.
///     5. Move DWARF tables of compile units into the resulting file.
///   }
/// }
///
/// Every compile unit is processed separately, visited only once
/// (except case inter-CU references exist), and used data is freed
/// after the compile unit is processed. The resulting file is glued together
/// from the generated debug tables which correspond to separate compile units.
///
/// Handling inter-CU references: inter-CU references are hard to process
/// using only one pass. f.e. if CU1 references CU100 and CU100 references
/// CU1, we could not finish handling of CU1 until we finished CU100.
/// Thus we either need to load all CUs into the memory, either load CUs several
/// times. This implementation loads inter-connected CU into memory at the first
/// pass and processes them at the second pass.
///
/// ODR deduplication: Artificial compilation unit will be constructed to keep
/// type dies. All types are moved into that compilation unit. Type's references
/// are patched so that they point to the corresponding types from artificial
/// compilation unit. All partial type definitions would be merged into single
/// type definition.
///

namespace llvm {
namespace dwarf_linker {
namespace parallel {

/// This structure keeps data of the concrete section.
struct SectionDescriptorBase {
  SectionDescriptorBase(DebugSectionKind SectionKind, dwarf::FormParams Format,
                        llvm::endianness Endianess)
      : SectionKind(SectionKind), Format(Format), Endianess(Endianess) {}
  virtual ~SectionDescriptorBase() = default;
  /// Returns section content.
  virtual StringRef getContents() = 0;
  /// Returns section kind.
  DebugSectionKind getKind() { return SectionKind; }
  /// Returns section name.
  const StringLiteral &getName() const { return getSectionName(SectionKind); }
  /// Returns endianess used by section.
  llvm::endianness getEndianess() const { return Endianess; }
  /// Returns FormParams used by section.
  dwarf::FormParams getFormParams() const { return Format; }

protected:
  /// The section kind.
  DebugSectionKind SectionKind = DebugSectionKind::NumberOfEnumEntries;
  /// Output format.
  dwarf::FormParams Format = {4, 4, dwarf::DWARF32};
  llvm::endianness Endianess = llvm::endianness::little;
};

using SectionHandlerTy =
    std::function<void(std::shared_ptr<SectionDescriptorBase> Section)>;

class DWARFLinker : public DWARFLinkerBase {
public:
  virtual ~DWARFLinker() = default;

  /// Creates dwarf linker instance.
  static std::unique_ptr<DWARFLinker>
  createLinker(MessageHandlerTy ErrorHandler, MessageHandlerTy WarningHandler);

  /// Set output DWARF handler. Result of linking DWARF is set of sections
  /// containing final debug info. DWARFLinkerBase::link() pass generated
  /// sections using specified \p SectionHandler.
  virtual void setOutputDWARFHandler(const Triple &TargetTriple,
                                     SectionHandlerTy SectionHandler) = 0;
};

} // end of namespace parallel
} // end of namespace dwarf_linker
} // end of namespace llvm

#endif // LLVM_DWARFLINKER_PARALLEL_DWARFLINKER_H
