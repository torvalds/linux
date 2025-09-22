//===- BTFDebug.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains support for writing BTF debug info.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BTFDEBUG_H
#define LLVM_LIB_TARGET_BPF_BTFDEBUG_H

#include "llvm/ADT/StringMap.h"
#include "llvm/CodeGen/DebugHandlerBase.h"
#include "llvm/DebugInfo/BTF/BTF.h"
#include <cstdint>
#include <map>
#include <set>
#include <unordered_map>

namespace llvm {

class AsmPrinter;
class BTFDebug;
class DIType;
class GlobalVariable;
class MachineFunction;
class MachineInstr;
class MachineOperand;
class MCInst;
class MCStreamer;
class MCSymbol;

/// The base class for BTF type generation.
class BTFTypeBase {
protected:
  uint8_t Kind;
  bool IsCompleted;
  uint32_t Id;
  struct BTF::CommonType BTFType;

public:
  BTFTypeBase() : IsCompleted(false) {}
  virtual ~BTFTypeBase() = default;
  void setId(uint32_t Id) { this->Id = Id; }
  uint32_t getId() { return Id; }
  uint32_t roundupToBytes(uint32_t NumBits) { return (NumBits + 7) >> 3; }
  /// Get the size of this BTF type entry.
  virtual uint32_t getSize() { return BTF::CommonTypeSize; }
  /// Complete BTF type generation after all related DebugInfo types
  /// have been visited so their BTF type id's are available
  /// for cross referece.
  virtual void completeType(BTFDebug &BDebug) {}
  /// Emit types for this BTF type entry.
  virtual void emitType(MCStreamer &OS);
};

/// Handle several derived types include pointer, const,
/// volatile, typedef and restrict.
class BTFTypeDerived : public BTFTypeBase {
  const DIDerivedType *DTy;
  bool NeedsFixup;
  StringRef Name;

public:
  BTFTypeDerived(const DIDerivedType *Ty, unsigned Tag, bool NeedsFixup);
  BTFTypeDerived(unsigned NextTypeId, unsigned Tag, StringRef Name);
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
  void setPointeeType(uint32_t PointeeType);
};

/// Handle struct or union forward declaration.
class BTFTypeFwd : public BTFTypeBase {
  StringRef Name;

public:
  BTFTypeFwd(StringRef Name, bool IsUnion);
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle int type.
class BTFTypeInt : public BTFTypeBase {
  StringRef Name;
  uint32_t IntVal; ///< Encoding, offset, bits

public:
  BTFTypeInt(uint32_t Encoding, uint32_t SizeInBits, uint32_t OffsetInBits,
             StringRef TypeName);
  uint32_t getSize() override { return BTFTypeBase::getSize() + sizeof(uint32_t); }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle enumerate type.
class BTFTypeEnum : public BTFTypeBase {
  const DICompositeType *ETy;
  std::vector<struct BTF::BTFEnum> EnumValues;

public:
  BTFTypeEnum(const DICompositeType *ETy, uint32_t NumValues, bool IsSigned);
  uint32_t getSize() override {
    return BTFTypeBase::getSize() + EnumValues.size() * BTF::BTFEnumSize;
  }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle array type.
class BTFTypeArray : public BTFTypeBase {
  struct BTF::BTFArray ArrayInfo;

public:
  BTFTypeArray(uint32_t ElemTypeId, uint32_t NumElems);
  uint32_t getSize() override { return BTFTypeBase::getSize() + BTF::BTFArraySize; }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle struct/union type.
class BTFTypeStruct : public BTFTypeBase {
  const DICompositeType *STy;
  bool HasBitField;
  std::vector<struct BTF::BTFMember> Members;

public:
  BTFTypeStruct(const DICompositeType *STy, bool IsStruct, bool HasBitField,
                uint32_t NumMembers);
  uint32_t getSize() override {
    return BTFTypeBase::getSize() + Members.size() * BTF::BTFMemberSize;
  }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
  std::string getName();
};

/// Handle function pointer.
class BTFTypeFuncProto : public BTFTypeBase {
  const DISubroutineType *STy;
  std::unordered_map<uint32_t, StringRef> FuncArgNames;
  std::vector<struct BTF::BTFParam> Parameters;

public:
  BTFTypeFuncProto(const DISubroutineType *STy, uint32_t NumParams,
                   const std::unordered_map<uint32_t, StringRef> &FuncArgNames);
  uint32_t getSize() override {
    return BTFTypeBase::getSize() + Parameters.size() * BTF::BTFParamSize;
  }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle subprogram
class BTFTypeFunc : public BTFTypeBase {
  StringRef Name;

public:
  BTFTypeFunc(StringRef FuncName, uint32_t ProtoTypeId, uint32_t Scope);
  uint32_t getSize() override { return BTFTypeBase::getSize(); }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle variable instances
class BTFKindVar : public BTFTypeBase {
  StringRef Name;
  uint32_t Info;

public:
  BTFKindVar(StringRef VarName, uint32_t TypeId, uint32_t VarInfo);
  uint32_t getSize() override { return BTFTypeBase::getSize() + 4; }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle data sections
class BTFKindDataSec : public BTFTypeBase {
  AsmPrinter *Asm;
  std::string Name;
  std::vector<std::tuple<uint32_t, const MCSymbol *, uint32_t>> Vars;

public:
  BTFKindDataSec(AsmPrinter *AsmPrt, std::string SecName);
  uint32_t getSize() override {
    return BTFTypeBase::getSize() + BTF::BTFDataSecVarSize * Vars.size();
  }
  void addDataSecEntry(uint32_t Id, const MCSymbol *Sym, uint32_t Size) {
    Vars.push_back(std::make_tuple(Id, Sym, Size));
  }
  std::string getName() { return Name; }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle binary floating point type.
class BTFTypeFloat : public BTFTypeBase {
  StringRef Name;

public:
  BTFTypeFloat(uint32_t SizeInBits, StringRef TypeName);
  void completeType(BTFDebug &BDebug) override;
};

/// Handle decl tags.
class BTFTypeDeclTag : public BTFTypeBase {
  uint32_t Info;
  StringRef Tag;

public:
  BTFTypeDeclTag(uint32_t BaseTypeId, int ComponentId, StringRef Tag);
  uint32_t getSize() override { return BTFTypeBase::getSize() + 4; }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

/// Handle 64-bit enumerate type.
class BTFTypeEnum64 : public BTFTypeBase {
  const DICompositeType *ETy;
  std::vector<struct BTF::BTFEnum64> EnumValues;

public:
  BTFTypeEnum64(const DICompositeType *ETy, uint32_t NumValues, bool IsSigned);
  uint32_t getSize() override {
    return BTFTypeBase::getSize() + EnumValues.size() * BTF::BTFEnum64Size;
  }
  void completeType(BTFDebug &BDebug) override;
  void emitType(MCStreamer &OS) override;
};

class BTFTypeTypeTag : public BTFTypeBase {
  const DIDerivedType *DTy;
  StringRef Tag;

public:
  BTFTypeTypeTag(uint32_t NextTypeId, StringRef Tag);
  BTFTypeTypeTag(const DIDerivedType *DTy, StringRef Tag);
  void completeType(BTFDebug &BDebug) override;
};

/// String table.
class BTFStringTable {
  /// String table size in bytes.
  uint32_t Size;
  /// A mapping from string table offset to the index
  /// of the Table. It is used to avoid putting
  /// duplicated strings in the table.
  std::map<uint32_t, uint32_t> OffsetToIdMap;
  /// A vector of strings to represent the string table.
  std::vector<std::string> Table;

public:
  BTFStringTable() : Size(0) {}
  uint32_t getSize() { return Size; }
  std::vector<std::string> &getTable() { return Table; }
  /// Add a string to the string table and returns its offset
  /// in the table.
  uint32_t addString(StringRef S);
};

/// Represent one func and its type id.
struct BTFFuncInfo {
  const MCSymbol *Label; ///< Func MCSymbol
  uint32_t TypeId;       ///< Type id referring to .BTF type section
};

/// Represent one line info.
struct BTFLineInfo {
  MCSymbol *Label;      ///< MCSymbol identifying insn for the lineinfo
  uint32_t FileNameOff; ///< file name offset in the .BTF string table
  uint32_t LineOff;     ///< line offset in the .BTF string table
  uint32_t LineNum;     ///< the line number
  uint32_t ColumnNum;   ///< the column number
};

/// Represent one field relocation.
struct BTFFieldReloc {
  const MCSymbol *Label;  ///< MCSymbol identifying insn for the reloc
  uint32_t TypeID;        ///< Type ID
  uint32_t OffsetNameOff; ///< The string to traverse types
  uint32_t RelocKind;     ///< What to patch the instruction
};

/// Collect and emit BTF information.
class BTFDebug : public DebugHandlerBase {
  MCStreamer &OS;
  bool SkipInstruction;
  bool LineInfoGenerated;
  uint32_t SecNameOff;
  uint32_t ArrayIndexTypeId;
  bool MapDefNotCollected;
  BTFStringTable StringTable;
  std::vector<std::unique_ptr<BTFTypeBase>> TypeEntries;
  std::unordered_map<const DIType *, uint32_t> DIToIdMap;
  std::map<uint32_t, std::vector<BTFFuncInfo>> FuncInfoTable;
  std::map<uint32_t, std::vector<BTFLineInfo>> LineInfoTable;
  std::map<uint32_t, std::vector<BTFFieldReloc>> FieldRelocTable;
  StringMap<std::vector<std::string>> FileContent;
  std::map<std::string, std::unique_ptr<BTFKindDataSec>> DataSecEntries;
  std::vector<BTFTypeStruct *> StructTypes;
  std::map<const GlobalVariable *, std::pair<int64_t, uint32_t>> PatchImms;
  std::map<const DICompositeType *,
           std::vector<std::pair<const DIDerivedType *, BTFTypeDerived *>>>
      FixupDerivedTypes;
  std::set<const Function *>ProtoFunctions;

