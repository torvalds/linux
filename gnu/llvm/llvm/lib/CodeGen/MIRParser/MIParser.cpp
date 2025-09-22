//===- MIParser.cpp - Machine instructions parser implementation ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the parsing of machine instructions.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "MILexer.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/AsmParser/SlotMapping.h"
#include "llvm/CodeGen/MIRFormatter.h"
#include "llvm/CodeGen/MIRPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Target/TargetIntrinsicInfo.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using namespace llvm;

void PerTargetMIParsingState::setTarget(
  const TargetSubtargetInfo &NewSubtarget) {

  // If the subtarget changed, over conservatively assume everything is invalid.
  if (&Subtarget == &NewSubtarget)
    return;

  Names2InstrOpCodes.clear();
  Names2Regs.clear();
  Names2RegMasks.clear();
  Names2SubRegIndices.clear();
  Names2TargetIndices.clear();
  Names2DirectTargetFlags.clear();
  Names2BitmaskTargetFlags.clear();
  Names2MMOTargetFlags.clear();

  initNames2RegClasses();
  initNames2RegBanks();
}

void PerTargetMIParsingState::initNames2Regs() {
  if (!Names2Regs.empty())
    return;

  // The '%noreg' register is the register 0.
  Names2Regs.insert(std::make_pair("noreg", 0));
  const auto *TRI = Subtarget.getRegisterInfo();
  assert(TRI && "Expected target register info");

  for (unsigned I = 0, E = TRI->getNumRegs(); I < E; ++I) {
    bool WasInserted =
        Names2Regs.insert(std::make_pair(StringRef(TRI->getName(I)).lower(), I))
            .second;
    (void)WasInserted;
    assert(WasInserted && "Expected registers to be unique case-insensitively");
  }
}

bool PerTargetMIParsingState::getRegisterByName(StringRef RegName,
                                                Register &Reg) {
  initNames2Regs();
  auto RegInfo = Names2Regs.find(RegName);
  if (RegInfo == Names2Regs.end())
    return true;
  Reg = RegInfo->getValue();
  return false;
}

void PerTargetMIParsingState::initNames2InstrOpCodes() {
  if (!Names2InstrOpCodes.empty())
    return;
  const auto *TII = Subtarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  for (unsigned I = 0, E = TII->getNumOpcodes(); I < E; ++I)
    Names2InstrOpCodes.insert(std::make_pair(StringRef(TII->getName(I)), I));
}

bool PerTargetMIParsingState::parseInstrName(StringRef InstrName,
                                             unsigned &OpCode) {
  initNames2InstrOpCodes();
  auto InstrInfo = Names2InstrOpCodes.find(InstrName);
  if (InstrInfo == Names2InstrOpCodes.end())
    return true;
  OpCode = InstrInfo->getValue();
  return false;
}

void PerTargetMIParsingState::initNames2RegMasks() {
  if (!Names2RegMasks.empty())
    return;
  const auto *TRI = Subtarget.getRegisterInfo();
  assert(TRI && "Expected target register info");
  ArrayRef<const uint32_t *> RegMasks = TRI->getRegMasks();
  ArrayRef<const char *> RegMaskNames = TRI->getRegMaskNames();
  assert(RegMasks.size() == RegMaskNames.size());
  for (size_t I = 0, E = RegMasks.size(); I < E; ++I)
    Names2RegMasks.insert(
        std::make_pair(StringRef(RegMaskNames[I]).lower(), RegMasks[I]));
}

const uint32_t *PerTargetMIParsingState::getRegMask(StringRef Identifier) {
  initNames2RegMasks();
  auto RegMaskInfo = Names2RegMasks.find(Identifier);
  if (RegMaskInfo == Names2RegMasks.end())
    return nullptr;
  return RegMaskInfo->getValue();
}

void PerTargetMIParsingState::initNames2SubRegIndices() {
  if (!Names2SubRegIndices.empty())
    return;
  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  for (unsigned I = 1, E = TRI->getNumSubRegIndices(); I < E; ++I)
    Names2SubRegIndices.insert(
        std::make_pair(TRI->getSubRegIndexName(I), I));
}

unsigned PerTargetMIParsingState::getSubRegIndex(StringRef Name) {
  initNames2SubRegIndices();
  auto SubRegInfo = Names2SubRegIndices.find(Name);
  if (SubRegInfo == Names2SubRegIndices.end())
    return 0;
  return SubRegInfo->getValue();
}

void PerTargetMIParsingState::initNames2TargetIndices() {
  if (!Names2TargetIndices.empty())
    return;
  const auto *TII = Subtarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Indices = TII->getSerializableTargetIndices();
  for (const auto &I : Indices)
    Names2TargetIndices.insert(std::make_pair(StringRef(I.second), I.first));
}

bool PerTargetMIParsingState::getTargetIndex(StringRef Name, int &Index) {
  initNames2TargetIndices();
  auto IndexInfo = Names2TargetIndices.find(Name);
  if (IndexInfo == Names2TargetIndices.end())
    return true;
  Index = IndexInfo->second;
  return false;
}

void PerTargetMIParsingState::initNames2DirectTargetFlags() {
  if (!Names2DirectTargetFlags.empty())
    return;

  const auto *TII = Subtarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Flags = TII->getSerializableDirectMachineOperandTargetFlags();
  for (const auto &I : Flags)
    Names2DirectTargetFlags.insert(
        std::make_pair(StringRef(I.second), I.first));
}

bool PerTargetMIParsingState::getDirectTargetFlag(StringRef Name,
                                                  unsigned &Flag) {
  initNames2DirectTargetFlags();
  auto FlagInfo = Names2DirectTargetFlags.find(Name);
  if (FlagInfo == Names2DirectTargetFlags.end())
    return true;
  Flag = FlagInfo->second;
  return false;
}

void PerTargetMIParsingState::initNames2BitmaskTargetFlags() {
  if (!Names2BitmaskTargetFlags.empty())
    return;

  const auto *TII = Subtarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Flags = TII->getSerializableBitmaskMachineOperandTargetFlags();
  for (const auto &I : Flags)
    Names2BitmaskTargetFlags.insert(
        std::make_pair(StringRef(I.second), I.first));
}

bool PerTargetMIParsingState::getBitmaskTargetFlag(StringRef Name,
                                                   unsigned &Flag) {
  initNames2BitmaskTargetFlags();
  auto FlagInfo = Names2BitmaskTargetFlags.find(Name);
  if (FlagInfo == Names2BitmaskTargetFlags.end())
    return true;
  Flag = FlagInfo->second;
  return false;
}

void PerTargetMIParsingState::initNames2MMOTargetFlags() {
  if (!Names2MMOTargetFlags.empty())
    return;

  const auto *TII = Subtarget.getInstrInfo();
  assert(TII && "Expected target instruction info");
  auto Flags = TII->getSerializableMachineMemOperandTargetFlags();
  for (const auto &I : Flags)
    Names2MMOTargetFlags.insert(std::make_pair(StringRef(I.second), I.first));
}

bool PerTargetMIParsingState::getMMOTargetFlag(StringRef Name,
                                               MachineMemOperand::Flags &Flag) {
  initNames2MMOTargetFlags();
  auto FlagInfo = Names2MMOTargetFlags.find(Name);
  if (FlagInfo == Names2MMOTargetFlags.end())
    return true;
  Flag = FlagInfo->second;
  return false;
}

void PerTargetMIParsingState::initNames2RegClasses() {
  if (!Names2RegClasses.empty())
    return;

  const TargetRegisterInfo *TRI = Subtarget.getRegisterInfo();
  for (unsigned I = 0, E = TRI->getNumRegClasses(); I < E; ++I) {
    const auto *RC = TRI->getRegClass(I);
    Names2RegClasses.insert(
        std::make_pair(StringRef(TRI->getRegClassName(RC)).lower(), RC));
  }
}

void PerTargetMIParsingState::initNames2RegBanks() {
  if (!Names2RegBanks.empty())
    return;

  const RegisterBankInfo *RBI = Subtarget.getRegBankInfo();
  // If the target does not support GlobalISel, we may not have a
  // register bank info.
  if (!RBI)
    return;

  for (unsigned I = 0, E = RBI->getNumRegBanks(); I < E; ++I) {
    const auto &RegBank = RBI->getRegBank(I);
    Names2RegBanks.insert(
        std::make_pair(StringRef(RegBank.getName()).lower(), &RegBank));
  }
}

const TargetRegisterClass *
PerTargetMIParsingState::getRegClass(StringRef Name) {
  auto RegClassInfo = Names2RegClasses.find(Name);
  if (RegClassInfo == Names2RegClasses.end())
    return nullptr;
  return RegClassInfo->getValue();
}

const RegisterBank *PerTargetMIParsingState::getRegBank(StringRef Name) {
  auto RegBankInfo = Names2RegBanks.find(Name);
  if (RegBankInfo == Names2RegBanks.end())
    return nullptr;
  return RegBankInfo->getValue();
}

PerFunctionMIParsingState::PerFunctionMIParsingState(MachineFunction &MF,
    SourceMgr &SM, const SlotMapping &IRSlots, PerTargetMIParsingState &T)
  : MF(MF), SM(&SM), IRSlots(IRSlots), Target(T) {
}

VRegInfo &PerFunctionMIParsingState::getVRegInfo(Register Num) {
  auto I = VRegInfos.insert(std::make_pair(Num, nullptr));
  if (I.second) {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    VRegInfo *Info = new (Allocator) VRegInfo;
    Info->VReg = MRI.createIncompleteVirtualRegister();
    I.first->second = Info;
  }
  return *I.first->second;
}

VRegInfo &PerFunctionMIParsingState::getVRegInfoNamed(StringRef RegName) {
  assert(RegName != "" && "Expected named reg.");

  auto I = VRegInfosNamed.insert(std::make_pair(RegName.str(), nullptr));
  if (I.second) {
    VRegInfo *Info = new (Allocator) VRegInfo;
    Info->VReg = MF.getRegInfo().createIncompleteVirtualRegister(RegName);
    I.first->second = Info;
  }
  return *I.first->second;
}

static void mapValueToSlot(const Value *V, ModuleSlotTracker &MST,
                           DenseMap<unsigned, const Value *> &Slots2Values) {
  int Slot = MST.getLocalSlot(V);
  if (Slot == -1)
    return;
  Slots2Values.insert(std::make_pair(unsigned(Slot), V));
}

/// Creates the mapping from slot numbers to function's unnamed IR values.
static void initSlots2Values(const Function &F,
                             DenseMap<unsigned, const Value *> &Slots2Values) {
  ModuleSlotTracker MST(F.getParent(), /*ShouldInitializeAllMetadata=*/false);
  MST.incorporateFunction(F);
  for (const auto &Arg : F.args())
    mapValueToSlot(&Arg, MST, Slots2Values);
  for (const auto &BB : F) {
    mapValueToSlot(&BB, MST, Slots2Values);
    for (const auto &I : BB)
      mapValueToSlot(&I, MST, Slots2Values);
  }
}

const Value* PerFunctionMIParsingState::getIRValue(unsigned Slot) {
  if (Slots2Values.empty())
    initSlots2Values(MF.getFunction(), Slots2Values);
  return Slots2Values.lookup(Slot);
}

namespace {

/// A wrapper struct around the 'MachineOperand' struct that includes a source
/// range and other attributes.
struct ParsedMachineOperand {
  MachineOperand Operand;
  StringRef::iterator Begin;
  StringRef::iterator End;
  std::optional<unsigned> TiedDefIdx;

  ParsedMachineOperand(const MachineOperand &Operand, StringRef::iterator Begin,
                       StringRef::iterator End,
                       std::optional<unsigned> &TiedDefIdx)
      : Operand(Operand), Begin(Begin), End(End), TiedDefIdx(TiedDefIdx) {
    if (TiedDefIdx)
      assert(Operand.isReg() && Operand.isUse() &&
             "Only used register operands can be tied");
  }
};

class MIParser {
  MachineFunction &MF;
  SMDiagnostic &Error;
  StringRef Source, CurrentSource;
  SMRange SourceRange;
  MIToken Token;
  PerFunctionMIParsingState &PFS;
  /// Maps from slot numbers to function's unnamed basic blocks.
  DenseMap<unsigned, const BasicBlock *> Slots2BasicBlocks;

public:
  MIParser(PerFunctionMIParsingState &PFS, SMDiagnostic &Error,
           StringRef Source);
  MIParser(PerFunctionMIParsingState &PFS, SMDiagnostic &Error,
           StringRef Source, SMRange SourceRange);

  /// \p SkipChar gives the number of characters to skip before looking
  /// for the next token.
  void lex(unsigned SkipChar = 0);

  /// Report an error at the current location with the given message.
  ///
  /// This function always return true.
  bool error(const Twine &Msg);

  /// Report an error at the given location with the given message.
  ///
  /// This function always return true.
  bool error(StringRef::iterator Loc, const Twine &Msg);

  bool
  parseBasicBlockDefinitions(DenseMap<unsigned, MachineBasicBlock *> &MBBSlots);
  bool parseBasicBlocks();
  bool parse(MachineInstr *&MI);
  bool parseStandaloneMBB(MachineBasicBlock *&MBB);
  bool parseStandaloneNamedRegister(Register &Reg);
  bool parseStandaloneVirtualRegister(VRegInfo *&Info);
  bool parseStandaloneRegister(Register &Reg);
  bool parseStandaloneStackObject(int &FI);
  bool parseStandaloneMDNode(MDNode *&Node);
  bool parseMachineMetadata();
  bool parseMDTuple(MDNode *&MD, bool IsDistinct);
  bool parseMDNodeVector(SmallVectorImpl<Metadata *> &Elts);
  bool parseMetadata(Metadata *&MD);

  bool
  parseBasicBlockDefinition(DenseMap<unsigned, MachineBasicBlock *> &MBBSlots);
  bool parseBasicBlock(MachineBasicBlock &MBB,
                       MachineBasicBlock *&AddFalthroughFrom);
  bool parseBasicBlockLiveins(MachineBasicBlock &MBB);
  bool parseBasicBlockSuccessors(MachineBasicBlock &MBB);

