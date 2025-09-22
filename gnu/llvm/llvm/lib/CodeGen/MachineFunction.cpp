//===- MachineFunction.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Collect native machine code information for a function.  This allows
// target-specific information about the generated code to be stored with each
// function.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/ProfileSummaryInfo.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WasmEHFuncInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "LiveDebugValues/LiveDebugValues.h"

using namespace llvm;

#define DEBUG_TYPE "codegen"

static cl::opt<unsigned> AlignAllFunctions(
    "align-all-functions",
    cl::desc("Force the alignment of all functions in log2 format (e.g. 4 "
             "means align on 16B boundaries)."),
    cl::init(0), cl::Hidden);

static const char *getPropertyName(MachineFunctionProperties::Property Prop) {
  using P = MachineFunctionProperties::Property;

  // clang-format off
  switch(Prop) {
  case P::FailedISel: return "FailedISel";
  case P::IsSSA: return "IsSSA";
  case P::Legalized: return "Legalized";
  case P::NoPHIs: return "NoPHIs";
  case P::NoVRegs: return "NoVRegs";
  case P::RegBankSelected: return "RegBankSelected";
  case P::Selected: return "Selected";
  case P::TracksLiveness: return "TracksLiveness";
  case P::TiedOpsRewritten: return "TiedOpsRewritten";
  case P::FailsVerification: return "FailsVerification";
  case P::TracksDebugUserValues: return "TracksDebugUserValues";
  }
  // clang-format on
  llvm_unreachable("Invalid machine function property");
}

void setUnsafeStackSize(const Function &F, MachineFrameInfo &FrameInfo) {
  if (!F.hasFnAttribute(Attribute::SafeStack))
    return;

  auto *Existing =
      dyn_cast_or_null<MDTuple>(F.getMetadata(LLVMContext::MD_annotation));

  if (!Existing || Existing->getNumOperands() != 2)
    return;

  auto *MetadataName = "unsafe-stack-size";
  if (auto &N = Existing->getOperand(0)) {
    if (N.equalsStr(MetadataName)) {
      if (auto &Op = Existing->getOperand(1)) {
        auto Val = mdconst::extract<ConstantInt>(Op)->getZExtValue();
        FrameInfo.setUnsafeStackSize(Val);
      }
    }
  }
}

// Pin the vtable to this file.
void MachineFunction::Delegate::anchor() {}

void MachineFunctionProperties::print(raw_ostream &OS) const {
  const char *Separator = "";
  for (BitVector::size_type I = 0; I < Properties.size(); ++I) {
    if (!Properties[I])
      continue;
    OS << Separator << getPropertyName(static_cast<Property>(I));
    Separator = ", ";
  }
}

//===----------------------------------------------------------------------===//
// MachineFunction implementation
//===----------------------------------------------------------------------===//

// Out-of-line virtual method.
MachineFunctionInfo::~MachineFunctionInfo() = default;

void ilist_alloc_traits<MachineBasicBlock>::deleteNode(MachineBasicBlock *MBB) {
  MBB->getParent()->deleteMachineBasicBlock(MBB);
}

static inline Align getFnStackAlignment(const TargetSubtargetInfo *STI,
                                           const Function &F) {
  if (auto MA = F.getFnStackAlign())
    return *MA;
  return STI->getFrameLowering()->getStackAlign();
}

MachineFunction::MachineFunction(Function &F, const LLVMTargetMachine &Target,
                                 const TargetSubtargetInfo &STI,
                                 unsigned FunctionNum, MachineModuleInfo &mmi)
    : F(F), Target(Target), STI(&STI), Ctx(mmi.getContext()), MMI(mmi) {
  FunctionNumber = FunctionNum;
  init();
}

void MachineFunction::handleInsertion(MachineInstr &MI) {
  if (TheDelegate)
    TheDelegate->MF_HandleInsertion(MI);
}

void MachineFunction::handleRemoval(MachineInstr &MI) {
  if (TheDelegate)
    TheDelegate->MF_HandleRemoval(MI);
}

void MachineFunction::handleChangeDesc(MachineInstr &MI,
                                       const MCInstrDesc &TID) {
  if (TheDelegate)
    TheDelegate->MF_HandleChangeDesc(MI, TID);
}

void MachineFunction::init() {
  // Assume the function starts in SSA form with correct liveness.
  Properties.set(MachineFunctionProperties::Property::IsSSA);
  Properties.set(MachineFunctionProperties::Property::TracksLiveness);
  if (STI->getRegisterInfo())
    RegInfo = new (Allocator) MachineRegisterInfo(this);
  else
    RegInfo = nullptr;

  MFInfo = nullptr;

  // We can realign the stack if the target supports it and the user hasn't
  // explicitly asked us not to.
  bool CanRealignSP = STI->getFrameLowering()->isStackRealignable() &&
                      !F.hasFnAttribute("no-realign-stack");
  bool ForceRealignSP = F.hasFnAttribute(Attribute::StackAlignment) ||
                        F.hasFnAttribute("stackrealign");
  FrameInfo = new (Allocator) MachineFrameInfo(
      getFnStackAlignment(STI, F), /*StackRealignable=*/CanRealignSP,
      /*ForcedRealign=*/ForceRealignSP && CanRealignSP);

  setUnsafeStackSize(F, *FrameInfo);

  if (F.hasFnAttribute(Attribute::StackAlignment))
    FrameInfo->ensureMaxAlignment(*F.getFnStackAlign());

  ConstantPool = new (Allocator) MachineConstantPool(getDataLayout());
  Alignment = STI->getTargetLowering()->getMinFunctionAlignment();

  // FIXME: Shouldn't use pref alignment if explicit alignment is set on F.
  // FIXME: Use Function::hasOptSize().
  if (!F.hasFnAttribute(Attribute::OptimizeForSize))
    Alignment = std::max(Alignment,
                         STI->getTargetLowering()->getPrefFunctionAlignment());

  // -fsanitize=function and -fsanitize=kcfi instrument indirect function calls
  // to load a type hash before the function label. Ensure functions are aligned
  // by a least 4 to avoid unaligned access, which is especially important for
  // -mno-unaligned-access.
  if (F.hasMetadata(LLVMContext::MD_func_sanitize) ||
      F.getMetadata(LLVMContext::MD_kcfi_type))
    Alignment = std::max(Alignment, Align(4));

  if (AlignAllFunctions)
    Alignment = Align(1ULL << AlignAllFunctions);

  JumpTableInfo = nullptr;

  if (isFuncletEHPersonality(classifyEHPersonality(
          F.hasPersonalityFn() ? F.getPersonalityFn() : nullptr))) {
    WinEHInfo = new (Allocator) WinEHFuncInfo();
  }

  if (isScopedEHPersonality(classifyEHPersonality(
          F.hasPersonalityFn() ? F.getPersonalityFn() : nullptr))) {
    WasmEHInfo = new (Allocator) WasmEHFuncInfo();
  }

  assert(Target.isCompatibleDataLayout(getDataLayout()) &&
         "Can't create a MachineFunction using a Module with a "
         "Target-incompatible DataLayout attached\n");

  PSVManager = std::make_unique<PseudoSourceValueManager>(getTarget());
}

