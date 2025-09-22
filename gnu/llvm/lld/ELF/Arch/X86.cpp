//===- X86.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "OutputSections.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {
class X86 : public TargetInfo {
public:
  X86();
  int getTlsGdRelaxSkip(RelType type) const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void writeGotPltHeader(uint8_t *buf) const override;
  RelType getDynRel(RelType type) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;

  RelExpr adjustTlsExpr(RelType type, RelExpr expr) const override;
  void relocateAlloc(InputSectionBase &sec, uint8_t *buf) const override;
};
} // namespace

X86::X86() {
  copyRel = R_386_COPY;
  gotRel = R_386_GLOB_DAT;
  pltRel = R_386_JUMP_SLOT;
  iRelativeRel = R_386_IRELATIVE;
  relativeRel = R_386_RELATIVE;
  symbolicRel = R_386_32;
  tlsDescRel = R_386_TLS_DESC;
  tlsGotRel = R_386_TLS_TPOFF;
  tlsModuleIndexRel = R_386_TLS_DTPMOD32;
  tlsOffsetRel = R_386_TLS_DTPOFF32;
  gotBaseSymInGotPlt = true;
  pltHeaderSize = 16;
  pltEntrySize = 16;
  ipltEntrySize = 16;
  trapInstr = {0xcc, 0xcc, 0xcc, 0xcc}; // 0xcc = INT3

  // Align to the non-PAE large page size (known as a superpage or huge page).
  // FreeBSD automatically promotes large, superpage-aligned allocations.
  defaultImageBase = 0x400000;
}

int X86::getTlsGdRelaxSkip(RelType type) const {
  // TLSDESC relocations are processed separately. See relaxTlsGdToLe below.
  return type == R_386_TLS_GOTDESC || type == R_386_TLS_DESC_CALL ? 1 : 2;
}

RelExpr X86::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_386_8:
  case R_386_16:
  case R_386_32:
    return R_ABS;
  case R_386_TLS_LDO_32:
    return R_DTPREL;
  case R_386_TLS_GD:
    return R_TLSGD_GOTPLT;
  case R_386_TLS_LDM:
    return R_TLSLD_GOTPLT;
  case R_386_PLT32:
    return R_PLT_PC;
  case R_386_PC8:
  case R_386_PC16:
  case R_386_PC32:
    return R_PC;
  case R_386_GOTPC:
    return R_GOTPLTONLY_PC;
  case R_386_TLS_IE:
    return R_GOT;
  case R_386_GOT32:
  case R_386_GOT32X:
    // These relocations are arguably mis-designed because their calculations
    // depend on the instructions they are applied to. This is bad because we
    // usually don't care about whether the target section contains valid
    // machine instructions or not. But this is part of the documented ABI, so
    // we had to implement as the standard requires.
    //
    // x86 does not support PC-relative data access. Therefore, in order to
    // access GOT contents, a GOT address needs to be known at link-time
    // (which means non-PIC) or compilers have to emit code to get a GOT
    // address at runtime (which means code is position-independent but
    // compilers need to emit extra code for each GOT access.) This decision
    // is made at compile-time. In the latter case, compilers emit code to
    // load a GOT address to a register, which is usually %ebx.
    //
    // So, there are two ways to refer to symbol foo's GOT entry: foo@GOT or
    // foo@GOT(%ebx).
    //
    // foo@GOT is not usable in PIC. If we are creating a PIC output and if we
    // find such relocation, we should report an error. foo@GOT is resolved to
    // an *absolute* address of foo's GOT entry, because both GOT address and
    // foo's offset are known. In other words, it's G + A.
    //
    // foo@GOT(%ebx) needs to be resolved to a *relative* offset from a GOT to
    // foo's GOT entry in the table, because GOT address is not known but foo's
    // offset in the table is known. It's G + A - GOT.
    //
    // It's unfortunate that compilers emit the same relocation for these
    // different use cases. In order to distinguish them, we have to read a
    // machine instruction.
    //
    // The following code implements it. We assume that Loc[0] is the first byte
    // of a displacement or an immediate field of a valid machine
    // instruction. That means a ModRM byte is at Loc[-1]. By taking a look at
    // the byte, we can determine whether the instruction uses the operand as an
    // absolute address (R_GOT) or a register-relative address (R_GOTPLT).
    return (loc[-1] & 0xc7) == 0x5 ? R_GOT : R_GOTPLT;
  case R_386_TLS_GOTDESC:
    return R_TLSDESC_GOTPLT;
  case R_386_TLS_DESC_CALL:
    return R_TLSDESC_CALL;
  case R_386_TLS_GOTIE:
    return R_GOTPLT;
  case R_386_GOTOFF:
    return R_GOTPLTREL;
  case R_386_TLS_LE:
    return R_TPREL;
  case R_386_TLS_LE_32:
    return R_TPREL_NEG;
  case R_386_NONE:
    return R_NONE;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

