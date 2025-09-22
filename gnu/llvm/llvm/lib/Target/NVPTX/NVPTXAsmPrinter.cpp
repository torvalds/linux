//===-- NVPTXAsmPrinter.cpp - NVPTX LLVM assembly writer ------------------===//
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

#include "NVPTXAsmPrinter.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "MCTargetDesc/NVPTXInstPrinter.h"
#include "MCTargetDesc/NVPTXMCAsmInfo.h"
#include "MCTargetDesc/NVPTXTargetStreamer.h"
#include "NVPTX.h"
#include "NVPTXMCExpr.h"
#include "NVPTXMachineFunctionInfo.h"
#include "NVPTXRegisterInfo.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXUtilities.h"
#include "TargetInfo/NVPTXTargetInfo.h"
#include "cl_common_defines.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/NativeFormatting.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/Utils/UnrollLoop.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

using namespace llvm;

static cl::opt<bool>
    LowerCtorDtor("nvptx-lower-global-ctor-dtor",
                  cl::desc("Lower GPU ctor / dtors to globals on the device."),
                  cl::init(false), cl::Hidden);

#define DEPOTNAME "__local_depot"

/// DiscoverDependentGlobals - Return a set of GlobalVariables on which \p V
/// depends.
static void
DiscoverDependentGlobals(const Value *V,
                         DenseSet<const GlobalVariable *> &Globals) {
  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(V))
    Globals.insert(GV);
  else {
    if (const User *U = dyn_cast<User>(V)) {
      for (unsigned i = 0, e = U->getNumOperands(); i != e; ++i) {
        DiscoverDependentGlobals(U->getOperand(i), Globals);
      }
    }
  }
}

/// VisitGlobalVariableForEmission - Add \p GV to the list of GlobalVariable
/// instances to be emitted, but only after any dependents have been added
/// first.s
static void
VisitGlobalVariableForEmission(const GlobalVariable *GV,
                               SmallVectorImpl<const GlobalVariable *> &Order,
                               DenseSet<const GlobalVariable *> &Visited,
                               DenseSet<const GlobalVariable *> &Visiting) {
  // Have we already visited this one?
  if (Visited.count(GV))
    return;

  // Do we have a circular dependency?
  if (!Visiting.insert(GV).second)
    report_fatal_error("Circular dependency found in global variable set");

  // Make sure we visit all dependents first
  DenseSet<const GlobalVariable *> Others;
  for (unsigned i = 0, e = GV->getNumOperands(); i != e; ++i)
    DiscoverDependentGlobals(GV->getOperand(i), Others);

  for (const GlobalVariable *GV : Others)
    VisitGlobalVariableForEmission(GV, Order, Visited, Visiting);

  // Now we can visit ourself
  Order.push_back(GV);
  Visited.insert(GV);
  Visiting.erase(GV);
}

void NVPTXAsmPrinter::emitInstruction(const MachineInstr *MI) {
  NVPTX_MC::verifyInstructionPredicates(MI->getOpcode(),
                                        getSubtargetInfo().getFeatureBits());

  MCInst Inst;
  lowerToMCInst(MI, Inst);
  EmitToStreamer(*OutStreamer, Inst);
}

// Handle symbol backtracking for targets that do not support image handles
bool NVPTXAsmPrinter::lowerImageHandleOperand(const MachineInstr *MI,
                                           unsigned OpNo, MCOperand &MCOp) {
  const MachineOperand &MO = MI->getOperand(OpNo);
  const MCInstrDesc &MCID = MI->getDesc();

  if (MCID.TSFlags & NVPTXII::IsTexFlag) {
    // This is a texture fetch, so operand 4 is a texref and operand 5 is
    // a samplerref
    if (OpNo == 4 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }
    if (OpNo == 5 && MO.isImm() && !(MCID.TSFlags & NVPTXII::IsTexModeUnifiedFlag)) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSuldMask) {
    unsigned VecSize =
      1 << (((MCID.TSFlags & NVPTXII::IsSuldMask) >> NVPTXII::IsSuldShift) - 1);

    // For a surface load of vector size N, the Nth operand will be the surfref
    if (OpNo == VecSize && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSustFlag) {
    // This is a surface store, so operand 0 is a surfref
    if (OpNo == 0 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  } else if (MCID.TSFlags & NVPTXII::IsSurfTexQueryFlag) {
    // This is a query, so operand 1 is a surfref/texref
    if (OpNo == 1 && MO.isImm()) {
      lowerImageHandleSymbol(MO.getImm(), MCOp);
      return true;
    }

    return false;
  }

  return false;
}

void NVPTXAsmPrinter::lowerImageHandleSymbol(unsigned Index, MCOperand &MCOp) {
  // Ewwww
  LLVMTargetMachine &TM = const_cast<LLVMTargetMachine&>(MF->getTarget());
  NVPTXTargetMachine &nvTM = static_cast<NVPTXTargetMachine&>(TM);
  const NVPTXMachineFunctionInfo *MFI = MF->getInfo<NVPTXMachineFunctionInfo>();
  const char *Sym = MFI->getImageHandleSymbol(Index);
  StringRef SymName = nvTM.getStrPool().save(Sym);
  MCOp = GetSymbolRef(OutContext.getOrCreateSymbol(SymName));
}

void NVPTXAsmPrinter::lowerToMCInst(const MachineInstr *MI, MCInst &OutMI) {
  OutMI.setOpcode(MI->getOpcode());
  // Special: Do not mangle symbol operand of CALL_PROTOTYPE
  if (MI->getOpcode() == NVPTX::CALL_PROTOTYPE) {
    const MachineOperand &MO = MI->getOperand(0);
    OutMI.addOperand(GetSymbolRef(
      OutContext.getOrCreateSymbol(Twine(MO.getSymbolName()))));
    return;
  }

  const NVPTXSubtarget &STI = MI->getMF()->getSubtarget<NVPTXSubtarget>();
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);

    MCOperand MCOp;
    if (!STI.hasImageHandles()) {
      if (lowerImageHandleOperand(MI, i, MCOp)) {
        OutMI.addOperand(MCOp);
        continue;
      }
    }

    if (lowerOperand(MO, MCOp))
      OutMI.addOperand(MCOp);
  }
}

bool NVPTXAsmPrinter::lowerOperand(const MachineOperand &MO,
                                   MCOperand &MCOp) {
  switch (MO.getType()) {
  default: llvm_unreachable("unknown operand type");
  case MachineOperand::MO_Register:
    MCOp = MCOperand::createReg(encodeVirtualRegister(MO.getReg()));
    break;
  case MachineOperand::MO_Immediate:
    MCOp = MCOperand::createImm(MO.getImm());
    break;
  case MachineOperand::MO_MachineBasicBlock:
    MCOp = MCOperand::createExpr(MCSymbolRefExpr::create(
        MO.getMBB()->getSymbol(), OutContext));
    break;
  case MachineOperand::MO_ExternalSymbol:
    MCOp = GetSymbolRef(GetExternalSymbolSymbol(MO.getSymbolName()));
    break;
  case MachineOperand::MO_GlobalAddress:
    MCOp = GetSymbolRef(getSymbol(MO.getGlobal()));
    break;
  case MachineOperand::MO_FPImmediate: {
    const ConstantFP *Cnt = MO.getFPImm();
    const APFloat &Val = Cnt->getValueAPF();

    switch (Cnt->getType()->getTypeID()) {
    default: report_fatal_error("Unsupported FP type"); break;
    case Type::HalfTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPHalf(Val, OutContext));
      break;
    case Type::BFloatTyID:
      MCOp = MCOperand::createExpr(
          NVPTXFloatMCExpr::createConstantBFPHalf(Val, OutContext));
      break;
    case Type::FloatTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPSingle(Val, OutContext));
      break;
    case Type::DoubleTyID:
      MCOp = MCOperand::createExpr(
        NVPTXFloatMCExpr::createConstantFPDouble(Val, OutContext));
      break;
    }
    break;
  }
  }
  return true;
}

unsigned NVPTXAsmPrinter::encodeVirtualRegister(unsigned Reg) {
  if (Register::isVirtualRegister(Reg)) {
    const TargetRegisterClass *RC = MRI->getRegClass(Reg);

    DenseMap<unsigned, unsigned> &RegMap = VRegMapping[RC];
    unsigned RegNum = RegMap[Reg];

    // Encode the register class in the upper 4 bits
    // Must be kept in sync with NVPTXInstPrinter::printRegName
    unsigned Ret = 0;
    if (RC == &NVPTX::Int1RegsRegClass) {
      Ret = (1 << 28);
    } else if (RC == &NVPTX::Int16RegsRegClass) {
      Ret = (2 << 28);
    } else if (RC == &NVPTX::Int32RegsRegClass) {
      Ret = (3 << 28);
    } else if (RC == &NVPTX::Int64RegsRegClass) {
      Ret = (4 << 28);
    } else if (RC == &NVPTX::Float32RegsRegClass) {
      Ret = (5 << 28);
    } else if (RC == &NVPTX::Float64RegsRegClass) {
      Ret = (6 << 28);
    } else if (RC == &NVPTX::Int128RegsRegClass) {
      Ret = (7 << 28);
    } else {
      report_fatal_error("Bad register class");
    }

    // Insert the vreg number
    Ret |= (RegNum & 0x0FFFFFFF);
    return Ret;
  } else {
    // Some special-use registers are actually physical registers.
    // Encode this as the register class ID of 0 and the real register ID.
    return Reg & 0x0FFFFFFF;
  }
}

MCOperand NVPTXAsmPrinter::GetSymbolRef(const MCSymbol *Symbol) {
  const MCExpr *Expr;
  Expr = MCSymbolRefExpr::create(Symbol, MCSymbolRefExpr::VK_None,
                                 OutContext);
  return MCOperand::createExpr(Expr);
}

static bool ShouldPassAsArray(Type *Ty) {
  return Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128) ||
         Ty->isHalfTy() || Ty->isBFloatTy();
}