void MachineFunction::initTargetMachineFunctionInfo(
    const TargetSubtargetInfo &STI) {
  assert(!MFInfo && "MachineFunctionInfo already set");
  MFInfo = Target.createMachineFunctionInfo(Allocator, F, &STI);
}

MachineFunction::~MachineFunction() {
  clear();
}

void MachineFunction::clear() {
  Properties.reset();
  // Don't call destructors on MachineInstr and MachineOperand. All of their
  // memory comes from the BumpPtrAllocator which is about to be purged.
  //
  // Do call MachineBasicBlock destructors, it contains std::vectors.
  for (iterator I = begin(), E = end(); I != E; I = BasicBlocks.erase(I))
    I->Insts.clearAndLeakNodesUnsafely();
  MBBNumbering.clear();

  InstructionRecycler.clear(Allocator);
  OperandRecycler.clear(Allocator);
  BasicBlockRecycler.clear(Allocator);
  CodeViewAnnotations.clear();
  VariableDbgInfos.clear();
  if (RegInfo) {
    RegInfo->~MachineRegisterInfo();
    Allocator.Deallocate(RegInfo);
  }
  if (MFInfo) {
    MFInfo->~MachineFunctionInfo();
    Allocator.Deallocate(MFInfo);
  }

  FrameInfo->~MachineFrameInfo();
  Allocator.Deallocate(FrameInfo);

  ConstantPool->~MachineConstantPool();
  Allocator.Deallocate(ConstantPool);

  if (JumpTableInfo) {
    JumpTableInfo->~MachineJumpTableInfo();
    Allocator.Deallocate(JumpTableInfo);
  }

  if (WinEHInfo) {
    WinEHInfo->~WinEHFuncInfo();
    Allocator.Deallocate(WinEHInfo);
  }

  if (WasmEHInfo) {
    WasmEHInfo->~WasmEHFuncInfo();
    Allocator.Deallocate(WasmEHInfo);
  }
}

const DataLayout &MachineFunction::getDataLayout() const {
  return F.getDataLayout();
}

/// Get the JumpTableInfo for this function.
/// If it does not already exist, allocate one.
MachineJumpTableInfo *MachineFunction::
getOrCreateJumpTableInfo(unsigned EntryKind) {
  if (JumpTableInfo) return JumpTableInfo;

  JumpTableInfo = new (Allocator)
    MachineJumpTableInfo((MachineJumpTableInfo::JTEntryKind)EntryKind);
  return JumpTableInfo;
}

DenormalMode MachineFunction::getDenormalMode(const fltSemantics &FPType) const {
  return F.getDenormalMode(FPType);
}

/// Should we be emitting segmented stack stuff for the function
bool MachineFunction::shouldSplitStack() const {
  return getFunction().hasFnAttribute("split-stack");
}

[[nodiscard]] unsigned
MachineFunction::addFrameInst(const MCCFIInstruction &Inst) {
  FrameInstructions.push_back(Inst);
  return FrameInstructions.size() - 1;
}

/// This discards all of the MachineBasicBlock numbers and recomputes them.
/// This guarantees that the MBB numbers are sequential, dense, and match the
/// ordering of the blocks within the function.  If a specific MachineBasicBlock
/// is specified, only that block and those after it are renumbered.
void MachineFunction::RenumberBlocks(MachineBasicBlock *MBB) {
  if (empty()) { MBBNumbering.clear(); return; }
  MachineFunction::iterator MBBI, E = end();
  if (MBB == nullptr)
    MBBI = begin();
  else
    MBBI = MBB->getIterator();

  // Figure out the block number this should have.
  unsigned BlockNo = 0;
  if (MBBI != begin())
    BlockNo = std::prev(MBBI)->getNumber() + 1;

  for (; MBBI != E; ++MBBI, ++BlockNo) {
    if (MBBI->getNumber() != (int)BlockNo) {
      // Remove use of the old number.
      if (MBBI->getNumber() != -1) {
        assert(MBBNumbering[MBBI->getNumber()] == &*MBBI &&
               "MBB number mismatch!");
        MBBNumbering[MBBI->getNumber()] = nullptr;
      }

      // If BlockNo is already taken, set that block's number to -1.
      if (MBBNumbering[BlockNo])
        MBBNumbering[BlockNo]->setNumber(-1);

      MBBNumbering[BlockNo] = &*MBBI;
      MBBI->setNumber(BlockNo);
    }
  }

  // Okay, all the blocks are renumbered.  If we have compactified the block
  // numbering, shrink MBBNumbering now.
  assert(BlockNo <= MBBNumbering.size() && "Mismatch!");
  MBBNumbering.resize(BlockNo);
}

/// This method iterates over the basic blocks and assigns their IsBeginSection
/// and IsEndSection fields. This must be called after MBB layout is finalized
/// and the SectionID's are assigned to MBBs.
void MachineFunction::assignBeginEndSections() {
  front().setIsBeginSection();
  auto CurrentSectionID = front().getSectionID();
  for (auto MBBI = std::next(begin()), E = end(); MBBI != E; ++MBBI) {
    if (MBBI->getSectionID() == CurrentSectionID)
      continue;
    MBBI->setIsBeginSection();
    std::prev(MBBI)->setIsEndSection();
    CurrentSectionID = MBBI->getSectionID();
  }
  back().setIsEndSection();
}

/// Allocate a new MachineInstr. Use this instead of `new MachineInstr'.
MachineInstr *MachineFunction::CreateMachineInstr(const MCInstrDesc &MCID,
                                                  DebugLoc DL,
                                                  bool NoImplicit) {
  return new (InstructionRecycler.Allocate<MachineInstr>(Allocator))
      MachineInstr(*this, MCID, std::move(DL), NoImplicit);
}

/// Create a new MachineInstr which is a copy of the 'Orig' instruction,
/// identical in all ways except the instruction has no parent, prev, or next.
MachineInstr *
MachineFunction::CloneMachineInstr(const MachineInstr *Orig) {
  return new (InstructionRecycler.Allocate<MachineInstr>(Allocator))
             MachineInstr(*this, *Orig);
}

MachineInstr &MachineFunction::cloneMachineInstrBundle(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    const MachineInstr &Orig) {
  MachineInstr *FirstClone = nullptr;
  MachineBasicBlock::const_instr_iterator I = Orig.getIterator();
  while (true) {
    MachineInstr *Cloned = CloneMachineInstr(&*I);
    MBB.insert(InsertBefore, Cloned);
    if (FirstClone == nullptr) {
      FirstClone = Cloned;
    } else {
      Cloned->bundleWithPred();
    }

    if (!I->isBundledWithSucc())
      break;
    ++I;
  }
  // Copy over call site info to the cloned instruction if needed. If Orig is in
  // a bundle, copyCallSiteInfo takes care of finding the call instruction in
  // the bundle.
  if (Orig.shouldUpdateCallSiteInfo())
    copyCallSiteInfo(&Orig, FirstClone);
  return *FirstClone;
}

