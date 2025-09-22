//===- llvm/MC/MCDisassembler.h - Disassembler interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCDISASSEMBLER_MCDISASSEMBLER_H
#define LLVM_MC_MCDISASSEMBLER_MCDISASSEMBLER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCDisassembler/MCSymbolizer.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace llvm {

struct XCOFFSymbolInfoTy {
  std::optional<XCOFF::StorageMappingClass> StorageMappingClass;
  std::optional<uint32_t> Index;
  bool IsLabel = false;
  bool operator<(const XCOFFSymbolInfoTy &SymInfo) const;
};

struct SymbolInfoTy {
  uint64_t Addr;
  StringRef Name;
  // XCOFF uses XCOFFSymInfo. Other targets use Type.
  XCOFFSymbolInfoTy XCOFFSymInfo;
  uint8_t Type;
  // Used by ELF to describe a mapping symbol that is usually not displayed.
  bool IsMappingSymbol;

private:
  bool IsXCOFF;
  bool HasType;

public:
  SymbolInfoTy(std::optional<XCOFF::StorageMappingClass> Smc, uint64_t Addr,
               StringRef Name, std::optional<uint32_t> Idx, bool Label)
      : Addr(Addr), Name(Name), XCOFFSymInfo{Smc, Idx, Label}, Type(0),
        IsMappingSymbol(false), IsXCOFF(true), HasType(false) {}
  SymbolInfoTy(uint64_t Addr, StringRef Name, uint8_t Type,
               bool IsMappingSymbol = false, bool IsXCOFF = false)
      : Addr(Addr), Name(Name), Type(Type), IsMappingSymbol(IsMappingSymbol),
        IsXCOFF(IsXCOFF), HasType(true) {}
  bool isXCOFF() const { return IsXCOFF; }

private:
  friend bool operator<(const SymbolInfoTy &P1, const SymbolInfoTy &P2) {
    assert((P1.IsXCOFF == P2.IsXCOFF && P1.HasType == P2.HasType) &&
           "The value of IsXCOFF and HasType in P1 and P2 should be the same "
           "respectively.");

    if (P1.IsXCOFF && P1.HasType)
      return std::tie(P1.Addr, P1.Type, P1.Name) <
             std::tie(P2.Addr, P2.Type, P2.Name);

    if (P1.IsXCOFF)
      return std::tie(P1.Addr, P1.XCOFFSymInfo, P1.Name) <
             std::tie(P2.Addr, P2.XCOFFSymInfo, P2.Name);

    // With the same address, place mapping symbols first.
    bool MS1 = !P1.IsMappingSymbol, MS2 = !P2.IsMappingSymbol;
    return std::tie(P1.Addr, MS1, P1.Name, P1.Type) <
           std::tie(P2.Addr, MS2, P2.Name, P2.Type);
  }
};

using SectionSymbolsTy = std::vector<SymbolInfoTy>;

template <typename T> class ArrayRef;
class MCContext;
class MCInst;
class MCSubtargetInfo;
class raw_ostream;

/// Superclass for all disassemblers. Consumes a memory region and provides an
/// array of assembly instructions.
class MCDisassembler {
public:
  /// Ternary decode status. Most backends will just use Fail and
  /// Success, however some have a concept of an instruction with
  /// understandable semantics but which is architecturally
  /// incorrect. An example of this is ARM UNPREDICTABLE instructions
  /// which are disassemblable but cause undefined behaviour.
  ///
  /// Because it makes sense to disassemble these instructions, there
  /// is a "soft fail" failure mode that indicates the MCInst& is
  /// valid but architecturally incorrect.
  ///
  /// The enum numbers are deliberately chosen such that reduction
  /// from Success->SoftFail ->Fail can be done with a simple
  /// bitwise-AND:
  ///
  ///   LEFT & TOP =  | Success       Unpredictable   Fail
  ///   --------------+-----------------------------------
  ///   Success       | Success       Unpredictable   Fail
  ///   Unpredictable | Unpredictable Unpredictable   Fail
  ///   Fail          | Fail          Fail            Fail
  ///
  /// An easy way of encoding this is as 0b11, 0b01, 0b00 for
  /// Success, SoftFail, Fail respectively.
  enum DecodeStatus {
    Fail = 0,
    SoftFail = 1,
    Success = 3
  };

  MCDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
    : Ctx(Ctx), STI(STI) {}

  virtual ~MCDisassembler();