void NVPTXAsmPrinter::printReturnValStr(const Function *F, raw_ostream &O) {
  const DataLayout &DL = getDataLayout();
  const NVPTXSubtarget &STI = TM.getSubtarget<NVPTXSubtarget>(*F);
  const auto *TLI = cast<NVPTXTargetLowering>(STI.getTargetLowering());

  Type *Ty = F->getReturnType();

  bool isABI = (STI.getSmVersion() >= 20);

  if (Ty->getTypeID() == Type::VoidTyID)
    return;
  O << " (";

  if (isABI) {
    if ((Ty->isFloatingPointTy() || Ty->isIntegerTy()) &&
        !ShouldPassAsArray(Ty)) {
      unsigned size = 0;
      if (auto *ITy = dyn_cast<IntegerType>(Ty)) {
        size = ITy->getBitWidth();
      } else {
        assert(Ty->isFloatingPointTy() && "Floating point type expected here");
        size = Ty->getPrimitiveSizeInBits();
      }
      size = promoteScalarArgumentSize(size);
      O << ".param .b" << size << " func_retval0";
    } else if (isa<PointerType>(Ty)) {
      O << ".param .b" << TLI->getPointerTy(DL).getSizeInBits()
        << " func_retval0";
    } else if (ShouldPassAsArray(Ty)) {
      unsigned totalsz = DL.getTypeAllocSize(Ty);
      Align RetAlignment = TLI->getFunctionArgumentAlignment(
          F, Ty, AttributeList::ReturnIndex, DL);
      O << ".param .align " << RetAlignment.value() << " .b8 func_retval0["
        << totalsz << "]";
    } else
      llvm_unreachable("Unknown return type");
  } else {
    SmallVector<EVT, 16> vtparts;
    ComputeValueVTs(*TLI, DL, Ty, vtparts);
    unsigned idx = 0;
    for (unsigned i = 0, e = vtparts.size(); i != e; ++i) {
      unsigned elems = 1;
      EVT elemtype = vtparts[i];
      if (vtparts[i].isVector()) {
        elems = vtparts[i].getVectorNumElements();
        elemtype = vtparts[i].getVectorElementType();
      }

      for (unsigned j = 0, je = elems; j != je; ++j) {
        unsigned sz = elemtype.getSizeInBits();
        if (elemtype.isInteger())
          sz = promoteScalarArgumentSize(sz);
        O << ".reg .b" << sz << " func_retval" << idx;
        if (j < je - 1)
          O << ", ";
        ++idx;
      }
      if (i < e - 1)
        O << ", ";
    }
  }
  O << ") ";
}

void NVPTXAsmPrinter::printReturnValStr(const MachineFunction &MF,
                                        raw_ostream &O) {
  const Function &F = MF.getFunction();
  printReturnValStr(&F, O);
}

// Return true if MBB is the header of a loop marked with
// llvm.loop.unroll.disable or llvm.loop.unroll.count=1.
bool NVPTXAsmPrinter::isLoopHeaderOfNoUnroll(
    const MachineBasicBlock &MBB) const {
  MachineLoopInfo &LI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();
  // We insert .pragma "nounroll" only to the loop header.
  if (!LI.isLoopHeader(&MBB))
    return false;

  // llvm.loop.unroll.disable is marked on the back edges of a loop. Therefore,
  // we iterate through each back edge of the loop with header MBB, and check
  // whether its metadata contains llvm.loop.unroll.disable.
  for (const MachineBasicBlock *PMBB : MBB.predecessors()) {
    if (LI.getLoopFor(PMBB) != LI.getLoopFor(&MBB)) {
      // Edges from other loops to MBB are not back edges.
      continue;
    }
    if (const BasicBlock *PBB = PMBB->getBasicBlock()) {
      if (MDNode *LoopID =
              PBB->getTerminator()->getMetadata(LLVMContext::MD_loop)) {
        if (GetUnrollMetadata(LoopID, "llvm.loop.unroll.disable"))
          return true;
        if (MDNode *UnrollCountMD =
                GetUnrollMetadata(LoopID, "llvm.loop.unroll.count")) {
          if (mdconst::extract<ConstantInt>(UnrollCountMD->getOperand(1))
                  ->isOne())
            return true;
        }
      }
    }
  }
  return false;
}

void NVPTXAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  AsmPrinter::emitBasicBlockStart(MBB);
  if (isLoopHeaderOfNoUnroll(MBB))
    OutStreamer->emitRawText(StringRef("\t.pragma \"nounroll\";\n"));
}

void NVPTXAsmPrinter::emitFunctionEntryLabel() {
  SmallString<128> Str;
  raw_svector_ostream O(Str);

  if (!GlobalsEmitted) {
    emitGlobals(*MF->getFunction().getParent());
    GlobalsEmitted = true;
  }

  // Set up
  MRI = &MF->getRegInfo();
  F = &MF->getFunction();
  emitLinkageDirective(F, O);
  if (isKernelFunction(*F))
    O << ".entry ";
  else {
    O << ".func ";
    printReturnValStr(*MF, O);
  }

  CurrentFnSym->print(O, MAI);

  emitFunctionParamList(F, O);
  O << "\n";

  if (isKernelFunction(*F))
    emitKernelFunctionDirectives(*F, O);

  if (shouldEmitPTXNoReturn(F, TM))
    O << ".noreturn";

  OutStreamer->emitRawText(O.str());

  VRegMapping.clear();
  // Emit open brace for function body.
  OutStreamer->emitRawText(StringRef("{\n"));
  setAndEmitFunctionVirtualRegisters(*MF);
  // Emit initial .loc debug directive for correct relocation symbol data.
  if (const DISubprogram *SP = MF->getFunction().getSubprogram()) {
    assert(SP->getUnit());
    if (!SP->getUnit()->isDebugDirectivesOnly() && MMI && MMI->hasDebugInfo())
      emitInitialRawDwarfLocDirective(*MF);
  }
}

bool NVPTXAsmPrinter::runOnMachineFunction(MachineFunction &F) {
  bool Result = AsmPrinter::runOnMachineFunction(F);
  // Emit closing brace for the body of function F.
  // The closing brace must be emitted here because we need to emit additional
  // debug labels/data after the last basic block.
  // We need to emit the closing brace here because we don't have function that
  // finished emission of the function body.
  OutStreamer->emitRawText(StringRef("}\n"));
  return Result;
}

void NVPTXAsmPrinter::emitFunctionBodyStart() {
  SmallString<128> Str;
  raw_svector_ostream O(Str);
  emitDemotedVars(&MF->getFunction(), O);
  OutStreamer->emitRawText(O.str());
}

void NVPTXAsmPrinter::emitFunctionBodyEnd() {
  VRegMapping.clear();
}

const MCSymbol *NVPTXAsmPrinter::getFunctionFrameSymbol() const {
    SmallString<128> Str;
    raw_svector_ostream(Str) << DEPOTNAME << getFunctionNumber();
    return OutContext.getOrCreateSymbol(Str);
}

void NVPTXAsmPrinter::emitImplicitDef(const MachineInstr *MI) const {
  Register RegNo = MI->getOperand(0).getReg();
  if (RegNo.isVirtual()) {
    OutStreamer->AddComment(Twine("implicit-def: ") +
                            getVirtualRegisterName(RegNo));
  } else {
    const NVPTXSubtarget &STI = MI->getMF()->getSubtarget<NVPTXSubtarget>();
    OutStreamer->AddComment(Twine("implicit-def: ") +
                            STI.getRegisterInfo()->getName(RegNo));
  }
  OutStreamer->addBlankLine();
}

void NVPTXAsmPrinter::emitKernelFunctionDirectives(const Function &F,
                                                   raw_ostream &O) const {
  // If the NVVM IR has some of reqntid* specified, then output
  // the reqntid directive, and set the unspecified ones to 1.
  // If none of Reqntid* is specified, don't output reqntid directive.
  std::optional<unsigned> Reqntidx = getReqNTIDx(F);
  std::optional<unsigned> Reqntidy = getReqNTIDy(F);
  std::optional<unsigned> Reqntidz = getReqNTIDz(F);

  if (Reqntidx || Reqntidy || Reqntidz)
    O << ".reqntid " << Reqntidx.value_or(1) << ", " << Reqntidy.value_or(1)
      << ", " << Reqntidz.value_or(1) << "\n";

  // If the NVVM IR has some of maxntid* specified, then output
  // the maxntid directive, and set the unspecified ones to 1.
  // If none of maxntid* is specified, don't output maxntid directive.
  std::optional<unsigned> Maxntidx = getMaxNTIDx(F);
  std::optional<unsigned> Maxntidy = getMaxNTIDy(F);
  std::optional<unsigned> Maxntidz = getMaxNTIDz(F);

  if (Maxntidx || Maxntidy || Maxntidz)
    O << ".maxntid " << Maxntidx.value_or(1) << ", " << Maxntidy.value_or(1)
      << ", " << Maxntidz.value_or(1) << "\n";

  unsigned Mincta = 0;
  if (getMinCTASm(F, Mincta))
    O << ".minnctapersm " << Mincta << "\n";

  unsigned Maxnreg = 0;
  if (getMaxNReg(F, Maxnreg))
    O << ".maxnreg " << Maxnreg << "\n";

  // .maxclusterrank directive requires SM_90 or higher, make sure that we
  // filter it out for lower SM versions, as it causes a hard ptxas crash.
  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const auto *STI = static_cast<const NVPTXSubtarget *>(NTM.getSubtargetImpl());
  unsigned Maxclusterrank = 0;
  if (getMaxClusterRank(F, Maxclusterrank) && STI->getSmVersion() >= 90)
    O << ".maxclusterrank " << Maxclusterrank << "\n";
}

