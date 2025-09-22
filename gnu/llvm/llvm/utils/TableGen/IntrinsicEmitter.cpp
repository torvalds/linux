//===- IntrinsicEmitter.cpp - Generate intrinsic information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tablegen backend emits information about intrinsic functions.
//
//===----------------------------------------------------------------------===//

#include "Basic/CodeGenIntrinsics.h"
#include "Basic/SequenceToOffsetTable.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringToOffsetTable.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using namespace llvm;

cl::OptionCategory GenIntrinsicCat("Options for -gen-intrinsic-enums");
cl::opt<std::string>
    IntrinsicPrefix("intrinsic-prefix",
                    cl::desc("Generate intrinsics with this target prefix"),
                    cl::value_desc("target prefix"), cl::cat(GenIntrinsicCat));

namespace {
class IntrinsicEmitter {
  RecordKeeper &Records;

public:
  IntrinsicEmitter(RecordKeeper &R) : Records(R) {}

  void run(raw_ostream &OS, bool Enums);

  void EmitEnumInfo(const CodeGenIntrinsicTable &Ints, raw_ostream &OS);
  void EmitArgKind(raw_ostream &OS);
  void EmitIITInfo(raw_ostream &OS);
  void EmitTargetInfo(const CodeGenIntrinsicTable &Ints, raw_ostream &OS);
  void EmitIntrinsicToNameTable(const CodeGenIntrinsicTable &Ints,
                                raw_ostream &OS);
  void EmitIntrinsicToOverloadTable(const CodeGenIntrinsicTable &Ints,
                                    raw_ostream &OS);
  void EmitGenerator(const CodeGenIntrinsicTable &Ints, raw_ostream &OS);
  void EmitAttributes(const CodeGenIntrinsicTable &Ints, raw_ostream &OS);
  void EmitIntrinsicToBuiltinMap(const CodeGenIntrinsicTable &Ints,
                                 bool IsClang, raw_ostream &OS);
};
} // End anonymous namespace

//===----------------------------------------------------------------------===//
// IntrinsicEmitter Implementation
//===----------------------------------------------------------------------===//

void IntrinsicEmitter::run(raw_ostream &OS, bool Enums) {
  emitSourceFileHeader("Intrinsic Function Source Fragment", OS);

  CodeGenIntrinsicTable Ints(Records);

  if (Enums) {
    // Emit the enum information.
    EmitEnumInfo(Ints, OS);

    // Emit ArgKind for Intrinsics.h.
    EmitArgKind(OS);
  } else {
    // Emit IIT_Info constants.
    EmitIITInfo(OS);

    // Emit the target metadata.
    EmitTargetInfo(Ints, OS);

    // Emit the intrinsic ID -> name table.
    EmitIntrinsicToNameTable(Ints, OS);

    // Emit the intrinsic ID -> overload table.
    EmitIntrinsicToOverloadTable(Ints, OS);

    // Emit the intrinsic declaration generator.
    EmitGenerator(Ints, OS);

    // Emit the intrinsic parameter attributes.
    EmitAttributes(Ints, OS);

    // Emit code to translate GCC builtins into LLVM intrinsics.
    EmitIntrinsicToBuiltinMap(Ints, true, OS);

    // Emit code to translate MS builtins into LLVM intrinsics.
    EmitIntrinsicToBuiltinMap(Ints, false, OS);
  }
}

