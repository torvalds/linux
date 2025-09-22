//===- ARM.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InputFiles.h"
#include "OutputSections.h"
#include "SymbolTable.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/ErrorHandler.h"
#include "lld/Common/Filesystem.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::support::endian;
using namespace llvm::support;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;
using namespace llvm::object;

namespace {
class ARM final : public TargetInfo {
public:
  ARM();
  uint32_t calcEFlags() const override;
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  RelType getDynRel(RelType type) const override;
  int64_t getImplicitAddend(const uint8_t *buf, RelType type) const override;
  void writeGotPlt(uint8_t *buf, const Symbol &s) const override;
  void writeIgotPlt(uint8_t *buf, const Symbol &s) const override;
  void writePltHeader(uint8_t *buf) const override;
  void writePlt(uint8_t *buf, const Symbol &sym,
                uint64_t pltEntryAddr) const override;
  void addPltSymbols(InputSection &isec, uint64_t off) const override;
  void addPltHeaderSymbols(InputSection &isd) const override;
  bool needsThunk(RelExpr expr, RelType type, const InputFile *file,
                  uint64_t branchAddr, const Symbol &s,
                  int64_t a) const override;
  uint32_t getThunkSectionSpacing() const override;
  bool inBranchRange(RelType type, uint64_t src, uint64_t dst) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};
enum class CodeState { Data = 0, Thumb = 2, Arm = 4 };
} // namespace

static DenseMap<InputSection *, SmallVector<const Defined *, 0>> sectionMap{};

ARM::ARM() {
  copyRel = R_ARM_COPY;
  relativeRel = R_ARM_RELATIVE;
  iRelativeRel = R_ARM_IRELATIVE;
  gotRel = R_ARM_GLOB_DAT;
  pltRel = R_ARM_JUMP_SLOT;
  symbolicRel = R_ARM_ABS32;
  tlsGotRel = R_ARM_TLS_TPOFF32;
  tlsModuleIndexRel = R_ARM_TLS_DTPMOD32;
  tlsOffsetRel = R_ARM_TLS_DTPOFF32;
  pltHeaderSize = 32;
  pltEntrySize = 16;
  ipltEntrySize = 16;
  trapInstr = {0xd4, 0xd4, 0xd4, 0xd4};
  needsThunks = true;
  defaultMaxPageSize = 65536;
}

uint32_t ARM::calcEFlags() const {
  // The ABIFloatType is used by loaders to detect the floating point calling
  // convention.
  uint32_t abiFloatType = 0;

  // Set the EF_ARM_BE8 flag in the ELF header, if ELF file is big-endian
  // with BE-8 code.
  uint32_t armBE8 = 0;

  if (config->armVFPArgs == ARMVFPArgKind::Base ||
      config->armVFPArgs == ARMVFPArgKind::Default)
    abiFloatType = EF_ARM_ABI_FLOAT_SOFT;
  else if (config->armVFPArgs == ARMVFPArgKind::VFP)
    abiFloatType = EF_ARM_ABI_FLOAT_HARD;

  if (!config->isLE && config->armBe8)
    armBE8 = EF_ARM_BE8;

  // We don't currently use any features incompatible with EF_ARM_EABI_VER5,
  // but we don't have any firm guarantees of conformance. Linux AArch64
  // kernels (as of 2016) require an EABI version to be set.
  return EF_ARM_EABI_VER5 | abiFloatType | armBE8;
}

RelExpr ARM::getRelExpr(RelType type, const Symbol &s,
                        const uint8_t *loc) const {
  switch (type) {
  case R_ARM_ABS32:
  case R_ARM_MOVW_ABS_NC:
  case R_ARM_MOVT_ABS:
  case R_ARM_THM_MOVW_ABS_NC:
  case R_ARM_THM_MOVT_ABS:
  case R_ARM_THM_ALU_ABS_G0_NC:
  case R_ARM_THM_ALU_ABS_G1_NC:
  case R_ARM_THM_ALU_ABS_G2_NC:
  case R_ARM_THM_ALU_ABS_G3:
    return R_ABS;
  case R_ARM_THM_JUMP8:
  case R_ARM_THM_JUMP11:
    return R_PC;
  case R_ARM_CALL:
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_PREL31:
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    return R_PLT_PC;
  case R_ARM_GOTOFF32:
    // (S + A) - GOT_ORG
    return R_GOTREL;
  case R_ARM_GOT_BREL:
    // GOT(S) + A - GOT_ORG
    return R_GOT_OFF;
  case R_ARM_GOT_PREL:
  case R_ARM_TLS_IE32:
    // GOT(S) + A - P
    return R_GOT_PC;
  case R_ARM_SBREL32:
    return R_ARM_SBREL;
  case R_ARM_TARGET1:
    return config->target1Rel ? R_PC : R_ABS;
  case R_ARM_TARGET2:
    if (config->target2 == Target2Policy::Rel)
      return R_PC;
    if (config->target2 == Target2Policy::Abs)
      return R_ABS;
    return R_GOT_PC;
  case R_ARM_TLS_GD32:
    return R_TLSGD_PC;
  case R_ARM_TLS_LDM32:
    return R_TLSLD_PC;
  case R_ARM_TLS_LDO32:
    return R_DTPREL;
  case R_ARM_BASE_PREL:
    // B(S) + A - P
    // FIXME: currently B(S) assumed to be .got, this may not hold for all
    // platforms.
    return R_GOTONLY_PC;
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVT_PREL:
  case R_ARM_REL32:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVT_PREL:
    return R_PC;
  case R_ARM_ALU_PC_G0:
  case R_ARM_ALU_PC_G0_NC:
  case R_ARM_ALU_PC_G1:
  case R_ARM_ALU_PC_G1_NC:
  case R_ARM_ALU_PC_G2:
  case R_ARM_LDR_PC_G0:
  case R_ARM_LDR_PC_G1:
  case R_ARM_LDR_PC_G2:
  case R_ARM_LDRS_PC_G0:
  case R_ARM_LDRS_PC_G1:
  case R_ARM_LDRS_PC_G2:
  case R_ARM_THM_ALU_PREL_11_0:
  case R_ARM_THM_PC8:
  case R_ARM_THM_PC12:
    return R_ARM_PCA;
  case R_ARM_MOVW_BREL_NC:
  case R_ARM_MOVW_BREL:
  case R_ARM_MOVT_BREL:
  case R_ARM_THM_MOVW_BREL_NC:
  case R_ARM_THM_MOVW_BREL:
  case R_ARM_THM_MOVT_BREL:
    return R_ARM_SBREL;
  case R_ARM_NONE:
    return R_NONE;
  case R_ARM_TLS_LE32:
    return R_TPREL;
  case R_ARM_V4BX:
    // V4BX is just a marker to indicate there's a "bx rN" instruction at the
    // given address. It can be used to implement a special linker mode which
    // rewrites ARMv4T inputs to ARMv4. Since we support only ARMv4 input and
    // not ARMv4 output, we can just ignore it.
    return R_NONE;
  default:
    error(getErrorLocation(loc) + "unknown relocation (" + Twine(type) +
          ") against symbol " + toString(s));
    return R_NONE;
  }
}

RelType ARM::getDynRel(RelType type) const {
  if ((type == R_ARM_ABS32) || (type == R_ARM_TARGET1 && !config->target1Rel))
    return R_ARM_ABS32;
  return R_ARM_NONE;
}

void ARM::writeGotPlt(uint8_t *buf, const Symbol &) const {
  write32(buf, in.plt->getVA());
}

void ARM::writeIgotPlt(uint8_t *buf, const Symbol &s) const {
  // An ARM entry is the address of the ifunc resolver function.
  write32(buf, s.getVA());
}

// Long form PLT Header that does not have any restrictions on the displacement
// of the .plt from the .got.plt.
static void writePltHeaderLong(uint8_t *buf) {
  write32(buf + 0, 0xe52de004);   //     str lr, [sp,#-4]!
  write32(buf + 4, 0xe59fe004);   //     ldr lr, L2
  write32(buf + 8, 0xe08fe00e);   // L1: add lr, pc, lr
  write32(buf + 12, 0xe5bef008);  //     ldr pc, [lr, #8]
  write32(buf + 16, 0x00000000);  // L2: .word   &(.got.plt) - L1 - 8
  write32(buf + 20, 0xd4d4d4d4);  //     Pad to 32-byte boundary
  write32(buf + 24, 0xd4d4d4d4);  //     Pad to 32-byte boundary
  write32(buf + 28, 0xd4d4d4d4);
  uint64_t gotPlt = in.gotPlt->getVA();
  uint64_t l1 = in.plt->getVA() + 8;
  write32(buf + 16, gotPlt - l1 - 8);
}