std::string NVPTXAsmPrinter::getVirtualRegisterName(unsigned Reg) const {
  const TargetRegisterClass *RC = MRI->getRegClass(Reg);

  std::string Name;
  raw_string_ostream NameStr(Name);

  VRegRCMap::const_iterator I = VRegMapping.find(RC);
  assert(I != VRegMapping.end() && "Bad register class");
  const DenseMap<unsigned, unsigned> &RegMap = I->second;

  VRegMap::const_iterator VI = RegMap.find(Reg);
  assert(VI != RegMap.end() && "Bad virtual register");
  unsigned MappedVR = VI->second;

  NameStr << getNVPTXRegClassStr(RC) << MappedVR;

  NameStr.flush();
  return Name;
}

void NVPTXAsmPrinter::emitVirtualRegister(unsigned int vr,
                                          raw_ostream &O) {
  O << getVirtualRegisterName(vr);
}

void NVPTXAsmPrinter::emitAliasDeclaration(const GlobalAlias *GA,
                                           raw_ostream &O) {
  const Function *F = dyn_cast_or_null<Function>(GA->getAliaseeObject());
  if (!F || isKernelFunction(*F) || F->isDeclaration())
    report_fatal_error(
        "NVPTX aliasee must be a non-kernel function definition");

  if (GA->hasLinkOnceLinkage() || GA->hasWeakLinkage() ||
      GA->hasAvailableExternallyLinkage() || GA->hasCommonLinkage())
    report_fatal_error("NVPTX aliasee must not be '.weak'");

  emitDeclarationWithName(F, getSymbol(GA), O);
}

void NVPTXAsmPrinter::emitDeclaration(const Function *F, raw_ostream &O) {
  emitDeclarationWithName(F, getSymbol(F), O);
}

void NVPTXAsmPrinter::emitDeclarationWithName(const Function *F, MCSymbol *S,
                                              raw_ostream &O) {
  emitLinkageDirective(F, O);
  if (isKernelFunction(*F))
    O << ".entry ";
  else
    O << ".func ";
  printReturnValStr(F, O);
  S->print(O, MAI);
  O << "\n";
  emitFunctionParamList(F, O);
  O << "\n";
  if (shouldEmitPTXNoReturn(F, TM))
    O << ".noreturn";
  O << ";\n";
}

static bool usedInGlobalVarDef(const Constant *C) {
  if (!C)
    return false;

  if (const GlobalVariable *GV = dyn_cast<GlobalVariable>(C)) {
    return GV->getName() != "llvm.used";
  }

  for (const User *U : C->users())
    if (const Constant *C = dyn_cast<Constant>(U))
      if (usedInGlobalVarDef(C))
        return true;

  return false;
}

static bool usedInOneFunc(const User *U, Function const *&oneFunc) {
  if (const GlobalVariable *othergv = dyn_cast<GlobalVariable>(U)) {
    if (othergv->getName() == "llvm.used")
      return true;
  }

  if (const Instruction *instr = dyn_cast<Instruction>(U)) {
    if (instr->getParent() && instr->getParent()->getParent()) {
      const Function *curFunc = instr->getParent()->getParent();
      if (oneFunc && (curFunc != oneFunc))
        return false;
      oneFunc = curFunc;
      return true;
    } else
      return false;
  }

  for (const User *UU : U->users())
    if (!usedInOneFunc(UU, oneFunc))
      return false;

  return true;
}

/* Find out if a global variable can be demoted to local scope.
 * Currently, this is valid for CUDA shared variables, which have local
 * scope and global lifetime. So the conditions to check are :
 * 1. Is the global variable in shared address space?
 * 2. Does it have local linkage?
 * 3. Is the global variable referenced only in one function?
 */
static bool canDemoteGlobalVar(const GlobalVariable *gv, Function const *&f) {
  if (!gv->hasLocalLinkage())
    return false;
  PointerType *Pty = gv->getType();
  if (Pty->getAddressSpace() != ADDRESS_SPACE_SHARED)
    return false;

  const Function *oneFunc = nullptr;

  bool flag = usedInOneFunc(gv, oneFunc);
  if (!flag)
    return false;
  if (!oneFunc)
    return false;
  f = oneFunc;
  return true;
}

static bool useFuncSeen(const Constant *C,
                        DenseMap<const Function *, bool> &seenMap) {
  for (const User *U : C->users()) {
    if (const Constant *cu = dyn_cast<Constant>(U)) {
      if (useFuncSeen(cu, seenMap))
        return true;
    } else if (const Instruction *I = dyn_cast<Instruction>(U)) {
      const BasicBlock *bb = I->getParent();
      if (!bb)
        continue;
      const Function *caller = bb->getParent();
      if (!caller)
        continue;
      if (seenMap.contains(caller))
        return true;
    }
  }
  return false;
}

void NVPTXAsmPrinter::emitDeclarations(const Module &M, raw_ostream &O) {
  DenseMap<const Function *, bool> seenMap;
  for (const Function &F : M) {
    if (F.getAttributes().hasFnAttr("nvptx-libcall-callee")) {
      emitDeclaration(&F, O);
      continue;
    }

    if (F.isDeclaration()) {
      if (F.use_empty())
        continue;
      if (F.getIntrinsicID())
        continue;
      emitDeclaration(&F, O);
      continue;
    }
    for (const User *U : F.users()) {
      if (const Constant *C = dyn_cast<Constant>(U)) {
        if (usedInGlobalVarDef(C)) {
          // The use is in the initialization of a global variable
          // that is a function pointer, so print a declaration
          // for the original function
          emitDeclaration(&F, O);
          break;
        }
        // Emit a declaration of this function if the function that
        // uses this constant expr has already been seen.
        if (useFuncSeen(C, seenMap)) {
          emitDeclaration(&F, O);
          break;
        }
      }

      if (!isa<Instruction>(U))
        continue;
      const Instruction *instr = cast<Instruction>(U);
      const BasicBlock *bb = instr->getParent();
      if (!bb)
        continue;
      const Function *caller = bb->getParent();
      if (!caller)
        continue;

      // If a caller has already been seen, then the caller is
      // appearing in the module before the callee. so print out
      // a declaration for the callee.
      if (seenMap.contains(caller)) {
        emitDeclaration(&F, O);
        break;
      }
    }
    seenMap[&F] = true;
  }
  for (const GlobalAlias &GA : M.aliases())
    emitAliasDeclaration(&GA, O);
}

static bool isEmptyXXStructor(GlobalVariable *GV) {
  if (!GV) return true;
  const ConstantArray *InitList = dyn_cast<ConstantArray>(GV->getInitializer());
  if (!InitList) return true;  // Not an array; we don't know how to parse.
  return InitList->getNumOperands() == 0;
}

void NVPTXAsmPrinter::emitStartOfAsmFile(Module &M) {
  // Construct a default subtarget off of the TargetMachine defaults. The
  // rest of NVPTX isn't friendly to change subtargets per function and
  // so the default TargetMachine will have all of the options.
  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const auto* STI = static_cast<const NVPTXSubtarget*>(NTM.getSubtargetImpl());
  SmallString<128> Str1;
  raw_svector_ostream OS1(Str1);

  // Emit header before any dwarf directives are emitted below.
  emitHeader(M, OS1, *STI);
  OutStreamer->emitRawText(OS1.str());
}

bool NVPTXAsmPrinter::doInitialization(Module &M) {
  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const NVPTXSubtarget &STI =
      *static_cast<const NVPTXSubtarget *>(NTM.getSubtargetImpl());
  if (M.alias_size() && (STI.getPTXVersion() < 63 || STI.getSmVersion() < 30))
    report_fatal_error(".alias requires PTX version >= 6.3 and sm_30");

  // OpenMP supports NVPTX global constructors and destructors.
  bool IsOpenMP = M.getModuleFlag("openmp") != nullptr;

  if (!isEmptyXXStructor(M.getNamedGlobal("llvm.global_ctors")) &&
      !LowerCtorDtor && !IsOpenMP) {
    report_fatal_error(
        "Module has a nontrivial global ctor, which NVPTX does not support.");
    return true;  // error
  }
  if (!isEmptyXXStructor(M.getNamedGlobal("llvm.global_dtors")) &&
      !LowerCtorDtor && !IsOpenMP) {
    report_fatal_error(
        "Module has a nontrivial global dtor, which NVPTX does not support.");
    return true;  // error
  }

  // We need to call the parent's one explicitly.
  bool Result = AsmPrinter::doInitialization(M);

  GlobalsEmitted = false;

  return Result;
}

void NVPTXAsmPrinter::emitGlobals(const Module &M) {
  SmallString<128> Str2;
  raw_svector_ostream OS2(Str2);

  emitDeclarations(M, OS2);

  // As ptxas does not support forward references of globals, we need to first
  // sort the list of module-level globals in def-use order. We visit each
  // global variable in order, and ensure that we emit it *after* its dependent
  // globals. We use a little extra memory maintaining both a set and a list to
  // have fast searches while maintaining a strict ordering.
  SmallVector<const GlobalVariable *, 8> Globals;
  DenseSet<const GlobalVariable *> GVVisited;
  DenseSet<const GlobalVariable *> GVVisiting;

  // Visit each global variable, in order
  for (const GlobalVariable &I : M.globals())
    VisitGlobalVariableForEmission(&I, Globals, GVVisited, GVVisiting);

  assert(GVVisited.size() == M.global_size() && "Missed a global variable");
  assert(GVVisiting.size() == 0 && "Did not fully process a global variable");

  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const NVPTXSubtarget &STI =
      *static_cast<const NVPTXSubtarget *>(NTM.getSubtargetImpl());

  // Print out module-level global variables in proper order
  for (const GlobalVariable *GV : Globals)
    printModuleLevelGV(GV, OS2, /*processDemoted=*/false, STI);

  OS2 << '\n';

  OutStreamer->emitRawText(OS2.str());
}

