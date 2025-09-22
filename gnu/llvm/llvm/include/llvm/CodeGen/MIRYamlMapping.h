//===- MIRYamlMapping.h - Describe mapping between MIR and YAML--*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the mapping between various MIR data structures and
// their corresponding YAML representation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MIRYAMLMAPPING_H
#define LLVM_CODEGEN_MIRYAMLMAPPING_H

#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace yaml {

/// A wrapper around std::string which contains a source range that's being
/// set during parsing.
struct StringValue {
  std::string Value;
  SMRange SourceRange;

  StringValue() = default;
  StringValue(std::string Value) : Value(std::move(Value)) {}
  StringValue(const char Val[]) : Value(Val) {}

  bool operator==(const StringValue &Other) const {
    return Value == Other.Value;
  }
};

template <> struct ScalarTraits<StringValue> {
  static void output(const StringValue &S, void *, raw_ostream &OS) {
    OS << S.Value;
  }

  static StringRef input(StringRef Scalar, void *Ctx, StringValue &S) {
    S.Value = Scalar.str();
    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      S.SourceRange = Node->getSourceRange();
    return "";
  }

  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

struct FlowStringValue : StringValue {
  FlowStringValue() = default;
  FlowStringValue(std::string Value) : StringValue(std::move(Value)) {}
};

template <> struct ScalarTraits<FlowStringValue> {
  static void output(const FlowStringValue &S, void *, raw_ostream &OS) {
    return ScalarTraits<StringValue>::output(S, nullptr, OS);
  }

  static StringRef input(StringRef Scalar, void *Ctx, FlowStringValue &S) {
    return ScalarTraits<StringValue>::input(Scalar, Ctx, S);
  }

  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

struct BlockStringValue {
  StringValue Value;

  bool operator==(const BlockStringValue &Other) const {
    return Value == Other.Value;
  }
};

template <> struct BlockScalarTraits<BlockStringValue> {
  static void output(const BlockStringValue &S, void *Ctx, raw_ostream &OS) {
    return ScalarTraits<StringValue>::output(S.Value, Ctx, OS);
  }

  static StringRef input(StringRef Scalar, void *Ctx, BlockStringValue &S) {
    return ScalarTraits<StringValue>::input(Scalar, Ctx, S.Value);
  }
};

/// A wrapper around unsigned which contains a source range that's being set
/// during parsing.
struct UnsignedValue {
  unsigned Value = 0;
  SMRange SourceRange;

  UnsignedValue() = default;
  UnsignedValue(unsigned Value) : Value(Value) {}

  bool operator==(const UnsignedValue &Other) const {
    return Value == Other.Value;
  }
};

template <> struct ScalarTraits<UnsignedValue> {
  static void output(const UnsignedValue &Value, void *Ctx, raw_ostream &OS) {
    return ScalarTraits<unsigned>::output(Value.Value, Ctx, OS);
  }

  static StringRef input(StringRef Scalar, void *Ctx, UnsignedValue &Value) {
    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      Value.SourceRange = Node->getSourceRange();
    return ScalarTraits<unsigned>::input(Scalar, Ctx, Value.Value);
  }

  static QuotingType mustQuote(StringRef Scalar) {
    return ScalarTraits<unsigned>::mustQuote(Scalar);
  }
};

template <> struct ScalarEnumerationTraits<MachineJumpTableInfo::JTEntryKind> {
  static void enumeration(yaml::IO &IO,
                          MachineJumpTableInfo::JTEntryKind &EntryKind) {
    IO.enumCase(EntryKind, "block-address",
                MachineJumpTableInfo::EK_BlockAddress);
    IO.enumCase(EntryKind, "gp-rel64-block-address",
                MachineJumpTableInfo::EK_GPRel64BlockAddress);
    IO.enumCase(EntryKind, "gp-rel32-block-address",
                MachineJumpTableInfo::EK_GPRel32BlockAddress);
    IO.enumCase(EntryKind, "label-difference32",
                MachineJumpTableInfo::EK_LabelDifference32);
    IO.enumCase(EntryKind, "label-difference64",
                MachineJumpTableInfo::EK_LabelDifference64);
    IO.enumCase(EntryKind, "inline", MachineJumpTableInfo::EK_Inline);
    IO.enumCase(EntryKind, "custom32", MachineJumpTableInfo::EK_Custom32);
  }
};

template <> struct ScalarTraits<MaybeAlign> {
  static void output(const MaybeAlign &Alignment, void *,
                     llvm::raw_ostream &out) {
    out << uint64_t(Alignment ? Alignment->value() : 0U);
  }
  static StringRef input(StringRef Scalar, void *, MaybeAlign &Alignment) {
    unsigned long long n;
    if (getAsUnsignedInteger(Scalar, 10, n))
      return "invalid number";
    if (n > 0 && !isPowerOf2_64(n))
      return "must be 0 or a power of two";
    Alignment = MaybeAlign(n);
    return StringRef();
  }
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

template <> struct ScalarTraits<Align> {
  static void output(const Align &Alignment, void *, llvm::raw_ostream &OS) {
    OS << Alignment.value();
  }
  static StringRef input(StringRef Scalar, void *, Align &Alignment) {
    unsigned long long N;
    if (getAsUnsignedInteger(Scalar, 10, N))
      return "invalid number";
    if (!isPowerOf2_64(N))
      return "must be a power of two";
    Alignment = Align(N);
    return StringRef();
  }
  static QuotingType mustQuote(StringRef) { return QuotingType::None; }
};

} // end namespace yaml
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::StringValue)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(llvm::yaml::FlowStringValue)
LLVM_YAML_IS_FLOW_SEQUENCE_VECTOR(llvm::yaml::UnsignedValue)

namespace llvm {
namespace yaml {

struct VirtualRegisterDefinition {
  UnsignedValue ID;
  StringValue Class;
  StringValue PreferredRegister;

  // TODO: Serialize the target specific register hints.

  bool operator==(const VirtualRegisterDefinition &Other) const {
    return ID == Other.ID && Class == Other.Class &&
           PreferredRegister == Other.PreferredRegister;
  }
};

template <> struct MappingTraits<VirtualRegisterDefinition> {
  static void mapping(IO &YamlIO, VirtualRegisterDefinition &Reg) {
    YamlIO.mapRequired("id", Reg.ID);
    YamlIO.mapRequired("class", Reg.Class);
    YamlIO.mapOptional("preferred-register", Reg.PreferredRegister,
                       StringValue()); // Don't print out when it's empty.
  }

  static const bool flow = true;
};

struct MachineFunctionLiveIn {
  StringValue Register;
  StringValue VirtualRegister;

  bool operator==(const MachineFunctionLiveIn &Other) const {
    return Register == Other.Register &&
           VirtualRegister == Other.VirtualRegister;
  }
};

template <> struct MappingTraits<MachineFunctionLiveIn> {
  static void mapping(IO &YamlIO, MachineFunctionLiveIn &LiveIn) {
    YamlIO.mapRequired("reg", LiveIn.Register);
    YamlIO.mapOptional(
        "virtual-reg", LiveIn.VirtualRegister,
        StringValue()); // Don't print the virtual register when it's empty.
  }

  static const bool flow = true;
};

/// Serializable representation of stack object from the MachineFrameInfo class.
///
/// The flags 'isImmutable' and 'isAliased' aren't serialized, as they are
/// determined by the object's type and frame information flags.
/// Dead stack objects aren't serialized.
///
/// The 'isPreallocated' flag is determined by the local offset.
struct MachineStackObject {
  enum ObjectType { DefaultType, SpillSlot, VariableSized };
  UnsignedValue ID;
  StringValue Name;
  // TODO: Serialize unnamed LLVM alloca reference.
  ObjectType Type = DefaultType;
  int64_t Offset = 0;
  uint64_t Size = 0;
  MaybeAlign Alignment = std::nullopt;
  TargetStackID::Value StackID;
  StringValue CalleeSavedRegister;
  bool CalleeSavedRestored = true;
  std::optional<int64_t> LocalOffset;
  StringValue DebugVar;
  StringValue DebugExpr;
  StringValue DebugLoc;

  bool operator==(const MachineStackObject &Other) const {
    return ID == Other.ID && Name == Other.Name && Type == Other.Type &&
           Offset == Other.Offset && Size == Other.Size &&
           Alignment == Other.Alignment &&
           StackID == Other.StackID &&
           CalleeSavedRegister == Other.CalleeSavedRegister &&
           CalleeSavedRestored == Other.CalleeSavedRestored &&
           LocalOffset == Other.LocalOffset && DebugVar == Other.DebugVar &&
           DebugExpr == Other.DebugExpr && DebugLoc == Other.DebugLoc;
  }
};

template <> struct ScalarEnumerationTraits<MachineStackObject::ObjectType> {
  static void enumeration(yaml::IO &IO, MachineStackObject::ObjectType &Type) {
    IO.enumCase(Type, "default", MachineStackObject::DefaultType);
    IO.enumCase(Type, "spill-slot", MachineStackObject::SpillSlot);
    IO.enumCase(Type, "variable-sized", MachineStackObject::VariableSized);
  }
};

template <> struct MappingTraits<MachineStackObject> {
  static void mapping(yaml::IO &YamlIO, MachineStackObject &Object) {
    YamlIO.mapRequired("id", Object.ID);
    YamlIO.mapOptional("name", Object.Name,
                       StringValue()); // Don't print out an empty name.
    YamlIO.mapOptional(
        "type", Object.Type,
        MachineStackObject::DefaultType); // Don't print the default type.
    YamlIO.mapOptional("offset", Object.Offset, (int64_t)0);
    if (Object.Type != MachineStackObject::VariableSized)
      YamlIO.mapRequired("size", Object.Size);
    YamlIO.mapOptional("alignment", Object.Alignment, std::nullopt);
    YamlIO.mapOptional("stack-id", Object.StackID, TargetStackID::Default);
    YamlIO.mapOptional("callee-saved-register", Object.CalleeSavedRegister,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("callee-saved-restored", Object.CalleeSavedRestored,
                       true);
    YamlIO.mapOptional("local-offset", Object.LocalOffset,
                       std::optional<int64_t>());
    YamlIO.mapOptional("debug-info-variable", Object.DebugVar,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-expression", Object.DebugExpr,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-location", Object.DebugLoc,
                       StringValue()); // Don't print it out when it's empty.
  }

  static const bool flow = true;
};

/// Serializable representation of the MCRegister variant of
/// MachineFunction::VariableDbgInfo.
struct EntryValueObject {
  StringValue EntryValueRegister;
  StringValue DebugVar;
  StringValue DebugExpr;
  StringValue DebugLoc;
  bool operator==(const EntryValueObject &Other) const {
    return EntryValueRegister == Other.EntryValueRegister &&
           DebugVar == Other.DebugVar && DebugExpr == Other.DebugExpr &&
           DebugLoc == Other.DebugLoc;
  }
};

template <> struct MappingTraits<EntryValueObject> {
  static void mapping(yaml::IO &YamlIO, EntryValueObject &Object) {
    YamlIO.mapRequired("entry-value-register", Object.EntryValueRegister);
    YamlIO.mapRequired("debug-info-variable", Object.DebugVar);
    YamlIO.mapRequired("debug-info-expression", Object.DebugExpr);
    YamlIO.mapRequired("debug-info-location", Object.DebugLoc);
  }
  static const bool flow = true;
};

/// Serializable representation of the fixed stack object from the
/// MachineFrameInfo class.
struct FixedMachineStackObject {
  enum ObjectType { DefaultType, SpillSlot };
  UnsignedValue ID;
  ObjectType Type = DefaultType;
  int64_t Offset = 0;
  uint64_t Size = 0;
  MaybeAlign Alignment = std::nullopt;
  TargetStackID::Value StackID;
  bool IsImmutable = false;
  bool IsAliased = false;
  StringValue CalleeSavedRegister;
  bool CalleeSavedRestored = true;
  StringValue DebugVar;
  StringValue DebugExpr;
  StringValue DebugLoc;

  bool operator==(const FixedMachineStackObject &Other) const {
    return ID == Other.ID && Type == Other.Type && Offset == Other.Offset &&
           Size == Other.Size && Alignment == Other.Alignment &&
           StackID == Other.StackID &&
           IsImmutable == Other.IsImmutable && IsAliased == Other.IsAliased &&
           CalleeSavedRegister == Other.CalleeSavedRegister &&
           CalleeSavedRestored == Other.CalleeSavedRestored &&
           DebugVar == Other.DebugVar && DebugExpr == Other.DebugExpr
           && DebugLoc == Other.DebugLoc;
  }
};

template <>
struct ScalarEnumerationTraits<FixedMachineStackObject::ObjectType> {
  static void enumeration(yaml::IO &IO,
                          FixedMachineStackObject::ObjectType &Type) {
    IO.enumCase(Type, "default", FixedMachineStackObject::DefaultType);
    IO.enumCase(Type, "spill-slot", FixedMachineStackObject::SpillSlot);
  }
};

template <>
struct ScalarEnumerationTraits<TargetStackID::Value> {
  static void enumeration(yaml::IO &IO, TargetStackID::Value &ID) {
    IO.enumCase(ID, "default", TargetStackID::Default);
    IO.enumCase(ID, "sgpr-spill", TargetStackID::SGPRSpill);
    IO.enumCase(ID, "scalable-vector", TargetStackID::ScalableVector);
    IO.enumCase(ID, "wasm-local", TargetStackID::WasmLocal);
    IO.enumCase(ID, "noalloc", TargetStackID::NoAlloc);
  }
};

template <> struct MappingTraits<FixedMachineStackObject> {
  static void mapping(yaml::IO &YamlIO, FixedMachineStackObject &Object) {
    YamlIO.mapRequired("id", Object.ID);
    YamlIO.mapOptional(
        "type", Object.Type,
        FixedMachineStackObject::DefaultType); // Don't print the default type.
    YamlIO.mapOptional("offset", Object.Offset, (int64_t)0);
    YamlIO.mapOptional("size", Object.Size, (uint64_t)0);
    YamlIO.mapOptional("alignment", Object.Alignment, std::nullopt);
    YamlIO.mapOptional("stack-id", Object.StackID, TargetStackID::Default);
    if (Object.Type != FixedMachineStackObject::SpillSlot) {
      YamlIO.mapOptional("isImmutable", Object.IsImmutable, false);
      YamlIO.mapOptional("isAliased", Object.IsAliased, false);
    }
    YamlIO.mapOptional("callee-saved-register", Object.CalleeSavedRegister,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("callee-saved-restored", Object.CalleeSavedRestored,
                     true);
    YamlIO.mapOptional("debug-info-variable", Object.DebugVar,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-expression", Object.DebugExpr,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("debug-info-location", Object.DebugLoc,
                       StringValue()); // Don't print it out when it's empty.
  }

  static const bool flow = true;
};

/// A serializaable representation of a reference to a stack object or fixed
/// stack object.
struct FrameIndex {
  // The frame index as printed. This is always a positive number, even for
  // fixed objects. To obtain the real index,
  // MachineFrameInfo::getObjectIndexBegin has to be added.
  int FI;
  bool IsFixed;
  SMRange SourceRange;

  FrameIndex() = default;
  FrameIndex(int FI, const llvm::MachineFrameInfo &MFI);

  Expected<int> getFI(const llvm::MachineFrameInfo &MFI) const;
};

template <> struct ScalarTraits<FrameIndex> {
  static void output(const FrameIndex &FI, void *, raw_ostream &OS) {
    MachineOperand::printStackObjectReference(OS, FI.FI, FI.IsFixed, "");
  }

  static StringRef input(StringRef Scalar, void *Ctx, FrameIndex &FI) {
    FI.IsFixed = false;
    StringRef Num;
    if (Scalar.starts_with("%stack.")) {
      Num = Scalar.substr(7);
    } else if (Scalar.starts_with("%fixed-stack.")) {
      Num = Scalar.substr(13);
      FI.IsFixed = true;
    } else {
      return "Invalid frame index, needs to start with %stack. or "
             "%fixed-stack.";
    }
    if (Num.consumeInteger(10, FI.FI))
      return "Invalid frame index, not a valid number";

    if (const auto *Node =
            reinterpret_cast<yaml::Input *>(Ctx)->getCurrentNode())
      FI.SourceRange = Node->getSourceRange();
    return StringRef();
  }

  static QuotingType mustQuote(StringRef S) { return needsQuotes(S); }
};

/// Serializable representation of CallSiteInfo.
struct CallSiteInfo {
  // Representation of call argument and register which is used to
  // transfer it.
  struct ArgRegPair {
    StringValue Reg;
    uint16_t ArgNo;

    bool operator==(const ArgRegPair &Other) const {
      return Reg == Other.Reg && ArgNo == Other.ArgNo;
    }
  };

  /// Identifies call instruction location in machine function.
  struct MachineInstrLoc {
    unsigned BlockNum;
    unsigned Offset;

    bool operator==(const MachineInstrLoc &Other) const {
      return BlockNum == Other.BlockNum && Offset == Other.Offset;
    }
  };

  MachineInstrLoc CallLocation;
  std::vector<ArgRegPair> ArgForwardingRegs;

  bool operator==(const CallSiteInfo &Other) const {
    return CallLocation.BlockNum == Other.CallLocation.BlockNum &&
           CallLocation.Offset == Other.CallLocation.Offset;
  }
};

template <> struct MappingTraits<CallSiteInfo::ArgRegPair> {
  static void mapping(IO &YamlIO, CallSiteInfo::ArgRegPair &ArgReg) {
    YamlIO.mapRequired("arg", ArgReg.ArgNo);
    YamlIO.mapRequired("reg", ArgReg.Reg);
  }

  static const bool flow = true;
};
}
}

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::CallSiteInfo::ArgRegPair)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<CallSiteInfo> {
  static void mapping(IO &YamlIO, CallSiteInfo &CSInfo) {
    YamlIO.mapRequired("bb", CSInfo.CallLocation.BlockNum);
    YamlIO.mapRequired("offset", CSInfo.CallLocation.Offset);
    YamlIO.mapOptional("fwdArgRegs", CSInfo.ArgForwardingRegs,
                       std::vector<CallSiteInfo::ArgRegPair>());
  }

  static const bool flow = true;
};

/// Serializable representation of debug value substitutions.
struct DebugValueSubstitution {
  unsigned SrcInst;
  unsigned SrcOp;
  unsigned DstInst;
  unsigned DstOp;
  unsigned Subreg;

  bool operator==(const DebugValueSubstitution &Other) const {
    return std::tie(SrcInst, SrcOp, DstInst, DstOp) ==
           std::tie(Other.SrcInst, Other.SrcOp, Other.DstInst, Other.DstOp);
  }
};

template <> struct MappingTraits<DebugValueSubstitution> {
  static void mapping(IO &YamlIO, DebugValueSubstitution &Sub) {
    YamlIO.mapRequired("srcinst", Sub.SrcInst);
    YamlIO.mapRequired("srcop", Sub.SrcOp);
    YamlIO.mapRequired("dstinst", Sub.DstInst);
    YamlIO.mapRequired("dstop", Sub.DstOp);
    YamlIO.mapRequired("subreg", Sub.Subreg);
  }

  static const bool flow = true;
};
} // namespace yaml
} // namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::DebugValueSubstitution)

