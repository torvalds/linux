//===- MIRParser.cpp - MIR serialization format parser implementation -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the class that parses the optional LLVM IR and machine
// functions that are stored in MIR files.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRParser/MIRParser.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/AsmParser/SlotMapping.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionAnalysis.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Target/TargetMachine.h"
#include <memory>

using namespace llvm;

namespace llvm {
class MDNode;
class RegisterBank;

/// This class implements the parsing of LLVM IR that's embedded inside a MIR
/// file.
class MIRParserImpl {
  SourceMgr SM;
  LLVMContext &Context;
  yaml::Input In;
  StringRef Filename;
  SlotMapping IRSlots;
  std::unique_ptr<PerTargetMIParsingState> Target;

  /// True when the MIR file doesn't have LLVM IR. Dummy IR functions are
  /// created and inserted into the given module when this is true.
  bool NoLLVMIR = false;
  /// True when a well formed MIR file does not contain any MIR/machine function
  /// parts.
  bool NoMIRDocuments = false;

  std::function<void(Function &)> ProcessIRFunction;

public:
  MIRParserImpl(std::unique_ptr<MemoryBuffer> Contents, StringRef Filename,
                LLVMContext &Context,
                std::function<void(Function &)> ProcessIRFunction);

  void reportDiagnostic(const SMDiagnostic &Diag);

  /// Report an error with the given message at unknown location.
  ///
  /// Always returns true.
  bool error(const Twine &Message);

  /// Report an error with the given message at the given location.
  ///
  /// Always returns true.
  bool error(SMLoc Loc, const Twine &Message);

  /// Report a given error with the location translated from the location in an
  /// embedded string literal to a location in the MIR file.
  ///
  /// Always returns true.
  bool error(const SMDiagnostic &Error, SMRange SourceRange);

  /// Try to parse the optional LLVM module and the machine functions in the MIR
  /// file.
  ///
  /// Return null if an error occurred.
  std::unique_ptr<Module>
  parseIRModule(DataLayoutCallbackTy DataLayoutCallback);

  /// Create an empty function with the given name.
  Function *createDummyFunction(StringRef Name, Module &M);

  bool parseMachineFunctions(Module &M, MachineModuleInfo &MMI,
                             ModuleAnalysisManager *FAM = nullptr);

  /// Parse the machine function in the current YAML document.
  ///
  ///
  /// Return true if an error occurred.
  bool parseMachineFunction(Module &M, MachineModuleInfo &MMI,
                            ModuleAnalysisManager *FAM);

  /// Initialize the machine function to the state that's described in the MIR
  /// file.
  ///
  /// Return true if error occurred.
  bool initializeMachineFunction(const yaml::MachineFunction &YamlMF,
                                 MachineFunction &MF);

  bool parseRegisterInfo(PerFunctionMIParsingState &PFS,
                         const yaml::MachineFunction &YamlMF);

  bool setupRegisterInfo(const PerFunctionMIParsingState &PFS,
                         const yaml::MachineFunction &YamlMF);

  bool initializeFrameInfo(PerFunctionMIParsingState &PFS,
                           const yaml::MachineFunction &YamlMF);

  bool initializeCallSiteInfo(PerFunctionMIParsingState &PFS,
                              const yaml::MachineFunction &YamlMF);

  bool parseCalleeSavedRegister(PerFunctionMIParsingState &PFS,
                                std::vector<CalleeSavedInfo> &CSIInfo,
                                const yaml::StringValue &RegisterSource,
                                bool IsRestored, int FrameIdx);

  struct VarExprLoc {
    DILocalVariable *DIVar = nullptr;
    DIExpression *DIExpr = nullptr;
    DILocation *DILoc = nullptr;
  };

  std::optional<VarExprLoc> parseVarExprLoc(PerFunctionMIParsingState &PFS,
                                            const yaml::StringValue &VarStr,
                                            const yaml::StringValue &ExprStr,
                                            const yaml::StringValue &LocStr);
  template <typename T>
  bool parseStackObjectsDebugInfo(PerFunctionMIParsingState &PFS,
                                  const T &Object,
                                  int FrameIdx);

  bool initializeConstantPool(PerFunctionMIParsingState &PFS,
                              MachineConstantPool &ConstantPool,
                              const yaml::MachineFunction &YamlMF);

  bool initializeJumpTableInfo(PerFunctionMIParsingState &PFS,
                               const yaml::MachineJumpTable &YamlJTI);

  bool parseMachineMetadataNodes(PerFunctionMIParsingState &PFS,
                                 MachineFunction &MF,
                                 const yaml::MachineFunction &YMF);

private:
  bool parseMDNode(PerFunctionMIParsingState &PFS, MDNode *&Node,
                   const yaml::StringValue &Source);

  bool parseMBBReference(PerFunctionMIParsingState &PFS,
                         MachineBasicBlock *&MBB,
                         const yaml::StringValue &Source);

  bool parseMachineMetadata(PerFunctionMIParsingState &PFS,
                            const yaml::StringValue &Source);

  /// Return a MIR diagnostic converted from an MI string diagnostic.
  SMDiagnostic diagFromMIStringDiag(const SMDiagnostic &Error,
                                    SMRange SourceRange);

  /// Return a MIR diagnostic converted from a diagnostic located in a YAML
  /// block scalar string.
  SMDiagnostic diagFromBlockStringDiag(const SMDiagnostic &Error,
                                       SMRange SourceRange);

  void computeFunctionProperties(MachineFunction &MF);

  void setupDebugValueTracking(MachineFunction &MF,
    PerFunctionMIParsingState &PFS, const yaml::MachineFunction &YamlMF);
};

} // end namespace llvm

static void handleYAMLDiag(const SMDiagnostic &Diag, void *Context) {
  reinterpret_cast<MIRParserImpl *>(Context)->reportDiagnostic(Diag);
}

