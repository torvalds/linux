//===--- ARMWinEHPrinter.h - Windows on ARM Unwind Information Printer ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_READOBJ_ARMWINEHPRINTER_H
#define LLVM_TOOLS_LLVM_READOBJ_ARMWINEHPRINTER_H

#include "llvm/Object/COFF.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/ScopedPrinter.h"

namespace llvm {
namespace ARM {
namespace WinEH {
class RuntimeFunction;
class RuntimeFunctionARM64;

class Decoder {
  static const size_t PDataEntrySize;

  ScopedPrinter &SW;
  raw_ostream &OS;
  bool isAArch64;

  struct RingEntry {
    uint8_t Mask;
    uint8_t Value;
    uint8_t Length;
    bool (Decoder::*Routine)(const uint8_t *, unsigned &, unsigned, bool);
  };
  static const RingEntry Ring[];
  static const RingEntry Ring64[];

  bool opcode_0xxxxxxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_10Lxxxxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_1100xxxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11010Lxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11011Lxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11100xxx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_111010xx(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_1110110L(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11101110(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11101111(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11110101(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11110110(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11110111(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111000(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111001(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111010(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111011(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111100(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111101(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111110(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_11111111(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);

  // ARM64 unwind codes start here.
  bool opcode_alloc_s(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                      bool Prologue);
  bool opcode_save_r19r20_x(const uint8_t *Opcodes, unsigned &Offset,
                            unsigned Length, bool Prologue);
  bool opcode_save_fplr(const uint8_t *Opcodes, unsigned &Offset,
                        unsigned Length, bool Prologue);
  bool opcode_save_fplr_x(const uint8_t *Opcodes, unsigned &Offset,
                          unsigned Length, bool Prologue);
  bool opcode_alloc_m(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                      bool Prologue);
  bool opcode_save_regp(const uint8_t *Opcodes, unsigned &Offset,
                        unsigned Length, bool Prologue);
  bool opcode_save_regp_x(const uint8_t *Opcodes, unsigned &Offset,
                          unsigned Length, bool Prologue);
  bool opcode_save_reg(const uint8_t *Opcodes, unsigned &Offset,
                       unsigned Length, bool Prologue);
  bool opcode_save_reg_x(const uint8_t *Opcodes, unsigned &Offset,
                         unsigned Length, bool Prologue);
  bool opcode_save_lrpair(const uint8_t *Opcodes, unsigned &Offset,
                          unsigned Length, bool Prologue);
  bool opcode_save_fregp(const uint8_t *Opcodes, unsigned &Offset,
                         unsigned Length, bool Prologue);
  bool opcode_save_fregp_x(const uint8_t *Opcodes, unsigned &Offset,
                           unsigned Length, bool Prologue);
  bool opcode_save_freg(const uint8_t *Opcodes, unsigned &Offset,
                        unsigned Length, bool Prologue);
  bool opcode_save_freg_x(const uint8_t *Opcodes, unsigned &Offset,
                          unsigned Length, bool Prologue);
  bool opcode_alloc_l(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                      bool Prologue);
  bool opcode_setfp(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                    bool Prologue);
  bool opcode_addfp(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                    bool Prologue);
  bool opcode_nop(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                  bool Prologue);
  bool opcode_end(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                  bool Prologue);
  bool opcode_end_c(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                    bool Prologue);
  bool opcode_save_next(const uint8_t *Opcodes, unsigned &Offset,
                        unsigned Length, bool Prologue);
  bool opcode_save_any_reg(const uint8_t *Opcodes, unsigned &Offset,
                           unsigned Length, bool Prologue);
  bool opcode_trap_frame(const uint8_t *Opcodes, unsigned &Offset,
                         unsigned Length, bool Prologue);
  bool opcode_machine_frame(const uint8_t *Opcodes, unsigned &Offset,
                            unsigned Length, bool Prologue);
  bool opcode_context(const uint8_t *Opcodes, unsigned &Offset, unsigned Length,
                      bool Prologue);
  bool opcode_ec_context(const uint8_t *Opcodes, unsigned &Offset,
                         unsigned Length, bool Prologue);
  bool opcode_clear_unwound_to_call(const uint8_t *Opcodes, unsigned &Offset,
                                    unsigned Length, bool Prologue);
  bool opcode_pac_sign_lr(const uint8_t *Opcodes, unsigned &Offset,
                          unsigned Length, bool Prologue);

  void decodeOpcodes(ArrayRef<uint8_t> Opcodes, unsigned Offset,
                     bool Prologue);

  void printGPRMask(uint16_t Mask);
  void printVFPMask(uint32_t Mask);

  ErrorOr<object::SectionRef>
  getSectionContaining(const object::COFFObjectFile &COFF, uint64_t Address);

  ErrorOr<object::SymbolRef>
  getSymbol(const object::COFFObjectFile &COFF, uint64_t Address,
            bool FunctionOnly = false);

  ErrorOr<object::SymbolRef>
  getRelocatedSymbol(const object::COFFObjectFile &COFF,
                     const object::SectionRef &Section, uint64_t Offset);

  ErrorOr<object::SymbolRef>
  getSymbolForLocation(const object::COFFObjectFile &COFF,
                       const object::SectionRef &Section,
                       uint64_t OffsetInSection, uint64_t ImmediateOffset,
                       uint64_t &SymbolAddress, uint64_t &SymbolOffset,
                       bool FunctionOnly = false);

  object::SymbolRef getPreferredSymbol(const object::COFFObjectFile &COFF,
                                       object::SymbolRef Sym,
                                       uint64_t &SymbolOffset);

  bool dumpXDataRecord(const object::COFFObjectFile &COFF,
                       const object::SectionRef &Section,
                       uint64_t FunctionAddress, uint64_t VA);
  bool dumpUnpackedEntry(const object::COFFObjectFile &COFF,
                         const object::SectionRef Section, uint64_t Offset,
                         unsigned Index, const RuntimeFunction &Entry);
  bool dumpPackedEntry(const object::COFFObjectFile &COFF,
                       const object::SectionRef Section, uint64_t Offset,
                       unsigned Index, const RuntimeFunction &Entry);
  bool dumpPackedARM64Entry(const object::COFFObjectFile &COFF,
                            const object::SectionRef Section, uint64_t Offset,
                            unsigned Index, const RuntimeFunctionARM64 &Entry);
  bool dumpProcedureDataEntry(const object::COFFObjectFile &COFF,
                              const object::SectionRef Section, unsigned Entry,
                              ArrayRef<uint8_t> Contents);
  void dumpProcedureData(const object::COFFObjectFile &COFF,
                         const object::SectionRef Section);

public:
  Decoder(ScopedPrinter &SW, bool isAArch64) : SW(SW),
                                               OS(SW.getOStream()),
                                               isAArch64(isAArch64) {}
  Error dumpProcedureData(const object::COFFObjectFile &COFF);
};
}
}
}

#endif