// True if we should use Thumb PLTs, which currently require Thumb2, and are
// only used if the target does not have the ARM ISA.
static bool useThumbPLTs() {
  return config->armHasThumb2ISA && !config->armHasArmISA;
}

// The default PLT header requires the .got.plt to be within 128 Mb of the
// .plt in the positive direction.
void ARM::writePltHeader(uint8_t *buf) const {
  if (useThumbPLTs()) {
    // The instruction sequence for thumb:
    //
    // 0: b500          push    {lr}
    // 2: f8df e008     ldr.w   lr, [pc, #0x8]          @ 0xe <func+0xe>
    // 6: 44fe          add     lr, pc
    // 8: f85e ff08     ldr     pc, [lr, #8]!
    // e:               .word   .got.plt - .plt - 16
    //
    // At 0x8, we want to jump to .got.plt, the -16 accounts for 8 bytes from
    // `pc` in the add instruction and 8 bytes for the `lr` adjustment.
    //
    uint64_t offset = in.gotPlt->getVA() - in.plt->getVA() - 16;
    assert(llvm::isUInt<32>(offset) && "This should always fit into a 32-bit offset");
    write16(buf + 0, 0xb500);
    // Split into two halves to support endianness correctly.
    write16(buf + 2, 0xf8df);
    write16(buf + 4, 0xe008);
    write16(buf + 6, 0x44fe);
    // Split into two halves to support endianness correctly.
    write16(buf + 8, 0xf85e);
    write16(buf + 10, 0xff08);
    write32(buf + 12, offset);

    memcpy(buf + 16, trapInstr.data(), 4);  // Pad to 32-byte boundary
    memcpy(buf + 20, trapInstr.data(), 4);
    memcpy(buf + 24, trapInstr.data(), 4);
    memcpy(buf + 28, trapInstr.data(), 4);
  } else {
    // Use a similar sequence to that in writePlt(), the difference is the
    // calling conventions mean we use lr instead of ip. The PLT entry is
    // responsible for saving lr on the stack, the dynamic loader is responsible
    // for reloading it.
    const uint32_t pltData[] = {
        0xe52de004, // L1: str lr, [sp,#-4]!
        0xe28fe600, //     add lr, pc,  #0x0NN00000 &(.got.plt - L1 - 4)
        0xe28eea00, //     add lr, lr,  #0x000NN000 &(.got.plt - L1 - 4)
        0xe5bef000, //     ldr pc, [lr, #0x00000NNN] &(.got.plt -L1 - 4)
    };

    uint64_t offset = in.gotPlt->getVA() - in.plt->getVA() - 4;
    if (!llvm::isUInt<27>(offset)) {
      // We cannot encode the Offset, use the long form.
      writePltHeaderLong(buf);
      return;
    }
    write32(buf + 0, pltData[0]);
    write32(buf + 4, pltData[1] | ((offset >> 20) & 0xff));
    write32(buf + 8, pltData[2] | ((offset >> 12) & 0xff));
    write32(buf + 12, pltData[3] | (offset & 0xfff));
    memcpy(buf + 16, trapInstr.data(), 4); // Pad to 32-byte boundary
    memcpy(buf + 20, trapInstr.data(), 4);
    memcpy(buf + 24, trapInstr.data(), 4);
    memcpy(buf + 28, trapInstr.data(), 4);
  }
}

void ARM::addPltHeaderSymbols(InputSection &isec) const {
  if (useThumbPLTs()) {
    addSyntheticLocal("$t", STT_NOTYPE, 0, 0, isec);
    addSyntheticLocal("$d", STT_NOTYPE, 12, 0, isec);
  } else {
    addSyntheticLocal("$a", STT_NOTYPE, 0, 0, isec);
    addSyntheticLocal("$d", STT_NOTYPE, 16, 0, isec);
  }
}

// Long form PLT entries that do not have any restrictions on the displacement
// of the .plt from the .got.plt.
static void writePltLong(uint8_t *buf, uint64_t gotPltEntryAddr,
                         uint64_t pltEntryAddr) {
  write32(buf + 0, 0xe59fc004);   //     ldr ip, L2
  write32(buf + 4, 0xe08cc00f);   // L1: add ip, ip, pc
  write32(buf + 8, 0xe59cf000);   //     ldr pc, [ip]
  write32(buf + 12, 0x00000000);  // L2: .word   Offset(&(.got.plt) - L1 - 8
  uint64_t l1 = pltEntryAddr + 4;
  write32(buf + 12, gotPltEntryAddr - l1 - 8);
}

// The default PLT entries require the .got.plt to be within 128 Mb of the
// .plt in the positive direction.
void ARM::writePlt(uint8_t *buf, const Symbol &sym,
                   uint64_t pltEntryAddr) const {

  if (!useThumbPLTs()) {
    uint64_t offset = sym.getGotPltVA() - pltEntryAddr - 8;

    // The PLT entry is similar to the example given in Appendix A of ELF for
    // the Arm Architecture. Instead of using the Group Relocations to find the
    // optimal rotation for the 8-bit immediate used in the add instructions we
    // hard code the most compact rotations for simplicity. This saves a load
    // instruction over the long plt sequences.
    const uint32_t pltData[] = {
        0xe28fc600, // L1: add ip, pc,  #0x0NN00000  Offset(&(.got.plt) - L1 - 8
        0xe28cca00, //     add ip, ip,  #0x000NN000  Offset(&(.got.plt) - L1 - 8
        0xe5bcf000, //     ldr pc, [ip, #0x00000NNN] Offset(&(.got.plt) - L1 - 8
    };
    if (!llvm::isUInt<27>(offset)) {
      // We cannot encode the Offset, use the long form.
      writePltLong(buf, sym.getGotPltVA(), pltEntryAddr);
      return;
    }
    write32(buf + 0, pltData[0] | ((offset >> 20) & 0xff));
    write32(buf + 4, pltData[1] | ((offset >> 12) & 0xff));
    write32(buf + 8, pltData[2] | (offset & 0xfff));
    memcpy(buf + 12, trapInstr.data(), 4); // Pad to 16-byte boundary
  } else {
    uint64_t offset = sym.getGotPltVA() - pltEntryAddr - 12;
    assert(llvm::isUInt<32>(offset) && "This should always fit into a 32-bit offset");

    // A PLT entry will be:
    //
    //       movw ip, #<lower 16 bits>
    //       movt ip, #<upper 16 bits>
    //       add ip, pc
    //   L1: ldr.w pc, [ip]
    //       b L1
    //
    // where ip = r12 = 0xc

    // movw ip, #<lower 16 bits>
    write16(buf + 2, 0x0c00); // use `ip`
    relocateNoSym(buf, R_ARM_THM_MOVW_ABS_NC, offset);

    // movt ip, #<upper 16 bits>
    write16(buf + 6, 0x0c00); // use `ip`
    relocateNoSym(buf + 4, R_ARM_THM_MOVT_ABS, offset);

    write16(buf + 8, 0x44fc);       // add ip, pc
    write16(buf + 10, 0xf8dc);      // ldr.w   pc, [ip] (bottom half)
    write16(buf + 12, 0xf000);      // ldr.w   pc, [ip] (upper half)
    write16(buf + 14, 0xe7fc);      // Branch to previous instruction
  }
}

void ARM::addPltSymbols(InputSection &isec, uint64_t off) const {
  if (useThumbPLTs()) {
    addSyntheticLocal("$t", STT_NOTYPE, off, 0, isec);
  } else {
    addSyntheticLocal("$a", STT_NOTYPE, off, 0, isec);
    addSyntheticLocal("$d", STT_NOTYPE, off + 12, 0, isec);
  }
}

