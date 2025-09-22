//===-- Assembler.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Assembler.h"

#include "SnippetRepetitor.h"
#include "SubprocessMemory.h"
#include "Target.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#ifdef HAVE_LIBPFM
#include "perfmon/perf_event.h"
#endif // HAVE_LIBPFM

#ifdef __linux__
#include <unistd.h>
#endif

namespace llvm {
namespace exegesis {

static constexpr const char ModuleID[] = "ExegesisInfoTest";
static constexpr const char FunctionID[] = "foo";
static const Align kFunctionAlignment(4096);

// Fills the given basic block with register setup code, and returns true if
// all registers could be setup correctly.
static bool generateSnippetSetupCode(const ExegesisTarget &ET,
                                     const MCSubtargetInfo *const MSI,
                                     BasicBlockFiller &BBF,
                                     const BenchmarkKey &Key,
                                     bool GenerateMemoryInstructions) {
  bool IsSnippetSetupComplete = true;
  if (GenerateMemoryInstructions) {
    BBF.addInstructions(ET.generateMemoryInitialSetup());
    for (const MemoryMapping &MM : Key.MemoryMappings) {
#ifdef __linux__
      // The frontend that generates that parses the memory mapping information
      // from the user should validate that the requested address is a multiple
      // of the page size. Assert that this is true here.
      assert(MM.Address % getpagesize() == 0 &&
             "Memory mappings need to be aligned to page boundaries.");
#endif
      BBF.addInstructions(ET.generateMmap(
          MM.Address, Key.MemoryValues.at(MM.MemoryValueName).SizeBytes,
          ET.getAuxiliaryMemoryStartAddress() +
              sizeof(int) * (Key.MemoryValues.at(MM.MemoryValueName).Index +
                             SubprocessMemory::AuxiliaryMemoryOffset)));
    }
    BBF.addInstructions(ET.setStackRegisterToAuxMem());
  }
  Register StackPointerRegister = BBF.MF.getSubtarget()
                                      .getTargetLowering()
                                      ->getStackPointerRegisterToSaveRestore();
  for (const RegisterValue &RV : Key.RegisterInitialValues) {
    if (GenerateMemoryInstructions) {
      // If we're generating memory instructions, don't load in the value for
      // the register with the stack pointer as it will be used later to finish
      // the setup.
      if (RV.Register == StackPointerRegister)
        continue;
    }
    // Load a constant in the register.
    const auto SetRegisterCode = ET.setRegTo(*MSI, RV.Register, RV.Value);
    if (SetRegisterCode.empty())
      IsSnippetSetupComplete = false;
    BBF.addInstructions(SetRegisterCode);
  }
  if (GenerateMemoryInstructions) {
#ifdef HAVE_LIBPFM
    BBF.addInstructions(ET.configurePerfCounter(PERF_EVENT_IOC_RESET, true));
#endif // HAVE_LIBPFM
    for (const RegisterValue &RV : Key.RegisterInitialValues) {
      // Load in the stack register now as we're done using it elsewhere
      // and need to set the value in preparation for executing the
      // snippet.
      if (RV.Register != StackPointerRegister)
        continue;
      const auto SetRegisterCode = ET.setRegTo(*MSI, RV.Register, RV.Value);
      if (SetRegisterCode.empty())
        IsSnippetSetupComplete = false;
      BBF.addInstructions(SetRegisterCode);
      break;
    }
  }
  return IsSnippetSetupComplete;
}

// Small utility function to add named passes.
static bool addPass(PassManagerBase &PM, StringRef PassName,
                    TargetPassConfig &TPC) {
  const PassRegistry *PR = PassRegistry::getPassRegistry();
  const PassInfo *PI = PR->getPassInfo(PassName);
  if (!PI) {
    errs() << " run-pass " << PassName << " is not registered.\n";
    return true;
  }

  if (!PI->getNormalCtor()) {
    errs() << " cannot create pass: " << PI->getPassName() << "\n";
    return true;
  }
  Pass *P = PI->getNormalCtor()();
  std::string Banner = std::string("After ") + std::string(P->getPassName());
  PM.add(P);
  TPC.printAndVerify(Banner);

  return false;
}

MachineFunction &createVoidVoidPtrMachineFunction(StringRef FunctionName,
                                                  Module *Module,
                                                  MachineModuleInfo *MMI) {
  Type *const ReturnType = Type::getInt32Ty(Module->getContext());
  Type *const MemParamType = PointerType::get(
      Type::getInt8Ty(Module->getContext()), 0 /*default address space*/);
  FunctionType *FunctionType =
      FunctionType::get(ReturnType, {MemParamType}, false);
  Function *const F = Function::Create(
      FunctionType, GlobalValue::ExternalLinkage, FunctionName, Module);
  BasicBlock *BB = BasicBlock::Create(Module->getContext(), "", F);
  new UnreachableInst(Module->getContext(), BB);
  return MMI->getOrCreateMachineFunction(*F);
}

BasicBlockFiller::BasicBlockFiller(MachineFunction &MF, MachineBasicBlock *MBB,
                                   const MCInstrInfo *MCII)
    : MF(MF), MBB(MBB), MCII(MCII) {}

void BasicBlockFiller::addInstruction(const MCInst &Inst, const DebugLoc &DL) {
  const unsigned Opcode = Inst.getOpcode();
  const MCInstrDesc &MCID = MCII->get(Opcode);
  MachineInstrBuilder Builder = BuildMI(MBB, DL, MCID);
  for (unsigned OpIndex = 0, E = Inst.getNumOperands(); OpIndex < E;
       ++OpIndex) {
    const MCOperand &Op = Inst.getOperand(OpIndex);
    if (Op.isReg()) {
      const bool IsDef = OpIndex < MCID.getNumDefs();
      unsigned Flags = 0;
      const MCOperandInfo &OpInfo = MCID.operands().begin()[OpIndex];
      if (IsDef && !OpInfo.isOptionalDef())
        Flags |= RegState::Define;
      Builder.addReg(Op.getReg(), Flags);
    } else if (Op.isImm()) {
      Builder.addImm(Op.getImm());
    } else if (!Op.isValid()) {
      llvm_unreachable("Operand is not set");
    } else {
      llvm_unreachable("Not yet implemented");
    }
  }
}

void BasicBlockFiller::addInstructions(ArrayRef<MCInst> Insts,
                                       const DebugLoc &DL) {
  for (const MCInst &Inst : Insts)
    addInstruction(Inst, DL);
}

void BasicBlockFiller::addReturn(const ExegesisTarget &ET,
                                 bool SubprocessCleanup, const DebugLoc &DL) {
  // Insert cleanup code
  if (SubprocessCleanup) {
#ifdef HAVE_LIBPFM
    addInstructions(ET.configurePerfCounter(PERF_EVENT_IOC_DISABLE, false));
#endif // HAVE_LIBPFM
#ifdef __linux__
    addInstructions(ET.generateExitSyscall(0));
#endif // __linux__
  }
  // Insert the return code.
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  if (TII->getReturnOpcode() < TII->getNumOpcodes()) {
    BuildMI(MBB, DL, TII->get(TII->getReturnOpcode()));
  } else {
    MachineIRBuilder MIB(MF);
    MIB.setMBB(*MBB);

    FunctionLoweringInfo FuncInfo;
    FuncInfo.CanLowerReturn = true;
    MF.getSubtarget().getCallLowering()->lowerReturn(MIB, nullptr, {}, FuncInfo,
                                                     0);
  }
}

FunctionFiller::FunctionFiller(MachineFunction &MF,
                               std::vector<unsigned> RegistersSetUp)
    : MF(MF), MCII(MF.getTarget().getMCInstrInfo()), Entry(addBasicBlock()),
      RegistersSetUp(std::move(RegistersSetUp)) {}

BasicBlockFiller FunctionFiller::addBasicBlock() {
  MachineBasicBlock *MBB = MF.CreateMachineBasicBlock();
  MF.push_back(MBB);
  return BasicBlockFiller(MF, MBB, MCII);
}

ArrayRef<unsigned> FunctionFiller::getRegistersSetUp() const {
  return RegistersSetUp;
}

static std::unique_ptr<Module>
createModule(const std::unique_ptr<LLVMContext> &Context, const DataLayout &DL) {
  auto Mod = std::make_unique<Module>(ModuleID, *Context);
  Mod->setDataLayout(DL);
  return Mod;
}

BitVector getFunctionReservedRegs(const TargetMachine &TM) {
  std::unique_ptr<LLVMContext> Context = std::make_unique<LLVMContext>();
  std::unique_ptr<Module> Module = createModule(Context, TM.createDataLayout());
  // TODO: This only works for targets implementing LLVMTargetMachine.
  const LLVMTargetMachine &LLVMTM = static_cast<const LLVMTargetMachine &>(TM);
  auto MMIWP = std::make_unique<MachineModuleInfoWrapperPass>(&LLVMTM);
  MachineFunction &MF = createVoidVoidPtrMachineFunction(
      FunctionID, Module.get(), &MMIWP->getMMI());
  // Saving reserved registers for client.
  return MF.getSubtarget().getRegisterInfo()->getReservedRegs(MF);
}

Error assembleToStream(const ExegesisTarget &ET,
                       std::unique_ptr<LLVMTargetMachine> TM,
                       ArrayRef<unsigned> LiveIns, const FillFunction &Fill,
                       raw_pwrite_stream &AsmStream, const BenchmarkKey &Key,
                       bool GenerateMemoryInstructions) {
  auto Context = std::make_unique<LLVMContext>();
  std::unique_ptr<Module> Module =
      createModule(Context, TM->createDataLayout());
  auto MMIWP = std::make_unique<MachineModuleInfoWrapperPass>(TM.get());
  MachineFunction &MF = createVoidVoidPtrMachineFunction(
      FunctionID, Module.get(), &MMIWP.get()->getMMI());
  MF.ensureAlignment(kFunctionAlignment);

  // We need to instruct the passes that we're done with SSA and virtual
  // registers.
  auto &Properties = MF.getProperties();
  Properties.set(MachineFunctionProperties::Property::NoVRegs);
  Properties.reset(MachineFunctionProperties::Property::IsSSA);
  Properties.set(MachineFunctionProperties::Property::NoPHIs);

  for (const unsigned Reg : LiveIns)
    MF.getRegInfo().addLiveIn(Reg);

  if (GenerateMemoryInstructions) {
    for (const unsigned Reg : ET.getArgumentRegisters())
      MF.getRegInfo().addLiveIn(Reg);
    // Add a live in for registers that need saving so that the machine verifier
    // doesn't fail if the register is never defined.
    for (const unsigned Reg : ET.getRegistersNeedSaving())
      MF.getRegInfo().addLiveIn(Reg);
  }

  std::vector<unsigned> RegistersSetUp;
  for (const auto &InitValue : Key.RegisterInitialValues) {
    RegistersSetUp.push_back(InitValue.Register);
  }
  FunctionFiller Sink(MF, std::move(RegistersSetUp));
  auto Entry = Sink.getEntry();

  for (const unsigned Reg : LiveIns)
    Entry.MBB->addLiveIn(Reg);

  if (GenerateMemoryInstructions) {
    for (const unsigned Reg : ET.getArgumentRegisters())
      Entry.MBB->addLiveIn(Reg);
    // Add a live in for registers that need saving so that the machine verifier
    // doesn't fail if the register is never defined.
    for (const unsigned Reg : ET.getRegistersNeedSaving())
      Entry.MBB->addLiveIn(Reg);
  }

  const bool IsSnippetSetupComplete = generateSnippetSetupCode(
      ET, TM->getMCSubtargetInfo(), Entry, Key, GenerateMemoryInstructions);

  // If the snippet setup is not complete, we disable liveliness tracking. This
  // means that we won't know what values are in the registers.
  // FIXME: this should probably be an assertion.
  if (!IsSnippetSetupComplete)
    Properties.reset(MachineFunctionProperties::Property::TracksLiveness);

  Fill(Sink);

  // prologue/epilogue pass needs the reserved registers to be frozen, this
  // is usually done by the SelectionDAGISel pass.
  MF.getRegInfo().freezeReservedRegs();

  // We create the pass manager, run the passes to populate AsmBuffer.
  MCContext &MCContext = MMIWP->getMMI().getContext();
  legacy::PassManager PM;

  TargetLibraryInfoImpl TLII(Triple(Module->getTargetTriple()));
  PM.add(new TargetLibraryInfoWrapperPass(TLII));

  TargetPassConfig *TPC = TM->createPassConfig(PM);
  PM.add(TPC);
  PM.add(MMIWP.release());
  TPC->printAndVerify("MachineFunctionGenerator::assemble");
  // Add target-specific passes.
  ET.addTargetSpecificPasses(PM);
  TPC->printAndVerify("After ExegesisTarget::addTargetSpecificPasses");
  // Adding the following passes:
  // - postrapseudos: expands pseudo return instructions used on some targets.
  // - machineverifier: checks that the MachineFunction is well formed.
  // - prologepilog: saves and restore callee saved registers.
  for (const char *PassName :
       {"postrapseudos", "machineverifier", "prologepilog"})
    if (addPass(PM, PassName, *TPC))
      return make_error<Failure>("Unable to add a mandatory pass");
  TPC->setInitialized();

  // AsmPrinter is responsible for generating the assembly into AsmBuffer.
  if (TM->addAsmPrinter(PM, AsmStream, nullptr, CodeGenFileType::ObjectFile,
                        MCContext))
    return make_error<Failure>("Cannot add AsmPrinter passes");

  PM.run(*Module); // Run all the passes
  return Error::success();
}

object::OwningBinary<object::ObjectFile>
getObjectFromBuffer(StringRef InputData) {
  // Storing the generated assembly into a MemoryBuffer that owns the memory.
  std::unique_ptr<MemoryBuffer> Buffer =
      MemoryBuffer::getMemBufferCopy(InputData);
  // Create the ObjectFile from the MemoryBuffer.
  std::unique_ptr<object::ObjectFile> Obj =
      cantFail(object::ObjectFile::createObjectFile(Buffer->getMemBufferRef()));
  // Returning both the MemoryBuffer and the ObjectFile.
  return object::OwningBinary<object::ObjectFile>(std::move(Obj),
                                                  std::move(Buffer));
}

object::OwningBinary<object::ObjectFile> getObjectFromFile(StringRef Filename) {
  return cantFail(object::ObjectFile::createObjectFile(Filename));
}

Expected<ExecutableFunction> ExecutableFunction::create(
    std::unique_ptr<LLVMTargetMachine> TM,
    object::OwningBinary<object::ObjectFile> &&ObjectFileHolder) {
  assert(ObjectFileHolder.getBinary() && "cannot create object file");
  std::unique_ptr<LLVMContext> Ctx = std::make_unique<LLVMContext>();

