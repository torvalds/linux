//===-- X86MachObjectWriter.cpp - X86 Mach-O Writer -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86FixupKinds.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"

using namespace llvm;

namespace {
class X86MachObjectWriter : public MCMachObjectTargetWriter {
  bool recordScatteredRelocation(MachObjectWriter *Writer,
                                 const MCAssembler &Asm,
                                 const MCFragment *Fragment,
                                 const MCFixup &Fixup,
                                 MCValue Target,
                                 unsigned Log2Size,
                                 uint64_t &FixedValue);
  void recordTLVPRelocation(MachObjectWriter *Writer,
                            const MCAssembler &Asm,
                            const MCFragment *Fragment,
                            const MCFixup &Fixup,
                            MCValue Target,
                            uint64_t &FixedValue);

  void RecordX86Relocation(MachObjectWriter *Writer,
                              const MCAssembler &Asm,
                              const MCFragment *Fragment,
                              const MCFixup &Fixup,
                              MCValue Target,
                              uint64_t &FixedValue);
  void RecordX86_64Relocation(MachObjectWriter *Writer, MCAssembler &Asm,
                              const MCFragment *Fragment, const MCFixup &Fixup,
                              MCValue Target, uint64_t &FixedValue);

public:
  X86MachObjectWriter(bool Is64Bit, uint32_t CPUType, uint32_t CPUSubtype)
      : MCMachObjectTargetWriter(Is64Bit, CPUType, CPUSubtype) {}

  void recordRelocation(MachObjectWriter *Writer, MCAssembler &Asm,
                        const MCFragment *Fragment, const MCFixup &Fixup,
                        MCValue Target, uint64_t &FixedValue) override {
    if (Writer->is64Bit())
      RecordX86_64Relocation(Writer, Asm, Fragment, Fixup, Target, FixedValue);
    else
      RecordX86Relocation(Writer, Asm, Fragment, Fixup, Target, FixedValue);
  }
};
} // namespace

static bool isFixupKindRIPRel(unsigned Kind) {
  return Kind == X86::reloc_riprel_4byte ||
         Kind == X86::reloc_riprel_4byte_movq_load ||
         Kind == X86::reloc_riprel_4byte_relax ||
         Kind == X86::reloc_riprel_4byte_relax_rex;
}

static unsigned getFixupKindLog2Size(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_PCRel_1:
  case FK_Data_1: return 0;
  case FK_PCRel_2:
  case FK_Data_2: return 1;
  case FK_PCRel_4:
    // FIXME: Remove these!!!
  case X86::reloc_riprel_4byte:
  case X86::reloc_riprel_4byte_relax:
  case X86::reloc_riprel_4byte_relax_rex:
  case X86::reloc_riprel_4byte_movq_load:
  case X86::reloc_signed_4byte:
  case X86::reloc_signed_4byte_relax:
  case X86::reloc_branch_4byte_pcrel:
  case FK_Data_4: return 2;
  case FK_Data_8: return 3;
  }
}