  bool parseNamedRegister(Register &Reg);
  bool parseVirtualRegister(VRegInfo *&Info);
  bool parseNamedVirtualRegister(VRegInfo *&Info);
  bool parseRegister(Register &Reg, VRegInfo *&VRegInfo);
  bool parseRegisterFlag(unsigned &Flags);
  bool parseRegisterClassOrBank(VRegInfo &RegInfo);
  bool parseSubRegisterIndex(unsigned &SubReg);
  bool parseRegisterTiedDefIndex(unsigned &TiedDefIdx);
  bool parseRegisterOperand(MachineOperand &Dest,
                            std::optional<unsigned> &TiedDefIdx,
                            bool IsDef = false);
  bool parseImmediateOperand(MachineOperand &Dest);
  bool parseIRConstant(StringRef::iterator Loc, StringRef StringValue,
                       const Constant *&C);
  bool parseIRConstant(StringRef::iterator Loc, const Constant *&C);
  bool parseLowLevelType(StringRef::iterator Loc, LLT &Ty);
  bool parseTypedImmediateOperand(MachineOperand &Dest);
  bool parseFPImmediateOperand(MachineOperand &Dest);
  bool parseMBBReference(MachineBasicBlock *&MBB);
  bool parseMBBOperand(MachineOperand &Dest);
  bool parseStackFrameIndex(int &FI);
  bool parseStackObjectOperand(MachineOperand &Dest);
  bool parseFixedStackFrameIndex(int &FI);
  bool parseFixedStackObjectOperand(MachineOperand &Dest);
  bool parseGlobalValue(GlobalValue *&GV);
  bool parseGlobalAddressOperand(MachineOperand &Dest);
  bool parseConstantPoolIndexOperand(MachineOperand &Dest);
  bool parseSubRegisterIndexOperand(MachineOperand &Dest);
  bool parseJumpTableIndexOperand(MachineOperand &Dest);
  bool parseExternalSymbolOperand(MachineOperand &Dest);
  bool parseMCSymbolOperand(MachineOperand &Dest);
  [[nodiscard]] bool parseMDNode(MDNode *&Node);
  bool parseDIExpression(MDNode *&Expr);
  bool parseDILocation(MDNode *&Expr);
  bool parseMetadataOperand(MachineOperand &Dest);
  bool parseCFIOffset(int &Offset);
  bool parseCFIRegister(Register &Reg);
  bool parseCFIAddressSpace(unsigned &AddressSpace);
  bool parseCFIEscapeValues(std::string& Values);
  bool parseCFIOperand(MachineOperand &Dest);
  bool parseIRBlock(BasicBlock *&BB, const Function &F);
  bool parseBlockAddressOperand(MachineOperand &Dest);
  bool parseIntrinsicOperand(MachineOperand &Dest);
  bool parsePredicateOperand(MachineOperand &Dest);
  bool parseShuffleMaskOperand(MachineOperand &Dest);
  bool parseTargetIndexOperand(MachineOperand &Dest);
  bool parseDbgInstrRefOperand(MachineOperand &Dest);
  bool parseCustomRegisterMaskOperand(MachineOperand &Dest);
  bool parseLiveoutRegisterMaskOperand(MachineOperand &Dest);
  bool parseMachineOperand(const unsigned OpCode, const unsigned OpIdx,
                           MachineOperand &Dest,
                           std::optional<unsigned> &TiedDefIdx);
  bool parseMachineOperandAndTargetFlags(const unsigned OpCode,
                                         const unsigned OpIdx,
                                         MachineOperand &Dest,
                                         std::optional<unsigned> &TiedDefIdx);
  bool parseOffset(int64_t &Offset);
  bool parseIRBlockAddressTaken(BasicBlock *&BB);
  bool parseAlignment(uint64_t &Alignment);
  bool parseAddrspace(unsigned &Addrspace);
  bool parseSectionID(std::optional<MBBSectionID> &SID);
  bool parseBBID(std::optional<UniqueBBID> &BBID);
  bool parseCallFrameSize(unsigned &CallFrameSize);
  bool parseOperandsOffset(MachineOperand &Op);
  bool parseIRValue(const Value *&V);
  bool parseMemoryOperandFlag(MachineMemOperand::Flags &Flags);
  bool parseMemoryPseudoSourceValue(const PseudoSourceValue *&PSV);
  bool parseMachinePointerInfo(MachinePointerInfo &Dest);
  bool parseOptionalScope(LLVMContext &Context, SyncScope::ID &SSID);
  bool parseOptionalAtomicOrdering(AtomicOrdering &Order);
  bool parseMachineMemoryOperand(MachineMemOperand *&Dest);
  bool parsePreOrPostInstrSymbol(MCSymbol *&Symbol);
  bool parseHeapAllocMarker(MDNode *&Node);
  bool parsePCSections(MDNode *&Node);

  bool parseTargetImmMnemonic(const unsigned OpCode, const unsigned OpIdx,
                              MachineOperand &Dest, const MIRFormatter &MF);

private:
  /// Convert the integer literal in the current token into an unsigned integer.
  ///
  /// Return true if an error occurred.
  bool getUnsigned(unsigned &Result);

  /// Convert the integer literal in the current token into an uint64.
  ///
  /// Return true if an error occurred.
  bool getUint64(uint64_t &Result);

  /// Convert the hexadecimal literal in the current token into an unsigned
  ///  APInt with a minimum bitwidth required to represent the value.
  ///
  /// Return true if the literal does not represent an integer value.
  bool getHexUint(APInt &Result);

  /// If the current token is of the given kind, consume it and return false.
  /// Otherwise report an error and return true.
  bool expectAndConsume(MIToken::TokenKind TokenKind);

  /// If the current token is of the given kind, consume it and return true.
  /// Otherwise return false.
  bool consumeIfPresent(MIToken::TokenKind TokenKind);

  bool parseInstruction(unsigned &OpCode, unsigned &Flags);

  bool assignRegisterTies(MachineInstr &MI,
                          ArrayRef<ParsedMachineOperand> Operands);

  bool verifyImplicitOperands(ArrayRef<ParsedMachineOperand> Operands,
                              const MCInstrDesc &MCID);

  const BasicBlock *getIRBlock(unsigned Slot);
  const BasicBlock *getIRBlock(unsigned Slot, const Function &F);

  /// Get or create an MCSymbol for a given name.
  MCSymbol *getOrCreateMCSymbol(StringRef Name);

  /// parseStringConstant
  ///   ::= StringConstant
  bool parseStringConstant(std::string &Result);

  /// Map the location in the MI string to the corresponding location specified
  /// in `SourceRange`.
  SMLoc mapSMLoc(StringRef::iterator Loc);
};

} // end anonymous namespace

MIParser::MIParser(PerFunctionMIParsingState &PFS, SMDiagnostic &Error,
                   StringRef Source)
    : MF(PFS.MF), Error(Error), Source(Source), CurrentSource(Source), PFS(PFS)
{}

MIParser::MIParser(PerFunctionMIParsingState &PFS, SMDiagnostic &Error,
                   StringRef Source, SMRange SourceRange)
    : MF(PFS.MF), Error(Error), Source(Source), CurrentSource(Source),
      SourceRange(SourceRange), PFS(PFS) {}

void MIParser::lex(unsigned SkipChar) {
  CurrentSource = lexMIToken(
      CurrentSource.slice(SkipChar, StringRef::npos), Token,
      [this](StringRef::iterator Loc, const Twine &Msg) { error(Loc, Msg); });
}

bool MIParser::error(const Twine &Msg) { return error(Token.location(), Msg); }

bool MIParser::error(StringRef::iterator Loc, const Twine &Msg) {
  const SourceMgr &SM = *PFS.SM;
  assert(Loc >= Source.data() && Loc <= (Source.data() + Source.size()));
  const MemoryBuffer &Buffer = *SM.getMemoryBuffer(SM.getMainFileID());
  if (Loc >= Buffer.getBufferStart() && Loc <= Buffer.getBufferEnd()) {
    // Create an ordinary diagnostic when the source manager's buffer is the
    // source string.
    Error = SM.GetMessage(SMLoc::getFromPointer(Loc), SourceMgr::DK_Error, Msg);
    return true;
  }
  // Create a diagnostic for a YAML string literal.
  Error = SMDiagnostic(SM, SMLoc(), Buffer.getBufferIdentifier(), 1,
                       Loc - Source.data(), SourceMgr::DK_Error, Msg.str(),
                       Source, std::nullopt, std::nullopt);
  return true;
}

SMLoc MIParser::mapSMLoc(StringRef::iterator Loc) {
  assert(SourceRange.isValid() && "Invalid source range");
  assert(Loc >= Source.data() && Loc <= (Source.data() + Source.size()));
  return SMLoc::getFromPointer(SourceRange.Start.getPointer() +
                               (Loc - Source.data()));
}

typedef function_ref<bool(StringRef::iterator Loc, const Twine &)>
    ErrorCallbackType;

static const char *toString(MIToken::TokenKind TokenKind) {
  switch (TokenKind) {
  case MIToken::comma:
    return "','";
  case MIToken::equal:
    return "'='";
  case MIToken::colon:
    return "':'";
  case MIToken::lparen:
    return "'('";
  case MIToken::rparen:
    return "')'";
  default:
    return "<unknown token>";
  }
}

bool MIParser::expectAndConsume(MIToken::TokenKind TokenKind) {
  if (Token.isNot(TokenKind))
    return error(Twine("expected ") + toString(TokenKind));
  lex();
  return false;
}

bool MIParser::consumeIfPresent(MIToken::TokenKind TokenKind) {
  if (Token.isNot(TokenKind))
    return false;
  lex();
  return true;
}

// Parse Machine Basic Block Section ID.
bool MIParser::parseSectionID(std::optional<MBBSectionID> &SID) {
  assert(Token.is(MIToken::kw_bbsections));
  lex();
  if (Token.is(MIToken::IntegerLiteral)) {
    unsigned Value = 0;
    if (getUnsigned(Value))
      return error("Unknown Section ID");
    SID = MBBSectionID{Value};
  } else {
    const StringRef &S = Token.stringValue();
    if (S == "Exception")
      SID = MBBSectionID::ExceptionSectionID;
    else if (S == "Cold")
      SID = MBBSectionID::ColdSectionID;
    else
      return error("Unknown Section ID");
  }
  lex();
  return false;
}

// Parse Machine Basic Block ID.
bool MIParser::parseBBID(std::optional<UniqueBBID> &BBID) {
  assert(Token.is(MIToken::kw_bb_id));
  lex();
  unsigned BaseID = 0;
  unsigned CloneID = 0;
  if (getUnsigned(BaseID))
    return error("Unknown BB ID");
  lex();
  if (Token.is(MIToken::IntegerLiteral)) {
    if (getUnsigned(CloneID))
      return error("Unknown Clone ID");
    lex();
  }
  BBID = {BaseID, CloneID};
  return false;
}

// Parse basic block call frame size.
bool MIParser::parseCallFrameSize(unsigned &CallFrameSize) {
  assert(Token.is(MIToken::kw_call_frame_size));
  lex();
  unsigned Value = 0;
  if (getUnsigned(Value))
    return error("Unknown call frame size");
  CallFrameSize = Value;
  lex();
  return false;
}

bool MIParser::parseBasicBlockDefinition(
    DenseMap<unsigned, MachineBasicBlock *> &MBBSlots) {
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  unsigned ID = 0;
  if (getUnsigned(ID))
    return true;
  auto Loc = Token.location();
  auto Name = Token.stringValue();
  lex();
  bool MachineBlockAddressTaken = false;
  BasicBlock *AddressTakenIRBlock = nullptr;
  bool IsLandingPad = false;
  bool IsInlineAsmBrIndirectTarget = false;
  bool IsEHFuncletEntry = false;
  std::optional<MBBSectionID> SectionID;
  uint64_t Alignment = 0;
  std::optional<UniqueBBID> BBID;
  unsigned CallFrameSize = 0;
  BasicBlock *BB = nullptr;
  if (consumeIfPresent(MIToken::lparen)) {
    do {
      // TODO: Report an error when multiple same attributes are specified.
      switch (Token.kind()) {
      case MIToken::kw_machine_block_address_taken:
        MachineBlockAddressTaken = true;
        lex();
        break;
      case MIToken::kw_ir_block_address_taken:
        if (parseIRBlockAddressTaken(AddressTakenIRBlock))
          return true;
        break;
      case MIToken::kw_landing_pad:
        IsLandingPad = true;
        lex();
        break;
      case MIToken::kw_inlineasm_br_indirect_target:
        IsInlineAsmBrIndirectTarget = true;
        lex();
        break;
      case MIToken::kw_ehfunclet_entry:
        IsEHFuncletEntry = true;
        lex();
        break;
      case MIToken::kw_align:
        if (parseAlignment(Alignment))
          return true;
        break;
      case MIToken::IRBlock:
      case MIToken::NamedIRBlock:
        // TODO: Report an error when both name and ir block are specified.
        if (parseIRBlock(BB, MF.getFunction()))
          return true;
        lex();
        break;
      case MIToken::kw_bbsections:
        if (parseSectionID(SectionID))
          return true;
        break;
      case MIToken::kw_bb_id:
        if (parseBBID(BBID))
          return true;
        break;
      case MIToken::kw_call_frame_size:
        if (parseCallFrameSize(CallFrameSize))
          return true;
        break;
      default:
        break;
      }
    } while (consumeIfPresent(MIToken::comma));
    if (expectAndConsume(MIToken::rparen))
      return true;
  }
  if (expectAndConsume(MIToken::colon))
    return true;

  if (!Name.empty()) {
    BB = dyn_cast_or_null<BasicBlock>(
        MF.getFunction().getValueSymbolTable()->lookup(Name));
    if (!BB)
      return error(Loc, Twine("basic block '") + Name +
                            "' is not defined in the function '" +
                            MF.getName() + "'");
  }
  auto *MBB = MF.CreateMachineBasicBlock(BB);
  MF.insert(MF.end(), MBB);
  bool WasInserted = MBBSlots.insert(std::make_pair(ID, MBB)).second;
  if (!WasInserted)
    return error(Loc, Twine("redefinition of machine basic block with id #") +
                          Twine(ID));
  if (Alignment)
    MBB->setAlignment(Align(Alignment));
  if (MachineBlockAddressTaken)
    MBB->setMachineBlockAddressTaken();
  if (AddressTakenIRBlock)
    MBB->setAddressTakenIRBlock(AddressTakenIRBlock);
  MBB->setIsEHPad(IsLandingPad);
  MBB->setIsInlineAsmBrIndirectTarget(IsInlineAsmBrIndirectTarget);
  MBB->setIsEHFuncletEntry(IsEHFuncletEntry);
  if (SectionID) {
    MBB->setSectionID(*SectionID);
    MF.setBBSectionsType(BasicBlockSection::List);
  }
  if (BBID.has_value()) {
    // BBSectionsType is set to `List` if any basic blocks has `SectionID`.
    // Here, we set it to `Labels` if it hasn't been set above.
    if (!MF.hasBBSections())
      MF.setBBSectionsType(BasicBlockSection::Labels);
    MBB->setBBID(BBID.value());
  }
  MBB->setCallFrameSize(CallFrameSize);
  return false;
}

bool MIParser::parseBasicBlockDefinitions(
    DenseMap<unsigned, MachineBasicBlock *> &MBBSlots) {
  lex();
  // Skip until the first machine basic block.
  while (Token.is(MIToken::Newline))
    lex();
  if (Token.isErrorOrEOF())
    return Token.isError();
  if (Token.isNot(MIToken::MachineBasicBlockLabel))
    return error("expected a basic block definition before instructions");
  unsigned BraceDepth = 0;
  do {
    if (parseBasicBlockDefinition(MBBSlots))
      return true;
    bool IsAfterNewline = false;
    // Skip until the next machine basic block.
    while (true) {
      if ((Token.is(MIToken::MachineBasicBlockLabel) && IsAfterNewline) ||
          Token.isErrorOrEOF())
        break;
      else if (Token.is(MIToken::MachineBasicBlockLabel))
        return error("basic block definition should be located at the start of "
                     "the line");
      else if (consumeIfPresent(MIToken::Newline)) {
        IsAfterNewline = true;
        continue;
      }
      IsAfterNewline = false;
      if (Token.is(MIToken::lbrace))
        ++BraceDepth;
      if (Token.is(MIToken::rbrace)) {
        if (!BraceDepth)
          return error("extraneous closing brace ('}')");
        --BraceDepth;
      }
      lex();
    }
    // Verify that we closed all of the '{' at the end of a file or a block.
    if (!Token.isError() && BraceDepth)
      return error("expected '}'"); // FIXME: Report a note that shows '{'.
  } while (!Token.isErrorOrEOF());
  return Token.isError();
}

bool MIParser::parseBasicBlockLiveins(MachineBasicBlock &MBB) {
  assert(Token.is(MIToken::kw_liveins));
  lex();
  if (expectAndConsume(MIToken::colon))
    return true;
  if (Token.isNewlineOrEOF()) // Allow an empty list of liveins.
    return false;
  do {
    if (Token.isNot(MIToken::NamedRegister))
      return error("expected a named register");
    Register Reg;
    if (parseNamedRegister(Reg))
      return true;
    lex();
    LaneBitmask Mask = LaneBitmask::getAll();
    if (consumeIfPresent(MIToken::colon)) {
      // Parse lane mask.
      if (Token.isNot(MIToken::IntegerLiteral) &&
          Token.isNot(MIToken::HexLiteral))
        return error("expected a lane mask");
      static_assert(sizeof(LaneBitmask::Type) == sizeof(uint64_t),
                    "Use correct get-function for lane mask");
      LaneBitmask::Type V;
      if (getUint64(V))
        return error("invalid lane mask value");
      Mask = LaneBitmask(V);
      lex();
    }
    MBB.addLiveIn(Reg, Mask);
  } while (consumeIfPresent(MIToken::comma));
  return false;
}

