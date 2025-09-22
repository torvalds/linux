//===-- SPIRVAsmPrinter.cpp - SPIR-V LLVM assembly writer ------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a printer that converts from our internal representation
// of machine-dependent LLVM code to the SPIR-V assembly language.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/SPIRVInstPrinter.h"
#include "SPIRV.h"
#include "SPIRVInstrInfo.h"
#include "SPIRVMCInstLower.h"
#include "SPIRVModuleAnalysis.h"
#include "SPIRVSubtarget.h"
#include "SPIRVTargetMachine.h"
#include "SPIRVUtils.h"
#include "TargetInfo/SPIRVTargetInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSPIRVObjectWriter.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

namespace {
class SPIRVAsmPrinter : public AsmPrinter {
  unsigned NLabels = 0;

public:
  explicit SPIRVAsmPrinter(TargetMachine &TM,
                           std::unique_ptr<MCStreamer> Streamer)
      : AsmPrinter(TM, std::move(Streamer)), ST(nullptr), TII(nullptr) {}
  bool ModuleSectionsEmitted;
  const SPIRVSubtarget *ST;
  const SPIRVInstrInfo *TII;

  StringRef getPassName() const override { return "SPIRV Assembly Printer"; }
  void printOperand(const MachineInstr *MI, int OpNum, raw_ostream &O);
  bool PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                       const char *ExtraCode, raw_ostream &O) override;

  void outputMCInst(MCInst &Inst);
  void outputInstruction(const MachineInstr *MI);
  void outputModuleSection(SPIRV::ModuleSectionType MSType);
  void outputGlobalRequirements();
  void outputEntryPoints();
  void outputDebugSourceAndStrings(const Module &M);
  void outputOpExtInstImports(const Module &M);
  void outputOpMemoryModel();
  void outputOpFunctionEnd();
  void outputExtFuncDecls();
  void outputExecutionModeFromMDNode(Register Reg, MDNode *Node,
                                     SPIRV::ExecutionMode::ExecutionMode EM,
                                     unsigned ExpectMDOps, int64_t DefVal);
  void outputExecutionModeFromNumthreadsAttribute(
      const Register &Reg, const Attribute &Attr,
      SPIRV::ExecutionMode::ExecutionMode EM);
  void outputExecutionMode(const Module &M);
  void outputAnnotations(const Module &M);
  void outputModuleSections();

  void emitInstruction(const MachineInstr *MI) override;
  void emitFunctionEntryLabel() override {}
  void emitFunctionHeader() override;
  void emitFunctionBodyStart() override {}
  void emitFunctionBodyEnd() override;
  void emitBasicBlockStart(const MachineBasicBlock &MBB) override;
  void emitBasicBlockEnd(const MachineBasicBlock &MBB) override {}
  void emitGlobalVariable(const GlobalVariable *GV) override {}
  void emitOpLabel(const MachineBasicBlock &MBB);
  void emitEndOfAsmFile(Module &M) override;
  bool doInitialization(Module &M) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;
  SPIRV::ModuleAnalysisInfo *MAI;
};
} // namespace

void SPIRVAsmPrinter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<SPIRVModuleAnalysis>();
  AU.addPreserved<SPIRVModuleAnalysis>();
  AsmPrinter::getAnalysisUsage(AU);
}

// If the module has no functions, we need output global info anyway.
void SPIRVAsmPrinter::emitEndOfAsmFile(Module &M) {
  if (ModuleSectionsEmitted == false) {
    outputModuleSections();
    ModuleSectionsEmitted = true;
  }

  ST = static_cast<const SPIRVTargetMachine &>(TM).getSubtargetImpl();
  VersionTuple SPIRVVersion = ST->getSPIRVVersion();
  uint32_t Major = SPIRVVersion.getMajor();
  uint32_t Minor = SPIRVVersion.getMinor().value_or(0);
  // Bound is an approximation that accounts for the maximum used register
  // number and number of generated OpLabels
  unsigned Bound = 2 * (ST->getBound() + 1) + NLabels;
  if (MCAssembler *Asm = OutStreamer->getAssemblerPtr())
    static_cast<SPIRVObjectWriter &>(Asm->getWriter())
        .setBuildVersion(Major, Minor, Bound);
}