bool ARM::needsThunk(RelExpr expr, RelType type, const InputFile *file,
                     uint64_t branchAddr, const Symbol &s,
                     int64_t a) const {
  // If s is an undefined weak symbol and does not have a PLT entry then it will
  // be resolved as a branch to the next instruction. If it is hidden, its
  // binding has been converted to local, so we just check isUndefined() here. A
  // undefined non-weak symbol will have been errored.
  if (s.isUndefined() && !s.isInPlt())
    return false;
  // A state change from ARM to Thumb and vice versa must go through an
  // interworking thunk if the relocation type is not R_ARM_CALL or
  // R_ARM_THM_CALL.
  switch (type) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
    // Source is ARM, all PLT entries are ARM so no interworking required.
    // Otherwise we need to interwork if STT_FUNC Symbol has bit 0 set (Thumb).
    assert(!useThumbPLTs() &&
           "If the source is ARM, we should not need Thumb PLTs");
    if (s.isFunc() && expr == R_PC && (s.getVA() & 1))
      return true;
    [[fallthrough]];
  case R_ARM_CALL: {
    uint64_t dst = (expr == R_PLT_PC) ? s.getPltVA() : s.getVA();
    return !inBranchRange(type, branchAddr, dst + a) ||
        (!config->armHasBlx && (s.getVA() & 1));
  }
  case R_ARM_THM_JUMP19:
  case R_ARM_THM_JUMP24:
    // Source is Thumb, when all PLT entries are ARM interworking is required.
    // Otherwise we need to interwork if STT_FUNC Symbol has bit 0 clear (ARM).
    if ((expr == R_PLT_PC && !useThumbPLTs()) ||
        (s.isFunc() && (s.getVA() & 1) == 0))
      return true;
    [[fallthrough]];
  case R_ARM_THM_CALL: {
    uint64_t dst = (expr == R_PLT_PC) ? s.getPltVA() : s.getVA();
    return !inBranchRange(type, branchAddr, dst + a) ||
        (!config->armHasBlx && (s.getVA() & 1) == 0);;
  }
  }
  return false;
}

uint32_t ARM::getThunkSectionSpacing() const {
  // The placing of pre-created ThunkSections is controlled by the value
  // thunkSectionSpacing returned by getThunkSectionSpacing(). The aim is to
  // place the ThunkSection such that all branches from the InputSections
  // prior to the ThunkSection can reach a Thunk placed at the end of the
  // ThunkSection. Graphically:
  // | up to thunkSectionSpacing .text input sections |
  // | ThunkSection                                   |
  // | up to thunkSectionSpacing .text input sections |
  // | ThunkSection                                   |

  // Pre-created ThunkSections are spaced roughly 16MiB apart on ARMv7. This
  // is to match the most common expected case of a Thumb 2 encoded BL, BLX or
  // B.W:
  // ARM B, BL, BLX range +/- 32MiB
  // Thumb B.W, BL, BLX range +/- 16MiB
  // Thumb B<cc>.W range +/- 1MiB
  // If a branch cannot reach a pre-created ThunkSection a new one will be
  // created so we can handle the rare cases of a Thumb 2 conditional branch.
  // We intentionally use a lower size for thunkSectionSpacing than the maximum
  // branch range so the end of the ThunkSection is more likely to be within
  // range of the branch instruction that is furthest away. The value we shorten
  // thunkSectionSpacing by is set conservatively to allow us to create 16,384
  // 12 byte Thunks at any offset in a ThunkSection without risk of a branch to
  // one of the Thunks going out of range.

  // On Arm the thunkSectionSpacing depends on the range of the Thumb Branch
  // range. On earlier Architectures such as ARMv4, ARMv5 and ARMv6 (except
  // ARMv6T2) the range is +/- 4MiB.

  return (config->armJ1J2BranchEncoding) ? 0x1000000 - 0x30000
                                         : 0x400000 - 0x7500;
}

bool ARM::inBranchRange(RelType type, uint64_t src, uint64_t dst) const {
  if ((dst & 0x1) == 0)
    // Destination is ARM, if ARM caller then Src is already 4-byte aligned.
    // If Thumb Caller (BLX) the Src address has bottom 2 bits cleared to ensure
    // destination will be 4 byte aligned.
    src &= ~0x3;
  else
    // Bit 0 == 1 denotes Thumb state, it is not part of the range.
    dst &= ~0x1;

  int64_t offset = dst - src;
  switch (type) {
  case R_ARM_PC24:
  case R_ARM_PLT32:
  case R_ARM_JUMP24:
  case R_ARM_CALL:
    return llvm::isInt<26>(offset);
  case R_ARM_THM_JUMP19:
    return llvm::isInt<21>(offset);
  case R_ARM_THM_JUMP24:
  case R_ARM_THM_CALL:
    return config->armJ1J2BranchEncoding ? llvm::isInt<25>(offset)
                                         : llvm::isInt<23>(offset);
  default:
    return true;
  }
}

// Helper to produce message text when LLD detects that a CALL relocation to
// a non STT_FUNC symbol that may result in incorrect interworking between ARM
// or Thumb.
static void stateChangeWarning(uint8_t *loc, RelType relt, const Symbol &s) {
  assert(!s.isFunc());
  const ErrorPlace place = getErrorPlace(loc);
  std::string hint;
  if (!place.srcLoc.empty())
    hint = "; " + place.srcLoc;
  if (s.isSection()) {
    // Section symbols must be defined and in a section. Users cannot change
    // the type. Use the section name as getName() returns an empty string.
    warn(place.loc + "branch and link relocation: " + toString(relt) +
         " to STT_SECTION symbol " + cast<Defined>(s).section->name +
         " ; interworking not performed" + hint);
  } else {
    // Warn with hint on how to alter the symbol type.
    warn(getErrorLocation(loc) + "branch and link relocation: " +
         toString(relt) + " to non STT_FUNC symbol: " + s.getName() +
         " interworking not performed; consider using directive '.type " +
         s.getName() +
         ", %function' to give symbol type STT_FUNC if interworking between "
         "ARM and Thumb is required" +
         hint);
  }
}

// Rotate a 32-bit unsigned value right by a specified amt of bits.
static uint32_t rotr32(uint32_t val, uint32_t amt) {
  assert(amt < 32 && "Invalid rotate amount");
  return (val >> amt) | (val << ((32 - amt) & 31));
}

static std::pair<uint32_t, uint32_t> getRemAndLZForGroup(unsigned group,
                                                         uint32_t val) {
  uint32_t rem, lz;
  do {
    lz = llvm::countl_zero(val) & ~1;
    rem = val;
    if (lz == 32) // implies rem == 0
      break;
    val &= 0xffffff >> lz;
  } while (group--);
  return {rem, lz};
}

static void encodeAluGroup(uint8_t *loc, const Relocation &rel, uint64_t val,
                           int group, bool check) {
  // ADD/SUB (immediate) add = bit23, sub = bit22
  // immediate field carries is a 12-bit modified immediate, made up of a 4-bit
  // even rotate right and an 8-bit immediate.
  uint32_t opcode = 0x00800000;
  if (val >> 63) {
    opcode = 0x00400000;
    val = -val;
  }
  uint32_t imm, lz;
  std::tie(imm, lz) = getRemAndLZForGroup(group, val);
  uint32_t rot = 0;
  if (lz < 24) {
    imm = rotr32(imm, 24 - lz);
    rot = (lz + 8) << 7;
  }
  if (check && imm > 0xff)
    error(getErrorLocation(loc) + "unencodeable immediate " + Twine(val).str() +
          " for relocation " + toString(rel.type));
  write32(loc, (read32(loc) & 0xff3ff000) | opcode | rot | (imm & 0xff));
}

static void encodeLdrGroup(uint8_t *loc, const Relocation &rel, uint64_t val,
                           int group) {
  // R_ARM_LDR_PC_Gn is S + A - P, we have ((S + A) | T) - P, if S is a
  // function then addr is 0 (modulo 2) and Pa is 0 (modulo 4) so we can clear
  // bottom bit to recover S + A - P.
  if (rel.sym->isFunc())
    val &= ~0x1;
  // LDR (literal) u = bit23
  uint32_t opcode = 0x00800000;
  if (val >> 63) {
    opcode = 0x0;
    val = -val;
  }
  uint32_t imm = getRemAndLZForGroup(group, val).first;
  checkUInt(loc, imm, 12, rel);
  write32(loc, (read32(loc) & 0xff7ff000) | opcode | imm);
}

static void encodeLdrsGroup(uint8_t *loc, const Relocation &rel, uint64_t val,
                            int group) {
  // R_ARM_LDRS_PC_Gn is S + A - P, we have ((S + A) | T) - P, if S is a
  // function then addr is 0 (modulo 2) and Pa is 0 (modulo 4) so we can clear
  // bottom bit to recover S + A - P.
  if (rel.sym->isFunc())
    val &= ~0x1;
  // LDRD/LDRH/LDRSB/LDRSH (literal) u = bit23
  uint32_t opcode = 0x00800000;
  if (val >> 63) {
    opcode = 0x0;
    val = -val;
  }
  uint32_t imm = getRemAndLZForGroup(group, val).first;
  checkUInt(loc, imm, 8, rel);
  write32(loc, (read32(loc) & 0xff7ff0f0) | opcode | ((imm & 0xf0) << 4) |
                     (imm & 0xf));
}