MIRParserImpl::MIRParserImpl(std::unique_ptr<MemoryBuffer> Contents,
                             StringRef Filename, LLVMContext &Context,
                             std::function<void(Function &)> Callback)
    : Context(Context),
      In(SM.getMemoryBuffer(SM.AddNewSourceBuffer(std::move(Contents), SMLoc()))
             ->getBuffer(),
         nullptr, handleYAMLDiag, this),
      Filename(Filename), ProcessIRFunction(Callback) {
  In.setContext(&In);
}

bool MIRParserImpl::error(const Twine &Message) {
  Context.diagnose(DiagnosticInfoMIRParser(
      DS_Error, SMDiagnostic(Filename, SourceMgr::DK_Error, Message.str())));
  return true;
}

bool MIRParserImpl::error(SMLoc Loc, const Twine &Message) {
  Context.diagnose(DiagnosticInfoMIRParser(
      DS_Error, SM.GetMessage(Loc, SourceMgr::DK_Error, Message)));
  return true;
}

bool MIRParserImpl::error(const SMDiagnostic &Error, SMRange SourceRange) {
  assert(Error.getKind() == SourceMgr::DK_Error && "Expected an error");
  reportDiagnostic(diagFromMIStringDiag(Error, SourceRange));
  return true;
}

void MIRParserImpl::reportDiagnostic(const SMDiagnostic &Diag) {
  DiagnosticSeverity Kind;
  switch (Diag.getKind()) {
  case SourceMgr::DK_Error:
    Kind = DS_Error;
    break;
  case SourceMgr::DK_Warning:
    Kind = DS_Warning;
    break;
  case SourceMgr::DK_Note:
    Kind = DS_Note;
    break;
  case SourceMgr::DK_Remark:
    llvm_unreachable("remark unexpected");
    break;
  }
  Context.diagnose(DiagnosticInfoMIRParser(Kind, Diag));
}

std::unique_ptr<Module>
MIRParserImpl::parseIRModule(DataLayoutCallbackTy DataLayoutCallback) {
  if (!In.setCurrentDocument()) {
    if (In.error())
      return nullptr;
    // Create an empty module when the MIR file is empty.
    NoMIRDocuments = true;
    auto M = std::make_unique<Module>(Filename, Context);
    if (auto LayoutOverride =
            DataLayoutCallback(M->getTargetTriple(), M->getDataLayoutStr()))
      M->setDataLayout(*LayoutOverride);
    return M;
  }

  std::unique_ptr<Module> M;
  // Parse the block scalar manually so that we can return unique pointer
  // without having to go trough YAML traits.
  if (const auto *BSN =
          dyn_cast_or_null<yaml::BlockScalarNode>(In.getCurrentNode())) {
    SMDiagnostic Error;
    M = parseAssembly(MemoryBufferRef(BSN->getValue(), Filename), Error,
                      Context, &IRSlots, DataLayoutCallback);
    if (!M) {
      reportDiagnostic(diagFromBlockStringDiag(Error, BSN->getSourceRange()));
      return nullptr;
    }
    In.nextDocument();
    if (!In.setCurrentDocument())
      NoMIRDocuments = true;
  } else {
    // Create an new, empty module.
    M = std::make_unique<Module>(Filename, Context);
    if (auto LayoutOverride =
            DataLayoutCallback(M->getTargetTriple(), M->getDataLayoutStr()))
      M->setDataLayout(*LayoutOverride);
    NoLLVMIR = true;
  }
  return M;
}

bool MIRParserImpl::parseMachineFunctions(Module &M, MachineModuleInfo &MMI,
                                          ModuleAnalysisManager *MAM) {
  if (NoMIRDocuments)
    return false;

  // Parse the machine functions.
  do {
    if (parseMachineFunction(M, MMI, MAM))
      return true;
    In.nextDocument();
  } while (In.setCurrentDocument());

  return false;
}

Function *MIRParserImpl::createDummyFunction(StringRef Name, Module &M) {
  auto &Context = M.getContext();
  Function *F =
      Function::Create(FunctionType::get(Type::getVoidTy(Context), false),
                       Function::ExternalLinkage, Name, M);
  BasicBlock *BB = BasicBlock::Create(Context, "entry", F);
  new UnreachableInst(Context, BB);

  if (ProcessIRFunction)
    ProcessIRFunction(*F);

  return F;
}

bool MIRParserImpl::parseMachineFunction(Module &M, MachineModuleInfo &MMI,
                                         ModuleAnalysisManager *MAM) {
  // Parse the yaml.
  yaml::MachineFunction YamlMF;
  yaml::EmptyContext Ctx;

  const LLVMTargetMachine &TM = MMI.getTarget();
  YamlMF.MachineFuncInfo = std::unique_ptr<yaml::MachineFunctionInfo>(
      TM.createDefaultFuncInfoYAML());

  yaml::yamlize(In, YamlMF, false, Ctx);
  if (In.error())
    return true;

  // Search for the corresponding IR function.
  StringRef FunctionName = YamlMF.Name;
  Function *F = M.getFunction(FunctionName);
  if (!F) {
    if (NoLLVMIR) {
      F = createDummyFunction(FunctionName, M);
    } else {
      return error(Twine("function '") + FunctionName +
                   "' isn't defined in the provided LLVM IR");
    }
  }

  if (!MAM) {
    if (MMI.getMachineFunction(*F) != nullptr)
      return error(Twine("redefinition of machine function '") + FunctionName +
                   "'");

    // Create the MachineFunction.
    MachineFunction &MF = MMI.getOrCreateMachineFunction(*F);
    if (initializeMachineFunction(YamlMF, MF))
      return true;
  } else {
    auto &FAM =
        MAM->getResult<FunctionAnalysisManagerModuleProxy>(M).getManager();
    if (FAM.getCachedResult<MachineFunctionAnalysis>(*F))
      return error(Twine("redefinition of machine function '") + FunctionName +
                   "'");

    // Create the MachineFunction.
    MachineFunction &MF = FAM.getResult<MachineFunctionAnalysis>(*F).getMF();
    if (initializeMachineFunction(YamlMF, MF))
      return true;
  }

  return false;
}