void IntrinsicEmitter::EmitEnumInfo(const CodeGenIntrinsicTable &Ints,
                                    raw_ostream &OS) {
  // Find the TargetSet for which to generate enums. There will be an initial
  // set with an empty target prefix which will include target independent
  // intrinsics like dbg.value.
  const CodeGenIntrinsicTable::TargetSet *Set = nullptr;
  for (const auto &Target : Ints.Targets) {
    if (Target.Name == IntrinsicPrefix) {
      Set = &Target;
      break;
    }
  }
  if (!Set) {
    std::vector<std::string> KnownTargets;
    for (const auto &Target : Ints.Targets)
      if (!Target.Name.empty())
        KnownTargets.push_back(Target.Name);
    PrintFatalError("tried to generate intrinsics for unknown target " +
                    IntrinsicPrefix +
                    "\nKnown targets are: " + join(KnownTargets, ", ") + "\n");
  }

  // Generate a complete header for target specific intrinsics.
  if (IntrinsicPrefix.empty()) {
    OS << "#ifdef GET_INTRINSIC_ENUM_VALUES\n";
  } else {
    std::string UpperPrefix = StringRef(IntrinsicPrefix).upper();
    OS << "#ifndef LLVM_IR_INTRINSIC_" << UpperPrefix << "_ENUMS_H\n";
    OS << "#define LLVM_IR_INTRINSIC_" << UpperPrefix << "_ENUMS_H\n\n";
    OS << "namespace llvm {\n";
    OS << "namespace Intrinsic {\n";
    OS << "enum " << UpperPrefix << "Intrinsics : unsigned {\n";
  }

  OS << "// Enum values for intrinsics\n";
  for (unsigned i = Set->Offset, e = Set->Offset + Set->Count; i != e; ++i) {
    OS << "    " << Ints[i].EnumName;

    // Assign a value to the first intrinsic in this target set so that all
    // intrinsic ids are distinct.
    if (i == Set->Offset)
      OS << " = " << (Set->Offset + 1);

    OS << ", ";
    if (Ints[i].EnumName.size() < 40)
      OS.indent(40 - Ints[i].EnumName.size());
    OS << " // " << Ints[i].Name << "\n";
  }

  // Emit num_intrinsics into the target neutral enum.
  if (IntrinsicPrefix.empty()) {
    OS << "    num_intrinsics = " << (Ints.size() + 1) << "\n";
    OS << "#endif\n\n";
  } else {
    OS << "}; // enum\n";
    OS << "} // namespace Intrinsic\n";
    OS << "} // namespace llvm\n\n";
    OS << "#endif\n";
  }
}

void IntrinsicEmitter::EmitArgKind(raw_ostream &OS) {
  if (!IntrinsicPrefix.empty())
    return;
  OS << "// llvm::Intrinsic::IITDescriptor::ArgKind\n";
  OS << "#ifdef GET_INTRINSIC_ARGKIND\n";
  if (auto RecArgKind = Records.getDef("ArgKind")) {
    for (auto &RV : RecArgKind->getValues())
      OS << "    AK_" << RV.getName() << " = " << *RV.getValue() << ",\n";
  } else {
    OS << "#error \"ArgKind is not defined\"\n";
  }
  OS << "#endif\n\n";
}

void IntrinsicEmitter::EmitIITInfo(raw_ostream &OS) {
  OS << "#ifdef GET_INTRINSIC_IITINFO\n";
  std::array<StringRef, 256> RecsByNumber;
  auto IIT_Base = Records.getAllDerivedDefinitionsIfDefined("IIT_Base");
  for (auto Rec : IIT_Base) {
    auto Number = Rec->getValueAsInt("Number");
    assert(0 <= Number && Number < (int)RecsByNumber.size() &&
           "IIT_Info.Number should be uint8_t");
    assert(RecsByNumber[Number].empty() && "Duplicate IIT_Info.Number");
    RecsByNumber[Number] = Rec->getName();
  }
  if (IIT_Base.size() > 0) {
    for (unsigned I = 0, E = RecsByNumber.size(); I < E; ++I)
      if (!RecsByNumber[I].empty())
        OS << "  " << RecsByNumber[I] << " = " << I << ",\n";
  } else {
    OS << "#error \"class IIT_Base is not defined\"\n";
  }
  OS << "#endif\n\n";
}

void IntrinsicEmitter::EmitTargetInfo(const CodeGenIntrinsicTable &Ints,
                                      raw_ostream &OS) {
  OS << "// Target mapping\n";
  OS << "#ifdef GET_INTRINSIC_TARGET_DATA\n";
  OS << "struct IntrinsicTargetInfo {\n"
     << "  llvm::StringLiteral Name;\n"
     << "  size_t Offset;\n"
     << "  size_t Count;\n"
     << "};\n";
  OS << "static constexpr IntrinsicTargetInfo TargetInfos[] = {\n";
  for (const auto &Target : Ints.Targets)
    OS << "  {llvm::StringLiteral(\"" << Target.Name << "\"), " << Target.Offset
       << ", " << Target.Count << "},\n";
  OS << "};\n";
  OS << "#endif\n\n";
}

