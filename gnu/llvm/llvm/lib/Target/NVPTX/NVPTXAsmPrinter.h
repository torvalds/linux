//===-- NVPTXAsmPrinter.h - NVPTX LLVM assembly writer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to NVPTX assembly language.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H
#define LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H

#include "NVPTX.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The ptx syntax and format is very different from that usually seem in a .s
// file,
// therefore we are not able to use the MCAsmStreamer interface here.
//
// We are handcrafting the output method here.
//
// A better approach is to clone the MCAsmStreamer to a MCPTXAsmStreamer
// (subclass of MCStreamer).

namespace llvm {

class MCOperand;

class LLVM_LIBRARY_VISIBILITY NVPTXAsmPrinter : public AsmPrinter {

  class AggBuffer {
    // Used to buffer the emitted string for initializing global aggregates.
    //
    // Normally an aggregate (array, vector, or structure) is emitted as a u8[].
    // However, if either element/field of the aggregate is a non-NULL address,
    // and all such addresses are properly aligned, then the aggregate is
    // emitted as u32[] or u64[]. In the case of unaligned addresses, the
    // aggregate is emitted as u8[], and the mask() operator is used for all
    // pointers.
    //
    // We first layout the aggregate in 'buffer' in bytes, except for those
    // symbol addresses. For the i-th symbol address in the aggregate, its
    // corresponding 4-byte or 8-byte elements in 'buffer' are filled with 0s.
    // symbolPosInBuffer[i-1] records its position in 'buffer', and Symbols[i-1]
    // records the Value*.
    //
    // Once we have this AggBuffer setup, we can choose how to print it out.
  public:
    // number of symbol addresses
    unsigned numSymbols() const { return Symbols.size(); }

    bool allSymbolsAligned(unsigned ptrSize) const {
      return llvm::all_of(symbolPosInBuffer,
                          [=](unsigned pos) { return pos % ptrSize == 0; });
    }

  private:
    const unsigned size;   // size of the buffer in bytes
    std::vector<unsigned char> buffer; // the buffer
    SmallVector<unsigned, 4> symbolPosInBuffer;
    SmallVector<const Value *, 4> Symbols;
    // SymbolsBeforeStripping[i] is the original form of Symbols[i] before
    // stripping pointer casts, i.e.,
    // Symbols[i] == SymbolsBeforeStripping[i]->stripPointerCasts().
    //
    // We need to keep these values because AggBuffer::print decides whether to
    // emit a "generic()" cast for Symbols[i] depending on the address space of
    // SymbolsBeforeStripping[i].
    SmallVector<const Value *, 4> SymbolsBeforeStripping;
    unsigned curpos;
    NVPTXAsmPrinter &AP;
    bool EmitGeneric;

  public:
    AggBuffer(unsigned size, NVPTXAsmPrinter &AP)
        : size(size), buffer(size), AP(AP) {
      curpos = 0;
      EmitGeneric = AP.EmitGeneric;
    }

    // Copy Num bytes from Ptr.
    // if Bytes > Num, zero fill up to Bytes.
    unsigned addBytes(unsigned char *Ptr, int Num, int Bytes) {
      assert((curpos + Num) <= size);
      assert((curpos + Bytes) <= size);
      for (int i = 0; i < Num; ++i) {
        buffer[curpos] = Ptr[i];
        curpos++;
      }
      for (int i = Num; i < Bytes; ++i) {
        buffer[curpos] = 0;
        curpos++;
      }
      return curpos;
    }

    unsigned addZeros(int Num) {
      assert((curpos + Num) <= size);
      for (int i = 0; i < Num; ++i) {
        buffer[curpos] = 0;
        curpos++;
      }
      return curpos;
    }

    void addSymbol(const Value *GVar, const Value *GVarBeforeStripping) {
      symbolPosInBuffer.push_back(curpos);
      Symbols.push_back(GVar);
      SymbolsBeforeStripping.push_back(GVarBeforeStripping);
    }

    void printBytes(raw_ostream &os);
    void printWords(raw_ostream &os);

  private:
    void printSymbol(unsigned nSym, raw_ostream &os);
  };

  friend class AggBuffer;

private:
  StringRef getPassName() const override { return "NVPTX Assembly Printer"; }

  const Function *F;
  std::string CurrentFnName;

  void emitStartOfAsmFile(Module &M) override;
  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;
  void emitFunctionEntryLabel() override;
  void emitFunctionBodyStart() override;
  void emitFunctionBodyEnd() override;
  void emitImplicitDef(const MachineInstr *MI) const override;

  void emitInstruction(const MachineInstr *) override;
  void lowerToMCInst(const MachineInstr *MI, MCInst &OutMI);
  bool lowerOperand(const MachineOperand &MO, MCOperand &MCOp);
  MCOperand GetSymbolRef(const MCSymbol *Symbol);
  unsigned encodeVirtualRegister(unsigned Reg);