bool MIParser::parseBasicBlockSuccessors(MachineBasicBlock &MBB) {
  assert(Token.is(MIToken::kw_successors));
  lex();
  if (expectAndConsume(MIToken::colon))
    return true;
  if (Token.isNewlineOrEOF()) // Allow an empty list of successors.
    return false;
  do {
    if (Token.isNot(MIToken::MachineBasicBlock))
      return error("expected a machine basic block reference");
    MachineBasicBlock *SuccMBB = nullptr;
    if (parseMBBReference(SuccMBB))
      return true;
    lex();
    unsigned Weight = 0;
    if (consumeIfPresent(MIToken::lparen)) {
      if (Token.isNot(MIToken::IntegerLiteral) &&
          Token.isNot(MIToken::HexLiteral))
        return error("expected an integer literal after '('");
      if (getUnsigned(Weight))
        return true;
      lex();
      if (expectAndConsume(MIToken::rparen))
        return true;
    }
    MBB.addSuccessor(SuccMBB, BranchProbability::getRaw(Weight));
  } while (consumeIfPresent(MIToken::comma));
  MBB.normalizeSuccProbs();
  return false;
}

bool MIParser::parseBasicBlock(MachineBasicBlock &MBB,
                               MachineBasicBlock *&AddFalthroughFrom) {
  // Skip the definition.
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  lex();
  if (consumeIfPresent(MIToken::lparen)) {
    while (Token.isNot(MIToken::rparen) && !Token.isErrorOrEOF())
      lex();
    consumeIfPresent(MIToken::rparen);
  }
  consumeIfPresent(MIToken::colon);

  // Parse the liveins and successors.
  // N.B: Multiple lists of successors and liveins are allowed and they're
  // merged into one.
  // Example:
  //   liveins: $edi
  //   liveins: $esi
  //
  // is equivalent to
  //   liveins: $edi, $esi
  bool ExplicitSuccessors = false;
  while (true) {
    if (Token.is(MIToken::kw_successors)) {
      if (parseBasicBlockSuccessors(MBB))
        return true;
      ExplicitSuccessors = true;
    } else if (Token.is(MIToken::kw_liveins)) {
      if (parseBasicBlockLiveins(MBB))
        return true;
    } else if (consumeIfPresent(MIToken::Newline)) {
      continue;
    } else
      break;
    if (!Token.isNewlineOrEOF())
      return error("expected line break at the end of a list");
    lex();
  }

  // Parse the instructions.
  bool IsInBundle = false;
  MachineInstr *PrevMI = nullptr;
  while (!Token.is(MIToken::MachineBasicBlockLabel) &&
         !Token.is(MIToken::Eof)) {
    if (consumeIfPresent(MIToken::Newline))
      continue;
    if (consumeIfPresent(MIToken::rbrace)) {
      // The first parsing pass should verify that all closing '}' have an
      // opening '{'.
      assert(IsInBundle);
      IsInBundle = false;
      continue;
    }
    MachineInstr *MI = nullptr;
    if (parse(MI))
      return true;
    MBB.insert(MBB.end(), MI);
    if (IsInBundle) {
      PrevMI->setFlag(MachineInstr::BundledSucc);
      MI->setFlag(MachineInstr::BundledPred);
    }
    PrevMI = MI;
    if (Token.is(MIToken::lbrace)) {
      if (IsInBundle)
        return error("nested instruction bundles are not allowed");
      lex();
      // This instruction is the start of the bundle.
      MI->setFlag(MachineInstr::BundledSucc);
      IsInBundle = true;
      if (!Token.is(MIToken::Newline))
        // The next instruction can be on the same line.
        continue;
    }
    assert(Token.isNewlineOrEOF() && "MI is not fully parsed");
    lex();
  }

  // Construct successor list by searching for basic block machine operands.
  if (!ExplicitSuccessors) {
    SmallVector<MachineBasicBlock*,4> Successors;
    bool IsFallthrough;
    guessSuccessors(MBB, Successors, IsFallthrough);
    for (MachineBasicBlock *Succ : Successors)
      MBB.addSuccessor(Succ);

    if (IsFallthrough) {
      AddFalthroughFrom = &MBB;
    } else {
      MBB.normalizeSuccProbs();
    }
  }

  return false;
}

bool MIParser::parseBasicBlocks() {
  lex();
  // Skip until the first machine basic block.
  while (Token.is(MIToken::Newline))
    lex();
  if (Token.isErrorOrEOF())
    return Token.isError();
  // The first parsing pass should have verified that this token is a MBB label
  // in the 'parseBasicBlockDefinitions' method.
  assert(Token.is(MIToken::MachineBasicBlockLabel));
  MachineBasicBlock *AddFalthroughFrom = nullptr;
  do {
    MachineBasicBlock *MBB = nullptr;
    if (parseMBBReference(MBB))
      return true;
    if (AddFalthroughFrom) {
      if (!AddFalthroughFrom->isSuccessor(MBB))
        AddFalthroughFrom->addSuccessor(MBB);
      AddFalthroughFrom->normalizeSuccProbs();
      AddFalthroughFrom = nullptr;
    }
    if (parseBasicBlock(*MBB, AddFalthroughFrom))
      return true;
    // The method 'parseBasicBlock' should parse the whole block until the next
    // block or the end of file.
    assert(Token.is(MIToken::MachineBasicBlockLabel) || Token.is(MIToken::Eof));
  } while (Token.isNot(MIToken::Eof));
  return false;
}

bool MIParser::parse(MachineInstr *&MI) {
  // Parse any register operands before '='
  MachineOperand MO = MachineOperand::CreateImm(0);
  SmallVector<ParsedMachineOperand, 8> Operands;
  while (Token.isRegister() || Token.isRegisterFlag()) {
    auto Loc = Token.location();
    std::optional<unsigned> TiedDefIdx;
    if (parseRegisterOperand(MO, TiedDefIdx, /*IsDef=*/true))
      return true;
    Operands.push_back(
        ParsedMachineOperand(MO, Loc, Token.location(), TiedDefIdx));
    if (Token.isNot(MIToken::comma))
      break;
    lex();
  }
  if (!Operands.empty() && expectAndConsume(MIToken::equal))
    return true;

  unsigned OpCode, Flags = 0;
  if (Token.isError() || parseInstruction(OpCode, Flags))
    return true;

  // Parse the remaining machine operands.
  while (!Token.isNewlineOrEOF() && Token.isNot(MIToken::kw_pre_instr_symbol) &&
         Token.isNot(MIToken::kw_post_instr_symbol) &&
         Token.isNot(MIToken::kw_heap_alloc_marker) &&
         Token.isNot(MIToken::kw_pcsections) &&
         Token.isNot(MIToken::kw_cfi_type) &&
         Token.isNot(MIToken::kw_debug_location) &&
         Token.isNot(MIToken::kw_debug_instr_number) &&
         Token.isNot(MIToken::coloncolon) && Token.isNot(MIToken::lbrace)) {
    auto Loc = Token.location();
    std::optional<unsigned> TiedDefIdx;
    if (parseMachineOperandAndTargetFlags(OpCode, Operands.size(), MO, TiedDefIdx))
      return true;
    Operands.push_back(
        ParsedMachineOperand(MO, Loc, Token.location(), TiedDefIdx));
    if (Token.isNewlineOrEOF() || Token.is(MIToken::coloncolon) ||
        Token.is(MIToken::lbrace))
      break;
    if (Token.isNot(MIToken::comma))
      return error("expected ',' before the next machine operand");
    lex();
  }

  MCSymbol *PreInstrSymbol = nullptr;
  if (Token.is(MIToken::kw_pre_instr_symbol))
    if (parsePreOrPostInstrSymbol(PreInstrSymbol))
      return true;
  MCSymbol *PostInstrSymbol = nullptr;
  if (Token.is(MIToken::kw_post_instr_symbol))
    if (parsePreOrPostInstrSymbol(PostInstrSymbol))
      return true;
  MDNode *HeapAllocMarker = nullptr;
  if (Token.is(MIToken::kw_heap_alloc_marker))
    if (parseHeapAllocMarker(HeapAllocMarker))
      return true;
  MDNode *PCSections = nullptr;
  if (Token.is(MIToken::kw_pcsections))
    if (parsePCSections(PCSections))
      return true;

  unsigned CFIType = 0;
  if (Token.is(MIToken::kw_cfi_type)) {
    lex();
    if (Token.isNot(MIToken::IntegerLiteral))
      return error("expected an integer literal after 'cfi-type'");
    // getUnsigned is sufficient for 32-bit integers.
    if (getUnsigned(CFIType))
      return true;
    lex();
    // Lex past trailing comma if present.
    if (Token.is(MIToken::comma))
      lex();
  }

  unsigned InstrNum = 0;
  if (Token.is(MIToken::kw_debug_instr_number)) {
    lex();
    if (Token.isNot(MIToken::IntegerLiteral))
      return error("expected an integer literal after 'debug-instr-number'");
    if (getUnsigned(InstrNum))
      return true;
    lex();
    // Lex past trailing comma if present.
    if (Token.is(MIToken::comma))
      lex();
  }

  DebugLoc DebugLocation;
  if (Token.is(MIToken::kw_debug_location)) {
    lex();
    MDNode *Node = nullptr;
    if (Token.is(MIToken::exclaim)) {
      if (parseMDNode(Node))
        return true;
    } else if (Token.is(MIToken::md_dilocation)) {
      if (parseDILocation(Node))
        return true;
    } else
      return error("expected a metadata node after 'debug-location'");
    if (!isa<DILocation>(Node))
      return error("referenced metadata is not a DILocation");
    DebugLocation = DebugLoc(Node);
  }

  // Parse the machine memory operands.
  SmallVector<MachineMemOperand *, 2> MemOperands;
  if (Token.is(MIToken::coloncolon)) {
    lex();
    while (!Token.isNewlineOrEOF()) {
      MachineMemOperand *MemOp = nullptr;
      if (parseMachineMemoryOperand(MemOp))
        return true;
      MemOperands.push_back(MemOp);
      if (Token.isNewlineOrEOF())
        break;
      if (Token.isNot(MIToken::comma))
        return error("expected ',' before the next machine memory operand");
      lex();
    }
  }

  const auto &MCID = MF.getSubtarget().getInstrInfo()->get(OpCode);
  if (!MCID.isVariadic()) {
    // FIXME: Move the implicit operand verification to the machine verifier.
    if (verifyImplicitOperands(Operands, MCID))
      return true;
  }

  MI = MF.CreateMachineInstr(MCID, DebugLocation, /*NoImplicit=*/true);
  MI->setFlags(Flags);

  // Don't check the operands make sense, let the verifier catch any
  // improprieties.
  for (const auto &Operand : Operands)
    MI->addOperand(MF, Operand.Operand);

  if (assignRegisterTies(*MI, Operands))
    return true;
  if (PreInstrSymbol)
    MI->setPreInstrSymbol(MF, PreInstrSymbol);
  if (PostInstrSymbol)
    MI->setPostInstrSymbol(MF, PostInstrSymbol);
  if (HeapAllocMarker)
    MI->setHeapAllocMarker(MF, HeapAllocMarker);
  if (PCSections)
    MI->setPCSections(MF, PCSections);
  if (CFIType)
    MI->setCFIType(MF, CFIType);
  if (!MemOperands.empty())
    MI->setMemRefs(MF, MemOperands);
  if (InstrNum)
    MI->setDebugInstrNum(InstrNum);
  return false;
}

bool MIParser::parseStandaloneMBB(MachineBasicBlock *&MBB) {
  lex();
  if (Token.isNot(MIToken::MachineBasicBlock))
    return error("expected a machine basic block reference");
  if (parseMBBReference(MBB))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error(
        "expected end of string after the machine basic block reference");
  return false;
}

bool MIParser::parseStandaloneNamedRegister(Register &Reg) {
  lex();
  if (Token.isNot(MIToken::NamedRegister))
    return error("expected a named register");
  if (parseNamedRegister(Reg))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the register reference");
  return false;
}

bool MIParser::parseStandaloneVirtualRegister(VRegInfo *&Info) {
  lex();
  if (Token.isNot(MIToken::VirtualRegister))
    return error("expected a virtual register");
  if (parseVirtualRegister(Info))
    return true;
  lex();
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the register reference");
  return false;
}

bool MIParser::parseStandaloneRegister(Register &Reg) {
  lex();
  if (Token.isNot(MIToken::NamedRegister) &&
      Token.isNot(MIToken::VirtualRegister))
    return error("expected either a named or virtual register");

  VRegInfo *Info;
  if (parseRegister(Reg, Info))
    return true;

  lex();
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the register reference");
  return false;
}

bool MIParser::parseStandaloneStackObject(int &FI) {
  lex();
  if (Token.isNot(MIToken::StackObject))
    return error("expected a stack object");
  if (parseStackFrameIndex(FI))
    return true;
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the stack object reference");
  return false;
}

bool MIParser::parseStandaloneMDNode(MDNode *&Node) {
  lex();
  if (Token.is(MIToken::exclaim)) {
    if (parseMDNode(Node))
      return true;
  } else if (Token.is(MIToken::md_diexpr)) {
    if (parseDIExpression(Node))
      return true;
  } else if (Token.is(MIToken::md_dilocation)) {
    if (parseDILocation(Node))
      return true;
  } else
    return error("expected a metadata node");
  if (Token.isNot(MIToken::Eof))
    return error("expected end of string after the metadata node");
  return false;
}

bool MIParser::parseMachineMetadata() {
  lex();
  if (Token.isNot(MIToken::exclaim))
    return error("expected a metadata node");

  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected metadata id after '!'");
  unsigned ID = 0;
  if (getUnsigned(ID))
    return true;
  lex();
  if (expectAndConsume(MIToken::equal))
    return true;
  bool IsDistinct = Token.is(MIToken::kw_distinct);
  if (IsDistinct)
    lex();
  if (Token.isNot(MIToken::exclaim))
    return error("expected a metadata node");
  lex();

  MDNode *MD;
  if (parseMDTuple(MD, IsDistinct))
    return true;

  auto FI = PFS.MachineForwardRefMDNodes.find(ID);
  if (FI != PFS.MachineForwardRefMDNodes.end()) {
    FI->second.first->replaceAllUsesWith(MD);
    PFS.MachineForwardRefMDNodes.erase(FI);

    assert(PFS.MachineMetadataNodes[ID] == MD && "Tracking VH didn't work");
  } else {
    if (PFS.MachineMetadataNodes.count(ID))
      return error("Metadata id is already used");
    PFS.MachineMetadataNodes[ID].reset(MD);
  }

  return false;
}

bool MIParser::parseMDTuple(MDNode *&MD, bool IsDistinct) {
  SmallVector<Metadata *, 16> Elts;
  if (parseMDNodeVector(Elts))
    return true;
  MD = (IsDistinct ? MDTuple::getDistinct
                   : MDTuple::get)(MF.getFunction().getContext(), Elts);
  return false;
}

bool MIParser::parseMDNodeVector(SmallVectorImpl<Metadata *> &Elts) {
  if (Token.isNot(MIToken::lbrace))
    return error("expected '{' here");
  lex();

  if (Token.is(MIToken::rbrace)) {
    lex();
    return false;
  }

  do {
    Metadata *MD;
    if (parseMetadata(MD))
      return true;

    Elts.push_back(MD);

    if (Token.isNot(MIToken::comma))
      break;
    lex();
  } while (true);

  if (Token.isNot(MIToken::rbrace))
    return error("expected end of metadata node");
  lex();

  return false;
}