void SPIRVAsmPrinter::emitFunctionHeader() {
  if (ModuleSectionsEmitted == false) {
    outputModuleSections();
    ModuleSectionsEmitted = true;
  }
  // Get the subtarget from the current MachineFunction.
  ST = &MF->getSubtarget<SPIRVSubtarget>();
  TII = ST->getInstrInfo();
  const Function &F = MF->getFunction();

  if (isVerbose()) {
    OutStreamer->getCommentOS()
        << "-- Begin function "
        << GlobalValue::dropLLVMManglingEscape(F.getName()) << '\n';
  }

  auto Section = getObjFileLowering().SectionForGlobal(&F, TM);
  MF->setSection(Section);
}

void SPIRVAsmPrinter::outputOpFunctionEnd() {
  MCInst FunctionEndInst;
  FunctionEndInst.setOpcode(SPIRV::OpFunctionEnd);
  outputMCInst(FunctionEndInst);
}

// Emit OpFunctionEnd at the end of MF and clear BBNumToRegMap.
void SPIRVAsmPrinter::emitFunctionBodyEnd() {
  outputOpFunctionEnd();
  MAI->BBNumToRegMap.clear();
}

void SPIRVAsmPrinter::emitOpLabel(const MachineBasicBlock &MBB) {
  MCInst LabelInst;
  LabelInst.setOpcode(SPIRV::OpLabel);
  LabelInst.addOperand(MCOperand::createReg(MAI->getOrCreateMBBRegister(MBB)));
  outputMCInst(LabelInst);
  ++NLabels;
}

void SPIRVAsmPrinter::emitBasicBlockStart(const MachineBasicBlock &MBB) {
  assert(!MBB.empty() && "MBB is empty!");

  // If it's the first MBB in MF, it has OpFunction and OpFunctionParameter, so
  // OpLabel should be output after them.
  if (MBB.getNumber() == MF->front().getNumber()) {
    for (const MachineInstr &MI : MBB)
      if (MI.getOpcode() == SPIRV::OpFunction)
        return;
    // TODO: this case should be checked by the verifier.
    report_fatal_error("OpFunction is expected in the front MBB of MF");
  }
  emitOpLabel(MBB);
}

void SPIRVAsmPrinter::printOperand(const MachineInstr *MI, int OpNum,
                                   raw_ostream &O) {
  const MachineOperand &MO = MI->getOperand(OpNum);

  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    O << SPIRVInstPrinter::getRegisterName(MO.getReg());
    break;

  case MachineOperand::MO_Immediate:
    O << MO.getImm();
    break;

  case MachineOperand::MO_FPImmediate:
    O << MO.getFPImm();
    break;

  case MachineOperand::MO_MachineBasicBlock:
    O << *MO.getMBB()->getSymbol();
    break;

  case MachineOperand::MO_GlobalAddress:
    O << *getSymbol(MO.getGlobal());
    break;

  case MachineOperand::MO_BlockAddress: {
    MCSymbol *BA = GetBlockAddressSymbol(MO.getBlockAddress());
    O << BA->getName();
    break;
  }

  case MachineOperand::MO_ExternalSymbol:
    O << *GetExternalSymbolSymbol(MO.getSymbolName());
    break;

  case MachineOperand::MO_JumpTableIndex:
  case MachineOperand::MO_ConstantPoolIndex:
  default:
    llvm_unreachable("<unknown operand type>");
  }
}

bool SPIRVAsmPrinter::PrintAsmOperand(const MachineInstr *MI, unsigned OpNo,
                                      const char *ExtraCode, raw_ostream &O) {
  if (ExtraCode && ExtraCode[0])
    return true; // Invalid instruction - SPIR-V does not have special modifiers

  printOperand(MI, OpNo, O);
  return false;
}

static bool isFuncOrHeaderInstr(const MachineInstr *MI,
                                const SPIRVInstrInfo *TII) {
  return TII->isHeaderInstr(*MI) || MI->getOpcode() == SPIRV::OpFunction ||
         MI->getOpcode() == SPIRV::OpFunctionParameter;
}

void SPIRVAsmPrinter::outputMCInst(MCInst &Inst) {
  OutStreamer->emitInstruction(Inst, *OutContext.getSubtargetInfo());
}

void SPIRVAsmPrinter::outputInstruction(const MachineInstr *MI) {
  SPIRVMCInstLower MCInstLowering;
  MCInst TmpInst;
  MCInstLowering.lower(MI, TmpInst, MAI);
  outputMCInst(TmpInst);
}