/// Delete the given MachineInstr.
///
/// This function also serves as the MachineInstr destructor - the real
/// ~MachineInstr() destructor must be empty.
void MachineFunction::deleteMachineInstr(MachineInstr *MI) {
  // Verify that a call site info is at valid state. This assertion should
  // be triggered during the implementation of support for the
  // call site info of a new architecture. If the assertion is triggered,
  // back trace will tell where to insert a call to updateCallSiteInfo().
  assert((!MI->isCandidateForCallSiteEntry() || !CallSitesInfo.contains(MI)) &&
         "Call site info was not updated!");
  // Strip it for parts. The operand array and the MI object itself are
  // independently recyclable.
  if (MI->Operands)
    deallocateOperandArray(MI->CapOperands, MI->Operands);
  // Don't call ~MachineInstr() which must be trivial anyway because
  // ~MachineFunction drops whole lists of MachineInstrs wihout calling their
  // destructors.
  InstructionRecycler.Deallocate(Allocator, MI);
}

/// Allocate a new MachineBasicBlock. Use this instead of
/// `new MachineBasicBlock'.
MachineBasicBlock *
MachineFunction::CreateMachineBasicBlock(const BasicBlock *BB,
                                         std::optional<UniqueBBID> BBID) {
  MachineBasicBlock *MBB =
      new (BasicBlockRecycler.Allocate<MachineBasicBlock>(Allocator))
          MachineBasicBlock(*this, BB);
  // Set BBID for `-basic-block=sections=labels` and
  // `-basic-block-sections=list` to allow robust mapping of profiles to basic
  // blocks.
  if (Target.getBBSectionsType() == BasicBlockSection::Labels ||
      Target.Options.BBAddrMap ||
      Target.getBBSectionsType() == BasicBlockSection::List)
    MBB->setBBID(BBID.has_value() ? *BBID : UniqueBBID{NextBBID++, 0});
  return MBB;
}

/// Delete the given MachineBasicBlock.
void MachineFunction::deleteMachineBasicBlock(MachineBasicBlock *MBB) {
  assert(MBB->getParent() == this && "MBB parent mismatch!");
  // Clean up any references to MBB in jump tables before deleting it.
  if (JumpTableInfo)
    JumpTableInfo->RemoveMBBFromJumpTables(MBB);
  MBB->~MachineBasicBlock();
  BasicBlockRecycler.Deallocate(Allocator, MBB);
}

MachineMemOperand *MachineFunction::getMachineMemOperand(
    MachinePointerInfo PtrInfo, MachineMemOperand::Flags F, LocationSize Size,
    Align BaseAlignment, const AAMDNodes &AAInfo, const MDNode *Ranges,
    SyncScope::ID SSID, AtomicOrdering Ordering,
    AtomicOrdering FailureOrdering) {
  assert((!Size.hasValue() ||
          Size.getValue().getKnownMinValue() != ~UINT64_C(0)) &&
         "Unexpected an unknown size to be represented using "
         "LocationSize::beforeOrAfter()");
  return new (Allocator)
      MachineMemOperand(PtrInfo, F, Size, BaseAlignment, AAInfo, Ranges, SSID,
                        Ordering, FailureOrdering);
}

MachineMemOperand *MachineFunction::getMachineMemOperand(
    MachinePointerInfo PtrInfo, MachineMemOperand::Flags f, LLT MemTy,
    Align base_alignment, const AAMDNodes &AAInfo, const MDNode *Ranges,
    SyncScope::ID SSID, AtomicOrdering Ordering,
    AtomicOrdering FailureOrdering) {
  return new (Allocator)
      MachineMemOperand(PtrInfo, f, MemTy, base_alignment, AAInfo, Ranges, SSID,
                        Ordering, FailureOrdering);
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      const MachinePointerInfo &PtrInfo,
                                      LocationSize Size) {
  assert((!Size.hasValue() ||
          Size.getValue().getKnownMinValue() != ~UINT64_C(0)) &&
         "Unexpected an unknown size to be represented using "
         "LocationSize::beforeOrAfter()");
  return new (Allocator)
      MachineMemOperand(PtrInfo, MMO->getFlags(), Size, MMO->getBaseAlign(),
                        AAMDNodes(), nullptr, MMO->getSyncScopeID(),
                        MMO->getSuccessOrdering(), MMO->getFailureOrdering());
}

MachineMemOperand *MachineFunction::getMachineMemOperand(
    const MachineMemOperand *MMO, const MachinePointerInfo &PtrInfo, LLT Ty) {
  return new (Allocator)
      MachineMemOperand(PtrInfo, MMO->getFlags(), Ty, MMO->getBaseAlign(),
                        AAMDNodes(), nullptr, MMO->getSyncScopeID(),
                        MMO->getSuccessOrdering(), MMO->getFailureOrdering());
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      int64_t Offset, LLT Ty) {
  const MachinePointerInfo &PtrInfo = MMO->getPointerInfo();

  // If there is no pointer value, the offset isn't tracked so we need to adjust
  // the base alignment.
  Align Alignment = PtrInfo.V.isNull()
                        ? commonAlignment(MMO->getBaseAlign(), Offset)
                        : MMO->getBaseAlign();

  // Do not preserve ranges, since we don't necessarily know what the high bits
  // are anymore.
  return new (Allocator) MachineMemOperand(
      PtrInfo.getWithOffset(Offset), MMO->getFlags(), Ty, Alignment,
      MMO->getAAInfo(), nullptr, MMO->getSyncScopeID(),
      MMO->getSuccessOrdering(), MMO->getFailureOrdering());
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      const AAMDNodes &AAInfo) {
  MachinePointerInfo MPI = MMO->getValue() ?
             MachinePointerInfo(MMO->getValue(), MMO->getOffset()) :
             MachinePointerInfo(MMO->getPseudoValue(), MMO->getOffset());

  return new (Allocator) MachineMemOperand(
      MPI, MMO->getFlags(), MMO->getSize(), MMO->getBaseAlign(), AAInfo,
      MMO->getRanges(), MMO->getSyncScopeID(), MMO->getSuccessOrdering(),
      MMO->getFailureOrdering());
}

MachineMemOperand *
MachineFunction::getMachineMemOperand(const MachineMemOperand *MMO,
                                      MachineMemOperand::Flags Flags) {
  return new (Allocator) MachineMemOperand(
      MMO->getPointerInfo(), Flags, MMO->getSize(), MMO->getBaseAlign(),
      MMO->getAAInfo(), MMO->getRanges(), MMO->getSyncScopeID(),
      MMO->getSuccessOrdering(), MMO->getFailureOrdering());
}

MachineInstr::ExtraInfo *MachineFunction::createMIExtraInfo(
    ArrayRef<MachineMemOperand *> MMOs, MCSymbol *PreInstrSymbol,
    MCSymbol *PostInstrSymbol, MDNode *HeapAllocMarker, MDNode *PCSections,
    uint32_t CFIType, MDNode *MMRAs) {
  return MachineInstr::ExtraInfo::create(Allocator, MMOs, PreInstrSymbol,
                                         PostInstrSymbol, HeapAllocMarker,
                                         PCSections, CFIType, MMRAs);
}

const char *MachineFunction::createExternalSymbolName(StringRef Name) {
  char *Dest = Allocator.Allocate<char>(Name.size() + 1);
  llvm::copy(Name, Dest);
  Dest[Name.size()] = 0;
  return Dest;
}

