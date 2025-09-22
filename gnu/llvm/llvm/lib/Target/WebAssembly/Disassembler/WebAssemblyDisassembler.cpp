//==- WebAssemblyDisassembler.cpp - Disassembler for WebAssembly -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the WebAssembly Disassembler.
///
/// It contains code to translate the data produced by the decoder into
/// MCInsts.
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTypeUtilities.h"
#include "TargetInfo/WebAssemblyTargetInfo.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/LEB128.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-disassembler"

using DecodeStatus = MCDisassembler::DecodeStatus;

#include "WebAssemblyGenDisassemblerTables.inc"

namespace {
static constexpr int WebAssemblyInstructionTableSize = 256;

class WebAssemblyDisassembler final : public MCDisassembler {
  std::unique_ptr<const MCInstrInfo> MCII;

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;

  Expected<bool> onSymbolStart(SymbolInfoTy &Symbol, uint64_t &Size,
                               ArrayRef<uint8_t> Bytes,
                               uint64_t Address) const override;

public:
  WebAssemblyDisassembler(const MCSubtargetInfo &STI, MCContext &Ctx,
                          std::unique_ptr<const MCInstrInfo> MCII)
      : MCDisassembler(STI, Ctx), MCII(std::move(MCII)) {}
};
} // end anonymous namespace

static MCDisassembler *createWebAssemblyDisassembler(const Target &T,
                                                     const MCSubtargetInfo &STI,
                                                     MCContext &Ctx) {
  std::unique_ptr<const MCInstrInfo> MCII(T.createMCInstrInfo());
  return new WebAssemblyDisassembler(STI, Ctx, std::move(MCII));
}

extern "C" LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeWebAssemblyDisassembler() {
  // Register the disassembler for each target.
  TargetRegistry::RegisterMCDisassembler(getTheWebAssemblyTarget32(),
                                         createWebAssemblyDisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheWebAssemblyTarget64(),
                                         createWebAssemblyDisassembler);
}

static int nextByte(ArrayRef<uint8_t> Bytes, uint64_t &Size) {
  if (Size >= Bytes.size())
    return -1;
  auto V = Bytes[Size];
  Size++;
  return V;
}

static bool nextLEB(int64_t &Val, ArrayRef<uint8_t> Bytes, uint64_t &Size,
                    bool Signed) {
  unsigned N = 0;
  const char *Error = nullptr;
  Val = Signed ? decodeSLEB128(Bytes.data() + Size, &N,
                               Bytes.data() + Bytes.size(), &Error)
               : static_cast<int64_t>(decodeULEB128(Bytes.data() + Size, &N,
                                                    Bytes.data() + Bytes.size(),
                                                    &Error));
  if (Error)
    return false;
  Size += N;
  return true;
}

static bool parseLEBImmediate(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, bool Signed) {
  int64_t Val;
  if (!nextLEB(Val, Bytes, Size, Signed))
    return false;
  MI.addOperand(MCOperand::createImm(Val));
  return true;
}

template <typename T>
bool parseImmediate(MCInst &MI, uint64_t &Size, ArrayRef<uint8_t> Bytes) {
  if (Size + sizeof(T) > Bytes.size())
    return false;
  T Val =
      support::endian::read<T, llvm::endianness::little>(Bytes.data() + Size);
  Size += sizeof(T);
  if (std::is_floating_point<T>::value) {
    MI.addOperand(
        MCOperand::createDFPImm(bit_cast<uint64_t>(static_cast<double>(Val))));
  } else {
    MI.addOperand(MCOperand::createImm(static_cast<int64_t>(Val)));
  }
  return true;
}

Expected<bool> WebAssemblyDisassembler::onSymbolStart(SymbolInfoTy &Symbol,
                                                      uint64_t &Size,
                                                      ArrayRef<uint8_t> Bytes,
                                                      uint64_t Address) const {
  Size = 0;
  if (Symbol.Type == wasm::WASM_SYMBOL_TYPE_SECTION) {
    // Start of a code section: we're parsing only the function count.
    int64_t FunctionCount;
    if (!nextLEB(FunctionCount, Bytes, Size, false))
      return false;
    outs() << "        # " << FunctionCount << " functions in section.";
  } else {
    // Parse the start of a single function.
    int64_t BodySize, LocalEntryCount;
    if (!nextLEB(BodySize, Bytes, Size, false) ||
        !nextLEB(LocalEntryCount, Bytes, Size, false))
      return false;
    if (LocalEntryCount) {
      outs() << "        .local ";
      for (int64_t I = 0; I < LocalEntryCount; I++) {
        int64_t Count, Type;
        if (!nextLEB(Count, Bytes, Size, false) ||
            !nextLEB(Type, Bytes, Size, false))
          return false;
        for (int64_t J = 0; J < Count; J++) {
          if (I || J)
            outs() << ", ";
          outs() << WebAssembly::anyTypeToString(Type);
        }
      }
    }
  }
  outs() << "\n";
  return true;
}