static bool isSSA(const MachineFunction &MF) {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    Register Reg = Register::index2VirtReg(I);
    if (!MRI.hasOneDef(Reg) && !MRI.def_empty(Reg))
      return false;

    // Subregister defs are invalid in SSA.
    const MachineOperand *RegDef = MRI.getOneDef(Reg);
    if (RegDef && RegDef->getSubReg() != 0)
      return false;
  }
  return true;
}

void MIRParserImpl::computeFunctionProperties(MachineFunction &MF) {
  MachineFunctionProperties &Properties = MF.getProperties();

  bool HasPHI = false;
  bool HasInlineAsm = false;
  bool AllTiedOpsRewritten = true, HasTiedOps = false;
  for (const MachineBasicBlock &MBB : MF) {
    for (const MachineInstr &MI : MBB) {
      if (MI.isPHI())
        HasPHI = true;
      if (MI.isInlineAsm())
        HasInlineAsm = true;
      for (unsigned I = 0; I < MI.getNumOperands(); ++I) {
        const MachineOperand &MO = MI.getOperand(I);
        if (!MO.isReg() || !MO.getReg())
          continue;
        unsigned DefIdx;
        if (MO.isUse() && MI.isRegTiedToDefOperand(I, &DefIdx)) {
          HasTiedOps = true;
          if (MO.getReg() != MI.getOperand(DefIdx).getReg())
            AllTiedOpsRewritten = false;
        }
      }
    }
  }
  if (!HasPHI)
    Properties.set(MachineFunctionProperties::Property::NoPHIs);
  MF.setHasInlineAsm(HasInlineAsm);

  if (HasTiedOps && AllTiedOpsRewritten)
    Properties.set(MachineFunctionProperties::Property::TiedOpsRewritten);

  if (isSSA(MF))
    Properties.set(MachineFunctionProperties::Property::IsSSA);
  else
    Properties.reset(MachineFunctionProperties::Property::IsSSA);

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  if (MRI.getNumVirtRegs() == 0)
    Properties.set(MachineFunctionProperties::Property::NoVRegs);
}

bool MIRParserImpl::initializeCallSiteInfo(
    PerFunctionMIParsingState &PFS, const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  SMDiagnostic Error;
  const LLVMTargetMachine &TM = MF.getTarget();
  for (auto &YamlCSInfo : YamlMF.CallSitesInfo) {
    yaml::CallSiteInfo::MachineInstrLoc MILoc = YamlCSInfo.CallLocation;
    if (MILoc.BlockNum >= MF.size())
      return error(Twine(MF.getName()) +
                   Twine(" call instruction block out of range.") +
                   " Unable to reference bb:" + Twine(MILoc.BlockNum));
    auto CallB = std::next(MF.begin(), MILoc.BlockNum);
    if (MILoc.Offset >= CallB->size())
      return error(Twine(MF.getName()) +
                   Twine(" call instruction offset out of range.") +
                   " Unable to reference instruction at bb: " +
                   Twine(MILoc.BlockNum) + " at offset:" + Twine(MILoc.Offset));
    auto CallI = std::next(CallB->instr_begin(), MILoc.Offset);
    if (!CallI->isCall(MachineInstr::IgnoreBundle))
      return error(Twine(MF.getName()) +
                   Twine(" call site info should reference call "
                         "instruction. Instruction at bb:") +
                   Twine(MILoc.BlockNum) + " at offset:" + Twine(MILoc.Offset) +
                   " is not a call instruction");
    MachineFunction::CallSiteInfo CSInfo;
    for (auto ArgRegPair : YamlCSInfo.ArgForwardingRegs) {
      Register Reg;
      if (parseNamedRegisterReference(PFS, Reg, ArgRegPair.Reg.Value, Error))
        return error(Error, ArgRegPair.Reg.SourceRange);
      CSInfo.ArgRegPairs.emplace_back(Reg, ArgRegPair.ArgNo);
    }

    if (TM.Options.EmitCallSiteInfo)
      MF.addCallSiteInfo(&*CallI, std::move(CSInfo));
  }

  if (YamlMF.CallSitesInfo.size() && !TM.Options.EmitCallSiteInfo)
    return error(Twine("Call site info provided but not used"));
  return false;
}

void MIRParserImpl::setupDebugValueTracking(
    MachineFunction &MF, PerFunctionMIParsingState &PFS,
    const yaml::MachineFunction &YamlMF) {
  // Compute the value of the "next instruction number" field.
  unsigned MaxInstrNum = 0;
  for (auto &MBB : MF)
    for (auto &MI : MBB)
      MaxInstrNum = std::max((unsigned)MI.peekDebugInstrNum(), MaxInstrNum);
  MF.setDebugInstrNumberingCount(MaxInstrNum);

  // Load any substitutions.
  for (const auto &Sub : YamlMF.DebugValueSubstitutions) {
    MF.makeDebugValueSubstitution({Sub.SrcInst, Sub.SrcOp},
                                  {Sub.DstInst, Sub.DstOp}, Sub.Subreg);
  }

  // Flag for whether we're supposed to be using DBG_INSTR_REF.
  MF.setUseDebugInstrRef(YamlMF.UseDebugInstrRef);
}