uint32_t *MachineFunction::allocateRegMask() {
  unsigned NumRegs = getSubtarget().getRegisterInfo()->getNumRegs();
  unsigned Size = MachineOperand::getRegMaskSize(NumRegs);
  uint32_t *Mask = Allocator.Allocate<uint32_t>(Size);
  memset(Mask, 0, Size * sizeof(Mask[0]));
  return Mask;
}

ArrayRef<int> MachineFunction::allocateShuffleMask(ArrayRef<int> Mask) {
  int* AllocMask = Allocator.Allocate<int>(Mask.size());
  copy(Mask, AllocMask);
  return {AllocMask, Mask.size()};
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineFunction::dump() const {
  print(dbgs());
}
#endif

StringRef MachineFunction::getName() const {
  return getFunction().getName();
}

void MachineFunction::print(raw_ostream &OS, const SlotIndexes *Indexes) const {
  OS << "# Machine code for function " << getName() << ": ";
  getProperties().print(OS);
  OS << '\n';

  // Print Frame Information
  FrameInfo->print(*this, OS);

  // Print JumpTable Information
  if (JumpTableInfo)
    JumpTableInfo->print(OS);

  // Print Constant Pool
  ConstantPool->print(OS);

  const TargetRegisterInfo *TRI = getSubtarget().getRegisterInfo();

  if (RegInfo && !RegInfo->livein_empty()) {
    OS << "Function Live Ins: ";
    for (MachineRegisterInfo::livein_iterator
         I = RegInfo->livein_begin(), E = RegInfo->livein_end(); I != E; ++I) {
      OS << printReg(I->first, TRI);
      if (I->second)
        OS << " in " << printReg(I->second, TRI);
      if (std::next(I) != E)
        OS << ", ";
    }
    OS << '\n';
  }

  ModuleSlotTracker MST(getFunction().getParent());
  MST.incorporateFunction(getFunction());
  for (const auto &BB : *this) {
    OS << '\n';
    // If we print the whole function, print it at its most verbose level.
    BB.print(OS, MST, Indexes, /*IsStandalone=*/true);
  }

  OS << "\n# End machine code for function " << getName() << ".\n\n";
}

/// True if this function needs frame moves for debug or exceptions.
bool MachineFunction::needsFrameMoves() const {
  return getMMI().hasDebugInfo() ||
         getTarget().Options.ForceDwarfFrameSection ||
         F.needsUnwindTableEntry();
}

namespace llvm {

  template<>
  struct DOTGraphTraits<const MachineFunction*> : public DefaultDOTGraphTraits {
    DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

    static std::string getGraphName(const MachineFunction *F) {
      return ("CFG for '" + F->getName() + "' function").str();
    }

    std::string getNodeLabel(const MachineBasicBlock *Node,
                             const MachineFunction *Graph) {
      std::string OutStr;
      {
        raw_string_ostream OSS(OutStr);

        if (isSimple()) {
          OSS << printMBBReference(*Node);
          if (const BasicBlock *BB = Node->getBasicBlock())
            OSS << ": " << BB->getName();
        } else
          Node->print(OSS);
      }

      if (OutStr[0] == '\n') OutStr.erase(OutStr.begin());

      // Process string output to make it nicer...
      for (unsigned i = 0; i != OutStr.length(); ++i)
        if (OutStr[i] == '\n') {                            // Left justify
          OutStr[i] = '\\';
          OutStr.insert(OutStr.begin()+i+1, 'l');
        }
      return OutStr;
    }
  };

} // end namespace llvm

void MachineFunction::viewCFG() const
{
#ifndef NDEBUG
  ViewGraph(this, "mf" + getName());
#else
  errs() << "MachineFunction::viewCFG is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif // NDEBUG
}

void MachineFunction::viewCFGOnly() const
{
#ifndef NDEBUG
  ViewGraph(this, "mf" + getName(), true);
#else
  errs() << "MachineFunction::viewCFGOnly is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif // NDEBUG
}

/// Add the specified physical register as a live-in value and
/// create a corresponding virtual register for it.
Register MachineFunction::addLiveIn(MCRegister PReg,
                                    const TargetRegisterClass *RC) {
  MachineRegisterInfo &MRI = getRegInfo();
  Register VReg = MRI.getLiveInVirtReg(PReg);
  if (VReg) {
    const TargetRegisterClass *VRegRC = MRI.getRegClass(VReg);
    (void)VRegRC;
    // A physical register can be added several times.
    // Between two calls, the register class of the related virtual register
    // may have been constrained to match some operation constraints.
    // In that case, check that the current register class includes the
    // physical register and is a sub class of the specified RC.
    assert((VRegRC == RC || (VRegRC->contains(PReg) &&
                             RC->hasSubClassEq(VRegRC))) &&
            "Register class mismatch!");
    return VReg;
  }
  VReg = MRI.createVirtualRegister(RC);
  MRI.addLiveIn(PReg, VReg);
  return VReg;
}

/// Return the MCSymbol for the specified non-empty jump table.
/// If isLinkerPrivate is specified, an 'l' label is returned, otherwise a
/// normal 'L' label is returned.
MCSymbol *MachineFunction::getJTISymbol(unsigned JTI, MCContext &Ctx,
                                        bool isLinkerPrivate) const {
  const DataLayout &DL = getDataLayout();
  assert(JumpTableInfo && "No jump tables");
  assert(JTI < JumpTableInfo->getJumpTables().size() && "Invalid JTI!");

  StringRef Prefix = isLinkerPrivate ? DL.getLinkerPrivateGlobalPrefix()
                                     : DL.getPrivateGlobalPrefix();
  SmallString<60> Name;
  raw_svector_ostream(Name)
    << Prefix << "JTI" << getFunctionNumber() << '_' << JTI;
  return Ctx.getOrCreateSymbol(Name);
}

/// Return a function-local symbol to represent the PIC base.
MCSymbol *MachineFunction::getPICBaseSymbol() const {
  const DataLayout &DL = getDataLayout();
  return Ctx.getOrCreateSymbol(Twine(DL.getPrivateGlobalPrefix()) +
                               Twine(getFunctionNumber()) + "$pb");
}

/// \name Exception Handling
/// \{

LandingPadInfo &
MachineFunction::getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad) {
  unsigned N = LandingPads.size();
  for (unsigned i = 0; i < N; ++i) {
    LandingPadInfo &LP = LandingPads[i];
    if (LP.LandingPadBlock == LandingPad)
      return LP;
  }

  LandingPads.push_back(LandingPadInfo(LandingPad));
  return LandingPads[N];
}

void MachineFunction::addInvoke(MachineBasicBlock *LandingPad,
                                MCSymbol *BeginLabel, MCSymbol *EndLabel) {
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  LP.BeginLabels.push_back(BeginLabel);
  LP.EndLabels.push_back(EndLabel);
}