RelExpr X86::adjustTlsExpr(RelType type, RelExpr expr) const {
  switch (expr) {
  default:
    return expr;
  case R_RELAX_TLS_GD_TO_IE:
    return R_RELAX_TLS_GD_TO_IE_GOTPLT;
  case R_RELAX_TLS_GD_TO_LE:
    return type == R_386_TLS_GD ? R_RELAX_TLS_GD_TO_LE_NEG
                                : R_RELAX_TLS_GD_TO_LE;
  }
}

void X86::writeGotPltHeader(uint8_t *buf) const {
  write32le(buf, mainPart->dynamic->getVA());
}

void X86::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  // Entries in .got.plt initially points back to the corresponding
  // PLT entries with a fixed offset to skip the first instruction.
  write32le(buf, s.getPltVA() + 6);
}

void X86::writeIgotPlt(uint8_t *buf, const Symbol &s) const {
  // An x86 entry is the address of the ifunc resolver function.
  write32le(buf, s.getVA());
}

RelType X86::getDynRel(RelType type) const {
  if (type == R_386_TLS_LE)
    return R_386_TLS_TPOFF;
  if (type == R_386_TLS_LE_32)
    return R_386_TLS_TPOFF32;
  return type;
}

void X86::writePltHeader(uint8_t *buf) const {
  if (config->isPic) {
    const uint8_t v[] = {
        0xff, 0xb3, 0x04, 0x00, 0x00, 0x00, // pushl 4(%ebx)
        0xff, 0xa3, 0x08, 0x00, 0x00, 0x00, // jmp *8(%ebx)
        0x90, 0x90, 0x90, 0x90              // nop
    };
    memcpy(buf, v, sizeof(v));
    return;
  }

  const uint8_t pltData[] = {
      0xff, 0x35, 0, 0, 0, 0, // pushl (GOTPLT+4)
      0xff, 0x25, 0, 0, 0, 0, // jmp *(GOTPLT+8)
      0x90, 0x90, 0x90, 0x90, // nop
  };
  memcpy(buf, pltData, sizeof(pltData));
  uint32_t gotPlt = in.gotPlt->getVA();
  write32le(buf + 2, gotPlt + 4);
  write32le(buf + 8, gotPlt + 8);
}

void X86::writePlt(uint8_t *buf, const Symbol &sym,
                   uint64_t pltEntryAddr) const {
  unsigned relOff = in.relaPlt->entsize * sym.getPltIdx();
  if (config->isPic) {
    const uint8_t inst[] = {
        0xff, 0xa3, 0, 0, 0, 0, // jmp *foo@GOT(%ebx)
        0x68, 0,    0, 0, 0,    // pushl $reloc_offset
        0xe9, 0,    0, 0, 0,    // jmp .PLT0@PC
    };
    memcpy(buf, inst, sizeof(inst));
    write32le(buf + 2, sym.getGotPltVA() - in.gotPlt->getVA());
  } else {
    const uint8_t inst[] = {
        0xff, 0x25, 0, 0, 0, 0, // jmp *foo@GOT
        0x68, 0,    0, 0, 0,    // pushl $reloc_offset
        0xe9, 0,    0, 0, 0,    // jmp .PLT0@PC
    };
    memcpy(buf, inst, sizeof(inst));
    write32le(buf + 2, sym.getGotPltVA());
  }

  write32le(buf + 7, relOff);
  write32le(buf + 12, in.plt->getVA() - pltEntryAddr - 16);
}