bool
MIRParserImpl::initializeMachineFunction(const yaml::MachineFunction &YamlMF,
                                         MachineFunction &MF) {
  // TODO: Recreate the machine function.
  if (Target) {
    // Avoid clearing state if we're using the same subtarget again.
    Target->setTarget(MF.getSubtarget());
  } else {
    Target.reset(new PerTargetMIParsingState(MF.getSubtarget()));
  }

  MF.setAlignment(YamlMF.Alignment.valueOrOne());
  MF.setExposesReturnsTwice(YamlMF.ExposesReturnsTwice);
  MF.setHasWinCFI(YamlMF.HasWinCFI);

  MF.setCallsEHReturn(YamlMF.CallsEHReturn);
  MF.setCallsUnwindInit(YamlMF.CallsUnwindInit);
  MF.setHasEHCatchret(YamlMF.HasEHCatchret);
  MF.setHasEHScopes(YamlMF.HasEHScopes);
  MF.setHasEHFunclets(YamlMF.HasEHFunclets);
  MF.setIsOutlined(YamlMF.IsOutlined);

  if (YamlMF.Legalized)
    MF.getProperties().set(MachineFunctionProperties::Property::Legalized);
  if (YamlMF.RegBankSelected)
    MF.getProperties().set(
        MachineFunctionProperties::Property::RegBankSelected);
  if (YamlMF.Selected)
    MF.getProperties().set(MachineFunctionProperties::Property::Selected);
  if (YamlMF.FailedISel)
    MF.getProperties().set(MachineFunctionProperties::Property::FailedISel);
  if (YamlMF.FailsVerification)
    MF.getProperties().set(
        MachineFunctionProperties::Property::FailsVerification);
  if (YamlMF.TracksDebugUserValues)
    MF.getProperties().set(
        MachineFunctionProperties::Property::TracksDebugUserValues);

  PerFunctionMIParsingState PFS(MF, SM, IRSlots, *Target);
  if (parseRegisterInfo(PFS, YamlMF))
    return true;
  if (!YamlMF.Constants.empty()) {
    auto *ConstantPool = MF.getConstantPool();
    assert(ConstantPool && "Constant pool must be created");
    if (initializeConstantPool(PFS, *ConstantPool, YamlMF))
      return true;
  }
  if (!YamlMF.MachineMetadataNodes.empty() &&
      parseMachineMetadataNodes(PFS, MF, YamlMF))
    return true;

  StringRef BlockStr = YamlMF.Body.Value.Value;
  SMDiagnostic Error;
  SourceMgr BlockSM;
  BlockSM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(BlockStr, "",/*RequiresNullTerminator=*/false),
      SMLoc());
  PFS.SM = &BlockSM;
  if (parseMachineBasicBlockDefinitions(PFS, BlockStr, Error)) {
    reportDiagnostic(
        diagFromBlockStringDiag(Error, YamlMF.Body.Value.SourceRange));
    return true;
  }
  // Check Basic Block Section Flags.
  if (MF.getTarget().getBBSectionsType() == BasicBlockSection::Labels) {
    MF.setBBSectionsType(BasicBlockSection::Labels);
  } else if (MF.hasBBSections()) {
    MF.assignBeginEndSections();
  }
  PFS.SM = &SM;

  // Initialize the frame information after creating all the MBBs so that the
  // MBB references in the frame information can be resolved.
  if (initializeFrameInfo(PFS, YamlMF))
    return true;
  // Initialize the jump table after creating all the MBBs so that the MBB
  // references can be resolved.
  if (!YamlMF.JumpTableInfo.Entries.empty() &&
      initializeJumpTableInfo(PFS, YamlMF.JumpTableInfo))
    return true;
  // Parse the machine instructions after creating all of the MBBs so that the
  // parser can resolve the MBB references.
  StringRef InsnStr = YamlMF.Body.Value.Value;
  SourceMgr InsnSM;
  InsnSM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(InsnStr, "", /*RequiresNullTerminator=*/false),
      SMLoc());
  PFS.SM = &InsnSM;
  if (parseMachineInstructions(PFS, InsnStr, Error)) {
    reportDiagnostic(
        diagFromBlockStringDiag(Error, YamlMF.Body.Value.SourceRange));
    return true;
  }
  PFS.SM = &SM;

  if (setupRegisterInfo(PFS, YamlMF))
    return true;

  if (YamlMF.MachineFuncInfo) {
    const LLVMTargetMachine &TM = MF.getTarget();
    // Note this is called after the initial constructor of the
    // MachineFunctionInfo based on the MachineFunction, which may depend on the
    // IR.

    SMRange SrcRange;
    if (TM.parseMachineFunctionInfo(*YamlMF.MachineFuncInfo, PFS, Error,
                                    SrcRange)) {
      return error(Error, SrcRange);
    }
  }

  // Set the reserved registers after parsing MachineFuncInfo. The target may
  // have been recording information used to select the reserved registers
  // there.
  // FIXME: This is a temporary workaround until the reserved registers can be
  // serialized.
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MRI.freezeReservedRegs();

  computeFunctionProperties(MF);

  if (initializeCallSiteInfo(PFS, YamlMF))
    return false;

  setupDebugValueTracking(MF, PFS, YamlMF);

  MF.getSubtarget().mirFileLoaded(MF);

  MF.verify();
  return false;
}