namespace llvm {
namespace yaml {
struct MachineConstantPoolValue {
  UnsignedValue ID;
  StringValue Value;
  MaybeAlign Alignment = std::nullopt;
  bool IsTargetSpecific = false;

  bool operator==(const MachineConstantPoolValue &Other) const {
    return ID == Other.ID && Value == Other.Value &&
           Alignment == Other.Alignment &&
           IsTargetSpecific == Other.IsTargetSpecific;
  }
};

template <> struct MappingTraits<MachineConstantPoolValue> {
  static void mapping(IO &YamlIO, MachineConstantPoolValue &Constant) {
    YamlIO.mapRequired("id", Constant.ID);
    YamlIO.mapOptional("value", Constant.Value, StringValue());
    YamlIO.mapOptional("alignment", Constant.Alignment, std::nullopt);
    YamlIO.mapOptional("isTargetSpecific", Constant.IsTargetSpecific, false);
  }
};

struct MachineJumpTable {
  struct Entry {
    UnsignedValue ID;
    std::vector<FlowStringValue> Blocks;

    bool operator==(const Entry &Other) const {
      return ID == Other.ID && Blocks == Other.Blocks;
    }
  };

  MachineJumpTableInfo::JTEntryKind Kind = MachineJumpTableInfo::EK_Custom32;
  std::vector<Entry> Entries;

