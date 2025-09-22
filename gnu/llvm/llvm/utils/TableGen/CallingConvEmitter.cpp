//===- CallingConvEmitter.cpp - Generate calling conventions --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting descriptions of the calling
// conventions supported by this target.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenTarget.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <deque>
#include <set>

using namespace llvm;

namespace {
class CallingConvEmitter {
  RecordKeeper &Records;
  unsigned Counter = 0u;
  std::string CurrentAction;
  bool SwiftAction = false;

  std::map<std::string, std::set<std::string>> AssignedRegsMap;
  std::map<std::string, std::set<std::string>> AssignedSwiftRegsMap;
  std::map<std::string, std::set<std::string>> DelegateToMap;

public:
  explicit CallingConvEmitter(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &o);

private:
  void EmitCallingConv(Record *CC, raw_ostream &O);
  void EmitAction(Record *Action, unsigned Indent, raw_ostream &O);
  void EmitArgRegisterLists(raw_ostream &O);
};
} // End anonymous namespace

void CallingConvEmitter::run(raw_ostream &O) {
  emitSourceFileHeader("Calling Convention Implementation Fragment", O);

  std::vector<Record *> CCs = Records.getAllDerivedDefinitions("CallingConv");

  // Emit prototypes for all of the non-custom CC's so that they can forward ref
  // each other.
  Records.startTimer("Emit prototypes");
  O << "#ifndef GET_CC_REGISTER_LISTS\n\n";
  for (Record *CC : CCs) {
    if (!CC->getValueAsBit("Custom")) {
      unsigned Pad = CC->getName().size();
      if (CC->getValueAsBit("Entry")) {
        O << "bool llvm::";
        Pad += 12;
      } else {
        O << "static bool ";
        Pad += 13;
      }
      O << CC->getName() << "(unsigned ValNo, MVT ValVT,\n"
        << std::string(Pad, ' ') << "MVT LocVT, CCValAssign::LocInfo LocInfo,\n"
        << std::string(Pad, ' ')
        << "ISD::ArgFlagsTy ArgFlags, CCState &State);\n";
    }
  }

  // Emit each non-custom calling convention description in full.
  Records.startTimer("Emit full descriptions");
  for (Record *CC : CCs) {
    if (!CC->getValueAsBit("Custom")) {
      EmitCallingConv(CC, O);
    }
  }

  EmitArgRegisterLists(O);

  O << "\n#endif // CC_REGISTER_LIST\n";
}

void CallingConvEmitter::EmitCallingConv(Record *CC, raw_ostream &O) {
  ListInit *CCActions = CC->getValueAsListInit("Actions");
  Counter = 0;

  CurrentAction = CC->getName().str();
  // Call upon the creation of a map entry from the void!
  // We want an entry in AssignedRegsMap for every action, even if that
  // entry is empty.
  AssignedRegsMap[CurrentAction] = {};

  O << "\n\n";
  unsigned Pad = CurrentAction.size();
  if (CC->getValueAsBit("Entry")) {
    O << "bool llvm::";
    Pad += 12;
  } else {
    O << "static bool ";
    Pad += 13;
  }
  O << CurrentAction << "(unsigned ValNo, MVT ValVT,\n"
    << std::string(Pad, ' ') << "MVT LocVT, CCValAssign::LocInfo LocInfo,\n"
    << std::string(Pad, ' ') << "ISD::ArgFlagsTy ArgFlags, CCState &State) {\n";
  // Emit all of the actions, in order.
  for (unsigned i = 0, e = CCActions->size(); i != e; ++i) {
    Record *Action = CCActions->getElementAsRecord(i);
    SwiftAction =
        llvm::any_of(Action->getSuperClasses(),
                     [](const std::pair<Record *, SMRange> &Class) {
                       std::string Name = Class.first->getNameInitAsString();
                       return StringRef(Name).starts_with("CCIfSwift");
                     });

    O << "\n";
    EmitAction(Action, 2, O);
  }

  O << "\n  return true; // CC didn't match.\n";
  O << "}\n";
}