int64_t X86::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  case R_386_8:
  case R_386_PC8:
    return SignExtend64<8>(*buf);
  case R_386_16:
  case R_386_PC16:
    return SignExtend64<16>(read16le(buf));
  case R_386_32:
  case R_386_GLOB_DAT:
  case R_386_GOT32:
  case R_386_GOT32X:
  case R_386_GOTOFF:
  case R_386_GOTPC:
  case R_386_IRELATIVE:
  case R_386_PC32:
  case R_386_PLT32:
  case R_386_RELATIVE:
  case R_386_TLS_GOTDESC:
  case R_386_TLS_DESC_CALL:
  case R_386_TLS_DTPMOD32:
  case R_386_TLS_DTPOFF32:
  case R_386_TLS_LDO_32:
  case R_386_TLS_LDM:
  case R_386_TLS_IE:
  case R_386_TLS_IE_32:
  case R_386_TLS_LE:
  case R_386_TLS_LE_32:
  case R_386_TLS_GD:
  case R_386_TLS_GD_32:
  case R_386_TLS_GOTIE:
  case R_386_TLS_TPOFF:
  case R_386_TLS_TPOFF32:
    return SignExtend64<32>(read32le(buf));
  case R_386_TLS_DESC:
    return SignExtend64<32>(read32le(buf + 4));
  case R_386_NONE:
  case R_386_JUMP_SLOT:
    // These relocations are defined as not having an implicit addend.
    return 0;
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  }
}

void X86::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_386_8:
    // R_386_{PC,}{8,16} are not part of the i386 psABI, but they are
    // being used for some 16-bit programs such as boot loaders, so
    // we want to support them.
    checkIntUInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_386_PC8:
    checkInt(loc, val, 8, rel);
    *loc = val;
    break;
  case R_386_16:
    checkIntUInt(loc, val, 16, rel);
    write16le(loc, val);
    break;
  case R_386_PC16:
    // R_386_PC16 is normally used with 16 bit code. In that situation
    // the PC is 16 bits, just like the addend. This means that it can
    // point from any 16 bit address to any other if the possibility
    // of wrapping is included.
    // The only restriction we have to check then is that the destination
    // address fits in 16 bits. That is impossible to do here. The problem is
    // that we are passed the final value, which already had the
    // current location subtracted from it.
    // We just check that Val fits in 17 bits. This misses some cases, but
    // should have no false positives.
    checkInt(loc, val, 17, rel);
    write16le(loc, val);
    break;
  case R_386_32:
  case R_386_GOT32:
  case R_386_GOT32X:
  case R_386_GOTOFF:
  case R_386_GOTPC:
  case R_386_PC32:
  case R_386_PLT32:
  case R_386_RELATIVE:
  case R_386_TLS_GOTDESC:
  case R_386_TLS_DESC_CALL:
  case R_386_TLS_DTPMOD32:
  case R_386_TLS_DTPOFF32:
  case R_386_TLS_GD:
  case R_386_TLS_GOTIE:
  case R_386_TLS_IE:
  case R_386_TLS_LDM:
  case R_386_TLS_LDO_32:
  case R_386_TLS_LE:
  case R_386_TLS_LE_32:
  case R_386_TLS_TPOFF:
  case R_386_TLS_TPOFF32:
    checkInt(loc, val, 32, rel);
    write32le(loc, val);
    break;
  case R_386_TLS_DESC:
    // The addend is stored in the second 32-bit word.
    write32le(loc + 4, val);
    break;
  default:
    llvm_unreachable("unknown relocation");
  }
}