bool MIRParserImpl::parseRegisterInfo(PerFunctionMIParsingState &PFS,
                                      const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  assert(RegInfo.tracksLiveness());
  if (!YamlMF.TracksRegLiveness)
    RegInfo.invalidateLiveness();

  SMDiagnostic Error;
  // Parse the virtual register information.
  for (const auto &VReg : YamlMF.VirtualRegisters) {
    VRegInfo &Info = PFS.getVRegInfo(VReg.ID.Value);
    if (Info.Explicit)
      return error(VReg.ID.SourceRange.Start,
                   Twine("redefinition of virtual register '%") +
                       Twine(VReg.ID.Value) + "'");
    Info.Explicit = true;

    if (VReg.Class.Value == "_") {
      Info.Kind = VRegInfo::GENERIC;
      Info.D.RegBank = nullptr;
    } else {
      const auto *RC = Target->getRegClass(VReg.Class.Value);
      if (RC) {
        Info.Kind = VRegInfo::NORMAL;
        Info.D.RC = RC;
      } else {
        const RegisterBank *RegBank = Target->getRegBank(VReg.Class.Value);
        if (!RegBank)
          return error(
              VReg.Class.SourceRange.Start,
              Twine("use of undefined register class or register bank '") +
                  VReg.Class.Value + "'");
        Info.Kind = VRegInfo::REGBANK;
        Info.D.RegBank = RegBank;
      }
    }

    if (!VReg.PreferredRegister.Value.empty()) {
      if (Info.Kind != VRegInfo::NORMAL)
        return error(VReg.Class.SourceRange.Start,
              Twine("preferred register can only be set for normal vregs"));

      if (parseRegisterReference(PFS, Info.PreferredReg,
                                 VReg.PreferredRegister.Value, Error))
        return error(Error, VReg.PreferredRegister.SourceRange);
    }
  }

  // Parse the liveins.
  for (const auto &LiveIn : YamlMF.LiveIns) {
    Register Reg;
    if (parseNamedRegisterReference(PFS, Reg, LiveIn.Register.Value, Error))
      return error(Error, LiveIn.Register.SourceRange);
    Register VReg;
    if (!LiveIn.VirtualRegister.Value.empty()) {
      VRegInfo *Info;
      if (parseVirtualRegisterReference(PFS, Info, LiveIn.VirtualRegister.Value,
                                        Error))
        return error(Error, LiveIn.VirtualRegister.SourceRange);
      VReg = Info->VReg;
    }
    RegInfo.addLiveIn(Reg, VReg);
  }

  // Parse the callee saved registers (Registers that will
  // be saved for the caller).
  if (YamlMF.CalleeSavedRegisters) {
    SmallVector<MCPhysReg, 16> CalleeSavedRegisters;
    for (const auto &RegSource : *YamlMF.CalleeSavedRegisters) {
      Register Reg;
      if (parseNamedRegisterReference(PFS, Reg, RegSource.Value, Error))
        return error(Error, RegSource.SourceRange);
      CalleeSavedRegisters.push_back(Reg);
    }
    RegInfo.setCalleeSavedRegs(CalleeSavedRegisters);
  }

  return false;
}

bool MIRParserImpl::setupRegisterInfo(const PerFunctionMIParsingState &PFS,
                                      const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();

  bool Error = false;
  // Create VRegs
  auto populateVRegInfo = [&](const VRegInfo &Info, Twine Name) {
    Register Reg = Info.VReg;
    switch (Info.Kind) {
    case VRegInfo::UNKNOWN:
      error(Twine("Cannot determine class/bank of virtual register ") +
            Name + " in function '" + MF.getName() + "'");
      Error = true;
      break;
    case VRegInfo::NORMAL:
      if (!Info.D.RC->isAllocatable()) {
        error(Twine("Cannot use non-allocatable class '") +
              TRI->getRegClassName(Info.D.RC) + "' for virtual register " +
              Name + " in function '" + MF.getName() + "'");
        Error = true;
        break;
      }

      MRI.setRegClass(Reg, Info.D.RC);
      if (Info.PreferredReg != 0)
        MRI.setSimpleHint(Reg, Info.PreferredReg);
      break;
    case VRegInfo::GENERIC:
      break;
    case VRegInfo::REGBANK:
      MRI.setRegBank(Reg, *Info.D.RegBank);
      break;
    }
  };

  for (const auto &P : PFS.VRegInfosNamed) {
    const VRegInfo &Info = *P.second;
    populateVRegInfo(Info, Twine(P.first()));
  }

  for (auto P : PFS.VRegInfos) {
    const VRegInfo &Info = *P.second;
    populateVRegInfo(Info, Twine(P.first));
  }

  // Compute MachineRegisterInfo::UsedPhysRegMask
  for (const MachineBasicBlock &MBB : MF) {
    // Make sure MRI knows about registers clobbered by unwinder.
    if (MBB.isEHPad())
      if (auto *RegMask = TRI->getCustomEHPadPreservedMask(MF))
        MRI.addPhysRegsUsedFromRegMask(RegMask);

    for (const MachineInstr &MI : MBB) {
      for (const MachineOperand &MO : MI.operands()) {
        if (!MO.isRegMask())
          continue;
        MRI.addPhysRegsUsedFromRegMask(MO.getRegMask());
      }
    }
  }

  return Error;
}