// ::= !42
// ::= !"string"
bool MIParser::parseMetadata(Metadata *&MD) {
  if (Token.isNot(MIToken::exclaim))
    return error("expected '!' here");
  lex();

  if (Token.is(MIToken::StringConstant)) {
    std::string Str;
    if (parseStringConstant(Str))
      return true;
    MD = MDString::get(MF.getFunction().getContext(), Str);
    return false;
  }

  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected metadata id after '!'");

  SMLoc Loc = mapSMLoc(Token.location());

  unsigned ID = 0;
  if (getUnsigned(ID))
    return true;
  lex();

  auto NodeInfo = PFS.IRSlots.MetadataNodes.find(ID);
  if (NodeInfo != PFS.IRSlots.MetadataNodes.end()) {
    MD = NodeInfo->second.get();
    return false;
  }
  // Check machine metadata.
  NodeInfo = PFS.MachineMetadataNodes.find(ID);
  if (NodeInfo != PFS.MachineMetadataNodes.end()) {
    MD = NodeInfo->second.get();
    return false;
  }
  // Forward reference.
  auto &FwdRef = PFS.MachineForwardRefMDNodes[ID];
  FwdRef = std::make_pair(
      MDTuple::getTemporary(MF.getFunction().getContext(), std::nullopt), Loc);
  PFS.MachineMetadataNodes[ID].reset(FwdRef.first.get());
  MD = FwdRef.first.get();

  return false;
}

static const char *printImplicitRegisterFlag(const MachineOperand &MO) {
  assert(MO.isImplicit());
  return MO.isDef() ? "implicit-def" : "implicit";
}

static std::string getRegisterName(const TargetRegisterInfo *TRI,
                                   Register Reg) {
  assert(Reg.isPhysical() && "expected phys reg");
  return StringRef(TRI->getName(Reg)).lower();
}

/// Return true if the parsed machine operands contain a given machine operand.
static bool isImplicitOperandIn(const MachineOperand &ImplicitOperand,
                                ArrayRef<ParsedMachineOperand> Operands) {
  for (const auto &I : Operands) {
    if (ImplicitOperand.isIdenticalTo(I.Operand))
      return true;
  }
  return false;
}

bool MIParser::verifyImplicitOperands(ArrayRef<ParsedMachineOperand> Operands,
                                      const MCInstrDesc &MCID) {
  if (MCID.isCall())
    // We can't verify call instructions as they can contain arbitrary implicit
    // register and register mask operands.
    return false;

  // Gather all the expected implicit operands.
  SmallVector<MachineOperand, 4> ImplicitOperands;
  for (MCPhysReg ImpDef : MCID.implicit_defs())
    ImplicitOperands.push_back(MachineOperand::CreateReg(ImpDef, true, true));
  for (MCPhysReg ImpUse : MCID.implicit_uses())
    ImplicitOperands.push_back(MachineOperand::CreateReg(ImpUse, false, true));

  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  for (const auto &I : ImplicitOperands) {
    if (isImplicitOperandIn(I, Operands))
      continue;
    return error(Operands.empty() ? Token.location() : Operands.back().End,
                 Twine("missing implicit register operand '") +
                     printImplicitRegisterFlag(I) + " $" +
                     getRegisterName(TRI, I.getReg()) + "'");
  }
  return false;
}

bool MIParser::parseInstruction(unsigned &OpCode, unsigned &Flags) {
  // Allow frame and fast math flags for OPCODE
  // clang-format off
  while (Token.is(MIToken::kw_frame_setup) ||
         Token.is(MIToken::kw_frame_destroy) ||
         Token.is(MIToken::kw_nnan) ||
         Token.is(MIToken::kw_ninf) ||
         Token.is(MIToken::kw_nsz) ||
         Token.is(MIToken::kw_arcp) ||
         Token.is(MIToken::kw_contract) ||
         Token.is(MIToken::kw_afn) ||
         Token.is(MIToken::kw_reassoc) ||
         Token.is(MIToken::kw_nuw) ||
         Token.is(MIToken::kw_nsw) ||
         Token.is(MIToken::kw_exact) ||
         Token.is(MIToken::kw_nofpexcept) ||
         Token.is(MIToken::kw_noconvergent) ||
         Token.is(MIToken::kw_unpredictable) ||
         Token.is(MIToken::kw_nneg) ||
         Token.is(MIToken::kw_disjoint)) {
    // clang-format on
    // Mine frame and fast math flags
    if (Token.is(MIToken::kw_frame_setup))
      Flags |= MachineInstr::FrameSetup;
    if (Token.is(MIToken::kw_frame_destroy))
      Flags |= MachineInstr::FrameDestroy;
    if (Token.is(MIToken::kw_nnan))
      Flags |= MachineInstr::FmNoNans;
    if (Token.is(MIToken::kw_ninf))
      Flags |= MachineInstr::FmNoInfs;
    if (Token.is(MIToken::kw_nsz))
      Flags |= MachineInstr::FmNsz;
    if (Token.is(MIToken::kw_arcp))
      Flags |= MachineInstr::FmArcp;
    if (Token.is(MIToken::kw_contract))
      Flags |= MachineInstr::FmContract;
    if (Token.is(MIToken::kw_afn))
      Flags |= MachineInstr::FmAfn;
    if (Token.is(MIToken::kw_reassoc))
      Flags |= MachineInstr::FmReassoc;
    if (Token.is(MIToken::kw_nuw))
      Flags |= MachineInstr::NoUWrap;
    if (Token.is(MIToken::kw_nsw))
      Flags |= MachineInstr::NoSWrap;
    if (Token.is(MIToken::kw_exact))
      Flags |= MachineInstr::IsExact;
    if (Token.is(MIToken::kw_nofpexcept))
      Flags |= MachineInstr::NoFPExcept;
    if (Token.is(MIToken::kw_unpredictable))
      Flags |= MachineInstr::Unpredictable;
    if (Token.is(MIToken::kw_noconvergent))
      Flags |= MachineInstr::NoConvergent;
    if (Token.is(MIToken::kw_nneg))
      Flags |= MachineInstr::NonNeg;
    if (Token.is(MIToken::kw_disjoint))
      Flags |= MachineInstr::Disjoint;

    lex();
  }
  if (Token.isNot(MIToken::Identifier))
    return error("expected a machine instruction");
  StringRef InstrName = Token.stringValue();
  if (PFS.Target.parseInstrName(InstrName, OpCode))
    return error(Twine("unknown machine instruction name '") + InstrName + "'");
  lex();
  return false;
}

bool MIParser::parseNamedRegister(Register &Reg) {
  assert(Token.is(MIToken::NamedRegister) && "Needs NamedRegister token");
  StringRef Name = Token.stringValue();
  if (PFS.Target.getRegisterByName(Name, Reg))
    return error(Twine("unknown register name '") + Name + "'");
  return false;
}

bool MIParser::parseNamedVirtualRegister(VRegInfo *&Info) {
  assert(Token.is(MIToken::NamedVirtualRegister) && "Expected NamedVReg token");
  StringRef Name = Token.stringValue();
  // TODO: Check that the VReg name is not the same as a physical register name.
  //       If it is, then print a warning (when warnings are implemented).
  Info = &PFS.getVRegInfoNamed(Name);
  return false;
}

bool MIParser::parseVirtualRegister(VRegInfo *&Info) {
  if (Token.is(MIToken::NamedVirtualRegister))
    return parseNamedVirtualRegister(Info);
  assert(Token.is(MIToken::VirtualRegister) && "Needs VirtualRegister token");
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  Info = &PFS.getVRegInfo(ID);
  return false;
}

bool MIParser::parseRegister(Register &Reg, VRegInfo *&Info) {
  switch (Token.kind()) {
  case MIToken::underscore:
    Reg = 0;
    return false;
  case MIToken::NamedRegister:
    return parseNamedRegister(Reg);
  case MIToken::NamedVirtualRegister:
  case MIToken::VirtualRegister:
    if (parseVirtualRegister(Info))
      return true;
    Reg = Info->VReg;
    return false;
  // TODO: Parse other register kinds.
  default:
    llvm_unreachable("The current token should be a register");
  }
}

bool MIParser::parseRegisterClassOrBank(VRegInfo &RegInfo) {
  if (Token.isNot(MIToken::Identifier) && Token.isNot(MIToken::underscore))
    return error("expected '_', register class, or register bank name");
  StringRef::iterator Loc = Token.location();
  StringRef Name = Token.stringValue();

  // Was it a register class?
  const TargetRegisterClass *RC = PFS.Target.getRegClass(Name);
  if (RC) {
    lex();

    switch (RegInfo.Kind) {
    case VRegInfo::UNKNOWN:
    case VRegInfo::NORMAL:
      RegInfo.Kind = VRegInfo::NORMAL;
      if (RegInfo.Explicit && RegInfo.D.RC != RC) {
        const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
        return error(Loc, Twine("conflicting register classes, previously: ") +
                     Twine(TRI.getRegClassName(RegInfo.D.RC)));
      }
      RegInfo.D.RC = RC;
      RegInfo.Explicit = true;
      return false;

    case VRegInfo::GENERIC:
    case VRegInfo::REGBANK:
      return error(Loc, "register class specification on generic register");
    }
    llvm_unreachable("Unexpected register kind");
  }

  // Should be a register bank or a generic register.
  const RegisterBank *RegBank = nullptr;
  if (Name != "_") {
    RegBank = PFS.Target.getRegBank(Name);
    if (!RegBank)
      return error(Loc, "expected '_', register class, or register bank name");
  }

  lex();

  switch (RegInfo.Kind) {
  case VRegInfo::UNKNOWN:
  case VRegInfo::GENERIC:
  case VRegInfo::REGBANK:
    RegInfo.Kind = RegBank ? VRegInfo::REGBANK : VRegInfo::GENERIC;
    if (RegInfo.Explicit && RegInfo.D.RegBank != RegBank)
      return error(Loc, "conflicting generic register banks");
    RegInfo.D.RegBank = RegBank;
    RegInfo.Explicit = true;
    return false;

  case VRegInfo::NORMAL:
    return error(Loc, "register bank specification on normal register");
  }
  llvm_unreachable("Unexpected register kind");
}

bool MIParser::parseRegisterFlag(unsigned &Flags) {
  const unsigned OldFlags = Flags;
  switch (Token.kind()) {
  case MIToken::kw_implicit:
    Flags |= RegState::Implicit;
    break;
  case MIToken::kw_implicit_define:
    Flags |= RegState::ImplicitDefine;
    break;
  case MIToken::kw_def:
    Flags |= RegState::Define;
    break;
  case MIToken::kw_dead:
    Flags |= RegState::Dead;
    break;
  case MIToken::kw_killed:
    Flags |= RegState::Kill;
    break;
  case MIToken::kw_undef:
    Flags |= RegState::Undef;
    break;
  case MIToken::kw_internal:
    Flags |= RegState::InternalRead;
    break;
  case MIToken::kw_early_clobber:
    Flags |= RegState::EarlyClobber;
    break;
  case MIToken::kw_debug_use:
    Flags |= RegState::Debug;
    break;
  case MIToken::kw_renamable:
    Flags |= RegState::Renamable;
    break;
  default:
    llvm_unreachable("The current token should be a register flag");
  }
  if (OldFlags == Flags)
    // We know that the same flag is specified more than once when the flags
    // weren't modified.
    return error("duplicate '" + Token.stringValue() + "' register flag");
  lex();
  return false;
}

bool MIParser::parseSubRegisterIndex(unsigned &SubReg) {
  assert(Token.is(MIToken::dot));
  lex();
  if (Token.isNot(MIToken::Identifier))
    return error("expected a subregister index after '.'");
  auto Name = Token.stringValue();
  SubReg = PFS.Target.getSubRegIndex(Name);
  if (!SubReg)
    return error(Twine("use of unknown subregister index '") + Name + "'");
  lex();
  return false;
}

bool MIParser::parseRegisterTiedDefIndex(unsigned &TiedDefIdx) {
  if (!consumeIfPresent(MIToken::kw_tied_def))
    return true;
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected an integer literal after 'tied-def'");
  if (getUnsigned(TiedDefIdx))
    return true;
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  return false;
}

bool MIParser::assignRegisterTies(MachineInstr &MI,
                                  ArrayRef<ParsedMachineOperand> Operands) {
  SmallVector<std::pair<unsigned, unsigned>, 4> TiedRegisterPairs;
  for (unsigned I = 0, E = Operands.size(); I != E; ++I) {
    if (!Operands[I].TiedDefIdx)
      continue;
    // The parser ensures that this operand is a register use, so we just have
    // to check the tied-def operand.
    unsigned DefIdx = *Operands[I].TiedDefIdx;
    if (DefIdx >= E)
      return error(Operands[I].Begin,
                   Twine("use of invalid tied-def operand index '" +
                         Twine(DefIdx) + "'; instruction has only ") +
                       Twine(E) + " operands");
    const auto &DefOperand = Operands[DefIdx].Operand;
    if (!DefOperand.isReg() || !DefOperand.isDef())
      // FIXME: add note with the def operand.
      return error(Operands[I].Begin,
                   Twine("use of invalid tied-def operand index '") +
                       Twine(DefIdx) + "'; the operand #" + Twine(DefIdx) +
                       " isn't a defined register");
    // Check that the tied-def operand wasn't tied elsewhere.
    for (const auto &TiedPair : TiedRegisterPairs) {
      if (TiedPair.first == DefIdx)
        return error(Operands[I].Begin,
                     Twine("the tied-def operand #") + Twine(DefIdx) +
                         " is already tied with another register operand");
    }
    TiedRegisterPairs.push_back(std::make_pair(DefIdx, I));
  }
  // FIXME: Verify that for non INLINEASM instructions, the def and use tied
  // indices must be less than tied max.
  for (const auto &TiedPair : TiedRegisterPairs)
    MI.tieOperands(TiedPair.first, TiedPair.second);
  return false;
}

bool MIParser::parseRegisterOperand(MachineOperand &Dest,
                                    std::optional<unsigned> &TiedDefIdx,
                                    bool IsDef) {
  unsigned Flags = IsDef ? RegState::Define : 0;
  while (Token.isRegisterFlag()) {
    if (parseRegisterFlag(Flags))
      return true;
  }
  if (!Token.isRegister())
    return error("expected a register after register flags");
  Register Reg;
  VRegInfo *RegInfo;
  if (parseRegister(Reg, RegInfo))
    return true;
  lex();
  unsigned SubReg = 0;
  if (Token.is(MIToken::dot)) {
    if (parseSubRegisterIndex(SubReg))
      return true;
    if (!Reg.isVirtual())
      return error("subregister index expects a virtual register");
  }
  if (Token.is(MIToken::colon)) {
    if (!Reg.isVirtual())
      return error("register class specification expects a virtual register");
    lex();
    if (parseRegisterClassOrBank(*RegInfo))
        return true;
  }
  MachineRegisterInfo &MRI = MF.getRegInfo();
  if ((Flags & RegState::Define) == 0) {
    if (consumeIfPresent(MIToken::lparen)) {
      unsigned Idx;
      if (!parseRegisterTiedDefIndex(Idx))
        TiedDefIdx = Idx;
      else {
        // Try a redundant low-level type.
        LLT Ty;
        if (parseLowLevelType(Token.location(), Ty))
          return error("expected tied-def or low-level type after '('");

        if (expectAndConsume(MIToken::rparen))
          return true;

        if (MRI.getType(Reg).isValid() && MRI.getType(Reg) != Ty)
          return error("inconsistent type for generic virtual register");

        MRI.setRegClassOrRegBank(Reg, static_cast<RegisterBank *>(nullptr));
        MRI.setType(Reg, Ty);
      }
    }
  } else if (consumeIfPresent(MIToken::lparen)) {
    // Virtual registers may have a tpe with GlobalISel.
    if (!Reg.isVirtual())
      return error("unexpected type on physical register");

    LLT Ty;
    if (parseLowLevelType(Token.location(), Ty))
      return true;

    if (expectAndConsume(MIToken::rparen))
      return true;

    if (MRI.getType(Reg).isValid() && MRI.getType(Reg) != Ty)
      return error("inconsistent type for generic virtual register");

    MRI.setRegClassOrRegBank(Reg, static_cast<RegisterBank *>(nullptr));
    MRI.setType(Reg, Ty);
  } else if (Reg.isVirtual()) {
    // Generic virtual registers must have a type.
    // If we end up here this means the type hasn't been specified and
    // this is bad!
    if (RegInfo->Kind == VRegInfo::GENERIC ||
        RegInfo->Kind == VRegInfo::REGBANK)
      return error("generic virtual registers must have a type");
  }

  if (Flags & RegState::Define) {
    if (Flags & RegState::Kill)
      return error("cannot have a killed def operand");
  } else {
    if (Flags & RegState::Dead)
      return error("cannot have a dead use operand");
  }

  Dest = MachineOperand::CreateReg(
      Reg, Flags & RegState::Define, Flags & RegState::Implicit,
      Flags & RegState::Kill, Flags & RegState::Dead, Flags & RegState::Undef,
      Flags & RegState::EarlyClobber, SubReg, Flags & RegState::Debug,
      Flags & RegState::InternalRead, Flags & RegState::Renamable);

  return false;
}

