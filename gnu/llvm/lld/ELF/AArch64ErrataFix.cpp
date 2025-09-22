//===- AArch64ErrataFix.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file implements Section Patching for the purpose of working around
// the AArch64 Cortex-53 errata 843419 that affects r0p0, r0p1, r0p2 and r0p4
// versions of the core.
//
// The general principle is that an erratum sequence of one or
// more instructions is detected in the instruction stream, one of the
// instructions in the sequence is replaced with a branch to a patch sequence
// of replacement instructions. At the end of the replacement sequence the
// patch branches back to the instruction stream.

// This technique is only suitable for fixing an erratum when:
// - There is a set of necessary conditions required to trigger the erratum that
// can be detected at static link time.
// - There is a set of replacement instructions that can be used to remove at
// least one of the necessary conditions that trigger the erratum.
// - We can overwrite an instruction in the erratum sequence with a branch to
// the replacement sequence.
// - We can place the replacement sequence within range of the branch.
//===----------------------------------------------------------------------===//

#include "AArch64ErrataFix.h"
#include "InputFiles.h"
#include "LinkerScript.h"
#include "OutputSections.h"
#include "Relocations.h"
#include "Symbols.h"
#include "SyntheticSections.h"
#include "Target.h"
#include "lld/Common/CommonLinkerContext.h"
#include "lld/Common/Strings.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Endian.h"
#include <algorithm>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::object;
using namespace llvm::support;
using namespace llvm::support::endian;
using namespace lld;
using namespace lld::elf;

// Helper functions to identify instructions and conditions needed to trigger
// the Cortex-A53-843419 erratum.

// ADRP
// | 1 | immlo (2) | 1 | 0 0 0 0 | immhi (19) | Rd (5) |
static bool isADRP(uint32_t instr) {
  return (instr & 0x9f000000) == 0x90000000;
}

// Load and store bit patterns from ARMv8-A.
// Instructions appear in order of appearance starting from table in
// C4.1.3 Loads and Stores.

// All loads and stores have 1 (at bit position 27), (0 at bit position 25).
// | op0 x op1 (2) | 1 op2 0 op3 (2) | x | op4 (5) | xxxx | op5 (2) | x (10) |
static bool isLoadStoreClass(uint32_t instr) {
  return (instr & 0x0a000000) == 0x08000000;
}

// LDN/STN multiple no offset
// | 0 Q 00 | 1100 | 0 L 00 | 0000 | opcode (4) | size (2) | Rn (5) | Rt (5) |
// LDN/STN multiple post-indexed
// | 0 Q 00 | 1100 | 1 L 0 | Rm (5)| opcode (4) | size (2) | Rn (5) | Rt (5) |
// L == 0 for stores.

// Utility routine to decode opcode field of LDN/STN multiple structure
// instructions to find the ST1 instructions.
// opcode == 0010 ST1 4 registers.
// opcode == 0110 ST1 3 registers.
// opcode == 0111 ST1 1 register.
// opcode == 1010 ST1 2 registers.
static bool isST1MultipleOpcode(uint32_t instr) {
  return (instr & 0x0000f000) == 0x00002000 ||
         (instr & 0x0000f000) == 0x00006000 ||
         (instr & 0x0000f000) == 0x00007000 ||
         (instr & 0x0000f000) == 0x0000a000;
}

static bool isST1Multiple(uint32_t instr) {
  return (instr & 0xbfff0000) == 0x0c000000 && isST1MultipleOpcode(instr);
}

// Writes to Rn (writeback).
static bool isST1MultiplePost(uint32_t instr) {
  return (instr & 0xbfe00000) == 0x0c800000 && isST1MultipleOpcode(instr);
}

// LDN/STN single no offset
// | 0 Q 00 | 1101 | 0 L R 0 | 0000 | opc (3) S | size (2) | Rn (5) | Rt (5)|
// LDN/STN single post-indexed
// | 0 Q 00 | 1101 | 1 L R | Rm (5) | opc (3) S | size (2) | Rn (5) | Rt (5)|
// L == 0 for stores