void ARM::relocate(uint8_t *loc, const Relocation &rel, uint64_t val) const {
  switch (rel.type) {
  case R_ARM_ABS32:
  case R_ARM_BASE_PREL:
  case R_ARM_GOTOFF32:
  case R_ARM_GOT_BREL:
  case R_ARM_GOT_PREL:
  case R_ARM_REL32:
  case R_ARM_RELATIVE:
  case R_ARM_SBREL32:
  case R_ARM_TARGET1:
  case R_ARM_TARGET2:
  case R_ARM_TLS_GD32:
  case R_ARM_TLS_IE32:
  case R_ARM_TLS_LDM32:
  case R_ARM_TLS_LDO32:
  case R_ARM_TLS_LE32:
  case R_ARM_TLS_TPOFF32:
  case R_ARM_TLS_DTPOFF32:
    write32(loc, val);
    break;
  case R_ARM_PREL31:
    checkInt(loc, val, 31, rel);
    write32(loc, (read32(loc) & 0x80000000) | (val & ~0x80000000));
    break;
  case R_ARM_CALL: {
    // R_ARM_CALL is used for BL and BLX instructions, for symbols of type
    // STT_FUNC we choose whether to write a BL or BLX depending on the
    // value of bit 0 of Val. With bit 0 == 1 denoting Thumb. If the symbol is
    // not of type STT_FUNC then we must preserve the original instruction.
    assert(rel.sym); // R_ARM_CALL is always reached via relocate().
    bool bit0Thumb = val & 1;
    bool isBlx = (read32(loc) & 0xfe000000) == 0xfa000000;
    // lld 10.0 and before always used bit0Thumb when deciding to write a BLX
    // even when type not STT_FUNC.
    if (!rel.sym->isFunc() && isBlx != bit0Thumb)
      stateChangeWarning(loc, rel.type, *rel.sym);
    if (rel.sym->isFunc() ? bit0Thumb : isBlx) {
      // The BLX encoding is 0xfa:H:imm24 where Val = imm24:H:'1'
      checkInt(loc, val, 26, rel);
      write32(loc, 0xfa000000 |                    // opcode
                         ((val & 2) << 23) |         // H
                         ((val >> 2) & 0x00ffffff)); // imm24
      break;
    }
    // BLX (always unconditional) instruction to an ARM Target, select an
    // unconditional BL.
    write32(loc, 0xeb000000 | (read32(loc) & 0x00ffffff));
    // fall through as BL encoding is shared with B
  }
    [[fallthrough]];
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
    checkInt(loc, val, 26, rel);
    write32(loc, (read32(loc) & ~0x00ffffff) | ((val >> 2) & 0x00ffffff));
    break;
  case R_ARM_THM_JUMP8:
    // We do a 9 bit check because val is right-shifted by 1 bit.
    checkInt(loc, val, 9, rel);
    write16(loc, (read32(loc) & 0xff00) | ((val >> 1) & 0x00ff));
    break;
  case R_ARM_THM_JUMP11:
    // We do a 12 bit check because val is right-shifted by 1 bit.
    checkInt(loc, val, 12, rel);
    write16(loc, (read32(loc) & 0xf800) | ((val >> 1) & 0x07ff));
    break;
  case R_ARM_THM_JUMP19:
    // Encoding T3: Val = S:J2:J1:imm6:imm11:0
    checkInt(loc, val, 21, rel);
    write16(loc,
              (read16(loc) & 0xfbc0) |   // opcode cond
                  ((val >> 10) & 0x0400) | // S
                  ((val >> 12) & 0x003f)); // imm6
    write16(loc + 2,
              0x8000 |                    // opcode
                  ((val >> 8) & 0x0800) | // J2
                  ((val >> 5) & 0x2000) | // J1
                  ((val >> 1) & 0x07ff)); // imm11
    break;
  case R_ARM_THM_CALL: {
    // R_ARM_THM_CALL is used for BL and BLX instructions, for symbols of type
    // STT_FUNC we choose whether to write a BL or BLX depending on the
    // value of bit 0 of Val. With bit 0 == 0 denoting ARM, if the symbol is
    // not of type STT_FUNC then we must preserve the original instruction.
    // PLT entries are always ARM state so we know we need to interwork.
    assert(rel.sym); // R_ARM_THM_CALL is always reached via relocate().
    bool bit0Thumb = val & 1;
    bool useThumb = bit0Thumb || useThumbPLTs();
    bool isBlx = (read16(loc + 2) & 0x1000) == 0;
    // lld 10.0 and before always used bit0Thumb when deciding to write a BLX
    // even when type not STT_FUNC.
    if (!rel.sym->isFunc() && !rel.sym->isInPlt() && isBlx == useThumb)
      stateChangeWarning(loc, rel.type, *rel.sym);
    if ((rel.sym->isFunc() || rel.sym->isInPlt()) ? !useThumb : isBlx) {
      // We are writing a BLX. Ensure BLX destination is 4-byte aligned. As
      // the BLX instruction may only be two byte aligned. This must be done
      // before overflow check.
      val = alignTo(val, 4);
      write16(loc + 2, read16(loc + 2) & ~0x1000);
    } else {
      write16(loc + 2, (read16(loc + 2) & ~0x1000) | 1 << 12);
    }
    if (!config->armJ1J2BranchEncoding) {
      // Older Arm architectures do not support R_ARM_THM_JUMP24 and have
      // different encoding rules and range due to J1 and J2 always being 1.
      checkInt(loc, val, 23, rel);
      write16(loc,
                0xf000 |                     // opcode
                    ((val >> 12) & 0x07ff)); // imm11
      write16(loc + 2,
                (read16(loc + 2) & 0xd000) | // opcode
                    0x2800 |                   // J1 == J2 == 1
                    ((val >> 1) & 0x07ff));    // imm11
      break;
    }
  }
    // Fall through as rest of encoding is the same as B.W
    [[fallthrough]];
  case R_ARM_THM_JUMP24:
    // Encoding B  T4, BL T1, BLX T2: Val = S:I1:I2:imm10:imm11:0
    checkInt(loc, val, 25, rel);
    write16(loc,
              0xf000 |                     // opcode
                  ((val >> 14) & 0x0400) | // S
                  ((val >> 12) & 0x03ff)); // imm10
    write16(loc + 2,
              (read16(loc + 2) & 0xd000) |                  // opcode
                  (((~(val >> 10)) ^ (val >> 11)) & 0x2000) | // J1
                  (((~(val >> 11)) ^ (val >> 13)) & 0x0800) | // J2
                  ((val >> 1) & 0x07ff));                     // imm11
    break;
  case R_ARM_MOVW_ABS_NC:
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVW_BREL_NC:
    write32(loc, (read32(loc) & ~0x000f0fff) | ((val & 0xf000) << 4) |
                       (val & 0x0fff));
    break;
  case R_ARM_MOVT_ABS:
  case R_ARM_MOVT_PREL:
  case R_ARM_MOVT_BREL:
    write32(loc, (read32(loc) & ~0x000f0fff) |
                       (((val >> 16) & 0xf000) << 4) | ((val >> 16) & 0xfff));
    break;
  case R_ARM_THM_MOVT_ABS:
  case R_ARM_THM_MOVT_PREL:
  case R_ARM_THM_MOVT_BREL:
    // Encoding T1: A = imm4:i:imm3:imm8

    write16(loc,
            0xf2c0 |                     // opcode
                ((val >> 17) & 0x0400) | // i
                ((val >> 28) & 0x000f)); // imm4

    write16(loc + 2,
              (read16(loc + 2) & 0x8f00) | // opcode
                  ((val >> 12) & 0x7000) |   // imm3
                  ((val >> 16) & 0x00ff));   // imm8
    break;
  case R_ARM_THM_MOVW_ABS_NC:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVW_BREL_NC:
    // Encoding T3: A = imm4:i:imm3:imm8
    write16(loc,
              0xf240 |                     // opcode
                  ((val >> 1) & 0x0400) |  // i
                  ((val >> 12) & 0x000f)); // imm4
    write16(loc + 2,
              (read16(loc + 2) & 0x8f00) | // opcode
                  ((val << 4) & 0x7000) |    // imm3
                  (val & 0x00ff));           // imm8
    break;
  case R_ARM_THM_ALU_ABS_G3:
    write16(loc, (read16(loc) &~ 0x00ff) | ((val >> 24) & 0x00ff));
    break;
  case R_ARM_THM_ALU_ABS_G2_NC:
    write16(loc, (read16(loc) &~ 0x00ff) | ((val >> 16) & 0x00ff));
    break;
  case R_ARM_THM_ALU_ABS_G1_NC:
    write16(loc, (read16(loc) &~ 0x00ff) | ((val >> 8) & 0x00ff));
    break;
  case R_ARM_THM_ALU_ABS_G0_NC:
    write16(loc, (read16(loc) &~ 0x00ff) | (val & 0x00ff));
    break;
  case R_ARM_ALU_PC_G0:
    encodeAluGroup(loc, rel, val, 0, true);
    break;
  case R_ARM_ALU_PC_G0_NC:
    encodeAluGroup(loc, rel, val, 0, false);
    break;
  case R_ARM_ALU_PC_G1:
    encodeAluGroup(loc, rel, val, 1, true);
    break;
  case R_ARM_ALU_PC_G1_NC:
    encodeAluGroup(loc, rel, val, 1, false);
    break;
  case R_ARM_ALU_PC_G2:
    encodeAluGroup(loc, rel, val, 2, true);
    break;
  case R_ARM_LDR_PC_G0:
    encodeLdrGroup(loc, rel, val, 0);
    break;
  case R_ARM_LDR_PC_G1:
    encodeLdrGroup(loc, rel, val, 1);
    break;
  case R_ARM_LDR_PC_G2:
    encodeLdrGroup(loc, rel, val, 2);
    break;
  case R_ARM_LDRS_PC_G0:
    encodeLdrsGroup(loc, rel, val, 0);
    break;
  case R_ARM_LDRS_PC_G1:
    encodeLdrsGroup(loc, rel, val, 1);
    break;
  case R_ARM_LDRS_PC_G2:
    encodeLdrsGroup(loc, rel, val, 2);
    break;
  case R_ARM_THM_ALU_PREL_11_0: {
    // ADR encoding T2 (sub), T3 (add) i:imm3:imm8
    int64_t imm = val;
    uint16_t sub = 0;
    if (imm < 0) {
      imm = -imm;
      sub = 0x00a0;
    }
    checkUInt(loc, imm, 12, rel);
    write16(loc, (read16(loc) & 0xfb0f) | sub | (imm & 0x800) >> 1);
    write16(loc + 2,
              (read16(loc + 2) & 0x8f00) | (imm & 0x700) << 4 | (imm & 0xff));
    break;
  }
  case R_ARM_THM_PC8:
    // ADR and LDR literal encoding T1 positive offset only imm8:00
    // R_ARM_THM_PC8 is S + A - Pa, we have ((S + A) | T) - Pa, if S is a
    // function then addr is 0 (modulo 2) and Pa is 0 (modulo 4) so we can clear
    // bottom bit to recover S + A - Pa.
    if (rel.sym->isFunc())
      val &= ~0x1;
    checkUInt(loc, val, 10, rel);
    checkAlignment(loc, val, 4, rel);
    write16(loc, (read16(loc) & 0xff00) | (val & 0x3fc) >> 2);
    break;
  case R_ARM_THM_PC12: {
    // LDR (literal) encoding T2, add = (U == '1') imm12
    // imm12 is unsigned
    // R_ARM_THM_PC12 is S + A - Pa, we have ((S + A) | T) - Pa, if S is a
    // function then addr is 0 (modulo 2) and Pa is 0 (modulo 4) so we can clear
    // bottom bit to recover S + A - Pa.
    if (rel.sym->isFunc())
      val &= ~0x1;
    int64_t imm12 = val;
    uint16_t u = 0x0080;
    if (imm12 < 0) {
      imm12 = -imm12;
      u = 0;
    }
    checkUInt(loc, imm12, 12, rel);
    write16(loc, read16(loc) | u);
    write16(loc + 2, (read16(loc + 2) & 0xf000) | imm12);
    break;
  }
  default:
    llvm_unreachable("unknown relocation");
  }
}