bool MIParser::parseImmediateOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::IntegerLiteral));
  const APSInt &Int = Token.integerValue();
  if (auto SImm = Int.trySExtValue(); Int.isSigned() && SImm.has_value())
    Dest = MachineOperand::CreateImm(*SImm);
  else if (auto UImm = Int.tryZExtValue(); !Int.isSigned() && UImm.has_value())
    Dest = MachineOperand::CreateImm(*UImm);
  else
    return error("integer literal is too large to be an immediate operand");
  lex();
  return false;
}

bool MIParser::parseTargetImmMnemonic(const unsigned OpCode,
                                      const unsigned OpIdx,
                                      MachineOperand &Dest,
                                      const MIRFormatter &MF) {
  assert(Token.is(MIToken::dot));
  auto Loc = Token.location(); // record start position
  size_t Len = 1;              // for "."
  lex();

  // Handle the case that mnemonic starts with number.
  if (Token.is(MIToken::IntegerLiteral)) {
    Len += Token.range().size();
    lex();
  }

  StringRef Src;
  if (Token.is(MIToken::comma))
    Src = StringRef(Loc, Len);
  else {
    assert(Token.is(MIToken::Identifier));
    Src = StringRef(Loc, Len + Token.stringValue().size());
  }
  int64_t Val;
  if (MF.parseImmMnemonic(OpCode, OpIdx, Src, Val,
                          [this](StringRef::iterator Loc, const Twine &Msg)
                              -> bool { return error(Loc, Msg); }))
    return true;

  Dest = MachineOperand::CreateImm(Val);
  if (!Token.is(MIToken::comma))
    lex();
  return false;
}

static bool parseIRConstant(StringRef::iterator Loc, StringRef StringValue,
                            PerFunctionMIParsingState &PFS, const Constant *&C,
                            ErrorCallbackType ErrCB) {
  auto Source = StringValue.str(); // The source has to be null terminated.
  SMDiagnostic Err;
  C = parseConstantValue(Source, Err, *PFS.MF.getFunction().getParent(),
                         &PFS.IRSlots);
  if (!C)
    return ErrCB(Loc + Err.getColumnNo(), Err.getMessage());
  return false;
}

bool MIParser::parseIRConstant(StringRef::iterator Loc, StringRef StringValue,
                               const Constant *&C) {
  return ::parseIRConstant(
      Loc, StringValue, PFS, C,
      [this](StringRef::iterator Loc, const Twine &Msg) -> bool {
        return error(Loc, Msg);
      });
}

bool MIParser::parseIRConstant(StringRef::iterator Loc, const Constant *&C) {
  if (parseIRConstant(Loc, StringRef(Loc, Token.range().end() - Loc), C))
    return true;
  lex();
  return false;
}

// See LLT implementation for bit size limits.
static bool verifyScalarSize(uint64_t Size) {
  return Size != 0 && isUInt<16>(Size);
}

static bool verifyVectorElementCount(uint64_t NumElts) {
  return NumElts != 0 && isUInt<16>(NumElts);
}

static bool verifyAddrSpace(uint64_t AddrSpace) {
  return isUInt<24>(AddrSpace);
}

bool MIParser::parseLowLevelType(StringRef::iterator Loc, LLT &Ty) {
  if (Token.range().front() == 's' || Token.range().front() == 'p') {
    StringRef SizeStr = Token.range().drop_front();
    if (SizeStr.size() == 0 || !llvm::all_of(SizeStr, isdigit))
      return error("expected integers after 's'/'p' type character");
  }

  if (Token.range().front() == 's') {
    auto ScalarSize = APSInt(Token.range().drop_front()).getZExtValue();
    if (ScalarSize) {
      if (!verifyScalarSize(ScalarSize))
        return error("invalid size for scalar type");
      Ty = LLT::scalar(ScalarSize);
    } else {
      Ty = LLT::token();
    }
    lex();
    return false;
  } else if (Token.range().front() == 'p') {
    const DataLayout &DL = MF.getDataLayout();
    uint64_t AS = APSInt(Token.range().drop_front()).getZExtValue();
    if (!verifyAddrSpace(AS))
      return error("invalid address space number");

    Ty = LLT::pointer(AS, DL.getPointerSizeInBits(AS));
    lex();
    return false;
  }

  // Now we're looking for a vector.
  if (Token.isNot(MIToken::less))
    return error(Loc, "expected sN, pA, <M x sN>, <M x pA>, <vscale x M x sN>, "
                      "or <vscale x M x pA> for GlobalISel type");
  lex();

  bool HasVScale =
      Token.is(MIToken::Identifier) && Token.stringValue() == "vscale";
  if (HasVScale) {
    lex();
    if (Token.isNot(MIToken::Identifier) || Token.stringValue() != "x")
      return error("expected <vscale x M x sN> or <vscale x M x pA>");
    lex();
  }

  auto GetError = [this, &HasVScale, Loc]() {
    if (HasVScale)
      return error(
          Loc, "expected <vscale x M x sN> or <vscale M x pA> for vector type");
    return error(Loc, "expected <M x sN> or <M x pA> for vector type");
  };

  if (Token.isNot(MIToken::IntegerLiteral))
    return GetError();
  uint64_t NumElements = Token.integerValue().getZExtValue();
  if (!verifyVectorElementCount(NumElements))
    return error("invalid number of vector elements");

  lex();

  if (Token.isNot(MIToken::Identifier) || Token.stringValue() != "x")
    return GetError();
  lex();

  if (Token.range().front() != 's' && Token.range().front() != 'p')
    return GetError();

  StringRef SizeStr = Token.range().drop_front();
  if (SizeStr.size() == 0 || !llvm::all_of(SizeStr, isdigit))
    return error("expected integers after 's'/'p' type character");

  if (Token.range().front() == 's') {
    auto ScalarSize = APSInt(Token.range().drop_front()).getZExtValue();
    if (!verifyScalarSize(ScalarSize))
      return error("invalid size for scalar element in vector");
    Ty = LLT::scalar(ScalarSize);
  } else if (Token.range().front() == 'p') {
    const DataLayout &DL = MF.getDataLayout();
    uint64_t AS = APSInt(Token.range().drop_front()).getZExtValue();
    if (!verifyAddrSpace(AS))
      return error("invalid address space number");

    Ty = LLT::pointer(AS, DL.getPointerSizeInBits(AS));
  } else
    return GetError();
  lex();

  if (Token.isNot(MIToken::greater))
    return GetError();

  lex();

  Ty = LLT::vector(ElementCount::get(NumElements, HasVScale), Ty);
  return false;
}

bool MIParser::parseTypedImmediateOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::Identifier));
  StringRef TypeStr = Token.range();
  if (TypeStr.front() != 'i' && TypeStr.front() != 's' &&
      TypeStr.front() != 'p')
    return error(
        "a typed immediate operand should start with one of 'i', 's', or 'p'");
  StringRef SizeStr = Token.range().drop_front();
  if (SizeStr.size() == 0 || !llvm::all_of(SizeStr, isdigit))
    return error("expected integers after 'i'/'s'/'p' type character");

  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::IntegerLiteral)) {
    if (Token.isNot(MIToken::Identifier) ||
        !(Token.range() == "true" || Token.range() == "false"))
      return error("expected an integer literal");
  }
  const Constant *C = nullptr;
  if (parseIRConstant(Loc, C))
    return true;
  Dest = MachineOperand::CreateCImm(cast<ConstantInt>(C));
  return false;
}

bool MIParser::parseFPImmediateOperand(MachineOperand &Dest) {
  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::FloatingPointLiteral) &&
      Token.isNot(MIToken::HexLiteral))
    return error("expected a floating point literal");
  const Constant *C = nullptr;
  if (parseIRConstant(Loc, C))
    return true;
  Dest = MachineOperand::CreateFPImm(cast<ConstantFP>(C));
  return false;
}

static bool getHexUint(const MIToken &Token, APInt &Result) {
  assert(Token.is(MIToken::HexLiteral));
  StringRef S = Token.range();
  assert(S[0] == '0' && tolower(S[1]) == 'x');
  // This could be a floating point literal with a special prefix.
  if (!isxdigit(S[2]))
    return true;
  StringRef V = S.substr(2);
  APInt A(V.size()*4, V, 16);

  // If A is 0, then A.getActiveBits() is 0. This isn't a valid bitwidth. Make
  // sure it isn't the case before constructing result.
  unsigned NumBits = (A == 0) ? 32 : A.getActiveBits();
  Result = APInt(NumBits, ArrayRef<uint64_t>(A.getRawData(), A.getNumWords()));
  return false;
}

static bool getUnsigned(const MIToken &Token, unsigned &Result,
                        ErrorCallbackType ErrCB) {
  if (Token.hasIntegerValue()) {
    const uint64_t Limit = uint64_t(std::numeric_limits<unsigned>::max()) + 1;
    uint64_t Val64 = Token.integerValue().getLimitedValue(Limit);
    if (Val64 == Limit)
      return ErrCB(Token.location(), "expected 32-bit integer (too large)");
    Result = Val64;
    return false;
  }
  if (Token.is(MIToken::HexLiteral)) {
    APInt A;
    if (getHexUint(Token, A))
      return true;
    if (A.getBitWidth() > 32)
      return ErrCB(Token.location(), "expected 32-bit integer (too large)");
    Result = A.getZExtValue();
    return false;
  }
  return true;
}

bool MIParser::getUnsigned(unsigned &Result) {
  return ::getUnsigned(
      Token, Result, [this](StringRef::iterator Loc, const Twine &Msg) -> bool {
        return error(Loc, Msg);
      });
}

bool MIParser::parseMBBReference(MachineBasicBlock *&MBB) {
  assert(Token.is(MIToken::MachineBasicBlock) ||
         Token.is(MIToken::MachineBasicBlockLabel));
  unsigned Number;
  if (getUnsigned(Number))
    return true;
  auto MBBInfo = PFS.MBBSlots.find(Number);
  if (MBBInfo == PFS.MBBSlots.end())
    return error(Twine("use of undefined machine basic block #") +
                 Twine(Number));
  MBB = MBBInfo->second;
  // TODO: Only parse the name if it's a MachineBasicBlockLabel. Deprecate once
  // we drop the <irname> from the bb.<id>.<irname> format.
  if (!Token.stringValue().empty() && Token.stringValue() != MBB->getName())
    return error(Twine("the name of machine basic block #") + Twine(Number) +
                 " isn't '" + Token.stringValue() + "'");
  return false;
}

bool MIParser::parseMBBOperand(MachineOperand &Dest) {
  MachineBasicBlock *MBB;
  if (parseMBBReference(MBB))
    return true;
  Dest = MachineOperand::CreateMBB(MBB);
  lex();
  return false;
}

bool MIParser::parseStackFrameIndex(int &FI) {
  assert(Token.is(MIToken::StackObject));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ObjectInfo = PFS.StackObjectSlots.find(ID);
  if (ObjectInfo == PFS.StackObjectSlots.end())
    return error(Twine("use of undefined stack object '%stack.") + Twine(ID) +
                 "'");
  StringRef Name;
  if (const auto *Alloca =
          MF.getFrameInfo().getObjectAllocation(ObjectInfo->second))
    Name = Alloca->getName();
  if (!Token.stringValue().empty() && Token.stringValue() != Name)
    return error(Twine("the name of the stack object '%stack.") + Twine(ID) +
                 "' isn't '" + Token.stringValue() + "'");
  lex();
  FI = ObjectInfo->second;
  return false;
}

bool MIParser::parseStackObjectOperand(MachineOperand &Dest) {
  int FI;
  if (parseStackFrameIndex(FI))
    return true;
  Dest = MachineOperand::CreateFI(FI);
  return false;
}

bool MIParser::parseFixedStackFrameIndex(int &FI) {
  assert(Token.is(MIToken::FixedStackObject));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ObjectInfo = PFS.FixedStackObjectSlots.find(ID);
  if (ObjectInfo == PFS.FixedStackObjectSlots.end())
    return error(Twine("use of undefined fixed stack object '%fixed-stack.") +
                 Twine(ID) + "'");
  lex();
  FI = ObjectInfo->second;
  return false;
}

bool MIParser::parseFixedStackObjectOperand(MachineOperand &Dest) {
  int FI;
  if (parseFixedStackFrameIndex(FI))
    return true;
  Dest = MachineOperand::CreateFI(FI);
  return false;
}

static bool parseGlobalValue(const MIToken &Token,
                             PerFunctionMIParsingState &PFS, GlobalValue *&GV,
                             ErrorCallbackType ErrCB) {
  switch (Token.kind()) {
  case MIToken::NamedGlobalValue: {
    const Module *M = PFS.MF.getFunction().getParent();
    GV = M->getNamedValue(Token.stringValue());
    if (!GV)
      return ErrCB(Token.location(), Twine("use of undefined global value '") +
                                         Token.range() + "'");
    break;
  }
  case MIToken::GlobalValue: {
    unsigned GVIdx;
    if (getUnsigned(Token, GVIdx, ErrCB))
      return true;
    GV = PFS.IRSlots.GlobalValues.get(GVIdx);
    if (!GV)
      return ErrCB(Token.location(), Twine("use of undefined global value '@") +
                                         Twine(GVIdx) + "'");
    break;
  }
  default:
    llvm_unreachable("The current token should be a global value");
  }
  return false;
}

bool MIParser::parseGlobalValue(GlobalValue *&GV) {
  return ::parseGlobalValue(
      Token, PFS, GV,
      [this](StringRef::iterator Loc, const Twine &Msg) -> bool {
        return error(Loc, Msg);
      });
}

bool MIParser::parseGlobalAddressOperand(MachineOperand &Dest) {
  GlobalValue *GV = nullptr;
  if (parseGlobalValue(GV))
    return true;
  lex();
  Dest = MachineOperand::CreateGA(GV, /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseConstantPoolIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::ConstantPoolItem));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto ConstantInfo = PFS.ConstantPoolSlots.find(ID);
  if (ConstantInfo == PFS.ConstantPoolSlots.end())
    return error("use of undefined constant '%const." + Twine(ID) + "'");
  lex();
  Dest = MachineOperand::CreateCPI(ID, /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseJumpTableIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::JumpTableIndex));
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto JumpTableEntryInfo = PFS.JumpTableSlots.find(ID);
  if (JumpTableEntryInfo == PFS.JumpTableSlots.end())
    return error("use of undefined jump table '%jump-table." + Twine(ID) + "'");
  lex();
  Dest = MachineOperand::CreateJTI(JumpTableEntryInfo->second);
  return false;
}