// Utility routine to decode opcode field of LDN/STN single structure
// instructions to find the ST1 instructions.
// R == 0 for ST1 and ST3, R == 1 for ST2 and ST4.
// opcode == 000 ST1 8-bit.
// opcode == 010 ST1 16-bit.
// opcode == 100 ST1 32 or 64-bit (Size determines which).
static bool isST1SingleOpcode(uint32_t instr) {
  return (instr & 0x0040e000) == 0x00000000 ||
         (instr & 0x0040e000) == 0x00004000 ||
         (instr & 0x0040e000) == 0x00008000;
}

static bool isST1Single(uint32_t instr) {
  return (instr & 0xbfff0000) == 0x0d000000 && isST1SingleOpcode(instr);
}

// Writes to Rn (writeback).
static bool isST1SinglePost(uint32_t instr) {
  return (instr & 0xbfe00000) == 0x0d800000 && isST1SingleOpcode(instr);
}

static bool isST1(uint32_t instr) {
  return isST1Multiple(instr) || isST1MultiplePost(instr) ||
         isST1Single(instr) || isST1SinglePost(instr);
}

// Load/store exclusive
// | size (2) 00 | 1000 | o2 L o1 | Rs (5) | o0 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for Stores.
static bool isLoadStoreExclusive(uint32_t instr) {
  return (instr & 0x3f000000) == 0x08000000;
}

static bool isLoadExclusive(uint32_t instr) {
  return (instr & 0x3f400000) == 0x08400000;
}

// Load register literal
// | opc (2) 01 | 1 V 00 | imm19 | Rt (5) |
static bool isLoadLiteral(uint32_t instr) {
  return (instr & 0x3b000000) == 0x18000000;
}

// Load/store no-allocate pair
// (offset)
// | opc (2) 10 | 1 V 00 | 0 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for stores.
// Never writes to register
static bool isSTNP(uint32_t instr) {
  return (instr & 0x3bc00000) == 0x28000000;
}

// Load/store register pair
// (post-indexed)
// | opc (2) 10 | 1 V 00 | 1 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// L == 0 for stores, V == 0 for Scalar, V == 1 for Simd/FP
// Writes to Rn.
static bool isSTPPost(uint32_t instr) {
  return (instr & 0x3bc00000) == 0x28800000;
}

// (offset)
// | opc (2) 10 | 1 V 01 | 0 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
static bool isSTPOffset(uint32_t instr) {
  return (instr & 0x3bc00000) == 0x29000000;
}

// (pre-index)
// | opc (2) 10 | 1 V 01 | 1 L | imm7 | Rt2 (5) | Rn (5) | Rt (5) |
// Writes to Rn.
static bool isSTPPre(uint32_t instr) {
  return (instr & 0x3bc00000) == 0x29800000;
}

static bool isSTP(uint32_t instr) {
  return isSTPPost(instr) || isSTPOffset(instr) || isSTPPre(instr);
}

// Load/store register (unscaled immediate)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 00 | Rn (5) | Rt (5) |
// V == 0 for Scalar, V == 1 for Simd/FP.
static bool isLoadStoreUnscaled(uint32_t instr) {
  return (instr & 0x3b000c00) == 0x38000000;
}

// Load/store register (immediate post-indexed)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 01 | Rn (5) | Rt (5) |
static bool isLoadStoreImmediatePost(uint32_t instr) {
  return (instr & 0x3b200c00) == 0x38000400;
}

// Load/store register (unprivileged)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 10 | Rn (5) | Rt (5) |
static bool isLoadStoreUnpriv(uint32_t instr) {
  return (instr & 0x3b200c00) == 0x38000800;
}

// Load/store register (immediate pre-indexed)
// | size (2) 11 | 1 V 00 | opc (2) 0 | imm9 | 11 | Rn (5) | Rt (5) |
static bool isLoadStoreImmediatePre(uint32_t instr) {
  return (instr & 0x3b200c00) == 0x38000c00;
}

