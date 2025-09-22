//===-- X86Disassembler.cpp - Disassembler for x86 and x86_64 -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is part of the X86 Disassembler.
// It contains code to translate the data produced by the decoder into
//  MCInsts.
//
//
// The X86 disassembler is a table-driven disassembler for the 16-, 32-, and
// 64-bit X86 instruction sets.  The main decode sequence for an assembly
// instruction in this disassembler is:
//
// 1. Read the prefix bytes and determine the attributes of the instruction.
//    These attributes, recorded in enum attributeBits
//    (X86DisassemblerDecoderCommon.h), form a bitmask.  The table CONTEXTS_SYM
//    provides a mapping from bitmasks to contexts, which are represented by
//    enum InstructionContext (ibid.).
//
// 2. Read the opcode, and determine what kind of opcode it is.  The
//    disassembler distinguishes four kinds of opcodes, which are enumerated in
//    OpcodeType (X86DisassemblerDecoderCommon.h): one-byte (0xnn), two-byte
//    (0x0f 0xnn), three-byte-38 (0x0f 0x38 0xnn), or three-byte-3a
//    (0x0f 0x3a 0xnn).  Mandatory prefixes are treated as part of the context.
//
// 3. Depending on the opcode type, look in one of four ClassDecision structures
//    (X86DisassemblerDecoderCommon.h).  Use the opcode class to determine which
//    OpcodeDecision (ibid.) to look the opcode in.  Look up the opcode, to get
//    a ModRMDecision (ibid.).
//
// 4. Some instructions, such as escape opcodes or extended opcodes, or even
//    instructions that have ModRM*Reg / ModRM*Mem forms in LLVM, need the
//    ModR/M byte to complete decode.  The ModRMDecision's type is an entry from
//    ModRMDecisionType (X86DisassemblerDecoderCommon.h) that indicates if the
//    ModR/M byte is required and how to interpret it.
//
// 5. After resolving the ModRMDecision, the disassembler has a unique ID
//    of type InstrUID (X86DisassemblerDecoderCommon.h).  Looking this ID up in
//    INSTRUCTIONS_SYM yields the name of the instruction and the encodings and
//    meanings of its operands.
//
// 6. For each operand, its encoding is an entry from OperandEncoding
//    (X86DisassemblerDecoderCommon.h) and its type is an entry from
//    OperandType (ibid.).  The encoding indicates how to read it from the
//    instruction; the type indicates how to interpret the value once it has
//    been read.  For example, a register operand could be stored in the R/M
//    field of the ModR/M byte, the REG field of the ModR/M byte, or added to
//    the main opcode.  This is orthogonal from its meaning (an GPR or an XMM
//    register, for instance).  Given this information, the operands can be
//    extracted and interpreted.
//
// 7. As the last step, the disassembler translates the instruction information
//    and operands into a format understandable by the client - in this case, an
//    MCInst for use by the MC infrastructure.
//
// The disassembler is broken broadly into two parts: the table emitter that
// emits the instruction decode tables discussed above during compilation, and
// the disassembler itself.  The table emitter is documented in more detail in
// utils/TableGen/X86DisassemblerEmitter.h.
//
// X86Disassembler.cpp contains the code responsible for step 7, and for
//   invoking the decoder to execute steps 1-6.
// X86DisassemblerDecoderCommon.h contains the definitions needed by both the
//   table emitter and the disassembler.
// X86DisassemblerDecoder.h contains the public interface of the decoder,
//   factored out into C for possible use by other projects.
// X86DisassemblerDecoder.c contains the source code of the decoder, which is
//   responsible for steps 1-6.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86MCTargetDesc.h"
#include "TargetInfo/X86TargetInfo.h"
#include "X86DisassemblerDecoder.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::X86Disassembler;

#define DEBUG_TYPE "x86-disassembler"

#define debug(s) LLVM_DEBUG(dbgs() << __LINE__ << ": " << s);

// Specifies whether a ModR/M byte is needed and (if so) which
// instruction each possible value of the ModR/M byte corresponds to.  Once
// this information is known, we have narrowed down to a single instruction.
struct ModRMDecision {
  uint8_t modrm_type;
  uint16_t instructionIDs;
};

// Specifies which set of ModR/M->instruction tables to look at
// given a particular opcode.
struct OpcodeDecision {
  ModRMDecision modRMDecisions[256];
};

// Specifies which opcode->instruction tables to look at given
// a particular context (set of attributes).  Since there are many possible
// contexts, the decoder first uses CONTEXTS_SYM to determine which context
// applies given a specific set of attributes.  Hence there are only IC_max
// entries in this table, rather than 2^(ATTR_max).
struct ContextDecision {
  OpcodeDecision opcodeDecisions[IC_max];
};

#include "X86GenDisassemblerTables.inc"

static InstrUID decode(OpcodeType type, InstructionContext insnContext,
                       uint8_t opcode, uint8_t modRM) {
  const struct ModRMDecision *dec;

  switch (type) {
  case ONEBYTE:
    dec = &ONEBYTE_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case TWOBYTE:
    dec = &TWOBYTE_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEBYTE_38:
    dec = &THREEBYTE38_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEBYTE_3A:
    dec = &THREEBYTE3A_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOP8_MAP:
    dec = &XOP8_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOP9_MAP:
    dec = &XOP9_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case XOPA_MAP:
    dec = &XOPA_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case THREEDNOW_MAP:
    dec =
        &THREEDNOW_MAP_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case MAP4:
    dec = &MAP4_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case MAP5:
    dec = &MAP5_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case MAP6:
    dec = &MAP6_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  case MAP7:
    dec = &MAP7_SYM.opcodeDecisions[insnContext].modRMDecisions[opcode];
    break;
  }

  switch (dec->modrm_type) {
  default:
    llvm_unreachable("Corrupt table!  Unknown modrm_type");
    return 0;
  case MODRM_ONEENTRY:
    return modRMTable[dec->instructionIDs];
  case MODRM_SPLITRM:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs + 1];
    return modRMTable[dec->instructionIDs];
  case MODRM_SPLITREG:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs + ((modRM & 0x38) >> 3) + 8];
    return modRMTable[dec->instructionIDs + ((modRM & 0x38) >> 3)];
  case MODRM_SPLITMISC:
    if (modFromModRM(modRM) == 0x3)
      return modRMTable[dec->instructionIDs + (modRM & 0x3f) + 8];
    return modRMTable[dec->instructionIDs + ((modRM & 0x38) >> 3)];
  case MODRM_FULL:
    return modRMTable[dec->instructionIDs + modRM];
  }
}

static bool peek(struct InternalInstruction *insn, uint8_t &byte) {
  uint64_t offset = insn->readerCursor - insn->startLocation;
  if (offset >= insn->bytes.size())
    return true;
  byte = insn->bytes[offset];
  return false;
}

template <typename T> static bool consume(InternalInstruction *insn, T &ptr) {
  auto r = insn->bytes;
  uint64_t offset = insn->readerCursor - insn->startLocation;
  if (offset + sizeof(T) > r.size())
    return true;
  ptr = support::endian::read<T>(&r[offset], llvm::endianness::little);
  insn->readerCursor += sizeof(T);
  return false;
}

static bool isREX(struct InternalInstruction *insn, uint8_t prefix) {
  return insn->mode == MODE_64BIT && prefix >= 0x40 && prefix <= 0x4f;
}

static bool isREX2(struct InternalInstruction *insn, uint8_t prefix) {
  return insn->mode == MODE_64BIT && prefix == 0xd5;
}