bool MIRParserImpl::initializeFrameInfo(PerFunctionMIParsingState &PFS,
                                        const yaml::MachineFunction &YamlMF) {
  MachineFunction &MF = PFS.MF;
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetFrameLowering *TFI = MF.getSubtarget().getFrameLowering();
  const Function &F = MF.getFunction();
  const yaml::MachineFrameInfo &YamlMFI = YamlMF.FrameInfo;
  MFI.setFrameAddressIsTaken(YamlMFI.IsFrameAddressTaken);
  MFI.setReturnAddressIsTaken(YamlMFI.IsReturnAddressTaken);
  MFI.setHasStackMap(YamlMFI.HasStackMap);
  MFI.setHasPatchPoint(YamlMFI.HasPatchPoint);
  MFI.setStackSize(YamlMFI.StackSize);
  MFI.setOffsetAdjustment(YamlMFI.OffsetAdjustment);
  if (YamlMFI.MaxAlignment)
    MFI.ensureMaxAlignment(Align(YamlMFI.MaxAlignment));
  MFI.setAdjustsStack(YamlMFI.AdjustsStack);
  MFI.setHasCalls(YamlMFI.HasCalls);
  if (YamlMFI.MaxCallFrameSize != ~0u)
    MFI.setMaxCallFrameSize(YamlMFI.MaxCallFrameSize);
  MFI.setCVBytesOfCalleeSavedRegisters(YamlMFI.CVBytesOfCalleeSavedRegisters);
  MFI.setHasOpaqueSPAdjustment(YamlMFI.HasOpaqueSPAdjustment);
  MFI.setHasVAStart(YamlMFI.HasVAStart);
  MFI.setHasMustTailInVarArgFunc(YamlMFI.HasMustTailInVarArgFunc);
  MFI.setHasTailCall(YamlMFI.HasTailCall);
  MFI.setCalleeSavedInfoValid(YamlMFI.IsCalleeSavedInfoValid);
  MFI.setLocalFrameSize(YamlMFI.LocalFrameSize);
  if (!YamlMFI.SavePoint.Value.empty()) {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(PFS, MBB, YamlMFI.SavePoint))
      return true;
    MFI.setSavePoint(MBB);
  }
  if (!YamlMFI.RestorePoint.Value.empty()) {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(PFS, MBB, YamlMFI.RestorePoint))
      return true;
    MFI.setRestorePoint(MBB);
  }

  std::vector<CalleeSavedInfo> CSIInfo;
  // Initialize the fixed frame objects.
  for (const auto &Object : YamlMF.FixedStackObjects) {
    int ObjectIdx;
    if (Object.Type != yaml::FixedMachineStackObject::SpillSlot)
      ObjectIdx = MFI.CreateFixedObject(Object.Size, Object.Offset,
                                        Object.IsImmutable, Object.IsAliased);
    else
      ObjectIdx = MFI.CreateFixedSpillStackObject(Object.Size, Object.Offset);

    if (!TFI->isSupportedStackID(Object.StackID))
      return error(Object.ID.SourceRange.Start,
                   Twine("StackID is not supported by target"));
    MFI.setStackID(ObjectIdx, Object.StackID);
    MFI.setObjectAlignment(ObjectIdx, Object.Alignment.valueOrOne());
    if (!PFS.FixedStackObjectSlots.insert(std::make_pair(Object.ID.Value,
                                                         ObjectIdx))
             .second)
      return error(Object.ID.SourceRange.Start,
                   Twine("redefinition of fixed stack object '%fixed-stack.") +
                       Twine(Object.ID.Value) + "'");
    if (parseCalleeSavedRegister(PFS, CSIInfo, Object.CalleeSavedRegister,
                                 Object.CalleeSavedRestored, ObjectIdx))
      return true;
    if (parseStackObjectsDebugInfo(PFS, Object, ObjectIdx))
      return true;
  }

  for (const auto &Object : YamlMF.EntryValueObjects) {
    SMDiagnostic Error;
    Register Reg;
    if (parseNamedRegisterReference(PFS, Reg, Object.EntryValueRegister.Value,
                                    Error))
      return error(Error, Object.EntryValueRegister.SourceRange);
    if (!Reg.isPhysical())
      return error(Object.EntryValueRegister.SourceRange.Start,
                   "Expected physical register for entry value field");
    std::optional<VarExprLoc> MaybeInfo = parseVarExprLoc(
        PFS, Object.DebugVar, Object.DebugExpr, Object.DebugLoc);
    if (!MaybeInfo)
      return true;
    if (MaybeInfo->DIVar || MaybeInfo->DIExpr || MaybeInfo->DILoc)
      PFS.MF.setVariableDbgInfo(MaybeInfo->DIVar, MaybeInfo->DIExpr,
                                Reg.asMCReg(), MaybeInfo->DILoc);
  }

  // Initialize the ordinary frame objects.
  for (const auto &Object : YamlMF.StackObjects) {
    int ObjectIdx;
    const AllocaInst *Alloca = nullptr;
    const yaml::StringValue &Name = Object.Name;
    if (!Name.Value.empty()) {
      Alloca = dyn_cast_or_null<AllocaInst>(
          F.getValueSymbolTable()->lookup(Name.Value));
      if (!Alloca)
        return error(Name.SourceRange.Start,
                     "alloca instruction named '" + Name.Value +
                         "' isn't defined in the function '" + F.getName() +
                         "'");
    }
    if (!TFI->isSupportedStackID(Object.StackID))
      return error(Object.ID.SourceRange.Start,
                   Twine("StackID is not supported by target"));
    if (Object.Type == yaml::MachineStackObject::VariableSized)
      ObjectIdx =
          MFI.CreateVariableSizedObject(Object.Alignment.valueOrOne(), Alloca);
    else
      ObjectIdx = MFI.CreateStackObject(
          Object.Size, Object.Alignment.valueOrOne(),
          Object.Type == yaml::MachineStackObject::SpillSlot, Alloca,
          Object.StackID);
    MFI.setObjectOffset(ObjectIdx, Object.Offset);

    if (!PFS.StackObjectSlots.insert(std::make_pair(Object.ID.Value, ObjectIdx))
             .second)
      return error(Object.ID.SourceRange.Start,
                   Twine("redefinition of stack object '%stack.") +
                       Twine(Object.ID.Value) + "'");
    if (parseCalleeSavedRegister(PFS, CSIInfo, Object.CalleeSavedRegister,
                                 Object.CalleeSavedRestored, ObjectIdx))
      return true;
    if (Object.LocalOffset)
      MFI.mapLocalFrameObject(ObjectIdx, *Object.LocalOffset);
    if (parseStackObjectsDebugInfo(PFS, Object, ObjectIdx))
      return true;
  }
  MFI.setCalleeSavedInfo(CSIInfo);
  if (!CSIInfo.empty())
    MFI.setCalleeSavedInfoValid(true);

  // Initialize the various stack object references after initializing the
  // stack objects.
  if (!YamlMFI.StackProtector.Value.empty()) {
    SMDiagnostic Error;
    int FI;
    if (parseStackObjectReference(PFS, FI, YamlMFI.StackProtector.Value, Error))
      return error(Error, YamlMFI.StackProtector.SourceRange);
    MFI.setStackProtectorIndex(FI);
  }

  if (!YamlMFI.FunctionContext.Value.empty()) {
    SMDiagnostic Error;
    int FI;
    if (parseStackObjectReference(PFS, FI, YamlMFI.FunctionContext.Value, Error))
      return error(Error, YamlMFI.FunctionContext.SourceRange);
    MFI.setFunctionContextIndex(FI);
  }

  return false;
}

