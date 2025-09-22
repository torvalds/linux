//===- Attributes.cpp - Generate attributes -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>
using namespace llvm;

#define DEBUG_TYPE "attr-enum"

namespace {

class Attributes {
public:
  Attributes(RecordKeeper &R) : Records(R) {}
  void run(raw_ostream &OS);

private:
  void emitTargetIndependentNames(raw_ostream &OS);
  void emitFnAttrCompatCheck(raw_ostream &OS, bool IsStringAttr);
  void emitAttributeProperties(raw_ostream &OF);

  RecordKeeper &Records;
};

} // End anonymous namespace.

void Attributes::emitTargetIndependentNames(raw_ostream &OS) {
  OS << "#ifdef GET_ATTR_NAMES\n";
  OS << "#undef GET_ATTR_NAMES\n";

  OS << "#ifndef ATTRIBUTE_ALL\n";
  OS << "#define ATTRIBUTE_ALL(FIRST, SECOND)\n";
  OS << "#endif\n\n";

  auto Emit = [&](ArrayRef<StringRef> KindNames, StringRef MacroName) {
    OS << "#ifndef " << MacroName << "\n";
    OS << "#define " << MacroName
       << "(FIRST, SECOND) ATTRIBUTE_ALL(FIRST, SECOND)\n";
    OS << "#endif\n\n";
    for (StringRef KindName : KindNames) {
      for (auto *A : Records.getAllDerivedDefinitions(KindName)) {
        OS << MacroName << "(" << A->getName() << ","
           << A->getValueAsString("AttrString") << ")\n";
      }
    }
    OS << "#undef " << MacroName << "\n\n";
  };

  // Emit attribute enums in the same order llvm::Attribute::operator< expects.
  Emit({"EnumAttr", "TypeAttr", "IntAttr", "ConstantRangeAttr",
        "ConstantRangeListAttr"},
       "ATTRIBUTE_ENUM");
  Emit({"StrBoolAttr"}, "ATTRIBUTE_STRBOOL");
  Emit({"ComplexStrAttr"}, "ATTRIBUTE_COMPLEXSTR");

  OS << "#undef ATTRIBUTE_ALL\n";
  OS << "#endif\n\n";

  OS << "#ifdef GET_ATTR_ENUM\n";
  OS << "#undef GET_ATTR_ENUM\n";
  unsigned Value = 1; // Leave zero for AttrKind::None.
  for (StringRef KindName : {"EnumAttr", "TypeAttr", "IntAttr",
                             "ConstantRangeAttr", "ConstantRangeListAttr"}) {
    OS << "First" << KindName << " = " << Value << ",\n";
    for (auto *A : Records.getAllDerivedDefinitions(KindName)) {
      OS << A->getName() << " = " << Value << ",\n";
      Value++;
    }
    OS << "Last" << KindName << " = " << (Value - 1) << ",\n";
  }
  OS << "#endif\n\n";
}

void Attributes::emitFnAttrCompatCheck(raw_ostream &OS, bool IsStringAttr) {
  OS << "#ifdef GET_ATTR_COMPAT_FUNC\n";
  OS << "#undef GET_ATTR_COMPAT_FUNC\n";

  OS << "static inline bool hasCompatibleFnAttrs(const Function &Caller,\n"
     << "                                        const Function &Callee) {\n";
  OS << "  bool Ret = true;\n\n";

  std::vector<Record *> CompatRules =
      Records.getAllDerivedDefinitions("CompatRule");

  for (auto *Rule : CompatRules) {
    StringRef FuncName = Rule->getValueAsString("CompatFunc");
    OS << "  Ret &= " << FuncName << "(Caller, Callee";
    StringRef AttrName = Rule->getValueAsString("AttrName");
    if (!AttrName.empty())
      OS << ", \"" << AttrName << "\"";
    OS << ");\n";
  }

  OS << "\n";
  OS << "  return Ret;\n";
  OS << "}\n\n";

  std::vector<Record *> MergeRules =
      Records.getAllDerivedDefinitions("MergeRule");
  OS << "static inline void mergeFnAttrs(Function &Caller,\n"
     << "                                const Function &Callee) {\n";

  for (auto *Rule : MergeRules) {
    StringRef FuncName = Rule->getValueAsString("MergeFunc");
    OS << "  " << FuncName << "(Caller, Callee);\n";
  }

  OS << "}\n\n";

  OS << "#endif\n";
}

void Attributes::emitAttributeProperties(raw_ostream &OS) {
  OS << "#ifdef GET_ATTR_PROP_TABLE\n";
  OS << "#undef GET_ATTR_PROP_TABLE\n";
  OS << "static const uint8_t AttrPropTable[] = {\n";
  for (StringRef KindName : {"EnumAttr", "TypeAttr", "IntAttr",
                             "ConstantRangeAttr", "ConstantRangeListAttr"}) {
    for (auto *A : Records.getAllDerivedDefinitions(KindName)) {
      OS << "0";
      for (Init *P : *A->getValueAsListInit("Properties"))
        OS << " | AttributeProperty::" << cast<DefInit>(P)->getDef()->getName();
      OS << ",\n";
    }
  }
  OS << "};\n";
  OS << "#endif\n";
}

void Attributes::run(raw_ostream &OS) {
  emitTargetIndependentNames(OS);
  emitFnAttrCompatCheck(OS, false);
  emitAttributeProperties(OS);
}

static TableGen::Emitter::OptClass<Attributes> X("gen-attrs",
                                                 "Generate attributes");
