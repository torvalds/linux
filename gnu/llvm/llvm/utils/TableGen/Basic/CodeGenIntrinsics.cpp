//===- CodeGenIntrinsics.cpp - Intrinsic Class Wrapper --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper class for the 'Intrinsic' TableGen class.
//
//===----------------------------------------------------------------------===//

#include "CodeGenIntrinsics.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include <algorithm>
#include <cassert>
using namespace llvm;

//===----------------------------------------------------------------------===//
// CodeGenIntrinsic Implementation
//===----------------------------------------------------------------------===//

CodeGenIntrinsicTable::CodeGenIntrinsicTable(const RecordKeeper &RC) {
  std::vector<Record *> IntrProperties =
      RC.getAllDerivedDefinitions("IntrinsicProperty");

  std::vector<Record *> DefaultProperties;
  for (Record *Rec : IntrProperties)
    if (Rec->getValueAsBit("IsDefault"))
      DefaultProperties.push_back(Rec);

  std::vector<Record *> Defs = RC.getAllDerivedDefinitions("Intrinsic");
  Intrinsics.reserve(Defs.size());

  for (unsigned I = 0, e = Defs.size(); I != e; ++I)
    Intrinsics.push_back(CodeGenIntrinsic(Defs[I], DefaultProperties));

  llvm::sort(Intrinsics,
             [](const CodeGenIntrinsic &LHS, const CodeGenIntrinsic &RHS) {
               return std::tie(LHS.TargetPrefix, LHS.Name) <
                      std::tie(RHS.TargetPrefix, RHS.Name);
             });
  Targets.push_back({"", 0, 0});
  for (size_t I = 0, E = Intrinsics.size(); I < E; ++I)
    if (Intrinsics[I].TargetPrefix != Targets.back().Name) {
      Targets.back().Count = I - Targets.back().Offset;
      Targets.push_back({Intrinsics[I].TargetPrefix, I, 0});
    }
  Targets.back().Count = Intrinsics.size() - Targets.back().Offset;
}

CodeGenIntrinsic::CodeGenIntrinsic(Record *R,
                                   ArrayRef<Record *> DefaultProperties) {
  TheDef = R;
  std::string DefName = std::string(R->getName());
  ArrayRef<SMLoc> DefLoc = R->getLoc();
  Properties = 0;
  isOverloaded = false;
  isCommutative = false;
  canThrow = false;
  isNoReturn = false;
  isNoCallback = false;
  isNoSync = false;
  isNoFree = false;
  isWillReturn = false;
  isCold = false;
  isNoDuplicate = false;
  isNoMerge = false;
  isConvergent = false;
  isSpeculatable = false;
  hasSideEffects = false;
  isStrictFP = false;

  if (DefName.size() <= 4 || DefName.substr(0, 4) != "int_")
    PrintFatalError(DefLoc,
                    "Intrinsic '" + DefName + "' does not start with 'int_'!");

  EnumName = DefName.substr(4);

  if (R->getValue(
          "ClangBuiltinName")) // Ignore a missing ClangBuiltinName field.
    ClangBuiltinName = std::string(R->getValueAsString("ClangBuiltinName"));
  if (R->getValue("MSBuiltinName")) // Ignore a missing MSBuiltinName field.
    MSBuiltinName = std::string(R->getValueAsString("MSBuiltinName"));

  TargetPrefix = std::string(R->getValueAsString("TargetPrefix"));
  Name = std::string(R->getValueAsString("LLVMName"));

  if (Name == "") {
    // If an explicit name isn't specified, derive one from the DefName.
    Name = "llvm.";

    for (unsigned i = 0, e = EnumName.size(); i != e; ++i)
      Name += (EnumName[i] == '_') ? '.' : EnumName[i];
  } else {
    // Verify it starts with "llvm.".
    if (Name.size() <= 5 || Name.substr(0, 5) != "llvm.")
      PrintFatalError(DefLoc, "Intrinsic '" + DefName +
                                  "'s name does not start with 'llvm.'!");
  }

  // If TargetPrefix is specified, make sure that Name starts with
  // "llvm.<targetprefix>.".
  if (!TargetPrefix.empty()) {
    if (Name.size() < 6 + TargetPrefix.size() ||
        Name.substr(5, 1 + TargetPrefix.size()) != (TargetPrefix + "."))
      PrintFatalError(DefLoc, "Intrinsic '" + DefName +
                                  "' does not start with 'llvm." +
                                  TargetPrefix + ".'!");
  }

  if (auto *Types = R->getValue("Types")) {
    auto *TypeList = cast<ListInit>(Types->getValue());
    isOverloaded = R->getValueAsBit("isOverloaded");

    unsigned I = 0;
    for (unsigned E = R->getValueAsListInit("RetTypes")->size(); I < E; ++I)
      IS.RetTys.push_back(TypeList->getElementAsRecord(I));

    for (unsigned E = TypeList->size(); I < E; ++I)
      IS.ParamTys.push_back(TypeList->getElementAsRecord(I));
  }

  // Parse the intrinsic properties.
  ListInit *PropList = R->getValueAsListInit("IntrProperties");
  for (unsigned i = 0, e = PropList->size(); i != e; ++i) {
    Record *Property = PropList->getElementAsRecord(i);
    assert(Property->isSubClassOf("IntrinsicProperty") &&
           "Expected a property!");

    setProperty(Property);
  }

  // Set default properties to true.
  setDefaultProperties(R, DefaultProperties);

  // Also record the SDPatternOperator Properties.
  Properties = parseSDPatternOperatorProperties(R);

  // Sort the argument attributes for later benefit.
  for (auto &Attrs : ArgumentAttributes)
    llvm::sort(Attrs);
}