// Load/store register (register offset)
// | size (2) 11 | 1 V 00 | opc (2) 1 | Rm (5) | option (3) S | 10 | Rn | Rt |
static bool isLoadStoreRegisterOff(uint32_t instr) {
  return (instr & 0x3b200c00) == 0x38200800;
}

// Load/store register (unsigned immediate)
// | size (2) 11 | 1 V 01 | opc (2) | imm12 | Rn (5) | Rt (5) |
static bool isLoadStoreRegisterUnsigned(uint32_t instr) {
  return (instr & 0x3b000000) == 0x39000000;
}

// Rt is always in bit position 0 - 4.
static uint32_t getRt(uint32_t instr) { return (instr & 0x1f); }

// Rn is always in bit position 5 - 9.
static uint32_t getRn(uint32_t instr) { return (instr >> 5) & 0x1f; }

// C4.1.2 Branches, Exception Generating and System instructions
// | op0 (3) 1 | 01 op1 (4) | x (22) |
// op0 == 010 101 op1 == 0xxx Conditional Branch.
// op0 == 110 101 op1 == 1xxx Unconditional Branch Register.
// op0 == x00 101 op1 == xxxx Unconditional Branch immediate.
// op0 == x01 101 op1 == 0xxx Compare and branch immediate.
// op0 == x01 101 op1 == 1xxx Test and branch immediate.
static bool isBranch(uint32_t instr) {
  return ((instr & 0xfe000000) == 0xd6000000) || // Cond branch.
         ((instr & 0xfe000000) == 0x54000000) || // Uncond branch reg.
         ((instr & 0x7c000000) == 0x14000000) || // Uncond branch imm.
         ((instr & 0x7c000000) == 0x34000000);   // Compare and test branch.
}

static bool isV8SingleRegisterNonStructureLoadStore(uint32_t instr) {
  return isLoadStoreUnscaled(instr) || isLoadStoreImmediatePost(instr) ||
         isLoadStoreUnpriv(instr) || isLoadStoreImmediatePre(instr) ||
         isLoadStoreRegisterOff(instr) || isLoadStoreRegisterUnsigned(instr);
}

// Note that this function refers to v8.0 only and does not include the
// additional load and store instructions added for in later revisions of
// the architecture such as the Atomic memory operations introduced
// in v8.1.
static bool isV8NonStructureLoad(uint32_t instr) {
  if (isLoadExclusive(instr))
    return true;
  if (isLoadLiteral(instr))
    return true;
  else if (isV8SingleRegisterNonStructureLoadStore(instr)) {
    // For Load and Store single register, Loads are derived from a
    // combination of the Size, V and Opc fields.
    uint32_t size = (instr >> 30) & 0xff;
    uint32_t v = (instr >> 26) & 0x1;
    uint32_t opc = (instr >> 22) & 0x3;
    // For the load and store instructions that we are decoding.
    // Opc == 0 are all stores.
    // Opc == 1 with a couple of exceptions are loads. The exceptions are:
    // Size == 00 (0), V == 1, Opc == 10 (2) which is a store and
    // Size == 11 (3), V == 0, Opc == 10 (2) which is a prefetch.
    return opc != 0 && !(size == 0 && v == 1 && opc == 2) &&
           !(size == 3 && v == 0 && opc == 2);
  }
  return false;
}

// The following decode instructions are only complete up to the instructions
// needed for errata 843419.

// Instruction with writeback updates the index register after the load/store.
static bool hasWriteback(uint32_t instr) {
  return isLoadStoreImmediatePre(instr) || isLoadStoreImmediatePost(instr) ||
         isSTPPre(instr) || isSTPPost(instr) || isST1SinglePost(instr) ||
         isST1MultiplePost(instr);
}

// For the load and store class of instructions, a load can write to the
// destination register, a load and a store can write to the base register when
// the instruction has writeback.
static bool doesLoadStoreWriteToReg(uint32_t instr, uint32_t reg) {
  return (isV8NonStructureLoad(instr) && getRt(instr) == reg) ||
         (hasWriteback(instr) && getRn(instr) == reg);
}