void NVPTXAsmPrinter::emitGlobalAlias(const Module &M, const GlobalAlias &GA) {
  SmallString<128> Str;
  raw_svector_ostream OS(Str);

  MCSymbol *Name = getSymbol(&GA);

  OS << ".alias " << Name->getName() << ", " << GA.getAliaseeObject()->getName()
     << ";\n";

  OutStreamer->emitRawText(OS.str());
}

void NVPTXAsmPrinter::emitHeader(Module &M, raw_ostream &O,
                                 const NVPTXSubtarget &STI) {
  O << "//\n";
  O << "// Generated by LLVM NVPTX Back-End\n";
  O << "//\n";
  O << "\n";

  unsigned PTXVersion = STI.getPTXVersion();
  O << ".version " << (PTXVersion / 10) << "." << (PTXVersion % 10) << "\n";

  O << ".target ";
  O << STI.getTargetName();

  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  if (NTM.getDrvInterface() == NVPTX::NVCL)
    O << ", texmode_independent";

  bool HasFullDebugInfo = false;
  for (DICompileUnit *CU : M.debug_compile_units()) {
    switch(CU->getEmissionKind()) {
    case DICompileUnit::NoDebug:
    case DICompileUnit::DebugDirectivesOnly:
      break;
    case DICompileUnit::LineTablesOnly:
    case DICompileUnit::FullDebug:
      HasFullDebugInfo = true;
      break;
    }
    if (HasFullDebugInfo)
      break;
  }
  if (MMI && MMI->hasDebugInfo() && HasFullDebugInfo)
    O << ", debug";

  O << "\n";

  O << ".address_size ";
  if (NTM.is64Bit())
    O << "64";
  else
    O << "32";
  O << "\n";

  O << "\n";
}

bool NVPTXAsmPrinter::doFinalization(Module &M) {
  bool HasDebugInfo = MMI && MMI->hasDebugInfo();

  // If we did not emit any functions, then the global declarations have not
  // yet been emitted.
  if (!GlobalsEmitted) {
    emitGlobals(M);
    GlobalsEmitted = true;
  }

  // call doFinalization
  bool ret = AsmPrinter::doFinalization(M);

  clearAnnotationCache(&M);

  auto *TS =
      static_cast<NVPTXTargetStreamer *>(OutStreamer->getTargetStreamer());
  // Close the last emitted section
  if (HasDebugInfo) {
    TS->closeLastSection();
    // Emit empty .debug_loc section for better support of the empty files.
    OutStreamer->emitRawText("\t.section\t.debug_loc\t{\t}");
  }

  // Output last DWARF .file directives, if any.
  TS->outputDwarfFileDirectives();

  return ret;
}

// This function emits appropriate linkage directives for
// functions and global variables.
//
// extern function declaration            -> .extern
// extern function definition             -> .visible
// external global variable with init     -> .visible
// external without init                  -> .extern
// appending                              -> not allowed, assert.
// for any linkage other than
// internal, private, linker_private,
// linker_private_weak, linker_private_weak_def_auto,
// we emit                                -> .weak.

void NVPTXAsmPrinter::emitLinkageDirective(const GlobalValue *V,
                                           raw_ostream &O) {
  if (static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() == NVPTX::CUDA) {
    if (V->hasExternalLinkage()) {
      if (isa<GlobalVariable>(V)) {
        const GlobalVariable *GVar = cast<GlobalVariable>(V);
        if (GVar) {
          if (GVar->hasInitializer())
            O << ".visible ";
          else
            O << ".extern ";
        }
      } else if (V->isDeclaration())
        O << ".extern ";
      else
        O << ".visible ";
    } else if (V->hasAppendingLinkage()) {
      std::string msg;
      msg.append("Error: ");
      msg.append("Symbol ");
      if (V->hasName())
        msg.append(std::string(V->getName()));
      msg.append("has unsupported appending linkage type");
      llvm_unreachable(msg.c_str());
    } else if (!V->hasInternalLinkage() &&
               !V->hasPrivateLinkage()) {
      O << ".weak ";
    }
  }
}

void NVPTXAsmPrinter::printModuleLevelGV(const GlobalVariable *GVar,
                                         raw_ostream &O, bool processDemoted,
                                         const NVPTXSubtarget &STI) {
  // Skip meta data
  if (GVar->hasSection()) {
    if (GVar->getSection() == "llvm.metadata")
      return;
  }

  // Skip LLVM intrinsic global variables
  if (GVar->getName().starts_with("llvm.") ||
      GVar->getName().starts_with("nvvm."))
    return;

  const DataLayout &DL = getDataLayout();

  // GlobalVariables are always constant pointers themselves.
  Type *ETy = GVar->getValueType();

  if (GVar->hasExternalLinkage()) {
    if (GVar->hasInitializer())
      O << ".visible ";
    else
      O << ".extern ";
  } else if (STI.getPTXVersion() >= 50 && GVar->hasCommonLinkage() &&
             GVar->getAddressSpace() == ADDRESS_SPACE_GLOBAL) {
    O << ".common ";
  } else if (GVar->hasLinkOnceLinkage() || GVar->hasWeakLinkage() ||
             GVar->hasAvailableExternallyLinkage() ||
             GVar->hasCommonLinkage()) {
    O << ".weak ";
  }

  if (isTexture(*GVar)) {
    O << ".global .texref " << getTextureName(*GVar) << ";\n";
    return;
  }

  if (isSurface(*GVar)) {
    O << ".global .surfref " << getSurfaceName(*GVar) << ";\n";
    return;
  }

  if (GVar->isDeclaration()) {
    // (extern) declarations, no definition or initializer
    // Currently the only known declaration is for an automatic __local
    // (.shared) promoted to global.
    emitPTXGlobalVariable(GVar, O, STI);
    O << ";\n";
    return;
  }

  if (isSampler(*GVar)) {
    O << ".global .samplerref " << getSamplerName(*GVar);

    const Constant *Initializer = nullptr;
    if (GVar->hasInitializer())
      Initializer = GVar->getInitializer();
    const ConstantInt *CI = nullptr;
    if (Initializer)
      CI = dyn_cast<ConstantInt>(Initializer);
    if (CI) {
      unsigned sample = CI->getZExtValue();

      O << " = { ";

      for (int i = 0,
               addr = ((sample & __CLK_ADDRESS_MASK) >> __CLK_ADDRESS_BASE);
           i < 3; i++) {
        O << "addr_mode_" << i << " = ";
        switch (addr) {
        case 0:
          O << "wrap";
          break;
        case 1:
          O << "clamp_to_border";
          break;
        case 2:
          O << "clamp_to_edge";
          break;
        case 3:
          O << "wrap";
          break;
        case 4:
          O << "mirror";
          break;
        }
        O << ", ";
      }
      O << "filter_mode = ";
      switch ((sample & __CLK_FILTER_MASK) >> __CLK_FILTER_BASE) {
      case 0:
        O << "nearest";
        break;
      case 1:
        O << "linear";
        break;
      case 2:
        llvm_unreachable("Anisotropic filtering is not supported");
      default:
        O << "nearest";
        break;
      }
      if (!((sample & __CLK_NORMALIZED_MASK) >> __CLK_NORMALIZED_BASE)) {
        O << ", force_unnormalized_coords = 1";
      }
      O << " }";
    }

    O << ";\n";
    return;
  }

  if (GVar->hasPrivateLinkage()) {
    if (strncmp(GVar->getName().data(), "unrollpragma", 12) == 0)
      return;

    // FIXME - need better way (e.g. Metadata) to avoid generating this global
    if (strncmp(GVar->getName().data(), "filename", 8) == 0)
      return;
    if (GVar->use_empty())
      return;
  }

  const Function *demotedFunc = nullptr;
  if (!processDemoted && canDemoteGlobalVar(GVar, demotedFunc)) {
    O << "// " << GVar->getName() << " has been demoted\n";
    if (localDecls.find(demotedFunc) != localDecls.end())
      localDecls[demotedFunc].push_back(GVar);
    else {
      std::vector<const GlobalVariable *> temp;
      temp.push_back(GVar);
      localDecls[demotedFunc] = temp;
    }
    return;
  }

  O << ".";
  emitPTXAddressSpace(GVar->getAddressSpace(), O);

  if (isManaged(*GVar)) {
    if (STI.getPTXVersion() < 40 || STI.getSmVersion() < 30) {
      report_fatal_error(
          ".attribute(.managed) requires PTX version >= 4.0 and sm_30");
    }
    O << " .attribute(.managed)";
  }

  if (MaybeAlign A = GVar->getAlign())
    O << " .align " << A->value();
  else
    O << " .align " << (int)DL.getPrefTypeAlign(ETy).value();

  if (ETy->isFloatingPointTy() || ETy->isPointerTy() ||
      (ETy->isIntegerTy() && ETy->getScalarSizeInBits() <= 64)) {
    O << " .";
    // Special case: ABI requires that we use .u8 for predicates
    if (ETy->isIntegerTy(1))
      O << "u8";
    else
      O << getPTXFundamentalTypeStr(ETy, false);
    O << " ";
    getSymbol(GVar)->print(O, MAI);

    // Ptx allows variable initilization only for constant and global state
    // spaces.
    if (GVar->hasInitializer()) {
      if ((GVar->getAddressSpace() == ADDRESS_SPACE_GLOBAL) ||
          (GVar->getAddressSpace() == ADDRESS_SPACE_CONST)) {
        const Constant *Initializer = GVar->getInitializer();
        // 'undef' is treated as there is no value specified.
        if (!Initializer->isNullValue() && !isa<UndefValue>(Initializer)) {
          O << " = ";
          printScalarConstant(Initializer, O);
        }
      } else {
        // The frontend adds zero-initializer to device and constant variables
        // that don't have an initial value, and UndefValue to shared
        // variables, so skip warning for this case.
        if (!GVar->getInitializer()->isNullValue() &&
            !isa<UndefValue>(GVar->getInitializer())) {
          report_fatal_error("initial value of '" + GVar->getName() +
                             "' is not allowed in addrspace(" +
                             Twine(GVar->getAddressSpace()) + ")");
        }
      }
    }
  } else {
    uint64_t ElementSize = 0;

    // Although PTX has direct support for struct type and array type and
    // LLVM IR is very similar to PTX, the LLVM CodeGen does not support for
    // targets that support these high level field accesses. Structs, arrays
    // and vectors are lowered into arrays of bytes.
    switch (ETy->getTypeID()) {
    case Type::IntegerTyID: // Integers larger than 64 bits
    case Type::StructTyID:
    case Type::ArrayTyID:
    case Type::FixedVectorTyID:
      ElementSize = DL.getTypeStoreSize(ETy);
      // Ptx allows variable initilization only for constant and
      // global state spaces.
      if (((GVar->getAddressSpace() == ADDRESS_SPACE_GLOBAL) ||
           (GVar->getAddressSpace() == ADDRESS_SPACE_CONST)) &&
          GVar->hasInitializer()) {
        const Constant *Initializer = GVar->getInitializer();
        if (!isa<UndefValue>(Initializer) && !Initializer->isNullValue()) {
          AggBuffer aggBuffer(ElementSize, *this);
          bufferAggregateConstant(Initializer, &aggBuffer);
          if (aggBuffer.numSymbols()) {
            unsigned int ptrSize = MAI->getCodePointerSize();
            if (ElementSize % ptrSize ||
                !aggBuffer.allSymbolsAligned(ptrSize)) {
              // Print in bytes and use the mask() operator for pointers.
              if (!STI.hasMaskOperator())
                report_fatal_error(
                    "initialized packed aggregate with pointers '" +
                    GVar->getName() +
                    "' requires at least PTX ISA version 7.1");
              O << " .u8 ";
              getSymbol(GVar)->print(O, MAI);
              O << "[" << ElementSize << "] = {";
              aggBuffer.printBytes(O);
              O << "}";
            } else {
              O << " .u" << ptrSize * 8 << " ";
              getSymbol(GVar)->print(O, MAI);
              O << "[" << ElementSize / ptrSize << "] = {";
              aggBuffer.printWords(O);
              O << "}";
            }
          } else {
            O << " .b8 ";
            getSymbol(GVar)->print(O, MAI);
            O << "[" << ElementSize << "] = {";
            aggBuffer.printBytes(O);
            O << "}";
          }
        } else {
          O << " .b8 ";
          getSymbol(GVar)->print(O, MAI);
          if (ElementSize) {
            O << "[";
            O << ElementSize;
            O << "]";
          }
        }
      } else {
        O << " .b8 ";
        getSymbol(GVar)->print(O, MAI);
        if (ElementSize) {
          O << "[";
          O << ElementSize;
          O << "]";
        }
      }
      break;
    default:
      llvm_unreachable("type not supported yet");
    }
  }
  O << ";\n";
}