void IntrinsicEmitter::EmitIntrinsicToNameTable(
    const CodeGenIntrinsicTable &Ints, raw_ostream &OS) {
  OS << "// Intrinsic ID to name table\n";
  OS << "#ifdef GET_INTRINSIC_NAME_TABLE\n";
  OS << "  // Note that entry #0 is the invalid intrinsic!\n";
  for (unsigned i = 0, e = Ints.size(); i != e; ++i)
    OS << "  \"" << Ints[i].Name << "\",\n";
  OS << "#endif\n\n";
}

void IntrinsicEmitter::EmitIntrinsicToOverloadTable(
    const CodeGenIntrinsicTable &Ints, raw_ostream &OS) {
  OS << "// Intrinsic ID to overload bitset\n";
  OS << "#ifdef GET_INTRINSIC_OVERLOAD_TABLE\n";
  OS << "static const uint8_t OTable[] = {\n";
  OS << "  0";
  for (unsigned i = 0, e = Ints.size(); i != e; ++i) {
    // Add one to the index so we emit a null bit for the invalid #0 intrinsic.
    if ((i + 1) % 8 == 0)
      OS << ",\n  0";
    if (Ints[i].isOverloaded)
      OS << " | (1<<" << (i + 1) % 8 << ')';
  }
  OS << "\n};\n\n";
  // OTable contains a true bit at the position if the intrinsic is overloaded.
  OS << "return (OTable[id/8] & (1 << (id%8))) != 0;\n";
  OS << "#endif\n\n";
}

/// ComputeFixedEncoding - If we can encode the type signature for this
/// intrinsic into 32 bits, return it.  If not, return ~0U.
static void ComputeFixedEncoding(const CodeGenIntrinsic &Int,
                                 std::vector<unsigned char> &TypeSig) {
  if (auto *R = Int.TheDef->getValue("TypeSig")) {
    for (auto &a : cast<ListInit>(R->getValue())->getValues()) {
      for (auto &b : cast<ListInit>(a)->getValues())
        TypeSig.push_back(cast<IntInit>(b)->getValue());
    }
  }
}

static void printIITEntry(raw_ostream &OS, unsigned char X) {
  OS << (unsigned)X;
}

void IntrinsicEmitter::EmitGenerator(const CodeGenIntrinsicTable &Ints,
                                     raw_ostream &OS) {
  // If we can compute a 32-bit fixed encoding for this intrinsic, do so and
  // capture it in this vector, otherwise store a ~0U.
  std::vector<unsigned> FixedEncodings;

  SequenceToOffsetTable<std::vector<unsigned char>> LongEncodingTable;

  std::vector<unsigned char> TypeSig;

  // Compute the unique argument type info.
  for (unsigned i = 0, e = Ints.size(); i != e; ++i) {
    // Get the signature for the intrinsic.
    TypeSig.clear();
    ComputeFixedEncoding(Ints[i], TypeSig);

    // Check to see if we can encode it into a 32-bit word.  We can only encode
    // 8 nibbles into a 32-bit word.
    if (TypeSig.size() <= 8) {
      bool Failed = false;
      unsigned Result = 0;
      for (unsigned i = 0, e = TypeSig.size(); i != e; ++i) {
        // If we had an unencodable argument, bail out.
        if (TypeSig[i] > 15) {
          Failed = true;
          break;
        }
        Result = (Result << 4) | TypeSig[e - i - 1];
      }

      // If this could be encoded into a 31-bit word, return it.
      if (!Failed && (Result >> 31) == 0) {
        FixedEncodings.push_back(Result);
        continue;
      }
    }

    // Otherwise, we're going to unique the sequence into the
    // LongEncodingTable, and use its offset in the 32-bit table instead.
    LongEncodingTable.add(TypeSig);

    // This is a placehold that we'll replace after the table is laid out.
    FixedEncodings.push_back(~0U);
  }

  LongEncodingTable.layout();

  OS << "// Global intrinsic function declaration type table.\n";
  OS << "#ifdef GET_INTRINSIC_GENERATOR_GLOBAL\n";

  OS << "static const unsigned IIT_Table[] = {\n  ";

  for (unsigned i = 0, e = FixedEncodings.size(); i != e; ++i) {
    if ((i & 7) == 7)
      OS << "\n  ";

    // If the entry fit in the table, just emit it.
    if (FixedEncodings[i] != ~0U) {
      OS << "0x" << Twine::utohexstr(FixedEncodings[i]) << ", ";
      continue;
    }

    TypeSig.clear();
    ComputeFixedEncoding(Ints[i], TypeSig);

    // Otherwise, emit the offset into the long encoding table.  We emit it this
    // way so that it is easier to read the offset in the .def file.
    OS << "(1U<<31) | " << LongEncodingTable.get(TypeSig) << ", ";
  }

  OS << "0\n};\n\n";

  // Emit the shared table of register lists.
  OS << "static const unsigned char IIT_LongEncodingTable[] = {\n";
  if (!LongEncodingTable.empty())
    LongEncodingTable.emit(OS, printIITEntry);
  OS << "  255\n};\n\n";

  OS << "#endif\n\n"; // End of GET_INTRINSIC_GENERATOR_GLOBAL
}