bool MIRParserImpl::parseCalleeSavedRegister(PerFunctionMIParsingState &PFS,
    std::vector<CalleeSavedInfo> &CSIInfo,
    const yaml::StringValue &RegisterSource, bool IsRestored, int FrameIdx) {
  if (RegisterSource.Value.empty())
    return false;
  Register Reg;
  SMDiagnostic Error;
  if (parseNamedRegisterReference(PFS, Reg, RegisterSource.Value, Error))
    return error(Error, RegisterSource.SourceRange);
  CalleeSavedInfo CSI(Reg, FrameIdx);
  CSI.setRestored(IsRestored);
  CSIInfo.push_back(CSI);
  return false;
}

/// Verify that given node is of a certain type. Return true on error.
template <typename T>
static bool typecheckMDNode(T *&Result, MDNode *Node,
                            const yaml::StringValue &Source,
                            StringRef TypeString, MIRParserImpl &Parser) {
  if (!Node)
    return false;
  Result = dyn_cast<T>(Node);
  if (!Result)
    return Parser.error(Source.SourceRange.Start,
                        "expected a reference to a '" + TypeString +
                            "' metadata node");
  return false;
}

std::optional<MIRParserImpl::VarExprLoc> MIRParserImpl::parseVarExprLoc(
    PerFunctionMIParsingState &PFS, const yaml::StringValue &VarStr,
    const yaml::StringValue &ExprStr, const yaml::StringValue &LocStr) {
  MDNode *Var = nullptr;
  MDNode *Expr = nullptr;
  MDNode *Loc = nullptr;
  if (parseMDNode(PFS, Var, VarStr) || parseMDNode(PFS, Expr, ExprStr) ||
      parseMDNode(PFS, Loc, LocStr))
    return std::nullopt;
  DILocalVariable *DIVar = nullptr;
  DIExpression *DIExpr = nullptr;
  DILocation *DILoc = nullptr;
  if (typecheckMDNode(DIVar, Var, VarStr, "DILocalVariable", *this) ||
      typecheckMDNode(DIExpr, Expr, ExprStr, "DIExpression", *this) ||
      typecheckMDNode(DILoc, Loc, LocStr, "DILocation", *this))
    return std::nullopt;
  return VarExprLoc{DIVar, DIExpr, DILoc};
}

template <typename T>
bool MIRParserImpl::parseStackObjectsDebugInfo(PerFunctionMIParsingState &PFS,
                                               const T &Object, int FrameIdx) {
  std::optional<VarExprLoc> MaybeInfo =
      parseVarExprLoc(PFS, Object.DebugVar, Object.DebugExpr, Object.DebugLoc);
  if (!MaybeInfo)
    return true;
  // Debug information can only be attached to stack objects; Fixed stack
  // objects aren't supported.
  if (MaybeInfo->DIVar || MaybeInfo->DIExpr || MaybeInfo->DILoc)
    PFS.MF.setVariableDbgInfo(MaybeInfo->DIVar, MaybeInfo->DIExpr, FrameIdx,
                              MaybeInfo->DILoc);
  return false;
}

bool MIRParserImpl::parseMDNode(PerFunctionMIParsingState &PFS,
    MDNode *&Node, const yaml::StringValue &Source) {
  if (Source.Value.empty())
    return false;
  SMDiagnostic Error;
  if (llvm::parseMDNode(PFS, Node, Source.Value, Error))
    return error(Error, Source.SourceRange);
  return false;
}

bool MIRParserImpl::initializeConstantPool(PerFunctionMIParsingState &PFS,
    MachineConstantPool &ConstantPool, const yaml::MachineFunction &YamlMF) {
  DenseMap<unsigned, unsigned> &ConstantPoolSlots = PFS.ConstantPoolSlots;
  const MachineFunction &MF = PFS.MF;
  const auto &M = *MF.getFunction().getParent();
  SMDiagnostic Error;
  for (const auto &YamlConstant : YamlMF.Constants) {
    if (YamlConstant.IsTargetSpecific)
      // FIXME: Support target-specific constant pools
      return error(YamlConstant.Value.SourceRange.Start,
                   "Can't parse target-specific constant pool entries yet");
    const Constant *Value = dyn_cast_or_null<Constant>(
        parseConstantValue(YamlConstant.Value.Value, Error, M));
    if (!Value)
      return error(Error, YamlConstant.Value.SourceRange);
    const Align PrefTypeAlign =
        M.getDataLayout().getPrefTypeAlign(Value->getType());
    const Align Alignment = YamlConstant.Alignment.value_or(PrefTypeAlign);
    unsigned Index = ConstantPool.getConstantPoolIndex(Value, Alignment);
    if (!ConstantPoolSlots.insert(std::make_pair(YamlConstant.ID.Value, Index))
             .second)
      return error(YamlConstant.ID.SourceRange.Start,
                   Twine("redefinition of constant pool item '%const.") +
                       Twine(YamlConstant.ID.Value) + "'");
  }
  return false;
}

bool MIRParserImpl::initializeJumpTableInfo(PerFunctionMIParsingState &PFS,
    const yaml::MachineJumpTable &YamlJTI) {
  MachineJumpTableInfo *JTI = PFS.MF.getOrCreateJumpTableInfo(YamlJTI.Kind);
  for (const auto &Entry : YamlJTI.Entries) {
    std::vector<MachineBasicBlock *> Blocks;
    for (const auto &MBBSource : Entry.Blocks) {
      MachineBasicBlock *MBB = nullptr;
      if (parseMBBReference(PFS, MBB, MBBSource.Value))
        return true;
      Blocks.push_back(MBB);
    }
    unsigned Index = JTI->createJumpTableIndex(Blocks);
    if (!PFS.JumpTableSlots.insert(std::make_pair(Entry.ID.Value, Index))
             .second)
      return error(Entry.ID.SourceRange.Start,
                   Twine("redefinition of jump table entry '%jump-table.") +
                       Twine(Entry.ID.Value) + "'");
  }
  return false;
}