  bool operator==(const MachineJumpTable &Other) const {
    return Kind == Other.Kind && Entries == Other.Entries;
  }
};

template <> struct MappingTraits<MachineJumpTable::Entry> {
  static void mapping(IO &YamlIO, MachineJumpTable::Entry &Entry) {
    YamlIO.mapRequired("id", Entry.ID);
    YamlIO.mapOptional("blocks", Entry.Blocks, std::vector<FlowStringValue>());
  }
};

} // end namespace yaml
} // end namespace llvm

LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::MachineFunctionLiveIn)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::VirtualRegisterDefinition)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::MachineStackObject)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::EntryValueObject)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::FixedMachineStackObject)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::CallSiteInfo)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::MachineConstantPoolValue)
LLVM_YAML_IS_SEQUENCE_VECTOR(llvm::yaml::MachineJumpTable::Entry)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<MachineJumpTable> {
  static void mapping(IO &YamlIO, MachineJumpTable &JT) {
    YamlIO.mapRequired("kind", JT.Kind);
    YamlIO.mapOptional("entries", JT.Entries,
                       std::vector<MachineJumpTable::Entry>());
  }
};

/// Serializable representation of MachineFrameInfo.
///
/// Doesn't serialize attributes like 'StackAlignment', 'IsStackRealignable' and
/// 'RealignOption' as they are determined by the target and LLVM function
/// attributes.
/// It also doesn't serialize attributes like 'NumFixedObject' and
/// 'HasVarSizedObjects' as they are determined by the frame objects themselves.
struct MachineFrameInfo {
  bool IsFrameAddressTaken = false;
  bool IsReturnAddressTaken = false;
  bool HasStackMap = false;
  bool HasPatchPoint = false;
  uint64_t StackSize = 0;
  int OffsetAdjustment = 0;
  unsigned MaxAlignment = 0;
  bool AdjustsStack = false;
  bool HasCalls = false;
  StringValue StackProtector;
  StringValue FunctionContext;
  unsigned MaxCallFrameSize = ~0u; ///< ~0u means: not computed yet.
  unsigned CVBytesOfCalleeSavedRegisters = 0;
  bool HasOpaqueSPAdjustment = false;
  bool HasVAStart = false;
  bool HasMustTailInVarArgFunc = false;
  bool HasTailCall = false;
  bool IsCalleeSavedInfoValid = false;
  unsigned LocalFrameSize = 0;
  StringValue SavePoint;
  StringValue RestorePoint;