  auto SymbolSizes = object::computeSymbolSizes(*ObjectFileHolder.getBinary());
  // Get the size of the function that we want to call into (with the name of
  // FunctionID).
  auto SymbolIt = find_if(SymbolSizes, [&](const auto &Pair) {
    auto SymbolName = Pair.first.getName();
    if (SymbolName)
      return *SymbolName == FunctionID;
    // We should always succeed in finding the FunctionID, hence we suppress
    // the error here and assert later on the search result, rather than
    // propagating the Expected<> error back to the caller.
    consumeError(SymbolName.takeError());
    return false;
  });
  assert(SymbolIt != SymbolSizes.end() &&
         "Cannot find the symbol for FunctionID");
  uintptr_t CodeSize = SymbolIt->second;

  auto EJITOrErr = orc::LLJITBuilder().create();
  if (!EJITOrErr)
    return EJITOrErr.takeError();

  auto EJIT = std::move(*EJITOrErr);

  if (auto ObjErr =
          EJIT->addObjectFile(std::get<1>(ObjectFileHolder.takeBinary())))
    return std::move(ObjErr);

  auto FunctionAddressOrErr = EJIT->lookup(FunctionID);
  if (!FunctionAddressOrErr)
    return FunctionAddressOrErr.takeError();

  const uint64_t FunctionAddress = FunctionAddressOrErr->getValue();