void SPIRVAsmPrinter::emitInstruction(const MachineInstr *MI) {
  SPIRV_MC::verifyInstructionPredicates(MI->getOpcode(),
                                        getSubtargetInfo().getFeatureBits());

  if (!MAI->getSkipEmission(MI))
    outputInstruction(MI);

  // Output OpLabel after OpFunction and OpFunctionParameter in the first MBB.
  const MachineInstr *NextMI = MI->getNextNode();
  if (!MAI->hasMBBRegister(*MI->getParent()) && isFuncOrHeaderInstr(MI, TII) &&
      (!NextMI || !isFuncOrHeaderInstr(NextMI, TII))) {
    assert(MI->getParent()->getNumber() == MF->front().getNumber() &&
           "OpFunction is not in the front MBB of MF");
    emitOpLabel(*MI->getParent());
  }
}

void SPIRVAsmPrinter::outputModuleSection(SPIRV::ModuleSectionType MSType) {
  for (MachineInstr *MI : MAI->getMSInstrs(MSType))
    outputInstruction(MI);
}

void SPIRVAsmPrinter::outputDebugSourceAndStrings(const Module &M) {
  // Output OpSourceExtensions.
  for (auto &Str : MAI->SrcExt) {
    MCInst Inst;
    Inst.setOpcode(SPIRV::OpSourceExtension);
    addStringImm(Str.first(), Inst);
    outputMCInst(Inst);
  }
  // Output OpSource.
  MCInst Inst;
  Inst.setOpcode(SPIRV::OpSource);
  Inst.addOperand(MCOperand::createImm(static_cast<unsigned>(MAI->SrcLang)));
  Inst.addOperand(
      MCOperand::createImm(static_cast<unsigned>(MAI->SrcLangVersion)));
  outputMCInst(Inst);
}

void SPIRVAsmPrinter::outputOpExtInstImports(const Module &M) {
  for (auto &CU : MAI->ExtInstSetMap) {
    unsigned Set = CU.first;
    Register Reg = CU.second;
    MCInst Inst;
    Inst.setOpcode(SPIRV::OpExtInstImport);
    Inst.addOperand(MCOperand::createReg(Reg));
    addStringImm(getExtInstSetName(
                     static_cast<SPIRV::InstructionSet::InstructionSet>(Set)),
                 Inst);
    outputMCInst(Inst);
  }
}

void SPIRVAsmPrinter::outputOpMemoryModel() {
  MCInst Inst;
  Inst.setOpcode(SPIRV::OpMemoryModel);
  Inst.addOperand(MCOperand::createImm(static_cast<unsigned>(MAI->Addr)));
  Inst.addOperand(MCOperand::createImm(static_cast<unsigned>(MAI->Mem)));
  outputMCInst(Inst);
}

// Before the OpEntryPoints' output, we need to add the entry point's
// interfaces. The interface is a list of IDs of global OpVariable instructions.
// These declare the set of global variables from a module that form
// the interface of this entry point.
void SPIRVAsmPrinter::outputEntryPoints() {
  // Find all OpVariable IDs with required StorageClass.
  DenseSet<Register> InterfaceIDs;
  for (MachineInstr *MI : MAI->GlobalVarList) {
    assert(MI->getOpcode() == SPIRV::OpVariable);
    auto SC = static_cast<SPIRV::StorageClass::StorageClass>(
        MI->getOperand(2).getImm());
    // Before version 1.4, the interface's storage classes are limited to
    // the Input and Output storage classes. Starting with version 1.4,
    // the interface's storage classes are all storage classes used in
    // declaring all global variables referenced by the entry point call tree.
    if (ST->isAtLeastSPIRVVer(VersionTuple(1, 4)) ||
        SC == SPIRV::StorageClass::Input || SC == SPIRV::StorageClass::Output) {
      MachineFunction *MF = MI->getMF();
      Register Reg = MAI->getRegisterAlias(MF, MI->getOperand(0).getReg());
      InterfaceIDs.insert(Reg);
    }
  }

  // Output OpEntryPoints adding interface args to all of them.
  for (MachineInstr *MI : MAI->getMSInstrs(SPIRV::MB_EntryPoints)) {
    SPIRVMCInstLower MCInstLowering;
    MCInst TmpInst;
    MCInstLowering.lower(MI, TmpInst, MAI);
    for (Register Reg : InterfaceIDs) {
      assert(Reg.isValid());
      TmpInst.addOperand(MCOperand::createReg(Reg));
    }
    outputMCInst(TmpInst);
  }
}