  bool operator==(const MachineFrameInfo &Other) const {
    return IsFrameAddressTaken == Other.IsFrameAddressTaken &&
           IsReturnAddressTaken == Other.IsReturnAddressTaken &&
           HasStackMap == Other.HasStackMap &&
           HasPatchPoint == Other.HasPatchPoint &&
           StackSize == Other.StackSize &&
           OffsetAdjustment == Other.OffsetAdjustment &&
           MaxAlignment == Other.MaxAlignment &&
           AdjustsStack == Other.AdjustsStack && HasCalls == Other.HasCalls &&
           StackProtector == Other.StackProtector &&
           FunctionContext == Other.FunctionContext &&
           MaxCallFrameSize == Other.MaxCallFrameSize &&
           CVBytesOfCalleeSavedRegisters ==
               Other.CVBytesOfCalleeSavedRegisters &&
           HasOpaqueSPAdjustment == Other.HasOpaqueSPAdjustment &&
           HasVAStart == Other.HasVAStart &&
           HasMustTailInVarArgFunc == Other.HasMustTailInVarArgFunc &&
           HasTailCall == Other.HasTailCall &&
           LocalFrameSize == Other.LocalFrameSize &&
           SavePoint == Other.SavePoint && RestorePoint == Other.RestorePoint &&
           IsCalleeSavedInfoValid == Other.IsCalleeSavedInfoValid;
  }
};