void CallingConvEmitter::EmitAction(Record *Action, unsigned Indent,
                                    raw_ostream &O) {
  std::string IndentStr = std::string(Indent, ' ');

  if (Action->isSubClassOf("CCPredicateAction")) {
    O << IndentStr << "if (";

    if (Action->isSubClassOf("CCIfType")) {
      ListInit *VTs = Action->getValueAsListInit("VTs");
      for (unsigned i = 0, e = VTs->size(); i != e; ++i) {
        Record *VT = VTs->getElementAsRecord(i);
        if (i != 0)
          O << " ||\n    " << IndentStr;
        O << "LocVT == " << getEnumName(getValueType(VT));
      }

    } else if (Action->isSubClassOf("CCIf")) {
      O << Action->getValueAsString("Predicate");
    } else {
      errs() << *Action;
      PrintFatalError(Action->getLoc(), "Unknown CCPredicateAction!");
    }

    O << ") {\n";
    EmitAction(Action->getValueAsDef("SubAction"), Indent + 2, O);
    O << IndentStr << "}\n";
  } else {
    if (Action->isSubClassOf("CCDelegateTo")) {
      Record *CC = Action->getValueAsDef("CC");
      O << IndentStr << "if (!" << CC->getName()
        << "(ValNo, ValVT, LocVT, LocInfo, ArgFlags, State))\n"
        << IndentStr << "  return false;\n";
      DelegateToMap[CurrentAction].insert(CC->getName().str());
    } else if (Action->isSubClassOf("CCAssignToReg") ||
               Action->isSubClassOf("CCAssignToRegAndStack")) {
      ListInit *RegList = Action->getValueAsListInit("RegList");
      if (RegList->size() == 1) {
        std::string Name = getQualifiedName(RegList->getElementAsRecord(0));
        O << IndentStr << "if (unsigned Reg = State.AllocateReg(" << Name
          << ")) {\n";
        if (SwiftAction)
          AssignedSwiftRegsMap[CurrentAction].insert(Name);
        else
          AssignedRegsMap[CurrentAction].insert(Name);
      } else {
        O << IndentStr << "static const MCPhysReg RegList" << ++Counter
          << "[] = {\n";
        O << IndentStr << "  ";
        ListSeparator LS;
        for (unsigned i = 0, e = RegList->size(); i != e; ++i) {
          std::string Name = getQualifiedName(RegList->getElementAsRecord(i));
          if (SwiftAction)
            AssignedSwiftRegsMap[CurrentAction].insert(Name);
          else
            AssignedRegsMap[CurrentAction].insert(Name);
          O << LS << Name;
        }
        O << "\n" << IndentStr << "};\n";
        O << IndentStr << "if (unsigned Reg = State.AllocateReg(RegList"
          << Counter << ")) {\n";
      }
      O << IndentStr << "  State.addLoc(CCValAssign::getReg(ValNo, ValVT, "
        << "Reg, LocVT, LocInfo));\n";
      if (Action->isSubClassOf("CCAssignToRegAndStack")) {
        int Size = Action->getValueAsInt("Size");
        int Align = Action->getValueAsInt("Align");
        O << IndentStr << "  (void)State.AllocateStack(";
        if (Size)
          O << Size << ", ";
        else
          O << "\n"
            << IndentStr
            << "  State.getMachineFunction().getDataLayout()."
               "getTypeAllocSize(EVT(LocVT).getTypeForEVT(State.getContext())),"
               " ";
        if (Align)
          O << "Align(" << Align << ")";
        else
          O << "\n"
            << IndentStr
            << "  State.getMachineFunction().getDataLayout()."
               "getABITypeAlign(EVT(LocVT).getTypeForEVT(State.getContext()"
               "))";
        O << ");\n";
      }
      O << IndentStr << "  return false;\n";
      O << IndentStr << "}\n";
    } else if (Action->isSubClassOf("CCAssignToRegWithShadow")) {
      ListInit *RegList = Action->getValueAsListInit("RegList");
      ListInit *ShadowRegList = Action->getValueAsListInit("ShadowRegList");
      if (!ShadowRegList->empty() && ShadowRegList->size() != RegList->size())
        PrintFatalError(Action->getLoc(),
                        "Invalid length of list of shadowed registers");

      if (RegList->size() == 1) {
        O << IndentStr << "if (unsigned Reg = State.AllocateReg(";
        O << getQualifiedName(RegList->getElementAsRecord(0));
        O << ", " << getQualifiedName(ShadowRegList->getElementAsRecord(0));
        O << ")) {\n";
      } else {
        unsigned RegListNumber = ++Counter;
        unsigned ShadowRegListNumber = ++Counter;

        O << IndentStr << "static const MCPhysReg RegList" << RegListNumber
          << "[] = {\n";
        O << IndentStr << "  ";
        ListSeparator LS;
        for (unsigned i = 0, e = RegList->size(); i != e; ++i)
          O << LS << getQualifiedName(RegList->getElementAsRecord(i));
        O << "\n" << IndentStr << "};\n";

        O << IndentStr << "static const MCPhysReg RegList"
          << ShadowRegListNumber << "[] = {\n";
        O << IndentStr << "  ";
        ListSeparator LSS;
        for (unsigned i = 0, e = ShadowRegList->size(); i != e; ++i)
          O << LSS << getQualifiedName(ShadowRegList->getElementAsRecord(i));
        O << "\n" << IndentStr << "};\n";

        O << IndentStr << "if (unsigned Reg = State.AllocateReg(RegList"
          << RegListNumber << ", "
          << "RegList" << ShadowRegListNumber << ")) {\n";
      }
      O << IndentStr << "  State.addLoc(CCValAssign::getReg(ValNo, ValVT, "
        << "Reg, LocVT, LocInfo));\n";
      O << IndentStr << "  return false;\n";
      O << IndentStr << "}\n";
    } else if (Action->isSubClassOf("CCAssignToStack")) {
      int Size = Action->getValueAsInt("Size");
      int Align = Action->getValueAsInt("Align");

      O << IndentStr << "int64_t Offset" << ++Counter
        << " = State.AllocateStack(";
      if (Size)
        O << Size << ", ";
      else
        O << "\n"
          << IndentStr
          << "  State.getMachineFunction().getDataLayout()."
             "getTypeAllocSize(EVT(LocVT).getTypeForEVT(State.getContext())),"
             " ";
      if (Align)
        O << "Align(" << Align << ")";
      else
        O << "\n"
          << IndentStr
          << "  State.getMachineFunction().getDataLayout()."
             "getABITypeAlign(EVT(LocVT).getTypeForEVT(State.getContext()"
             "))";
      O << ");\n"
        << IndentStr << "State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset"
        << Counter << ", LocVT, LocInfo));\n";
      O << IndentStr << "return false;\n";
    } else if (Action->isSubClassOf("CCAssignToStackWithShadow")) {
      int Size = Action->getValueAsInt("Size");
      int Align = Action->getValueAsInt("Align");
      ListInit *ShadowRegList = Action->getValueAsListInit("ShadowRegList");

      unsigned ShadowRegListNumber = ++Counter;

      O << IndentStr << "static const MCPhysReg ShadowRegList"
        << ShadowRegListNumber << "[] = {\n";
      O << IndentStr << "  ";
      ListSeparator LS;
      for (unsigned i = 0, e = ShadowRegList->size(); i != e; ++i)
        O << LS << getQualifiedName(ShadowRegList->getElementAsRecord(i));
      O << "\n" << IndentStr << "};\n";

      O << IndentStr << "int64_t Offset" << ++Counter
        << " = State.AllocateStack(" << Size << ", Align(" << Align << "), "
        << "ShadowRegList" << ShadowRegListNumber << ");\n";
      O << IndentStr << "State.addLoc(CCValAssign::getMem(ValNo, ValVT, Offset"
        << Counter << ", LocVT, LocInfo));\n";
      O << IndentStr << "return false;\n";
    } else if (Action->isSubClassOf("CCPromoteToType")) {
      Record *DestTy = Action->getValueAsDef("DestTy");
      MVT::SimpleValueType DestVT = getValueType(DestTy);
      O << IndentStr << "LocVT = " << getEnumName(DestVT) << ";\n";
      if (MVT(DestVT).isFloatingPoint()) {
        O << IndentStr << "LocInfo = CCValAssign::FPExt;\n";
      } else {
        O << IndentStr << "if (ArgFlags.isSExt())\n"
          << IndentStr << "  LocInfo = CCValAssign::SExt;\n"
          << IndentStr << "else if (ArgFlags.isZExt())\n"
          << IndentStr << "  LocInfo = CCValAssign::ZExt;\n"
          << IndentStr << "else\n"
          << IndentStr << "  LocInfo = CCValAssign::AExt;\n";
      }
    } else if (Action->isSubClassOf("CCPromoteToUpperBitsInType")) {
      Record *DestTy = Action->getValueAsDef("DestTy");
      MVT::SimpleValueType DestVT = getValueType(DestTy);
      O << IndentStr << "LocVT = " << getEnumName(DestVT) << ";\n";
      if (MVT(DestVT).isFloatingPoint()) {
        PrintFatalError(Action->getLoc(),
                        "CCPromoteToUpperBitsInType does not handle floating "
                        "point");
      } else {
        O << IndentStr << "if (ArgFlags.isSExt())\n"
          << IndentStr << "  LocInfo = CCValAssign::SExtUpper;\n"
          << IndentStr << "else if (ArgFlags.isZExt())\n"
          << IndentStr << "  LocInfo = CCValAssign::ZExtUpper;\n"
          << IndentStr << "else\n"
          << IndentStr << "  LocInfo = CCValAssign::AExtUpper;\n";
      }
    } else if (Action->isSubClassOf("CCBitConvertToType")) {
      Record *DestTy = Action->getValueAsDef("DestTy");
      O << IndentStr << "LocVT = " << getEnumName(getValueType(DestTy))
        << ";\n";
      O << IndentStr << "LocInfo = CCValAssign::BCvt;\n";
    } else if (Action->isSubClassOf("CCTruncToType")) {
      Record *DestTy = Action->getValueAsDef("DestTy");
      O << IndentStr << "LocVT = " << getEnumName(getValueType(DestTy))
        << ";\n";
      O << IndentStr << "LocInfo = CCValAssign::Trunc;\n";
    } else if (Action->isSubClassOf("CCPassIndirect")) {
      Record *DestTy = Action->getValueAsDef("DestTy");
      O << IndentStr << "LocVT = " << getEnumName(getValueType(DestTy))
        << ";\n";
      O << IndentStr << "LocInfo = CCValAssign::Indirect;\n";
    } else if (Action->isSubClassOf("CCPassByVal")) {
      int Size = Action->getValueAsInt("Size");
      int Align = Action->getValueAsInt("Align");
      O << IndentStr << "State.HandleByVal(ValNo, ValVT, LocVT, LocInfo, "
        << Size << ", Align(" << Align << "), ArgFlags);\n";
      O << IndentStr << "return false;\n";
    } else if (Action->isSubClassOf("CCCustom")) {
      O << IndentStr << "if (" << Action->getValueAsString("FuncName")
        << "(ValNo, ValVT, "
        << "LocVT, LocInfo, ArgFlags, State))\n";
      O << IndentStr << "  return false;\n";
    } else {
      errs() << *Action;
      PrintFatalError(Action->getLoc(), "Unknown CCAction!");
    }
  }
}