bool MIRParserImpl::parseMBBReference(PerFunctionMIParsingState &PFS,
                                      MachineBasicBlock *&MBB,
                                      const yaml::StringValue &Source) {
  SMDiagnostic Error;
  if (llvm::parseMBBReference(PFS, MBB, Source.Value, Error))
    return error(Error, Source.SourceRange);
  return false;
}

bool MIRParserImpl::parseMachineMetadata(PerFunctionMIParsingState &PFS,
                                         const yaml::StringValue &Source) {
  SMDiagnostic Error;
  if (llvm::parseMachineMetadata(PFS, Source.Value, Source.SourceRange, Error))
    return error(Error, Source.SourceRange);
  return false;
}

bool MIRParserImpl::parseMachineMetadataNodes(
    PerFunctionMIParsingState &PFS, MachineFunction &MF,
    const yaml::MachineFunction &YMF) {
  for (const auto &MDS : YMF.MachineMetadataNodes) {
    if (parseMachineMetadata(PFS, MDS))
      return true;
  }
  // Report missing definitions from forward referenced nodes.
  if (!PFS.MachineForwardRefMDNodes.empty())
    return error(PFS.MachineForwardRefMDNodes.begin()->second.second,
                 "use of undefined metadata '!" +
                     Twine(PFS.MachineForwardRefMDNodes.begin()->first) + "'");
  return false;
}

SMDiagnostic MIRParserImpl::diagFromMIStringDiag(const SMDiagnostic &Error,
                                                 SMRange SourceRange) {
  assert(SourceRange.isValid() && "Invalid source range");
  SMLoc Loc = SourceRange.Start;
  bool HasQuote = Loc.getPointer() < SourceRange.End.getPointer() &&
                  *Loc.getPointer() == '\'';
  // Translate the location of the error from the location in the MI string to
  // the corresponding location in the MIR file.
  Loc = Loc.getFromPointer(Loc.getPointer() + Error.getColumnNo() +
                           (HasQuote ? 1 : 0));

  // TODO: Translate any source ranges as well.
  return SM.GetMessage(Loc, Error.getKind(), Error.getMessage(), std::nullopt,
                       Error.getFixIts());
}

SMDiagnostic MIRParserImpl::diagFromBlockStringDiag(const SMDiagnostic &Error,
                                                    SMRange SourceRange) {
  assert(SourceRange.isValid());

  // Translate the location of the error from the location in the llvm IR string
  // to the corresponding location in the MIR file.
  auto LineAndColumn = SM.getLineAndColumn(SourceRange.Start);
  unsigned Line = LineAndColumn.first + Error.getLineNo() - 1;
  unsigned Column = Error.getColumnNo();
  StringRef LineStr = Error.getLineContents();
  SMLoc Loc = Error.getLoc();

  // Get the full line and adjust the column number by taking the indentation of
  // LLVM IR into account.
  for (line_iterator L(*SM.getMemoryBuffer(SM.getMainFileID()), false), E;
       L != E; ++L) {
    if (L.line_number() == Line) {
      LineStr = *L;
      Loc = SMLoc::getFromPointer(LineStr.data());
      auto Indent = LineStr.find(Error.getLineContents());
      if (Indent != StringRef::npos)
        Column += Indent;
      break;
    }
  }

  return SMDiagnostic(SM, Loc, Filename, Line, Column, Error.getKind(),
                      Error.getMessage(), LineStr, Error.getRanges(),
                      Error.getFixIts());
}

MIRParser::MIRParser(std::unique_ptr<MIRParserImpl> Impl)
    : Impl(std::move(Impl)) {}

MIRParser::~MIRParser() = default;

std::unique_ptr<Module>
MIRParser::parseIRModule(DataLayoutCallbackTy DataLayoutCallback) {
  return Impl->parseIRModule(DataLayoutCallback);
}

bool MIRParser::parseMachineFunctions(Module &M, MachineModuleInfo &MMI) {
  return Impl->parseMachineFunctions(M, MMI);
}

bool MIRParser::parseMachineFunctions(Module &M, ModuleAnalysisManager &MAM) {
  auto &MMI = MAM.getResult<MachineModuleAnalysis>(M).getMMI();
  return Impl->parseMachineFunctions(M, MMI, &MAM);
}

std::unique_ptr<MIRParser> llvm::createMIRParserFromFile(
    StringRef Filename, SMDiagnostic &Error, LLVMContext &Context,
    std::function<void(Function &)> ProcessIRFunction) {
  auto FileOrErr = MemoryBuffer::getFileOrSTDIN(Filename, /*IsText=*/true);
  if (std::error_code EC = FileOrErr.getError()) {
    Error = SMDiagnostic(Filename, SourceMgr::DK_Error,
                         "Could not open input file: " + EC.message());
    return nullptr;
  }
  return createMIRParser(std::move(FileOrErr.get()), Context,
                         ProcessIRFunction);
}

std::unique_ptr<MIRParser>
llvm::createMIRParser(std::unique_ptr<MemoryBuffer> Contents,
                      LLVMContext &Context,
                      std::function<void(Function &)> ProcessIRFunction) {
  auto Filename = Contents->getBufferIdentifier();
  if (Context.shouldDiscardValueNames()) {
    Context.diagnose(DiagnosticInfoMIRParser(
        DS_Error,
        SMDiagnostic(
            Filename, SourceMgr::DK_Error,
            "Can't read MIR with a Context that discards named Values")));
    return nullptr;
  }
  return std::make_unique<MIRParser>(std::make_unique<MIRParserImpl>(
      std::move(Contents), Filename, Context, ProcessIRFunction));
}