  void printMemOperand(const MachineInstr *MI, unsigned OpNum, raw_ostream &O,
                       const char *Modifier = nullptr);
  void printModuleLevelGV(const GlobalVariable *GVar, raw_ostream &O,
                          bool processDemoted, const NVPTXSubtarget &STI);
  void emitGlobals(const Module &M);
  void emitGlobalAlias(const Module &M, const GlobalAlias &GA) override;
  void emitHeader(Module &M, raw_ostream &O, const NVPTXSubtarget &STI);
  void emitKernelFunctionDirectives(const Function &F, raw_ostream &O) const;
  void emitVirtualRegister(unsigned int vr, raw_ostream &);
  void emitFunctionParamList(const Function *, raw_ostream &O);
  void setAndEmitFunctionVirtualRegisters(const MachineFunction &MF);
  void printReturnValStr(const Function *, raw_ostream &O);
  void printReturnValStr(const MachineFunction &MF, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &) override;
  void printOperand(const MachineInstr *MI, unsigned OpNum, raw_ostream &O);
  bool PrintAsmMemoryOperand(const MachineInstr *MI, unsigned OpNo,
                             const char *ExtraCode, raw_ostream &) override;

  const MCExpr *lowerConstantForGV(const Constant *CV, bool ProcessingGeneric);
  void printMCExpr(const MCExpr &Expr, raw_ostream &OS);

protected:
  bool doInitialization(Module &M) override;
  bool doFinalization(Module &M) override;

private:
  bool GlobalsEmitted;

  // This is specific per MachineFunction.
  const MachineRegisterInfo *MRI;
  // The contents are specific for each
  // MachineFunction. But the size of the
  // array is not.
  typedef DenseMap<unsigned, unsigned> VRegMap;
  typedef DenseMap<const TargetRegisterClass *, VRegMap> VRegRCMap;
  VRegRCMap VRegMapping;

  // List of variables demoted to a function scope.
  std::map<const Function *, std::vector<const GlobalVariable *>> localDecls;

  void emitPTXGlobalVariable(const GlobalVariable *GVar, raw_ostream &O,
                             const NVPTXSubtarget &STI);
  void emitPTXAddressSpace(unsigned int AddressSpace, raw_ostream &O) const;
  std::string getPTXFundamentalTypeStr(Type *Ty, bool = true) const;
  void printScalarConstant(const Constant *CPV, raw_ostream &O);
  void printFPConstant(const ConstantFP *Fp, raw_ostream &O);
  void bufferLEByte(const Constant *CPV, int Bytes, AggBuffer *aggBuffer);
  void bufferAggregateConstant(const Constant *CV, AggBuffer *aggBuffer);

  void emitLinkageDirective(const GlobalValue *V, raw_ostream &O);
  void emitDeclarations(const Module &, raw_ostream &O);
  void emitDeclaration(const Function *, raw_ostream &O);
  void emitAliasDeclaration(const GlobalAlias *, raw_ostream &O);
  void emitDeclarationWithName(const Function *, MCSymbol *, raw_ostream &O);
  void emitDemotedVars(const Function *, raw_ostream &);

  bool lowerImageHandleOperand(const MachineInstr *MI, unsigned OpNo,
                               MCOperand &MCOp);
  void lowerImageHandleSymbol(unsigned Index, MCOperand &MCOp);

  bool isLoopHeaderOfNoUnroll(const MachineBasicBlock &MBB) const;

  // Used to control the need to emit .generic() in the initializer of
  // module scope variables.
  // Although ptx supports the hybrid mode like the following,
  //    .global .u32 a;
  //    .global .u32 b;
  //    .global .u32 addr[] = {a, generic(b)}
  // we have difficulty representing the difference in the NVVM IR.
  //
  // Since the address value should always be generic in CUDA C and always
  // be specific in OpenCL, we use this simple control here.
  //
  bool EmitGeneric;

public:
  NVPTXAsmPrinter(TargetMachine &TM, std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)),
        EmitGeneric(static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() ==
                    NVPTX::CUDA) {}

  bool runOnMachineFunction(MachineFunction &F) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<MachineLoopInfoWrapperPass>();
    AsmPrinter::getAnalysisUsage(AU);
  }

  std::string getVirtualRegisterName(unsigned) const;

  const MCSymbol *getFunctionFrameSymbol() const override;

  // Make emitGlobalVariable() no-op for NVPTX.
  // Global variables have been already emitted by the time the base AsmPrinter
  // attempts to do so in doFinalization() (see NVPTXAsmPrinter::emitGlobals()).
  void emitGlobalVariable(const GlobalVariable *GV) override {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_NVPTX_NVPTXASMPRINTER_H