  assert(isAligned(kFunctionAlignment, FunctionAddress) &&
         "function is not properly aligned");

  StringRef FBytes =
      StringRef(reinterpret_cast<const char *>(FunctionAddress), CodeSize);
  return ExecutableFunction(std::move(Ctx), std::move(EJIT), FBytes);
}

ExecutableFunction::ExecutableFunction(std::unique_ptr<LLVMContext> Ctx,
                                       std::unique_ptr<orc::LLJIT> EJIT,
                                       StringRef FB)
    : FunctionBytes(FB), Context(std::move(Ctx)), ExecJIT(std::move(EJIT)) {}

Error getBenchmarkFunctionBytes(const StringRef InputData,
                                std::vector<uint8_t> &Bytes) {
  const auto Holder = getObjectFromBuffer(InputData);
  const auto *Obj = Holder.getBinary();
  // See RuntimeDyldImpl::loadObjectImpl(Obj) for much more complete
  // implementation.

  // Find the only function in the object file.
  SmallVector<object::SymbolRef, 1> Functions;
  for (auto &Sym : Obj->symbols()) {
    auto SymType = Sym.getType();
    if (SymType && *SymType == object::SymbolRef::Type::ST_Function)
      Functions.push_back(Sym);
  }
  if (Functions.size() != 1)
    return make_error<Failure>("Exactly one function expected");

  // Find the containing section - it is assumed to contain only this function.
  auto SectionOrErr = Functions.front().getSection();
  if (!SectionOrErr || *SectionOrErr == Obj->section_end())
    return make_error<Failure>("Section not found");

  auto Address = Functions.front().getAddress();
  if (!Address || *Address != SectionOrErr.get()->getAddress())
    return make_error<Failure>("Unexpected layout");

  auto ContentsOrErr = SectionOrErr.get()->getContents();
  if (!ContentsOrErr)
    return ContentsOrErr.takeError();
  Bytes.assign(ContentsOrErr->begin(), ContentsOrErr->end());
  return Error::success();
}

} // namespace exegesis
} // namespace llvm
