//==- WebAssemblyAsmTypeCheck.cpp - Assembler for WebAssembly -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file is part of the WebAssembly Assembler.
///
/// It contains code to translate a parsed .s file into MCInsts.
///
//===----------------------------------------------------------------------===//

#include "AsmParser/WebAssemblyAsmTypeCheck.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "MCTargetDesc/WebAssemblyMCTypeUtilities.h"
#include "MCTargetDesc/WebAssemblyTargetStreamer.h"
#include "TargetInfo/WebAssemblyTargetInfo.h"
#include "WebAssembly.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

#define DEBUG_TYPE "wasm-asm-parser"

extern StringRef GetMnemonic(unsigned Opc);

namespace llvm {

WebAssemblyAsmTypeCheck::WebAssemblyAsmTypeCheck(MCAsmParser &Parser,
                                                 const MCInstrInfo &MII,
                                                 bool is64)
    : Parser(Parser), MII(MII), is64(is64) {}

void WebAssemblyAsmTypeCheck::funcDecl(const wasm::WasmSignature &Sig) {
  LocalTypes.assign(Sig.Params.begin(), Sig.Params.end());
  ReturnTypes.assign(Sig.Returns.begin(), Sig.Returns.end());
  BrStack.emplace_back(Sig.Returns.begin(), Sig.Returns.end());
}

void WebAssemblyAsmTypeCheck::localDecl(
    const SmallVectorImpl<wasm::ValType> &Locals) {
  LocalTypes.insert(LocalTypes.end(), Locals.begin(), Locals.end());
}

void WebAssemblyAsmTypeCheck::dumpTypeStack(Twine Msg) {
  LLVM_DEBUG({
    std::string s;
    for (auto VT : Stack) {
      s += WebAssembly::typeToString(VT);
      s += " ";
    }
    dbgs() << Msg << s << '\n';
  });
}

bool WebAssemblyAsmTypeCheck::typeError(SMLoc ErrorLoc, const Twine &Msg) {
  // Once you get one type error in a function, it will likely trigger more
  // which are mostly not helpful.
  if (TypeErrorThisFunction)
    return true;
  // If we're currently in unreachable code, we suppress errors completely.
  if (Unreachable)
    return false;
  TypeErrorThisFunction = true;
  dumpTypeStack("current stack: ");
  return Parser.Error(ErrorLoc, Msg);
}

bool WebAssemblyAsmTypeCheck::popType(SMLoc ErrorLoc,
                                      std::optional<wasm::ValType> EVT) {
  if (Stack.empty()) {
    return typeError(ErrorLoc,
                     EVT ? StringRef("empty stack while popping ") +
                               WebAssembly::typeToString(*EVT)
                         : StringRef("empty stack while popping value"));
  }
  auto PVT = Stack.pop_back_val();
  if (EVT && *EVT != PVT) {
    return typeError(ErrorLoc,
                     StringRef("popped ") + WebAssembly::typeToString(PVT) +
                         ", expected " + WebAssembly::typeToString(*EVT));
  }
  return false;
}

bool WebAssemblyAsmTypeCheck::popRefType(SMLoc ErrorLoc) {
  if (Stack.empty()) {
    return typeError(ErrorLoc, StringRef("empty stack while popping reftype"));
  }
  auto PVT = Stack.pop_back_val();
  if (!WebAssembly::isRefType(PVT)) {
    return typeError(ErrorLoc, StringRef("popped ") +
                                   WebAssembly::typeToString(PVT) +
                                   ", expected reftype");
  }
  return false;
}

bool WebAssemblyAsmTypeCheck::getLocal(SMLoc ErrorLoc, const MCInst &Inst,
                                       wasm::ValType &Type) {
  auto Local = static_cast<size_t>(Inst.getOperand(0).getImm());
  if (Local >= LocalTypes.size())
    return typeError(ErrorLoc, StringRef("no local type specified for index ") +
                                   std::to_string(Local));
  Type = LocalTypes[Local];
  return false;
}

static std::optional<std::string>
checkStackTop(const SmallVectorImpl<wasm::ValType> &ExpectedStackTop,
              const SmallVectorImpl<wasm::ValType> &Got) {
  for (size_t I = 0; I < ExpectedStackTop.size(); I++) {
    auto EVT = ExpectedStackTop[I];
    auto PVT = Got[Got.size() - ExpectedStackTop.size() + I];
    if (PVT != EVT)
      return std::string{"got "} + WebAssembly::typeToString(PVT) +
             ", expected " + WebAssembly::typeToString(EVT);
  }
  return std::nullopt;
}

bool WebAssemblyAsmTypeCheck::checkBr(SMLoc ErrorLoc, size_t Level) {
  if (Level >= BrStack.size())
    return typeError(ErrorLoc,
                     StringRef("br: invalid depth ") + std::to_string(Level));
  const SmallVector<wasm::ValType, 4> &Expected =
      BrStack[BrStack.size() - Level - 1];
  if (Expected.size() > Stack.size())
    return typeError(ErrorLoc, "br: insufficient values on the type stack");
  auto IsStackTopInvalid = checkStackTop(Expected, Stack);
  if (IsStackTopInvalid)
    return typeError(ErrorLoc, "br " + IsStackTopInvalid.value());
  return false;
}

bool WebAssemblyAsmTypeCheck::checkEnd(SMLoc ErrorLoc, bool PopVals) {
  if (!PopVals)
    BrStack.pop_back();
  if (LastSig.Returns.size() > Stack.size())
    return typeError(ErrorLoc, "end: insufficient values on the type stack");

  if (PopVals) {
    for (auto VT : llvm::reverse(LastSig.Returns)) {
      if (popType(ErrorLoc, VT))
        return true;
    }
    return false;
  }

  auto IsStackTopInvalid = checkStackTop(LastSig.Returns, Stack);
  if (IsStackTopInvalid)
    return typeError(ErrorLoc, "end " + IsStackTopInvalid.value());
  return false;
}

bool WebAssemblyAsmTypeCheck::checkSig(SMLoc ErrorLoc,
                                       const wasm::WasmSignature &Sig) {
  for (auto VT : llvm::reverse(Sig.Params))
    if (popType(ErrorLoc, VT))
      return true;
  Stack.insert(Stack.end(), Sig.Returns.begin(), Sig.Returns.end());
  return false;
}

bool WebAssemblyAsmTypeCheck::getSymRef(SMLoc ErrorLoc, const MCInst &Inst,
                                        const MCSymbolRefExpr *&SymRef) {
  auto Op = Inst.getOperand(0);
  if (!Op.isExpr())
    return typeError(ErrorLoc, StringRef("expected expression operand"));
  SymRef = dyn_cast<MCSymbolRefExpr>(Op.getExpr());
  if (!SymRef)
    return typeError(ErrorLoc, StringRef("expected symbol operand"));
  return false;
}

bool WebAssemblyAsmTypeCheck::getGlobal(SMLoc ErrorLoc, const MCInst &Inst,
                                        wasm::ValType &Type) {
  const MCSymbolRefExpr *SymRef;
  if (getSymRef(ErrorLoc, Inst, SymRef))
    return true;
  auto WasmSym = cast<MCSymbolWasm>(&SymRef->getSymbol());
  switch (WasmSym->getType().value_or(wasm::WASM_SYMBOL_TYPE_DATA)) {
  case wasm::WASM_SYMBOL_TYPE_GLOBAL:
    Type = static_cast<wasm::ValType>(WasmSym->getGlobalType().Type);
    break;
  case wasm::WASM_SYMBOL_TYPE_FUNCTION:
  case wasm::WASM_SYMBOL_TYPE_DATA:
    switch (SymRef->getKind()) {
    case MCSymbolRefExpr::VK_GOT:
    case MCSymbolRefExpr::VK_WASM_GOT_TLS:
      Type = is64 ? wasm::ValType::I64 : wasm::ValType::I32;
      return false;
    default:
      break;
    }
    [[fallthrough]];
  default:
    return typeError(ErrorLoc, StringRef("symbol ") + WasmSym->getName() +
                                   " missing .globaltype");
  }
  return false;
}

bool WebAssemblyAsmTypeCheck::getTable(SMLoc ErrorLoc, const MCInst &Inst,
                                       wasm::ValType &Type) {
  const MCSymbolRefExpr *SymRef;
  if (getSymRef(ErrorLoc, Inst, SymRef))
    return true;
  auto WasmSym = cast<MCSymbolWasm>(&SymRef->getSymbol());
  if (WasmSym->getType().value_or(wasm::WASM_SYMBOL_TYPE_DATA) !=
      wasm::WASM_SYMBOL_TYPE_TABLE)
    return typeError(ErrorLoc, StringRef("symbol ") + WasmSym->getName() +
                                   " missing .tabletype");
  Type = static_cast<wasm::ValType>(WasmSym->getTableType().ElemType);
  return false;
}

bool WebAssemblyAsmTypeCheck::endOfFunction(SMLoc ErrorLoc) {
  // Check the return types.
  for (auto RVT : llvm::reverse(ReturnTypes)) {
    if (popType(ErrorLoc, RVT))
      return true;
  }
  if (!Stack.empty()) {
    return typeError(ErrorLoc, std::to_string(Stack.size()) +
                                   " superfluous return values");
  }
  Unreachable = true;
  return false;
}

bool WebAssemblyAsmTypeCheck::typeCheck(SMLoc ErrorLoc, const MCInst &Inst,
                                        OperandVector &Operands) {
  auto Opc = Inst.getOpcode();
  auto Name = GetMnemonic(Opc);
  dumpTypeStack("typechecking " + Name + ": ");
  wasm::ValType Type;
  if (Name == "local.get") {
    if (getLocal(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    Stack.push_back(Type);
  } else if (Name == "local.set") {
    if (getLocal(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
  } else if (Name == "local.tee") {
    if (getLocal(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
    Stack.push_back(Type);
  } else if (Name == "global.get") {
    if (getGlobal(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    Stack.push_back(Type);
  } else if (Name == "global.set") {
    if (getGlobal(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
  } else if (Name == "table.get") {
    if (getTable(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    Stack.push_back(Type);
  } else if (Name == "table.set") {
    if (getTable(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
  } else if (Name == "table.size") {
    if (getTable(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    Stack.push_back(wasm::ValType::I32);
  } else if (Name == "table.grow") {
    if (getTable(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
    Stack.push_back(wasm::ValType::I32);
  } else if (Name == "table.fill") {
    if (getTable(Operands[1]->getStartLoc(), Inst, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
  } else if (Name == "memory.fill") {
    Type = is64 ? wasm::ValType::I64 : wasm::ValType::I32;
    if (popType(ErrorLoc, Type))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
  } else if (Name == "memory.copy") {
    Type = is64 ? wasm::ValType::I64 : wasm::ValType::I32;
    if (popType(ErrorLoc, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
  } else if (Name == "memory.init") {
    Type = is64 ? wasm::ValType::I64 : wasm::ValType::I32;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (popType(ErrorLoc, Type))
      return true;
  } else if (Name == "drop") {
    if (popType(ErrorLoc, {}))
      return true;
  } else if (Name == "try" || Name == "block" || Name == "loop" ||
             Name == "if") {
    if (Name == "if" && popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (Name == "loop")
      BrStack.emplace_back(LastSig.Params.begin(), LastSig.Params.end());
    else
      BrStack.emplace_back(LastSig.Returns.begin(), LastSig.Returns.end());
  } else if (Name == "end_block" || Name == "end_loop" || Name == "end_if" ||
             Name == "else" || Name == "end_try" || Name == "catch" ||
             Name == "catch_all" || Name == "delegate") {
    if (checkEnd(ErrorLoc,
                 Name == "else" || Name == "catch" || Name == "catch_all"))
      return true;
    Unreachable = false;
    if (Name == "catch") {
      const MCSymbolRefExpr *SymRef;
      if (getSymRef(Operands[1]->getStartLoc(), Inst, SymRef))
        return true;
      const auto *WasmSym = cast<MCSymbolWasm>(&SymRef->getSymbol());
      const auto *Sig = WasmSym->getSignature();
      if (!Sig || WasmSym->getType() != wasm::WASM_SYMBOL_TYPE_TAG)
        return typeError(Operands[1]->getStartLoc(), StringRef("symbol ") +
                                                         WasmSym->getName() +
                                                         " missing .tagtype");
      // catch instruction pushes values whose types are specified in the tag's
      // "params" part
      Stack.insert(Stack.end(), Sig->Params.begin(), Sig->Params.end());
    }
  } else if (Name == "br") {
    const MCOperand &Operand = Inst.getOperand(0);
    if (!Operand.isImm())
      return false;
    if (checkBr(ErrorLoc, static_cast<size_t>(Operand.getImm())))
      return true;
  } else if (Name == "return") {
    if (endOfFunction(ErrorLoc))
      return true;
  } else if (Name == "call_indirect" || Name == "return_call_indirect") {
    // Function value.
    if (popType(ErrorLoc, wasm::ValType::I32))
      return true;
    if (checkSig(ErrorLoc, LastSig))
      return true;
    if (Name == "return_call_indirect" && endOfFunction(ErrorLoc))
      return true;
  } else if (Name == "call" || Name == "return_call") {
    const MCSymbolRefExpr *SymRef;
    if (getSymRef(Operands[1]->getStartLoc(), Inst, SymRef))
      return true;
    auto WasmSym = cast<MCSymbolWasm>(&SymRef->getSymbol());
    auto Sig = WasmSym->getSignature();
    if (!Sig || WasmSym->getType() != wasm::WASM_SYMBOL_TYPE_FUNCTION)
      return typeError(Operands[1]->getStartLoc(), StringRef("symbol ") +
                                                       WasmSym->getName() +
                                                       " missing .functype");
    if (checkSig(ErrorLoc, *Sig))
      return true;
    if (Name == "return_call" && endOfFunction(ErrorLoc))
      return true;
  } else if (Name == "unreachable") {
    Unreachable = true;
  } else if (Name == "ref.is_null") {
    if (popRefType(ErrorLoc))
      return true;
    Stack.push_back(wasm::ValType::I32);
  } else {
    // The current instruction is a stack instruction which doesn't have
    // explicit operands that indicate push/pop types, so we get those from
    // the register version of the same instruction.
    auto RegOpc = WebAssembly::getRegisterOpcode(Opc);
    assert(RegOpc != -1 && "Failed to get register version of MC instruction");
    const auto &II = MII.get(RegOpc);
    // First pop all the uses off the stack and check them.
    for (unsigned I = II.getNumOperands(); I > II.getNumDefs(); I--) {
      const auto &Op = II.operands()[I - 1];
      if (Op.OperandType == MCOI::OPERAND_REGISTER) {
        auto VT = WebAssembly::regClassToValType(Op.RegClass);
        if (popType(ErrorLoc, VT))
          return true;
      }
    }
    // Now push all the defs onto the stack.
    for (unsigned I = 0; I < II.getNumDefs(); I++) {
      const auto &Op = II.operands()[I];
      assert(Op.OperandType == MCOI::OPERAND_REGISTER && "Register expected");
      auto VT = WebAssembly::regClassToValType(Op.RegClass);
      Stack.push_back(VT);
    }
  }
  return false;
}

} // end namespace llvm