// Scanner for Cortex-A53 errata 843419
// Full details are available in the Cortex A53 MPCore revision 0 Software
// Developers Errata Notice (ARM-EPM-048406).
//
// The instruction sequence that triggers the erratum is common in compiled
// AArch64 code, however it is sensitive to the offset of the sequence within
// a 4k page. This means that by scanning and fixing the patch after we have
// assigned addresses we only need to disassemble and fix instances of the
// sequence in the range of affected offsets.
//
// In summary the erratum conditions are a series of 4 instructions:
// 1.) An ADRP instruction that writes to register Rn with low 12 bits of
//     address of instruction either 0xff8 or 0xffc.
// 2.) A load or store instruction that can be:
// - A single register load or store, of either integer or vector registers.
// - An STP or STNP, of either integer or vector registers.
// - An Advanced SIMD ST1 store instruction.
// - Must not write to Rn, but may optionally read from it.
// 3.) An optional instruction that is not a branch and does not write to Rn.
// 4.) A load or store from the  Load/store register (unsigned immediate) class
//     that uses Rn as the base address register.
//
// Note that we do not attempt to scan for Sequence 2 as described in the
// Software Developers Errata Notice as this has been assessed to be extremely
// unlikely to occur in compiled code. This matches gold and ld.bfd behavior.

// Return true if the Instruction sequence Adrp, Instr2, and Instr4 match
// the erratum sequence. The Adrp, Instr2 and Instr4 correspond to 1.), 2.),
// and 4.) in the Scanner for Cortex-A53 errata comment above.
static bool is843419ErratumSequence(uint32_t instr1, uint32_t instr2,
                                    uint32_t instr4) {
  if (!isADRP(instr1))
    return false;

  uint32_t rn = getRt(instr1);
  return isLoadStoreClass(instr2) &&
         (isLoadStoreExclusive(instr2) || isLoadLiteral(instr2) ||
          isV8SingleRegisterNonStructureLoadStore(instr2) || isSTP(instr2) ||
          isSTNP(instr2) || isST1(instr2)) &&
         !doesLoadStoreWriteToReg(instr2, rn) &&
         isLoadStoreRegisterUnsigned(instr4) && getRn(instr4) == rn;
}

// Scan the instruction sequence starting at Offset Off from the base of
// InputSection isec. We update Off in this function rather than in the caller
// as we can skip ahead much further into the section when we know how many
// instructions we've scanned.
// Return the offset of the load or store instruction in isec that we want to
// patch or 0 if no patch required.
static uint64_t scanCortexA53Errata843419(InputSection *isec, uint64_t &off,
                                          uint64_t limit) {
  uint64_t isecAddr = isec->getVA(0);

  // Advance Off so that (isecAddr + Off) modulo 0x1000 is at least 0xff8.
  uint64_t initialPageOff = (isecAddr + off) & 0xfff;
  if (initialPageOff < 0xff8)
    off += 0xff8 - initialPageOff;

  bool optionalAllowed = limit - off > 12;
  if (off >= limit || limit - off < 12) {
    // Need at least 3 4-byte sized instructions to trigger erratum.
    off = limit;
    return 0;
  }

  uint64_t patchOff = 0;
  const uint8_t *buf = isec->content().begin();
  const ulittle32_t *instBuf = reinterpret_cast<const ulittle32_t *>(buf + off);
  uint32_t instr1 = *instBuf++;
  uint32_t instr2 = *instBuf++;
  uint32_t instr3 = *instBuf++;
  if (is843419ErratumSequence(instr1, instr2, instr3)) {
    patchOff = off + 8;
  } else if (optionalAllowed && !isBranch(instr3)) {
    uint32_t instr4 = *instBuf++;
    if (is843419ErratumSequence(instr1, instr2, instr4))
      patchOff = off + 12;
  }
  if (((isecAddr + off) & 0xfff) == 0xff8)
    off += 4;
  else
    off += 0xffc;
  return patchOff;
}