// Consumes all of an instruction's prefix bytes, and marks the
// instruction as having them.  Also sets the instruction's default operand,
// address, and other relevant data sizes to report operands correctly.
//
// insn must not be empty.
static int readPrefixes(struct InternalInstruction *insn) {
  bool isPrefix = true;
  uint8_t byte = 0;
  uint8_t nextByte;

  LLVM_DEBUG(dbgs() << "readPrefixes()");

  while (isPrefix) {
    // If we fail reading prefixes, just stop here and let the opcode reader
    // deal with it.
    if (consume(insn, byte))
      break;

    // If the byte is a LOCK/REP/REPNE prefix and not a part of the opcode, then
    // break and let it be disassembled as a normal "instruction".
    if (insn->readerCursor - 1 == insn->startLocation && byte == 0xf0) // LOCK
      break;

    if ((byte == 0xf2 || byte == 0xf3) && !peek(insn, nextByte)) {
      // If the byte is 0xf2 or 0xf3, and any of the following conditions are
      // met:
      // - it is followed by a LOCK (0xf0) prefix
      // - it is followed by an xchg instruction
      // then it should be disassembled as a xacquire/xrelease not repne/rep.
      if (((nextByte == 0xf0) ||
           ((nextByte & 0xfe) == 0x86 || (nextByte & 0xf8) == 0x90))) {
        insn->xAcquireRelease = true;
        if (!(byte == 0xf3 && nextByte == 0x90)) // PAUSE instruction support
          break;
      }
      // Also if the byte is 0xf3, and the following condition is met:
      // - it is followed by a "mov mem, reg" (opcode 0x88/0x89) or
      //                       "mov mem, imm" (opcode 0xc6/0xc7) instructions.
      // then it should be disassembled as an xrelease not rep.
      if (byte == 0xf3 && (nextByte == 0x88 || nextByte == 0x89 ||
                           nextByte == 0xc6 || nextByte == 0xc7)) {
        insn->xAcquireRelease = true;
        break;
      }
      if (isREX(insn, nextByte)) {
        uint8_t nnextByte;
        // Go to REX prefix after the current one
        if (consume(insn, nnextByte))
          return -1;
        // We should be able to read next byte after REX prefix
        if (peek(insn, nnextByte))
          return -1;
        --insn->readerCursor;
      }
    }

    switch (byte) {
    case 0xf0: // LOCK
      insn->hasLockPrefix = true;
      break;
    case 0xf2: // REPNE/REPNZ
    case 0xf3: { // REP or REPE/REPZ
      uint8_t nextByte;
      if (peek(insn, nextByte))
        break;
      // TODO:
      //  1. There could be several 0x66
      //  2. if (nextByte == 0x66) and nextNextByte != 0x0f then
      //      it's not mandatory prefix
      //  3. if (nextByte >= 0x40 && nextByte <= 0x4f) it's REX and we need
      //     0x0f exactly after it to be mandatory prefix
      //  4. if (nextByte == 0xd5) it's REX2 and we need
      //     0x0f exactly after it to be mandatory prefix
      if (isREX(insn, nextByte) || isREX2(insn, nextByte) || nextByte == 0x0f ||
          nextByte == 0x66)
        // The last of 0xf2 /0xf3 is mandatory prefix
        insn->mandatoryPrefix = byte;
      insn->repeatPrefix = byte;
      break;
    }
    case 0x2e: // CS segment override -OR- Branch not taken
      insn->segmentOverride = SEG_OVERRIDE_CS;
      break;
    case 0x36: // SS segment override -OR- Branch taken
      insn->segmentOverride = SEG_OVERRIDE_SS;
      break;
    case 0x3e: // DS segment override
      insn->segmentOverride = SEG_OVERRIDE_DS;
      break;
    case 0x26: // ES segment override
      insn->segmentOverride = SEG_OVERRIDE_ES;
      break;
    case 0x64: // FS segment override
      insn->segmentOverride = SEG_OVERRIDE_FS;
      break;
    case 0x65: // GS segment override
      insn->segmentOverride = SEG_OVERRIDE_GS;
      break;
    case 0x66: { // Operand-size override {
      uint8_t nextByte;
      insn->hasOpSize = true;
      if (peek(insn, nextByte))
        break;
      // 0x66 can't overwrite existing mandatory prefix and should be ignored
      if (!insn->mandatoryPrefix && (nextByte == 0x0f || isREX(insn, nextByte)))
        insn->mandatoryPrefix = byte;
      break;
    }
    case 0x67: // Address-size override
      insn->hasAdSize = true;
      break;
    default: // Not a prefix byte
      isPrefix = false;
      break;
    }

    if (isPrefix)
      LLVM_DEBUG(dbgs() << format("Found prefix 0x%hhx", byte));
  }

  insn->vectorExtensionType = TYPE_NO_VEX_XOP;

  if (byte == 0x62) {
    uint8_t byte1, byte2;
    if (consume(insn, byte1)) {
      LLVM_DEBUG(dbgs() << "Couldn't read second byte of EVEX prefix");
      return -1;
    }

    if (peek(insn, byte2)) {
      LLVM_DEBUG(dbgs() << "Couldn't read third byte of EVEX prefix");
      return -1;
    }

    if ((insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0)) {
      insn->vectorExtensionType = TYPE_EVEX;
    } else {
      --insn->readerCursor; // unconsume byte1
      --insn->readerCursor; // unconsume byte
    }

    if (insn->vectorExtensionType == TYPE_EVEX) {
      insn->vectorExtensionPrefix[0] = byte;
      insn->vectorExtensionPrefix[1] = byte1;
      if (consume(insn, insn->vectorExtensionPrefix[2])) {
        LLVM_DEBUG(dbgs() << "Couldn't read third byte of EVEX prefix");
        return -1;
      }
      if (consume(insn, insn->vectorExtensionPrefix[3])) {
        LLVM_DEBUG(dbgs() << "Couldn't read fourth byte of EVEX prefix");
        return -1;
      }

      if (insn->mode == MODE_64BIT) {
        // We simulate the REX prefix for simplicity's sake
        insn->rexPrefix = 0x40 |
                          (wFromEVEX3of4(insn->vectorExtensionPrefix[2]) << 3) |
                          (rFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 2) |
                          (xFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 1) |
                          (bFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 0);

        // We simulate the REX2 prefix for simplicity's sake
        insn->rex2ExtensionPrefix[1] =
            (r2FromEVEX2of4(insn->vectorExtensionPrefix[1]) << 6) |
            (x2FromEVEX3of4(insn->vectorExtensionPrefix[2]) << 5) |
            (b2FromEVEX2of4(insn->vectorExtensionPrefix[1]) << 4);
      }

      LLVM_DEBUG(
          dbgs() << format(
              "Found EVEX prefix 0x%hhx 0x%hhx 0x%hhx 0x%hhx",
              insn->vectorExtensionPrefix[0], insn->vectorExtensionPrefix[1],
              insn->vectorExtensionPrefix[2], insn->vectorExtensionPrefix[3]));
    }
  } else if (byte == 0xc4) {
    uint8_t byte1;
    if (peek(insn, byte1)) {
      LLVM_DEBUG(dbgs() << "Couldn't read second byte of VEX");
      return -1;
    }

    if (insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0)
      insn->vectorExtensionType = TYPE_VEX_3B;
    else
      --insn->readerCursor;

    if (insn->vectorExtensionType == TYPE_VEX_3B) {
      insn->vectorExtensionPrefix[0] = byte;
      consume(insn, insn->vectorExtensionPrefix[1]);
      consume(insn, insn->vectorExtensionPrefix[2]);

      // We simulate the REX prefix for simplicity's sake

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix = 0x40 |
                          (wFromVEX3of3(insn->vectorExtensionPrefix[2]) << 3) |
                          (rFromVEX2of3(insn->vectorExtensionPrefix[1]) << 2) |
                          (xFromVEX2of3(insn->vectorExtensionPrefix[1]) << 1) |
                          (bFromVEX2of3(insn->vectorExtensionPrefix[1]) << 0);

      LLVM_DEBUG(dbgs() << format("Found VEX prefix 0x%hhx 0x%hhx 0x%hhx",
                                  insn->vectorExtensionPrefix[0],
                                  insn->vectorExtensionPrefix[1],
                                  insn->vectorExtensionPrefix[2]));
    }
  } else if (byte == 0xc5) {
    uint8_t byte1;
    if (peek(insn, byte1)) {
      LLVM_DEBUG(dbgs() << "Couldn't read second byte of VEX");
      return -1;
    }

    if (insn->mode == MODE_64BIT || (byte1 & 0xc0) == 0xc0)
      insn->vectorExtensionType = TYPE_VEX_2B;
    else
      --insn->readerCursor;

    if (insn->vectorExtensionType == TYPE_VEX_2B) {
      insn->vectorExtensionPrefix[0] = byte;
      consume(insn, insn->vectorExtensionPrefix[1]);

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix =
            0x40 | (rFromVEX2of2(insn->vectorExtensionPrefix[1]) << 2);

      switch (ppFromVEX2of2(insn->vectorExtensionPrefix[1])) {
      default:
        break;
      case VEX_PREFIX_66:
        insn->hasOpSize = true;
        break;
      }

      LLVM_DEBUG(dbgs() << format("Found VEX prefix 0x%hhx 0x%hhx",
                                  insn->vectorExtensionPrefix[0],
                                  insn->vectorExtensionPrefix[1]));
    }
  } else if (byte == 0x8f) {
    uint8_t byte1;
    if (peek(insn, byte1)) {
      LLVM_DEBUG(dbgs() << "Couldn't read second byte of XOP");
      return -1;
    }

    if ((byte1 & 0x38) != 0x0) // 0 in these 3 bits is a POP instruction.
      insn->vectorExtensionType = TYPE_XOP;
    else
      --insn->readerCursor;

    if (insn->vectorExtensionType == TYPE_XOP) {
      insn->vectorExtensionPrefix[0] = byte;
      consume(insn, insn->vectorExtensionPrefix[1]);
      consume(insn, insn->vectorExtensionPrefix[2]);

      // We simulate the REX prefix for simplicity's sake

      if (insn->mode == MODE_64BIT)
        insn->rexPrefix = 0x40 |
                          (wFromXOP3of3(insn->vectorExtensionPrefix[2]) << 3) |
                          (rFromXOP2of3(insn->vectorExtensionPrefix[1]) << 2) |
                          (xFromXOP2of3(insn->vectorExtensionPrefix[1]) << 1) |
                          (bFromXOP2of3(insn->vectorExtensionPrefix[1]) << 0);

      switch (ppFromXOP3of3(insn->vectorExtensionPrefix[2])) {
      default:
        break;
      case VEX_PREFIX_66:
        insn->hasOpSize = true;
        break;
      }

      LLVM_DEBUG(dbgs() << format("Found XOP prefix 0x%hhx 0x%hhx 0x%hhx",
                                  insn->vectorExtensionPrefix[0],
                                  insn->vectorExtensionPrefix[1],
                                  insn->vectorExtensionPrefix[2]));
    }
  } else if (isREX2(insn, byte)) {
    uint8_t byte1;
    if (peek(insn, byte1)) {
      LLVM_DEBUG(dbgs() << "Couldn't read second byte of REX2");
      return -1;
    }
    insn->rex2ExtensionPrefix[0] = byte;
    consume(insn, insn->rex2ExtensionPrefix[1]);

    // We simulate the REX prefix for simplicity's sake
    insn->rexPrefix = 0x40 | (wFromREX2(insn->rex2ExtensionPrefix[1]) << 3) |
                      (rFromREX2(insn->rex2ExtensionPrefix[1]) << 2) |
                      (xFromREX2(insn->rex2ExtensionPrefix[1]) << 1) |
                      (bFromREX2(insn->rex2ExtensionPrefix[1]) << 0);
    LLVM_DEBUG(dbgs() << format("Found REX2 prefix 0x%hhx 0x%hhx",
                                insn->rex2ExtensionPrefix[0],
                                insn->rex2ExtensionPrefix[1]));
  } else if (isREX(insn, byte)) {
    if (peek(insn, nextByte))
      return -1;
    insn->rexPrefix = byte;
    LLVM_DEBUG(dbgs() << format("Found REX prefix 0x%hhx", byte));
  } else
    --insn->readerCursor;

  if (insn->mode == MODE_16BIT) {
    insn->registerSize = (insn->hasOpSize ? 4 : 2);
    insn->addressSize = (insn->hasAdSize ? 4 : 2);
    insn->displacementSize = (insn->hasAdSize ? 4 : 2);
    insn->immediateSize = (insn->hasOpSize ? 4 : 2);
  } else if (insn->mode == MODE_32BIT) {
    insn->registerSize = (insn->hasOpSize ? 2 : 4);
    insn->addressSize = (insn->hasAdSize ? 2 : 4);
    insn->displacementSize = (insn->hasAdSize ? 2 : 4);
    insn->immediateSize = (insn->hasOpSize ? 2 : 4);
  } else if (insn->mode == MODE_64BIT) {
    insn->displacementSize = 4;
    if (insn->rexPrefix && wFromREX(insn->rexPrefix)) {
      insn->registerSize = 8;
      insn->addressSize = (insn->hasAdSize ? 4 : 8);
      insn->immediateSize = 4;
      insn->hasOpSize = false;
    } else {
      insn->registerSize = (insn->hasOpSize ? 2 : 4);
      insn->addressSize = (insn->hasAdSize ? 4 : 8);
      insn->immediateSize = (insn->hasOpSize ? 2 : 4);
    }
  }

  return 0;
}