MCSymbol *MachineFunction::addLandingPad(MachineBasicBlock *LandingPad) {
  MCSymbol *LandingPadLabel = Ctx.createTempSymbol();
  LandingPadInfo &LP = getOrCreateLandingPadInfo(LandingPad);
  LP.LandingPadLabel = LandingPadLabel;

  const Instruction *FirstI = LandingPad->getBasicBlock()->getFirstNonPHI();
  if (const auto *LPI = dyn_cast<LandingPadInst>(FirstI)) {
    // If there's no typeid list specified, then "cleanup" is implicit.
    // Otherwise, id 0 is reserved for the cleanup action.
    if (LPI->isCleanup() && LPI->getNumClauses() != 0)
      LP.TypeIds.push_back(0);

    // FIXME: New EH - Add the clauses in reverse order. This isn't 100%
    //        correct, but we need to do it this way because of how the DWARF EH
    //        emitter processes the clauses.
    for (unsigned I = LPI->getNumClauses(); I != 0; --I) {
      Value *Val = LPI->getClause(I - 1);
      if (LPI->isCatch(I - 1)) {
        LP.TypeIds.push_back(
            getTypeIDFor(dyn_cast<GlobalValue>(Val->stripPointerCasts())));
      } else {
        // Add filters in a list.
        auto *CVal = cast<Constant>(Val);
        SmallVector<unsigned, 4> FilterList;
        for (const Use &U : CVal->operands())
          FilterList.push_back(
              getTypeIDFor(cast<GlobalValue>(U->stripPointerCasts())));

        LP.TypeIds.push_back(getFilterIDFor(FilterList));
      }
    }

  } else if (const auto *CPI = dyn_cast<CatchPadInst>(FirstI)) {
    for (unsigned I = CPI->arg_size(); I != 0; --I) {
      auto *TypeInfo =
          dyn_cast<GlobalValue>(CPI->getArgOperand(I - 1)->stripPointerCasts());
      LP.TypeIds.push_back(getTypeIDFor(TypeInfo));
    }

  } else {
    assert(isa<CleanupPadInst>(FirstI) && "Invalid landingpad!");
  }

  return LandingPadLabel;
}

void MachineFunction::setCallSiteLandingPad(MCSymbol *Sym,
                                            ArrayRef<unsigned> Sites) {
  LPadToCallSiteMap[Sym].append(Sites.begin(), Sites.end());
}

unsigned MachineFunction::getTypeIDFor(const GlobalValue *TI) {
  for (unsigned i = 0, N = TypeInfos.size(); i != N; ++i)
    if (TypeInfos[i] == TI) return i + 1;

  TypeInfos.push_back(TI);
  return TypeInfos.size();
}

int MachineFunction::getFilterIDFor(ArrayRef<unsigned> TyIds) {
  // If the new filter coincides with the tail of an existing filter, then
  // re-use the existing filter.  Folding filters more than this requires
  // re-ordering filters and/or their elements - probably not worth it.
  for (unsigned i : FilterEnds) {
    unsigned j = TyIds.size();

    while (i && j)
      if (FilterIds[--i] != TyIds[--j])
        goto try_next;

    if (!j)
      // The new filter coincides with range [i, end) of the existing filter.
      return -(1 + i);

try_next:;
  }

  // Add the new filter.
  int FilterID = -(1 + FilterIds.size());
  FilterIds.reserve(FilterIds.size() + TyIds.size() + 1);
  llvm::append_range(FilterIds, TyIds);
  FilterEnds.push_back(FilterIds.size());
  FilterIds.push_back(0); // terminator
  return FilterID;
}

MachineFunction::CallSiteInfoMap::iterator
MachineFunction::getCallSiteInfo(const MachineInstr *MI) {
  assert(MI->isCandidateForCallSiteEntry() &&
         "Call site info refers only to call (MI) candidates");

  if (!Target.Options.EmitCallSiteInfo)
    return CallSitesInfo.end();
  return CallSitesInfo.find(MI);
}

/// Return the call machine instruction or find a call within bundle.
static const MachineInstr *getCallInstr(const MachineInstr *MI) {
  if (!MI->isBundle())
    return MI;

  for (const auto &BMI : make_range(getBundleStart(MI->getIterator()),
                                    getBundleEnd(MI->getIterator())))
    if (BMI.isCandidateForCallSiteEntry())
      return &BMI;

  llvm_unreachable("Unexpected bundle without a call site candidate");
}

void MachineFunction::eraseCallSiteInfo(const MachineInstr *MI) {
  assert(MI->shouldUpdateCallSiteInfo() &&
         "Call site info refers only to call (MI) candidates or "
         "candidates inside bundles");

  const MachineInstr *CallMI = getCallInstr(MI);
  CallSiteInfoMap::iterator CSIt = getCallSiteInfo(CallMI);
  if (CSIt == CallSitesInfo.end())
    return;
  CallSitesInfo.erase(CSIt);
}

void MachineFunction::copyCallSiteInfo(const MachineInstr *Old,
                                       const MachineInstr *New) {
  assert(Old->shouldUpdateCallSiteInfo() &&
         "Call site info refers only to call (MI) candidates or "
         "candidates inside bundles");

  if (!New->isCandidateForCallSiteEntry())
    return eraseCallSiteInfo(Old);

  const MachineInstr *OldCallMI = getCallInstr(Old);
  CallSiteInfoMap::iterator CSIt = getCallSiteInfo(OldCallMI);
  if (CSIt == CallSitesInfo.end())
    return;

  CallSiteInfo CSInfo = CSIt->second;
  CallSitesInfo[New] = CSInfo;
}

void MachineFunction::moveCallSiteInfo(const MachineInstr *Old,
                                       const MachineInstr *New) {
  assert(Old->shouldUpdateCallSiteInfo() &&
         "Call site info refers only to call (MI) candidates or "
         "candidates inside bundles");

  if (!New->isCandidateForCallSiteEntry())
    return eraseCallSiteInfo(Old);

  const MachineInstr *OldCallMI = getCallInstr(Old);
  CallSiteInfoMap::iterator CSIt = getCallSiteInfo(OldCallMI);
  if (CSIt == CallSitesInfo.end())
    return;

  CallSiteInfo CSInfo = std::move(CSIt->second);
  CallSitesInfo.erase(CSIt);
  CallSitesInfo[New] = CSInfo;
}

void MachineFunction::setDebugInstrNumberingCount(unsigned Num) {
  DebugInstrNumberingCount = Num;
}

void MachineFunction::makeDebugValueSubstitution(DebugInstrOperandPair A,
                                                 DebugInstrOperandPair B,
                                                 unsigned Subreg) {
  // Catch any accidental self-loops.
  assert(A.first != B.first);
  // Don't allow any substitutions _from_ the memory operand number.
  assert(A.second != DebugOperandMemNumber);

  DebugValueSubstitutions.push_back({A, B, Subreg});
}

void MachineFunction::substituteDebugValuesForInst(const MachineInstr &Old,
                                                   MachineInstr &New,
                                                   unsigned MaxOperand) {
  // If the Old instruction wasn't tracked at all, there is no work to do.
  unsigned OldInstrNum = Old.peekDebugInstrNum();
  if (!OldInstrNum)
    return;

  // Iterate over all operands looking for defs to create substitutions for.
  // Avoid creating new instr numbers unless we create a new substitution.
  // While this has no functional effect, it risks confusing someone reading
  // MIR output.
  // Examine all the operands, or the first N specified by the caller.
  MaxOperand = std::min(MaxOperand, Old.getNumOperands());
  for (unsigned int I = 0; I < MaxOperand; ++I) {
    const auto &OldMO = Old.getOperand(I);
    auto &NewMO = New.getOperand(I);
    (void)NewMO;

    if (!OldMO.isReg() || !OldMO.isDef())
      continue;
    assert(NewMO.isDef());

    unsigned NewInstrNum = New.getDebugInstrNum();
    makeDebugValueSubstitution(std::make_pair(OldInstrNum, I),
                               std::make_pair(NewInstrNum, I));
  }
}