  /// Returns the disassembly of a single instruction.
  ///
  /// \param Instr    - An MCInst to populate with the contents of the
  ///                   instruction.
  /// \param Size     - A value to populate with the size of the instruction, or
  ///                   the number of bytes consumed while attempting to decode
  ///                   an invalid instruction.
  /// \param Address  - The address, in the memory space of region, of the first
  ///                   byte of the instruction.
  /// \param Bytes    - A reference to the actual bytes of the instruction.
  /// \param CStream  - The stream to print comments and annotations on.
  /// \return         - MCDisassembler::Success if the instruction is valid,
  ///                   MCDisassembler::SoftFail if the instruction was
  ///                                            disassemblable but invalid,
  ///                   MCDisassembler::Fail if the instruction was invalid.
  virtual DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                                      ArrayRef<uint8_t> Bytes, uint64_t Address,
                                      raw_ostream &CStream) const = 0;

  /// Used to perform separate target specific disassembly for a particular
  /// symbol. May parse any prelude that precedes instructions after the
  /// start of a symbol, or the entire symbol.
  /// This is used for example by WebAssembly to decode preludes.
  ///
  /// Base implementation returns false. So all targets by default decline to
  /// treat symbols separately.
  ///
  /// \param Symbol   - The symbol.
  /// \param Size     - The number of bytes consumed.
  /// \param Address  - The address, in the memory space of region, of the first
  ///                   byte of the symbol.
  /// \param Bytes    - A reference to the actual bytes at the symbol location.
  /// \return         - True if this symbol triggered some target specific
  ///                   disassembly for this symbol. Size must be set with the
  ///                   number of bytes consumed.
  ///                 - Error if this symbol triggered some target specific
  ///                   disassembly for this symbol, but an error was found with
  ///                   it. Size must be set with the number of bytes consumed.
  ///                 - False if the target doesn't want to handle the symbol
  ///                   separately. The value of Size is ignored in this case,
  ///                   and Err must not be set.
  virtual Expected<bool> onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                                       ArrayRef<uint8_t> Bytes,
                                       uint64_t Address) const;
  // TODO:
  // Implement similar hooks that can be used at other points during
  // disassembly. Something along the following lines:
  // - onBeforeInstructionDecode()
  // - onAfterInstructionDecode()
  // - onSymbolEnd()
  // It should help move much of the target specific code from llvm-objdump to
  // respective target disassemblers.

  /// Suggest a distance to skip in a buffer of data to find the next
  /// place to look for the start of an instruction. For example, if
  /// all instructions have a fixed alignment, this might advance to
  /// the next multiple of that alignment.
  ///
  /// If not overridden, the default is 1.
  ///
  /// \param Address  - The address, in the memory space of region, of the
  ///                   starting point (typically the first byte of something
  ///                   that did not decode as a valid instruction at all).
  /// \param Bytes    - A reference to the actual bytes at Address. May be
  ///                   needed in order to determine the width of an
  ///                   unrecognized instruction (e.g. in Thumb this is a simple
  ///                   consistent criterion that doesn't require knowing the
  ///                   specific instruction). The caller can pass as much data
  ///                   as they have available, and the function is required to
  ///                   make a reasonable default choice if not enough data is
  ///                   available to make a better one.
  /// \return         - A number of bytes to skip. Must always be greater than
  ///                   zero. May be greater than the size of Bytes.
  virtual uint64_t suggestBytesToSkip(ArrayRef<uint8_t> Bytes,
                                      uint64_t Address) const;

private:
  MCContext &Ctx;

protected:
  // Subtarget information, for instruction decoding predicates if required.
  const MCSubtargetInfo &STI;
  std::unique_ptr<MCSymbolizer> Symbolizer;

public:
  // Helpers around MCSymbolizer
  bool tryAddingSymbolicOperand(MCInst &Inst, int64_t Value, uint64_t Address,
                                bool IsBranch, uint64_t Offset, uint64_t OpSize,
                                uint64_t InstSize) const;

  void tryAddingPcLoadReferenceComment(int64_t Value, uint64_t Address) const;

  /// Set \p Symzer as the current symbolizer.
  /// This takes ownership of \p Symzer, and deletes the previously set one.
  void setSymbolizer(std::unique_ptr<MCSymbolizer> Symzer);

  MCContext& getContext() const { return Ctx; }

  const MCSubtargetInfo& getSubtargetInfo() const { return STI; }

  /// ELF-specific, set the ABI version from the object header.
  virtual void setABIVersion(unsigned Version) {}

  // Marked mutable because we cache it inside the disassembler, rather than
  // having to pass it around as an argument through all the autogenerated code.
  mutable raw_ostream *CommentStream = nullptr;
};

} // end namespace llvm

#endif // LLVM_MC_MCDISASSEMBLER_MCDISASSEMBLER_H