// Create global OpCapability instructions for the required capabilities.
void SPIRVAsmPrinter::outputGlobalRequirements() {
  // Abort here if not all requirements can be satisfied.
  MAI->Reqs.checkSatisfiable(*ST);

  for (const auto &Cap : MAI->Reqs.getMinimalCapabilities()) {
    MCInst Inst;
    Inst.setOpcode(SPIRV::OpCapability);
    Inst.addOperand(MCOperand::createImm(Cap));
    outputMCInst(Inst);
  }

  // Generate the final OpExtensions with strings instead of enums.
  for (const auto &Ext : MAI->Reqs.getExtensions()) {
    MCInst Inst;
    Inst.setOpcode(SPIRV::OpExtension);
    addStringImm(getSymbolicOperandMnemonic(
                     SPIRV::OperandCategory::ExtensionOperand, Ext),
                 Inst);
    outputMCInst(Inst);
  }
  // TODO add a pseudo instr for version number.
}

void SPIRVAsmPrinter::outputExtFuncDecls() {
  // Insert OpFunctionEnd after each declaration.
  SmallVectorImpl<MachineInstr *>::iterator
      I = MAI->getMSInstrs(SPIRV::MB_ExtFuncDecls).begin(),
      E = MAI->getMSInstrs(SPIRV::MB_ExtFuncDecls).end();
  for (; I != E; ++I) {
    outputInstruction(*I);
    if ((I + 1) == E || (*(I + 1))->getOpcode() == SPIRV::OpFunction)
      outputOpFunctionEnd();
  }
}

// Encode LLVM type by SPIR-V execution mode VecTypeHint.
static unsigned encodeVecTypeHint(Type *Ty) {
  if (Ty->isHalfTy())
    return 4;
  if (Ty->isFloatTy())
    return 5;
  if (Ty->isDoubleTy())
    return 6;
  if (IntegerType *IntTy = dyn_cast<IntegerType>(Ty)) {
    switch (IntTy->getIntegerBitWidth()) {
    case 8:
      return 0;
    case 16:
      return 1;
    case 32:
      return 2;
    case 64:
      return 3;
    default:
      llvm_unreachable("invalid integer type");
    }
  }
  if (FixedVectorType *VecTy = dyn_cast<FixedVectorType>(Ty)) {
    Type *EleTy = VecTy->getElementType();
    unsigned Size = VecTy->getNumElements();
    return Size << 16 | encodeVecTypeHint(EleTy);
  }
  llvm_unreachable("invalid type");
}

static void addOpsFromMDNode(MDNode *MDN, MCInst &Inst,
                             SPIRV::ModuleAnalysisInfo *MAI) {
  for (const MDOperand &MDOp : MDN->operands()) {
    if (auto *CMeta = dyn_cast<ConstantAsMetadata>(MDOp)) {
      Constant *C = CMeta->getValue();
      if (ConstantInt *Const = dyn_cast<ConstantInt>(C)) {
        Inst.addOperand(MCOperand::createImm(Const->getZExtValue()));
      } else if (auto *CE = dyn_cast<Function>(C)) {
        Register FuncReg = MAI->getFuncReg(CE);
        assert(FuncReg.isValid());
        Inst.addOperand(MCOperand::createReg(FuncReg));
      }
    }
  }
}

void SPIRVAsmPrinter::outputExecutionModeFromMDNode(
    Register Reg, MDNode *Node, SPIRV::ExecutionMode::ExecutionMode EM,
    unsigned ExpectMDOps, int64_t DefVal) {
  MCInst Inst;
  Inst.setOpcode(SPIRV::OpExecutionMode);
  Inst.addOperand(MCOperand::createReg(Reg));
  Inst.addOperand(MCOperand::createImm(static_cast<unsigned>(EM)));
  addOpsFromMDNode(Node, Inst, MAI);
  // reqd_work_group_size and work_group_size_hint require 3 operands,
  // if metadata contains less operands, just add a default value
  unsigned NodeSz = Node->getNumOperands();
  if (ExpectMDOps > 0 && NodeSz < ExpectMDOps)
    for (unsigned i = NodeSz; i < ExpectMDOps; ++i)
      Inst.addOperand(MCOperand::createImm(DefVal));
  outputMCInst(Inst);
}