void NVPTXAsmPrinter::AggBuffer::printSymbol(unsigned nSym, raw_ostream &os) {
  const Value *v = Symbols[nSym];
  const Value *v0 = SymbolsBeforeStripping[nSym];
  if (const GlobalValue *GVar = dyn_cast<GlobalValue>(v)) {
    MCSymbol *Name = AP.getSymbol(GVar);
    PointerType *PTy = dyn_cast<PointerType>(v0->getType());
    // Is v0 a generic pointer?
    bool isGenericPointer = PTy && PTy->getAddressSpace() == 0;
    if (EmitGeneric && isGenericPointer && !isa<Function>(v)) {
      os << "generic(";
      Name->print(os, AP.MAI);
      os << ")";
    } else {
      Name->print(os, AP.MAI);
    }
  } else if (const ConstantExpr *CExpr = dyn_cast<ConstantExpr>(v0)) {
    const MCExpr *Expr = AP.lowerConstantForGV(cast<Constant>(CExpr), false);
    AP.printMCExpr(*Expr, os);
  } else
    llvm_unreachable("symbol type unknown");
}

void NVPTXAsmPrinter::AggBuffer::printBytes(raw_ostream &os) {
  unsigned int ptrSize = AP.MAI->getCodePointerSize();
  // Do not emit trailing zero initializers. They will be zero-initialized by
  // ptxas. This saves on both space requirements for the generated PTX and on
  // memory use by ptxas. (See:
  // https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#global-state-space)
  unsigned int InitializerCount = size;
  // TODO: symbols make this harder, but it would still be good to trim trailing
  // 0s for aggs with symbols as well.
  if (numSymbols() == 0)
    while (InitializerCount >= 1 && !buffer[InitializerCount - 1])
      InitializerCount--;

  symbolPosInBuffer.push_back(InitializerCount);
  unsigned int nSym = 0;
  unsigned int nextSymbolPos = symbolPosInBuffer[nSym];
  for (unsigned int pos = 0; pos < InitializerCount;) {
    if (pos)
      os << ", ";
    if (pos != nextSymbolPos) {
      os << (unsigned int)buffer[pos];
      ++pos;
      continue;
    }
    // Generate a per-byte mask() operator for the symbol, which looks like:
    //   .global .u8 addr[] = {0xFF(foo), 0xFF00(foo), 0xFF0000(foo), ...};
    // See https://docs.nvidia.com/cuda/parallel-thread-execution/index.html#initializers
    std::string symText;
    llvm::raw_string_ostream oss(symText);
    printSymbol(nSym, oss);
    for (unsigned i = 0; i < ptrSize; ++i) {
      if (i)
        os << ", ";
      llvm::write_hex(os, 0xFFULL << i * 8, HexPrintStyle::PrefixUpper);
      os << "(" << symText << ")";
    }
    pos += ptrSize;
    nextSymbolPos = symbolPosInBuffer[++nSym];
    assert(nextSymbolPos >= pos);
  }
}

void NVPTXAsmPrinter::AggBuffer::printWords(raw_ostream &os) {
  unsigned int ptrSize = AP.MAI->getCodePointerSize();
  symbolPosInBuffer.push_back(size);
  unsigned int nSym = 0;
  unsigned int nextSymbolPos = symbolPosInBuffer[nSym];
  assert(nextSymbolPos % ptrSize == 0);
  for (unsigned int pos = 0; pos < size; pos += ptrSize) {
    if (pos)
      os << ", ";
    if (pos == nextSymbolPos) {
      printSymbol(nSym, os);
      nextSymbolPos = symbolPosInBuffer[++nSym];
      assert(nextSymbolPos % ptrSize == 0);
      assert(nextSymbolPos >= pos + ptrSize);
    } else if (ptrSize == 4)
      os << support::endian::read32le(&buffer[pos]);
    else
      os << support::endian::read64le(&buffer[pos]);
  }
}

void NVPTXAsmPrinter::emitDemotedVars(const Function *f, raw_ostream &O) {
  if (localDecls.find(f) == localDecls.end())
    return;

  std::vector<const GlobalVariable *> &gvars = localDecls[f];

  const NVPTXTargetMachine &NTM = static_cast<const NVPTXTargetMachine &>(TM);
  const NVPTXSubtarget &STI =
      *static_cast<const NVPTXSubtarget *>(NTM.getSubtargetImpl());

  for (const GlobalVariable *GV : gvars) {
    O << "\t// demoted variable\n\t";
    printModuleLevelGV(GV, O, /*processDemoted=*/true, STI);
  }
}

void NVPTXAsmPrinter::emitPTXAddressSpace(unsigned int AddressSpace,
                                          raw_ostream &O) const {
  switch (AddressSpace) {
  case ADDRESS_SPACE_LOCAL:
    O << "local";
    break;
  case ADDRESS_SPACE_GLOBAL:
    O << "global";
    break;
  case ADDRESS_SPACE_CONST:
    O << "const";
    break;
  case ADDRESS_SPACE_SHARED:
    O << "shared";
    break;
  default:
    report_fatal_error("Bad address space found while emitting PTX: " +
                       llvm::Twine(AddressSpace));
    break;
  }
}

std::string
NVPTXAsmPrinter::getPTXFundamentalTypeStr(Type *Ty, bool useB4PTR) const {
  switch (Ty->getTypeID()) {
  case Type::IntegerTyID: {
    unsigned NumBits = cast<IntegerType>(Ty)->getBitWidth();
    if (NumBits == 1)
      return "pred";
    else if (NumBits <= 64) {
      std::string name = "u";
      return name + utostr(NumBits);
    } else {
      llvm_unreachable("Integer too large");
      break;
    }
    break;
  }
  case Type::BFloatTyID:
  case Type::HalfTyID:
    // fp16 and bf16 are stored as .b16 for compatibility with pre-sm_53
    // PTX assembly.
    return "b16";
  case Type::FloatTyID:
    return "f32";
  case Type::DoubleTyID:
    return "f64";
  case Type::PointerTyID: {
    unsigned PtrSize = TM.getPointerSizeInBits(Ty->getPointerAddressSpace());
    assert((PtrSize == 64 || PtrSize == 32) && "Unexpected pointer size");

    if (PtrSize == 64)
      if (useB4PTR)
        return "b64";
      else
        return "u64";
    else if (useB4PTR)
      return "b32";
    else
      return "u32";
  }
  default:
    break;
  }
  llvm_unreachable("unexpected type");
}