void CallingConvEmitter::EmitArgRegisterLists(raw_ostream &O) {
  // Transitively merge all delegated CCs into AssignedRegsMap.
  using EntryTy = std::pair<std::string, std::set<std::string>>;
  bool Redo;
  do {
    Redo = false;
    std::deque<EntryTy> Worklist(DelegateToMap.begin(), DelegateToMap.end());

    while (!Worklist.empty()) {
      EntryTy Entry = Worklist.front();
      Worklist.pop_front();

      const std::string &CCName = Entry.first;
      std::set<std::string> &Registers = Entry.second;
      if (!Registers.empty())
        continue;

      for (auto &InnerEntry : Worklist) {
        const std::string &InnerCCName = InnerEntry.first;
        std::set<std::string> &InnerRegisters = InnerEntry.second;

        if (InnerRegisters.find(CCName) != InnerRegisters.end()) {
          AssignedRegsMap[InnerCCName].insert(AssignedRegsMap[CCName].begin(),
                                              AssignedRegsMap[CCName].end());
          InnerRegisters.erase(CCName);
        }
      }

      DelegateToMap.erase(CCName);
      Redo = true;
    }
  } while (Redo);

  if (AssignedRegsMap.empty())
    return;

  O << "\n#else\n\n";

  for (auto &Entry : AssignedRegsMap) {
    const std::string &RegName = Entry.first;
    std::set<std::string> &Registers = Entry.second;

    if (RegName.empty())
      continue;

    O << "const MCRegister " << Entry.first << "_ArgRegs[] = { ";

    if (Registers.empty()) {
      O << "0";
    } else {
      ListSeparator LS;
      for (const std::string &Reg : Registers)
        O << LS << Reg;
    }

    O << " };\n";
  }

  if (AssignedSwiftRegsMap.empty())
    return;

  O << "\n// Registers used by Swift.\n";
  for (auto &Entry : AssignedSwiftRegsMap) {
    const std::string &RegName = Entry.first;
    std::set<std::string> &Registers = Entry.second;

    O << "const MCRegister " << RegName << "_Swift_ArgRegs[] = { ";

    ListSeparator LS;
    for (const std::string &Reg : Registers)
      O << LS << Reg;

    O << " };\n";
  }
}

static TableGen::Emitter::OptClass<CallingConvEmitter>
    X("gen-callingconv", "Generate calling convention descriptions");