auto MachineFunction::salvageCopySSA(
    MachineInstr &MI, DenseMap<Register, DebugInstrOperandPair> &DbgPHICache)
    -> DebugInstrOperandPair {
  const TargetInstrInfo &TII = *getSubtarget().getInstrInfo();

  // Check whether this copy-like instruction has already been salvaged into
  // an operand pair.
  Register Dest;
  if (auto CopyDstSrc = TII.isCopyInstr(MI)) {
    Dest = CopyDstSrc->Destination->getReg();
  } else {
    assert(MI.isSubregToReg());
    Dest = MI.getOperand(0).getReg();
  }

  auto CacheIt = DbgPHICache.find(Dest);
  if (CacheIt != DbgPHICache.end())
    return CacheIt->second;

  // Calculate the instruction number to use, or install a DBG_PHI.
  auto OperandPair = salvageCopySSAImpl(MI);
  DbgPHICache.insert({Dest, OperandPair});
  return OperandPair;
}

auto MachineFunction::salvageCopySSAImpl(MachineInstr &MI)
    -> DebugInstrOperandPair {
  MachineRegisterInfo &MRI = getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  const TargetInstrInfo &TII = *getSubtarget().getInstrInfo();

  // Chase the value read by a copy-like instruction back to the instruction
  // that ultimately _defines_ that value. This may pass:
  //  * Through multiple intermediate copies, including subregister moves /
  //    copies,
  //  * Copies from physical registers that must then be traced back to the
  //    defining instruction,
  //  * Or, physical registers may be live-in to (only) the entry block, which
  //    requires a DBG_PHI to be created.
  // We can pursue this problem in that order: trace back through copies,
  // optionally through a physical register, to a defining instruction. We
  // should never move from physreg to vreg. As we're still in SSA form, no need
  // to worry about partial definitions of registers.

  // Helper lambda to interpret a copy-like instruction. Takes instruction,
  // returns the register read and any subregister identifying which part is
  // read.
  auto GetRegAndSubreg =
      [&](const MachineInstr &Cpy) -> std::pair<Register, unsigned> {
    Register NewReg, OldReg;
    unsigned SubReg;
    if (Cpy.isCopy()) {
      OldReg = Cpy.getOperand(0).getReg();
      NewReg = Cpy.getOperand(1).getReg();
      SubReg = Cpy.getOperand(1).getSubReg();
    } else if (Cpy.isSubregToReg()) {
      OldReg = Cpy.getOperand(0).getReg();
      NewReg = Cpy.getOperand(2).getReg();
      SubReg = Cpy.getOperand(3).getImm();
    } else {
      auto CopyDetails = *TII.isCopyInstr(Cpy);
      const MachineOperand &Src = *CopyDetails.Source;
      const MachineOperand &Dest = *CopyDetails.Destination;
      OldReg = Dest.getReg();
      NewReg = Src.getReg();
      SubReg = Src.getSubReg();
    }

    return {NewReg, SubReg};
  };

  // First seek either the defining instruction, or a copy from a physreg.
  // During search, the current state is the current copy instruction, and which
  // register we've read. Accumulate qualifying subregisters into SubregsSeen;
  // deal with those later.
  auto State = GetRegAndSubreg(MI);
  auto CurInst = MI.getIterator();
  SmallVector<unsigned, 4> SubregsSeen;
  while (true) {
    // If we've found a copy from a physreg, first portion of search is over.
    if (!State.first.isVirtual())
      break;

    // Record any subregister qualifier.
    if (State.second)
      SubregsSeen.push_back(State.second);

    assert(MRI.hasOneDef(State.first));
    MachineInstr &Inst = *MRI.def_begin(State.first)->getParent();
    CurInst = Inst.getIterator();

    // Any non-copy instruction is the defining instruction we're seeking.
    if (!Inst.isCopyLike() && !TII.isCopyInstr(Inst))
      break;
    State = GetRegAndSubreg(Inst);
  };

  // Helper lambda to apply additional subregister substitutions to a known
  // instruction/operand pair. Adds new (fake) substitutions so that we can
  // record the subregister. FIXME: this isn't very space efficient if multiple
  // values are tracked back through the same copies; cache something later.
  auto ApplySubregisters =
      [&](DebugInstrOperandPair P) -> DebugInstrOperandPair {
    for (unsigned Subreg : reverse(SubregsSeen)) {
      // Fetch a new instruction number, not attached to an actual instruction.
      unsigned NewInstrNumber = getNewDebugInstrNum();
      // Add a substitution from the "new" number to the known one, with a
      // qualifying subreg.
      makeDebugValueSubstitution({NewInstrNumber, 0}, P, Subreg);
      // Return the new number; to find the underlying value, consumers need to
      // deal with the qualifying subreg.
      P = {NewInstrNumber, 0};
    }
    return P;
  };

  // If we managed to find the defining instruction after COPYs, return an
  // instruction / operand pair after adding subregister qualifiers.
  if (State.first.isVirtual()) {
    // Virtual register def -- we can just look up where this happens.
    MachineInstr *Inst = MRI.def_begin(State.first)->getParent();
    for (auto &MO : Inst->all_defs()) {
      if (MO.getReg() != State.first)
        continue;
      return ApplySubregisters({Inst->getDebugInstrNum(), MO.getOperandNo()});
    }

    llvm_unreachable("Vreg def with no corresponding operand?");
  }

  // Our search ended in a copy from a physreg: walk back up the function
  // looking for whatever defines the physreg.
  assert(CurInst->isCopyLike() || TII.isCopyInstr(*CurInst));
  State = GetRegAndSubreg(*CurInst);
  Register RegToSeek = State.first;

  auto RMII = CurInst->getReverseIterator();
  auto PrevInstrs = make_range(RMII, CurInst->getParent()->instr_rend());
  for (auto &ToExamine : PrevInstrs) {
    for (auto &MO : ToExamine.all_defs()) {
      // Test for operand that defines something aliasing RegToSeek.
      if (!TRI.regsOverlap(RegToSeek, MO.getReg()))
        continue;

      return ApplySubregisters(
          {ToExamine.getDebugInstrNum(), MO.getOperandNo()});
    }
  }

  MachineBasicBlock &InsertBB = *CurInst->getParent();

  // We reached the start of the block before finding a defining instruction.
  // There are numerous scenarios where this can happen:
  // * Constant physical registers,
  // * Several intrinsics that allow LLVM-IR to read arbitary registers,
  // * Arguments in the entry block,
  // * Exception handling landing pads.
  // Validating all of them is too difficult, so just insert a DBG_PHI reading
  // the variable value at this position, rather than checking it makes sense.

  // Create DBG_PHI for specified physreg.
  auto Builder = BuildMI(InsertBB, InsertBB.getFirstNonPHI(), DebugLoc(),
                         TII.get(TargetOpcode::DBG_PHI));
  Builder.addReg(State.first);
  unsigned NewNum = getNewDebugInstrNum();
  Builder.addImm(NewNum);
  return ApplySubregisters({NewNum, 0u});
}