// Consumes the SIB byte to determine addressing information.
static int readSIB(struct InternalInstruction *insn) {
  SIBBase sibBaseBase = SIB_BASE_NONE;
  uint8_t index, base;

  LLVM_DEBUG(dbgs() << "readSIB()");
  switch (insn->addressSize) {
  case 2:
  default:
    llvm_unreachable("SIB-based addressing doesn't work in 16-bit mode");
  case 4:
    insn->sibIndexBase = SIB_INDEX_EAX;
    sibBaseBase = SIB_BASE_EAX;
    break;
  case 8:
    insn->sibIndexBase = SIB_INDEX_RAX;
    sibBaseBase = SIB_BASE_RAX;
    break;
  }

  if (consume(insn, insn->sib))
    return -1;

  index = indexFromSIB(insn->sib) | (xFromREX(insn->rexPrefix) << 3) |
          (x2FromREX2(insn->rex2ExtensionPrefix[1]) << 4);

  if (index == 0x4) {
    insn->sibIndex = SIB_INDEX_NONE;
  } else {
    insn->sibIndex = (SIBIndex)(insn->sibIndexBase + index);
  }

  insn->sibScale = 1 << scaleFromSIB(insn->sib);

  base = baseFromSIB(insn->sib) | (bFromREX(insn->rexPrefix) << 3) |
         (b2FromREX2(insn->rex2ExtensionPrefix[1]) << 4);

  switch (base) {
  case 0x5:
  case 0xd:
    switch (modFromModRM(insn->modRM)) {
    case 0x0:
      insn->eaDisplacement = EA_DISP_32;
      insn->sibBase = SIB_BASE_NONE;
      break;
    case 0x1:
      insn->eaDisplacement = EA_DISP_8;
      insn->sibBase = (SIBBase)(sibBaseBase + base);
      break;
    case 0x2:
      insn->eaDisplacement = EA_DISP_32;
      insn->sibBase = (SIBBase)(sibBaseBase + base);
      break;
    default:
      llvm_unreachable("Cannot have Mod = 0b11 and a SIB byte");
    }
    break;
  default:
    insn->sibBase = (SIBBase)(sibBaseBase + base);
    break;
  }

  return 0;
}

static int readDisplacement(struct InternalInstruction *insn) {
  int8_t d8;
  int16_t d16;
  int32_t d32;
  LLVM_DEBUG(dbgs() << "readDisplacement()");

  insn->displacementOffset = insn->readerCursor - insn->startLocation;
  switch (insn->eaDisplacement) {
  case EA_DISP_NONE:
    break;
  case EA_DISP_8:
    if (consume(insn, d8))
      return -1;
    insn->displacement = d8;
    break;
  case EA_DISP_16:
    if (consume(insn, d16))
      return -1;
    insn->displacement = d16;
    break;
  case EA_DISP_32:
    if (consume(insn, d32))
      return -1;
    insn->displacement = d32;
    break;
  }

  return 0;
}

// Consumes all addressing information (ModR/M byte, SIB byte, and displacement.
static int readModRM(struct InternalInstruction *insn) {
  uint8_t mod, rm, reg;
  LLVM_DEBUG(dbgs() << "readModRM()");

  if (insn->consumedModRM)
    return 0;

  if (consume(insn, insn->modRM))
    return -1;
  insn->consumedModRM = true;

  mod = modFromModRM(insn->modRM);
  rm = rmFromModRM(insn->modRM);
  reg = regFromModRM(insn->modRM);

  // This goes by insn->registerSize to pick the correct register, which messes
  // up if we're using (say) XMM or 8-bit register operands. That gets fixed in
  // fixupReg().
  switch (insn->registerSize) {
  case 2:
    insn->regBase = MODRM_REG_AX;
    insn->eaRegBase = EA_REG_AX;
    break;
  case 4:
    insn->regBase = MODRM_REG_EAX;
    insn->eaRegBase = EA_REG_EAX;
    break;
  case 8:
    insn->regBase = MODRM_REG_RAX;
    insn->eaRegBase = EA_REG_RAX;
    break;
  }

  reg |= (rFromREX(insn->rexPrefix) << 3) |
         (r2FromREX2(insn->rex2ExtensionPrefix[1]) << 4);
  rm |= (bFromREX(insn->rexPrefix) << 3) |
        (b2FromREX2(insn->rex2ExtensionPrefix[1]) << 4);

  if (insn->vectorExtensionType == TYPE_EVEX && insn->mode == MODE_64BIT)
    reg |= r2FromEVEX2of4(insn->vectorExtensionPrefix[1]) << 4;

  insn->reg = (Reg)(insn->regBase + reg);

  switch (insn->addressSize) {
  case 2: {
    EABase eaBaseBase = EA_BASE_BX_SI;

    switch (mod) {
    case 0x0:
      if (rm == 0x6) {
        insn->eaBase = EA_BASE_NONE;
        insn->eaDisplacement = EA_DISP_16;
        if (readDisplacement(insn))
          return -1;
      } else {
        insn->eaBase = (EABase)(eaBaseBase + rm);
        insn->eaDisplacement = EA_DISP_NONE;
      }
      break;
    case 0x1:
      insn->eaBase = (EABase)(eaBaseBase + rm);
      insn->eaDisplacement = EA_DISP_8;
      insn->displacementSize = 1;
      if (readDisplacement(insn))
        return -1;
      break;
    case 0x2:
      insn->eaBase = (EABase)(eaBaseBase + rm);
      insn->eaDisplacement = EA_DISP_16;
      if (readDisplacement(insn))
        return -1;
      break;
    case 0x3:
      insn->eaBase = (EABase)(insn->eaRegBase + rm);
      if (readDisplacement(insn))
        return -1;
      break;
    }
    break;
  }
  case 4:
  case 8: {
    EABase eaBaseBase = (insn->addressSize == 4 ? EA_BASE_EAX : EA_BASE_RAX);

    switch (mod) {
    case 0x0:
      insn->eaDisplacement = EA_DISP_NONE; // readSIB may override this
      // In determining whether RIP-relative mode is used (rm=5),
      // or whether a SIB byte is present (rm=4),
      // the extension bits (REX.b and EVEX.x) are ignored.
      switch (rm & 7) {
      case 0x4: // SIB byte is present
        insn->eaBase = (insn->addressSize == 4 ? EA_BASE_sib : EA_BASE_sib64);
        if (readSIB(insn) || readDisplacement(insn))
          return -1;
        break;
      case 0x5: // RIP-relative
        insn->eaBase = EA_BASE_NONE;
        insn->eaDisplacement = EA_DISP_32;
        if (readDisplacement(insn))
          return -1;
        break;
      default:
        insn->eaBase = (EABase)(eaBaseBase + rm);
        break;
      }
      break;
    case 0x1:
      insn->displacementSize = 1;
      [[fallthrough]];
    case 0x2:
      insn->eaDisplacement = (mod == 0x1 ? EA_DISP_8 : EA_DISP_32);
      switch (rm & 7) {
      case 0x4: // SIB byte is present
        insn->eaBase = EA_BASE_sib;
        if (readSIB(insn) || readDisplacement(insn))
          return -1;
        break;
      default:
        insn->eaBase = (EABase)(eaBaseBase + rm);
        if (readDisplacement(insn))
          return -1;
        break;
      }
      break;
    case 0x3:
      insn->eaDisplacement = EA_DISP_NONE;
      insn->eaBase = (EABase)(insn->eaRegBase + rm);
      break;
    }
    break;
  }
  } // switch (insn->addressSize)

  return 0;
}

#define GENERIC_FIXUP_FUNC(name, base, prefix)                                 \
  static uint16_t name(struct InternalInstruction *insn, OperandType type,     \
                       uint8_t index, uint8_t *valid) {                        \
    *valid = 1;                                                                \
    switch (type) {                                                            \
    default:                                                                   \
      debug("Unhandled register type");                                        \
      *valid = 0;                                                              \
      return 0;                                                                \
    case TYPE_Rv:                                                              \
      return base + index;                                                     \
    case TYPE_R8:                                                              \
      if (insn->rexPrefix && index >= 4 && index <= 7)                         \
        return prefix##_SPL + (index - 4);                                     \
      else                                                                     \
        return prefix##_AL + index;                                            \
    case TYPE_R16:                                                             \
      return prefix##_AX + index;                                              \
    case TYPE_R32:                                                             \
      return prefix##_EAX + index;                                             \
    case TYPE_R64:                                                             \
      return prefix##_RAX + index;                                             \
    case TYPE_ZMM:                                                             \
      return prefix##_ZMM0 + index;                                            \
    case TYPE_YMM:                                                             \
      return prefix##_YMM0 + index;                                            \
    case TYPE_XMM:                                                             \
      return prefix##_XMM0 + index;                                            \
    case TYPE_TMM:                                                             \
      if (index > 7)                                                           \
        *valid = 0;                                                            \
      return prefix##_TMM0 + index;                                            \
    case TYPE_VK:                                                              \
      index &= 0xf;                                                            \
      if (index > 7)                                                           \
        *valid = 0;                                                            \
      return prefix##_K0 + index;                                              \
    case TYPE_VK_PAIR:                                                         \
      if (index > 7)                                                           \
        *valid = 0;                                                            \
      return prefix##_K0_K1 + (index / 2);                                     \
    case TYPE_MM64:                                                            \
      return prefix##_MM0 + (index & 0x7);                                     \
    case TYPE_SEGMENTREG:                                                      \
      if ((index & 7) > 5)                                                     \
        *valid = 0;                                                            \
      return prefix##_ES + (index & 7);                                        \
    case TYPE_DEBUGREG:                                                        \
      if (index > 15)                                                          \
        *valid = 0;                                                            \
      return prefix##_DR0 + index;                                             \
    case TYPE_CONTROLREG:                                                      \
      if (index > 15)                                                          \
        *valid = 0;                                                            \
      return prefix##_CR0 + index;                                             \
    case TYPE_MVSIBX:                                                          \
      return prefix##_XMM0 + index;                                            \
    case TYPE_MVSIBY:                                                          \
      return prefix##_YMM0 + index;                                            \
    case TYPE_MVSIBZ:                                                          \
      return prefix##_ZMM0 + index;                                            \
    }                                                                          \
  }

// Consult an operand type to determine the meaning of the reg or R/M field. If
// the operand is an XMM operand, for example, an operand would be XMM0 instead
// of AX, which readModRM() would otherwise misinterpret it as.
//
// @param insn  - The instruction containing the operand.
// @param type  - The operand type.
// @param index - The existing value of the field as reported by readModRM().
// @param valid - The address of a uint8_t.  The target is set to 1 if the
//                field is valid for the register class; 0 if not.
// @return      - The proper value.
GENERIC_FIXUP_FUNC(fixupRegValue, insn->regBase, MODRM_REG)
GENERIC_FIXUP_FUNC(fixupRMValue, insn->eaRegBase, EA_REG)

