//===- SubtargetFeatureInfo.cpp - Helpers for subtarget features ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SubtargetFeatureInfo.h"
#include "Types.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"

using namespace llvm;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SubtargetFeatureInfo::dump() const {
  errs() << getEnumName() << " " << Index << "\n" << *TheDef;
}
#endif

std::vector<std::pair<Record *, SubtargetFeatureInfo>>
SubtargetFeatureInfo::getAll(const RecordKeeper &Records) {
  std::vector<std::pair<Record *, SubtargetFeatureInfo>> SubtargetFeatures;
  std::vector<Record *> AllPredicates =
      Records.getAllDerivedDefinitions("Predicate");
  for (Record *Pred : AllPredicates) {
    // Ignore predicates that are not intended for the assembler.
    //
    // The "AssemblerMatcherPredicate" string should be promoted to an argument
    // if we re-use the machinery for non-assembler purposes in future.
    if (!Pred->getValueAsBit("AssemblerMatcherPredicate"))
      continue;

    if (Pred->getName().empty())
      PrintFatalError(Pred->getLoc(), "Predicate has no name!");

    // Ignore always true predicates.
    if (Pred->getValueAsString("CondString").empty())
      continue;

    SubtargetFeatures.emplace_back(
        Pred, SubtargetFeatureInfo(Pred, SubtargetFeatures.size()));
  }
  return SubtargetFeatures;
}

void SubtargetFeatureInfo::emitSubtargetFeatureBitEnumeration(
    const SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS,
    const std::map<std::string, unsigned> *HwModes) {
  OS << "// Bits for subtarget features that participate in "
     << "instruction matching.\n";
  unsigned Size = SubtargetFeatures.size();
  if (HwModes)
    Size += HwModes->size();

  OS << "enum SubtargetFeatureBits : " << getMinimalTypeForRange(Size)
     << " {\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;
    OS << "  " << SFI.getEnumBitName() << " = " << SFI.Index << ",\n";
  }

  if (HwModes) {
    unsigned Offset = SubtargetFeatures.size();
    for (const auto &M : *HwModes) {
      OS << "  Feature_HwMode" << M.second << "Bit = " << (M.second + Offset)
         << ",\n";
    }
  }

  OS << "};\n\n";
}

void SubtargetFeatureInfo::emitNameTable(
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  // Need to sort the name table so that lookup by the log of the enum value
  // gives the proper name. More specifically, for a feature of value 1<<n,
  // SubtargetFeatureNames[n] should be the name of the feature.
  uint64_t IndexUB = 0;
  for (const auto &SF : SubtargetFeatures)
    if (IndexUB <= SF.second.Index)
      IndexUB = SF.second.Index + 1;

  std::vector<std::string> Names;
  if (IndexUB > 0)
    Names.resize(IndexUB);
  for (const auto &SF : SubtargetFeatures)
    Names[SF.second.Index] = SF.second.getEnumName();

  OS << "static const char *SubtargetFeatureNames[] = {\n";
  for (uint64_t I = 0; I < IndexUB; ++I)
    OS << "  \"" << Names[I] << "\",\n";

  // A small number of targets have no predicates. Null terminate the array to
  // avoid a zero-length array.
  OS << "  nullptr\n"
     << "};\n\n";
}

void SubtargetFeatureInfo::emitComputeAvailableFeatures(
    StringRef TargetName, StringRef ClassName, StringRef FuncName,
    const SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS,
    StringRef ExtraParams, const std::map<std::string, unsigned> *HwModes) {
  OS << "PredicateBitset " << ClassName << "::\n"
     << FuncName << "(const " << TargetName << "Subtarget *Subtarget";
  if (!ExtraParams.empty())
    OS << ", " << ExtraParams;
  OS << ") const {\n";
  OS << "  PredicateBitset Features{};\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;
    StringRef CondStr = SFI.TheDef->getValueAsString("CondString");
    assert(!CondStr.empty() && "true predicate should have been filtered");

    OS << "  if (" << CondStr << ")\n";
    OS << "    Features.set(" << SFI.getEnumBitName() << ");\n";
  }

  if (HwModes) {
    for (const auto &M : *HwModes) {
      OS << "  if (" << M.first << ")\n";
      OS << "    Features.set(Feature_HwMode" << M.second << "Bit);\n";
    }
  }

  OS << "  return Features;\n";
  OS << "}\n\n";
}

// If ParenIfBinOp is true, print a surrounding () if Val uses && or ||.
static bool emitFeaturesAux(StringRef TargetName, const Init &Val,
                            bool ParenIfBinOp, raw_ostream &OS) {
  if (auto *D = dyn_cast<DefInit>(&Val)) {
    if (!D->getDef()->isSubClassOf("SubtargetFeature"))
      return true;
    OS << "FB[" << TargetName << "::" << D->getAsString() << "]";
    return false;
  }
  if (auto *D = dyn_cast<DagInit>(&Val)) {
    auto *Op = dyn_cast<DefInit>(D->getOperator());
    if (!Op)
      return true;
    StringRef OpName = Op->getDef()->getName();
    if (OpName == "not" && D->getNumArgs() == 1) {
      OS << '!';
      return emitFeaturesAux(TargetName, *D->getArg(0), true, OS);
    }
    if ((OpName == "any_of" || OpName == "all_of") && D->getNumArgs() > 0) {
      bool Paren = D->getNumArgs() > 1 && std::exchange(ParenIfBinOp, true);
      if (Paren)
        OS << '(';
      ListSeparator LS(OpName == "any_of" ? " || " : " && ");
      for (auto *Arg : D->getArgs()) {
        OS << LS;
        if (emitFeaturesAux(TargetName, *Arg, ParenIfBinOp, OS))
          return true;
      }
      if (Paren)
        OS << ')';
      return false;
    }
  }
  return true;
}

void SubtargetFeatureInfo::emitComputeAssemblerAvailableFeatures(
    StringRef TargetName, StringRef ClassName, StringRef FuncName,
    SubtargetFeatureInfoMap &SubtargetFeatures, raw_ostream &OS) {
  OS << "FeatureBitset ";
  if (!ClassName.empty())
    OS << TargetName << ClassName << "::\n";
  OS << FuncName << "(const FeatureBitset &FB) ";
  if (!ClassName.empty())
    OS << "const ";
  OS << "{\n";
  OS << "  FeatureBitset Features;\n";
  for (const auto &SF : SubtargetFeatures) {
    const SubtargetFeatureInfo &SFI = SF.second;

    OS << "  if (";
    emitFeaturesAux(TargetName, *SFI.TheDef->getValueAsDag("AssemblerCondDag"),
                    /*ParenIfBinOp=*/false, OS);
    OS << ")\n";
    OS << "    Features.set(" << SFI.getEnumBitName() << ");\n";
  }
  OS << "  return Features;\n";
  OS << "}\n\n";
}
