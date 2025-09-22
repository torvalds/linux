//===- RegisterBankEmitter.cpp - Generate a Register Bank Desc. -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend is responsible for emitting a description of a target
// register bank for a code generator.
//
//===----------------------------------------------------------------------===//

#include "Common/CodeGenRegisters.h"
#include "Common/CodeGenTarget.h"
#include "Common/InfoByHwMode.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

#define DEBUG_TYPE "register-bank-emitter"

using namespace llvm;

namespace {
class RegisterBank {

  /// A vector of register classes that are included in the register bank.
  typedef std::vector<const CodeGenRegisterClass *> RegisterClassesTy;

private:
  const Record &TheDef;

  /// The register classes that are covered by the register bank.
  RegisterClassesTy RCs;

  /// The register class with the largest register size.
  std::vector<const CodeGenRegisterClass *> RCsWithLargestRegSize;

public:
  RegisterBank(const Record &TheDef, unsigned NumModeIds)
      : TheDef(TheDef), RCsWithLargestRegSize(NumModeIds) {}

  /// Get the human-readable name for the bank.
  StringRef getName() const { return TheDef.getValueAsString("Name"); }
  /// Get the name of the enumerator in the ID enumeration.
  std::string getEnumeratorName() const {
    return (TheDef.getName() + "ID").str();
  }

  /// Get the name of the array holding the register class coverage data;
  std::string getCoverageArrayName() const {
    return (TheDef.getName() + "CoverageData").str();
  }

  /// Get the name of the global instance variable.
  StringRef getInstanceVarName() const { return TheDef.getName(); }

  const Record &getDef() const { return TheDef; }

  /// Get the register classes listed in the RegisterBank.RegisterClasses field.
  std::vector<const CodeGenRegisterClass *>
  getExplicitlySpecifiedRegisterClasses(
      const CodeGenRegBank &RegisterClassHierarchy) const {
    std::vector<const CodeGenRegisterClass *> RCs;
    for (const auto *RCDef : getDef().getValueAsListOfDefs("RegisterClasses"))
      RCs.push_back(RegisterClassHierarchy.getRegClass(RCDef));
    return RCs;
  }

  /// Add a register class to the bank without duplicates.
  void addRegisterClass(const CodeGenRegisterClass *RC) {
    if (llvm::is_contained(RCs, RC))
      return;

    // FIXME? We really want the register size rather than the spill size
    //        since the spill size may be bigger on some targets with
    //        limited load/store instructions. However, we don't store the
    //        register size anywhere (we could sum the sizes of the subregisters
    //        but there may be additional bits too) and we can't derive it from
    //        the VT's reliably due to Untyped.
    unsigned NumModeIds = RCsWithLargestRegSize.size();
    for (unsigned M = 0; M < NumModeIds; ++M) {
      if (RCsWithLargestRegSize[M] == nullptr)
        RCsWithLargestRegSize[M] = RC;
      else if (RCsWithLargestRegSize[M]->RSI.get(M).SpillSize <
               RC->RSI.get(M).SpillSize)
        RCsWithLargestRegSize[M] = RC;
      assert(RCsWithLargestRegSize[M] && "RC was nullptr?");
    }

    RCs.emplace_back(RC);
  }

  const CodeGenRegisterClass *getRCWithLargestRegSize(unsigned HwMode) const {
    return RCsWithLargestRegSize[HwMode];
  }

  iterator_range<typename RegisterClassesTy::const_iterator>
  register_classes() const {
    return llvm::make_range(RCs.begin(), RCs.end());
  }
};

class RegisterBankEmitter {
private:
  CodeGenTarget Target;
  RecordKeeper &Records;

  void emitHeader(raw_ostream &OS, const StringRef TargetName,
                  const std::vector<RegisterBank> &Banks);
  void emitBaseClassDefinition(raw_ostream &OS, const StringRef TargetName,
                               const std::vector<RegisterBank> &Banks);
  void emitBaseClassImplementation(raw_ostream &OS, const StringRef TargetName,
                                   std::vector<RegisterBank> &Banks);

public:
  RegisterBankEmitter(RecordKeeper &R) : Target(R), Records(R) {}

  void run(raw_ostream &OS);
};

} // end anonymous namespace

/// Emit code to declare the ID enumeration and external global instance
/// variables.
void RegisterBankEmitter::emitHeader(raw_ostream &OS,
                                     const StringRef TargetName,
                                     const std::vector<RegisterBank> &Banks) {
  // <Target>RegisterBankInfo.h
  OS << "namespace llvm {\n"
     << "namespace " << TargetName << " {\n"
     << "enum : unsigned {\n";

  OS << "  InvalidRegBankID = ~0u,\n";
  unsigned ID = 0;
  for (const auto &Bank : Banks)
    OS << "  " << Bank.getEnumeratorName() << " = " << ID++ << ",\n";
  OS << "  NumRegisterBanks,\n"
     << "};\n"
     << "} // end namespace " << TargetName << "\n"
     << "} // end namespace llvm\n";
}