void NVPTXAsmPrinter::emitPTXGlobalVariable(const GlobalVariable *GVar,
                                            raw_ostream &O,
                                            const NVPTXSubtarget &STI) {
  const DataLayout &DL = getDataLayout();

  // GlobalVariables are always constant pointers themselves.
  Type *ETy = GVar->getValueType();

  O << ".";
  emitPTXAddressSpace(GVar->getType()->getAddressSpace(), O);
  if (isManaged(*GVar)) {
    if (STI.getPTXVersion() < 40 || STI.getSmVersion() < 30) {
      report_fatal_error(
          ".attribute(.managed) requires PTX version >= 4.0 and sm_30");
    }
    O << " .attribute(.managed)";
  }
  if (MaybeAlign A = GVar->getAlign())
    O << " .align " << A->value();
  else
    O << " .align " << (int)DL.getPrefTypeAlign(ETy).value();

  // Special case for i128
  if (ETy->isIntegerTy(128)) {
    O << " .b8 ";
    getSymbol(GVar)->print(O, MAI);
    O << "[16]";
    return;
  }

  if (ETy->isFloatingPointTy() || ETy->isIntOrPtrTy()) {
    O << " .";
    O << getPTXFundamentalTypeStr(ETy);
    O << " ";
    getSymbol(GVar)->print(O, MAI);
    return;
  }

  int64_t ElementSize = 0;

  // Although PTX has direct support for struct type and array type and LLVM IR
  // is very similar to PTX, the LLVM CodeGen does not support for targets that
  // support these high level field accesses. Structs and arrays are lowered
  // into arrays of bytes.
  switch (ETy->getTypeID()) {
  case Type::StructTyID:
  case Type::ArrayTyID:
  case Type::FixedVectorTyID:
    ElementSize = DL.getTypeStoreSize(ETy);
    O << " .b8 ";
    getSymbol(GVar)->print(O, MAI);
    O << "[";
    if (ElementSize) {
      O << ElementSize;
    }
    O << "]";
    break;
  default:
    llvm_unreachable("type not supported yet");
  }
}

void NVPTXAsmPrinter::emitFunctionParamList(const Function *F, raw_ostream &O) {
  const DataLayout &DL = getDataLayout();
  const AttributeList &PAL = F->getAttributes();
  const NVPTXSubtarget &STI = TM.getSubtarget<NVPTXSubtarget>(*F);
  const auto *TLI = cast<NVPTXTargetLowering>(STI.getTargetLowering());

  Function::const_arg_iterator I, E;
  unsigned paramIndex = 0;
  bool first = true;
  bool isKernelFunc = isKernelFunction(*F);
  bool isABI = (STI.getSmVersion() >= 20);
  bool hasImageHandles = STI.hasImageHandles();

  if (F->arg_empty() && !F->isVarArg()) {
    O << "()";
    return;
  }

  O << "(\n";

  for (I = F->arg_begin(), E = F->arg_end(); I != E; ++I, paramIndex++) {
    Type *Ty = I->getType();

    if (!first)
      O << ",\n";

    first = false;

    // Handle image/sampler parameters
    if (isKernelFunction(*F)) {
      if (isSampler(*I) || isImage(*I)) {
        if (isImage(*I)) {
          if (isImageWriteOnly(*I) || isImageReadWrite(*I)) {
            if (hasImageHandles)
              O << "\t.param .u64 .ptr .surfref ";
            else
              O << "\t.param .surfref ";
            O << TLI->getParamName(F, paramIndex);
          }
          else { // Default image is read_only
            if (hasImageHandles)
              O << "\t.param .u64 .ptr .texref ";
            else
              O << "\t.param .texref ";
            O << TLI->getParamName(F, paramIndex);
          }
        } else {
          if (hasImageHandles)
            O << "\t.param .u64 .ptr .samplerref ";
          else
            O << "\t.param .samplerref ";
          O << TLI->getParamName(F, paramIndex);
        }
        continue;
      }
    }

    auto getOptimalAlignForParam = [TLI, &DL, &PAL, F,
                                    paramIndex](Type *Ty) -> Align {
      if (MaybeAlign StackAlign =
              getAlign(*F, paramIndex + AttributeList::FirstArgIndex))
        return StackAlign.value();

      Align TypeAlign = TLI->getFunctionParamOptimizedAlign(F, Ty, DL);
      MaybeAlign ParamAlign = PAL.getParamAlignment(paramIndex);
      return std::max(TypeAlign, ParamAlign.valueOrOne());
    };

    if (!PAL.hasParamAttr(paramIndex, Attribute::ByVal)) {
      if (ShouldPassAsArray(Ty)) {
        // Just print .param .align <a> .b8 .param[size];
        // <a>  = optimal alignment for the element type; always multiple of
        //        PAL.getParamAlignment
        // size = typeallocsize of element type
        Align OptimalAlign = getOptimalAlignForParam(Ty);

        O << "\t.param .align " << OptimalAlign.value() << " .b8 ";
        O << TLI->getParamName(F, paramIndex);
        O << "[" << DL.getTypeAllocSize(Ty) << "]";

        continue;
      }
      // Just a scalar
      auto *PTy = dyn_cast<PointerType>(Ty);
      unsigned PTySizeInBits = 0;
      if (PTy) {
        PTySizeInBits =
            TLI->getPointerTy(DL, PTy->getAddressSpace()).getSizeInBits();
        assert(PTySizeInBits && "Invalid pointer size");
      }

      if (isKernelFunc) {
        if (PTy) {
          // Special handling for pointer arguments to kernel
          O << "\t.param .u" << PTySizeInBits << " ";

          if (static_cast<NVPTXTargetMachine &>(TM).getDrvInterface() !=
              NVPTX::CUDA) {
            int addrSpace = PTy->getAddressSpace();
            switch (addrSpace) {
            default:
              O << ".ptr ";
              break;
            case ADDRESS_SPACE_CONST:
              O << ".ptr .const ";
              break;
            case ADDRESS_SPACE_SHARED:
              O << ".ptr .shared ";
              break;
            case ADDRESS_SPACE_GLOBAL:
              O << ".ptr .global ";
              break;
            }
            Align ParamAlign = I->getParamAlign().valueOrOne();
            O << ".align " << ParamAlign.value() << " ";
          }
          O << TLI->getParamName(F, paramIndex);
          continue;
        }

        // non-pointer scalar to kernel func
        O << "\t.param .";
        // Special case: predicate operands become .u8 types
        if (Ty->isIntegerTy(1))
          O << "u8";
        else
          O << getPTXFundamentalTypeStr(Ty);
        O << " ";
        O << TLI->getParamName(F, paramIndex);
        continue;
      }
      // Non-kernel function, just print .param .b<size> for ABI
      // and .reg .b<size> for non-ABI
      unsigned sz = 0;
      if (isa<IntegerType>(Ty)) {
        sz = cast<IntegerType>(Ty)->getBitWidth();
        sz = promoteScalarArgumentSize(sz);
      } else if (PTy) {
        assert(PTySizeInBits && "Invalid pointer size");
        sz = PTySizeInBits;
      } else
        sz = Ty->getPrimitiveSizeInBits();
      if (isABI)
        O << "\t.param .b" << sz << " ";
      else
        O << "\t.reg .b" << sz << " ";
      O << TLI->getParamName(F, paramIndex);
      continue;
    }

    // param has byVal attribute.
    Type *ETy = PAL.getParamByValType(paramIndex);
    assert(ETy && "Param should have byval type");

    if (isABI || isKernelFunc) {
      // Just print .param .align <a> .b8 .param[size];
      // <a>  = optimal alignment for the element type; always multiple of
      //        PAL.getParamAlignment
      // size = typeallocsize of element type
      Align OptimalAlign =
          isKernelFunc
              ? getOptimalAlignForParam(ETy)
              : TLI->getFunctionByValParamAlign(
                    F, ETy, PAL.getParamAlignment(paramIndex).valueOrOne(), DL);

      unsigned sz = DL.getTypeAllocSize(ETy);
      O << "\t.param .align " << OptimalAlign.value() << " .b8 ";
      O << TLI->getParamName(F, paramIndex);
      O << "[" << sz << "]";
      continue;
    } else {
      // Split the ETy into constituent parts and
      // print .param .b<size> <name> for each part.
      // Further, if a part is vector, print the above for
      // each vector element.
      SmallVector<EVT, 16> vtparts;
      ComputeValueVTs(*TLI, DL, ETy, vtparts);
      for (unsigned i = 0, e = vtparts.size(); i != e; ++i) {
        unsigned elems = 1;
        EVT elemtype = vtparts[i];
        if (vtparts[i].isVector()) {
          elems = vtparts[i].getVectorNumElements();
          elemtype = vtparts[i].getVectorElementType();
        }

        for (unsigned j = 0, je = elems; j != je; ++j) {
          unsigned sz = elemtype.getSizeInBits();
          if (elemtype.isInteger())
            sz = promoteScalarArgumentSize(sz);
          O << "\t.reg .b" << sz << " ";
          O << TLI->getParamName(F, paramIndex);
          if (j < je - 1)
            O << ",\n";
          ++paramIndex;
        }
        if (i < e - 1)
          O << ",\n";
      }
      --paramIndex;
      continue;
    }
  }

  if (F->isVarArg()) {
    if (!first)
      O << ",\n";
    O << "\t.param .align " << STI.getMaxRequiredAlignment();
    O << " .b8 ";
    O << TLI->getParamName(F, /* vararg */ -1) << "[]";
  }

  O << "\n)";
}