void SPIRVAsmPrinter::outputExecutionModeFromNumthreadsAttribute(
    const Register &Reg, const Attribute &Attr,
    SPIRV::ExecutionMode::ExecutionMode EM) {
  assert(Attr.isValid() && "Function called with an invalid attribute.");

  MCInst Inst;
  Inst.setOpcode(SPIRV::OpExecutionMode);
  Inst.addOperand(MCOperand::createReg(Reg));
  Inst.addOperand(MCOperand::createImm(static_cast<unsigned>(EM)));

  SmallVector<StringRef> NumThreads;
  Attr.getValueAsString().split(NumThreads, ',');
  assert(NumThreads.size() == 3 && "invalid numthreads");
  for (uint32_t i = 0; i < 3; ++i) {
    uint32_t V;
    [[maybe_unused]] bool Result = NumThreads[i].getAsInteger(10, V);
    assert(!Result && "Failed to parse numthreads");
    Inst.addOperand(MCOperand::createImm(V));
  }

  outputMCInst(Inst);
}

void SPIRVAsmPrinter::outputExecutionMode(const Module &M) {
  NamedMDNode *Node = M.getNamedMetadata("spirv.ExecutionMode");
  if (Node) {
    for (unsigned i = 0; i < Node->getNumOperands(); i++) {
      MCInst Inst;
      Inst.setOpcode(SPIRV::OpExecutionMode);
      addOpsFromMDNode(cast<MDNode>(Node->getOperand(i)), Inst, MAI);
      outputMCInst(Inst);
    }
  }
  for (auto FI = M.begin(), E = M.end(); FI != E; ++FI) {
    const Function &F = *FI;
    // Only operands of OpEntryPoint instructions are allowed to be
    // <Entry Point> operands of OpExecutionMode
    if (F.isDeclaration() || !isEntryPoint(F))
      continue;
    Register FReg = MAI->getFuncReg(&F);
    assert(FReg.isValid());
    if (MDNode *Node = F.getMetadata("reqd_work_group_size"))
      outputExecutionModeFromMDNode(FReg, Node, SPIRV::ExecutionMode::LocalSize,
                                    3, 1);
    if (Attribute Attr = F.getFnAttribute("hlsl.numthreads"); Attr.isValid())
      outputExecutionModeFromNumthreadsAttribute(
          FReg, Attr, SPIRV::ExecutionMode::LocalSize);
    if (MDNode *Node = F.getMetadata("work_group_size_hint"))
      outputExecutionModeFromMDNode(FReg, Node,
                                    SPIRV::ExecutionMode::LocalSizeHint, 3, 1);
    if (MDNode *Node = F.getMetadata("intel_reqd_sub_group_size"))
      outputExecutionModeFromMDNode(FReg, Node,
                                    SPIRV::ExecutionMode::SubgroupSize, 0, 0);
    if (MDNode *Node = F.getMetadata("vec_type_hint")) {
      MCInst Inst;
      Inst.setOpcode(SPIRV::OpExecutionMode);
      Inst.addOperand(MCOperand::createReg(FReg));
      unsigned EM = static_cast<unsigned>(SPIRV::ExecutionMode::VecTypeHint);
      Inst.addOperand(MCOperand::createImm(EM));
      unsigned TypeCode = encodeVecTypeHint(getMDOperandAsType(Node, 0));
      Inst.addOperand(MCOperand::createImm(TypeCode));
      outputMCInst(Inst);
    }
    if (ST->isOpenCLEnv() && !M.getNamedMetadata("spirv.ExecutionMode") &&
        !M.getNamedMetadata("opencl.enable.FP_CONTRACT")) {
      MCInst Inst;
      Inst.setOpcode(SPIRV::OpExecutionMode);
      Inst.addOperand(MCOperand::createReg(FReg));
      unsigned EM = static_cast<unsigned>(SPIRV::ExecutionMode::ContractionOff);
      Inst.addOperand(MCOperand::createImm(EM));
      outputMCInst(Inst);
    }
  }
}