class elf::Patch843419Section final : public SyntheticSection {
public:
  Patch843419Section(InputSection *p, uint64_t off);

  void writeTo(uint8_t *buf) override;

  size_t getSize() const override { return 8; }

  uint64_t getLDSTAddr() const;

  static bool classof(const SectionBase *d) {
    return d->kind() == InputSectionBase::Synthetic && d->name == ".text.patch";
  }

  // The Section we are patching.
  const InputSection *patchee;
  // The offset of the instruction in the patchee section we are patching.
  uint64_t patcheeOffset;
  // A label for the start of the Patch that we can use as a relocation target.
  Symbol *patchSym;
};

Patch843419Section::Patch843419Section(InputSection *p, uint64_t off)
    : SyntheticSection(SHF_ALLOC | SHF_EXECINSTR, SHT_PROGBITS, 4,
                       ".text.patch"),
      patchee(p), patcheeOffset(off) {
  this->parent = p->getParent();
  patchSym = addSyntheticLocal(
      saver().save("__CortexA53843419_" + utohexstr(getLDSTAddr())), STT_FUNC,
      0, getSize(), *this);
  addSyntheticLocal(saver().save("$x"), STT_NOTYPE, 0, 0, *this);
}

uint64_t Patch843419Section::getLDSTAddr() const {
  return patchee->getVA(patcheeOffset);
}

void Patch843419Section::writeTo(uint8_t *buf) {
  // Copy the instruction that we will be replacing with a branch in the
  // patchee Section.
  write32le(buf, read32le(patchee->content().begin() + patcheeOffset));

  // Apply any relocation transferred from the original patchee section.
  target->relocateAlloc(*this, buf);

  // Return address is the next instruction after the one we have just copied.
  uint64_t s = getLDSTAddr() + 4;
  uint64_t p = patchSym->getVA() + 4;
  target->relocateNoSym(buf + 4, R_AARCH64_JUMP26, s - p);
}

void AArch64Err843419Patcher::init() {
  // The AArch64 ABI permits data in executable sections. We must avoid scanning
  // this data as if it were instructions to avoid false matches. We use the
  // mapping symbols in the InputObjects to identify this data, caching the
  // results in sectionMap so we don't have to recalculate it each pass.

  // The ABI Section 4.5.4 Mapping symbols; defines local symbols that describe
  // half open intervals [Symbol Value, Next Symbol Value) of code and data
  // within sections. If there is no next symbol then the half open interval is
  // [Symbol Value, End of section). The type, code or data, is determined by
  // the mapping symbol name, $x for code, $d for data.
  auto isCodeMapSymbol = [](const Symbol *b) {
    return b->getName() == "$x" || b->getName().starts_with("$x.");
  };
  auto isDataMapSymbol = [](const Symbol *b) {
    return b->getName() == "$d" || b->getName().starts_with("$d.");
  };

  // Collect mapping symbols for every executable InputSection.
  for (ELFFileBase *file : ctx.objectFiles) {
    for (Symbol *b : file->getLocalSymbols()) {
      auto *def = dyn_cast<Defined>(b);
      if (!def)
        continue;
      if (!isCodeMapSymbol(def) && !isDataMapSymbol(def))
        continue;
      if (auto *sec = dyn_cast_or_null<InputSection>(def->section))
        if (sec->flags & SHF_EXECINSTR)
          sectionMap[sec].push_back(def);
    }
  }
  // For each InputSection make sure the mapping symbols are in sorted in
  // ascending order and free from consecutive runs of mapping symbols with
  // the same type. For example we must remove the redundant $d.1 from $x.0
  // $d.0 $d.1 $x.1.
  for (auto &kv : sectionMap) {
    std::vector<const Defined *> &mapSyms = kv.second;
    llvm::stable_sort(mapSyms, [](const Defined *a, const Defined *b) {
      return a->value < b->value;
    });
    mapSyms.erase(
        std::unique(mapSyms.begin(), mapSyms.end(),
                    [=](const Defined *a, const Defined *b) {
                      return isCodeMapSymbol(a) == isCodeMapSymbol(b);
                    }),
        mapSyms.end());
    // Always start with a Code Mapping Symbol.
    if (!mapSyms.empty() && !isCodeMapSymbol(mapSyms.front()))
      mapSyms.erase(mapSyms.begin());
  }
  initialized = true;
}