void NVPTXAsmPrinter::setAndEmitFunctionVirtualRegisters(
    const MachineFunction &MF) {
  SmallString<128> Str;
  raw_svector_ostream O(Str);

  // Map the global virtual register number to a register class specific
  // virtual register number starting from 1 with that class.
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
  //unsigned numRegClasses = TRI->getNumRegClasses();

  // Emit the Fake Stack Object
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t NumBytes = MFI.getStackSize();
  if (NumBytes) {
    O << "\t.local .align " << MFI.getMaxAlign().value() << " .b8 \t"
      << DEPOTNAME << getFunctionNumber() << "[" << NumBytes << "];\n";
    if (static_cast<const NVPTXTargetMachine &>(MF.getTarget()).is64Bit()) {
      O << "\t.reg .b64 \t%SP;\n";
      O << "\t.reg .b64 \t%SPL;\n";
    } else {
      O << "\t.reg .b32 \t%SP;\n";
      O << "\t.reg .b32 \t%SPL;\n";
    }
  }

  // Go through all virtual registers to establish the mapping between the
  // global virtual
  // register number and the per class virtual register number.
  // We use the per class virtual register number in the ptx output.
  unsigned int numVRs = MRI->getNumVirtRegs();
  for (unsigned i = 0; i < numVRs; i++) {
    Register vr = Register::index2VirtReg(i);
    const TargetRegisterClass *RC = MRI->getRegClass(vr);
    DenseMap<unsigned, unsigned> &regmap = VRegMapping[RC];
    int n = regmap.size();
    regmap.insert(std::make_pair(vr, n + 1));
  }

  // Emit register declarations
  // @TODO: Extract out the real register usage
  // O << "\t.reg .pred %p<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s16 %rc<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s16 %rs<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s32 %r<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .s64 %rd<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .f32 %f<" << NVPTXNumRegisters << ">;\n";
  // O << "\t.reg .f64 %fd<" << NVPTXNumRegisters << ">;\n";

  // Emit declaration of the virtual registers or 'physical' registers for
  // each register class
  for (unsigned i=0; i< TRI->getNumRegClasses(); i++) {
    const TargetRegisterClass *RC = TRI->getRegClass(i);
    DenseMap<unsigned, unsigned> &regmap = VRegMapping[RC];
    std::string rcname = getNVPTXRegClassName(RC);
    std::string rcStr = getNVPTXRegClassStr(RC);
    int n = regmap.size();

    // Only declare those registers that may be used.
    if (n) {
       O << "\t.reg " << rcname << " \t" << rcStr << "<" << (n+1)
         << ">;\n";
    }
  }

  OutStreamer->emitRawText(O.str());
}

void NVPTXAsmPrinter::printFPConstant(const ConstantFP *Fp, raw_ostream &O) {
  APFloat APF = APFloat(Fp->getValueAPF()); // make a copy
  bool ignored;
  unsigned int numHex;
  const char *lead;

  if (Fp->getType()->getTypeID() == Type::FloatTyID) {
    numHex = 8;
    lead = "0f";
    APF.convert(APFloat::IEEEsingle(), APFloat::rmNearestTiesToEven, &ignored);
  } else if (Fp->getType()->getTypeID() == Type::DoubleTyID) {
    numHex = 16;
    lead = "0d";
    APF.convert(APFloat::IEEEdouble(), APFloat::rmNearestTiesToEven, &ignored);
  } else
    llvm_unreachable("unsupported fp type");

  APInt API = APF.bitcastToAPInt();
  O << lead << format_hex_no_prefix(API.getZExtValue(), numHex, /*Upper=*/true);
}

void NVPTXAsmPrinter::printScalarConstant(const Constant *CPV, raw_ostream &O) {
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CPV)) {
    O << CI->getValue();
    return;
  }
  if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CPV)) {
    printFPConstant(CFP, O);
    return;
  }
  if (isa<ConstantPointerNull>(CPV)) {
    O << "0";
    return;
  }
  if (const GlobalValue *GVar = dyn_cast<GlobalValue>(CPV)) {
    bool IsNonGenericPointer = false;
    if (GVar->getType()->getAddressSpace() != 0) {
      IsNonGenericPointer = true;
    }
    if (EmitGeneric && !isa<Function>(CPV) && !IsNonGenericPointer) {
      O << "generic(";
      getSymbol(GVar)->print(O, MAI);
      O << ")";
    } else {
      getSymbol(GVar)->print(O, MAI);
    }
    return;
  }
  if (const ConstantExpr *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
    const MCExpr *E = lowerConstantForGV(cast<Constant>(Cexpr), false);
    printMCExpr(*E, O);
    return;
  }
  llvm_unreachable("Not scalar type found in printScalarConstant()");
}

void NVPTXAsmPrinter::bufferLEByte(const Constant *CPV, int Bytes,
                                   AggBuffer *AggBuffer) {
  const DataLayout &DL = getDataLayout();
  int AllocSize = DL.getTypeAllocSize(CPV->getType());
  if (isa<UndefValue>(CPV) || CPV->isNullValue()) {
    // Non-zero Bytes indicates that we need to zero-fill everything. Otherwise,
    // only the space allocated by CPV.
    AggBuffer->addZeros(Bytes ? Bytes : AllocSize);
    return;
  }

  // Helper for filling AggBuffer with APInts.
  auto AddIntToBuffer = [AggBuffer, Bytes](const APInt &Val) {
    size_t NumBytes = (Val.getBitWidth() + 7) / 8;
    SmallVector<unsigned char, 16> Buf(NumBytes);
    // `extractBitsAsZExtValue` does not allow the extraction of bits beyond the
    // input's bit width, and i1 arrays may not have a length that is a multuple
    // of 8. We handle the last byte separately, so we never request out of
    // bounds bits.
    for (unsigned I = 0; I < NumBytes - 1; ++I) {
      Buf[I] = Val.extractBitsAsZExtValue(8, I * 8);
    }
    size_t LastBytePosition = (NumBytes - 1) * 8;
    size_t LastByteBits = Val.getBitWidth() - LastBytePosition;
    Buf[NumBytes - 1] =
        Val.extractBitsAsZExtValue(LastByteBits, LastBytePosition);
    AggBuffer->addBytes(Buf.data(), NumBytes, Bytes);
  };

  switch (CPV->getType()->getTypeID()) {
  case Type::IntegerTyID:
    if (const auto CI = dyn_cast<ConstantInt>(CPV)) {
      AddIntToBuffer(CI->getValue());
      break;
    }
    if (const auto *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
      if (const auto *CI =
              dyn_cast<ConstantInt>(ConstantFoldConstant(Cexpr, DL))) {
        AddIntToBuffer(CI->getValue());
        break;
      }
      if (Cexpr->getOpcode() == Instruction::PtrToInt) {
        Value *V = Cexpr->getOperand(0)->stripPointerCasts();
        AggBuffer->addSymbol(V, Cexpr->getOperand(0));
        AggBuffer->addZeros(AllocSize);
        break;
      }
    }
    llvm_unreachable("unsupported integer const type");
    break;

  case Type::HalfTyID:
  case Type::BFloatTyID:
  case Type::FloatTyID:
  case Type::DoubleTyID:
    AddIntToBuffer(cast<ConstantFP>(CPV)->getValueAPF().bitcastToAPInt());
    break;

  case Type::PointerTyID: {
    if (const GlobalValue *GVar = dyn_cast<GlobalValue>(CPV)) {
      AggBuffer->addSymbol(GVar, GVar);
    } else if (const ConstantExpr *Cexpr = dyn_cast<ConstantExpr>(CPV)) {
      const Value *v = Cexpr->stripPointerCasts();
      AggBuffer->addSymbol(v, Cexpr);
    }
    AggBuffer->addZeros(AllocSize);
    break;
  }

  case Type::ArrayTyID:
  case Type::FixedVectorTyID:
  case Type::StructTyID: {
    if (isa<ConstantAggregate>(CPV) || isa<ConstantDataSequential>(CPV)) {
      bufferAggregateConstant(CPV, AggBuffer);
      if (Bytes > AllocSize)
        AggBuffer->addZeros(Bytes - AllocSize);
    } else if (isa<ConstantAggregateZero>(CPV))
      AggBuffer->addZeros(Bytes);
    else
      llvm_unreachable("Unexpected Constant type");
    break;
  }

  default:
    llvm_unreachable("unsupported type");
  }
}

void NVPTXAsmPrinter::bufferAggregateConstant(const Constant *CPV,
                                              AggBuffer *aggBuffer) {
  const DataLayout &DL = getDataLayout();
  int Bytes;

  // Integers of arbitrary width
  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CPV)) {
    APInt Val = CI->getValue();
    for (unsigned I = 0, E = DL.getTypeAllocSize(CPV->getType()); I < E; ++I) {
      uint8_t Byte = Val.getLoBits(8).getZExtValue();
      aggBuffer->addBytes(&Byte, 1, 1);
      Val.lshrInPlace(8);
    }
    return;
  }

  // Old constants
  if (isa<ConstantArray>(CPV) || isa<ConstantVector>(CPV)) {
    if (CPV->getNumOperands())
      for (unsigned i = 0, e = CPV->getNumOperands(); i != e; ++i)
        bufferLEByte(cast<Constant>(CPV->getOperand(i)), 0, aggBuffer);
    return;
  }

  if (const ConstantDataSequential *CDS =
          dyn_cast<ConstantDataSequential>(CPV)) {
    if (CDS->getNumElements())
      for (unsigned i = 0; i < CDS->getNumElements(); ++i)
        bufferLEByte(cast<Constant>(CDS->getElementAsConstant(i)), 0,
                     aggBuffer);
    return;
  }

  if (isa<ConstantStruct>(CPV)) {
    if (CPV->getNumOperands()) {
      StructType *ST = cast<StructType>(CPV->getType());
      for (unsigned i = 0, e = CPV->getNumOperands(); i != e; ++i) {
        if (i == (e - 1))
          Bytes = DL.getStructLayout(ST)->getElementOffset(0) +
                  DL.getTypeAllocSize(ST) -
                  DL.getStructLayout(ST)->getElementOffset(i);
        else
          Bytes = DL.getStructLayout(ST)->getElementOffset(i + 1) -
                  DL.getStructLayout(ST)->getElementOffset(i);
        bufferLEByte(cast<Constant>(CPV->getOperand(i)), Bytes, aggBuffer);
      }
    }
    return;
  }
  llvm_unreachable("unsupported constant type in printAggregateConstant()");
}