static void relaxTlsGdToLe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  if (rel.type == R_386_TLS_GD) {
    // Convert (loc[-2] == 0x04)
    //   leal x@tlsgd(, %ebx, 1), %eax
    //   call ___tls_get_addr@plt
    // or
    //   leal x@tlsgd(%reg), %eax
    //   call *___tls_get_addr@got(%reg)
    // to
    const uint8_t inst[] = {
        0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0, %eax
        0x81, 0xe8, 0,    0,    0,    0,    // subl x@ntpoff(%ebx), %eax
    };
    uint8_t *w = loc[-2] == 0x04 ? loc - 3 : loc - 2;
    memcpy(w, inst, sizeof(inst));
    write32le(w + 8, val);
  } else if (rel.type == R_386_TLS_GOTDESC) {
    // Convert leal x@tlsdesc(%ebx), %eax to leal x@ntpoff, %eax.
    //
    // Note: call *x@tlsdesc(%eax) may not immediately follow this instruction.
    if (memcmp(loc - 2, "\x8d\x83", 2)) {
      error(getErrorLocation(loc - 2) +
            "R_386_TLS_GOTDESC must be used in leal x@tlsdesc(%ebx), %eax");
      return;
    }
    loc[-1] = 0x05;
    write32le(loc, val);
  } else {
    // Convert call *x@tlsdesc(%eax) to xchg ax, ax.
    assert(rel.type == R_386_TLS_DESC_CALL);
    loc[0] = 0x66;
    loc[1] = 0x90;
  }
}

static void relaxTlsGdToIe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  if (rel.type == R_386_TLS_GD) {
    // Convert (loc[-2] == 0x04)
    //   leal x@tlsgd(, %ebx, 1), %eax
    //   call ___tls_get_addr@plt
    // or
    //   leal x@tlsgd(%reg), %eax
    //   call *___tls_get_addr@got(%reg)
    const uint8_t inst[] = {
        0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0, %eax
        0x03, 0x83, 0,    0,    0,    0,    // addl x@gottpoff(%ebx), %eax
    };
    uint8_t *w = loc[-2] == 0x04 ? loc - 3 : loc - 2;
    memcpy(w, inst, sizeof(inst));
    write32le(w + 8, val);
  } else if (rel.type == R_386_TLS_GOTDESC) {
    // Convert leal x@tlsdesc(%ebx), %eax to movl x@gotntpoff(%ebx), %eax.
    if (memcmp(loc - 2, "\x8d\x83", 2)) {
      error(getErrorLocation(loc - 2) +
            "R_386_TLS_GOTDESC must be used in leal x@tlsdesc(%ebx), %eax");
      return;
    }
    loc[-2] = 0x8b;
    write32le(loc, val);
  } else {
    // Convert call *x@tlsdesc(%eax) to xchg ax, ax.
    assert(rel.type == R_386_TLS_DESC_CALL);
    loc[0] = 0x66;
    loc[1] = 0x90;
  }
}

// In some conditions, relocations can be optimized to avoid using GOT.
// This function does that for Initial Exec to Local Exec case.
static void relaxTlsIeToLe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  // Ulrich's document section 6.2 says that @gotntpoff can
  // be used with MOVL or ADDL instructions.
  // @indntpoff is similar to @gotntpoff, but for use in
  // position dependent code.
  uint8_t reg = (loc[-1] >> 3) & 7;

  if (rel.type == R_386_TLS_IE) {
    if (loc[-1] == 0xa1) {
      // "movl foo@indntpoff,%eax" -> "movl $foo,%eax"
      // This case is different from the generic case below because
      // this is a 5 byte instruction while below is 6 bytes.
      loc[-1] = 0xb8;
    } else if (loc[-2] == 0x8b) {
      // "movl foo@indntpoff,%reg" -> "movl $foo,%reg"
      loc[-2] = 0xc7;
      loc[-1] = 0xc0 | reg;
    } else {
      // "addl foo@indntpoff,%reg" -> "addl $foo,%reg"
      loc[-2] = 0x81;
      loc[-1] = 0xc0 | reg;
    }
  } else {
    assert(rel.type == R_386_TLS_GOTIE);
    if (loc[-2] == 0x8b) {
      // "movl foo@gottpoff(%rip),%reg" -> "movl $foo,%reg"
      loc[-2] = 0xc7;
      loc[-1] = 0xc0 | reg;
    } else {
      // "addl foo@gotntpoff(%rip),%reg" -> "leal foo(%reg),%reg"
      loc[-2] = 0x8d;
      loc[-1] = 0x80 | (reg << 3) | reg;
    }
  }
  write32le(loc, val);
}