// Consult an operand specifier to determine which of the fixup*Value functions
// to use in correcting readModRM()'ss interpretation.
//
// @param insn  - See fixup*Value().
// @param op    - The operand specifier.
// @return      - 0 if fixup was successful; -1 if the register returned was
//                invalid for its class.
static int fixupReg(struct InternalInstruction *insn,
                    const struct OperandSpecifier *op) {
  uint8_t valid;
  LLVM_DEBUG(dbgs() << "fixupReg()");

  switch ((OperandEncoding)op->encoding) {
  default:
    debug("Expected a REG or R/M encoding in fixupReg");
    return -1;
  case ENCODING_VVVV:
    insn->vvvv =
        (Reg)fixupRegValue(insn, (OperandType)op->type, insn->vvvv, &valid);
    if (!valid)
      return -1;
    break;
  case ENCODING_REG:
    insn->reg = (Reg)fixupRegValue(insn, (OperandType)op->type,
                                   insn->reg - insn->regBase, &valid);
    if (!valid)
      return -1;
    break;
  CASE_ENCODING_RM:
    if (insn->vectorExtensionType == TYPE_EVEX && insn->mode == MODE_64BIT &&
        modFromModRM(insn->modRM) == 3) {
      // EVEX_X can extend the register id to 32 for a non-GPR register that is
      // encoded in RM.
      // mode : MODE_64_BIT
      //  Only 8 vector registers are available in 32 bit mode
      // mod : 3
      //  RM encodes a register
      switch (op->type) {
      case TYPE_Rv:
      case TYPE_R8:
      case TYPE_R16:
      case TYPE_R32:
      case TYPE_R64:
        break;
      default:
        insn->eaBase =
            (EABase)(insn->eaBase +
                     (xFromEVEX2of4(insn->vectorExtensionPrefix[1]) << 4));
        break;
      }
    }
    [[fallthrough]];
  case ENCODING_SIB:
    if (insn->eaBase >= insn->eaRegBase) {
      insn->eaBase = (EABase)fixupRMValue(
          insn, (OperandType)op->type, insn->eaBase - insn->eaRegBase, &valid);
      if (!valid)
        return -1;
    }
    break;
  }

  return 0;
}

// Read the opcode (except the ModR/M byte in the case of extended or escape
// opcodes).
static bool readOpcode(struct InternalInstruction *insn) {
  uint8_t current;
  LLVM_DEBUG(dbgs() << "readOpcode()");

  insn->opcodeType = ONEBYTE;
  if (insn->vectorExtensionType == TYPE_EVEX) {
    switch (mmmFromEVEX2of4(insn->vectorExtensionPrefix[1])) {
    default:
      LLVM_DEBUG(
          dbgs() << format("Unhandled mmm field for instruction (0x%hhx)",
                           mmmFromEVEX2of4(insn->vectorExtensionPrefix[1])));
      return true;
    case VEX_LOB_0F:
      insn->opcodeType = TWOBYTE;
      return consume(insn, insn->opcode);
    case VEX_LOB_0F38:
      insn->opcodeType = THREEBYTE_38;
      return consume(insn, insn->opcode);
    case VEX_LOB_0F3A:
      insn->opcodeType = THREEBYTE_3A;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP4:
      insn->opcodeType = MAP4;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP5:
      insn->opcodeType = MAP5;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP6:
      insn->opcodeType = MAP6;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP7:
      insn->opcodeType = MAP7;
      return consume(insn, insn->opcode);
    }
  } else if (insn->vectorExtensionType == TYPE_VEX_3B) {
    switch (mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1])) {
    default:
      LLVM_DEBUG(
          dbgs() << format("Unhandled m-mmmm field for instruction (0x%hhx)",
                           mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1])));
      return true;
    case VEX_LOB_0F:
      insn->opcodeType = TWOBYTE;
      return consume(insn, insn->opcode);
    case VEX_LOB_0F38:
      insn->opcodeType = THREEBYTE_38;
      return consume(insn, insn->opcode);
    case VEX_LOB_0F3A:
      insn->opcodeType = THREEBYTE_3A;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP5:
      insn->opcodeType = MAP5;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP6:
      insn->opcodeType = MAP6;
      return consume(insn, insn->opcode);
    case VEX_LOB_MAP7:
      insn->opcodeType = MAP7;
      return consume(insn, insn->opcode);
    }
  } else if (insn->vectorExtensionType == TYPE_VEX_2B) {
    insn->opcodeType = TWOBYTE;
    return consume(insn, insn->opcode);
  } else if (insn->vectorExtensionType == TYPE_XOP) {
    switch (mmmmmFromXOP2of3(insn->vectorExtensionPrefix[1])) {
    default:
      LLVM_DEBUG(
          dbgs() << format("Unhandled m-mmmm field for instruction (0x%hhx)",
                           mmmmmFromVEX2of3(insn->vectorExtensionPrefix[1])));
      return true;
    case XOP_MAP_SELECT_8:
      insn->opcodeType = XOP8_MAP;
      return consume(insn, insn->opcode);
    case XOP_MAP_SELECT_9:
      insn->opcodeType = XOP9_MAP;
      return consume(insn, insn->opcode);
    case XOP_MAP_SELECT_A:
      insn->opcodeType = XOPA_MAP;
      return consume(insn, insn->opcode);
    }
  } else if (mFromREX2(insn->rex2ExtensionPrefix[1])) {
    // m bit indicates opcode map 1
    insn->opcodeType = TWOBYTE;
    return consume(insn, insn->opcode);
  }

  if (consume(insn, current))
    return true;

  if (current == 0x0f) {
    LLVM_DEBUG(
        dbgs() << format("Found a two-byte escape prefix (0x%hhx)", current));
    if (consume(insn, current))
      return true;

    if (current == 0x38) {
      LLVM_DEBUG(dbgs() << format("Found a three-byte escape prefix (0x%hhx)",
                                  current));
      if (consume(insn, current))
        return true;

      insn->opcodeType = THREEBYTE_38;
    } else if (current == 0x3a) {
      LLVM_DEBUG(dbgs() << format("Found a three-byte escape prefix (0x%hhx)",
                                  current));
      if (consume(insn, current))
        return true;

      insn->opcodeType = THREEBYTE_3A;
    } else if (current == 0x0f) {
      LLVM_DEBUG(
          dbgs() << format("Found a 3dnow escape prefix (0x%hhx)", current));

      // Consume operands before the opcode to comply with the 3DNow encoding
      if (readModRM(insn))
        return true;

      if (consume(insn, current))
        return true;

      insn->opcodeType = THREEDNOW_MAP;
    } else {
      LLVM_DEBUG(dbgs() << "Didn't find a three-byte escape prefix");
      insn->opcodeType = TWOBYTE;
    }
  } else if (insn->mandatoryPrefix)
    // The opcode with mandatory prefix must start with opcode escape.
    // If not it's legacy repeat prefix
    insn->mandatoryPrefix = 0;

  // At this point we have consumed the full opcode.
  // Anything we consume from here on must be unconsumed.
  insn->opcode = current;

  return false;
}

// Determine whether equiv is the 16-bit equivalent of orig (32-bit or 64-bit).
static bool is16BitEquivalent(const char *orig, const char *equiv) {
  for (int i = 0;; i++) {
    if (orig[i] == '\0' && equiv[i] == '\0')
      return true;
    if (orig[i] == '\0' || equiv[i] == '\0')
      return false;
    if (orig[i] != equiv[i]) {
      if ((orig[i] == 'Q' || orig[i] == 'L') && equiv[i] == 'W')
        continue;
      if ((orig[i] == '6' || orig[i] == '3') && equiv[i] == '1')
        continue;
      if ((orig[i] == '4' || orig[i] == '2') && equiv[i] == '6')
        continue;
      return false;
    }
  }
}

// Determine whether this instruction is a 64-bit instruction.
static bool is64Bit(const char *name) {
  for (int i = 0;; ++i) {
    if (name[i] == '\0')
      return false;
    if (name[i] == '6' && name[i + 1] == '4')
      return true;
  }
}

// Determine the ID of an instruction, consuming the ModR/M byte as appropriate
// for extended and escape opcodes, and using a supplied attribute mask.
static int getInstructionIDWithAttrMask(uint16_t *instructionID,
                                        struct InternalInstruction *insn,
                                        uint16_t attrMask) {
  auto insnCtx = InstructionContext(x86DisassemblerContexts[attrMask]);
  const ContextDecision *decision;
  switch (insn->opcodeType) {
  case ONEBYTE:
    decision = &ONEBYTE_SYM;
    break;
  case TWOBYTE:
    decision = &TWOBYTE_SYM;
    break;
  case THREEBYTE_38:
    decision = &THREEBYTE38_SYM;
    break;
  case THREEBYTE_3A:
    decision = &THREEBYTE3A_SYM;
    break;
  case XOP8_MAP:
    decision = &XOP8_MAP_SYM;
    break;
  case XOP9_MAP:
    decision = &XOP9_MAP_SYM;
    break;
  case XOPA_MAP:
    decision = &XOPA_MAP_SYM;
    break;
  case THREEDNOW_MAP:
    decision = &THREEDNOW_MAP_SYM;
    break;
  case MAP4:
    decision = &MAP4_SYM;
    break;
  case MAP5:
    decision = &MAP5_SYM;
    break;
  case MAP6:
    decision = &MAP6_SYM;
    break;
  case MAP7:
    decision = &MAP7_SYM;
    break;
  }

  if (decision->opcodeDecisions[insnCtx]
          .modRMDecisions[insn->opcode]
          .modrm_type != MODRM_ONEENTRY) {
    if (readModRM(insn))
      return -1;
    *instructionID =
        decode(insn->opcodeType, insnCtx, insn->opcode, insn->modRM);
  } else {
    *instructionID = decode(insn->opcodeType, insnCtx, insn->opcode, 0);
  }

  return 0;
}

static bool isCCMPOrCTEST(InternalInstruction *insn) {
  if (insn->opcodeType != MAP4)
    return false;
  if (insn->opcode == 0x83 && regFromModRM(insn->modRM) == 7)
    return true;
  switch (insn->opcode & 0xfe) {
  default:
    return false;
  case 0x38:
  case 0x3a:
  case 0x84:
    return true;
  case 0x80:
    return regFromModRM(insn->modRM) == 7;
  case 0xf6:
    return regFromModRM(insn->modRM) == 0;
  }
}

static bool isNF(InternalInstruction *insn) {
  if (!nfFromEVEX4of4(insn->vectorExtensionPrefix[3]))
    return false;
  if (insn->opcodeType == MAP4)
    return true;
  // Below NF instructions are not in map4.
  if (insn->opcodeType == THREEBYTE_38 &&
      ppFromEVEX3of4(insn->vectorExtensionPrefix[2]) == VEX_PREFIX_NONE) {
    switch (insn->opcode) {
    case 0xf2: // ANDN
    case 0xf3: // BLSI, BLSR, BLSMSK
    case 0xf5: // BZHI
    case 0xf7: // BEXTR
      return true;
    default:
      break;
    }
  }
  return false;
}