void CodeGenIntrinsic::setDefaultProperties(
    Record *R, ArrayRef<Record *> DefaultProperties) {
  // opt-out of using default attributes.
  if (R->getValueAsBit("DisableDefaultAttributes"))
    return;

  for (Record *Rec : DefaultProperties)
    setProperty(Rec);
}

void CodeGenIntrinsic::setProperty(Record *R) {
  if (R->getName() == "IntrNoMem")
    ME = MemoryEffects::none();
  else if (R->getName() == "IntrReadMem") {
    if (ME.onlyWritesMemory())
      PrintFatalError(TheDef->getLoc(),
                      Twine("IntrReadMem cannot be used after IntrNoMem or "
                            "IntrWriteMem. Default is ReadWrite"));
    ME &= MemoryEffects::readOnly();
  } else if (R->getName() == "IntrWriteMem") {
    if (ME.onlyReadsMemory())
      PrintFatalError(TheDef->getLoc(),
                      Twine("IntrWriteMem cannot be used after IntrNoMem or "
                            "IntrReadMem. Default is ReadWrite"));
    ME &= MemoryEffects::writeOnly();
  } else if (R->getName() == "IntrArgMemOnly")
    ME &= MemoryEffects::argMemOnly();
  else if (R->getName() == "IntrInaccessibleMemOnly")
    ME &= MemoryEffects::inaccessibleMemOnly();
  else if (R->getName() == "IntrInaccessibleMemOrArgMemOnly")
    ME &= MemoryEffects::inaccessibleOrArgMemOnly();
  else if (R->getName() == "Commutative")
    isCommutative = true;
  else if (R->getName() == "Throws")
    canThrow = true;
  else if (R->getName() == "IntrNoDuplicate")
    isNoDuplicate = true;
  else if (R->getName() == "IntrNoMerge")
    isNoMerge = true;
  else if (R->getName() == "IntrConvergent")
    isConvergent = true;
  else if (R->getName() == "IntrNoReturn")
    isNoReturn = true;
  else if (R->getName() == "IntrNoCallback")
    isNoCallback = true;
  else if (R->getName() == "IntrNoSync")
    isNoSync = true;
  else if (R->getName() == "IntrNoFree")
    isNoFree = true;
  else if (R->getName() == "IntrWillReturn")
    isWillReturn = !isNoReturn;
  else if (R->getName() == "IntrCold")
    isCold = true;
  else if (R->getName() == "IntrSpeculatable")
    isSpeculatable = true;
  else if (R->getName() == "IntrHasSideEffects")
    hasSideEffects = true;
  else if (R->getName() == "IntrStrictFP")
    isStrictFP = true;
  else if (R->isSubClassOf("NoCapture")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, NoCapture);
  } else if (R->isSubClassOf("NoAlias")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, NoAlias);
  } else if (R->isSubClassOf("NoUndef")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, NoUndef);
  } else if (R->isSubClassOf("NonNull")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, NonNull);
  } else if (R->isSubClassOf("Returned")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, Returned);
  } else if (R->isSubClassOf("ReadOnly")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, ReadOnly);
  } else if (R->isSubClassOf("WriteOnly")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, WriteOnly);
  } else if (R->isSubClassOf("ReadNone")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, ReadNone);
  } else if (R->isSubClassOf("ImmArg")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    addArgAttribute(ArgNo, ImmArg);
  } else if (R->isSubClassOf("Align")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    uint64_t Align = R->getValueAsInt("Align");
    addArgAttribute(ArgNo, Alignment, Align);
  } else if (R->isSubClassOf("Dereferenceable")) {
    unsigned ArgNo = R->getValueAsInt("ArgNo");
    uint64_t Bytes = R->getValueAsInt("Bytes");
    addArgAttribute(ArgNo, Dereferenceable, Bytes);
  } else
    llvm_unreachable("Unknown property!");
}

bool CodeGenIntrinsic::isParamAPointer(unsigned ParamIdx) const {
  if (ParamIdx >= IS.ParamTys.size())
    return false;
  return (IS.ParamTys[ParamIdx]->isSubClassOf("LLVMQualPointerType") ||
          IS.ParamTys[ParamIdx]->isSubClassOf("LLVMAnyPointerType"));
}

bool CodeGenIntrinsic::isParamImmArg(unsigned ParamIdx) const {
  // Convert argument index to attribute index starting from `FirstArgIndex`.
  ++ParamIdx;
  if (ParamIdx >= ArgumentAttributes.size())
    return false;
  ArgAttribute Val{ImmArg, 0};
  return std::binary_search(ArgumentAttributes[ParamIdx].begin(),
                            ArgumentAttributes[ParamIdx].end(), Val);
}

void CodeGenIntrinsic::addArgAttribute(unsigned Idx, ArgAttrKind AK,
                                       uint64_t V) {
  if (Idx >= ArgumentAttributes.size())
    ArgumentAttributes.resize(Idx + 1);
  ArgumentAttributes[Idx].emplace_back(AK, V);
}