/// Emit declarations of the <Target>GenRegisterBankInfo class.
void RegisterBankEmitter::emitBaseClassDefinition(
    raw_ostream &OS, const StringRef TargetName,
    const std::vector<RegisterBank> &Banks) {
  OS << "private:\n"
     << "  static const RegisterBank *RegBanks[];\n"
     << "  static const unsigned Sizes[];\n\n"
     << "protected:\n"
     << "  " << TargetName << "GenRegisterBankInfo(unsigned HwMode = 0);\n"
     << "\n";
}

/// Visit each register class belonging to the given register bank.
///
/// A class belongs to the bank iff any of these apply:
/// * It is explicitly specified
/// * It is a subclass of a class that is a member.
/// * It is a class containing subregisters of the registers of a class that
///   is a member. This is known as a subreg-class.
///
/// This function must be called for each explicitly specified register class.
///
/// \param RC The register class to search.
/// \param Kind A debug string containing the path the visitor took to reach RC.
/// \param VisitFn The action to take for each class visited. It may be called
///                multiple times for a given class if there are multiple paths
///                to the class.
static void visitRegisterBankClasses(
    const CodeGenRegBank &RegisterClassHierarchy,
    const CodeGenRegisterClass *RC, const Twine &Kind,
    std::function<void(const CodeGenRegisterClass *, StringRef)> VisitFn,
    SmallPtrSetImpl<const CodeGenRegisterClass *> &VisitedRCs) {

  // Make sure we only visit each class once to avoid infinite loops.
  if (!VisitedRCs.insert(RC).second)
    return;

  // Visit each explicitly named class.
  VisitFn(RC, Kind.str());

  for (const auto &PossibleSubclass : RegisterClassHierarchy.getRegClasses()) {
    std::string TmpKind =
        (Kind + " (" + PossibleSubclass.getName() + ")").str();

    // Visit each subclass of an explicitly named class.
    if (RC != &PossibleSubclass && RC->hasSubClass(&PossibleSubclass))
      visitRegisterBankClasses(RegisterClassHierarchy, &PossibleSubclass,
                               TmpKind + " " + RC->getName() + " subclass",
                               VisitFn, VisitedRCs);

    // Visit each class that contains only subregisters of RC with a common
    // subregister-index.
    //
    // More precisely, PossibleSubclass is a subreg-class iff Reg:SubIdx is in
    // PossibleSubclass for all registers Reg from RC using any
    // subregister-index SubReg
    for (const auto &SubIdx : RegisterClassHierarchy.getSubRegIndices()) {
      BitVector BV(RegisterClassHierarchy.getRegClasses().size());
      PossibleSubclass.getSuperRegClasses(&SubIdx, BV);
      if (BV.test(RC->EnumValue)) {
        std::string TmpKind2 = (Twine(TmpKind) + " " + RC->getName() +
                                " class-with-subregs: " + RC->getName())
                                   .str();
        VisitFn(&PossibleSubclass, TmpKind2);
      }
    }
  }
}