void X86MachObjectWriter::RecordX86_64Relocation(
    MachObjectWriter *Writer, MCAssembler &Asm, const MCFragment *Fragment,
    const MCFixup &Fixup, MCValue Target, uint64_t &FixedValue) {
  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());
  unsigned IsRIPRel = isFixupKindRIPRel(Fixup.getKind());
  unsigned Log2Size = getFixupKindLog2Size(Fixup.getKind());

  // See <reloc.h>.
  uint32_t FixupOffset = Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();
  uint32_t FixupAddress =
      Writer->getFragmentAddress(Asm, Fragment) + Fixup.getOffset();
  int64_t Value = 0;
  unsigned Index = 0;
  unsigned IsExtern = 0;
  unsigned Type = 0;
  const MCSymbol *RelSymbol = nullptr;

  Value = Target.getConstant();

  if (IsPCRel) {
    // Compensate for the relocation offset, Darwin x86_64 relocations only have
    // the addend and appear to have attempted to define it to be the actual
    // expression addend without the PCrel bias. However, instructions with data
    // following the relocation are not accommodated for (see comment below
    // regarding SIGNED{1,2,4}), so it isn't exactly that either.
    Value += 1LL << Log2Size;
  }

  if (Target.isAbsolute()) { // constant
    // SymbolNum of 0 indicates the absolute section.
    Type = MachO::X86_64_RELOC_UNSIGNED;

    // FIXME: I believe this is broken, I don't think the linker can understand
    // it. I think it would require a local relocation, but I'm not sure if that
    // would work either. The official way to get an absolute PCrel relocation
    // is to use an absolute symbol (which we don't support yet).
    if (IsPCRel) {
      IsExtern = 1;
      Type = MachO::X86_64_RELOC_BRANCH;
    }
  } else if (Target.getSymB()) { // A - B + constant
    const MCSymbol *A = &Target.getSymA()->getSymbol();
    if (A->isTemporary())
      A = &Writer->findAliasedSymbol(*A);
    const MCSymbol *A_Base = Writer->getAtom(*A);

    const MCSymbol *B = &Target.getSymB()->getSymbol();
    if (B->isTemporary())
      B = &Writer->findAliasedSymbol(*B);
    const MCSymbol *B_Base = Writer->getAtom(*B);

    // Neither symbol can be modified.
    if (Target.getSymA()->getKind() != MCSymbolRefExpr::VK_None) {
      Asm.getContext().reportError(Fixup.getLoc(),
                                   "unsupported relocation of modified symbol");
      return;
    }

    // We don't support PCrel relocations of differences. Darwin 'as' doesn't
    // implement most of these correctly.
    if (IsPCRel) {
      Asm.getContext().reportError(
          Fixup.getLoc(), "unsupported pc-relative relocation of difference");
      return;
    }

    // The support for the situation where one or both of the symbols would
    // require a local relocation is handled just like if the symbols were
    // external.  This is certainly used in the case of debug sections where the
    // section has only temporary symbols and thus the symbols don't have base
    // symbols.  This is encoded using the section ordinal and non-extern
    // relocation entries.

    // Darwin 'as' doesn't emit correct relocations for this (it ends up with a
    // single SIGNED relocation); reject it for now.  Except the case where both
    // symbols don't have a base, equal but both NULL.
    if (A_Base == B_Base && A_Base) {
      Asm.getContext().reportError(
          Fixup.getLoc(), "unsupported relocation with identical base");
      return;
    }

    // A subtraction expression where either symbol is undefined is a
    // non-relocatable expression.
    if (A->isUndefined() || B->isUndefined()) {
      StringRef Name = A->isUndefined() ? A->getName() : B->getName();
      Asm.getContext().reportError(Fixup.getLoc(),
        "unsupported relocation with subtraction expression, symbol '" +
        Name + "' can not be undefined in a subtraction expression");
      return;
    }

    Value += Writer->getSymbolAddress(*A, Asm) -
             (!A_Base ? 0 : Writer->getSymbolAddress(*A_Base, Asm));
    Value -= Writer->getSymbolAddress(*B, Asm) -
             (!B_Base ? 0 : Writer->getSymbolAddress(*B_Base, Asm));

    if (!A_Base)
      Index = A->getFragment()->getParent()->getOrdinal() + 1;
    Type = MachO::X86_64_RELOC_UNSIGNED;

    MachO::any_relocation_info MRE;
    MRE.r_word0 = FixupOffset;
    MRE.r_word1 =
        (Index << 0) | (IsPCRel << 24) | (Log2Size << 25) | (Type << 28);
    Writer->addRelocation(A_Base, Fragment->getParent(), MRE);

    if (B_Base)
      RelSymbol = B_Base;
    else
      Index = B->getFragment()->getParent()->getOrdinal() + 1;
    Type = MachO::X86_64_RELOC_SUBTRACTOR;
  } else {
    const MCSymbol *Symbol = &Target.getSymA()->getSymbol();
    if (Symbol->isTemporary() && Value) {
      const MCSection &Sec = Symbol->getSection();
      if (!MCAsmInfoDarwin::isSectionAtomizableBySymbols(Sec))
        Symbol->setUsedInReloc();
    }
    RelSymbol = Writer->getAtom(*Symbol);

    // Relocations inside debug sections always use local relocations when
    // possible. This seems to be done because the debugger doesn't fully
    // understand x86_64 relocation entries, and expects to find values that
    // have already been fixed up.
    if (Symbol->isInSection()) {
      const MCSectionMachO &Section =
          static_cast<const MCSectionMachO &>(*Fragment->getParent());
      if (Section.hasAttribute(MachO::S_ATTR_DEBUG))
        RelSymbol = nullptr;
    }

    // x86_64 almost always uses external relocations, except when there is no
    // symbol to use as a base address (a local symbol with no preceding
    // non-local symbol).
    if (RelSymbol) {
      // Add the local offset, if needed.
      if (RelSymbol != Symbol)
        Value += Asm.getSymbolOffset(*Symbol) - Asm.getSymbolOffset(*RelSymbol);
    } else if (Symbol->isInSection() && !Symbol->isVariable()) {
      // The index is the section ordinal (1-based).
      Index = Symbol->getFragment()->getParent()->getOrdinal() + 1;
      Value += Writer->getSymbolAddress(*Symbol, Asm);

      if (IsPCRel)
        Value -= FixupAddress + (1 << Log2Size);
    } else if (Symbol->isVariable()) {
      const MCExpr *Value = Symbol->getVariableValue();
      int64_t Res;
      bool isAbs =
          Value->evaluateAsAbsolute(Res, Asm, Writer->getSectionAddressMap());
      if (isAbs) {
        FixedValue = Res;
        return;
      } else {
        Asm.getContext().reportError(Fixup.getLoc(),
                                     "unsupported relocation of variable '" +
                                         Symbol->getName() + "'");
        return;
      }
    } else {
      Asm.getContext().reportError(
          Fixup.getLoc(), "unsupported relocation of undefined symbol '" +
                              Symbol->getName() + "'");
      return;
    }

    MCSymbolRefExpr::VariantKind Modifier = Target.getSymA()->getKind();
    if (IsPCRel) {
      if (IsRIPRel) {
        if (Modifier == MCSymbolRefExpr::VK_GOTPCREL) {
          // x86_64 distinguishes movq foo@GOTPCREL so that the linker can
          // rewrite the movq to an leaq at link time if the symbol ends up in
          // the same linkage unit.
          if (Fixup.getTargetKind() == X86::reloc_riprel_4byte_movq_load)
            Type = MachO::X86_64_RELOC_GOT_LOAD;
          else
            Type = MachO::X86_64_RELOC_GOT;
        }  else if (Modifier == MCSymbolRefExpr::VK_TLVP) {
          Type = MachO::X86_64_RELOC_TLV;
        }  else if (Modifier != MCSymbolRefExpr::VK_None) {
          Asm.getContext().reportError(
              Fixup.getLoc(), "unsupported symbol modifier in relocation");
          return;
        } else {
          Type = MachO::X86_64_RELOC_SIGNED;

          // The Darwin x86_64 relocation format has a problem where it cannot
          // encode an address (L<foo> + <constant>) which is outside the atom
          // containing L<foo>. Generally, this shouldn't occur but it does
          // happen when we have a RIPrel instruction with data following the
          // relocation entry (e.g., movb $012, L0(%rip)). Even with the PCrel
          // adjustment Darwin x86_64 uses, the offset is still negative and the
          // linker has no way to recognize this.
          //
          // To work around this, Darwin uses several special relocation types
          // to indicate the offsets. However, the specification or
          // implementation of these seems to also be incomplete; they should
          // adjust the addend as well based on the actual encoded instruction
          // (the additional bias), but instead appear to just look at the final
          // offset.
          switch (-(Target.getConstant() + (1LL << Log2Size))) {
          case 1: Type = MachO::X86_64_RELOC_SIGNED_1; break;
          case 2: Type = MachO::X86_64_RELOC_SIGNED_2; break;
          case 4: Type = MachO::X86_64_RELOC_SIGNED_4; break;
          }
        }
      } else {
        if (Modifier != MCSymbolRefExpr::VK_None) {
          Asm.getContext().reportError(
              Fixup.getLoc(),
              "unsupported symbol modifier in branch relocation");
          return;
        }

        Type = MachO::X86_64_RELOC_BRANCH;
      }
    } else {
      if (Modifier == MCSymbolRefExpr::VK_GOT) {
        Type = MachO::X86_64_RELOC_GOT;
      } else if (Modifier == MCSymbolRefExpr::VK_GOTPCREL) {
        // GOTPCREL is allowed as a modifier on non-PCrel instructions, in which
        // case all we do is set the PCrel bit in the relocation entry; this is
        // used with exception handling, for example. The source is required to
        // include any necessary offset directly.
        Type = MachO::X86_64_RELOC_GOT;
        IsPCRel = 1;
      } else if (Modifier == MCSymbolRefExpr::VK_TLVP) {
        Asm.getContext().reportError(
            Fixup.getLoc(), "TLVP symbol modifier should have been rip-rel");
        return;
      } else if (Modifier != MCSymbolRefExpr::VK_None) {
        Asm.getContext().reportError(
            Fixup.getLoc(), "unsupported symbol modifier in relocation");
        return;
      } else {
        Type = MachO::X86_64_RELOC_UNSIGNED;
        if (Fixup.getTargetKind() == X86::reloc_signed_4byte) {
          Asm.getContext().reportError(
              Fixup.getLoc(),
              "32-bit absolute addressing is not supported in 64-bit mode");
          return;
        }
      }
    }
  }

  // x86_64 always writes custom values into the fixups.
  FixedValue = Value;

  // struct relocation_info (8 bytes)
  MachO::any_relocation_info MRE;
  MRE.r_word0 = FixupOffset;
  MRE.r_word1 = (Index << 0) | (IsPCRel << 24) | (Log2Size << 25) |
                (IsExtern << 27) | (Type << 28);
  Writer->addRelocation(RelSymbol, Fragment->getParent(), MRE);
}