static void relaxTlsLdToLe(uint8_t *loc, const Relocation &rel, uint64_t val) {
  if (rel.type == R_386_TLS_LDO_32) {
    write32le(loc, val);
    return;
  }

  if (loc[4] == 0xe8) {
    // Convert
    //   leal x(%reg),%eax
    //   call ___tls_get_addr@plt
    // to
    const uint8_t inst[] = {
        0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0,%eax
        0x90,                               // nop
        0x8d, 0x74, 0x26, 0x00,             // leal 0(%esi,1),%esi
    };
    memcpy(loc - 2, inst, sizeof(inst));
    return;
  }

  // Convert
  //   leal x(%reg),%eax
  //   call *___tls_get_addr@got(%reg)
  // to
  const uint8_t inst[] = {
      0x65, 0xa1, 0x00, 0x00, 0x00, 0x00, // movl %gs:0,%eax
      0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00, // leal (%esi),%esi
  };
  memcpy(loc - 2, inst, sizeof(inst));
}

void X86::relocateAlloc(InputSectionBase &sec, uint8_t *buf) const {
  uint64_t secAddr = sec.getOutputSection()->addr;
  if (auto *s = dyn_cast<InputSection>(&sec))
    secAddr += s->outSecOff;
  for (const Relocation &rel : sec.relocs()) {
    uint8_t *loc = buf + rel.offset;
    const uint64_t val = SignExtend64(
        sec.getRelocTargetVA(sec.file, rel.type, rel.addend,
                             secAddr + rel.offset, *rel.sym, rel.expr),
        32);
    switch (rel.expr) {
    case R_RELAX_TLS_GD_TO_IE_GOTPLT:
      relaxTlsGdToIe(loc, rel, val);
      continue;
    case R_RELAX_TLS_GD_TO_LE:
    case R_RELAX_TLS_GD_TO_LE_NEG:
      relaxTlsGdToLe(loc, rel, val);
      continue;
    case R_RELAX_TLS_LD_TO_LE:
      relaxTlsLdToLe(loc, rel, val);
      break;
    case R_RELAX_TLS_IE_TO_LE:
      relaxTlsIeToLe(loc, rel, val);
      continue;
    default:
      relocate(loc, rel, val);
      break;
    }
  }
}

// If Intel Indirect Branch Tracking is enabled, we have to emit special PLT
// entries containing endbr32 instructions. A PLT entry will be split into two
// parts, one in .plt.sec (writePlt), and the other in .plt (writeIBTPlt).
namespace {
class IntelIBT : public X86 {
public:
  IntelIBT();
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void writeIBTPlt(uint8_t *buf, size_t numEntries) const override;

  static const unsigned IBTPltHeaderSize = 16;
};
} // namespace

IntelIBT::IntelIBT() { pltHeaderSize = 0; }

void IntelIBT::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  uint64_t va =
      in.ibtPlt->getVA() + IBTPltHeaderSize + s.getPltIdx() * pltEntrySize;
  write32le(buf, va);
}