namespace {
std::optional<bool> compareFnAttributes(const CodeGenIntrinsic *L,
                                        const CodeGenIntrinsic *R) {
  // Sort throwing intrinsics after non-throwing intrinsics.
  if (L->canThrow != R->canThrow)
    return R->canThrow;

  if (L->isNoDuplicate != R->isNoDuplicate)
    return R->isNoDuplicate;

  if (L->isNoMerge != R->isNoMerge)
    return R->isNoMerge;

  if (L->isNoReturn != R->isNoReturn)
    return R->isNoReturn;

  if (L->isNoCallback != R->isNoCallback)
    return R->isNoCallback;

  if (L->isNoSync != R->isNoSync)
    return R->isNoSync;

  if (L->isNoFree != R->isNoFree)
    return R->isNoFree;

  if (L->isWillReturn != R->isWillReturn)
    return R->isWillReturn;

  if (L->isCold != R->isCold)
    return R->isCold;

  if (L->isConvergent != R->isConvergent)
    return R->isConvergent;

  if (L->isSpeculatable != R->isSpeculatable)
    return R->isSpeculatable;

  if (L->hasSideEffects != R->hasSideEffects)
    return R->hasSideEffects;

  if (L->isStrictFP != R->isStrictFP)
    return R->isStrictFP;

  // Try to order by readonly/readnone attribute.
  uint32_t LK = L->ME.toIntValue();
  uint32_t RK = R->ME.toIntValue();
  if (LK != RK)
    return (LK > RK);

  return std::nullopt;
}

struct FnAttributeComparator {
  bool operator()(const CodeGenIntrinsic *L, const CodeGenIntrinsic *R) const {
    return compareFnAttributes(L, R).value_or(false);
  }
};

struct AttributeComparator {
  bool operator()(const CodeGenIntrinsic *L, const CodeGenIntrinsic *R) const {
    if (std::optional<bool> Res = compareFnAttributes(L, R))
      return *Res;

    // Order by argument attributes.
    // This is reliable because each side is already sorted internally.
    return (L->ArgumentAttributes < R->ArgumentAttributes);
  }
};
} // End anonymous namespace