int64_t ARM::getImplicitAddend(const uint8_t *buf, RelType type) const {
  switch (type) {
  default:
    internalLinkerError(getErrorLocation(buf),
                        "cannot read addend for relocation " + toString(type));
    return 0;
  case R_ARM_ABS32:
  case R_ARM_BASE_PREL:
  case R_ARM_GLOB_DAT:
  case R_ARM_GOTOFF32:
  case R_ARM_GOT_BREL:
  case R_ARM_GOT_PREL:
  case R_ARM_IRELATIVE:
  case R_ARM_REL32:
  case R_ARM_RELATIVE:
  case R_ARM_SBREL32:
  case R_ARM_TARGET1:
  case R_ARM_TARGET2:
  case R_ARM_TLS_DTPMOD32:
  case R_ARM_TLS_DTPOFF32:
  case R_ARM_TLS_GD32:
  case R_ARM_TLS_IE32:
  case R_ARM_TLS_LDM32:
  case R_ARM_TLS_LE32:
  case R_ARM_TLS_LDO32:
  case R_ARM_TLS_TPOFF32:
    return SignExtend64<32>(read32(buf));
  case R_ARM_PREL31:
    return SignExtend64<31>(read32(buf));
  case R_ARM_CALL:
  case R_ARM_JUMP24:
  case R_ARM_PC24:
  case R_ARM_PLT32:
    return SignExtend64<26>(read32(buf) << 2);
  case R_ARM_THM_JUMP8:
    return SignExtend64<9>(read16(buf) << 1);
  case R_ARM_THM_JUMP11:
    return SignExtend64<12>(read16(buf) << 1);
  case R_ARM_THM_JUMP19: {
    // Encoding T3: A = S:J2:J1:imm10:imm6:0
    uint16_t hi = read16(buf);
    uint16_t lo = read16(buf + 2);
    return SignExtend64<20>(((hi & 0x0400) << 10) | // S
                            ((lo & 0x0800) << 8) |  // J2
                            ((lo & 0x2000) << 5) |  // J1
                            ((hi & 0x003f) << 12) | // imm6
                            ((lo & 0x07ff) << 1));  // imm11:0
  }
  case R_ARM_THM_CALL:
    if (!config->armJ1J2BranchEncoding) {
      // Older Arm architectures do not support R_ARM_THM_JUMP24 and have
      // different encoding rules and range due to J1 and J2 always being 1.
      uint16_t hi = read16(buf);
      uint16_t lo = read16(buf + 2);
      return SignExtend64<22>(((hi & 0x7ff) << 12) | // imm11
                              ((lo & 0x7ff) << 1));  // imm11:0
      break;
    }
    [[fallthrough]];
  case R_ARM_THM_JUMP24: {
    // Encoding B T4, BL T1, BLX T2: A = S:I1:I2:imm10:imm11:0
    // I1 = NOT(J1 EOR S), I2 = NOT(J2 EOR S)
    uint16_t hi = read16(buf);
    uint16_t lo = read16(buf + 2);
    return SignExtend64<24>(((hi & 0x0400) << 14) |                    // S
                            (~((lo ^ (hi << 3)) << 10) & 0x00800000) | // I1
                            (~((lo ^ (hi << 1)) << 11) & 0x00400000) | // I2
                            ((hi & 0x003ff) << 12) |                   // imm0
                            ((lo & 0x007ff) << 1)); // imm11:0
  }
  // ELF for the ARM Architecture 4.6.1.1 the implicit addend for MOVW and
  // MOVT is in the range -32768 <= A < 32768
  case R_ARM_MOVW_ABS_NC:
  case R_ARM_MOVT_ABS:
  case R_ARM_MOVW_PREL_NC:
  case R_ARM_MOVT_PREL:
  case R_ARM_MOVW_BREL_NC:
  case R_ARM_MOVT_BREL: {
    uint64_t val = read32(buf) & 0x000f0fff;
    return SignExtend64<16>(((val & 0x000f0000) >> 4) | (val & 0x00fff));
  }
  case R_ARM_THM_MOVW_ABS_NC:
  case R_ARM_THM_MOVT_ABS:
  case R_ARM_THM_MOVW_PREL_NC:
  case R_ARM_THM_MOVT_PREL:
  case R_ARM_THM_MOVW_BREL_NC:
  case R_ARM_THM_MOVT_BREL: {
    // Encoding T3: A = imm4:i:imm3:imm8
    uint16_t hi = read16(buf);
    uint16_t lo = read16(buf + 2);
    return SignExtend64<16>(((hi & 0x000f) << 12) | // imm4
                            ((hi & 0x0400) << 1) |  // i
                            ((lo & 0x7000) >> 4) |  // imm3
                            (lo & 0x00ff));         // imm8
  }
  case R_ARM_THM_ALU_ABS_G0_NC:
  case R_ARM_THM_ALU_ABS_G1_NC:
  case R_ARM_THM_ALU_ABS_G2_NC:
  case R_ARM_THM_ALU_ABS_G3:
    return read16(buf) & 0xff;
  case R_ARM_ALU_PC_G0:
  case R_ARM_ALU_PC_G0_NC:
  case R_ARM_ALU_PC_G1:
  case R_ARM_ALU_PC_G1_NC:
  case R_ARM_ALU_PC_G2: {
    // 12-bit immediate is a modified immediate made up of a 4-bit even
    // right rotation and 8-bit constant. After the rotation the value
    // is zero-extended. When bit 23 is set the instruction is an add, when
    // bit 22 is set it is a sub.
    uint32_t instr = read32(buf);
    uint32_t val = rotr32(instr & 0xff, ((instr & 0xf00) >> 8) * 2);
    return (instr & 0x00400000) ? -val : val;
  }
  case R_ARM_LDR_PC_G0:
  case R_ARM_LDR_PC_G1:
  case R_ARM_LDR_PC_G2: {
    // ADR (literal) add = bit23, sub = bit22
    // LDR (literal) u = bit23 unsigned imm12
    bool u = read32(buf) & 0x00800000;
    uint32_t imm12 = read32(buf) & 0xfff;
    return u ? imm12 : -imm12;
  }
  case R_ARM_LDRS_PC_G0:
  case R_ARM_LDRS_PC_G1:
  case R_ARM_LDRS_PC_G2: {
    // LDRD/LDRH/LDRSB/LDRSH (literal) u = bit23 unsigned imm8
    uint32_t opcode = read32(buf);
    bool u = opcode & 0x00800000;
    uint32_t imm4l = opcode & 0xf;
    uint32_t imm4h = (opcode & 0xf00) >> 4;
    return u ? (imm4h | imm4l) : -(imm4h | imm4l);
  }
  case R_ARM_THM_ALU_PREL_11_0: {
    // Thumb2 ADR, which is an alias for a sub or add instruction with an
    // unsigned immediate.
    // ADR encoding T2 (sub), T3 (add) i:imm3:imm8
    uint16_t hi = read16(buf);
    uint16_t lo = read16(buf + 2);
    uint64_t imm = (hi & 0x0400) << 1 | // i
                   (lo & 0x7000) >> 4 | // imm3
                   (lo & 0x00ff);       // imm8
    // For sub, addend is negative, add is positive.
    return (hi & 0x00f0) ? -imm : imm;
  }
  case R_ARM_THM_PC8:
    // ADR and LDR (literal) encoding T1
    // From ELF for the ARM Architecture the initial signed addend is formed
    // from an unsigned field using expression (((imm8:00 + 4) & 0x3ff) – 4)
    // this trick permits the PC bias of -4 to be encoded using imm8 = 0xff
    return ((((read16(buf) & 0xff) << 2) + 4) & 0x3ff) - 4;
  case R_ARM_THM_PC12: {
    // LDR (literal) encoding T2, add = (U == '1') imm12
    bool u = read16(buf) & 0x0080;
    uint64_t imm12 = read16(buf + 2) & 0x0fff;
    return u ? imm12 : -imm12;
  }
  case R_ARM_NONE:
  case R_ARM_V4BX:
  case R_ARM_JUMP_SLOT:
    // These relocations are defined as not having an implicit addend.
    return 0;
  }
}