/// lowerConstantForGV - Return an MCExpr for the given Constant.  This is mostly
/// a copy from AsmPrinter::lowerConstant, except customized to only handle
/// expressions that are representable in PTX and create
/// NVPTXGenericMCSymbolRefExpr nodes for addrspacecast instructions.
const MCExpr *
NVPTXAsmPrinter::lowerConstantForGV(const Constant *CV, bool ProcessingGeneric) {
  MCContext &Ctx = OutContext;

  if (CV->isNullValue() || isa<UndefValue>(CV))
    return MCConstantExpr::create(0, Ctx);

  if (const ConstantInt *CI = dyn_cast<ConstantInt>(CV))
    return MCConstantExpr::create(CI->getZExtValue(), Ctx);

  if (const GlobalValue *GV = dyn_cast<GlobalValue>(CV)) {
    const MCSymbolRefExpr *Expr =
      MCSymbolRefExpr::create(getSymbol(GV), Ctx);
    if (ProcessingGeneric) {
      return NVPTXGenericMCSymbolRefExpr::create(Expr, Ctx);
    } else {
      return Expr;
    }
  }

  const ConstantExpr *CE = dyn_cast<ConstantExpr>(CV);
  if (!CE) {
    llvm_unreachable("Unknown constant value to lower!");
  }

  switch (CE->getOpcode()) {
  default:
    break; // Error

  case Instruction::AddrSpaceCast: {
    // Strip the addrspacecast and pass along the operand
    PointerType *DstTy = cast<PointerType>(CE->getType());
    if (DstTy->getAddressSpace() == 0)
      return lowerConstantForGV(cast<const Constant>(CE->getOperand(0)), true);

    break; // Error
  }

  case Instruction::GetElementPtr: {
    const DataLayout &DL = getDataLayout();

    // Generate a symbolic expression for the byte address
    APInt OffsetAI(DL.getPointerTypeSizeInBits(CE->getType()), 0);
    cast<GEPOperator>(CE)->accumulateConstantOffset(DL, OffsetAI);

    const MCExpr *Base = lowerConstantForGV(CE->getOperand(0),
                                            ProcessingGeneric);
    if (!OffsetAI)
      return Base;

    int64_t Offset = OffsetAI.getSExtValue();
    return MCBinaryExpr::createAdd(Base, MCConstantExpr::create(Offset, Ctx),
                                   Ctx);
  }

  case Instruction::Trunc:
    // We emit the value and depend on the assembler to truncate the generated
    // expression properly.  This is important for differences between
    // blockaddress labels.  Since the two labels are in the same function, it
    // is reasonable to treat their delta as a 32-bit value.
    [[fallthrough]];
  case Instruction::BitCast:
    return lowerConstantForGV(CE->getOperand(0), ProcessingGeneric);

  case Instruction::IntToPtr: {
    const DataLayout &DL = getDataLayout();

    // Handle casts to pointers by changing them into casts to the appropriate
    // integer type.  This promotes constant folding and simplifies this code.
    Constant *Op = CE->getOperand(0);
    Op = ConstantFoldIntegerCast(Op, DL.getIntPtrType(CV->getType()),
                                 /*IsSigned*/ false, DL);
    if (Op)
      return lowerConstantForGV(Op, ProcessingGeneric);

    break; // Error
  }

  case Instruction::PtrToInt: {
    const DataLayout &DL = getDataLayout();

    // Support only foldable casts to/from pointers that can be eliminated by
    // changing the pointer to the appropriately sized integer type.
    Constant *Op = CE->getOperand(0);
    Type *Ty = CE->getType();

    const MCExpr *OpExpr = lowerConstantForGV(Op, ProcessingGeneric);

    // We can emit the pointer value into this slot if the slot is an
    // integer slot equal to the size of the pointer.
    if (DL.getTypeAllocSize(Ty) == DL.getTypeAllocSize(Op->getType()))
      return OpExpr;

    // Otherwise the pointer is smaller than the resultant integer, mask off
    // the high bits so we are sure to get a proper truncation if the input is
    // a constant expr.
    unsigned InBits = DL.getTypeAllocSizeInBits(Op->getType());
    const MCExpr *MaskExpr = MCConstantExpr::create(~0ULL >> (64-InBits), Ctx);
    return MCBinaryExpr::createAnd(OpExpr, MaskExpr, Ctx);
  }

  // The MC library also has a right-shift operator, but it isn't consistently
  // signed or unsigned between different targets.
  case Instruction::Add: {
    const MCExpr *LHS = lowerConstantForGV(CE->getOperand(0), ProcessingGeneric);
    const MCExpr *RHS = lowerConstantForGV(CE->getOperand(1), ProcessingGeneric);
    switch (CE->getOpcode()) {
    default: llvm_unreachable("Unknown binary operator constant cast expr");
    case Instruction::Add: return MCBinaryExpr::createAdd(LHS, RHS, Ctx);
    }
  }
  }

  // If the code isn't optimized, there may be outstanding folding
  // opportunities. Attempt to fold the expression using DataLayout as a
  // last resort before giving up.
  Constant *C = ConstantFoldConstant(CE, getDataLayout());
  if (C != CE)
    return lowerConstantForGV(C, ProcessingGeneric);

  // Otherwise report the problem to the user.
  std::string S;
  raw_string_ostream OS(S);
  OS << "Unsupported expression in static initializer: ";
  CE->printAsOperand(OS, /*PrintType=*/false,
                 !MF ? nullptr : MF->getFunction().getParent());
  report_fatal_error(Twine(OS.str()));
}

// Copy of MCExpr::print customized for NVPTX
void NVPTXAsmPrinter::printMCExpr(const MCExpr &Expr, raw_ostream &OS) {
  switch (Expr.getKind()) {
  case MCExpr::Target:
    return cast<MCTargetExpr>(&Expr)->printImpl(OS, MAI);
  case MCExpr::Constant:
    OS << cast<MCConstantExpr>(Expr).getValue();
    return;

  case MCExpr::SymbolRef: {
    const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(Expr);
    const MCSymbol &Sym = SRE.getSymbol();
    Sym.print(OS, MAI);
    return;
  }

  case MCExpr::Unary: {
    const MCUnaryExpr &UE = cast<MCUnaryExpr>(Expr);
    switch (UE.getOpcode()) {
    case MCUnaryExpr::LNot:  OS << '!'; break;
    case MCUnaryExpr::Minus: OS << '-'; break;
    case MCUnaryExpr::Not:   OS << '~'; break;
    case MCUnaryExpr::Plus:  OS << '+'; break;
    }
    printMCExpr(*UE.getSubExpr(), OS);
    return;
  }

  case MCExpr::Binary: {
    const MCBinaryExpr &BE = cast<MCBinaryExpr>(Expr);

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getLHS()) || isa<MCSymbolRefExpr>(BE.getLHS()) ||
        isa<NVPTXGenericMCSymbolRefExpr>(BE.getLHS())) {
      printMCExpr(*BE.getLHS(), OS);
    } else {
      OS << '(';
      printMCExpr(*BE.getLHS(), OS);
      OS<< ')';
    }

    switch (BE.getOpcode()) {
    case MCBinaryExpr::Add:
      // Print "X-42" instead of "X+-42".
      if (const MCConstantExpr *RHSC = dyn_cast<MCConstantExpr>(BE.getRHS())) {
        if (RHSC->getValue() < 0) {
          OS << RHSC->getValue();
          return;
        }
      }

      OS <<  '+';
      break;
    default: llvm_unreachable("Unhandled binary operator");
    }

    // Only print parens around the LHS if it is non-trivial.
    if (isa<MCConstantExpr>(BE.getRHS()) || isa<MCSymbolRefExpr>(BE.getRHS())) {
      printMCExpr(*BE.getRHS(), OS);
    } else {
      OS << '(';
      printMCExpr(*BE.getRHS(), OS);
      OS << ')';
    }
    return;
  }
  }

  llvm_unreachable("Invalid expression kind!");
}

/// PrintAsmOperand - Print out an operand for an inline asm expression.
///
bool NVPTXAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0]) {
    if (ExtraCode[1] != 0)
      return true; // Unknown modifier.

    switch (ExtraCode[0]) {
    default:
      // See if this is a generic print operand
      return AsmPrinter::PrintAsmOperand(MI, OpNo, ExtraCode, O);
    case 'r':
      break;
    }
  }

  printOperand(MI, OpNo, O);

  return false;
}

bool NVPTXAsmPrinter::PrintAsmMemoryOperand(const MachineInstr *MI,
                                            unsigned OpNo,
                                            const char *ExtraCode,
                                            raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Unknown modifier

  O << '[';
  printMemOperand(MI, OpNo, O);
  O << ']';

  return false;
}

void NVPTXAsmPrinter::printOperand(const MachineInstr *MI, unsigned OpNum,
                                   raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.getReg().isPhysical()) {
      if (MO.getReg() == NVPTX::VRDepot)
        O << DEPOTNAME << getFunctionNumber();
      else
        O << NVPTXInstPrinter::getRegisterName(MO.getReg());
    } else {
      emitVirtualRegister(MO.getReg(), O);
    }
    break;

  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    break;

  case MachineOperand::MO_FPImmediate:
    printFPConstant(MO.getFPImm(), O);
    break;

  case MachineOperand::MO_GlobalAddress:
    PrintSymbolOperand(MO, O);
    break;

  case MachineOperand::MO_MachineBasicBlock:
    MO.getMBB()->getSymbol()->print(O, MAI);
    break;

  default:
    llvm_unreachable("Operand type not supported.");
  }
}

void NVPTXAsmPrinter::printMemOperand(const MachineInstr *MI, unsigned OpNum,
                                      raw_ostream &O, const char *Modifier) {
  printOperand(MI, OpNum, O);

  if (Modifier && strcmp(Modifier, "add") == 0) {
    O << ", ";
    printOperand(MI, OpNum + 1, O);
  } else {
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0)
      return; // don't print ',0' or '+0'
    O << "+";
    printOperand(MI, OpNum + 1, O);
  }
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeNVPTXAsmPrinter() {
  RegisterAsmPrinter<NVPTXAsmPrinter> X(getTheNVPTXTarget32());
  RegisterAsmPrinter<NVPTXAsmPrinter> Y(getTheNVPTXTarget64());
}