void MachineFunction::finalizeDebugInstrRefs() {
  auto *TII = getSubtarget().getInstrInfo();

  auto MakeUndefDbgValue = [&](MachineInstr &MI) {
    const MCInstrDesc &RefII = TII->get(TargetOpcode::DBG_VALUE_LIST);
    MI.setDesc(RefII);
    MI.setDebugValueUndef();
  };

  DenseMap<Register, DebugInstrOperandPair> ArgDbgPHIs;
  for (auto &MBB : *this) {
    for (auto &MI : MBB) {
      if (!MI.isDebugRef())
        continue;

      bool IsValidRef = true;

      for (MachineOperand &MO : MI.debug_operands()) {
        if (!MO.isReg())
          continue;

        Register Reg = MO.getReg();

        // Some vregs can be deleted as redundant in the meantime. Mark those
        // as DBG_VALUE $noreg. Additionally, some normal instructions are
        // quickly deleted, leaving dangling references to vregs with no def.
        if (Reg == 0 || !RegInfo->hasOneDef(Reg)) {
          IsValidRef = false;
          break;
        }

        assert(Reg.isVirtual());
        MachineInstr &DefMI = *RegInfo->def_instr_begin(Reg);

        // If we've found a copy-like instruction, follow it back to the
        // instruction that defines the source value, see salvageCopySSA docs
        // for why this is important.
        if (DefMI.isCopyLike() || TII->isCopyInstr(DefMI)) {
          auto Result = salvageCopySSA(DefMI, ArgDbgPHIs);
          MO.ChangeToDbgInstrRef(Result.first, Result.second);
        } else {
          // Otherwise, identify the operand number that the VReg refers to.
          unsigned OperandIdx = 0;
          for (const auto &DefMO : DefMI.operands()) {
            if (DefMO.isReg() && DefMO.isDef() && DefMO.getReg() == Reg)
              break;
            ++OperandIdx;
          }
          assert(OperandIdx < DefMI.getNumOperands());

          // Morph this instr ref to point at the given instruction and operand.
          unsigned ID = DefMI.getDebugInstrNum();
          MO.ChangeToDbgInstrRef(ID, OperandIdx);
        }
      }

      if (!IsValidRef)
        MakeUndefDbgValue(MI);
    }
  }
}

bool MachineFunction::shouldUseDebugInstrRef() const {
  // Disable instr-ref at -O0: it's very slow (in compile time). We can still
  // have optimized code inlined into this unoptimized code, however with
  // fewer and less aggressive optimizations happening, coverage and accuracy
  // should not suffer.
  if (getTarget().getOptLevel() == CodeGenOptLevel::None)
    return false;

  // Don't use instr-ref if this function is marked optnone.
  if (F.hasFnAttribute(Attribute::OptimizeNone))
    return false;

  if (llvm::debuginfoShouldUseDebugInstrRef(getTarget().getTargetTriple()))
    return true;

  return false;
}

bool MachineFunction::useDebugInstrRef() const {
  return UseDebugInstrRef;
}

void MachineFunction::setUseDebugInstrRef(bool Use) {
  UseDebugInstrRef = Use;
}

// Use one million as a high / reserved number.
const unsigned MachineFunction::DebugOperandMemNumber = 1000000;

/// \}

//===----------------------------------------------------------------------===//
//  MachineJumpTableInfo implementation
//===----------------------------------------------------------------------===//

/// Return the size of each entry in the jump table.
unsigned MachineJumpTableInfo::getEntrySize(const DataLayout &TD) const {
  // The size of a jump table entry is 4 bytes unless the entry is just the
  // address of a block, in which case it is the pointer size.
  switch (getEntryKind()) {
  case MachineJumpTableInfo::EK_BlockAddress:
    return TD.getPointerSize();
  case MachineJumpTableInfo::EK_GPRel64BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference64:
    return 8;
  case MachineJumpTableInfo::EK_GPRel32BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference32:
  case MachineJumpTableInfo::EK_Custom32:
    return 4;
  case MachineJumpTableInfo::EK_Inline:
    return 0;
  }
  llvm_unreachable("Unknown jump table encoding!");
}

/// Return the alignment of each entry in the jump table.
unsigned MachineJumpTableInfo::getEntryAlignment(const DataLayout &TD) const {
  // The alignment of a jump table entry is the alignment of int32 unless the
  // entry is just the address of a block, in which case it is the pointer
  // alignment.
  switch (getEntryKind()) {
  case MachineJumpTableInfo::EK_BlockAddress:
    return TD.getPointerABIAlignment(0).value();
  case MachineJumpTableInfo::EK_GPRel64BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference64:
    return TD.getABIIntegerTypeAlignment(64).value();
  case MachineJumpTableInfo::EK_GPRel32BlockAddress:
  case MachineJumpTableInfo::EK_LabelDifference32:
  case MachineJumpTableInfo::EK_Custom32:
    return TD.getABIIntegerTypeAlignment(32).value();
  case MachineJumpTableInfo::EK_Inline:
    return 1;
  }
  llvm_unreachable("Unknown jump table encoding!");
}

/// Create a new jump table entry in the jump table info.
unsigned MachineJumpTableInfo::createJumpTableIndex(
                               const std::vector<MachineBasicBlock*> &DestBBs) {
  assert(!DestBBs.empty() && "Cannot create an empty jump table!");
  JumpTables.push_back(MachineJumpTableEntry(DestBBs));
  return JumpTables.size()-1;
}

/// If Old is the target of any jump tables, update the jump tables to branch
/// to New instead.
bool MachineJumpTableInfo::ReplaceMBBInJumpTables(MachineBasicBlock *Old,
                                                  MachineBasicBlock *New) {
  assert(Old != New && "Not making a change?");
  bool MadeChange = false;
  for (size_t i = 0, e = JumpTables.size(); i != e; ++i)
    ReplaceMBBInJumpTable(i, Old, New);
  return MadeChange;
}

/// If MBB is present in any jump tables, remove it.
bool MachineJumpTableInfo::RemoveMBBFromJumpTables(MachineBasicBlock *MBB) {
  bool MadeChange = false;
  for (MachineJumpTableEntry &JTE : JumpTables) {
    auto removeBeginItr = std::remove(JTE.MBBs.begin(), JTE.MBBs.end(), MBB);
    MadeChange |= (removeBeginItr != JTE.MBBs.end());
    JTE.MBBs.erase(removeBeginItr, JTE.MBBs.end());
  }
  return MadeChange;
}

/// If Old is a target of the jump tables, update the jump table to branch to
/// New instead.
bool MachineJumpTableInfo::ReplaceMBBInJumpTable(unsigned Idx,
                                                 MachineBasicBlock *Old,
                                                 MachineBasicBlock *New) {
  assert(Old != New && "Not making a change?");
  bool MadeChange = false;
  MachineJumpTableEntry &JTE = JumpTables[Idx];
  for (MachineBasicBlock *&MBB : JTE.MBBs)
    if (MBB == Old) {
      MBB = New;
      MadeChange = true;
    }
  return MadeChange;
}