// Determine the ID of an instruction, consuming the ModR/M byte as appropriate
// for extended and escape opcodes. Determines the attributes and context for
// the instruction before doing so.
static int getInstructionID(struct InternalInstruction *insn,
                            const MCInstrInfo *mii) {
  uint16_t attrMask;
  uint16_t instructionID;

  LLVM_DEBUG(dbgs() << "getID()");

  attrMask = ATTR_NONE;

  if (insn->mode == MODE_64BIT)
    attrMask |= ATTR_64BIT;

  if (insn->vectorExtensionType != TYPE_NO_VEX_XOP) {
    attrMask |= (insn->vectorExtensionType == TYPE_EVEX) ? ATTR_EVEX : ATTR_VEX;

    if (insn->vectorExtensionType == TYPE_EVEX) {
      switch (ppFromEVEX3of4(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (zFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXKZ;
      if (bFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXB;
      if (isNF(insn) && !readModRM(insn) &&
          !isCCMPOrCTEST(insn)) // NF bit is the MSB of aaa.
        attrMask |= ATTR_EVEXNF;
      // aaa is not used a opmask in MAP4
      else if (aaaFromEVEX4of4(insn->vectorExtensionPrefix[3]) &&
               (insn->opcodeType != MAP4))
        attrMask |= ATTR_EVEXK;
      if (lFromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_VEXL;
      if (l2FromEVEX4of4(insn->vectorExtensionPrefix[3]))
        attrMask |= ATTR_EVEXL2;
    } else if (insn->vectorExtensionType == TYPE_VEX_3B) {
      switch (ppFromVEX3of3(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromVEX3of3(insn->vectorExtensionPrefix[2]))
        attrMask |= ATTR_VEXL;
    } else if (insn->vectorExtensionType == TYPE_VEX_2B) {
      switch (ppFromVEX2of2(insn->vectorExtensionPrefix[1])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        if (insn->hasAdSize)
          attrMask |= ATTR_ADSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromVEX2of2(insn->vectorExtensionPrefix[1]))
        attrMask |= ATTR_VEXL;
    } else if (insn->vectorExtensionType == TYPE_XOP) {
      switch (ppFromXOP3of3(insn->vectorExtensionPrefix[2])) {
      case VEX_PREFIX_66:
        attrMask |= ATTR_OPSIZE;
        break;
      case VEX_PREFIX_F3:
        attrMask |= ATTR_XS;
        break;
      case VEX_PREFIX_F2:
        attrMask |= ATTR_XD;
        break;
      }

      if (lFromXOP3of3(insn->vectorExtensionPrefix[2]))
        attrMask |= ATTR_VEXL;
    } else {
      return -1;
    }
  } else if (!insn->mandatoryPrefix) {
    // If we don't have mandatory prefix we should use legacy prefixes here
    if (insn->hasOpSize && (insn->mode != MODE_16BIT))
      attrMask |= ATTR_OPSIZE;
    if (insn->hasAdSize)
      attrMask |= ATTR_ADSIZE;
    if (insn->opcodeType == ONEBYTE) {
      if (insn->repeatPrefix == 0xf3 && (insn->opcode == 0x90))
        // Special support for PAUSE
        attrMask |= ATTR_XS;
    } else {
      if (insn->repeatPrefix == 0xf2)
        attrMask |= ATTR_XD;
      else if (insn->repeatPrefix == 0xf3)
        attrMask |= ATTR_XS;
    }
  } else {
    switch (insn->mandatoryPrefix) {
    case 0xf2:
      attrMask |= ATTR_XD;
      break;
    case 0xf3:
      attrMask |= ATTR_XS;
      break;
    case 0x66:
      if (insn->mode != MODE_16BIT)
        attrMask |= ATTR_OPSIZE;
      if (insn->hasAdSize)
        attrMask |= ATTR_ADSIZE;
      break;
    case 0x67:
      attrMask |= ATTR_ADSIZE;
      break;
    }
  }

  if (insn->rexPrefix & 0x08) {
    attrMask |= ATTR_REXW;
    attrMask &= ~ATTR_ADSIZE;
  }

  // Absolute jump and pushp/popp need special handling
  if (insn->rex2ExtensionPrefix[0] == 0xd5 && insn->opcodeType == ONEBYTE &&
      (insn->opcode == 0xA1 || (insn->opcode & 0xf0) == 0x50))
    attrMask |= ATTR_REX2;

  if (insn->mode == MODE_16BIT) {
    // JCXZ/JECXZ need special handling for 16-bit mode because the meaning
    // of the AdSize prefix is inverted w.r.t. 32-bit mode.
    if (insn->opcodeType == ONEBYTE && insn->opcode == 0xE3)
      attrMask ^= ATTR_ADSIZE;
    // If we're in 16-bit mode and this is one of the relative jumps and opsize
    // prefix isn't present, we need to force the opsize attribute since the
    // prefix is inverted relative to 32-bit mode.
    if (!insn->hasOpSize && insn->opcodeType == ONEBYTE &&
        (insn->opcode == 0xE8 || insn->opcode == 0xE9))
      attrMask |= ATTR_OPSIZE;

    if (!insn->hasOpSize && insn->opcodeType == TWOBYTE &&
        insn->opcode >= 0x80 && insn->opcode <= 0x8F)
      attrMask |= ATTR_OPSIZE;
  }


  if (getInstructionIDWithAttrMask(&instructionID, insn, attrMask))
    return -1;

  // The following clauses compensate for limitations of the tables.

  if (insn->mode != MODE_64BIT &&
      insn->vectorExtensionType != TYPE_NO_VEX_XOP) {
    // The tables can't distinquish between cases where the W-bit is used to
    // select register size and cases where its a required part of the opcode.
    if ((insn->vectorExtensionType == TYPE_EVEX &&
         wFromEVEX3of4(insn->vectorExtensionPrefix[2])) ||
        (insn->vectorExtensionType == TYPE_VEX_3B &&
         wFromVEX3of3(insn->vectorExtensionPrefix[2])) ||
        (insn->vectorExtensionType == TYPE_XOP &&
         wFromXOP3of3(insn->vectorExtensionPrefix[2]))) {

      uint16_t instructionIDWithREXW;
      if (getInstructionIDWithAttrMask(&instructionIDWithREXW, insn,
                                       attrMask | ATTR_REXW)) {
        insn->instructionID = instructionID;
        insn->spec = &INSTRUCTIONS_SYM[instructionID];
        return 0;
      }

      auto SpecName = mii->getName(instructionIDWithREXW);
      // If not a 64-bit instruction. Switch the opcode.
      if (!is64Bit(SpecName.data())) {
        insn->instructionID = instructionIDWithREXW;
        insn->spec = &INSTRUCTIONS_SYM[instructionIDWithREXW];
        return 0;
      }
    }
  }

  // Absolute moves, umonitor, and movdir64b need special handling.
  // -For 16-bit mode because the meaning of the AdSize and OpSize prefixes are
  //  inverted w.r.t.
  // -For 32-bit mode we need to ensure the ADSIZE prefix is observed in
  //  any position.
  if ((insn->opcodeType == ONEBYTE && ((insn->opcode & 0xFC) == 0xA0)) ||
      (insn->opcodeType == TWOBYTE && (insn->opcode == 0xAE)) ||
      (insn->opcodeType == THREEBYTE_38 && insn->opcode == 0xF8) ||
      (insn->opcodeType == MAP4 && insn->opcode == 0xF8)) {
    // Make sure we observed the prefixes in any position.
    if (insn->hasAdSize)
      attrMask |= ATTR_ADSIZE;
    if (insn->hasOpSize)
      attrMask |= ATTR_OPSIZE;

    // In 16-bit, invert the attributes.
    if (insn->mode == MODE_16BIT) {
      attrMask ^= ATTR_ADSIZE;

      // The OpSize attribute is only valid with the absolute moves.
      if (insn->opcodeType == ONEBYTE && ((insn->opcode & 0xFC) == 0xA0))
        attrMask ^= ATTR_OPSIZE;
    }

    if (getInstructionIDWithAttrMask(&instructionID, insn, attrMask))
      return -1;

    insn->instructionID = instructionID;
    insn->spec = &INSTRUCTIONS_SYM[instructionID];
    return 0;
  }

  if ((insn->mode == MODE_16BIT || insn->hasOpSize) &&
      !(attrMask & ATTR_OPSIZE)) {
    // The instruction tables make no distinction between instructions that
    // allow OpSize anywhere (i.e., 16-bit operations) and that need it in a
    // particular spot (i.e., many MMX operations). In general we're
    // conservative, but in the specific case where OpSize is present but not in
    // the right place we check if there's a 16-bit operation.
    const struct InstructionSpecifier *spec;
    uint16_t instructionIDWithOpsize;
    llvm::StringRef specName, specWithOpSizeName;

    spec = &INSTRUCTIONS_SYM[instructionID];

    if (getInstructionIDWithAttrMask(&instructionIDWithOpsize, insn,
                                     attrMask | ATTR_OPSIZE)) {
      // ModRM required with OpSize but not present. Give up and return the
      // version without OpSize set.
      insn->instructionID = instructionID;
      insn->spec = spec;
      return 0;
    }

    specName = mii->getName(instructionID);
    specWithOpSizeName = mii->getName(instructionIDWithOpsize);

    if (is16BitEquivalent(specName.data(), specWithOpSizeName.data()) &&
        (insn->mode == MODE_16BIT) ^ insn->hasOpSize) {
      insn->instructionID = instructionIDWithOpsize;
      insn->spec = &INSTRUCTIONS_SYM[instructionIDWithOpsize];
    } else {
      insn->instructionID = instructionID;
      insn->spec = spec;
    }
    return 0;
  }

  if (insn->opcodeType == ONEBYTE && insn->opcode == 0x90 &&
      insn->rexPrefix & 0x01) {
    // NOOP shouldn't decode as NOOP if REX.b is set. Instead it should decode
    // as XCHG %r8, %eax.
    const struct InstructionSpecifier *spec;
    uint16_t instructionIDWithNewOpcode;
    const struct InstructionSpecifier *specWithNewOpcode;

    spec = &INSTRUCTIONS_SYM[instructionID];

    // Borrow opcode from one of the other XCHGar opcodes
    insn->opcode = 0x91;

    if (getInstructionIDWithAttrMask(&instructionIDWithNewOpcode, insn,
                                     attrMask)) {
      insn->opcode = 0x90;

      insn->instructionID = instructionID;
      insn->spec = spec;
      return 0;
    }

    specWithNewOpcode = &INSTRUCTIONS_SYM[instructionIDWithNewOpcode];

    // Change back
    insn->opcode = 0x90;

    insn->instructionID = instructionIDWithNewOpcode;
    insn->spec = specWithNewOpcode;

    return 0;
  }

  insn->instructionID = instructionID;
  insn->spec = &INSTRUCTIONS_SYM[insn->instructionID];

  return 0;
}

// Read an operand from the opcode field of an instruction and interprets it
// appropriately given the operand width. Handles AddRegFrm instructions.
//
// @param insn  - the instruction whose opcode field is to be read.
// @param size  - The width (in bytes) of the register being specified.
//                1 means AL and friends, 2 means AX, 4 means EAX, and 8 means
//                RAX.
// @return      - 0 on success; nonzero otherwise.
static int readOpcodeRegister(struct InternalInstruction *insn, uint8_t size) {
  LLVM_DEBUG(dbgs() << "readOpcodeRegister()");

  if (size == 0)
    size = insn->registerSize;

  auto setOpcodeRegister = [&](unsigned base) {
    insn->opcodeRegister =
        (Reg)(base + ((bFromREX(insn->rexPrefix) << 3) |
                      (b2FromREX2(insn->rex2ExtensionPrefix[1]) << 4) |
                      (insn->opcode & 7)));
  };

  switch (size) {
  case 1:
    setOpcodeRegister(MODRM_REG_AL);
    if (insn->rexPrefix && insn->opcodeRegister >= MODRM_REG_AL + 0x4 &&
        insn->opcodeRegister < MODRM_REG_AL + 0x8) {
      insn->opcodeRegister =
          (Reg)(MODRM_REG_SPL + (insn->opcodeRegister - MODRM_REG_AL - 4));
    }

    break;
  case 2:
    setOpcodeRegister(MODRM_REG_AX);
    break;
  case 4:
    setOpcodeRegister(MODRM_REG_EAX);
    break;
  case 8:
    setOpcodeRegister(MODRM_REG_RAX);
    break;
  }

  return 0;
}

// Consume an immediate operand from an instruction, given the desired operand
// size.
//
// @param insn  - The instruction whose operand is to be read.
// @param size  - The width (in bytes) of the operand.
// @return      - 0 if the immediate was successfully consumed; nonzero
//                otherwise.
static int readImmediate(struct InternalInstruction *insn, uint8_t size) {
  uint8_t imm8;
  uint16_t imm16;
  uint32_t imm32;
  uint64_t imm64;

  LLVM_DEBUG(dbgs() << "readImmediate()");

  assert(insn->numImmediatesConsumed < 2 && "Already consumed two immediates");

  insn->immediateSize = size;
  insn->immediateOffset = insn->readerCursor - insn->startLocation;

  switch (size) {
  case 1:
    if (consume(insn, imm8))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm8;
    break;
  case 2:
    if (consume(insn, imm16))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm16;
    break;
  case 4:
    if (consume(insn, imm32))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm32;
    break;
  case 8:
    if (consume(insn, imm64))
      return -1;
    insn->immediates[insn->numImmediatesConsumed] = imm64;
    break;
  default:
    llvm_unreachable("invalid size");
  }

  insn->numImmediatesConsumed++;

  return 0;
}

// Consume vvvv from an instruction if it has a VEX prefix.
static int readVVVV(struct InternalInstruction *insn) {
  LLVM_DEBUG(dbgs() << "readVVVV()");

  int vvvv;
  if (insn->vectorExtensionType == TYPE_EVEX)
    vvvv = (v2FromEVEX4of4(insn->vectorExtensionPrefix[3]) << 4 |
            vvvvFromEVEX3of4(insn->vectorExtensionPrefix[2]));
  else if (insn->vectorExtensionType == TYPE_VEX_3B)
    vvvv = vvvvFromVEX3of3(insn->vectorExtensionPrefix[2]);
  else if (insn->vectorExtensionType == TYPE_VEX_2B)
    vvvv = vvvvFromVEX2of2(insn->vectorExtensionPrefix[1]);
  else if (insn->vectorExtensionType == TYPE_XOP)
    vvvv = vvvvFromXOP3of3(insn->vectorExtensionPrefix[2]);
  else
    return -1;

  if (insn->mode != MODE_64BIT)
    vvvv &= 0xf; // Can only clear bit 4. Bit 3 must be cleared later.

  insn->vvvv = static_cast<Reg>(vvvv);
  return 0;
}

// Read an mask register from the opcode field of an instruction.
//
// @param insn    - The instruction whose opcode field is to be read.
// @return        - 0 on success; nonzero otherwise.
static int readMaskRegister(struct InternalInstruction *insn) {
  LLVM_DEBUG(dbgs() << "readMaskRegister()");

  if (insn->vectorExtensionType != TYPE_EVEX)
    return -1;

  insn->writemask =
      static_cast<Reg>(aaaFromEVEX4of4(insn->vectorExtensionPrefix[3]));
  return 0;
}

// Consults the specifier for an instruction and consumes all
// operands for that instruction, interpreting them as it goes.
static int readOperands(struct InternalInstruction *insn) {
  int hasVVVV, needVVVV;
  int sawRegImm = 0;

  LLVM_DEBUG(dbgs() << "readOperands()");

  // If non-zero vvvv specified, make sure one of the operands uses it.
  hasVVVV = !readVVVV(insn);
  needVVVV = hasVVVV && (insn->vvvv != 0);

  for (const auto &Op : x86OperandSets[insn->spec->operands]) {
    switch (Op.encoding) {
    case ENCODING_NONE:
    case ENCODING_SI:
    case ENCODING_DI:
      break;
    CASE_ENCODING_VSIB:
      // VSIB can use the V2 bit so check only the other bits.
      if (needVVVV)
        needVVVV = hasVVVV & ((insn->vvvv & 0xf) != 0);
      if (readModRM(insn))
        return -1;

      // Reject if SIB wasn't used.
      if (insn->eaBase != EA_BASE_sib && insn->eaBase != EA_BASE_sib64)
        return -1;

      // If sibIndex was set to SIB_INDEX_NONE, index offset is 4.
      if (insn->sibIndex == SIB_INDEX_NONE)
        insn->sibIndex = (SIBIndex)(insn->sibIndexBase + 4);

      // If EVEX.v2 is set this is one of the 16-31 registers.
      if (insn->vectorExtensionType == TYPE_EVEX && insn->mode == MODE_64BIT &&
          v2FromEVEX4of4(insn->vectorExtensionPrefix[3]))
        insn->sibIndex = (SIBIndex)(insn->sibIndex + 16);

      // Adjust the index register to the correct size.
      switch ((OperandType)Op.type) {
      default:
        debug("Unhandled VSIB index type");
        return -1;
      case TYPE_MVSIBX:
        insn->sibIndex =
            (SIBIndex)(SIB_INDEX_XMM0 + (insn->sibIndex - insn->sibIndexBase));
        break;
      case TYPE_MVSIBY:
        insn->sibIndex =
            (SIBIndex)(SIB_INDEX_YMM0 + (insn->sibIndex - insn->sibIndexBase));
        break;
      case TYPE_MVSIBZ:
        insn->sibIndex =
            (SIBIndex)(SIB_INDEX_ZMM0 + (insn->sibIndex - insn->sibIndexBase));
        break;
      }

      // Apply the AVX512 compressed displacement scaling factor.
      if (Op.encoding != ENCODING_REG && insn->eaDisplacement == EA_DISP_8)
        insn->displacement *= 1 << (Op.encoding - ENCODING_VSIB);
      break;
    case ENCODING_SIB:
      // Reject if SIB wasn't used.
      if (insn->eaBase != EA_BASE_sib && insn->eaBase != EA_BASE_sib64)
        return -1;
      if (readModRM(insn))
        return -1;
      if (fixupReg(insn, &Op))
        return -1;
      break;
    case ENCODING_REG:
    CASE_ENCODING_RM:
      if (readModRM(insn))
        return -1;
      if (fixupReg(insn, &Op))
        return -1;
      // Apply the AVX512 compressed displacement scaling factor.
      if (Op.encoding != ENCODING_REG && insn->eaDisplacement == EA_DISP_8)
        insn->displacement *= 1 << (Op.encoding - ENCODING_RM);
      break;
    case ENCODING_IB:
      if (sawRegImm) {
        // Saw a register immediate so don't read again and instead split the
        // previous immediate. FIXME: This is a hack.
        insn->immediates[insn->numImmediatesConsumed] =
            insn->immediates[insn->numImmediatesConsumed - 1] & 0xf;
        ++insn->numImmediatesConsumed;
        break;
      }
      if (readImmediate(insn, 1))
        return -1;
      if (Op.type == TYPE_XMM || Op.type == TYPE_YMM)
        sawRegImm = 1;
      break;
    case ENCODING_IW:
      if (readImmediate(insn, 2))
        return -1;
      break;
    case ENCODING_ID:
      if (readImmediate(insn, 4))
        return -1;
      break;
    case ENCODING_IO:
      if (readImmediate(insn, 8))
        return -1;
      break;
    case ENCODING_Iv:
      if (readImmediate(insn, insn->immediateSize))
        return -1;
      break;
    case ENCODING_Ia:
      if (readImmediate(insn, insn->addressSize))
        return -1;
      break;
    case ENCODING_IRC:
      insn->RC = (l2FromEVEX4of4(insn->vectorExtensionPrefix[3]) << 1) |
                 lFromEVEX4of4(insn->vectorExtensionPrefix[3]);
      break;
    case ENCODING_RB:
      if (readOpcodeRegister(insn, 1))
        return -1;
      break;
    case ENCODING_RW:
      if (readOpcodeRegister(insn, 2))
        return -1;
      break;
    case ENCODING_RD:
      if (readOpcodeRegister(insn, 4))
        return -1;
      break;
    case ENCODING_RO:
      if (readOpcodeRegister(insn, 8))
        return -1;
      break;
    case ENCODING_Rv:
      if (readOpcodeRegister(insn, 0))
        return -1;
      break;
    case ENCODING_CF:
      insn->immediates[1] = oszcFromEVEX3of4(insn->vectorExtensionPrefix[2]);
      needVVVV = false; // oszc shares the same bits with VVVV
      break;
    case ENCODING_CC:
      if (isCCMPOrCTEST(insn))
        insn->immediates[2] = scFromEVEX4of4(insn->vectorExtensionPrefix[3]);
      else
        insn->immediates[1] = insn->opcode & 0xf;
      break;
    case ENCODING_FP:
      break;
    case ENCODING_VVVV:
      needVVVV = 0; // Mark that we have found a VVVV operand.
      if (!hasVVVV)
        return -1;
      if (insn->mode != MODE_64BIT)
        insn->vvvv = static_cast<Reg>(insn->vvvv & 0x7);
      if (fixupReg(insn, &Op))
        return -1;
      break;
    case ENCODING_WRITEMASK:
      if (readMaskRegister(insn))
        return -1;
      break;
    case ENCODING_DUP:
      break;
    default:
      LLVM_DEBUG(dbgs() << "Encountered an operand with an unknown encoding.");
      return -1;
    }
  }

  // If we didn't find ENCODING_VVVV operand, but non-zero vvvv present, fail
  if (needVVVV)
    return -1;

  return 0;
}

namespace llvm {

// Fill-ins to make the compiler happy. These constants are never actually
// assigned; they are just filler to make an automatically-generated switch
// statement work.
namespace X86 {
  enum {
    BX_SI = 500,
    BX_DI = 501,
    BP_SI = 502,
    BP_DI = 503,
    sib   = 504,
    sib64 = 505
  };
} // namespace X86

} // namespace llvm

static bool translateInstruction(MCInst &target,
                                InternalInstruction &source,
                                const MCDisassembler *Dis);

namespace {

/// Generic disassembler for all X86 platforms. All each platform class should
/// have to do is subclass the constructor, and provide a different
/// disassemblerMode value.
class X86GenericDisassembler : public MCDisassembler {
  std::unique_ptr<const MCInstrInfo> MII;
public:
  X86GenericDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                         std::unique_ptr<const MCInstrInfo> MII);
public:
  DecodeStatus getInstruction(MCInst &instr, uint64_t &size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &cStream) const override;

private:
  DisassemblerMode              fMode;
};

} // namespace

X86GenericDisassembler::X86GenericDisassembler(
                                         const MCSubtargetInfo &STI,
                                         MCContext &Ctx,
                                         std::unique_ptr<const MCInstrInfo> MII)
  : MCDisassembler(STI, Ctx), MII(std::move(MII)) {
  const FeatureBitset &FB = STI.getFeatureBits();
  if (FB[X86::Is16Bit]) {
    fMode = MODE_16BIT;
    return;
  } else if (FB[X86::Is32Bit]) {
    fMode = MODE_32BIT;
    return;
  } else if (FB[X86::Is64Bit]) {
    fMode = MODE_64BIT;
    return;
  }

  llvm_unreachable("Invalid CPU mode");
}

MCDisassembler::DecodeStatus X86GenericDisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream &CStream) const {
  CommentStream = &CStream;

  InternalInstruction Insn;
  memset(&Insn, 0, sizeof(InternalInstruction));
  Insn.bytes = Bytes;
  Insn.startLocation = Address;
  Insn.readerCursor = Address;
  Insn.mode = fMode;

  if (Bytes.empty() || readPrefixes(&Insn) || readOpcode(&Insn) ||
      getInstructionID(&Insn, MII.get()) || Insn.instructionID == 0 ||
      readOperands(&Insn)) {
    Size = Insn.readerCursor - Address;
    return Fail;
  }

  Insn.operands = x86OperandSets[Insn.spec->operands];
  Insn.length = Insn.readerCursor - Insn.startLocation;
  Size = Insn.length;
  if (Size > 15)
    LLVM_DEBUG(dbgs() << "Instruction exceeds 15-byte limit");

  bool Ret = translateInstruction(Instr, Insn, this);
  if (!Ret) {
    unsigned Flags = X86::IP_NO_PREFIX;
    if (Insn.hasAdSize)
      Flags |= X86::IP_HAS_AD_SIZE;
    if (!Insn.mandatoryPrefix) {
      if (Insn.hasOpSize)
        Flags |= X86::IP_HAS_OP_SIZE;
      if (Insn.repeatPrefix == 0xf2)
        Flags |= X86::IP_HAS_REPEAT_NE;
      else if (Insn.repeatPrefix == 0xf3 &&
               // It should not be 'pause' f3 90
               Insn.opcode != 0x90)
        Flags |= X86::IP_HAS_REPEAT;
      if (Insn.hasLockPrefix)
        Flags |= X86::IP_HAS_LOCK;
    }
    Instr.setFlags(Flags);
  }
  return (!Ret) ? Success : Fail;
}

//
// Private code that translates from struct InternalInstructions to MCInsts.
//

/// translateRegister - Translates an internal register to the appropriate LLVM
///   register, and appends it as an operand to an MCInst.
///
/// @param mcInst     - The MCInst to append to.
/// @param reg        - The Reg to append.
static void translateRegister(MCInst &mcInst, Reg reg) {
#define ENTRY(x) X86::x,
  static constexpr MCPhysReg llvmRegnums[] = {ALL_REGS};
#undef ENTRY

  MCPhysReg llvmRegnum = llvmRegnums[reg];
  mcInst.addOperand(MCOperand::createReg(llvmRegnum));
}

static const uint8_t segmentRegnums[SEG_OVERRIDE_max] = {
  0,        // SEG_OVERRIDE_NONE
  X86::CS,
  X86::SS,
  X86::DS,
  X86::ES,
  X86::FS,
  X86::GS
};

/// translateSrcIndex   - Appends a source index operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction.
static bool translateSrcIndex(MCInst &mcInst, InternalInstruction &insn) {
  unsigned baseRegNo;

  if (insn.mode == MODE_64BIT)
    baseRegNo = insn.hasAdSize ? X86::ESI : X86::RSI;
  else if (insn.mode == MODE_32BIT)
    baseRegNo = insn.hasAdSize ? X86::SI : X86::ESI;
  else {
    assert(insn.mode == MODE_16BIT);
    baseRegNo = insn.hasAdSize ? X86::ESI : X86::SI;
  }
  MCOperand baseReg = MCOperand::createReg(baseRegNo);
  mcInst.addOperand(baseReg);

  MCOperand segmentReg;
  segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);
  mcInst.addOperand(segmentReg);
  return false;
}

/// translateDstIndex   - Appends a destination index operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction.

static bool translateDstIndex(MCInst &mcInst, InternalInstruction &insn) {
  unsigned baseRegNo;

  if (insn.mode == MODE_64BIT)
    baseRegNo = insn.hasAdSize ? X86::EDI : X86::RDI;
  else if (insn.mode == MODE_32BIT)
    baseRegNo = insn.hasAdSize ? X86::DI : X86::EDI;
  else {
    assert(insn.mode == MODE_16BIT);
    baseRegNo = insn.hasAdSize ? X86::EDI : X86::DI;
  }
  MCOperand baseReg = MCOperand::createReg(baseRegNo);
  mcInst.addOperand(baseReg);
  return false;
}

/// translateImmediate  - Appends an immediate operand to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param immediate    - The immediate value to append.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The internal instruction.
static void translateImmediate(MCInst &mcInst, uint64_t immediate,
                               const OperandSpecifier &operand,
                               InternalInstruction &insn,
                               const MCDisassembler *Dis) {
  // Sign-extend the immediate if necessary.

  OperandType type = (OperandType)operand.type;

  bool isBranch = false;
  uint64_t pcrel = 0;
  if (type == TYPE_REL) {
    isBranch = true;
    pcrel = insn.startLocation + insn.length;
    switch (operand.encoding) {
    default:
      break;
    case ENCODING_Iv:
      switch (insn.displacementSize) {
      default:
        break;
      case 1:
        if(immediate & 0x80)
          immediate |= ~(0xffull);
        break;
      case 2:
        if(immediate & 0x8000)
          immediate |= ~(0xffffull);
        break;
      case 4:
        if(immediate & 0x80000000)
          immediate |= ~(0xffffffffull);
        break;
      case 8:
        break;
      }
      break;
    case ENCODING_IB:
      if(immediate & 0x80)
        immediate |= ~(0xffull);
      break;
    case ENCODING_IW:
      if(immediate & 0x8000)
        immediate |= ~(0xffffull);
      break;
    case ENCODING_ID:
      if(immediate & 0x80000000)
        immediate |= ~(0xffffffffull);
      break;
    }
  }
  // By default sign-extend all X86 immediates based on their encoding.
  else if (type == TYPE_IMM) {
    switch (operand.encoding) {
    default:
      break;
    case ENCODING_IB:
      if(immediate & 0x80)
        immediate |= ~(0xffull);
      break;
    case ENCODING_IW:
      if(immediate & 0x8000)
        immediate |= ~(0xffffull);
      break;
    case ENCODING_ID:
      if(immediate & 0x80000000)
        immediate |= ~(0xffffffffull);
      break;
    case ENCODING_IO:
      break;
    }
  }

  switch (type) {
  case TYPE_XMM:
    mcInst.addOperand(MCOperand::createReg(X86::XMM0 + (immediate >> 4)));
    return;
  case TYPE_YMM:
    mcInst.addOperand(MCOperand::createReg(X86::YMM0 + (immediate >> 4)));
    return;
  case TYPE_ZMM:
    mcInst.addOperand(MCOperand::createReg(X86::ZMM0 + (immediate >> 4)));
    return;
  default:
    // operand is 64 bits wide.  Do nothing.
    break;
  }

  if (!Dis->tryAddingSymbolicOperand(
          mcInst, immediate + pcrel, insn.startLocation, isBranch,
          insn.immediateOffset, insn.immediateSize, insn.length))
    mcInst.addOperand(MCOperand::createImm(immediate));

  if (type == TYPE_MOFFS) {
    MCOperand segmentReg;
    segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);
    mcInst.addOperand(segmentReg);
  }
}

/// translateRMRegister - Translates a register stored in the R/M field of the
///   ModR/M byte to its LLVM equivalent and appends it to an MCInst.
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The internal instruction to extract the R/M field
///                       from.
/// @return             - 0 on success; -1 otherwise
static bool translateRMRegister(MCInst &mcInst,
                                InternalInstruction &insn) {
  if (insn.eaBase == EA_BASE_sib || insn.eaBase == EA_BASE_sib64) {
    debug("A R/M register operand may not have a SIB byte");
    return true;
  }

  switch (insn.eaBase) {
  default:
    debug("Unexpected EA base register");
    return true;
  case EA_BASE_NONE:
    debug("EA_BASE_NONE for ModR/M base");
    return true;
#define ENTRY(x) case EA_BASE_##x:
  ALL_EA_BASES
#undef ENTRY
    debug("A R/M register operand may not have a base; "
          "the operand must be a register.");
    return true;
#define ENTRY(x)                                                      \
  case EA_REG_##x:                                                    \
    mcInst.addOperand(MCOperand::createReg(X86::x)); break;
  ALL_REGS
#undef ENTRY
  }

  return false;
}

/// translateRMMemory - Translates a memory operand stored in the Mod and R/M
///   fields of an internal instruction (and possibly its SIB byte) to a memory
///   operand in LLVM's format, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param insn         - The instruction to extract Mod, R/M, and SIB fields
///                       from.
/// @param ForceSIB     - The instruction must use SIB.
/// @return             - 0 on success; nonzero otherwise
static bool translateRMMemory(MCInst &mcInst, InternalInstruction &insn,
                              const MCDisassembler *Dis,
                              bool ForceSIB = false) {
  // Addresses in an MCInst are represented as five operands:
  //   1. basereg       (register)  The R/M base, or (if there is a SIB) the
  //                                SIB base
  //   2. scaleamount   (immediate) 1, or (if there is a SIB) the specified
  //                                scale amount
  //   3. indexreg      (register)  x86_registerNONE, or (if there is a SIB)
  //                                the index (which is multiplied by the
  //                                scale amount)
  //   4. displacement  (immediate) 0, or the displacement if there is one
  //   5. segmentreg    (register)  x86_registerNONE for now, but could be set
  //                                if we have segment overrides

  MCOperand baseReg;
  MCOperand scaleAmount;
  MCOperand indexReg;
  MCOperand displacement;
  MCOperand segmentReg;
  uint64_t pcrel = 0;

  if (insn.eaBase == EA_BASE_sib || insn.eaBase == EA_BASE_sib64) {
    if (insn.sibBase != SIB_BASE_NONE) {
      switch (insn.sibBase) {
      default:
        debug("Unexpected sibBase");
        return true;
#define ENTRY(x)                                          \
      case SIB_BASE_##x:                                  \
        baseReg = MCOperand::createReg(X86::x); break;
      ALL_SIB_BASES
#undef ENTRY
      }
    } else {
      baseReg = MCOperand::createReg(X86::NoRegister);
    }

    if (insn.sibIndex != SIB_INDEX_NONE) {
      switch (insn.sibIndex) {
      default:
        debug("Unexpected sibIndex");
        return true;
#define ENTRY(x)                                          \
      case SIB_INDEX_##x:                                 \
        indexReg = MCOperand::createReg(X86::x); break;
      EA_BASES_32BIT
      EA_BASES_64BIT
      REGS_XMM
      REGS_YMM
      REGS_ZMM
#undef ENTRY
      }
    } else {
      // Use EIZ/RIZ for a few ambiguous cases where the SIB byte is present,
      // but no index is used and modrm alone should have been enough.
      // -No base register in 32-bit mode. In 64-bit mode this is used to
      //  avoid rip-relative addressing.
      // -Any base register used other than ESP/RSP/R12D/R12. Using these as a
      //  base always requires a SIB byte.
      // -A scale other than 1 is used.
      if (!ForceSIB &&
          (insn.sibScale != 1 ||
           (insn.sibBase == SIB_BASE_NONE && insn.mode != MODE_64BIT) ||
           (insn.sibBase != SIB_BASE_NONE &&
            insn.sibBase != SIB_BASE_ESP && insn.sibBase != SIB_BASE_RSP &&
            insn.sibBase != SIB_BASE_R12D && insn.sibBase != SIB_BASE_R12))) {
        indexReg = MCOperand::createReg(insn.addressSize == 4 ? X86::EIZ :
                                                                X86::RIZ);
      } else
        indexReg = MCOperand::createReg(X86::NoRegister);
    }

    scaleAmount = MCOperand::createImm(insn.sibScale);
  } else {
    switch (insn.eaBase) {
    case EA_BASE_NONE:
      if (insn.eaDisplacement == EA_DISP_NONE) {
        debug("EA_BASE_NONE and EA_DISP_NONE for ModR/M base");
        return true;
      }
      if (insn.mode == MODE_64BIT){
        pcrel = insn.startLocation + insn.length;
        Dis->tryAddingPcLoadReferenceComment(insn.displacement + pcrel,
                                             insn.startLocation +
                                                 insn.displacementOffset);
        // Section 2.2.1.6
        baseReg = MCOperand::createReg(insn.addressSize == 4 ? X86::EIP :
                                                               X86::RIP);
      }
      else
        baseReg = MCOperand::createReg(X86::NoRegister);

      indexReg = MCOperand::createReg(X86::NoRegister);
      break;
    case EA_BASE_BX_SI:
      baseReg = MCOperand::createReg(X86::BX);
      indexReg = MCOperand::createReg(X86::SI);
      break;
    case EA_BASE_BX_DI:
      baseReg = MCOperand::createReg(X86::BX);
      indexReg = MCOperand::createReg(X86::DI);
      break;
    case EA_BASE_BP_SI:
      baseReg = MCOperand::createReg(X86::BP);
      indexReg = MCOperand::createReg(X86::SI);
      break;
    case EA_BASE_BP_DI:
      baseReg = MCOperand::createReg(X86::BP);
      indexReg = MCOperand::createReg(X86::DI);
      break;
    default:
      indexReg = MCOperand::createReg(X86::NoRegister);
      switch (insn.eaBase) {
      default:
        debug("Unexpected eaBase");
        return true;
        // Here, we will use the fill-ins defined above.  However,
        //   BX_SI, BX_DI, BP_SI, and BP_DI are all handled above and
        //   sib and sib64 were handled in the top-level if, so they're only
        //   placeholders to keep the compiler happy.
#define ENTRY(x)                                        \
      case EA_BASE_##x:                                 \
        baseReg = MCOperand::createReg(X86::x); break;
      ALL_EA_BASES
#undef ENTRY
#define ENTRY(x) case EA_REG_##x:
      ALL_REGS
#undef ENTRY
        debug("A R/M memory operand may not be a register; "
              "the base field must be a base.");
        return true;
      }
    }

    scaleAmount = MCOperand::createImm(1);
  }

  displacement = MCOperand::createImm(insn.displacement);

  segmentReg = MCOperand::createReg(segmentRegnums[insn.segmentOverride]);

  mcInst.addOperand(baseReg);
  mcInst.addOperand(scaleAmount);
  mcInst.addOperand(indexReg);

  const uint8_t dispSize =
      (insn.eaDisplacement == EA_DISP_NONE) ? 0 : insn.displacementSize;

  if (!Dis->tryAddingSymbolicOperand(
          mcInst, insn.displacement + pcrel, insn.startLocation, false,
          insn.displacementOffset, dispSize, insn.length))
    mcInst.addOperand(displacement);
  mcInst.addOperand(segmentReg);
  return false;
}

/// translateRM - Translates an operand stored in the R/M (and possibly SIB)
///   byte of an instruction to LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The instruction to extract Mod, R/M, and SIB fields
///                       from.
/// @return             - 0 on success; nonzero otherwise
static bool translateRM(MCInst &mcInst, const OperandSpecifier &operand,
                        InternalInstruction &insn, const MCDisassembler *Dis) {
  switch (operand.type) {
  default:
    debug("Unexpected type for a R/M operand");
    return true;
  case TYPE_R8:
  case TYPE_R16:
  case TYPE_R32:
  case TYPE_R64:
  case TYPE_Rv:
  case TYPE_MM64:
  case TYPE_XMM:
  case TYPE_YMM:
  case TYPE_ZMM:
  case TYPE_TMM:
  case TYPE_VK_PAIR:
  case TYPE_VK:
  case TYPE_DEBUGREG:
  case TYPE_CONTROLREG:
  case TYPE_BNDR:
    return translateRMRegister(mcInst, insn);
  case TYPE_M:
  case TYPE_MVSIBX:
  case TYPE_MVSIBY:
  case TYPE_MVSIBZ:
    return translateRMMemory(mcInst, insn, Dis);
  case TYPE_MSIB:
    return translateRMMemory(mcInst, insn, Dis, true);
  }
}

/// translateFPRegister - Translates a stack position on the FPU stack to its
///   LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param stackPos     - The stack position to translate.
static void translateFPRegister(MCInst &mcInst,
                                uint8_t stackPos) {
  mcInst.addOperand(MCOperand::createReg(X86::ST0 + stackPos));
}

/// translateMaskRegister - Translates a 3-bit mask register number to
///   LLVM form, and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param maskRegNum   - Number of mask register from 0 to 7.
/// @return             - false on success; true otherwise.
static bool translateMaskRegister(MCInst &mcInst,
                                uint8_t maskRegNum) {
  if (maskRegNum >= 8) {
    debug("Invalid mask register number");
    return true;
  }

  mcInst.addOperand(MCOperand::createReg(X86::K0 + maskRegNum));
  return false;
}

/// translateOperand - Translates an operand stored in an internal instruction
///   to LLVM's format and appends it to an MCInst.
///
/// @param mcInst       - The MCInst to append to.
/// @param operand      - The operand, as stored in the descriptor table.
/// @param insn         - The internal instruction.
/// @return             - false on success; true otherwise.
static bool translateOperand(MCInst &mcInst, const OperandSpecifier &operand,
                             InternalInstruction &insn,
                             const MCDisassembler *Dis) {
  switch (operand.encoding) {
  default:
    debug("Unhandled operand encoding during translation");
    return true;
  case ENCODING_REG:
    translateRegister(mcInst, insn.reg);
    return false;
  case ENCODING_WRITEMASK:
    return translateMaskRegister(mcInst, insn.writemask);
  case ENCODING_SIB:
  CASE_ENCODING_RM:
  CASE_ENCODING_VSIB:
    return translateRM(mcInst, operand, insn, Dis);
  case ENCODING_IB:
  case ENCODING_IW:
  case ENCODING_ID:
  case ENCODING_IO:
  case ENCODING_Iv:
  case ENCODING_Ia:
    translateImmediate(mcInst,
                       insn.immediates[insn.numImmediatesTranslated++],
                       operand,
                       insn,
                       Dis);
    return false;
  case ENCODING_IRC:
    mcInst.addOperand(MCOperand::createImm(insn.RC));
    return false;
  case ENCODING_SI:
    return translateSrcIndex(mcInst, insn);
  case ENCODING_DI:
    return translateDstIndex(mcInst, insn);
  case ENCODING_RB:
  case ENCODING_RW:
  case ENCODING_RD:
  case ENCODING_RO:
  case ENCODING_Rv:
    translateRegister(mcInst, insn.opcodeRegister);
    return false;
  case ENCODING_CF:
    mcInst.addOperand(MCOperand::createImm(insn.immediates[1]));
    return false;
  case ENCODING_CC:
    if (isCCMPOrCTEST(&insn))
      mcInst.addOperand(MCOperand::createImm(insn.immediates[2]));
    else
      mcInst.addOperand(MCOperand::createImm(insn.immediates[1]));
    return false;
  case ENCODING_FP:
    translateFPRegister(mcInst, insn.modRM & 7);
    return false;
  case ENCODING_VVVV:
    translateRegister(mcInst, insn.vvvv);
    return false;
  case ENCODING_DUP:
    return translateOperand(mcInst, insn.operands[operand.type - TYPE_DUP0],
                            insn, Dis);
  }
}

/// translateInstruction - Translates an internal instruction and all its
///   operands to an MCInst.
///
/// @param mcInst       - The MCInst to populate with the instruction's data.
/// @param insn         - The internal instruction.
/// @return             - false on success; true otherwise.
static bool translateInstruction(MCInst &mcInst,
                                InternalInstruction &insn,
                                const MCDisassembler *Dis) {
  if (!insn.spec) {
    debug("Instruction has no specification");
    return true;
  }

  mcInst.clear();
  mcInst.setOpcode(insn.instructionID);
  // If when reading the prefix bytes we determined the overlapping 0xf2 or 0xf3
  // prefix bytes should be disassembled as xrelease and xacquire then set the
  // opcode to those instead of the rep and repne opcodes.
  if (insn.xAcquireRelease) {
    if(mcInst.getOpcode() == X86::REP_PREFIX)
      mcInst.setOpcode(X86::XRELEASE_PREFIX);
    else if(mcInst.getOpcode() == X86::REPNE_PREFIX)
      mcInst.setOpcode(X86::XACQUIRE_PREFIX);
  }

  insn.numImmediatesTranslated = 0;

  for (const auto &Op : insn.operands) {
    if (Op.encoding != ENCODING_NONE) {
      if (translateOperand(mcInst, Op, insn, Dis)) {
        return true;
      }
    }
  }

  return false;
}

static MCDisassembler *createX86Disassembler(const Target &T,
                                             const MCSubtargetInfo &STI,
                                             MCContext &Ctx) {
  std::unique_ptr<const MCInstrInfo> MII(T.createMCInstrInfo());
  return new X86GenericDisassembler(STI, Ctx, std::move(MII));
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeX86Disassembler() {
  // Register the disassembler.
  TargetRegistry::RegisterMCDisassembler(getTheX86_32Target(),
                                         createX86Disassembler);
  TargetRegistry::RegisterMCDisassembler(getTheX86_64Target(),
                                         createX86Disassembler);
}