void RegisterBankEmitter::emitBaseClassImplementation(
    raw_ostream &OS, StringRef TargetName, std::vector<RegisterBank> &Banks) {
  const CodeGenRegBank &RegisterClassHierarchy = Target.getRegBank();
  const CodeGenHwModes &CGH = Target.getHwModes();

  OS << "namespace llvm {\n"
     << "namespace " << TargetName << " {\n";
  for (const auto &Bank : Banks) {
    std::vector<std::vector<const CodeGenRegisterClass *>> RCsGroupedByWord(
        (RegisterClassHierarchy.getRegClasses().size() + 31) / 32);

    for (const auto &RC : Bank.register_classes())
      RCsGroupedByWord[RC->EnumValue / 32].push_back(RC);

    OS << "const uint32_t " << Bank.getCoverageArrayName() << "[] = {\n";
    unsigned LowestIdxInWord = 0;
    for (const auto &RCs : RCsGroupedByWord) {
      OS << "    // " << LowestIdxInWord << "-" << (LowestIdxInWord + 31)
         << "\n";
      for (const auto &RC : RCs) {
        OS << "    (1u << (" << RC->getQualifiedIdName() << " - "
           << LowestIdxInWord << ")) |\n";
      }
      OS << "    0,\n";
      LowestIdxInWord += 32;
    }
    OS << "};\n";
  }
  OS << "\n";

  for (const auto &Bank : Banks) {
    std::string QualifiedBankID =
        (TargetName + "::" + Bank.getEnumeratorName()).str();
    OS << "constexpr RegisterBank " << Bank.getInstanceVarName() << "(/* ID */ "
       << QualifiedBankID << ", /* Name */ \"" << Bank.getName() << "\", "
       << "/* CoveredRegClasses */ " << Bank.getCoverageArrayName()
       << ", /* NumRegClasses */ "
       << RegisterClassHierarchy.getRegClasses().size() << ");\n";
  }
  OS << "} // end namespace " << TargetName << "\n"
     << "\n";

  OS << "const RegisterBank *" << TargetName
     << "GenRegisterBankInfo::RegBanks[] = {\n";
  for (const auto &Bank : Banks)
    OS << "    &" << TargetName << "::" << Bank.getInstanceVarName() << ",\n";
  OS << "};\n\n";

  unsigned NumModeIds = CGH.getNumModeIds();
  OS << "const unsigned " << TargetName << "GenRegisterBankInfo::Sizes[] = {\n";
  for (unsigned M = 0; M < NumModeIds; ++M) {
    OS << "    // Mode = " << M << " (";
    if (M == DefaultMode)
      OS << "Default";
    else
      OS << CGH.getMode(M).Name;
    OS << ")\n";
    for (const auto &Bank : Banks) {
      const CodeGenRegisterClass &RC = *Bank.getRCWithLargestRegSize(M);
      unsigned Size = RC.RSI.get(M).SpillSize;
      OS << "    " << Size << ",\n";
    }
  }
  OS << "};\n\n";

  OS << TargetName << "GenRegisterBankInfo::" << TargetName
     << "GenRegisterBankInfo(unsigned HwMode)\n"
     << "    : RegisterBankInfo(RegBanks, " << TargetName
     << "::NumRegisterBanks, Sizes, HwMode) {\n"
     << "  // Assert that RegBank indices match their ID's\n"
     << "#ifndef NDEBUG\n"
     << "  for (auto RB : enumerate(RegBanks))\n"
     << "    assert(RB.index() == RB.value()->getID() && \"Index != ID\");\n"
     << "#endif // NDEBUG\n"
     << "}\n"
     << "} // end namespace llvm\n";
}

void RegisterBankEmitter::run(raw_ostream &OS) {
  StringRef TargetName = Target.getName();
  const CodeGenRegBank &RegisterClassHierarchy = Target.getRegBank();
  const CodeGenHwModes &CGH = Target.getHwModes();

  Records.startTimer("Analyze records");
  std::vector<RegisterBank> Banks;
  for (const auto &V : Records.getAllDerivedDefinitions("RegisterBank")) {
    SmallPtrSet<const CodeGenRegisterClass *, 8> VisitedRCs;
    RegisterBank Bank(*V, CGH.getNumModeIds());

    for (const CodeGenRegisterClass *RC :
         Bank.getExplicitlySpecifiedRegisterClasses(RegisterClassHierarchy)) {
      visitRegisterBankClasses(
          RegisterClassHierarchy, RC, "explicit",
          [&Bank](const CodeGenRegisterClass *RC, StringRef Kind) {
            LLVM_DEBUG(dbgs()
                       << "Added " << RC->getName() << "(" << Kind << ")\n");
            Bank.addRegisterClass(RC);
          },
          VisitedRCs);
    }

    Banks.push_back(Bank);
  }

  // Warn about ambiguous MIR caused by register bank/class name clashes.
  Records.startTimer("Warn ambiguous");
  for (const auto &Class : RegisterClassHierarchy.getRegClasses()) {
    for (const auto &Bank : Banks) {
      if (Bank.getName().lower() == StringRef(Class.getName()).lower()) {
        PrintWarning(Bank.getDef().getLoc(), "Register bank names should be "
                                             "distinct from register classes "
                                             "to avoid ambiguous MIR");
        PrintNote(Bank.getDef().getLoc(), "RegisterBank was declared here");
        PrintNote(Class.getDef()->getLoc(), "RegisterClass was declared here");
      }
    }
  }

  Records.startTimer("Emit output");
  emitSourceFileHeader("Register Bank Source Fragments", OS);
  OS << "#ifdef GET_REGBANK_DECLARATIONS\n"
     << "#undef GET_REGBANK_DECLARATIONS\n";
  emitHeader(OS, TargetName, Banks);
  OS << "#endif // GET_REGBANK_DECLARATIONS\n\n"
     << "#ifdef GET_TARGET_REGBANK_CLASS\n"
     << "#undef GET_TARGET_REGBANK_CLASS\n";
  emitBaseClassDefinition(OS, TargetName, Banks);
  OS << "#endif // GET_TARGET_REGBANK_CLASS\n\n"
     << "#ifdef GET_TARGET_REGBANK_IMPL\n"
     << "#undef GET_TARGET_REGBANK_IMPL\n";
  emitBaseClassImplementation(OS, TargetName, Banks);
  OS << "#endif // GET_TARGET_REGBANK_IMPL\n";
}

static TableGen::Emitter::OptClass<RegisterBankEmitter>
    X("gen-register-bank", "Generate registers bank descriptions");