template <> struct MappingTraits<MachineFrameInfo> {
  static void mapping(IO &YamlIO, MachineFrameInfo &MFI) {
    YamlIO.mapOptional("isFrameAddressTaken", MFI.IsFrameAddressTaken, false);
    YamlIO.mapOptional("isReturnAddressTaken", MFI.IsReturnAddressTaken, false);
    YamlIO.mapOptional("hasStackMap", MFI.HasStackMap, false);
    YamlIO.mapOptional("hasPatchPoint", MFI.HasPatchPoint, false);
    YamlIO.mapOptional("stackSize", MFI.StackSize, (uint64_t)0);
    YamlIO.mapOptional("offsetAdjustment", MFI.OffsetAdjustment, (int)0);
    YamlIO.mapOptional("maxAlignment", MFI.MaxAlignment, (unsigned)0);
    YamlIO.mapOptional("adjustsStack", MFI.AdjustsStack, false);
    YamlIO.mapOptional("hasCalls", MFI.HasCalls, false);
    YamlIO.mapOptional("stackProtector", MFI.StackProtector,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("functionContext", MFI.FunctionContext,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("maxCallFrameSize", MFI.MaxCallFrameSize, (unsigned)~0);
    YamlIO.mapOptional("cvBytesOfCalleeSavedRegisters",
                       MFI.CVBytesOfCalleeSavedRegisters, 0U);
    YamlIO.mapOptional("hasOpaqueSPAdjustment", MFI.HasOpaqueSPAdjustment,
                       false);
    YamlIO.mapOptional("hasVAStart", MFI.HasVAStart, false);
    YamlIO.mapOptional("hasMustTailInVarArgFunc", MFI.HasMustTailInVarArgFunc,
                       false);
    YamlIO.mapOptional("hasTailCall", MFI.HasTailCall, false);
    YamlIO.mapOptional("isCalleeSavedInfoValid", MFI.IsCalleeSavedInfoValid,
                       false);
    YamlIO.mapOptional("localFrameSize", MFI.LocalFrameSize, (unsigned)0);
    YamlIO.mapOptional("savePoint", MFI.SavePoint,
                       StringValue()); // Don't print it out when it's empty.
    YamlIO.mapOptional("restorePoint", MFI.RestorePoint,
                       StringValue()); // Don't print it out when it's empty.
  }
};

/// Targets should override this in a way that mirrors the implementation of
/// llvm::MachineFunctionInfo.
struct MachineFunctionInfo {
  virtual ~MachineFunctionInfo() = default;
  virtual void mappingImpl(IO &YamlIO) {}
};

template <> struct MappingTraits<std::unique_ptr<MachineFunctionInfo>> {
  static void mapping(IO &YamlIO, std::unique_ptr<MachineFunctionInfo> &MFI) {
    if (MFI)
      MFI->mappingImpl(YamlIO);
  }
};

struct MachineFunction {
  StringRef Name;
  MaybeAlign Alignment = std::nullopt;
  bool ExposesReturnsTwice = false;
  // GISel MachineFunctionProperties.
  bool Legalized = false;
  bool RegBankSelected = false;
  bool Selected = false;
  bool FailedISel = false;
  // Register information
  bool TracksRegLiveness = false;
  bool HasWinCFI = false;

  bool CallsEHReturn = false;
  bool CallsUnwindInit = false;
  bool HasEHCatchret = false;
  bool HasEHScopes = false;
  bool HasEHFunclets = false;
  bool IsOutlined = false;

  bool FailsVerification = false;
  bool TracksDebugUserValues = false;
  bool UseDebugInstrRef = false;
  std::vector<VirtualRegisterDefinition> VirtualRegisters;
  std::vector<MachineFunctionLiveIn> LiveIns;
  std::optional<std::vector<FlowStringValue>> CalleeSavedRegisters;
  // TODO: Serialize the various register masks.
  // Frame information
  MachineFrameInfo FrameInfo;
  std::vector<FixedMachineStackObject> FixedStackObjects;
  std::vector<EntryValueObject> EntryValueObjects;
  std::vector<MachineStackObject> StackObjects;
  std::vector<MachineConstantPoolValue> Constants; /// Constant pool.
  std::unique_ptr<MachineFunctionInfo> MachineFuncInfo;
  std::vector<CallSiteInfo> CallSitesInfo;
  std::vector<DebugValueSubstitution> DebugValueSubstitutions;
  MachineJumpTable JumpTableInfo;
  std::vector<StringValue> MachineMetadataNodes;
  BlockStringValue Body;
};

template <> struct MappingTraits<MachineFunction> {
  static void mapping(IO &YamlIO, MachineFunction &MF) {
    YamlIO.mapRequired("name", MF.Name);
    YamlIO.mapOptional("alignment", MF.Alignment, std::nullopt);
    YamlIO.mapOptional("exposesReturnsTwice", MF.ExposesReturnsTwice, false);
    YamlIO.mapOptional("legalized", MF.Legalized, false);
    YamlIO.mapOptional("regBankSelected", MF.RegBankSelected, false);
    YamlIO.mapOptional("selected", MF.Selected, false);
    YamlIO.mapOptional("failedISel", MF.FailedISel, false);
    YamlIO.mapOptional("tracksRegLiveness", MF.TracksRegLiveness, false);
    YamlIO.mapOptional("hasWinCFI", MF.HasWinCFI, false);

    YamlIO.mapOptional("callsEHReturn", MF.CallsEHReturn, false);
    YamlIO.mapOptional("callsUnwindInit", MF.CallsUnwindInit, false);
    YamlIO.mapOptional("hasEHCatchret", MF.HasEHCatchret, false);
    YamlIO.mapOptional("hasEHScopes", MF.HasEHScopes, false);
    YamlIO.mapOptional("hasEHFunclets", MF.HasEHFunclets, false);
    YamlIO.mapOptional("isOutlined", MF.IsOutlined, false);
    YamlIO.mapOptional("debugInstrRef", MF.UseDebugInstrRef, false);

    YamlIO.mapOptional("failsVerification", MF.FailsVerification, false);
    YamlIO.mapOptional("tracksDebugUserValues", MF.TracksDebugUserValues,
                       false);
    YamlIO.mapOptional("registers", MF.VirtualRegisters,
                       std::vector<VirtualRegisterDefinition>());
    YamlIO.mapOptional("liveins", MF.LiveIns,
                       std::vector<MachineFunctionLiveIn>());
    YamlIO.mapOptional("calleeSavedRegisters", MF.CalleeSavedRegisters,
                       std::optional<std::vector<FlowStringValue>>());
    YamlIO.mapOptional("frameInfo", MF.FrameInfo, MachineFrameInfo());
    YamlIO.mapOptional("fixedStack", MF.FixedStackObjects,
                       std::vector<FixedMachineStackObject>());
    YamlIO.mapOptional("stack", MF.StackObjects,
                       std::vector<MachineStackObject>());
    YamlIO.mapOptional("entry_values", MF.EntryValueObjects,
                       std::vector<EntryValueObject>());
    YamlIO.mapOptional("callSites", MF.CallSitesInfo,
                       std::vector<CallSiteInfo>());
    YamlIO.mapOptional("debugValueSubstitutions", MF.DebugValueSubstitutions,
                       std::vector<DebugValueSubstitution>());
    YamlIO.mapOptional("constants", MF.Constants,
                       std::vector<MachineConstantPoolValue>());
    YamlIO.mapOptional("machineFunctionInfo", MF.MachineFuncInfo);
    if (!YamlIO.outputting() || !MF.JumpTableInfo.Entries.empty())
      YamlIO.mapOptional("jumpTable", MF.JumpTableInfo, MachineJumpTable());
    if (!YamlIO.outputting() || !MF.MachineMetadataNodes.empty())
      YamlIO.mapOptional("machineMetadataNodes", MF.MachineMetadataNodes,
                         std::vector<StringValue>());
    YamlIO.mapOptional("body", MF.Body, BlockStringValue());
  }
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_CODEGEN_MIRYAMLMAPPING_H