bool MIParser::parseExternalSymbolOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::ExternalSymbol));
  const char *Symbol = MF.createExternalSymbolName(Token.stringValue());
  lex();
  Dest = MachineOperand::CreateES(Symbol);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseMCSymbolOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::MCSymbol));
  MCSymbol *Symbol = getOrCreateMCSymbol(Token.stringValue());
  lex();
  Dest = MachineOperand::CreateMCSymbol(Symbol);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseSubRegisterIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::SubRegisterIndex));
  StringRef Name = Token.stringValue();
  unsigned SubRegIndex = PFS.Target.getSubRegIndex(Token.stringValue());
  if (SubRegIndex == 0)
    return error(Twine("unknown subregister index '") + Name + "'");
  lex();
  Dest = MachineOperand::CreateImm(SubRegIndex);
  return false;
}

bool MIParser::parseMDNode(MDNode *&Node) {
  assert(Token.is(MIToken::exclaim));

  auto Loc = Token.location();
  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected metadata id after '!'");
  unsigned ID;
  if (getUnsigned(ID))
    return true;
  auto NodeInfo = PFS.IRSlots.MetadataNodes.find(ID);
  if (NodeInfo == PFS.IRSlots.MetadataNodes.end()) {
    NodeInfo = PFS.MachineMetadataNodes.find(ID);
    if (NodeInfo == PFS.MachineMetadataNodes.end())
      return error(Loc, "use of undefined metadata '!" + Twine(ID) + "'");
  }
  lex();
  Node = NodeInfo->second.get();
  return false;
}

bool MIParser::parseDIExpression(MDNode *&Expr) {
  unsigned Read;
  Expr = llvm::parseDIExpressionBodyAtBeginning(
      CurrentSource, Read, Error, *PFS.MF.getFunction().getParent(),
      &PFS.IRSlots);
  CurrentSource = CurrentSource.slice(Read, StringRef::npos);
  lex();
  if (!Expr)
    return error(Error.getMessage());
  return false;
}

bool MIParser::parseDILocation(MDNode *&Loc) {
  assert(Token.is(MIToken::md_dilocation));
  lex();

  bool HaveLine = false;
  unsigned Line = 0;
  unsigned Column = 0;
  MDNode *Scope = nullptr;
  MDNode *InlinedAt = nullptr;
  bool ImplicitCode = false;

  if (expectAndConsume(MIToken::lparen))
    return true;

  if (Token.isNot(MIToken::rparen)) {
    do {
      if (Token.is(MIToken::Identifier)) {
        if (Token.stringValue() == "line") {
          lex();
          if (expectAndConsume(MIToken::colon))
            return true;
          if (Token.isNot(MIToken::IntegerLiteral) ||
              Token.integerValue().isSigned())
            return error("expected unsigned integer");
          Line = Token.integerValue().getZExtValue();
          HaveLine = true;
          lex();
          continue;
        }
        if (Token.stringValue() == "column") {
          lex();
          if (expectAndConsume(MIToken::colon))
            return true;
          if (Token.isNot(MIToken::IntegerLiteral) ||
              Token.integerValue().isSigned())
            return error("expected unsigned integer");
          Column = Token.integerValue().getZExtValue();
          lex();
          continue;
        }
        if (Token.stringValue() == "scope") {
          lex();
          if (expectAndConsume(MIToken::colon))
            return true;
          if (parseMDNode(Scope))
            return error("expected metadata node");
          if (!isa<DIScope>(Scope))
            return error("expected DIScope node");
          continue;
        }
        if (Token.stringValue() == "inlinedAt") {
          lex();
          if (expectAndConsume(MIToken::colon))
            return true;
          if (Token.is(MIToken::exclaim)) {
            if (parseMDNode(InlinedAt))
              return true;
          } else if (Token.is(MIToken::md_dilocation)) {
            if (parseDILocation(InlinedAt))
              return true;
          } else
            return error("expected metadata node");
          if (!isa<DILocation>(InlinedAt))
            return error("expected DILocation node");
          continue;
        }
        if (Token.stringValue() == "isImplicitCode") {
          lex();
          if (expectAndConsume(MIToken::colon))
            return true;
          if (!Token.is(MIToken::Identifier))
            return error("expected true/false");
          // As far as I can see, we don't have any existing need for parsing
          // true/false in MIR yet. Do it ad-hoc until there's something else
          // that needs it.
          if (Token.stringValue() == "true")
            ImplicitCode = true;
          else if (Token.stringValue() == "false")
            ImplicitCode = false;
          else
            return error("expected true/false");
          lex();
          continue;
        }
      }
      return error(Twine("invalid DILocation argument '") +
                   Token.stringValue() + "'");
    } while (consumeIfPresent(MIToken::comma));
  }

  if (expectAndConsume(MIToken::rparen))
    return true;

  if (!HaveLine)
    return error("DILocation requires line number");
  if (!Scope)
    return error("DILocation requires a scope");

  Loc = DILocation::get(MF.getFunction().getContext(), Line, Column, Scope,
                        InlinedAt, ImplicitCode);
  return false;
}

bool MIParser::parseMetadataOperand(MachineOperand &Dest) {
  MDNode *Node = nullptr;
  if (Token.is(MIToken::exclaim)) {
    if (parseMDNode(Node))
      return true;
  } else if (Token.is(MIToken::md_diexpr)) {
    if (parseDIExpression(Node))
      return true;
  }
  Dest = MachineOperand::CreateMetadata(Node);
  return false;
}

bool MIParser::parseCFIOffset(int &Offset) {
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected a cfi offset");
  if (Token.integerValue().getSignificantBits() > 32)
    return error("expected a 32 bit integer (the cfi offset is too large)");
  Offset = (int)Token.integerValue().getExtValue();
  lex();
  return false;
}

bool MIParser::parseCFIRegister(Register &Reg) {
  if (Token.isNot(MIToken::NamedRegister))
    return error("expected a cfi register");
  Register LLVMReg;
  if (parseNamedRegister(LLVMReg))
    return true;
  const auto *TRI = MF.getSubtarget().getRegisterInfo();
  assert(TRI && "Expected target register info");
  int DwarfReg = TRI->getDwarfRegNum(LLVMReg, true);
  if (DwarfReg < 0)
    return error("invalid DWARF register");
  Reg = (unsigned)DwarfReg;
  lex();
  return false;
}

bool MIParser::parseCFIAddressSpace(unsigned &AddressSpace) {
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected a cfi address space literal");
  if (Token.integerValue().isSigned())
    return error("expected an unsigned integer (cfi address space)");
  AddressSpace = Token.integerValue().getZExtValue();
  lex();
  return false;
}

bool MIParser::parseCFIEscapeValues(std::string &Values) {
  do {
    if (Token.isNot(MIToken::HexLiteral))
      return error("expected a hexadecimal literal");
    unsigned Value;
    if (getUnsigned(Value))
      return true;
    if (Value > UINT8_MAX)
      return error("expected a 8-bit integer (too large)");
    Values.push_back(static_cast<uint8_t>(Value));
    lex();
  } while (consumeIfPresent(MIToken::comma));
  return false;
}

bool MIParser::parseCFIOperand(MachineOperand &Dest) {
  auto Kind = Token.kind();
  lex();
  int Offset;
  Register Reg;
  unsigned AddressSpace;
  unsigned CFIIndex;
  switch (Kind) {
  case MIToken::kw_cfi_same_value:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createSameValue(nullptr, Reg));
    break;
  case MIToken::kw_cfi_offset:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset))
      return true;
    CFIIndex =
        MF.addFrameInst(MCCFIInstruction::createOffset(nullptr, Reg, Offset));
    break;
  case MIToken::kw_cfi_rel_offset:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset))
      return true;
    CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createRelOffset(nullptr, Reg, Offset));
    break;
  case MIToken::kw_cfi_def_cfa_register:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex =
        MF.addFrameInst(MCCFIInstruction::createDefCfaRegister(nullptr, Reg));
    break;
  case MIToken::kw_cfi_def_cfa_offset:
    if (parseCFIOffset(Offset))
      return true;
    CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(nullptr, Offset));
    break;
  case MIToken::kw_cfi_adjust_cfa_offset:
    if (parseCFIOffset(Offset))
      return true;
    CFIIndex = MF.addFrameInst(
        MCCFIInstruction::createAdjustCfaOffset(nullptr, Offset));
    break;
  case MIToken::kw_cfi_def_cfa:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset))
      return true;
    CFIIndex =
        MF.addFrameInst(MCCFIInstruction::cfiDefCfa(nullptr, Reg, Offset));
    break;
  case MIToken::kw_cfi_llvm_def_aspace_cfa:
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIOffset(Offset) || expectAndConsume(MIToken::comma) ||
        parseCFIAddressSpace(AddressSpace))
      return true;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createLLVMDefAspaceCfa(
        nullptr, Reg, Offset, AddressSpace, SMLoc()));
    break;
  case MIToken::kw_cfi_remember_state:
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createRememberState(nullptr));
    break;
  case MIToken::kw_cfi_restore:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createRestore(nullptr, Reg));
    break;
  case MIToken::kw_cfi_restore_state:
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createRestoreState(nullptr));
    break;
  case MIToken::kw_cfi_undefined:
    if (parseCFIRegister(Reg))
      return true;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createUndefined(nullptr, Reg));
    break;
  case MIToken::kw_cfi_register: {
    Register Reg2;
    if (parseCFIRegister(Reg) || expectAndConsume(MIToken::comma) ||
        parseCFIRegister(Reg2))
      return true;

    CFIIndex =
        MF.addFrameInst(MCCFIInstruction::createRegister(nullptr, Reg, Reg2));
    break;
  }
  case MIToken::kw_cfi_window_save:
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createWindowSave(nullptr));
    break;
  case MIToken::kw_cfi_aarch64_negate_ra_sign_state:
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createNegateRAState(nullptr));
    break;
  case MIToken::kw_cfi_escape: {
    std::string Values;
    if (parseCFIEscapeValues(Values))
      return true;
    CFIIndex = MF.addFrameInst(MCCFIInstruction::createEscape(nullptr, Values));
    break;
  }
  default:
    // TODO: Parse the other CFI operands.
    llvm_unreachable("The current token should be a cfi operand");
  }
  Dest = MachineOperand::CreateCFIIndex(CFIIndex);
  return false;
}

bool MIParser::parseIRBlock(BasicBlock *&BB, const Function &F) {
  switch (Token.kind()) {
  case MIToken::NamedIRBlock: {
    BB = dyn_cast_or_null<BasicBlock>(
        F.getValueSymbolTable()->lookup(Token.stringValue()));
    if (!BB)
      return error(Twine("use of undefined IR block '") + Token.range() + "'");
    break;
  }
  case MIToken::IRBlock: {
    unsigned SlotNumber = 0;
    if (getUnsigned(SlotNumber))
      return true;
    BB = const_cast<BasicBlock *>(getIRBlock(SlotNumber, F));
    if (!BB)
      return error(Twine("use of undefined IR block '%ir-block.") +
                   Twine(SlotNumber) + "'");
    break;
  }
  default:
    llvm_unreachable("The current token should be an IR block reference");
  }
  return false;
}

bool MIParser::parseBlockAddressOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_blockaddress));
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  if (Token.isNot(MIToken::GlobalValue) &&
      Token.isNot(MIToken::NamedGlobalValue))
    return error("expected a global value");
  GlobalValue *GV = nullptr;
  if (parseGlobalValue(GV))
    return true;
  auto *F = dyn_cast<Function>(GV);
  if (!F)
    return error("expected an IR function reference");
  lex();
  if (expectAndConsume(MIToken::comma))
    return true;
  BasicBlock *BB = nullptr;
  if (Token.isNot(MIToken::IRBlock) && Token.isNot(MIToken::NamedIRBlock))
    return error("expected an IR block reference");
  if (parseIRBlock(BB, *F))
    return true;
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateBA(BlockAddress::get(F, BB), /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseIntrinsicOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_intrinsic));
  lex();
  if (expectAndConsume(MIToken::lparen))
    return error("expected syntax intrinsic(@llvm.whatever)");

  if (Token.isNot(MIToken::NamedGlobalValue))
    return error("expected syntax intrinsic(@llvm.whatever)");

  std::string Name = std::string(Token.stringValue());
  lex();

  if (expectAndConsume(MIToken::rparen))
    return error("expected ')' to terminate intrinsic name");

  // Find out what intrinsic we're dealing with, first try the global namespace
  // and then the target's private intrinsics if that fails.
  const TargetIntrinsicInfo *TII = MF.getTarget().getIntrinsicInfo();
  Intrinsic::ID ID = Function::lookupIntrinsicID(Name);
  if (ID == Intrinsic::not_intrinsic && TII)
    ID = static_cast<Intrinsic::ID>(TII->lookupName(Name));

  if (ID == Intrinsic::not_intrinsic)
    return error("unknown intrinsic name");
  Dest = MachineOperand::CreateIntrinsicID(ID);

  return false;
}

bool MIParser::parsePredicateOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_intpred) || Token.is(MIToken::kw_floatpred));
  bool IsFloat = Token.is(MIToken::kw_floatpred);
  lex();

  if (expectAndConsume(MIToken::lparen))
    return error("expected syntax intpred(whatever) or floatpred(whatever");

  if (Token.isNot(MIToken::Identifier))
    return error("whatever");

  CmpInst::Predicate Pred;
  if (IsFloat) {
    Pred = StringSwitch<CmpInst::Predicate>(Token.stringValue())
               .Case("false", CmpInst::FCMP_FALSE)
               .Case("oeq", CmpInst::FCMP_OEQ)
               .Case("ogt", CmpInst::FCMP_OGT)
               .Case("oge", CmpInst::FCMP_OGE)
               .Case("olt", CmpInst::FCMP_OLT)
               .Case("ole", CmpInst::FCMP_OLE)
               .Case("one", CmpInst::FCMP_ONE)
               .Case("ord", CmpInst::FCMP_ORD)
               .Case("uno", CmpInst::FCMP_UNO)
               .Case("ueq", CmpInst::FCMP_UEQ)
               .Case("ugt", CmpInst::FCMP_UGT)
               .Case("uge", CmpInst::FCMP_UGE)
               .Case("ult", CmpInst::FCMP_ULT)
               .Case("ule", CmpInst::FCMP_ULE)
               .Case("une", CmpInst::FCMP_UNE)
               .Case("true", CmpInst::FCMP_TRUE)
               .Default(CmpInst::BAD_FCMP_PREDICATE);
    if (!CmpInst::isFPPredicate(Pred))
      return error("invalid floating-point predicate");
  } else {
    Pred = StringSwitch<CmpInst::Predicate>(Token.stringValue())
               .Case("eq", CmpInst::ICMP_EQ)
               .Case("ne", CmpInst::ICMP_NE)
               .Case("sgt", CmpInst::ICMP_SGT)
               .Case("sge", CmpInst::ICMP_SGE)
               .Case("slt", CmpInst::ICMP_SLT)
               .Case("sle", CmpInst::ICMP_SLE)
               .Case("ugt", CmpInst::ICMP_UGT)
               .Case("uge", CmpInst::ICMP_UGE)
               .Case("ult", CmpInst::ICMP_ULT)
               .Case("ule", CmpInst::ICMP_ULE)
               .Default(CmpInst::BAD_ICMP_PREDICATE);
    if (!CmpInst::isIntPredicate(Pred))
      return error("invalid integer predicate");
  }

  lex();
  Dest = MachineOperand::CreatePredicate(Pred);
  if (expectAndConsume(MIToken::rparen))
    return error("predicate should be terminated by ')'.");

  return false;
}