static bool isArmMapSymbol(const Symbol *b) {
  return b->getName() == "$a" || b->getName().starts_with("$a.");
}

static bool isThumbMapSymbol(const Symbol *s) {
  return s->getName() == "$t" || s->getName().starts_with("$t.");
}

static bool isDataMapSymbol(const Symbol *b) {
  return b->getName() == "$d" || b->getName().starts_with("$d.");
}

void elf::sortArmMappingSymbols() {
  // For each input section make sure the mapping symbols are sorted in
  // ascending order.
  for (auto &kv : sectionMap) {
    SmallVector<const Defined *, 0> &mapSyms = kv.second;
    llvm::stable_sort(mapSyms, [](const Defined *a, const Defined *b) {
      return a->value < b->value;
    });
  }
}

void elf::addArmInputSectionMappingSymbols() {
  // Collect mapping symbols for every executable input sections.
  // The linker generated mapping symbols for all the synthetic
  // sections are adding into the sectionmap through the function
  // addArmSyntheitcSectionMappingSymbol.
  for (ELFFileBase *file : ctx.objectFiles) {
    for (Symbol *sym : file->getLocalSymbols()) {
      auto *def = dyn_cast<Defined>(sym);
      if (!def)
        continue;
      if (!isArmMapSymbol(def) && !isDataMapSymbol(def) &&
          !isThumbMapSymbol(def))
        continue;
      if (auto *sec = cast_if_present<InputSection>(def->section))
        if (sec->flags & SHF_EXECINSTR)
          sectionMap[sec].push_back(def);
    }
  }
}

// Synthetic sections are not backed by an ELF file where we can access the
// symbol table, instead mapping symbols added to synthetic sections are stored
// in the synthetic symbol table. Due to the presence of strip (--strip-all),
// we can not rely on the synthetic symbol table retaining the mapping symbols.
// Instead we record the mapping symbols locally.
void elf::addArmSyntheticSectionMappingSymbol(Defined *sym) {
  if (!isArmMapSymbol(sym) && !isDataMapSymbol(sym) && !isThumbMapSymbol(sym))
    return;
  if (auto *sec = cast_if_present<InputSection>(sym->section))
    if (sec->flags & SHF_EXECINSTR)
      sectionMap[sec].push_back(sym);
}

static void toLittleEndianInstructions(uint8_t *buf, uint64_t start,
                                       uint64_t end, uint64_t width) {
  CodeState curState = static_cast<CodeState>(width);
  if (curState == CodeState::Arm)
    for (uint64_t i = start; i < end; i += width)
      write32le(buf + i, read32(buf + i));

  if (curState == CodeState::Thumb)
    for (uint64_t i = start; i < end; i += width)
      write16le(buf + i, read16(buf + i));
}

// Arm BE8 big endian format requires instructions to be little endian, with
// the initial contents big-endian. Convert the big-endian instructions to
// little endian leaving literal data untouched. We use mapping symbols to
// identify half open intervals of Arm code [$a, non $a) and Thumb code
// [$t, non $t) and convert these to little endian a word or half word at a
// time respectively.
void elf::convertArmInstructionstoBE8(InputSection *sec, uint8_t *buf) {
  if (!sectionMap.contains(sec))
    return;

  SmallVector<const Defined *, 0> &mapSyms = sectionMap[sec];

  if (mapSyms.empty())
    return;

  CodeState curState = CodeState::Data;
  uint64_t start = 0, width = 0, size = sec->getSize();
  for (auto &msym : mapSyms) {
    CodeState newState = CodeState::Data;
    if (isThumbMapSymbol(msym))
      newState = CodeState::Thumb;
    else if (isArmMapSymbol(msym))
      newState = CodeState::Arm;

    if (newState == curState)
      continue;

    if (curState != CodeState::Data) {
      width = static_cast<uint64_t>(curState);
      toLittleEndianInstructions(buf, start, msym->value, width);
    }
    start = msym->value;
    curState = newState;
  }

  // Passed last mapping symbol, may need to reverse
  // up to end of section.
  if (curState != CodeState::Data) {
    width = static_cast<uint64_t>(curState);
    toLittleEndianInstructions(buf, start, size, width);
  }
}

// The Arm Cortex-M Security Extensions (CMSE) splits a system into two parts;
// the non-secure and secure states with the secure state inaccessible from the
// non-secure state, apart from an area of memory in secure state called the
// secure gateway which is accessible from non-secure state. The secure gateway
// contains one or more entry points which must start with a landing pad
// instruction SG. Arm recommends that the secure gateway consists only of
// secure gateway veneers, which are made up of a SG instruction followed by a
// branch to the destination in secure state. Full details can be found in Arm
// v8-M Security Extensions Requirements on Development Tools.
//
// The CMSE model of software development requires the non-secure and secure
// states to be developed as two separate programs. The non-secure developer is
// provided with an import library defining symbols describing the entry points
// in the secure gateway. No additional linker support is required for the
// non-secure state.
//
// Development of the secure state requires linker support to manage the secure
// gateway veneers. The management consists of:
// - Creation of new secure gateway veneers based on symbol conventions.
// - Checking the address of existing secure gateway veneers.
// - Warning when existing secure gateway veneers removed.
//
// The secure gateway veneers are created in an import library, which is just an
// ELF object with a symbol table. The import library is controlled by two
// command line options:
// --in-implib (specify an input import library from a previous revision of the
// program).
// --out-implib (specify an output import library to be created by the linker).
//
// The input import library is used to manage consistency of the secure entry
// points. The output import library is for new and updated secure entry points.
//
// The symbol convention that identifies secure entry functions is the prefix
// __acle_se_ for a symbol called name the linker is expected to create a secure
// gateway veneer if symbols __acle_se_name and name have the same address.
// After creating a secure gateway veneer the symbol name labels the secure
// gateway veneer and the __acle_se_name labels the function definition.
//
// The LLD implementation:
// - Reads an existing import library with importCmseSymbols().
// - Determines which new secure gateway veneers to create and redirects calls
//   within the secure state to the __acle_se_ prefixed symbol with
//   processArmCmseSymbols().
// - Models the SG veneers as a synthetic section.