bool X86MachObjectWriter::recordScatteredRelocation(MachObjectWriter *Writer,
                                                    const MCAssembler &Asm,
                                                    const MCFragment *Fragment,
                                                    const MCFixup &Fixup,
                                                    MCValue Target,
                                                    unsigned Log2Size,
                                                    uint64_t &FixedValue) {
  uint64_t OriginalFixedValue = FixedValue;
  uint32_t FixupOffset = Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();
  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());
  unsigned Type = MachO::GENERIC_RELOC_VANILLA;

  // See <reloc.h>.
  const MCSymbol *A = &Target.getSymA()->getSymbol();

  if (!A->getFragment()) {
    Asm.getContext().reportError(
        Fixup.getLoc(),
        "symbol '" + A->getName() +
            "' can not be undefined in a subtraction expression");
    return false;
  }

  uint32_t Value = Writer->getSymbolAddress(*A, Asm);
  uint64_t SecAddr = Writer->getSectionAddress(A->getFragment()->getParent());
  FixedValue += SecAddr;
  uint32_t Value2 = 0;

  if (const MCSymbolRefExpr *B = Target.getSymB()) {
    const MCSymbol *SB = &B->getSymbol();

    if (!SB->getFragment()) {
      Asm.getContext().reportError(
          Fixup.getLoc(),
          "symbol '" + SB->getName() +
              "' can not be undefined in a subtraction expression");
      return false;
    }

    // Select the appropriate difference relocation type.
    //
    // Note that there is no longer any semantic difference between these two
    // relocation types from the linkers point of view, this is done solely for
    // pedantic compatibility with 'as'.
    Type = A->isExternal() ? (unsigned)MachO::GENERIC_RELOC_SECTDIFF
                           : (unsigned)MachO::GENERIC_RELOC_LOCAL_SECTDIFF;
    Value2 = Writer->getSymbolAddress(*SB, Asm);
    FixedValue -= Writer->getSectionAddress(SB->getFragment()->getParent());
  }

  // Relocations are written out in reverse order, so the PAIR comes first.
  if (Type == MachO::GENERIC_RELOC_SECTDIFF ||
      Type == MachO::GENERIC_RELOC_LOCAL_SECTDIFF) {
    // If the offset is too large to fit in a scattered relocation,
    // we're hosed. It's an unfortunate limitation of the MachO format.
    if (FixupOffset > 0xffffff) {
      char Buffer[32];
      format("0x%x", FixupOffset).print(Buffer, sizeof(Buffer));
      Asm.getContext().reportError(Fixup.getLoc(),
                         Twine("Section too large, can't encode "
                                "r_address (") + Buffer +
                         ") into 24 bits of scattered "
                         "relocation entry.");
      return false;
    }

    MachO::any_relocation_info MRE;
    MRE.r_word0 = ((0                         <<  0) | // r_address
                   (MachO::GENERIC_RELOC_PAIR << 24) | // r_type
                   (Log2Size                  << 28) |
                   (IsPCRel                   << 30) |
                   MachO::R_SCATTERED);
    MRE.r_word1 = Value2;
    Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  } else {
    // If the offset is more than 24-bits, it won't fit in a scattered
    // relocation offset field, so we fall back to using a non-scattered
    // relocation. This is a bit risky, as if the offset reaches out of
    // the block and the linker is doing scattered loading on this
    // symbol, things can go badly.
    //
    // Required for 'as' compatibility.
    if (FixupOffset > 0xffffff) {
      FixedValue = OriginalFixedValue;
      return false;
    }
  }

  MachO::any_relocation_info MRE;
  MRE.r_word0 = ((FixupOffset <<  0) |
                 (Type        << 24) |
                 (Log2Size    << 28) |
                 (IsPCRel     << 30) |
                 MachO::R_SCATTERED);
  MRE.r_word1 = Value;
  Writer->addRelocation(nullptr, Fragment->getParent(), MRE);
  return true;
}