bool MIParser::parseShuffleMaskOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_shufflemask));

  lex();
  if (expectAndConsume(MIToken::lparen))
    return error("expected syntax shufflemask(<integer or undef>, ...)");

  SmallVector<int, 32> ShufMask;
  do {
    if (Token.is(MIToken::kw_undef)) {
      ShufMask.push_back(-1);
    } else if (Token.is(MIToken::IntegerLiteral)) {
      const APSInt &Int = Token.integerValue();
      ShufMask.push_back(Int.getExtValue());
    } else
      return error("expected integer constant");

    lex();
  } while (consumeIfPresent(MIToken::comma));

  if (expectAndConsume(MIToken::rparen))
    return error("shufflemask should be terminated by ')'.");

  ArrayRef<int> MaskAlloc = MF.allocateShuffleMask(ShufMask);
  Dest = MachineOperand::CreateShuffleMask(MaskAlloc);
  return false;
}

bool MIParser::parseDbgInstrRefOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_dbg_instr_ref));

  lex();
  if (expectAndConsume(MIToken::lparen))
    return error("expected syntax dbg-instr-ref(<unsigned>, <unsigned>)");

  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isNegative())
    return error("expected unsigned integer for instruction index");
  uint64_t InstrIdx = Token.integerValue().getZExtValue();
  assert(InstrIdx <= std::numeric_limits<unsigned>::max() &&
         "Instruction reference's instruction index is too large");
  lex();

  if (expectAndConsume(MIToken::comma))
    return error("expected syntax dbg-instr-ref(<unsigned>, <unsigned>)");

  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isNegative())
    return error("expected unsigned integer for operand index");
  uint64_t OpIdx = Token.integerValue().getZExtValue();
  assert(OpIdx <= std::numeric_limits<unsigned>::max() &&
         "Instruction reference's operand index is too large");
  lex();

  if (expectAndConsume(MIToken::rparen))
    return error("expected syntax dbg-instr-ref(<unsigned>, <unsigned>)");

  Dest = MachineOperand::CreateDbgInstrRef(InstrIdx, OpIdx);
  return false;
}

bool MIParser::parseTargetIndexOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_target_index));
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  if (Token.isNot(MIToken::Identifier))
    return error("expected the name of the target index");
  int Index = 0;
  if (PFS.Target.getTargetIndex(Token.stringValue(), Index))
    return error("use of undefined target index '" + Token.stringValue() + "'");
  lex();
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateTargetIndex(unsigned(Index), /*Offset=*/0);
  if (parseOperandsOffset(Dest))
    return true;
  return false;
}

bool MIParser::parseCustomRegisterMaskOperand(MachineOperand &Dest) {
  assert(Token.stringValue() == "CustomRegMask" && "Expected a custom RegMask");
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;

  uint32_t *Mask = MF.allocateRegMask();
  do {
    if (Token.isNot(MIToken::rparen)) {
      if (Token.isNot(MIToken::NamedRegister))
        return error("expected a named register");
      Register Reg;
      if (parseNamedRegister(Reg))
        return true;
      lex();
      Mask[Reg / 32] |= 1U << (Reg % 32);
    }

    // TODO: Report an error if the same register is used more than once.
  } while (consumeIfPresent(MIToken::comma));

  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateRegMask(Mask);
  return false;
}

bool MIParser::parseLiveoutRegisterMaskOperand(MachineOperand &Dest) {
  assert(Token.is(MIToken::kw_liveout));
  uint32_t *Mask = MF.allocateRegMask();
  lex();
  if (expectAndConsume(MIToken::lparen))
    return true;
  while (true) {
    if (Token.isNot(MIToken::NamedRegister))
      return error("expected a named register");
    Register Reg;
    if (parseNamedRegister(Reg))
      return true;
    lex();
    Mask[Reg / 32] |= 1U << (Reg % 32);
    // TODO: Report an error if the same register is used more than once.
    if (Token.isNot(MIToken::comma))
      break;
    lex();
  }
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MachineOperand::CreateRegLiveOut(Mask);
  return false;
}

bool MIParser::parseMachineOperand(const unsigned OpCode, const unsigned OpIdx,
                                   MachineOperand &Dest,
                                   std::optional<unsigned> &TiedDefIdx) {
  switch (Token.kind()) {
  case MIToken::kw_implicit:
  case MIToken::kw_implicit_define:
  case MIToken::kw_def:
  case MIToken::kw_dead:
  case MIToken::kw_killed:
  case MIToken::kw_undef:
  case MIToken::kw_internal:
  case MIToken::kw_early_clobber:
  case MIToken::kw_debug_use:
  case MIToken::kw_renamable:
  case MIToken::underscore:
  case MIToken::NamedRegister:
  case MIToken::VirtualRegister:
  case MIToken::NamedVirtualRegister:
    return parseRegisterOperand(Dest, TiedDefIdx);
  case MIToken::IntegerLiteral:
    return parseImmediateOperand(Dest);
  case MIToken::kw_half:
  case MIToken::kw_bfloat:
  case MIToken::kw_float:
  case MIToken::kw_double:
  case MIToken::kw_x86_fp80:
  case MIToken::kw_fp128:
  case MIToken::kw_ppc_fp128:
    return parseFPImmediateOperand(Dest);
  case MIToken::MachineBasicBlock:
    return parseMBBOperand(Dest);
  case MIToken::StackObject:
    return parseStackObjectOperand(Dest);
  case MIToken::FixedStackObject:
    return parseFixedStackObjectOperand(Dest);
  case MIToken::GlobalValue:
  case MIToken::NamedGlobalValue:
    return parseGlobalAddressOperand(Dest);
  case MIToken::ConstantPoolItem:
    return parseConstantPoolIndexOperand(Dest);
  case MIToken::JumpTableIndex:
    return parseJumpTableIndexOperand(Dest);
  case MIToken::ExternalSymbol:
    return parseExternalSymbolOperand(Dest);
  case MIToken::MCSymbol:
    return parseMCSymbolOperand(Dest);
  case MIToken::SubRegisterIndex:
    return parseSubRegisterIndexOperand(Dest);
  case MIToken::md_diexpr:
  case MIToken::exclaim:
    return parseMetadataOperand(Dest);
  case MIToken::kw_cfi_same_value:
  case MIToken::kw_cfi_offset:
  case MIToken::kw_cfi_rel_offset:
  case MIToken::kw_cfi_def_cfa_register:
  case MIToken::kw_cfi_def_cfa_offset:
  case MIToken::kw_cfi_adjust_cfa_offset:
  case MIToken::kw_cfi_escape:
  case MIToken::kw_cfi_def_cfa:
  case MIToken::kw_cfi_llvm_def_aspace_cfa:
  case MIToken::kw_cfi_register:
  case MIToken::kw_cfi_remember_state:
  case MIToken::kw_cfi_restore:
  case MIToken::kw_cfi_restore_state:
  case MIToken::kw_cfi_undefined:
  case MIToken::kw_cfi_window_save:
  case MIToken::kw_cfi_aarch64_negate_ra_sign_state:
    return parseCFIOperand(Dest);
  case MIToken::kw_blockaddress:
    return parseBlockAddressOperand(Dest);
  case MIToken::kw_intrinsic:
    return parseIntrinsicOperand(Dest);
  case MIToken::kw_target_index:
    return parseTargetIndexOperand(Dest);
  case MIToken::kw_liveout:
    return parseLiveoutRegisterMaskOperand(Dest);
  case MIToken::kw_floatpred:
  case MIToken::kw_intpred:
    return parsePredicateOperand(Dest);
  case MIToken::kw_shufflemask:
    return parseShuffleMaskOperand(Dest);
  case MIToken::kw_dbg_instr_ref:
    return parseDbgInstrRefOperand(Dest);
  case MIToken::Error:
    return true;
  case MIToken::Identifier:
    if (const auto *RegMask = PFS.Target.getRegMask(Token.stringValue())) {
      Dest = MachineOperand::CreateRegMask(RegMask);
      lex();
      break;
    } else if (Token.stringValue() == "CustomRegMask") {
      return parseCustomRegisterMaskOperand(Dest);
    } else
      return parseTypedImmediateOperand(Dest);
  case MIToken::dot: {
    const auto *TII = MF.getSubtarget().getInstrInfo();
    if (const auto *Formatter = TII->getMIRFormatter()) {
      return parseTargetImmMnemonic(OpCode, OpIdx, Dest, *Formatter);
    }
    [[fallthrough]];
  }
  default:
    // FIXME: Parse the MCSymbol machine operand.
    return error("expected a machine operand");
  }
  return false;
}

bool MIParser::parseMachineOperandAndTargetFlags(
    const unsigned OpCode, const unsigned OpIdx, MachineOperand &Dest,
    std::optional<unsigned> &TiedDefIdx) {
  unsigned TF = 0;
  bool HasTargetFlags = false;
  if (Token.is(MIToken::kw_target_flags)) {
    HasTargetFlags = true;
    lex();
    if (expectAndConsume(MIToken::lparen))
      return true;
    if (Token.isNot(MIToken::Identifier))
      return error("expected the name of the target flag");
    if (PFS.Target.getDirectTargetFlag(Token.stringValue(), TF)) {
      if (PFS.Target.getBitmaskTargetFlag(Token.stringValue(), TF))
        return error("use of undefined target flag '" + Token.stringValue() +
                     "'");
    }
    lex();
    while (Token.is(MIToken::comma)) {
      lex();
      if (Token.isNot(MIToken::Identifier))
        return error("expected the name of the target flag");
      unsigned BitFlag = 0;
      if (PFS.Target.getBitmaskTargetFlag(Token.stringValue(), BitFlag))
        return error("use of undefined target flag '" + Token.stringValue() +
                     "'");
      // TODO: Report an error when using a duplicate bit target flag.
      TF |= BitFlag;
      lex();
    }
    if (expectAndConsume(MIToken::rparen))
      return true;
  }
  auto Loc = Token.location();
  if (parseMachineOperand(OpCode, OpIdx, Dest, TiedDefIdx))
    return true;
  if (!HasTargetFlags)
    return false;
  if (Dest.isReg())
    return error(Loc, "register operands can't have target flags");
  Dest.setTargetFlags(TF);
  return false;
}

bool MIParser::parseOffset(int64_t &Offset) {
  if (Token.isNot(MIToken::plus) && Token.isNot(MIToken::minus))
    return false;
  StringRef Sign = Token.range();
  bool IsNegative = Token.is(MIToken::minus);
  lex();
  if (Token.isNot(MIToken::IntegerLiteral))
    return error("expected an integer literal after '" + Sign + "'");
  if (Token.integerValue().getSignificantBits() > 64)
    return error("expected 64-bit integer (too large)");
  Offset = Token.integerValue().getExtValue();
  if (IsNegative)
    Offset = -Offset;
  lex();
  return false;
}

bool MIParser::parseIRBlockAddressTaken(BasicBlock *&BB) {
  assert(Token.is(MIToken::kw_ir_block_address_taken));
  lex();
  if (Token.isNot(MIToken::IRBlock) && Token.isNot(MIToken::NamedIRBlock))
    return error("expected basic block after 'ir_block_address_taken'");

  if (parseIRBlock(BB, MF.getFunction()))
    return true;

  lex();
  return false;
}

bool MIParser::parseAlignment(uint64_t &Alignment) {
  assert(Token.is(MIToken::kw_align) || Token.is(MIToken::kw_basealign));
  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected an integer literal after 'align'");
  if (getUint64(Alignment))
    return true;
  lex();

  if (!isPowerOf2_64(Alignment))
    return error("expected a power-of-2 literal after 'align'");

  return false;
}

bool MIParser::parseAddrspace(unsigned &Addrspace) {
  assert(Token.is(MIToken::kw_addrspace));
  lex();
  if (Token.isNot(MIToken::IntegerLiteral) || Token.integerValue().isSigned())
    return error("expected an integer literal after 'addrspace'");
  if (getUnsigned(Addrspace))
    return true;
  lex();
  return false;
}

bool MIParser::parseOperandsOffset(MachineOperand &Op) {
  int64_t Offset = 0;
  if (parseOffset(Offset))
    return true;
  Op.setOffset(Offset);
  return false;
}

static bool parseIRValue(const MIToken &Token, PerFunctionMIParsingState &PFS,
                         const Value *&V, ErrorCallbackType ErrCB) {
  switch (Token.kind()) {
  case MIToken::NamedIRValue: {
    V = PFS.MF.getFunction().getValueSymbolTable()->lookup(Token.stringValue());
    break;
  }
  case MIToken::IRValue: {
    unsigned SlotNumber = 0;
    if (getUnsigned(Token, SlotNumber, ErrCB))
      return true;
    V = PFS.getIRValue(SlotNumber);
    break;
  }
  case MIToken::NamedGlobalValue:
  case MIToken::GlobalValue: {
    GlobalValue *GV = nullptr;
    if (parseGlobalValue(Token, PFS, GV, ErrCB))
      return true;
    V = GV;
    break;
  }
  case MIToken::QuotedIRValue: {
    const Constant *C = nullptr;
    if (parseIRConstant(Token.location(), Token.stringValue(), PFS, C, ErrCB))
      return true;
    V = C;
    break;
  }
  case MIToken::kw_unknown_address:
    V = nullptr;
    return false;
  default:
    llvm_unreachable("The current token should be an IR block reference");
  }
  if (!V)
    return ErrCB(Token.location(), Twine("use of undefined IR value '") + Token.range() + "'");
  return false;
}

bool MIParser::parseIRValue(const Value *&V) {
  return ::parseIRValue(
      Token, PFS, V, [this](StringRef::iterator Loc, const Twine &Msg) -> bool {
        return error(Loc, Msg);
      });
}

bool MIParser::getUint64(uint64_t &Result) {
  if (Token.hasIntegerValue()) {
    if (Token.integerValue().getActiveBits() > 64)
      return error("expected 64-bit integer (too large)");
    Result = Token.integerValue().getZExtValue();
    return false;
  }
  if (Token.is(MIToken::HexLiteral)) {
    APInt A;
    if (getHexUint(A))
      return true;
    if (A.getBitWidth() > 64)
      return error("expected 64-bit integer (too large)");
    Result = A.getZExtValue();
    return false;
  }
  return true;
}

bool MIParser::getHexUint(APInt &Result) {
  return ::getHexUint(Token, Result);
}

bool MIParser::parseMemoryOperandFlag(MachineMemOperand::Flags &Flags) {
  const auto OldFlags = Flags;
  switch (Token.kind()) {
  case MIToken::kw_volatile:
    Flags |= MachineMemOperand::MOVolatile;
    break;
  case MIToken::kw_non_temporal:
    Flags |= MachineMemOperand::MONonTemporal;
    break;
  case MIToken::kw_dereferenceable:
    Flags |= MachineMemOperand::MODereferenceable;
    break;
  case MIToken::kw_invariant:
    Flags |= MachineMemOperand::MOInvariant;
    break;
  case MIToken::StringConstant: {
    MachineMemOperand::Flags TF;
    if (PFS.Target.getMMOTargetFlag(Token.stringValue(), TF))
      return error("use of undefined target MMO flag '" + Token.stringValue() +
                   "'");
    Flags |= TF;
    break;
  }
  default:
    llvm_unreachable("The current token should be a memory operand flag");
  }
  if (OldFlags == Flags)
    // We know that the same flag is specified more than once when the flags
    // weren't modified.
    return error("duplicate '" + Token.stringValue() + "' memory operand flag");
  lex();
  return false;
}