void SPIRVAsmPrinter::outputAnnotations(const Module &M) {
  outputModuleSection(SPIRV::MB_Annotations);
  // Process llvm.global.annotations special global variable.
  for (auto F = M.global_begin(), E = M.global_end(); F != E; ++F) {
    if ((*F).getName() != "llvm.global.annotations")
      continue;
    const GlobalVariable *V = &(*F);
    const ConstantArray *CA = cast<ConstantArray>(V->getOperand(0));
    for (Value *Op : CA->operands()) {
      ConstantStruct *CS = cast<ConstantStruct>(Op);
      // The first field of the struct contains a pointer to
      // the annotated variable.
      Value *AnnotatedVar = CS->getOperand(0)->stripPointerCasts();
      if (!isa<Function>(AnnotatedVar))
        report_fatal_error("Unsupported value in llvm.global.annotations");
      Function *Func = cast<Function>(AnnotatedVar);
      Register Reg = MAI->getFuncReg(Func);
      if (!Reg.isValid()) {
        std::string DiagMsg;
        raw_string_ostream OS(DiagMsg);
        AnnotatedVar->print(OS);
        DiagMsg = "Unknown function in llvm.global.annotations: " + DiagMsg;
        report_fatal_error(DiagMsg.c_str());
      }

      // The second field contains a pointer to a global annotation string.
      GlobalVariable *GV =
          cast<GlobalVariable>(CS->getOperand(1)->stripPointerCasts());

      StringRef AnnotationString;
      getConstantStringInfo(GV, AnnotationString);
      MCInst Inst;
      Inst.setOpcode(SPIRV::OpDecorate);
      Inst.addOperand(MCOperand::createReg(Reg));
      unsigned Dec = static_cast<unsigned>(SPIRV::Decoration::UserSemantic);
      Inst.addOperand(MCOperand::createImm(Dec));
      addStringImm(AnnotationString, Inst);
      outputMCInst(Inst);
    }
  }
}

void SPIRVAsmPrinter::outputModuleSections() {
  const Module *M = MMI->getModule();
  // Get the global subtarget to output module-level info.
  ST = static_cast<const SPIRVTargetMachine &>(TM).getSubtargetImpl();
  TII = ST->getInstrInfo();
  MAI = &SPIRVModuleAnalysis::MAI;
  assert(ST && TII && MAI && M && "Module analysis is required");
  // Output instructions according to the Logical Layout of a Module:
  // 1,2. All OpCapability instructions, then optional OpExtension instructions.
  outputGlobalRequirements();
  // 3. Optional OpExtInstImport instructions.
  outputOpExtInstImports(*M);
  // 4. The single required OpMemoryModel instruction.
  outputOpMemoryModel();
  // 5. All entry point declarations, using OpEntryPoint.
  outputEntryPoints();
  // 6. Execution-mode declarations, using OpExecutionMode or OpExecutionModeId.
  outputExecutionMode(*M);
  // 7a. Debug: all OpString, OpSourceExtension, OpSource, and
  // OpSourceContinued, without forward references.
  outputDebugSourceAndStrings(*M);
  // 7b. Debug: all OpName and all OpMemberName.
  outputModuleSection(SPIRV::MB_DebugNames);
  // 7c. Debug: all OpModuleProcessed instructions.
  outputModuleSection(SPIRV::MB_DebugModuleProcessed);
  // 8. All annotation instructions (all decorations).
  outputAnnotations(*M);
  // 9. All type declarations (OpTypeXXX instructions), all constant
  // instructions, and all global variable declarations. This section is
  // the first section to allow use of: OpLine and OpNoLine debug information;
  // non-semantic instructions with OpExtInst.
  outputModuleSection(SPIRV::MB_TypeConstVars);
  // 10. All function declarations (functions without a body).
  outputExtFuncDecls();
  // 11. All function definitions (functions with a body).
  // This is done in regular function output.
}

bool SPIRVAsmPrinter::doInitialization(Module &M) {
  ModuleSectionsEmitted = false;
  // We need to call the parent's one explicitly.
  return AsmPrinter::doInitialization(M);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeSPIRVAsmPrinter() {
  RegisterAsmPrinter<SPIRVAsmPrinter> X(getTheSPIRV32Target());
  RegisterAsmPrinter<SPIRVAsmPrinter> Y(getTheSPIRV64Target());
  RegisterAsmPrinter<SPIRVAsmPrinter> Z(getTheSPIRVLogicalTarget());
}