  /// Add types to TypeEntries.
  /// @{
  /// Add types to TypeEntries and DIToIdMap.
  uint32_t addType(std::unique_ptr<BTFTypeBase> TypeEntry, const DIType *Ty);
  /// Add types to TypeEntries only and return type id.
  uint32_t addType(std::unique_ptr<BTFTypeBase> TypeEntry);
  /// @}

  /// IR type visiting functions.
  /// @{
  void visitTypeEntry(const DIType *Ty);
  void visitTypeEntry(const DIType *Ty, uint32_t &TypeId, bool CheckPointer,
                      bool SeenPointer);
  void visitBasicType(const DIBasicType *BTy, uint32_t &TypeId);
  void visitSubroutineType(
      const DISubroutineType *STy, bool ForSubprog,
      const std::unordered_map<uint32_t, StringRef> &FuncArgNames,
      uint32_t &TypeId);
  void visitFwdDeclType(const DICompositeType *CTy, bool IsUnion,
                        uint32_t &TypeId);
  void visitCompositeType(const DICompositeType *CTy, uint32_t &TypeId);
  void visitStructType(const DICompositeType *STy, bool IsStruct,
                       uint32_t &TypeId);
  void visitArrayType(const DICompositeType *ATy, uint32_t &TypeId);
  void visitEnumType(const DICompositeType *ETy, uint32_t &TypeId);
  void visitDerivedType(const DIDerivedType *DTy, uint32_t &TypeId,
                        bool CheckPointer, bool SeenPointer);
  void visitMapDefType(const DIType *Ty, uint32_t &TypeId);
  /// @}