MCDisassembler::DecodeStatus WebAssemblyDisassembler::getInstruction(
    MCInst &MI, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t /*Address*/,
    raw_ostream &CS) const {
  CommentStream = &CS;
  Size = 0;
  int Opc = nextByte(Bytes, Size);
  if (Opc < 0)
    return MCDisassembler::Fail;
  const auto *WasmInst = &InstructionTable0[Opc];
  // If this is a prefix byte, indirect to another table.
  if (WasmInst->ET == ET_Prefix) {
    WasmInst = nullptr;
    // Linear search, so far only 2 entries.
    for (auto PT = PrefixTable; PT->Table; PT++) {
      if (PT->Prefix == Opc) {
        WasmInst = PT->Table;
        break;
      }
    }
    if (!WasmInst)
      return MCDisassembler::Fail;
    int64_t PrefixedOpc;
    if (!nextLEB(PrefixedOpc, Bytes, Size, false))
      return MCDisassembler::Fail;
    if (PrefixedOpc < 0 || PrefixedOpc >= WebAssemblyInstructionTableSize)
      return MCDisassembler::Fail;
    WasmInst += PrefixedOpc;
  }
  if (WasmInst->ET == ET_Unused)
    return MCDisassembler::Fail;
  // At this point we must have a valid instruction to decode.
  assert(WasmInst->ET == ET_Instruction);
  MI.setOpcode(WasmInst->Opcode);
  // Parse any operands.
  for (uint8_t OPI = 0; OPI < WasmInst->NumOperands; OPI++) {
    auto OT = OperandTable[WasmInst->OperandStart + OPI];
    switch (OT) {
    // ULEB operands:
    case WebAssembly::OPERAND_BASIC_BLOCK:
    case WebAssembly::OPERAND_LOCAL:
    case WebAssembly::OPERAND_GLOBAL:
    case WebAssembly::OPERAND_FUNCTION32:
    case WebAssembly::OPERAND_TABLE:
    case WebAssembly::OPERAND_OFFSET32:
    case WebAssembly::OPERAND_OFFSET64:
    case WebAssembly::OPERAND_P2ALIGN:
    case WebAssembly::OPERAND_TYPEINDEX:
    case WebAssembly::OPERAND_TAG:
    case MCOI::OPERAND_IMMEDIATE: {
      if (!parseLEBImmediate(MI, Size, Bytes, false))
        return MCDisassembler::Fail;
      break;
    }
    // SLEB operands:
    case WebAssembly::OPERAND_I32IMM:
    case WebAssembly::OPERAND_I64IMM: {
      if (!parseLEBImmediate(MI, Size, Bytes, true))
        return MCDisassembler::Fail;
      break;
    }
    // block_type operands:
    case WebAssembly::OPERAND_SIGNATURE: {
      int64_t Val;
      uint64_t PrevSize = Size;
      if (!nextLEB(Val, Bytes, Size, true))
        return MCDisassembler::Fail;
      if (Val < 0) {
        // Negative values are single septet value types or empty types
        if (Size != PrevSize + 1) {
          MI.addOperand(
              MCOperand::createImm(int64_t(WebAssembly::BlockType::Invalid)));
        } else {
          MI.addOperand(MCOperand::createImm(Val & 0x7f));
        }
      } else {
        // We don't have access to the signature, so create a symbol without one
        MCSymbol *Sym = getContext().createTempSymbol("typeindex", true);
        auto *WasmSym = cast<MCSymbolWasm>(Sym);
        WasmSym->setType(wasm::WASM_SYMBOL_TYPE_FUNCTION);
        const MCExpr *Expr = MCSymbolRefExpr::create(
            WasmSym, MCSymbolRefExpr::VK_WASM_TYPEINDEX, getContext());
        MI.addOperand(MCOperand::createExpr(Expr));
      }
      break;
    }
    // FP operands.
    case WebAssembly::OPERAND_F32IMM: {
      if (!parseImmediate<float>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    case WebAssembly::OPERAND_F64IMM: {
      if (!parseImmediate<double>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    // Vector lane operands (not LEB encoded).
    case WebAssembly::OPERAND_VEC_I8IMM: {
      if (!parseImmediate<uint8_t>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    case WebAssembly::OPERAND_VEC_I16IMM: {
      if (!parseImmediate<uint16_t>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    case WebAssembly::OPERAND_VEC_I32IMM: {
      if (!parseImmediate<uint32_t>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    case WebAssembly::OPERAND_VEC_I64IMM: {
      if (!parseImmediate<uint64_t>(MI, Size, Bytes))
        return MCDisassembler::Fail;
      break;
    }
    case WebAssembly::OPERAND_BRLIST: {
      int64_t TargetTableLen;
      if (!nextLEB(TargetTableLen, Bytes, Size, false))
        return MCDisassembler::Fail;
      for (int64_t I = 0; I < TargetTableLen; I++) {
        if (!parseLEBImmediate(MI, Size, Bytes, false))
          return MCDisassembler::Fail;
      }
      // Default case.
      if (!parseLEBImmediate(MI, Size, Bytes, false))
        return MCDisassembler::Fail;
      break;
    }
    case MCOI::OPERAND_REGISTER:
      // The tablegen header currently does not have any register operands since
      // we use only the stack (_S) instructions.
      // If you hit this that probably means a bad instruction definition in
      // tablegen.
      llvm_unreachable("Register operand in WebAssemblyDisassembler");
    default:
      llvm_unreachable("Unknown operand type in WebAssemblyDisassembler");
    }
  }
  return MCDisassembler::Success;
}