// Insert the PatchSections we have created back into the
// InputSectionDescription. As inserting patches alters the addresses of
// InputSections that follow them, we try and place the patches after all the
// executable sections, although we may need to insert them earlier if the
// InputSectionDescription is larger than the maximum branch range.
void AArch64Err843419Patcher::insertPatches(
    InputSectionDescription &isd, std::vector<Patch843419Section *> &patches) {
  uint64_t isecLimit;
  uint64_t prevIsecLimit = isd.sections.front()->outSecOff;
  uint64_t patchUpperBound = prevIsecLimit + target->getThunkSectionSpacing();
  uint64_t outSecAddr = isd.sections.front()->getParent()->addr;

  // Set the outSecOff of patches to the place where we want to insert them.
  // We use a similar strategy to Thunk placement. Place patches roughly
  // every multiple of maximum branch range.
  auto patchIt = patches.begin();
  auto patchEnd = patches.end();
  for (const InputSection *isec : isd.sections) {
    isecLimit = isec->outSecOff + isec->getSize();
    if (isecLimit > patchUpperBound) {
      while (patchIt != patchEnd) {
        if ((*patchIt)->getLDSTAddr() - outSecAddr >= prevIsecLimit)
          break;
        (*patchIt)->outSecOff = prevIsecLimit;
        ++patchIt;
      }
      patchUpperBound = prevIsecLimit + target->getThunkSectionSpacing();
    }
    prevIsecLimit = isecLimit;
  }
  for (; patchIt != patchEnd; ++patchIt) {
    (*patchIt)->outSecOff = isecLimit;
  }

  // Merge all patch sections. We use the outSecOff assigned above to
  // determine the insertion point. This is ok as we only merge into an
  // InputSectionDescription once per pass, and at the end of the pass
  // assignAddresses() will recalculate all the outSecOff values.
  SmallVector<InputSection *, 0> tmp;
  tmp.reserve(isd.sections.size() + patches.size());
  auto mergeCmp = [](const InputSection *a, const InputSection *b) {
    if (a->outSecOff != b->outSecOff)
      return a->outSecOff < b->outSecOff;
    return isa<Patch843419Section>(a) && !isa<Patch843419Section>(b);
  };
  std::merge(isd.sections.begin(), isd.sections.end(), patches.begin(),
             patches.end(), std::back_inserter(tmp), mergeCmp);
  isd.sections = std::move(tmp);
}