void IntelIBT::writePlt(uint8_t *buf, const Symbol &sym,
                        uint64_t /*pltEntryAddr*/) const {
  if (config->isPic) {
    const uint8_t inst[] = {
        0xf3, 0x0f, 0x1e, 0xfb,       // endbr32
        0xff, 0xa3, 0,    0,    0, 0, // jmp *name@GOT(%ebx)
        0x66, 0x0f, 0x1f, 0x44, 0, 0, // nop
    };
    memcpy(buf, inst, sizeof(inst));
    write32le(buf + 6, sym.getGotPltVA() - in.gotPlt->getVA());
    return;
  }

  const uint8_t inst[] = {
      0xf3, 0x0f, 0x1e, 0xfb,       // endbr32
      0xff, 0x25, 0,    0,    0, 0, // jmp *foo@GOT
      0x66, 0x0f, 0x1f, 0x44, 0, 0, // nop
  };
  memcpy(buf, inst, sizeof(inst));
  write32le(buf + 6, sym.getGotPltVA());
}

void IntelIBT::writeIBTPlt(uint8_t *buf, size_t numEntries) const {
  writePltHeader(buf);
  buf += IBTPltHeaderSize;

  const uint8_t inst[] = {
      0xf3, 0x0f, 0x1e, 0xfb,    // endbr32
      0x68, 0,    0,    0,    0, // pushl $reloc_offset
      0xe9, 0,    0,    0,    0, // jmpq .PLT0@PC
      0x66, 0x90,                // nop
  };

  for (size_t i = 0; i < numEntries; ++i) {
    memcpy(buf, inst, sizeof(inst));
    write32le(buf + 5, i * sizeof(object::ELF32LE::Rel));
    write32le(buf + 10, -pltHeaderSize - sizeof(inst) * i - 30);
    buf += sizeof(inst);
  }
}

namespace {
class RetpolinePic : public X86 {
public:
  RetpolinePic();
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};

class RetpolineNoPic : public X86 {
public:
  RetpolineNoPic();
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
};
} // namespace

RetpolinePic::RetpolinePic() {
  pltHeaderSize = 48;
  pltEntrySize = 32;
  ipltEntrySize = 32;
}

void RetpolinePic::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write32le(buf, s.getPltVA() + 17);
}

void RetpolinePic::writePltHeader(uint8_t *buf) const {
  const uint8_t insn[] = {
      0xff, 0xb3, 4,    0,    0,    0,          // 0:    pushl 4(%ebx)
      0x50,                                     // 6:    pushl %eax
      0x8b, 0x83, 8,    0,    0,    0,          // 7:    mov 8(%ebx), %eax
      0xe8, 0x0e, 0x00, 0x00, 0x00,             // d:    call next
      0xf3, 0x90,                               // 12: loop: pause
      0x0f, 0xae, 0xe8,                         // 14:   lfence
      0xeb, 0xf9,                               // 17:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 19:   int3; .align 16
      0x89, 0x0c, 0x24,                         // 20: next: mov %ecx, (%esp)
      0x8b, 0x4c, 0x24, 0x04,                   // 23:   mov 0x4(%esp), %ecx
      0x89, 0x44, 0x24, 0x04,                   // 27:   mov %eax ,0x4(%esp)
      0x89, 0xc8,                               // 2b:   mov %ecx, %eax
      0x59,                                     // 2d:   pop %ecx
      0xc3,                                     // 2e:   ret
      0xcc,                                     // 2f:   int3; padding
  };
  memcpy(buf, insn, sizeof(insn));
}

void RetpolinePic::writePlt(uint8_t *buf, const Symbol &sym,
                            uint64_t pltEntryAddr) const {
  unsigned relOff = in.relaPlt->entsize * sym.getPltIdx();
  const uint8_t insn[] = {
      0x50,                            // pushl %eax
      0x8b, 0x83, 0,    0,    0,    0, // mov foo@GOT(%ebx), %eax
      0xe8, 0,    0,    0,    0,       // call plt+0x20
      0xe9, 0,    0,    0,    0,       // jmp plt+0x12
      0x68, 0,    0,    0,    0,       // pushl $reloc_offset
      0xe9, 0,    0,    0,    0,       // jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc,    // int3; padding
  };
  memcpy(buf, insn, sizeof(insn));

  uint32_t ebx = in.gotPlt->getVA();
  unsigned off = pltEntryAddr - in.plt->getVA();
  write32le(buf + 3, sym.getGotPltVA() - ebx);
  write32le(buf + 8, -off - 12 + 32);
  write32le(buf + 13, -off - 17 + 18);
  write32le(buf + 18, relOff);
  write32le(buf + 23, -off - 27);
}