/// EmitAttributes - This emits the Intrinsic::getAttributes method.
void IntrinsicEmitter::EmitAttributes(const CodeGenIntrinsicTable &Ints,
                                      raw_ostream &OS) {
  OS << "// Add parameter attributes that are not common to all intrinsics.\n";
  OS << "#ifdef GET_INTRINSIC_ATTRIBUTES\n";

  // Compute unique argument attribute sets.
  std::map<SmallVector<CodeGenIntrinsic::ArgAttribute, 0>, unsigned>
      UniqArgAttributes;
  OS << "static AttributeSet getIntrinsicArgAttributeSet("
     << "LLVMContext &C, unsigned ID) {\n"
     << "  switch (ID) {\n"
     << "  default: llvm_unreachable(\"Invalid attribute set number\");\n";
  for (const CodeGenIntrinsic &Int : Ints) {
    for (auto &Attrs : Int.ArgumentAttributes) {
      if (Attrs.empty())
        continue;

      unsigned ID = UniqArgAttributes.size();
      if (!UniqArgAttributes.try_emplace(Attrs, ID).second)
        continue;

      assert(is_sorted(Attrs) && "Argument attributes are not sorted");

      OS << "  case " << ID << ":\n";
      OS << "    return AttributeSet::get(C, {\n";
      for (const CodeGenIntrinsic::ArgAttribute &Attr : Attrs) {
        switch (Attr.Kind) {
        case CodeGenIntrinsic::NoCapture:
          OS << "      Attribute::get(C, Attribute::NoCapture),\n";
          break;
        case CodeGenIntrinsic::NoAlias:
          OS << "      Attribute::get(C, Attribute::NoAlias),\n";
          break;
        case CodeGenIntrinsic::NoUndef:
          OS << "      Attribute::get(C, Attribute::NoUndef),\n";
          break;
        case CodeGenIntrinsic::NonNull:
          OS << "      Attribute::get(C, Attribute::NonNull),\n";
          break;
        case CodeGenIntrinsic::Returned:
          OS << "      Attribute::get(C, Attribute::Returned),\n";
          break;
        case CodeGenIntrinsic::ReadOnly:
          OS << "      Attribute::get(C, Attribute::ReadOnly),\n";
          break;
        case CodeGenIntrinsic::WriteOnly:
          OS << "      Attribute::get(C, Attribute::WriteOnly),\n";
          break;
        case CodeGenIntrinsic::ReadNone:
          OS << "      Attribute::get(C, Attribute::ReadNone),\n";
          break;
        case CodeGenIntrinsic::ImmArg:
          OS << "      Attribute::get(C, Attribute::ImmArg),\n";
          break;
        case CodeGenIntrinsic::Alignment:
          OS << "      Attribute::get(C, Attribute::Alignment, " << Attr.Value
             << "),\n";
          break;
        case CodeGenIntrinsic::Dereferenceable:
          OS << "      Attribute::get(C, Attribute::Dereferenceable, "
             << Attr.Value << "),\n";
          break;
        }
      }
      OS << "    });\n";
    }
  }
  OS << "  }\n";
  OS << "}\n\n";

  // Compute unique function attribute sets.
  std::map<const CodeGenIntrinsic *, unsigned, FnAttributeComparator>
      UniqFnAttributes;
  OS << "static AttributeSet getIntrinsicFnAttributeSet("
     << "LLVMContext &C, unsigned ID) {\n"
     << "  switch (ID) {\n"
     << "  default: llvm_unreachable(\"Invalid attribute set number\");\n";
  for (const CodeGenIntrinsic &Intrinsic : Ints) {
    unsigned ID = UniqFnAttributes.size();
    if (!UniqFnAttributes.try_emplace(&Intrinsic, ID).second)
      continue;

    OS << "  case " << ID << ":\n"
       << "    return AttributeSet::get(C, {\n";
    if (!Intrinsic.canThrow)
      OS << "      Attribute::get(C, Attribute::NoUnwind),\n";
    if (Intrinsic.isNoReturn)
      OS << "      Attribute::get(C, Attribute::NoReturn),\n";
    if (Intrinsic.isNoCallback)
      OS << "      Attribute::get(C, Attribute::NoCallback),\n";
    if (Intrinsic.isNoSync)
      OS << "      Attribute::get(C, Attribute::NoSync),\n";
    if (Intrinsic.isNoFree)
      OS << "      Attribute::get(C, Attribute::NoFree),\n";
    if (Intrinsic.isWillReturn)
      OS << "      Attribute::get(C, Attribute::WillReturn),\n";
    if (Intrinsic.isCold)
      OS << "      Attribute::get(C, Attribute::Cold),\n";
    if (Intrinsic.isNoDuplicate)
      OS << "      Attribute::get(C, Attribute::NoDuplicate),\n";
    if (Intrinsic.isNoMerge)
      OS << "      Attribute::get(C, Attribute::NoMerge),\n";
    if (Intrinsic.isConvergent)
      OS << "      Attribute::get(C, Attribute::Convergent),\n";
    if (Intrinsic.isSpeculatable)
      OS << "      Attribute::get(C, Attribute::Speculatable),\n";
    if (Intrinsic.isStrictFP)
      OS << "      Attribute::get(C, Attribute::StrictFP),\n";

    MemoryEffects ME = Intrinsic.ME;
    // TODO: IntrHasSideEffects should affect not only readnone intrinsics.
    if (ME.doesNotAccessMemory() && Intrinsic.hasSideEffects)
      ME = MemoryEffects::unknown();
    if (ME != MemoryEffects::unknown()) {
      OS << "      Attribute::getWithMemoryEffects(C, "
         << "MemoryEffects::createFromIntValue(" << ME.toIntValue() << ")),\n";
    }
    OS << "    });\n";
  }
  OS << "  }\n";
  OS << "}\n\n";
  OS << "AttributeList Intrinsic::getAttributes(LLVMContext &C, ID id) {\n";

  // Compute the maximum number of attribute arguments and the map
  typedef std::map<const CodeGenIntrinsic *, unsigned, AttributeComparator>
      UniqAttrMapTy;
  UniqAttrMapTy UniqAttributes;
  unsigned maxArgAttrs = 0;
  unsigned AttrNum = 0;
  for (unsigned i = 0, e = Ints.size(); i != e; ++i) {
    const CodeGenIntrinsic &intrinsic = Ints[i];
    maxArgAttrs =
        std::max(maxArgAttrs, unsigned(intrinsic.ArgumentAttributes.size()));
    unsigned &N = UniqAttributes[&intrinsic];
    if (N)
      continue;
    N = ++AttrNum;
    assert(N < 65536 && "Too many unique attributes for table!");
  }

  // Emit an array of AttributeList.  Most intrinsics will have at least one
  // entry, for the function itself (index ~1), which is usually nounwind.
  OS << "  static const uint16_t IntrinsicsToAttributesMap[] = {\n";

  for (unsigned i = 0, e = Ints.size(); i != e; ++i) {
    const CodeGenIntrinsic &intrinsic = Ints[i];

    OS << "    " << UniqAttributes[&intrinsic] << ", // " << intrinsic.Name
       << "\n";
  }
  OS << "  };\n\n";

  OS << "  std::pair<unsigned, AttributeSet> AS[" << maxArgAttrs + 1 << "];\n";
  OS << "  unsigned NumAttrs = 0;\n";
  OS << "  if (id != 0) {\n";
  OS << "    switch(IntrinsicsToAttributesMap[id - 1]) {\n";
  OS << "    default: llvm_unreachable(\"Invalid attribute number\");\n";
  for (auto UniqAttribute : UniqAttributes) {
    OS << "    case " << UniqAttribute.second << ": {\n";

    const CodeGenIntrinsic &Intrinsic = *(UniqAttribute.first);

    // Keep track of the number of attributes we're writing out.
    unsigned numAttrs = 0;

    for (const auto &[AttrIdx, Attrs] :
         enumerate(Intrinsic.ArgumentAttributes)) {
      if (Attrs.empty())
        continue;

      unsigned ID = UniqArgAttributes.find(Attrs)->second;
      OS << "      AS[" << numAttrs++ << "] = {" << AttrIdx
         << ", getIntrinsicArgAttributeSet(C, " << ID << ")};\n";
    }

    if (!Intrinsic.canThrow ||
        (Intrinsic.ME != MemoryEffects::unknown() &&
         !Intrinsic.hasSideEffects) ||
        Intrinsic.isNoReturn || Intrinsic.isNoCallback || Intrinsic.isNoSync ||
        Intrinsic.isNoFree || Intrinsic.isWillReturn || Intrinsic.isCold ||
        Intrinsic.isNoDuplicate || Intrinsic.isNoMerge ||
        Intrinsic.isConvergent || Intrinsic.isSpeculatable ||
        Intrinsic.isStrictFP) {
      unsigned ID = UniqFnAttributes.find(&Intrinsic)->second;
      OS << "      AS[" << numAttrs++ << "] = {AttributeList::FunctionIndex, "
         << "getIntrinsicFnAttributeSet(C, " << ID << ")};\n";
    }

    if (numAttrs) {
      OS << "      NumAttrs = " << numAttrs << ";\n";
      OS << "      break;\n";
      OS << "    }\n";
    } else {
      OS << "      return AttributeList();\n";
      OS << "    }\n";
    }
  }

  OS << "    }\n";
  OS << "  }\n";
  OS << "  return AttributeList::get(C, ArrayRef(AS, NumAttrs));\n";
  OS << "}\n";
  OS << "#endif // GET_INTRINSIC_ATTRIBUTES\n\n";
}