// Given an erratum sequence that starts at address adrpAddr, with an
// instruction that we need to patch at patcheeOffset from the start of
// InputSection isec, create a Patch843419 Section and add it to the
// Patches that we need to insert.
static void implementPatch(uint64_t adrpAddr, uint64_t patcheeOffset,
                           InputSection *isec,
                           std::vector<Patch843419Section *> &patches) {
  // There may be a relocation at the same offset that we are patching. There
  // are four cases that we need to consider.
  // Case 1: R_AARCH64_JUMP26 branch relocation. We have already patched this
  // instance of the erratum on a previous patch and altered the relocation. We
  // have nothing more to do.
  // Case 2: A TLS Relaxation R_RELAX_TLS_IE_TO_LE. In this case the ADRP that
  // we read will be transformed into a MOVZ later so we actually don't match
  // the sequence and have nothing more to do.
  // Case 3: A load/store register (unsigned immediate) class relocation. There
  // are two of these R_AARCH_LD64_ABS_LO12_NC and R_AARCH_LD64_GOT_LO12_NC and
  // they are both absolute. We need to add the same relocation to the patch,
  // and replace the relocation with a R_AARCH_JUMP26 branch relocation.
  // Case 4: No relocation. We must create a new R_AARCH64_JUMP26 branch
  // relocation at the offset.
  auto relIt = llvm::find_if(isec->relocs(), [=](const Relocation &r) {
    return r.offset == patcheeOffset;
  });
  if (relIt != isec->relocs().end() &&
      (relIt->type == R_AARCH64_JUMP26 || relIt->expr == R_RELAX_TLS_IE_TO_LE))
    return;

  log("detected cortex-a53-843419 erratum sequence starting at " +
      utohexstr(adrpAddr) + " in unpatched output.");

  auto *ps = make<Patch843419Section>(isec, patcheeOffset);
  patches.push_back(ps);

  auto makeRelToPatch = [](uint64_t offset, Symbol *patchSym) {
    return Relocation{R_PC, R_AARCH64_JUMP26, offset, 0, patchSym};
  };

  if (relIt != isec->relocs().end()) {
    ps->addReloc({relIt->expr, relIt->type, 0, relIt->addend, relIt->sym});
    *relIt = makeRelToPatch(patcheeOffset, ps->patchSym);
  } else
    isec->addReloc(makeRelToPatch(patcheeOffset, ps->patchSym));
}

// Scan all the instructions in InputSectionDescription, for each instance of
// the erratum sequence create a Patch843419Section. We return the list of
// Patch843419Sections that need to be applied to the InputSectionDescription.
std::vector<Patch843419Section *>
AArch64Err843419Patcher::patchInputSectionDescription(
    InputSectionDescription &isd) {
  std::vector<Patch843419Section *> patches;
  for (InputSection *isec : isd.sections) {
    //  LLD doesn't use the erratum sequence in SyntheticSections.
    if (isa<SyntheticSection>(isec))
      continue;
    // Use sectionMap to make sure we only scan code and not inline data.
    // We have already sorted MapSyms in ascending order and removed consecutive
    // mapping symbols of the same type. Our range of executable instructions to
    // scan is therefore [codeSym->value, dataSym->value) or [codeSym->value,
    // section size).
    std::vector<const Defined *> &mapSyms = sectionMap[isec];

    auto codeSym = mapSyms.begin();
    while (codeSym != mapSyms.end()) {
      auto dataSym = std::next(codeSym);
      uint64_t off = (*codeSym)->value;
      uint64_t limit = (dataSym == mapSyms.end()) ? isec->content().size()
                                                  : (*dataSym)->value;

      while (off < limit) {
        uint64_t startAddr = isec->getVA(off);
        if (uint64_t patcheeOffset =
                scanCortexA53Errata843419(isec, off, limit))
          implementPatch(startAddr, patcheeOffset, isec, patches);
      }
      if (dataSym == mapSyms.end())
        break;
      codeSym = std::next(dataSym);
    }
  }
  return patches;
}

// For each InputSectionDescription make one pass over the executable sections
// looking for the erratum sequence; creating a synthetic Patch843419Section
// for each instance found. We insert these synthetic patch sections after the
// executable code in each InputSectionDescription.
//
// PreConditions:
// The Output and Input Sections have had their final addresses assigned.
//
// PostConditions:
// Returns true if at least one patch was added. The addresses of the
// Output and Input Sections may have been changed.
// Returns false if no patches were required and no changes were made.
bool AArch64Err843419Patcher::createFixes() {
  if (!initialized)
    init();

  bool addressesChanged = false;
  for (OutputSection *os : outputSections) {
    if (!(os->flags & SHF_ALLOC) || !(os->flags & SHF_EXECINSTR))
      continue;
    for (SectionCommand *cmd : os->commands)
      if (auto *isd = dyn_cast<InputSectionDescription>(cmd)) {
        std::vector<Patch843419Section *> patches =
            patchInputSectionDescription(*isd);
        if (!patches.empty()) {
          insertPatches(*isd, patches);
          addressesChanged = true;
        }
      }
  }
  return addressesChanged;
}