bool MIParser::parseMemoryPseudoSourceValue(const PseudoSourceValue *&PSV) {
  switch (Token.kind()) {
  case MIToken::kw_stack:
    PSV = MF.getPSVManager().getStack();
    break;
  case MIToken::kw_got:
    PSV = MF.getPSVManager().getGOT();
    break;
  case MIToken::kw_jump_table:
    PSV = MF.getPSVManager().getJumpTable();
    break;
  case MIToken::kw_constant_pool:
    PSV = MF.getPSVManager().getConstantPool();
    break;
  case MIToken::FixedStackObject: {
    int FI;
    if (parseFixedStackFrameIndex(FI))
      return true;
    PSV = MF.getPSVManager().getFixedStack(FI);
    // The token was already consumed, so use return here instead of break.
    return false;
  }
  case MIToken::StackObject: {
    int FI;
    if (parseStackFrameIndex(FI))
      return true;
    PSV = MF.getPSVManager().getFixedStack(FI);
    // The token was already consumed, so use return here instead of break.
    return false;
  }
  case MIToken::kw_call_entry:
    lex();
    switch (Token.kind()) {
    case MIToken::GlobalValue:
    case MIToken::NamedGlobalValue: {
      GlobalValue *GV = nullptr;
      if (parseGlobalValue(GV))
        return true;
      PSV = MF.getPSVManager().getGlobalValueCallEntry(GV);
      break;
    }
    case MIToken::ExternalSymbol:
      PSV = MF.getPSVManager().getExternalSymbolCallEntry(
          MF.createExternalSymbolName(Token.stringValue()));
      break;
    default:
      return error(
          "expected a global value or an external symbol after 'call-entry'");
    }
    break;
  case MIToken::kw_custom: {
    lex();
    const auto *TII = MF.getSubtarget().getInstrInfo();
    if (const auto *Formatter = TII->getMIRFormatter()) {
      if (Formatter->parseCustomPseudoSourceValue(
              Token.stringValue(), MF, PFS, PSV,
              [this](StringRef::iterator Loc, const Twine &Msg) -> bool {
                return error(Loc, Msg);
              }))
        return true;
    } else
      return error("unable to parse target custom pseudo source value");
    break;
  }
  default:
    llvm_unreachable("The current token should be pseudo source value");
  }
  lex();
  return false;
}

bool MIParser::parseMachinePointerInfo(MachinePointerInfo &Dest) {
  if (Token.is(MIToken::kw_constant_pool) || Token.is(MIToken::kw_stack) ||
      Token.is(MIToken::kw_got) || Token.is(MIToken::kw_jump_table) ||
      Token.is(MIToken::FixedStackObject) || Token.is(MIToken::StackObject) ||
      Token.is(MIToken::kw_call_entry) || Token.is(MIToken::kw_custom)) {
    const PseudoSourceValue *PSV = nullptr;
    if (parseMemoryPseudoSourceValue(PSV))
      return true;
    int64_t Offset = 0;
    if (parseOffset(Offset))
      return true;
    Dest = MachinePointerInfo(PSV, Offset);
    return false;
  }
  if (Token.isNot(MIToken::NamedIRValue) && Token.isNot(MIToken::IRValue) &&
      Token.isNot(MIToken::GlobalValue) &&
      Token.isNot(MIToken::NamedGlobalValue) &&
      Token.isNot(MIToken::QuotedIRValue) &&
      Token.isNot(MIToken::kw_unknown_address))
    return error("expected an IR value reference");
  const Value *V = nullptr;
  if (parseIRValue(V))
    return true;
  if (V && !V->getType()->isPointerTy())
    return error("expected a pointer IR value");
  lex();
  int64_t Offset = 0;
  if (parseOffset(Offset))
    return true;
  Dest = MachinePointerInfo(V, Offset);
  return false;
}

bool MIParser::parseOptionalScope(LLVMContext &Context,
                                  SyncScope::ID &SSID) {
  SSID = SyncScope::System;
  if (Token.is(MIToken::Identifier) && Token.stringValue() == "syncscope") {
    lex();
    if (expectAndConsume(MIToken::lparen))
      return error("expected '(' in syncscope");

    std::string SSN;
    if (parseStringConstant(SSN))
      return true;

    SSID = Context.getOrInsertSyncScopeID(SSN);
    if (expectAndConsume(MIToken::rparen))
      return error("expected ')' in syncscope");
  }

  return false;
}

bool MIParser::parseOptionalAtomicOrdering(AtomicOrdering &Order) {
  Order = AtomicOrdering::NotAtomic;
  if (Token.isNot(MIToken::Identifier))
    return false;

  Order = StringSwitch<AtomicOrdering>(Token.stringValue())
              .Case("unordered", AtomicOrdering::Unordered)
              .Case("monotonic", AtomicOrdering::Monotonic)
              .Case("acquire", AtomicOrdering::Acquire)
              .Case("release", AtomicOrdering::Release)
              .Case("acq_rel", AtomicOrdering::AcquireRelease)
              .Case("seq_cst", AtomicOrdering::SequentiallyConsistent)
              .Default(AtomicOrdering::NotAtomic);

  if (Order != AtomicOrdering::NotAtomic) {
    lex();
    return false;
  }

  return error("expected an atomic scope, ordering or a size specification");
}

bool MIParser::parseMachineMemoryOperand(MachineMemOperand *&Dest) {
  if (expectAndConsume(MIToken::lparen))
    return true;
  MachineMemOperand::Flags Flags = MachineMemOperand::MONone;
  while (Token.isMemoryOperandFlag()) {
    if (parseMemoryOperandFlag(Flags))
      return true;
  }
  if (Token.isNot(MIToken::Identifier) ||
      (Token.stringValue() != "load" && Token.stringValue() != "store"))
    return error("expected 'load' or 'store' memory operation");
  if (Token.stringValue() == "load")
    Flags |= MachineMemOperand::MOLoad;
  else
    Flags |= MachineMemOperand::MOStore;
  lex();

  // Optional 'store' for operands that both load and store.
  if (Token.is(MIToken::Identifier) && Token.stringValue() == "store") {
    Flags |= MachineMemOperand::MOStore;
    lex();
  }

  // Optional synchronization scope.
  SyncScope::ID SSID;
  if (parseOptionalScope(MF.getFunction().getContext(), SSID))
    return true;

  // Up to two atomic orderings (cmpxchg provides guarantees on failure).
  AtomicOrdering Order, FailureOrder;
  if (parseOptionalAtomicOrdering(Order))
    return true;

  if (parseOptionalAtomicOrdering(FailureOrder))
    return true;

  LLT MemoryType;
  if (Token.isNot(MIToken::IntegerLiteral) &&
      Token.isNot(MIToken::kw_unknown_size) &&
      Token.isNot(MIToken::lparen))
    return error("expected memory LLT, the size integer literal or 'unknown-size' after "
                 "memory operation");

  uint64_t Size = MemoryLocation::UnknownSize;
  if (Token.is(MIToken::IntegerLiteral)) {
    if (getUint64(Size))
      return true;

    // Convert from bytes to bits for storage.
    MemoryType = LLT::scalar(8 * Size);
    lex();
  } else if (Token.is(MIToken::kw_unknown_size)) {
    Size = MemoryLocation::UnknownSize;
    lex();
  } else {
    if (expectAndConsume(MIToken::lparen))
      return true;
    if (parseLowLevelType(Token.location(), MemoryType))
      return true;
    if (expectAndConsume(MIToken::rparen))
      return true;

    Size = MemoryType.getSizeInBytes();
  }

  MachinePointerInfo Ptr = MachinePointerInfo();
  if (Token.is(MIToken::Identifier)) {
    const char *Word =
        ((Flags & MachineMemOperand::MOLoad) &&
         (Flags & MachineMemOperand::MOStore))
            ? "on"
            : Flags & MachineMemOperand::MOLoad ? "from" : "into";
    if (Token.stringValue() != Word)
      return error(Twine("expected '") + Word + "'");
    lex();

    if (parseMachinePointerInfo(Ptr))
      return true;
  }
  uint64_t BaseAlignment =
      (Size != MemoryLocation::UnknownSize ? PowerOf2Ceil(Size) : 1);
  AAMDNodes AAInfo;
  MDNode *Range = nullptr;
  while (consumeIfPresent(MIToken::comma)) {
    switch (Token.kind()) {
    case MIToken::kw_align: {
      // align is printed if it is different than size.
      uint64_t Alignment;
      if (parseAlignment(Alignment))
        return true;
      if (Ptr.Offset & (Alignment - 1)) {
        // MachineMemOperand::getAlign never returns a value greater than the
        // alignment of offset, so this just guards against hand-written MIR
        // that specifies a large "align" value when it should probably use
        // "basealign" instead.
        return error("specified alignment is more aligned than offset");
      }
      BaseAlignment = Alignment;
      break;
    }
    case MIToken::kw_basealign:
      // basealign is printed if it is different than align.
      if (parseAlignment(BaseAlignment))
        return true;
      break;
    case MIToken::kw_addrspace:
      if (parseAddrspace(Ptr.AddrSpace))
        return true;
      break;
    case MIToken::md_tbaa:
      lex();
      if (parseMDNode(AAInfo.TBAA))
        return true;
      break;
    case MIToken::md_alias_scope:
      lex();
      if (parseMDNode(AAInfo.Scope))
        return true;
      break;
    case MIToken::md_noalias:
      lex();
      if (parseMDNode(AAInfo.NoAlias))
        return true;
      break;
    case MIToken::md_range:
      lex();
      if (parseMDNode(Range))
        return true;
      break;
    // TODO: Report an error on duplicate metadata nodes.
    default:
      return error("expected 'align' or '!tbaa' or '!alias.scope' or "
                   "'!noalias' or '!range'");
    }
  }
  if (expectAndConsume(MIToken::rparen))
    return true;
  Dest = MF.getMachineMemOperand(Ptr, Flags, MemoryType, Align(BaseAlignment),
                                 AAInfo, Range, SSID, Order, FailureOrder);
  return false;
}

bool MIParser::parsePreOrPostInstrSymbol(MCSymbol *&Symbol) {
  assert((Token.is(MIToken::kw_pre_instr_symbol) ||
          Token.is(MIToken::kw_post_instr_symbol)) &&
         "Invalid token for a pre- post-instruction symbol!");
  lex();
  if (Token.isNot(MIToken::MCSymbol))
    return error("expected a symbol after 'pre-instr-symbol'");
  Symbol = getOrCreateMCSymbol(Token.stringValue());
  lex();
  if (Token.isNewlineOrEOF() || Token.is(MIToken::coloncolon) ||
      Token.is(MIToken::lbrace))
    return false;
  if (Token.isNot(MIToken::comma))
    return error("expected ',' before the next machine operand");
  lex();
  return false;
}

bool MIParser::parseHeapAllocMarker(MDNode *&Node) {
  assert(Token.is(MIToken::kw_heap_alloc_marker) &&
         "Invalid token for a heap alloc marker!");
  lex();
  if (parseMDNode(Node))
    return true;
  if (!Node)
    return error("expected a MDNode after 'heap-alloc-marker'");
  if (Token.isNewlineOrEOF() || Token.is(MIToken::coloncolon) ||
      Token.is(MIToken::lbrace))
    return false;
  if (Token.isNot(MIToken::comma))
    return error("expected ',' before the next machine operand");
  lex();
  return false;
}

bool MIParser::parsePCSections(MDNode *&Node) {
  assert(Token.is(MIToken::kw_pcsections) &&
         "Invalid token for a PC sections!");
  lex();
  if (parseMDNode(Node))
    return true;
  if (!Node)
    return error("expected a MDNode after 'pcsections'");
  if (Token.isNewlineOrEOF() || Token.is(MIToken::coloncolon) ||
      Token.is(MIToken::lbrace))
    return false;
  if (Token.isNot(MIToken::comma))
    return error("expected ',' before the next machine operand");
  lex();
  return false;
}

static void initSlots2BasicBlocks(
    const Function &F,
    DenseMap<unsigned, const BasicBlock *> &Slots2BasicBlocks) {
  ModuleSlotTracker MST(F.getParent(), /*ShouldInitializeAllMetadata=*/false);
  MST.incorporateFunction(F);
  for (const auto &BB : F) {
    if (BB.hasName())
      continue;
    int Slot = MST.getLocalSlot(&BB);
    if (Slot == -1)
      continue;
    Slots2BasicBlocks.insert(std::make_pair(unsigned(Slot), &BB));
  }
}

static const BasicBlock *getIRBlockFromSlot(
    unsigned Slot,
    const DenseMap<unsigned, const BasicBlock *> &Slots2BasicBlocks) {
  return Slots2BasicBlocks.lookup(Slot);
}

const BasicBlock *MIParser::getIRBlock(unsigned Slot) {
  if (Slots2BasicBlocks.empty())
    initSlots2BasicBlocks(MF.getFunction(), Slots2BasicBlocks);
  return getIRBlockFromSlot(Slot, Slots2BasicBlocks);
}

const BasicBlock *MIParser::getIRBlock(unsigned Slot, const Function &F) {
  if (&F == &MF.getFunction())
    return getIRBlock(Slot);
  DenseMap<unsigned, const BasicBlock *> CustomSlots2BasicBlocks;
  initSlots2BasicBlocks(F, CustomSlots2BasicBlocks);
  return getIRBlockFromSlot(Slot, CustomSlots2BasicBlocks);
}

MCSymbol *MIParser::getOrCreateMCSymbol(StringRef Name) {
  // FIXME: Currently we can't recognize temporary or local symbols and call all
  // of the appropriate forms to create them. However, this handles basic cases
  // well as most of the special aspects are recognized by a prefix on their
  // name, and the input names should already be unique. For test cases, keeping
  // the symbol name out of the symbol table isn't terribly important.
  return MF.getContext().getOrCreateSymbol(Name);
}

bool MIParser::parseStringConstant(std::string &Result) {
  if (Token.isNot(MIToken::StringConstant))
    return error("expected string constant");
  Result = std::string(Token.stringValue());
  lex();
  return false;
}

bool llvm::parseMachineBasicBlockDefinitions(PerFunctionMIParsingState &PFS,
                                             StringRef Src,
                                             SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseBasicBlockDefinitions(PFS.MBBSlots);
}

bool llvm::parseMachineInstructions(PerFunctionMIParsingState &PFS,
                                    StringRef Src, SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseBasicBlocks();
}

bool llvm::parseMBBReference(PerFunctionMIParsingState &PFS,
                             MachineBasicBlock *&MBB, StringRef Src,
                             SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneMBB(MBB);
}

bool llvm::parseRegisterReference(PerFunctionMIParsingState &PFS,
                                  Register &Reg, StringRef Src,
                                  SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneRegister(Reg);
}

bool llvm::parseNamedRegisterReference(PerFunctionMIParsingState &PFS,
                                       Register &Reg, StringRef Src,
                                       SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneNamedRegister(Reg);
}

bool llvm::parseVirtualRegisterReference(PerFunctionMIParsingState &PFS,
                                         VRegInfo *&Info, StringRef Src,
                                         SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneVirtualRegister(Info);
}

bool llvm::parseStackObjectReference(PerFunctionMIParsingState &PFS,
                                     int &FI, StringRef Src,
                                     SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneStackObject(FI);
}

bool llvm::parseMDNode(PerFunctionMIParsingState &PFS,
                       MDNode *&Node, StringRef Src, SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src).parseStandaloneMDNode(Node);
}

bool llvm::parseMachineMetadata(PerFunctionMIParsingState &PFS, StringRef Src,
                                SMRange SrcRange, SMDiagnostic &Error) {
  return MIParser(PFS, Error, Src, SrcRange).parseMachineMetadata();
}

bool MIRFormatter::parseIRValue(StringRef Src, MachineFunction &MF,
                                PerFunctionMIParsingState &PFS, const Value *&V,
                                ErrorCallbackType ErrorCallback) {
  MIToken Token;
  Src = lexMIToken(Src, Token, [&](StringRef::iterator Loc, const Twine &Msg) {
    ErrorCallback(Loc, Msg);
  });
  V = nullptr;

  return ::parseIRValue(Token, PFS, V, ErrorCallback);
}