void X86MachObjectWriter::recordTLVPRelocation(MachObjectWriter *Writer,
                                               const MCAssembler &Asm,
                                               const MCFragment *Fragment,
                                               const MCFixup &Fixup,
                                               MCValue Target,
                                               uint64_t &FixedValue) {
  const MCSymbolRefExpr *SymA = Target.getSymA();
  assert(SymA->getKind() == MCSymbolRefExpr::VK_TLVP && !is64Bit() &&
         "Should only be called with a 32-bit TLVP relocation!");

  unsigned Log2Size = getFixupKindLog2Size(Fixup.getKind());
  uint32_t Value = Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();
  unsigned IsPCRel = 0;

  // We're only going to have a second symbol in pic mode and it'll be a
  // subtraction from the picbase. For 32-bit pic the addend is the difference
  // between the picbase and the next address.  For 32-bit static the addend is
  // zero.
  if (auto *SymB = Target.getSymB()) {
    // If this is a subtraction then we're pcrel.
    uint32_t FixupAddress =
        Writer->getFragmentAddress(Asm, Fragment) + Fixup.getOffset();
    IsPCRel = 1;
    FixedValue = FixupAddress -
                 Writer->getSymbolAddress(SymB->getSymbol(), Asm) +
                 Target.getConstant();
    FixedValue += 1ULL << Log2Size;
  } else {
    FixedValue = 0;
  }

  // struct relocation_info (8 bytes)
  MachO::any_relocation_info MRE;
  MRE.r_word0 = Value;
  MRE.r_word1 =
      (IsPCRel << 24) | (Log2Size << 25) | (MachO::GENERIC_RELOC_TLV << 28);
  Writer->addRelocation(&SymA->getSymbol(), Fragment->getParent(), MRE);
}