  /// Check whether the type is a forward declaration candidate or not.
  bool IsForwardDeclCandidate(const DIType *Base);

  /// Get the file content for the subprogram. Certain lines of the file
  /// later may be put into string table and referenced by line info.
  std::string populateFileContent(const DIFile *File);

  /// Construct a line info.
  void constructLineInfo(MCSymbol *Label, const DIFile *File, uint32_t Line,
                         uint32_t Column);

  /// Generate types and variables for globals.
  void processGlobals(bool ProcessingMapDef);

  /// Process global variable initializer in pursuit for function
  /// pointers.
  void processGlobalInitializer(const Constant *C);

  /// Generate types for function prototypes.
  void processFuncPrototypes(const Function *);

  /// Generate types for decl annotations.
  void processDeclAnnotations(DINodeArray Annotations, uint32_t BaseTypeId,
                              int ComponentId);

  /// Generate types for DISubprogram and it's arguments.
  uint32_t processDISubprogram(const DISubprogram *SP, uint32_t ProtoTypeId,
                               uint8_t Scope);

  /// Generate BTF type_tag's. If BaseTypeId is nonnegative, the last
  /// BTF type_tag in the chain points to BaseTypeId. Otherwise, it points to
  /// the base type of DTy. Return the type id of the first BTF type_tag
  /// in the chain. If no type_tag's are generated, a negative value
  /// is returned.
  int genBTFTypeTags(const DIDerivedType *DTy, int BaseTypeId);

  /// Generate one field relocation record.
  void generatePatchImmReloc(const MCSymbol *ORSym, uint32_t RootId,
                             const GlobalVariable *, bool IsAma);

  /// Populating unprocessed type on demand.
  unsigned populateType(const DIType *Ty);

  /// Process global variables referenced by relocation instructions
  /// and extern function references.
  void processGlobalValue(const MachineOperand &MO);

  /// Emit common header of .BTF and .BTF.ext sections.
  void emitCommonHeader();

  /// Emit the .BTF section.
  void emitBTFSection();

  /// Emit the .BTF.ext section.
  void emitBTFExtSection();

protected:
  /// Gather pre-function debug information.
  void beginFunctionImpl(const MachineFunction *MF) override;

  /// Post process after all instructions in this function are processed.
  void endFunctionImpl(const MachineFunction *MF) override;

public:
  BTFDebug(AsmPrinter *AP);

  ///
  bool InstLower(const MachineInstr *MI, MCInst &OutMI);

  /// Get the special array index type id.
  uint32_t getArrayIndexTypeId() {
    assert(ArrayIndexTypeId);
    return ArrayIndexTypeId;
  }

  /// Add string to the string table.
  size_t addString(StringRef S) { return StringTable.addString(S); }

  /// Get the type id for a particular DIType.
  uint32_t getTypeId(const DIType *Ty) {
    assert(Ty && "Invalid null Type");
    assert(DIToIdMap.find(Ty) != DIToIdMap.end() &&
           "DIType not added in the BDIToIdMap");
    return DIToIdMap[Ty];
  }

  /// Process beginning of an instruction.
  void beginInstruction(const MachineInstr *MI) override;

  /// Complete all the types and emit the BTF sections.
  void endModule() override;
};

} // end namespace llvm

#endif
