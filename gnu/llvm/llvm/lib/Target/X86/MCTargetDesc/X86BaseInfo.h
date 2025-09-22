//===-- X86BaseInfo.h - Top level definitions for X86 -------- --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains small standalone helper functions and enum definitions for
// the X86 target useful for the compiler back-end and the MC libraries.
// As such, it deliberately does not include references to LLVM core
// code gen types, passes, etc..
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_MCTARGETDESC_X86BASEINFO_H
#define LLVM_LIB_TARGET_X86_MCTARGETDESC_X86BASEINFO_H

#include "X86MCTargetDesc.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/ErrorHandling.h"

namespace llvm {
namespace X86 {
// Enums for memory operand decoding. Each memory operand is represented with
// a 5 operand sequence in the form: [Base, Scale, Index, Disp, Segment]
enum {
  AddrBaseReg = 0,
  AddrScaleAmt = 1,
  AddrIndexReg = 2,
  AddrDisp = 3,
  // The operand # of the segment in the memory operand.
  AddrSegmentReg = 4,
  // Total number of operands in a memory reference.
  AddrNumOperands = 5
};

/// AVX512 static rounding constants. These need to match the values in
/// avx512fintrin.h.
enum STATIC_ROUNDING {
  TO_NEAREST_INT = 0,
  TO_NEG_INF = 1,
  TO_POS_INF = 2,
  TO_ZERO = 3,
  CUR_DIRECTION = 4,
  NO_EXC = 8
};

/// The constants to describe instr prefixes if there are
enum IPREFIXES {
  IP_NO_PREFIX = 0,
  IP_HAS_OP_SIZE = 1U << 0,
  IP_HAS_AD_SIZE = 1U << 1,
  IP_HAS_REPEAT_NE = 1U << 2,
  IP_HAS_REPEAT = 1U << 3,
  IP_HAS_LOCK = 1U << 4,
  IP_HAS_NOTRACK = 1U << 5,
  IP_USE_REX = 1U << 6,
  IP_USE_REX2 = 1U << 7,
  IP_USE_VEX = 1U << 8,
  IP_USE_VEX2 = 1U << 9,
  IP_USE_VEX3 = 1U << 10,
  IP_USE_EVEX = 1U << 11,
  IP_USE_DISP8 = 1U << 12,
  IP_USE_DISP32 = 1U << 13,
};

enum OperandType : unsigned {
  // AVX512 embedded rounding control. This should only have values 0-3.
  OPERAND_ROUNDING_CONTROL = MCOI::OPERAND_FIRST_TARGET,
  OPERAND_COND_CODE,
};

// X86 specific condition code. These correspond to X86_*_COND in
// X86InstrInfo.td. They must be kept in synch.
enum CondCode {
  COND_O = 0,
  COND_NO = 1,
  COND_B = 2,
  COND_AE = 3,
  COND_E = 4,
  COND_NE = 5,
  COND_BE = 6,
  COND_A = 7,
  COND_S = 8,
  COND_NS = 9,
  COND_P = 10,
  COND_NP = 11,
  COND_L = 12,
  COND_GE = 13,
  COND_LE = 14,
  COND_G = 15,
  LAST_VALID_COND = COND_G,
  // Artificial condition codes. These are used by analyzeBranch
  // to indicate a block terminated with two conditional branches that together
  // form a compound condition. They occur in code using FCMP_OEQ or FCMP_UNE,
  // which can't be represented on x86 with a single condition. These
  // are never used in MachineInstrs and are inverses of one another.
  COND_NE_OR_P,
  COND_E_AND_NP,
  COND_INVALID
};

// The classification for the first instruction in macro fusion.
// FIXME: Zen 3 support branch fusion for OR/XOR.
enum class FirstMacroFusionInstKind {
  Test,   // TEST
  Cmp,    // CMP
  And,    // AND
  AddSub, // ADD, SUB
  IncDec, // INC, DEC
  Invalid // Not valid as a first macro fusion instruction
};

enum class SecondMacroFusionInstKind {
  AB,      // JA, JB and variants
  ELG,     // JE, JL, JG and variants
  SPO,     // JS, JP, JO and variants
  Invalid, // Not a fusible jump.
};

/// \returns the type of the first instruction in macro-fusion.
// FIXME: Zen 3 support branch fusion for OR/XOR.
inline FirstMacroFusionInstKind
classifyFirstOpcodeInMacroFusion(unsigned Opcode) {
  switch (Opcode) {
  default:
    return FirstMacroFusionInstKind::Invalid;
  // TEST
  case X86::TEST16i16:
  case X86::TEST16mr:
  case X86::TEST16ri:
  case X86::TEST16rr:
  case X86::TEST32i32:
  case X86::TEST32mr:
  case X86::TEST32ri:
  case X86::TEST32rr:
  case X86::TEST64i32:
  case X86::TEST64mr:
  case X86::TEST64ri32:
  case X86::TEST64rr:
  case X86::TEST8i8:
  case X86::TEST8mr:
  case X86::TEST8ri:
  case X86::TEST8rr:
    return FirstMacroFusionInstKind::Test;
  case X86::AND16i16:
  case X86::AND16ri:
  case X86::AND16ri8:
  case X86::AND16rm:
  case X86::AND16rr:
  case X86::AND32i32:
  case X86::AND32ri:
  case X86::AND32ri8:
  case X86::AND32rm:
  case X86::AND32rr:
  case X86::AND64i32:
  case X86::AND64ri32:
  case X86::AND64ri8:
  case X86::AND64rm:
  case X86::AND64rr:
  case X86::AND8i8:
  case X86::AND8ri:
  case X86::AND8ri8:
  case X86::AND8rm:
  case X86::AND8rr:
    return FirstMacroFusionInstKind::And;
  // CMP
  case X86::CMP16i16:
  case X86::CMP16mr:
  case X86::CMP16ri:
  case X86::CMP16ri8:
  case X86::CMP16rm:
  case X86::CMP16rr:
  case X86::CMP32i32:
  case X86::CMP32mr:
  case X86::CMP32ri:
  case X86::CMP32ri8:
  case X86::CMP32rm:
  case X86::CMP32rr:
  case X86::CMP64i32:
  case X86::CMP64mr:
  case X86::CMP64ri32:
  case X86::CMP64ri8:
  case X86::CMP64rm:
  case X86::CMP64rr:
  case X86::CMP8i8:
  case X86::CMP8mr:
  case X86::CMP8ri:
  case X86::CMP8ri8:
  case X86::CMP8rm:
  case X86::CMP8rr:
    return FirstMacroFusionInstKind::Cmp;
  // ADD
  case X86::ADD16i16:
  case X86::ADD16ri:
  case X86::ADD16ri8:
  case X86::ADD16rm:
  case X86::ADD16rr:
  case X86::ADD32i32:
  case X86::ADD32ri:
  case X86::ADD32ri8:
  case X86::ADD32rm:
  case X86::ADD32rr:
  case X86::ADD64i32:
  case X86::ADD64ri32:
  case X86::ADD64ri8:
  case X86::ADD64rm:
  case X86::ADD64rr:
  case X86::ADD8i8:
  case X86::ADD8ri:
  case X86::ADD8ri8:
  case X86::ADD8rm:
  case X86::ADD8rr:
  // SUB
  case X86::SUB16i16:
  case X86::SUB16ri:
  case X86::SUB16ri8:
  case X86::SUB16rm:
  case X86::SUB16rr:
  case X86::SUB32i32:
  case X86::SUB32ri:
  case X86::SUB32ri8:
  case X86::SUB32rm:
  case X86::SUB32rr:
  case X86::SUB64i32:
  case X86::SUB64ri32:
  case X86::SUB64ri8:
  case X86::SUB64rm:
  case X86::SUB64rr:
  case X86::SUB8i8:
  case X86::SUB8ri:
  case X86::SUB8ri8:
  case X86::SUB8rm:
  case X86::SUB8rr:
    return FirstMacroFusionInstKind::AddSub;
  // INC
  case X86::INC16r:
  case X86::INC16r_alt:
  case X86::INC32r:
  case X86::INC32r_alt:
  case X86::INC64r:
  case X86::INC8r:
  // DEC
  case X86::DEC16r:
  case X86::DEC16r_alt:
  case X86::DEC32r:
  case X86::DEC32r_alt:
  case X86::DEC64r:
  case X86::DEC8r:
    return FirstMacroFusionInstKind::IncDec;
  }
}

/// \returns the type of the second instruction in macro-fusion.
inline SecondMacroFusionInstKind
classifySecondCondCodeInMacroFusion(X86::CondCode CC) {
  if (CC == X86::COND_INVALID)
    return SecondMacroFusionInstKind::Invalid;
  switch (CC) {
  default:
    return SecondMacroFusionInstKind::Invalid;
  case X86::COND_E:  // JE,JZ
  case X86::COND_NE: // JNE,JNZ
  case X86::COND_L:  // JL,JNGE
  case X86::COND_LE: // JLE,JNG
  case X86::COND_G:  // JG,JNLE
  case X86::COND_GE: // JGE,JNL
    return SecondMacroFusionInstKind::ELG;
  case X86::COND_B:  // JB,JC
  case X86::COND_BE: // JNA,JBE
  case X86::COND_A:  // JA,JNBE
  case X86::COND_AE: // JAE,JNC,JNB
    return SecondMacroFusionInstKind::AB;
  case X86::COND_S:  // JS
  case X86::COND_NS: // JNS
  case X86::COND_P:  // JP,JPE
  case X86::COND_NP: // JNP,JPO
  case X86::COND_O:  // JO
  case X86::COND_NO: // JNO
    return SecondMacroFusionInstKind::SPO;
  }
}

/// \param FirstKind kind of the first instruction in macro fusion.
/// \param SecondKind kind of the second instruction in macro fusion.
///
/// \returns true if the two instruction can be macro fused.
inline bool isMacroFused(FirstMacroFusionInstKind FirstKind,
                         SecondMacroFusionInstKind SecondKind) {
  switch (FirstKind) {
  case X86::FirstMacroFusionInstKind::Test:
  case X86::FirstMacroFusionInstKind::And:
    return true;
  case X86::FirstMacroFusionInstKind::Cmp:
  case X86::FirstMacroFusionInstKind::AddSub:
    return SecondKind == X86::SecondMacroFusionInstKind::AB ||
           SecondKind == X86::SecondMacroFusionInstKind::ELG;
  case X86::FirstMacroFusionInstKind::IncDec:
    return SecondKind == X86::SecondMacroFusionInstKind::ELG;
  case X86::FirstMacroFusionInstKind::Invalid:
    return false;
  }
  llvm_unreachable("unknown fusion type");
}

/// Defines the possible values of the branch boundary alignment mask.
enum AlignBranchBoundaryKind : uint8_t {
  AlignBranchNone = 0,
  AlignBranchFused = 1U << 0,
  AlignBranchJcc = 1U << 1,
  AlignBranchJmp = 1U << 2,
  AlignBranchCall = 1U << 3,
  AlignBranchRet = 1U << 4,
  AlignBranchIndirect = 1U << 5
};

/// Defines the encoding values for segment override prefix.
enum EncodingOfSegmentOverridePrefix : uint8_t {
  CS_Encoding = 0x2E,
  DS_Encoding = 0x3E,
  ES_Encoding = 0x26,
  FS_Encoding = 0x64,
  GS_Encoding = 0x65,
  SS_Encoding = 0x36
};

/// Given a segment register, return the encoding of the segment override
/// prefix for it.
inline EncodingOfSegmentOverridePrefix
getSegmentOverridePrefixForReg(unsigned Reg) {
  switch (Reg) {
  default:
    llvm_unreachable("Unknown segment register!");
  case X86::CS:
    return CS_Encoding;
  case X86::DS:
    return DS_Encoding;
  case X86::ES:
    return ES_Encoding;
  case X86::FS:
    return FS_Encoding;
  case X86::GS:
    return GS_Encoding;
  case X86::SS:
    return SS_Encoding;
  }
}

} // namespace X86

/// X86II - This namespace holds all of the target specific flags that
/// instruction info tracks.
///
namespace X86II {
/// Target Operand Flag enum.
enum TOF {
  //===------------------------------------------------------------------===//
  // X86 Specific MachineOperand flags.
  //
  /// MO_NO_FLAG - No flag for the operand
  MO_NO_FLAG,
  /// MO_GOT_ABSOLUTE_ADDRESS - On a symbol operand, this represents a
  /// relocation of:
  ///    SYMBOL_LABEL + [. - PICBASELABEL]
  MO_GOT_ABSOLUTE_ADDRESS,
  /// MO_PIC_BASE_OFFSET - On a symbol operand this indicates that the
  /// immediate should get the value of the symbol minus the PIC base label:
  ///    SYMBOL_LABEL - PICBASELABEL
  MO_PIC_BASE_OFFSET,
  /// MO_GOT - On a symbol operand this indicates that the immediate is the
  /// offset to the GOT entry for the symbol name from the base of the GOT.
  /// See the X86-64 ELF ABI supplement for more details.
  ///    SYMBOL_LABEL @GOT
  MO_GOT,
  /// MO_GOTOFF - On a symbol operand this indicates that the immediate is
  /// the offset to the location of the symbol name from the base of the GOT.
  /// See the X86-64 ELF ABI supplement for more details.
  ///    SYMBOL_LABEL @GOTOFF
  MO_GOTOFF,
  /// MO_GOTPCREL - On a symbol operand this indicates that the immediate is
  /// offset to the GOT entry for the symbol name from the current code
  /// location.
  /// See the X86-64 ELF ABI supplement for more details.
  ///    SYMBOL_LABEL @GOTPCREL
  MO_GOTPCREL,
  /// MO_GOTPCREL_NORELAX - Same as MO_GOTPCREL except that R_X86_64_GOTPCREL
  /// relocations are guaranteed to be emitted by the integrated assembler
  /// instead of the relaxable R_X86_64[_REX]_GOTPCRELX relocations.
  MO_GOTPCREL_NORELAX,
  /// MO_PLT - On a symbol operand this indicates that the immediate is
  /// offset to the PLT entry of symbol name from the current code location.
  /// See the X86-64 ELF ABI supplement for more details.
  ///    SYMBOL_LABEL @PLT
  MO_PLT,
  /// MO_TLSGD - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the TLS index structure that contains
  /// the module number and variable offset for the symbol. Used in the
  /// general dynamic TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @TLSGD
  MO_TLSGD,
  /// MO_TLSLD - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the TLS index for the module that
  /// contains the symbol. When this index is passed to a call to
  /// __tls_get_addr, the function will return the base address of the TLS
  /// block for the symbol. Used in the x86-64 local dynamic TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @TLSLD
  MO_TLSLD,
  /// MO_TLSLDM - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the TLS index for the module that
  /// contains the symbol. When this index is passed to a call to
  /// ___tls_get_addr, the function will return the base address of the TLS
  /// block for the symbol. Used in the IA32 local dynamic TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @TLSLDM
  MO_TLSLDM,
  /// MO_GOTTPOFF - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the thread-pointer offset for the
  /// symbol. Used in the x86-64 initial exec TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @GOTTPOFF
  MO_GOTTPOFF,
  /// MO_INDNTPOFF - On a symbol operand this indicates that the immediate is
  /// the absolute address of the GOT entry with the negative thread-pointer
  /// offset for the symbol. Used in the non-PIC IA32 initial exec TLS access
  /// model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @INDNTPOFF
  MO_INDNTPOFF,
  /// MO_TPOFF - On a symbol operand this indicates that the immediate is
  /// the thread-pointer offset for the symbol. Used in the x86-64 local
  /// exec TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @TPOFF
  MO_TPOFF,
  /// MO_DTPOFF - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the TLS offset of the symbol. Used
  /// in the local dynamic TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @DTPOFF
  MO_DTPOFF,
  /// MO_NTPOFF - On a symbol operand this indicates that the immediate is
  /// the negative thread-pointer offset for the symbol. Used in the IA32
  /// local exec TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @NTPOFF
  MO_NTPOFF,
  /// MO_GOTNTPOFF - On a symbol operand this indicates that the immediate is
  /// the offset of the GOT entry with the negative thread-pointer offset for
  /// the symbol. Used in the PIC IA32 initial exec TLS access model.
  /// See 'ELF Handling for Thread-Local Storage' for more details.
  ///    SYMBOL_LABEL @GOTNTPOFF
  MO_GOTNTPOFF,
  /// MO_DLLIMPORT - On a symbol operand "FOO", this indicates that the
  /// reference is actually to the "__imp_FOO" symbol.  This is used for
  /// dllimport linkage on windows.
  MO_DLLIMPORT,
  /// MO_DARWIN_NONLAZY - On a symbol operand "FOO", this indicates that the
  /// reference is actually to the "FOO$non_lazy_ptr" symbol, which is a
  /// non-PIC-base-relative reference to a non-hidden dyld lazy pointer stub.
  MO_DARWIN_NONLAZY,
  /// MO_DARWIN_NONLAZY_PIC_BASE - On a symbol operand "FOO", this indicates
  /// that the reference is actually to "FOO$non_lazy_ptr - PICBASE", which is
  /// a PIC-base-relative reference to a non-hidden dyld lazy pointer stub.
  MO_DARWIN_NONLAZY_PIC_BASE,
  /// MO_TLVP - On a symbol operand this indicates that the immediate is
  /// some TLS offset.
  /// This is the TLS offset for the Darwin TLS mechanism.
  MO_TLVP,
  /// MO_TLVP_PIC_BASE - On a symbol operand this indicates that the immediate
  /// is some TLS offset from the picbase.
  /// This is the 32-bit TLS offset for Darwin TLS in PIC mode.
  MO_TLVP_PIC_BASE,
  /// MO_SECREL - On a symbol operand this indicates that the immediate is
  /// the offset from beginning of section.
  /// This is the TLS offset for the COFF/Windows TLS mechanism.
  MO_SECREL,
  /// MO_ABS8 - On a symbol operand this indicates that the symbol is known
  /// to be an absolute symbol in range [0,128), so we can use the @ABS8
  /// symbol modifier.
  MO_ABS8,
  /// MO_COFFSTUB - On a symbol operand "FOO", this indicates that the
  /// reference is actually to the ".refptr.FOO" symbol.  This is used for
  /// stub symbols on windows.
  MO_COFFSTUB,
};

enum : uint64_t {
  //===------------------------------------------------------------------===//
  // Instruction encodings.  These are the standard/most common forms for X86
  // instructions.
  //
  /// PseudoFrm - This represents an instruction that is a pseudo instruction
  /// or one that has not been implemented yet.  It is illegal to code generate
  /// it, but tolerated for intermediate implementation stages.
  Pseudo = 0,
  /// Raw - This form is for instructions that don't have any operands, so
  /// they are just a fixed opcode value, like 'leave'.
  RawFrm = 1,
  /// AddRegFrm - This form is used for instructions like 'push r32' that have
  /// their one register operand added to their opcode.
  AddRegFrm = 2,
  /// RawFrmMemOffs - This form is for instructions that store an absolute
  /// memory offset as an immediate with a possible segment override.
  RawFrmMemOffs = 3,
  /// RawFrmSrc - This form is for instructions that use the source index
  /// register SI/ESI/RSI with a possible segment override.
  RawFrmSrc = 4,
  /// RawFrmDst - This form is for instructions that use the destination index
  /// register DI/EDI/RDI.
  RawFrmDst = 5,
  /// RawFrmDstSrc - This form is for instructions that use the source index
  /// register SI/ESI/RSI with a possible segment override, and also the
  /// destination index register DI/EDI/RDI.
  RawFrmDstSrc = 6,
  /// RawFrmImm8 - This is used for the ENTER instruction, which has two
  /// immediates, the first of which is a 16-bit immediate (specified by
  /// the imm encoding) and the second is a 8-bit fixed value.
  RawFrmImm8 = 7,
  /// RawFrmImm16 - This is used for CALL FAR instructions, which have two
  /// immediates, the first of which is a 16 or 32-bit immediate (specified by
  /// the imm encoding) and the second is a 16-bit fixed value.  In the AMD
  /// manual, this operand is described as pntr16:32 and pntr16:16
  RawFrmImm16 = 8,
  /// AddCCFrm - This form is used for Jcc that encode the condition code
  /// in the lower 4 bits of the opcode.
  AddCCFrm = 9,
  /// PrefixByte - This form is used for instructions that represent a prefix
  /// byte like data16 or rep.
  PrefixByte = 10,
  /// MRMDestRegCC - This form is used for the cfcmov instructions, which use
  /// the Mod/RM byte to specify the operands reg(r/m) and reg(reg) and also
  /// encodes a condition code.
  MRMDestRegCC = 18,
  /// MRMDestMemCC - This form is used for the cfcmov instructions, which use
  /// the Mod/RM byte to specify the operands mem(r/m) and reg(reg) and also
  /// encodes a condition code.
  MRMDestMemCC = 19,
  /// MRMDestMem4VOp3CC - This form is used for instructions that use the Mod/RM
  /// byte to specify a destination which in this case is memory and operand 3
  /// with VEX.VVVV, and also encodes a condition code.
  MRMDestMem4VOp3CC = 20,
  /// Instructions operate on a register Reg/Opcode operand not the r/m field.
  MRMr0 = 21,
  /// MRMSrcMem - But force to use the SIB field.
  MRMSrcMemFSIB = 22,
  /// MRMDestMem - But force to use the SIB field.
  MRMDestMemFSIB = 23,
  /// MRMDestMem - This form is used for instructions that use the Mod/RM byte
  /// to specify a destination, which in this case is memory.
  MRMDestMem = 24,
  /// MRMSrcMem - This form is used for instructions that use the Mod/RM byte
  /// to specify a source, which in this case is memory.
  MRMSrcMem = 25,
  /// MRMSrcMem4VOp3 - This form is used for instructions that encode
  /// operand 3 with VEX.VVVV and load from memory.
  MRMSrcMem4VOp3 = 26,
  /// MRMSrcMemOp4 - This form is used for instructions that use the Mod/RM
  /// byte to specify the fourth source, which in this case is memory.
  MRMSrcMemOp4 = 27,
  /// MRMSrcMemCC - This form is used for instructions that use the Mod/RM
  /// byte to specify the operands and also encodes a condition code.
  MRMSrcMemCC = 28,
  /// MRMXm - This form is used for instructions that use the Mod/RM byte
  /// to specify a memory source, but doesn't use the middle field. And has
  /// a condition code.
  MRMXmCC = 30,
  /// MRMXm - This form is used for instructions that use the Mod/RM byte
  /// to specify a memory source, but doesn't use the middle field.
  MRMXm = 31,
  /// MRM0m-MRM7m - Instructions that operate on a memory r/m operand and use
  /// reg field to hold extended opcode, which is represented as /0, /1, ...
  MRM0m = 32, // Format /0
  MRM1m = 33, // Format /1
  MRM2m = 34, // Format /2
  MRM3m = 35, // Format /3
  MRM4m = 36, // Format /4
  MRM5m = 37, // Format /5
  MRM6m = 38, // Format /6
  MRM7m = 39, // Format /7
  /// MRMDestReg - This form is used for instructions that use the Mod/RM byte
  /// to specify a destination, which in this case is a register.
  MRMDestReg = 40,
  /// MRMSrcReg - This form is used for instructions that use the Mod/RM byte
  /// to specify a source, which in this case is a register.
  MRMSrcReg = 41,
  /// MRMSrcReg4VOp3 - This form is used for instructions that encode
  /// operand 3 with VEX.VVVV and do not load from memory.
  MRMSrcReg4VOp3 = 42,
  /// MRMSrcRegOp4 - This form is used for instructions that use the Mod/RM
  /// byte to specify the fourth source, which in this case is a register.
  MRMSrcRegOp4 = 43,
  /// MRMSrcRegCC - This form is used for instructions that use the Mod/RM
  /// byte to specify the operands and also encodes a condition code
  MRMSrcRegCC = 44,
  /// MRMXCCr - This form is used for instructions that use the Mod/RM byte
  /// to specify a register source, but doesn't use the middle field. And has
  /// a condition code.
  MRMXrCC = 46,
  /// MRMXr - This form is used for instructions that use the Mod/RM byte
  /// to specify a register source, but doesn't use the middle field.
  MRMXr = 47,
  /// MRM0r-MRM7r - Instructions that operate on a register r/m operand and use
  /// reg field to hold extended opcode, which is represented as /0, /1, ...
  MRM0r = 48, // Format /0
  MRM1r = 49, // Format /1
  MRM2r = 50, // Format /2
  MRM3r = 51, // Format /3
  MRM4r = 52, // Format /4
  MRM5r = 53, // Format /5
  MRM6r = 54, // Format /6
  MRM7r = 55, // Format /7
  /// MRM0X-MRM7X - Instructions that operate that have mod=11 and an opcode but
  /// ignore r/m.
  MRM0X = 56, // Format /0
  MRM1X = 57, // Format /1
  MRM2X = 58, // Format /2
  MRM3X = 59, // Format /3
  MRM4X = 60, // Format /4
  MRM5X = 61, // Format /5
  MRM6X = 62, // Format /6
  MRM7X = 63, // Format /7
  /// MRM_XX (XX: C0-FF)- A mod/rm byte of exactly 0xXX.
  MRM_C0 = 64,
  MRM_C1 = 65,
  MRM_C2 = 66,
  MRM_C3 = 67,
  MRM_C4 = 68,
  MRM_C5 = 69,
  MRM_C6 = 70,
  MRM_C7 = 71,
  MRM_C8 = 72,
  MRM_C9 = 73,
  MRM_CA = 74,
  MRM_CB = 75,
  MRM_CC = 76,
  MRM_CD = 77,
  MRM_CE = 78,
  MRM_CF = 79,
  MRM_D0 = 80,
  MRM_D1 = 81,
  MRM_D2 = 82,
  MRM_D3 = 83,
  MRM_D4 = 84,
  MRM_D5 = 85,
  MRM_D6 = 86,
  MRM_D7 = 87,
  MRM_D8 = 88,
  MRM_D9 = 89,
  MRM_DA = 90,
  MRM_DB = 91,
  MRM_DC = 92,
  MRM_DD = 93,
  MRM_DE = 94,
  MRM_DF = 95,
  MRM_E0 = 96,
  MRM_E1 = 97,
  MRM_E2 = 98,
  MRM_E3 = 99,
  MRM_E4 = 100,
  MRM_E5 = 101,
  MRM_E6 = 102,
  MRM_E7 = 103,
  MRM_E8 = 104,
  MRM_E9 = 105,
  MRM_EA = 106,
  MRM_EB = 107,
  MRM_EC = 108,
  MRM_ED = 109,
  MRM_EE = 110,
  MRM_EF = 111,
  MRM_F0 = 112,
  MRM_F1 = 113,
  MRM_F2 = 114,
  MRM_F3 = 115,
  MRM_F4 = 116,
  MRM_F5 = 117,
  MRM_F6 = 118,
  MRM_F7 = 119,
  MRM_F8 = 120,
  MRM_F9 = 121,
  MRM_FA = 122,
  MRM_FB = 123,
  MRM_FC = 124,
  MRM_FD = 125,
  MRM_FE = 126,
  MRM_FF = 127,
  FormMask = 127,
  //===------------------------------------------------------------------===//
  // Actual flags...
  /// OpSize - OpSizeFixed implies instruction never needs a 0x66 prefix.
  /// OpSize16 means this is a 16-bit instruction and needs 0x66 prefix in
  /// 32-bit mode. OpSize32 means this is a 32-bit instruction needs a 0x66
  /// prefix in 16-bit mode.
  OpSizeShift = 7,
  OpSizeMask = 0x3 << OpSizeShift,
  OpSizeFixed = 0 << OpSizeShift,
  OpSize16 = 1 << OpSizeShift,
  OpSize32 = 2 << OpSizeShift,
  /// AsSize - AdSizeX implies this instruction determines its need of 0x67
  /// prefix from a normal ModRM memory operand. The other types indicate that
  /// an operand is encoded with a specific width and a prefix is needed if
  /// it differs from the current mode.
  AdSizeShift = OpSizeShift + 2,
  AdSizeMask = 0x3 << AdSizeShift,
  AdSizeX = 0 << AdSizeShift,
  AdSize16 = 1 << AdSizeShift,
  AdSize32 = 2 << AdSizeShift,
  AdSize64 = 3 << AdSizeShift,
  //===------------------------------------------------------------------===//
  /// OpPrefix - There are several prefix bytes that are used as opcode
  /// extensions. These are 0x66, 0xF3, and 0xF2. If this field is 0 there is
  /// no prefix.
  OpPrefixShift = AdSizeShift + 2,
  OpPrefixMask = 0x3 << OpPrefixShift,
  /// PD - Prefix code for packed double precision vector floating point
  /// operations performed in the SSE registers.
  PD = 1 << OpPrefixShift,
  /// XS, XD - These prefix codes are for single and double precision scalar
  /// floating point operations performed in the SSE registers.
  XS = 2 << OpPrefixShift,
  XD = 3 << OpPrefixShift,
  //===------------------------------------------------------------------===//
  /// OpMap - This field determines which opcode map this instruction
  /// belongs to. i.e. one-byte, two-byte, 0x0f 0x38, 0x0f 0x3a, etc.
  OpMapShift = OpPrefixShift + 2,
  OpMapMask = 0xF << OpMapShift,
  /// OB - OneByte - Set if this instruction has a one byte opcode.
  OB = 0 << OpMapShift,
  /// TB - TwoByte - Set if this instruction has a two byte opcode, which
  /// starts with a 0x0F byte before the real opcode.
  TB = 1 << OpMapShift,
  /// T8, TA - Prefix after the 0x0F prefix.
  T8 = 2 << OpMapShift,
  TA = 3 << OpMapShift,
  /// XOP8 - Prefix to include use of imm byte.
  XOP8 = 4 << OpMapShift,
  /// XOP9 - Prefix to exclude use of imm byte.
  XOP9 = 5 << OpMapShift,
  /// XOPA - Prefix to encode 0xA in VEX.MMMM of XOP instructions.
  XOPA = 6 << OpMapShift,
  /// ThreeDNow - This indicates that the instruction uses the
  /// wacky 0x0F 0x0F prefix for 3DNow! instructions.  The manual documents
  /// this as having a 0x0F prefix with a 0x0F opcode, and each instruction
  /// storing a classifier in the imm8 field.  To simplify our implementation,
  /// we handle this by storeing the classifier in the opcode field and using
  /// this flag to indicate that the encoder should do the wacky 3DNow! thing.
  ThreeDNow = 7 << OpMapShift,
  /// MAP4, MAP5, MAP6, MAP7 - Prefix after the 0x0F prefix.
  T_MAP4 = 8 << OpMapShift,
  T_MAP5 = 9 << OpMapShift,
  T_MAP6 = 10 << OpMapShift,
  T_MAP7 = 11 << OpMapShift,
  //===------------------------------------------------------------------===//
  /// REX_W - REX prefixes are instruction prefixes used in 64-bit mode.
  /// They are used to specify GPRs and SSE registers, 64-bit operand size,
  /// etc. We only cares about REX.W and REX.R bits and only the former is
  /// statically determined.
  REXShift = OpMapShift + 4,
  REX_W = 1 << REXShift,
  //===------------------------------------------------------------------===//
  // This 4-bit field describes the size of an immediate operand. Zero is
  // unused so that we can tell if we forgot to set a value.
  ImmShift = REXShift + 1,
  Imm8 = 1 << ImmShift,
  Imm8PCRel = 2 << ImmShift,
  Imm8Reg = 3 << ImmShift,
  Imm16 = 4 << ImmShift,
  Imm16PCRel = 5 << ImmShift,
  Imm32 = 6 << ImmShift,
  Imm32PCRel = 7 << ImmShift,
  Imm32S = 8 << ImmShift,
  Imm64 = 9 << ImmShift,
  ImmMask = 15 << ImmShift,
  //===------------------------------------------------------------------===//
  /// FP Instruction Classification...  Zero is non-fp instruction.
  /// FPTypeMask - Mask for all of the FP types...
  FPTypeShift = ImmShift + 4,
  FPTypeMask = 7 << FPTypeShift,
  /// NotFP - The default, set for instructions that do not use FP registers.
  NotFP = 0 << FPTypeShift,
  /// ZeroArgFP - 0 arg FP instruction which implicitly pushes ST(0), f.e. fld0
  ZeroArgFP = 1 << FPTypeShift,
  /// OneArgFP - 1 arg FP instructions which implicitly read ST(0), such as fst
  OneArgFP = 2 << FPTypeShift,
  /// OneArgFPRW - 1 arg FP instruction which implicitly read ST(0) and write a
  /// result back to ST(0).  For example, fcos, fsqrt, etc.
  OneArgFPRW = 3 << FPTypeShift,
  /// TwoArgFP - 2 arg FP instructions which implicitly read ST(0), and an
  /// explicit argument, storing the result to either ST(0) or the implicit
  /// argument.  For example: fadd, fsub, fmul, etc...
  TwoArgFP = 4 << FPTypeShift,
  /// CompareFP - 2 arg FP instructions which implicitly read ST(0) and an
  /// explicit argument, but have no destination.  Example: fucom, fucomi, ...
  CompareFP = 5 << FPTypeShift,
  /// CondMovFP - "2 operand" floating point conditional move instructions.
  CondMovFP = 6 << FPTypeShift,
  /// SpecialFP - Special instruction forms.  Dispatch by opcode explicitly.
  SpecialFP = 7 << FPTypeShift,
  /// Lock prefix
  LOCKShift = FPTypeShift + 3,
  LOCK = 1 << LOCKShift,
  /// REP prefix
  REPShift = LOCKShift + 1,
  REP = 1 << REPShift,
  /// Execution domain for SSE instructions.
  /// 0 means normal, non-SSE instruction.
  SSEDomainShift = REPShift + 1,
  /// Encoding
  EncodingShift = SSEDomainShift + 2,
  EncodingMask = 0x3 << EncodingShift,
  /// LEGACY - encoding using REX/REX2 or w/o opcode prefix.
  LEGACY = 0 << EncodingShift,
  /// VEX - encoding using 0xC4/0xC5
  VEX = 1 << EncodingShift,
  /// XOP - Opcode prefix used by XOP instructions.
  XOP = 2 << EncodingShift,
  /// EVEX - Specifies that this instruction use EVEX form which provides
  /// syntax support up to 32 512-bit register operands and up to 7 16-bit
  /// mask operands as well as source operand data swizzling/memory operand
  /// conversion, eviction hint, and rounding mode.
  EVEX = 3 << EncodingShift,
  /// Opcode
  OpcodeShift = EncodingShift + 2,
  /// VEX_4V - Used to specify an additional AVX/SSE register. Several 2
  /// address instructions in SSE are represented as 3 address ones in AVX
  /// and the additional register is encoded in VEX_VVVV prefix.
  VEX_4VShift = OpcodeShift + 8,
  VEX_4V = 1ULL << VEX_4VShift,
  /// VEX_L - Stands for a bit in the VEX opcode prefix meaning the current
  /// instruction uses 256-bit wide registers. This is usually auto detected
  /// if a VR256 register is used, but some AVX instructions also have this
  /// field marked when using a f256 memory references.
  VEX_LShift = VEX_4VShift + 1,
  VEX_L = 1ULL << VEX_LShift,
  /// EVEX_K - Set if this instruction requires masking
  EVEX_KShift = VEX_LShift + 1,
  EVEX_K = 1ULL << EVEX_KShift,
  /// EVEX_Z - Set if this instruction has EVEX.Z field set.
  EVEX_ZShift = EVEX_KShift + 1,
  EVEX_Z = 1ULL << EVEX_ZShift,
  /// EVEX_L2 - Set if this instruction has EVEX.L' field set.
  EVEX_L2Shift = EVEX_ZShift + 1,
  EVEX_L2 = 1ULL << EVEX_L2Shift,
  /// EVEX_B - Set if this instruction has EVEX.B field set.
  EVEX_BShift = EVEX_L2Shift + 1,
  EVEX_B = 1ULL << EVEX_BShift,
  /// The scaling factor for the AVX512's 8-bit compressed displacement.
  CD8_Scale_Shift = EVEX_BShift + 1,
  CD8_Scale_Mask = 7ULL << CD8_Scale_Shift,
  /// Explicitly specified rounding control
  EVEX_RCShift = CD8_Scale_Shift + 3,
  EVEX_RC = 1ULL << EVEX_RCShift,
  /// NOTRACK prefix
  NoTrackShift = EVEX_RCShift + 1,
  NOTRACK = 1ULL << NoTrackShift,
  /// Force REX2/VEX/EVEX encoding
  ExplicitOpPrefixShift = NoTrackShift + 1,
  /// For instructions that require REX2 prefix even if EGPR is not used.
  ExplicitREX2Prefix = 1ULL << ExplicitOpPrefixShift,
  /// For instructions that use VEX encoding only when {vex}, {vex2} or {vex3}
  /// is present.
  ExplicitVEXPrefix = 2ULL << ExplicitOpPrefixShift,
  /// For instructions that are promoted to EVEX space for EGPR.
  ExplicitEVEXPrefix = 3ULL << ExplicitOpPrefixShift,
  ExplicitOpPrefixMask = 3ULL << ExplicitOpPrefixShift,
  /// EVEX_NF - Set if this instruction has EVEX.NF field set.
  EVEX_NFShift = ExplicitOpPrefixShift + 2,
  EVEX_NF = 1ULL << EVEX_NFShift,
  // TwoConditionalOps - Set if this instruction has two conditional operands
  TwoConditionalOps_Shift = EVEX_NFShift + 1,
  TwoConditionalOps = 1ULL << TwoConditionalOps_Shift
};

/// \returns true if the instruction with given opcode is a prefix.
inline bool isPrefix(uint64_t TSFlags) {
  return (TSFlags & X86II::FormMask) == PrefixByte;
}

/// \returns true if the instruction with given opcode is a pseudo.
inline bool isPseudo(uint64_t TSFlags) {
  return (TSFlags & X86II::FormMask) == Pseudo;
}

/// \returns the "base" X86 opcode for the specified machine
/// instruction.
inline uint8_t getBaseOpcodeFor(uint64_t TSFlags) {
  return TSFlags >> X86II::OpcodeShift;
}

inline bool hasImm(uint64_t TSFlags) { return (TSFlags & X86II::ImmMask) != 0; }

/// Decode the "size of immediate" field from the TSFlags field of the
/// specified instruction.
inline unsigned getSizeOfImm(uint64_t TSFlags) {
  switch (TSFlags & X86II::ImmMask) {
  default:
    llvm_unreachable("Unknown immediate size");
  case X86II::Imm8:
  case X86II::Imm8PCRel:
  case X86II::Imm8Reg:
    return 1;
  case X86II::Imm16:
  case X86II::Imm16PCRel:
    return 2;
  case X86II::Imm32:
  case X86II::Imm32S:
  case X86II::Imm32PCRel:
    return 4;
  case X86II::Imm64:
    return 8;
  }
}

/// \returns true if the immediate of the specified instruction's TSFlags
/// indicates that it is pc relative.
inline bool isImmPCRel(uint64_t TSFlags) {
  switch (TSFlags & X86II::ImmMask) {
  default:
    llvm_unreachable("Unknown immediate size");
  case X86II::Imm8PCRel:
  case X86II::Imm16PCRel:
  case X86II::Imm32PCRel:
    return true;
  case X86II::Imm8:
  case X86II::Imm8Reg:
  case X86II::Imm16:
  case X86II::Imm32:
  case X86II::Imm32S:
  case X86II::Imm64:
    return false;
  }
}

/// \returns true if the immediate of the specified instruction's
/// TSFlags indicates that it is signed.
inline bool isImmSigned(uint64_t TSFlags) {
  switch (TSFlags & X86II::ImmMask) {
  default:
    llvm_unreachable("Unknown immediate signedness");
  case X86II::Imm32S:
    return true;
  case X86II::Imm8:
  case X86II::Imm8PCRel:
  case X86II::Imm8Reg:
  case X86II::Imm16:
  case X86II::Imm16PCRel:
  case X86II::Imm32:
  case X86II::Imm32PCRel:
  case X86II::Imm64:
    return false;
  }
}

/// Compute whether all of the def operands are repeated in the uses and
/// therefore should be skipped.
/// This determines the start of the unique operand list. We need to determine
/// if all of the defs have a corresponding tied operand in the uses.
/// Unfortunately, the tied operand information is encoded in the uses not
/// the defs so we have to use some heuristics to find which operands to
/// query.
inline unsigned getOperandBias(const MCInstrDesc &Desc) {
  unsigned NumDefs = Desc.getNumDefs();
  unsigned NumOps = Desc.getNumOperands();
  switch (NumDefs) {
  default:
    llvm_unreachable("Unexpected number of defs");
  case 0:
    return 0;
  case 1:
    // Common two addr case.
    if (NumOps > 1 && Desc.getOperandConstraint(1, MCOI::TIED_TO) == 0)
      return 1;
    // Check for AVX-512 scatter which has a TIED_TO in the second to last
    // operand.
    if (NumOps == 8 && Desc.getOperandConstraint(6, MCOI::TIED_TO) == 0)
      return 1;
    return 0;
  case 2:
    // XCHG/XADD have two destinations and two sources.
    if (NumOps >= 4 && Desc.getOperandConstraint(2, MCOI::TIED_TO) == 0 &&
        Desc.getOperandConstraint(3, MCOI::TIED_TO) == 1)
      return 2;
    // Check for gather. AVX-512 has the second tied operand early. AVX2
    // has it as the last op.
    if (NumOps == 9 && Desc.getOperandConstraint(2, MCOI::TIED_TO) == 0 &&
        (Desc.getOperandConstraint(3, MCOI::TIED_TO) == 1 ||
         Desc.getOperandConstraint(8, MCOI::TIED_TO) == 1))
      return 2;
    return 0;
  }
}

/// \returns true if the instruction has a NDD (new data destination).
inline bool hasNewDataDest(uint64_t TSFlags) {
  return (TSFlags & X86II::OpMapMask) == X86II::T_MAP4 &&
         (TSFlags & X86II::EVEX_B) && (TSFlags & X86II::VEX_4V);
}

/// \returns operand # for the first field of the memory operand or -1 if no
/// memory operands.
/// NOTE: This ignores tied operands.  If there is a tied register which is
/// duplicated in the MCInst (e.g. "EAX = addl EAX, [mem]") it is only counted
/// as one operand.
inline int getMemoryOperandNo(uint64_t TSFlags) {
  bool HasVEX_4V = TSFlags & X86II::VEX_4V;
  bool HasEVEX_K = TSFlags & X86II::EVEX_K;

  switch (TSFlags & X86II::FormMask) {
  default:
    llvm_unreachable("Unknown FormMask value in getMemoryOperandNo!");
  case X86II::Pseudo:
  case X86II::RawFrm:
  case X86II::AddRegFrm:
  case X86II::RawFrmImm8:
  case X86II::RawFrmImm16:
  case X86II::RawFrmMemOffs:
  case X86II::RawFrmSrc:
  case X86II::RawFrmDst:
  case X86II::RawFrmDstSrc:
  case X86II::AddCCFrm:
  case X86II::PrefixByte:
    return -1;
  case X86II::MRMDestMem:
  case X86II::MRMDestMemFSIB:
  case X86II::MRMDestMemCC:
    return hasNewDataDest(TSFlags);
  case X86II::MRMSrcMem:
  case X86II::MRMSrcMemFSIB:
    // Start from 1, skip any registers encoded in VEX_VVVV or I8IMM, or a
    // mask register.
    return 1 + HasVEX_4V + HasEVEX_K;
  case X86II::MRMSrcMem4VOp3:
    // Skip registers encoded in reg.
    return 1 + HasEVEX_K;
  case X86II::MRMSrcMemOp4:
    // Skip registers encoded in reg, VEX_VVVV, and I8IMM.
    return 3;
  case X86II::MRMSrcMemCC:
    return 1 + hasNewDataDest(TSFlags);
  case X86II::MRMDestMem4VOp3CC:
    // Start from 1, skip any registers encoded in VEX_VVVV or I8IMM, or a
    // mask register.
    return 1;
  case X86II::MRMDestReg:
  case X86II::MRMDestRegCC:
  case X86II::MRMSrcReg:
  case X86II::MRMSrcReg4VOp3:
  case X86II::MRMSrcRegOp4:
  case X86II::MRMSrcRegCC:
  case X86II::MRMXrCC:
  case X86II::MRMr0:
  case X86II::MRMXr:
  case X86II::MRM0r:
  case X86II::MRM1r:
  case X86II::MRM2r:
  case X86II::MRM3r:
  case X86II::MRM4r:
  case X86II::MRM5r:
  case X86II::MRM6r:
  case X86II::MRM7r:
    return -1;
  case X86II::MRM0X:
  case X86II::MRM1X:
  case X86II::MRM2X:
  case X86II::MRM3X:
  case X86II::MRM4X:
  case X86II::MRM5X:
  case X86II::MRM6X:
  case X86II::MRM7X:
    return -1;
  case X86II::MRMXmCC:
  case X86II::MRMXm:
  case X86II::MRM0m:
  case X86II::MRM1m:
  case X86II::MRM2m:
  case X86II::MRM3m:
  case X86II::MRM4m:
  case X86II::MRM5m:
  case X86II::MRM6m:
  case X86II::MRM7m:
    // Start from 0, skip registers encoded in VEX_VVVV or a mask register.
    return 0 + HasVEX_4V + HasEVEX_K;
  case X86II::MRM_C0:
  case X86II::MRM_C1:
  case X86II::MRM_C2:
  case X86II::MRM_C3:
  case X86II::MRM_C4:
  case X86II::MRM_C5:
  case X86II::MRM_C6:
  case X86II::MRM_C7:
  case X86II::MRM_C8:
  case X86II::MRM_C9:
  case X86II::MRM_CA:
  case X86II::MRM_CB:
  case X86II::MRM_CC:
  case X86II::MRM_CD:
  case X86II::MRM_CE:
  case X86II::MRM_CF:
  case X86II::MRM_D0:
  case X86II::MRM_D1:
  case X86II::MRM_D2:
  case X86II::MRM_D3:
  case X86II::MRM_D4:
  case X86II::MRM_D5:
  case X86II::MRM_D6:
  case X86II::MRM_D7:
  case X86II::MRM_D8:
  case X86II::MRM_D9:
  case X86II::MRM_DA:
  case X86II::MRM_DB:
  case X86II::MRM_DC:
  case X86II::MRM_DD:
  case X86II::MRM_DE:
  case X86II::MRM_DF:
  case X86II::MRM_E0:
  case X86II::MRM_E1:
  case X86II::MRM_E2:
  case X86II::MRM_E3:
  case X86II::MRM_E4:
  case X86II::MRM_E5:
  case X86II::MRM_E6:
  case X86II::MRM_E7:
  case X86II::MRM_E8:
  case X86II::MRM_E9:
  case X86II::MRM_EA:
  case X86II::MRM_EB:
  case X86II::MRM_EC:
  case X86II::MRM_ED:
  case X86II::MRM_EE:
  case X86II::MRM_EF:
  case X86II::MRM_F0:
  case X86II::MRM_F1:
  case X86II::MRM_F2:
  case X86II::MRM_F3:
  case X86II::MRM_F4:
  case X86II::MRM_F5:
  case X86II::MRM_F6:
  case X86II::MRM_F7:
  case X86II::MRM_F8:
  case X86II::MRM_F9:
  case X86II::MRM_FA:
  case X86II::MRM_FB:
  case X86II::MRM_FC:
  case X86II::MRM_FD:
  case X86II::MRM_FE:
  case X86II::MRM_FF:
    return -1;
  }
}

/// \returns true if the register is a XMM.
inline bool isXMMReg(unsigned RegNo) {
  static_assert(X86::XMM15 - X86::XMM0 == 15,
                "XMM0-15 registers are not continuous");
  static_assert(X86::XMM31 - X86::XMM16 == 15,
                "XMM16-31 registers are not continuous");
  return (RegNo >= X86::XMM0 && RegNo <= X86::XMM15) ||
         (RegNo >= X86::XMM16 && RegNo <= X86::XMM31);
}

/// \returns true if the register is a YMM.
inline bool isYMMReg(unsigned RegNo) {
  static_assert(X86::YMM15 - X86::YMM0 == 15,
                "YMM0-15 registers are not continuous");
  static_assert(X86::YMM31 - X86::YMM16 == 15,
                "YMM16-31 registers are not continuous");
  return (RegNo >= X86::YMM0 && RegNo <= X86::YMM15) ||
         (RegNo >= X86::YMM16 && RegNo <= X86::YMM31);
}

/// \returns true if the register is a ZMM.
inline bool isZMMReg(unsigned RegNo) {
  static_assert(X86::ZMM31 - X86::ZMM0 == 31,
                "ZMM registers are not continuous");
  return RegNo >= X86::ZMM0 && RegNo <= X86::ZMM31;
}

/// \returns true if \p RegNo is an apx extended register.
inline bool isApxExtendedReg(unsigned RegNo) {
  static_assert(X86::R31WH - X86::R16 == 95, "EGPRs are not continuous");
  return RegNo >= X86::R16 && RegNo <= X86::R31WH;
}

/// \returns true if the MachineOperand is a x86-64 extended (r8 or
/// higher) register,  e.g. r8, xmm8, xmm13, etc.
inline bool isX86_64ExtendedReg(unsigned RegNo) {
  if ((RegNo >= X86::XMM8 && RegNo <= X86::XMM15) ||
      (RegNo >= X86::XMM16 && RegNo <= X86::XMM31) ||
      (RegNo >= X86::YMM8 && RegNo <= X86::YMM15) ||
      (RegNo >= X86::YMM16 && RegNo <= X86::YMM31) ||
      (RegNo >= X86::ZMM8 && RegNo <= X86::ZMM31))
    return true;

  if (isApxExtendedReg(RegNo))
    return true;

  switch (RegNo) {
  default:
    break;
  case X86::R8:
  case X86::R9:
  case X86::R10:
  case X86::R11:
  case X86::R12:
  case X86::R13:
  case X86::R14:
  case X86::R15:
  case X86::R8D:
  case X86::R9D:
  case X86::R10D:
  case X86::R11D:
  case X86::R12D:
  case X86::R13D:
  case X86::R14D:
  case X86::R15D:
  case X86::R8W:
  case X86::R9W:
  case X86::R10W:
  case X86::R11W:
  case X86::R12W:
  case X86::R13W:
  case X86::R14W:
  case X86::R15W:
  case X86::R8B:
  case X86::R9B:
  case X86::R10B:
  case X86::R11B:
  case X86::R12B:
  case X86::R13B:
  case X86::R14B:
  case X86::R15B:
  case X86::CR8:
  case X86::CR9:
  case X86::CR10:
  case X86::CR11:
  case X86::CR12:
  case X86::CR13:
  case X86::CR14:
  case X86::CR15:
  case X86::DR8:
  case X86::DR9:
  case X86::DR10:
  case X86::DR11:
  case X86::DR12:
  case X86::DR13:
  case X86::DR14:
  case X86::DR15:
    return true;
  }
  return false;
}

inline bool canUseApxExtendedReg(const MCInstrDesc &Desc) {
  uint64_t TSFlags = Desc.TSFlags;
  uint64_t Encoding = TSFlags & EncodingMask;
  // EVEX can always use egpr.
  if (Encoding == X86II::EVEX)
    return true;

  unsigned Opcode = Desc.Opcode;
  // MOV32r0 is always expanded to XOR32rr
  if (Opcode == X86::MOV32r0)
    return true;
  // To be conservative, egpr is not used for all pseudo instructions
  // because we are not sure what instruction it will become.
  // FIXME: Could we improve it in X86ExpandPseudo?
  if (isPseudo(TSFlags))
    return false;

  // MAP OB/TB in legacy encoding space can always use egpr except
  // XSAVE*/XRSTOR*.
  switch (Opcode) {
  default:
    break;
  case X86::XSAVE:
  case X86::XSAVE64:
  case X86::XSAVEOPT:
  case X86::XSAVEOPT64:
  case X86::XSAVEC:
  case X86::XSAVEC64:
  case X86::XSAVES:
  case X86::XSAVES64:
  case X86::XRSTOR:
  case X86::XRSTOR64:
  case X86::XRSTORS:
  case X86::XRSTORS64:
    return false;
  }
  uint64_t OpMap = TSFlags & X86II::OpMapMask;
  return !Encoding && (OpMap == X86II::OB || OpMap == X86II::TB);
}

/// \returns true if the MemoryOperand is a 32 extended (zmm16 or higher)
/// registers, e.g. zmm21, etc.
static inline bool is32ExtendedReg(unsigned RegNo) {
  return ((RegNo >= X86::XMM16 && RegNo <= X86::XMM31) ||
          (RegNo >= X86::YMM16 && RegNo <= X86::YMM31) ||
          (RegNo >= X86::ZMM16 && RegNo <= X86::ZMM31));
}

inline bool isX86_64NonExtLowByteReg(unsigned reg) {
  return (reg == X86::SPL || reg == X86::BPL || reg == X86::SIL ||
          reg == X86::DIL);
}

/// \returns true if this is a masked instruction.
inline bool isKMasked(uint64_t TSFlags) {
  return (TSFlags & X86II::EVEX_K) != 0;
}

/// \returns true if this is a merge masked instruction.
inline bool isKMergeMasked(uint64_t TSFlags) {
  return isKMasked(TSFlags) && (TSFlags & X86II::EVEX_Z) == 0;
}

/// \returns true if the intruction needs a SIB.
inline bool needSIB(unsigned BaseReg, unsigned IndexReg, bool In64BitMode) {
  // The SIB byte must be used if there is an index register.
  if (IndexReg)
    return true;

  // The SIB byte must be used if the base is ESP/RSP/R12/R20/R28, all of
  // which encode to an R/M value of 4, which indicates that a SIB byte is
  // present.
  switch (BaseReg) {
  default:
    // If there is no base register and we're in 64-bit mode, we need a SIB
    // byte to emit an addr that is just 'disp32' (the non-RIP relative form).
    return In64BitMode && !BaseReg;
  case X86::ESP:
  case X86::RSP:
  case X86::R12:
  case X86::R12D:
  case X86::R20:
  case X86::R20D:
  case X86::R28:
  case X86::R28D:
    return true;
  }
}

} // namespace X86II
} // namespace llvm
#endif