// Initialize symbols. symbols is a parallel array to the corresponding ELF
// symbol table.
template <class ELFT> void ObjFile<ELFT>::importCmseSymbols() {
  ArrayRef<Elf_Sym> eSyms = getELFSyms<ELFT>();
  // Error for local symbols. The symbol at index 0 is LOCAL. So skip it.
  for (size_t i = 1, end = firstGlobal; i != end; ++i) {
    errorOrWarn("CMSE symbol '" + CHECK(eSyms[i].getName(stringTable), this) +
                "' in import library '" + toString(this) + "' is not global");
  }

  for (size_t i = firstGlobal, end = eSyms.size(); i != end; ++i) {
    const Elf_Sym &eSym = eSyms[i];
    Defined *sym = reinterpret_cast<Defined *>(make<SymbolUnion>());

    // Initialize symbol fields.
    memset(sym, 0, sizeof(Symbol));
    sym->setName(CHECK(eSyms[i].getName(stringTable), this));
    sym->value = eSym.st_value;
    sym->size = eSym.st_size;
    sym->type = eSym.getType();
    sym->binding = eSym.getBinding();
    sym->stOther = eSym.st_other;

    if (eSym.st_shndx != SHN_ABS) {
      error("CMSE symbol '" + sym->getName() + "' in import library '" +
            toString(this) + "' is not absolute");
      continue;
    }

    if (!(eSym.st_value & 1) || (eSym.getType() != STT_FUNC)) {
      error("CMSE symbol '" + sym->getName() + "' in import library '" +
            toString(this) + "' is not a Thumb function definition");
      continue;
    }

    if (symtab.cmseImportLib.count(sym->getName())) {
      error("CMSE symbol '" + sym->getName() +
            "' is multiply defined in import library '" + toString(this) + "'");
      continue;
    }

    if (eSym.st_size != ACLESESYM_SIZE) {
      warn("CMSE symbol '" + sym->getName() + "' in import library '" +
           toString(this) + "' does not have correct size of " +
           Twine(ACLESESYM_SIZE) + " bytes");
    }

    symtab.cmseImportLib[sym->getName()] = sym;
  }
}

// Check symbol attributes of the acleSeSym, sym pair.
// Both symbols should be global/weak Thumb code symbol definitions.
static std::string checkCmseSymAttributes(Symbol *acleSeSym, Symbol *sym) {
  auto check = [](Symbol *s, StringRef type) -> std::optional<std::string> {
    auto d = dyn_cast_or_null<Defined>(s);
    if (!(d && d->isFunc() && (d->value & 1)))
      return (Twine(toString(s->file)) + ": cmse " + type + " symbol '" +
              s->getName() + "' is not a Thumb function definition")
          .str();
    if (!d->section)
      return (Twine(toString(s->file)) + ": cmse " + type + " symbol '" +
              s->getName() + "' cannot be an absolute symbol")
          .str();
    return std::nullopt;
  };
  for (auto [sym, type] :
       {std::make_pair(acleSeSym, "special"), std::make_pair(sym, "entry")})
    if (auto err = check(sym, type))
      return *err;
  return "";
}

// Look for [__acle_se_<sym>, <sym>] pairs, as specified in the Cortex-M
// Security Extensions specification.
// 1) <sym> : A standard function name.
// 2) __acle_se_<sym> : A special symbol that prefixes the standard function
// name with __acle_se_.
// Both these symbols are Thumb function symbols with external linkage.
// <sym> may be redefined in .gnu.sgstubs.
void elf::processArmCmseSymbols() {
  if (!config->cmseImplib)
    return;
  // Only symbols with external linkage end up in symtab, so no need to do
  // linkage checks. Only check symbol type.
  for (Symbol *acleSeSym : symtab.getSymbols()) {
    if (!acleSeSym->getName().starts_with(ACLESESYM_PREFIX))
      continue;
    // If input object build attributes do not support CMSE, error and disable
    // further scanning for <sym>, __acle_se_<sym> pairs.
    if (!config->armCMSESupport) {
      error("CMSE is only supported by ARMv8-M architecture or later");
      config->cmseImplib = false;
      break;
    }

    // Try to find the associated symbol definition.
    // Symbol must have external linkage.
    StringRef name = acleSeSym->getName().substr(std::strlen(ACLESESYM_PREFIX));
    Symbol *sym = symtab.find(name);
    if (!sym) {
      error(toString(acleSeSym->file) + ": cmse special symbol '" +
            acleSeSym->getName() +
            "' detected, but no associated entry function definition '" + name +
            "' with external linkage found");
      continue;
    }

    std::string errMsg = checkCmseSymAttributes(acleSeSym, sym);
    if (!errMsg.empty()) {
      error(errMsg);
      continue;
    }

    // <sym> may be redefined later in the link in .gnu.sgstubs
    symtab.cmseSymMap[name] = {acleSeSym, sym};
  }

  // If this is an Arm CMSE secure app, replace references to entry symbol <sym>
  // with its corresponding special symbol __acle_se_<sym>.
  parallelForEach(ctx.objectFiles, [&](InputFile *file) {
    MutableArrayRef<Symbol *> syms = file->getMutableSymbols();
    for (size_t i = 0, e = syms.size(); i != e; ++i) {
      StringRef symName = syms[i]->getName();
      if (symtab.cmseSymMap.count(symName))
        syms[i] = symtab.cmseSymMap[symName].acleSeSym;
    }
  });
}

class elf::ArmCmseSGVeneer {
public:
  ArmCmseSGVeneer(Symbol *sym, Symbol *acleSeSym,
                  std::optional<uint64_t> addr = std::nullopt)
      : sym(sym), acleSeSym(acleSeSym), entAddr{addr} {}
  static const size_t size{ACLESESYM_SIZE};
  const std::optional<uint64_t> getAddr() const { return entAddr; };

  Symbol *sym;
  Symbol *acleSeSym;
  uint64_t offset = 0;

private:
  const std::optional<uint64_t> entAddr;
};

ArmCmseSGSection::ArmCmseSGSection()
    : SyntheticSection(llvm::ELF::SHF_ALLOC | llvm::ELF::SHF_EXECINSTR,
                       llvm::ELF::SHT_PROGBITS,
                       /*alignment=*/32, ".gnu.sgstubs") {
  entsize = ACLESESYM_SIZE;
  // The range of addresses used in the CMSE import library should be fixed.
  for (auto &[_, sym] : symtab.cmseImportLib) {
    if (impLibMaxAddr <= sym->value)
      impLibMaxAddr = sym->value + sym->size;
  }
  if (symtab.cmseSymMap.empty())
    return;
  addMappingSymbol();
  for (auto &[_, entryFunc] : symtab.cmseSymMap)
    addSGVeneer(cast<Defined>(entryFunc.acleSeSym),
                cast<Defined>(entryFunc.sym));
  for (auto &[_, sym] : symtab.cmseImportLib) {
    if (!symtab.inCMSEOutImpLib.count(sym->getName()))
      warn("entry function '" + sym->getName() +
           "' from CMSE import library is not present in secure application");
  }

  if (!symtab.cmseImportLib.empty() && config->cmseOutputLib.empty()) {
    for (auto &[_, entryFunc] : symtab.cmseSymMap) {
      Symbol *sym = entryFunc.sym;
      if (!symtab.inCMSEOutImpLib.count(sym->getName()))
        warn("new entry function '" + sym->getName() +
             "' introduced but no output import library specified");
    }
  }
}