void MachineJumpTableInfo::print(raw_ostream &OS) const {
  if (JumpTables.empty()) return;

  OS << "Jump Tables:\n";

  for (unsigned i = 0, e = JumpTables.size(); i != e; ++i) {
    OS << printJumpTableEntryReference(i) << ':';
    for (const MachineBasicBlock *MBB : JumpTables[i].MBBs)
      OS << ' ' << printMBBReference(*MBB);
    if (i != e)
      OS << '\n';
  }

  OS << '\n';
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineJumpTableInfo::dump() const { print(dbgs()); }
#endif

Printable llvm::printJumpTableEntryReference(unsigned Idx) {
  return Printable([Idx](raw_ostream &OS) { OS << "%jump-table." << Idx; });
}

//===----------------------------------------------------------------------===//
//  MachineConstantPool implementation
//===----------------------------------------------------------------------===//

void MachineConstantPoolValue::anchor() {}

unsigned MachineConstantPoolValue::getSizeInBytes(const DataLayout &DL) const {
  return DL.getTypeAllocSize(Ty);
}

unsigned MachineConstantPoolEntry::getSizeInBytes(const DataLayout &DL) const {
  if (isMachineConstantPoolEntry())
    return Val.MachineCPVal->getSizeInBytes(DL);
  return DL.getTypeAllocSize(Val.ConstVal->getType());
}

bool MachineConstantPoolEntry::needsRelocation() const {
  if (isMachineConstantPoolEntry())
    return true;
  return Val.ConstVal->needsDynamicRelocation();
}

SectionKind
MachineConstantPoolEntry::getSectionKind(const DataLayout *DL) const {
  if (needsRelocation())
    return SectionKind::getReadOnlyWithRel();
  switch (getSizeInBytes(*DL)) {
  case 4:
    return SectionKind::getMergeableConst4();
  case 8:
    return SectionKind::getMergeableConst8();
  case 16:
    return SectionKind::getMergeableConst16();
  case 32:
    return SectionKind::getMergeableConst32();
  default:
    return SectionKind::getReadOnly();
  }
}

MachineConstantPool::~MachineConstantPool() {
  // A constant may be a member of both Constants and MachineCPVsSharingEntries,
  // so keep track of which we've deleted to avoid double deletions.
  DenseSet<MachineConstantPoolValue*> Deleted;
  for (const MachineConstantPoolEntry &C : Constants)
    if (C.isMachineConstantPoolEntry()) {
      Deleted.insert(C.Val.MachineCPVal);
      delete C.Val.MachineCPVal;
    }
  for (MachineConstantPoolValue *CPV : MachineCPVsSharingEntries) {
    if (Deleted.count(CPV) == 0)
      delete CPV;
  }
}

/// Test whether the given two constants can be allocated the same constant pool
/// entry referenced by \param A.
static bool CanShareConstantPoolEntry(const Constant *A, const Constant *B,
                                      const DataLayout &DL) {
  // Handle the trivial case quickly.
  if (A == B) return true;

  // If they have the same type but weren't the same constant, quickly
  // reject them.
  if (A->getType() == B->getType()) return false;

  // We can't handle structs or arrays.
  if (isa<StructType>(A->getType()) || isa<ArrayType>(A->getType()) ||
      isa<StructType>(B->getType()) || isa<ArrayType>(B->getType()))
    return false;

  // For now, only support constants with the same size.
  uint64_t StoreSize = DL.getTypeStoreSize(A->getType());
  if (StoreSize != DL.getTypeStoreSize(B->getType()) || StoreSize > 128)
    return false;

  bool ContainsUndefOrPoisonA = A->containsUndefOrPoisonElement();

  Type *IntTy = IntegerType::get(A->getContext(), StoreSize*8);

  // Try constant folding a bitcast of both instructions to an integer.  If we
  // get two identical ConstantInt's, then we are good to share them.  We use
  // the constant folding APIs to do this so that we get the benefit of
  // DataLayout.
  if (isa<PointerType>(A->getType()))
    A = ConstantFoldCastOperand(Instruction::PtrToInt,
                                const_cast<Constant *>(A), IntTy, DL);
  else if (A->getType() != IntTy)
    A = ConstantFoldCastOperand(Instruction::BitCast, const_cast<Constant *>(A),
                                IntTy, DL);
  if (isa<PointerType>(B->getType()))
    B = ConstantFoldCastOperand(Instruction::PtrToInt,
                                const_cast<Constant *>(B), IntTy, DL);
  else if (B->getType() != IntTy)
    B = ConstantFoldCastOperand(Instruction::BitCast, const_cast<Constant *>(B),
                                IntTy, DL);

  if (A != B)
    return false;

  // Constants only safely match if A doesn't contain undef/poison.
  // As we'll be reusing A, it doesn't matter if B contain undef/poison.
  // TODO: Handle cases where A and B have the same undef/poison elements.
  // TODO: Merge A and B with mismatching undef/poison elements.
  return !ContainsUndefOrPoisonA;
}

/// Create a new entry in the constant pool or return an existing one.
/// User must specify the log2 of the minimum required alignment for the object.
unsigned MachineConstantPool::getConstantPoolIndex(const Constant *C,
                                                   Align Alignment) {
  if (Alignment > PoolAlignment) PoolAlignment = Alignment;

  // Check to see if we already have this constant.
  //
  // FIXME, this could be made much more efficient for large constant pools.
  for (unsigned i = 0, e = Constants.size(); i != e; ++i)
    if (!Constants[i].isMachineConstantPoolEntry() &&
        CanShareConstantPoolEntry(Constants[i].Val.ConstVal, C, DL)) {
      if (Constants[i].getAlign() < Alignment)
        Constants[i].Alignment = Alignment;
      return i;
    }

  Constants.push_back(MachineConstantPoolEntry(C, Alignment));
  return Constants.size()-1;
}

unsigned MachineConstantPool::getConstantPoolIndex(MachineConstantPoolValue *V,
                                                   Align Alignment) {
  if (Alignment > PoolAlignment) PoolAlignment = Alignment;

  // Check to see if we already have this constant.
  //
  // FIXME, this could be made much more efficient for large constant pools.
  int Idx = V->getExistingMachineCPValue(this, Alignment);
  if (Idx != -1) {
    MachineCPVsSharingEntries.insert(V);
    return (unsigned)Idx;
  }

  Constants.push_back(MachineConstantPoolEntry(V, Alignment));
  return Constants.size()-1;
}

void MachineConstantPool::print(raw_ostream &OS) const {
  if (Constants.empty()) return;

  OS << "Constant Pool:\n";
  for (unsigned i = 0, e = Constants.size(); i != e; ++i) {
    OS << "  cp#" << i << ": ";
    if (Constants[i].isMachineConstantPoolEntry())
      Constants[i].Val.MachineCPVal->print(OS);
    else
      Constants[i].Val.ConstVal->printAsOperand(OS, /*PrintType=*/false);
    OS << ", align=" << Constants[i].getAlign().value();
    OS << "\n";
  }
}

//===----------------------------------------------------------------------===//
// Template specialization for MachineFunction implementation of
// ProfileSummaryInfo::getEntryCount().
//===----------------------------------------------------------------------===//
template <>
std::optional<Function::ProfileCount>
ProfileSummaryInfo::getEntryCount<llvm::MachineFunction>(
    const llvm::MachineFunction *F) const {
  return F->getFunction().getEntryCount();
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void MachineConstantPool::dump() const { print(dbgs()); }
#endif