void IntrinsicEmitter::EmitIntrinsicToBuiltinMap(
    const CodeGenIntrinsicTable &Ints, bool IsClang, raw_ostream &OS) {
  StringRef CompilerName = (IsClang ? "Clang" : "MS");
  StringRef UpperCompilerName = (IsClang ? "CLANG" : "MS");
  typedef std::map<std::string, std::map<std::string, std::string>> BIMTy;
  BIMTy BuiltinMap;
  StringToOffsetTable Table;
  for (unsigned i = 0, e = Ints.size(); i != e; ++i) {
    const std::string &BuiltinName =
        IsClang ? Ints[i].ClangBuiltinName : Ints[i].MSBuiltinName;
    if (!BuiltinName.empty()) {
      // Get the map for this target prefix.
      std::map<std::string, std::string> &BIM =
          BuiltinMap[Ints[i].TargetPrefix];

      if (!BIM.insert(std::pair(BuiltinName, Ints[i].EnumName)).second)
        PrintFatalError(Ints[i].TheDef->getLoc(),
                        "Intrinsic '" + Ints[i].TheDef->getName() +
                            "': duplicate " + CompilerName + " builtin name!");
      Table.GetOrAddStringOffset(BuiltinName);
    }
  }

  OS << "// Get the LLVM intrinsic that corresponds to a builtin.\n";
  OS << "// This is used by the C front-end.  The builtin name is passed\n";
  OS << "// in as BuiltinName, and a target prefix (e.g. 'ppc') is passed\n";
  OS << "// in as TargetPrefix.  The result is assigned to 'IntrinsicID'.\n";
  OS << "#ifdef GET_LLVM_INTRINSIC_FOR_" << UpperCompilerName << "_BUILTIN\n";

  OS << "Intrinsic::ID Intrinsic::getIntrinsicFor" << CompilerName
     << "Builtin(const char "
     << "*TargetPrefixStr, StringRef BuiltinNameStr) {\n";

  if (Table.Empty()) {
    OS << "  return Intrinsic::not_intrinsic;\n";
    OS << "}\n";
    OS << "#endif\n\n";
    return;
  }

  OS << "  static const char BuiltinNames[] = {\n";
  Table.EmitCharArray(OS);
  OS << "  };\n\n";

  OS << "  struct BuiltinEntry {\n";
  OS << "    Intrinsic::ID IntrinID;\n";
  OS << "    unsigned StrTabOffset;\n";
  OS << "    const char *getName() const {\n";
  OS << "      return &BuiltinNames[StrTabOffset];\n";
  OS << "    }\n";
  OS << "    bool operator<(StringRef RHS) const {\n";
  OS << "      return strncmp(getName(), RHS.data(), RHS.size()) < 0;\n";
  OS << "    }\n";
  OS << "  };\n";

  OS << "  StringRef TargetPrefix(TargetPrefixStr);\n\n";

  // Note: this could emit significantly better code if we cared.
  for (auto &I : BuiltinMap) {
    OS << "  ";
    if (!I.first.empty())
      OS << "if (TargetPrefix == \"" << I.first << "\") ";
    else
      OS << "/* Target Independent Builtins */ ";
    OS << "{\n";

    // Emit the comparisons for this target prefix.
    OS << "    static const BuiltinEntry " << I.first << "Names[] = {\n";
    for (const auto &P : I.second) {
      OS << "      {Intrinsic::" << P.second << ", "
         << Table.GetOrAddStringOffset(P.first) << "}, // " << P.first << "\n";
    }
    OS << "    };\n";
    OS << "    auto I = std::lower_bound(std::begin(" << I.first << "Names),\n";
    OS << "                              std::end(" << I.first << "Names),\n";
    OS << "                              BuiltinNameStr);\n";
    OS << "    if (I != std::end(" << I.first << "Names) &&\n";
    OS << "        I->getName() == BuiltinNameStr)\n";
    OS << "      return I->IntrinID;\n";
    OS << "  }\n";
  }
  OS << "  return ";
  OS << "Intrinsic::not_intrinsic;\n";
  OS << "}\n";
  OS << "#endif\n\n";
}

static void EmitIntrinsicEnums(RecordKeeper &RK, raw_ostream &OS) {
  IntrinsicEmitter(RK).run(OS, /*Enums=*/true);
}

static TableGen::Emitter::Opt X("gen-intrinsic-enums", EmitIntrinsicEnums,
                                "Generate intrinsic enums");

static void EmitIntrinsicImpl(RecordKeeper &RK, raw_ostream &OS) {
  IntrinsicEmitter(RK).run(OS, /*Enums=*/false);
}

static TableGen::Emitter::Opt Y("gen-intrinsic-impl", EmitIntrinsicImpl,
                                "Generate intrinsic information");