void ArmCmseSGSection::addSGVeneer(Symbol *acleSeSym, Symbol *sym) {
  entries.emplace_back(acleSeSym, sym);
  if (symtab.cmseImportLib.count(sym->getName()))
    symtab.inCMSEOutImpLib[sym->getName()] = true;
  // Symbol addresses different, nothing to do.
  if (acleSeSym->file != sym->file ||
      cast<Defined>(*acleSeSym).value != cast<Defined>(*sym).value)
    return;
  // Only secure symbols with values equal to that of it's non-secure
  // counterpart needs to be in the .gnu.sgstubs section.
  ArmCmseSGVeneer *ss = nullptr;
  if (symtab.cmseImportLib.count(sym->getName())) {
    Defined *impSym = symtab.cmseImportLib[sym->getName()];
    ss = make<ArmCmseSGVeneer>(sym, acleSeSym, impSym->value);
  } else {
    ss = make<ArmCmseSGVeneer>(sym, acleSeSym);
    ++newEntries;
  }
  sgVeneers.emplace_back(ss);
}

void ArmCmseSGSection::writeTo(uint8_t *buf) {
  for (ArmCmseSGVeneer *s : sgVeneers) {
    uint8_t *p = buf + s->offset;
    write16(p + 0, 0xe97f); // SG
    write16(p + 2, 0xe97f);
    write16(p + 4, 0xf000); // B.W S
    write16(p + 6, 0xb000);
    target->relocateNoSym(p + 4, R_ARM_THM_JUMP24,
                          s->acleSeSym->getVA() -
                              (getVA() + s->offset + s->size));
  }
}

void ArmCmseSGSection::addMappingSymbol() {
  addSyntheticLocal("$t", STT_NOTYPE, /*off=*/0, /*size=*/0, *this);
}

size_t ArmCmseSGSection::getSize() const {
  if (sgVeneers.empty())
    return (impLibMaxAddr ? impLibMaxAddr - getVA() : 0) + newEntries * entsize;

  return entries.size() * entsize;
}

void ArmCmseSGSection::finalizeContents() {
  if (sgVeneers.empty())
    return;

  auto it =
      std::stable_partition(sgVeneers.begin(), sgVeneers.end(),
                            [](auto *i) { return i->getAddr().has_value(); });
  std::sort(sgVeneers.begin(), it, [](auto *a, auto *b) {
    return a->getAddr().value() < b->getAddr().value();
  });
  // This is the partition of the veneers with fixed addresses.
  uint64_t addr = (*sgVeneers.begin())->getAddr().has_value()
                      ? (*sgVeneers.begin())->getAddr().value()
                      : getVA();
  // Check if the start address of '.gnu.sgstubs' correspond to the
  // linker-synthesized veneer with the lowest address.
  if ((getVA() & ~1) != (addr & ~1)) {
    error("start address of '.gnu.sgstubs' is different from previous link");
    return;
  }

  for (size_t i = 0; i < sgVeneers.size(); ++i) {
    ArmCmseSGVeneer *s = sgVeneers[i];
    s->offset = i * s->size;
    Defined(file, StringRef(), s->sym->binding, s->sym->stOther, s->sym->type,
            s->offset | 1, s->size, this)
        .overwrite(*s->sym);
  }
}

// Write the CMSE import library to disk.
// The CMSE import library is a relocatable object with only a symbol table.
// The symbols are copies of the (absolute) symbols of the secure gateways
// in the executable output by this link.
// See Arm® v8-M Security Extensions: Requirements on Development Tools
// https://developer.arm.com/documentation/ecm0359818/latest
template <typename ELFT> void elf::writeARMCmseImportLib() {
  StringTableSection *shstrtab =
      make<StringTableSection>(".shstrtab", /*dynamic=*/false);
  StringTableSection *strtab =
      make<StringTableSection>(".strtab", /*dynamic=*/false);
  SymbolTableBaseSection *impSymTab = make<SymbolTableSection<ELFT>>(*strtab);

  SmallVector<std::pair<OutputSection *, SyntheticSection *>, 0> osIsPairs;
  osIsPairs.emplace_back(make<OutputSection>(strtab->name, 0, 0), strtab);
  osIsPairs.emplace_back(make<OutputSection>(impSymTab->name, 0, 0), impSymTab);
  osIsPairs.emplace_back(make<OutputSection>(shstrtab->name, 0, 0), shstrtab);

  std::sort(symtab.cmseSymMap.begin(), symtab.cmseSymMap.end(),
            [](const auto &a, const auto &b) -> bool {
              return a.second.sym->getVA() < b.second.sym->getVA();
            });
  // Copy the secure gateway entry symbols to the import library symbol table.
  for (auto &p : symtab.cmseSymMap) {
    Defined *d = cast<Defined>(p.second.sym);
    impSymTab->addSymbol(makeDefined(
        ctx.internalFile, d->getName(), d->computeBinding(),
        /*stOther=*/0, STT_FUNC, d->getVA(), d->getSize(), nullptr));
  }

  size_t idx = 0;
  uint64_t off = sizeof(typename ELFT::Ehdr);
  for (auto &[osec, isec] : osIsPairs) {
    osec->sectionIndex = ++idx;
    osec->recordSection(isec);
    osec->finalizeInputSections();
    osec->shName = shstrtab->addString(osec->name);
    osec->size = isec->getSize();
    isec->finalizeContents();
    osec->offset = alignToPowerOf2(off, osec->addralign);
    off = osec->offset + osec->size;
  }

  const uint64_t sectionHeaderOff = alignToPowerOf2(off, config->wordsize);
  const auto shnum = osIsPairs.size() + 1;
  const uint64_t fileSize =
      sectionHeaderOff + shnum * sizeof(typename ELFT::Shdr);
  const unsigned flags =
      config->mmapOutputFile ? 0 : (unsigned)FileOutputBuffer::F_no_mmap;
  unlinkAsync(config->cmseOutputLib);
  Expected<std::unique_ptr<FileOutputBuffer>> bufferOrErr =
      FileOutputBuffer::create(config->cmseOutputLib, fileSize, flags);
  if (!bufferOrErr) {
    error("failed to open " + config->cmseOutputLib + ": " +
          llvm::toString(bufferOrErr.takeError()));
    return;
  }

  // Write the ELF Header
  std::unique_ptr<FileOutputBuffer> &buffer = *bufferOrErr;
  uint8_t *const buf = buffer->getBufferStart();
  memcpy(buf, "\177ELF", 4);
  auto *eHdr = reinterpret_cast<typename ELFT::Ehdr *>(buf);
  eHdr->e_type = ET_REL;
  eHdr->e_entry = 0;
  eHdr->e_shoff = sectionHeaderOff;
  eHdr->e_ident[EI_CLASS] = ELFCLASS32;
  eHdr->e_ident[EI_DATA] = config->isLE ? ELFDATA2LSB : ELFDATA2MSB;
  eHdr->e_ident[EI_VERSION] = EV_CURRENT;
  eHdr->e_ident[EI_OSABI] = config->osabi;
  eHdr->e_ident[EI_ABIVERSION] = 0;
  eHdr->e_machine = EM_ARM;
  eHdr->e_version = EV_CURRENT;
  eHdr->e_flags = config->eflags;
  eHdr->e_ehsize = sizeof(typename ELFT::Ehdr);
  eHdr->e_phnum = 0;
  eHdr->e_shentsize = sizeof(typename ELFT::Shdr);
  eHdr->e_phoff = 0;
  eHdr->e_phentsize = 0;
  eHdr->e_shnum = shnum;
  eHdr->e_shstrndx = shstrtab->getParent()->sectionIndex;

  // Write the section header table.
  auto *sHdrs = reinterpret_cast<typename ELFT::Shdr *>(buf + eHdr->e_shoff);
  for (auto &[osec, _] : osIsPairs)
    osec->template writeHeaderTo<ELFT>(++sHdrs);

  // Write section contents to a mmap'ed file.
  {
    parallel::TaskGroup tg;
    for (auto &[osec, _] : osIsPairs)
      osec->template writeTo<ELFT>(buf + osec->offset, tg);
  }

  if (auto e = buffer->commit())
    fatal("failed to write output '" + buffer->getPath() +
          "': " + toString(std::move(e)));
}

TargetInfo *elf::getARMTargetInfo() {
  static ARM target;
  return &target;
}

template void elf::writeARMCmseImportLib<ELF32LE>();
template void elf::writeARMCmseImportLib<ELF32BE>();
template void elf::writeARMCmseImportLib<ELF64LE>();
template void elf::writeARMCmseImportLib<ELF64BE>();

template void ObjFile<ELF32LE>::importCmseSymbols();
template void ObjFile<ELF32BE>::importCmseSymbols();
template void ObjFile<ELF64LE>::importCmseSymbols();
template void ObjFile<ELF64BE>::importCmseSymbols();