void X86MachObjectWriter::RecordX86Relocation(MachObjectWriter *Writer,
                                              const MCAssembler &Asm,
                                              const MCFragment *Fragment,
                                              const MCFixup &Fixup,
                                              MCValue Target,
                                              uint64_t &FixedValue) {
  unsigned IsPCRel = Writer->isFixupKindPCRel(Asm, Fixup.getKind());
  unsigned Log2Size = getFixupKindLog2Size(Fixup.getKind());

  // If this is a 32-bit TLVP reloc it's handled a bit differently.
  if (Target.getSymA() &&
      Target.getSymA()->getKind() == MCSymbolRefExpr::VK_TLVP) {
    recordTLVPRelocation(Writer, Asm, Fragment, Fixup, Target, FixedValue);
    return;
  }

  // If this is a difference or a defined symbol plus an offset, then we need a
  // scattered relocation entry. Differences always require scattered
  // relocations.
  if (Target.getSymB()) {
    recordScatteredRelocation(Writer, Asm, Fragment, Fixup, Target, Log2Size,
                              FixedValue);
    return;
  }

  // Get the symbol data, if any.
  const MCSymbol *A = nullptr;
  if (Target.getSymA())
    A = &Target.getSymA()->getSymbol();

  // If this is an internal relocation with an offset, it also needs a scattered
  // relocation entry.
  uint32_t Offset = Target.getConstant();
  if (IsPCRel)
    Offset += 1 << Log2Size;

  // Try to record the scattered relocation if needed. Fall back to non
  // scattered if necessary (see comments in recordScatteredRelocation()
  // for details).
  if (Offset && A && !Writer->doesSymbolRequireExternRelocation(*A) &&
      recordScatteredRelocation(Writer, Asm, Fragment, Fixup, Target, Log2Size,
                                FixedValue))
    return;

  // See <reloc.h>.
  uint32_t FixupOffset = Asm.getFragmentOffset(*Fragment) + Fixup.getOffset();
  unsigned Index = 0;
  unsigned Type = 0;
  const MCSymbol *RelSymbol = nullptr;

  if (Target.isAbsolute()) { // constant
    // SymbolNum of 0 indicates the absolute section.
    //
    // FIXME: Currently, these are never generated (see code below). I cannot
    // find a case where they are actually emitted.
    Type = MachO::GENERIC_RELOC_VANILLA;
  } else {
    assert(A && "Unknown symbol data");

    // Resolve constant variables.
    if (A->isVariable()) {
      int64_t Res;
      if (A->getVariableValue()->evaluateAsAbsolute(
              Res, Asm, Writer->getSectionAddressMap())) {
        FixedValue = Res;
        return;
      }
    }

    // Check whether we need an external or internal relocation.
    if (Writer->doesSymbolRequireExternRelocation(*A)) {
      RelSymbol = A;
      // For external relocations, make sure to offset the fixup value to
      // compensate for the addend of the symbol address, if it was
      // undefined. This occurs with weak definitions, for example.
      if (!A->isUndefined())
        FixedValue -= Asm.getSymbolOffset(*A);
    } else {
      // The index is the section ordinal (1-based).
      const MCSection &Sec = A->getSection();
      Index = Sec.getOrdinal() + 1;
      FixedValue += Writer->getSectionAddress(&Sec);
    }
    if (IsPCRel)
      FixedValue -= Writer->getSectionAddress(Fragment->getParent());

    Type = MachO::GENERIC_RELOC_VANILLA;
  }

  // struct relocation_info (8 bytes)
  MachO::any_relocation_info MRE;
  MRE.r_word0 = FixupOffset;
  MRE.r_word1 =
      (Index << 0) | (IsPCRel << 24) | (Log2Size << 25) | (Type << 28);
  Writer->addRelocation(RelSymbol, Fragment->getParent(), MRE);
}

std::unique_ptr<MCObjectTargetWriter>
llvm::createX86MachObjectWriter(bool Is64Bit, uint32_t CPUType,
                                uint32_t CPUSubtype) {
  return std::make_unique<X86MachObjectWriter>(Is64Bit, CPUType, CPUSubtype);
}