RetpolineNoPic::RetpolineNoPic() {
  pltHeaderSize = 48;
  pltEntrySize = 32;
  ipltEntrySize = 32;
}

void RetpolineNoPic::writeGotPlt(uint8_t *buf, const Symbol &s) const {
  write32le(buf, s.getPltVA() + 16);
}

void RetpolineNoPic::writePltHeader(uint8_t *buf) const {
  const uint8_t insn[] = {
      0xff, 0x35, 0,    0,    0,    0, // 0:    pushl GOTPLT+4
      0x50,                            // 6:    pushl %eax
      0xa1, 0,    0,    0,    0,       // 7:    mov GOTPLT+8, %eax
      0xe8, 0x0f, 0x00, 0x00, 0x00,    // c:    call next
      0xf3, 0x90,                      // 11: loop: pause
      0x0f, 0xae, 0xe8,                // 13:   lfence
      0xeb, 0xf9,                      // 16:   jmp loop
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc,    // 18:   int3
      0xcc, 0xcc, 0xcc,                // 1f:   int3; .align 16
      0x89, 0x0c, 0x24,                // 20: next: mov %ecx, (%esp)
      0x8b, 0x4c, 0x24, 0x04,          // 23:   mov 0x4(%esp), %ecx
      0x89, 0x44, 0x24, 0x04,          // 27:   mov %eax ,0x4(%esp)
      0x89, 0xc8,                      // 2b:   mov %ecx, %eax
      0x59,                            // 2d:   pop %ecx
      0xc3,                            // 2e:   ret
      0xcc,                            // 2f:   int3; padding
  };
  memcpy(buf, insn, sizeof(insn));

  uint32_t gotPlt = in.gotPlt->getVA();
  write32le(buf + 2, gotPlt + 4);
  write32le(buf + 8, gotPlt + 8);
}

void RetpolineNoPic::writePlt(uint8_t *buf, const Symbol &sym,
                              uint64_t pltEntryAddr) const {
  unsigned relOff = in.relaPlt->entsize * sym.getPltIdx();
  const uint8_t insn[] = {
      0x50,                         // 0:  pushl %eax
      0xa1, 0,    0,    0,    0,    // 1:  mov foo_in_GOT, %eax
      0xe8, 0,    0,    0,    0,    // 6:  call plt+0x20
      0xe9, 0,    0,    0,    0,    // b:  jmp plt+0x11
      0x68, 0,    0,    0,    0,    // 10: pushl $reloc_offset
      0xe9, 0,    0,    0,    0,    // 15: jmp plt+0
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, // 1a: int3; padding
      0xcc,                         // 1f: int3; padding
  };
  memcpy(buf, insn, sizeof(insn));

  unsigned off = pltEntryAddr - in.plt->getVA();
  write32le(buf + 2, sym.getGotPltVA());
  write32le(buf + 7, -off - 11 + 32);
  write32le(buf + 12, -off - 16 + 17);
  write32le(buf + 17, relOff);
  write32le(buf + 22, -off - 26);
}

TargetInfo *elf::getX86TargetInfo() {
  if (config->zRetpolineplt) {
    if (config->isPic) {
      static RetpolinePic t;
      return &t;
    }
    static RetpolineNoPic t;
    return &t;
  }

  if (config->andFeatures & GNU_PROPERTY_X86_FEATURE_1_IBT) {
    static IntelIBT t;
    return &t;
  }

  static X86 t;
  return &t;
}
