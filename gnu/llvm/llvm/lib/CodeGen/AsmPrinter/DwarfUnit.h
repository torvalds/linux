//===-- llvm/CodeGen/DwarfUnit.h - Dwarf Compile Unit ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_CODEGEN_ASMPRINTER_DWARFUNIT_H
#define LLVM_LIB_CODEGEN_ASMPRINTER_DWARFUNIT_H

#include "DwarfDebug.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/Target/TargetMachine.h"
#include <optional>
#include <string>

namespace llvm {

class ConstantFP;
class ConstantInt;
class DwarfCompileUnit;
class MCDwarfDwoLineTable;
class MCSymbol;

//===----------------------------------------------------------------------===//
/// This dwarf writer support class manages information associated with a
/// source file.
class DwarfUnit : public DIEUnit {
protected:
  /// A numeric ID unique among all CUs in the module
  unsigned UniqueID;
  /// MDNode for the compile unit.
  const DICompileUnit *CUNode;

  // All DIEValues are allocated through this allocator.
  BumpPtrAllocator DIEValueAllocator;

  /// Target of Dwarf emission.
  AsmPrinter *Asm;

  /// The start of the unit within its section.
  MCSymbol *LabelBegin = nullptr;

  /// Emitted at the end of the CU and used to compute the CU Length field.
  MCSymbol *EndLabel = nullptr;

  // Holders for some common dwarf information.
  DwarfDebug *DD;
  DwarfFile *DU;

  /// An anonymous type for index type.  Owned by DIEUnit.
  DIE *IndexTyDie = nullptr;

  /// Tracks the mapping of unit level debug information variables to debug
  /// information entries.
  DenseMap<const MDNode *, DIE *> MDNodeToDieMap;

  /// A list of all the DIEBlocks in use.
  std::vector<DIEBlock *> DIEBlocks;

  /// A list of all the DIELocs in use.
  std::vector<DIELoc *> DIELocs;

  /// This map is used to keep track of subprogram DIEs that need
  /// DW_AT_containing_type attribute. This attribute points to a DIE that
  /// corresponds to the MDNode mapped with the subprogram DIE.
  DenseMap<DIE *, const DINode *> ContainingTypeMap;

  DwarfUnit(dwarf::Tag, const DICompileUnit *Node, AsmPrinter *A,
            DwarfDebug *DW, DwarfFile *DWU, unsigned UniqueID = 0);

  bool applySubprogramDefinitionAttributes(const DISubprogram *SP, DIE &SPDie, bool Minimal);

  bool isShareableAcrossCUs(const DINode *D) const;

  template <typename T>
  void addAttribute(DIEValueList &Die, dwarf::Attribute Attribute,
                    dwarf::Form Form, T &&Value) {
    // For strict DWARF mode, only generate attributes available to current
    // DWARF version.
    // Attribute 0 is used when emitting form-encoded values in blocks, which
    // don't have attributes (only forms) so we cannot detect their DWARF
    // version compatibility here and assume they are compatible.
    if (Attribute != 0 && Asm->TM.Options.DebugStrictDwarf &&
        DD->getDwarfVersion() < dwarf::AttributeVersion(Attribute))
      return;

    Die.addValue(DIEValueAllocator,
                 DIEValue(Attribute, Form, std::forward<T>(Value)));
  }

public:
  /// Gets Unique ID for this unit.
  unsigned getUniqueID() const { return UniqueID; }
  // Accessors.
  AsmPrinter* getAsmPrinter() const { return Asm; }
  /// Get the the symbol for start of the section for this unit.
  MCSymbol *getLabelBegin() const {
    assert(LabelBegin && "LabelBegin is not initialized");
    return LabelBegin;
  }
  MCSymbol *getEndLabel() const { return EndLabel; }
  uint16_t getLanguage() const { return CUNode->getSourceLanguage(); }
  const DICompileUnit *getCUNode() const { return CUNode; }
  DwarfDebug &getDwarfDebug() const { return *DD; }

  /// Return true if this compile unit has something to write out.
  bool hasContent() const { return getUnitDie().hasChildren(); }

  /// Get string containing language specific context for a global name.
  ///
  /// Walks the metadata parent chain in a language specific manner (using the
  /// compile unit language) and returns it as a string. This is done at the
  /// metadata level because DIEs may not currently have been added to the
  /// parent context and walking the DIEs looking for names is more expensive
  /// than walking the metadata.
  std::string getParentContextString(const DIScope *Context) const;

  /// Add a new global name to the compile unit.
  virtual void addGlobalName(StringRef Name, const DIE &Die,
                             const DIScope *Context) = 0;

  /// Add a new global type to the compile unit.
  virtual void addGlobalTypeImpl(const DIType *Ty, const DIE &Die,
                                 const DIScope *Context) = 0;

  void addGlobalType(const DIType *Ty, const DIE &Die, const DIScope *Context);

  /// Returns the DIE map slot for the specified debug variable.
  ///
  /// We delegate the request to DwarfDebug when the MDNode can be part of the
  /// type system, since DIEs for the type system can be shared across CUs and
  /// the mappings are kept in DwarfDebug.
  DIE *getDIE(const DINode *D) const;

  /// Returns a fresh newly allocated DIELoc.
  DIELoc *getDIELoc() { return new (DIEValueAllocator) DIELoc; }

  /// Insert DIE into the map.
  ///
  /// We delegate the request to DwarfDebug when the MDNode can be part of the
  /// type system, since DIEs for the type system can be shared across CUs and
  /// the mappings are kept in DwarfDebug.
  void insertDIE(const DINode *Desc, DIE *D);

  void insertDIE(DIE *D);

  /// Add a flag that is true to the DIE.
  void addFlag(DIE &Die, dwarf::Attribute Attribute);

  /// Add an unsigned integer attribute data and value.
  void addUInt(DIEValueList &Die, dwarf::Attribute Attribute,
               std::optional<dwarf::Form> Form, uint64_t Integer);

  void addUInt(DIEValueList &Block, dwarf::Form Form, uint64_t Integer);

  /// Add an signed integer attribute data and value.
  void addSInt(DIEValueList &Die, dwarf::Attribute Attribute,
               std::optional<dwarf::Form> Form, int64_t Integer);

  void addSInt(DIELoc &Die, std::optional<dwarf::Form> Form, int64_t Integer);

  /// Add a string attribute data and value.
  ///
  /// We always emit a reference to the string pool instead of immediate
  /// strings so that DIEs have more predictable sizes. In the case of split
  /// dwarf we emit an index into another table which gets us the static offset
  /// into the string table.
  void addString(DIE &Die, dwarf::Attribute Attribute, StringRef Str);

  /// Add a Dwarf label attribute data and value.
  void addLabel(DIEValueList &Die, dwarf::Attribute Attribute, dwarf::Form Form,
                const MCSymbol *Label);

  void addLabel(DIELoc &Die, dwarf::Form Form, const MCSymbol *Label);

  /// Add an offset into a section attribute data and value.
  void addSectionOffset(DIE &Die, dwarf::Attribute Attribute, uint64_t Integer);

  /// Add a dwarf op address data and value using the form given and an
  /// op of either DW_FORM_addr or DW_FORM_GNU_addr_index.
  void addOpAddress(DIELoc &Die, const MCSymbol *Sym);
  void addPoolOpAddress(DIEValueList &Die, const MCSymbol *Label);

  /// Add a label delta attribute data and value.
  void addLabelDelta(DIEValueList &Die, dwarf::Attribute Attribute,
                     const MCSymbol *Hi, const MCSymbol *Lo);

  /// Add a DIE attribute data and value.
  void addDIEEntry(DIE &Die, dwarf::Attribute Attribute, DIE &Entry);

  /// Add a DIE attribute data and value.
  void addDIEEntry(DIE &Die, dwarf::Attribute Attribute, DIEEntry Entry);

  /// Add a type's DW_AT_signature and set the  declaration flag.
  void addDIETypeSignature(DIE &Die, uint64_t Signature);

  /// Add block data.
  void addBlock(DIE &Die, dwarf::Attribute Attribute, DIELoc *Loc);

  /// Add block data.
  void addBlock(DIE &Die, dwarf::Attribute Attribute, DIEBlock *Block);
  void addBlock(DIE &Die, dwarf::Attribute Attribute, dwarf::Form Form,
                DIEBlock *Block);

  /// Add location information to specified debug information entry.
  void addSourceLine(DIE &Die, unsigned Line, const DIFile *File);
  void addSourceLine(DIE &Die, const DILocalVariable *V);
  void addSourceLine(DIE &Die, const DIGlobalVariable *G);
  void addSourceLine(DIE &Die, const DISubprogram *SP);
  void addSourceLine(DIE &Die, const DILabel *L);
  void addSourceLine(DIE &Die, const DIType *Ty);
  void addSourceLine(DIE &Die, const DIObjCProperty *Ty);

  /// Add constant value entry in variable DIE.
  void addConstantValue(DIE &Die, const ConstantInt *CI, const DIType *Ty);
  void addConstantValue(DIE &Die, const APInt &Val, const DIType *Ty);
  void addConstantValue(DIE &Die, const APInt &Val, bool Unsigned);
  void addConstantValue(DIE &Die, uint64_t Val, const DIType *Ty);
  void addConstantValue(DIE &Die, bool Unsigned, uint64_t Val);

  /// Add constant value entry in variable DIE.
  void addConstantFPValue(DIE &Die, const ConstantFP *CFP);

  /// Add a linkage name, if it isn't empty.
  void addLinkageName(DIE &Die, StringRef LinkageName);

  /// Add template parameters in buffer.
  void addTemplateParams(DIE &Buffer, DINodeArray TParams);

  /// Add thrown types.
  void addThrownTypes(DIE &Die, DINodeArray ThrownTypes);

  /// Add the accessibility attribute.
  void addAccess(DIE &Die, DINode::DIFlags Flags);

  /// Add a new type attribute to the specified entity.
  ///
  /// This takes and attribute parameter because DW_AT_friend attributes are
  /// also type references.
  void addType(DIE &Entity, const DIType *Ty,
               dwarf::Attribute Attribute = dwarf::DW_AT_type);

  DIE *getOrCreateNameSpace(const DINamespace *NS);
  DIE *getOrCreateModule(const DIModule *M);
  DIE *getOrCreateSubprogramDIE(const DISubprogram *SP, bool Minimal = false);

  void applySubprogramAttributes(const DISubprogram *SP, DIE &SPDie,
                                 bool SkipSPAttributes = false);

  /// Creates type DIE with specific context.
  DIE *createTypeDIE(const DIScope *Context, DIE &ContextDIE, const DIType *Ty);

  /// Find existing DIE or create new DIE for the given type.
  virtual DIE *getOrCreateTypeDIE(const MDNode *TyNode);

  /// Get context owner's DIE.
  virtual DIE *getOrCreateContextDIE(const DIScope *Context);

  /// Construct DIEs for types that contain vtables.
  void constructContainingTypeDIEs();

  /// Construct function argument DIEs.
  void constructSubprogramArguments(DIE &Buffer, DITypeRefArray Args);

  /// Create a DIE with the given Tag, add the DIE to its parent, and
  /// call insertDIE if MD is not null.
  DIE &createAndAddDIE(dwarf::Tag Tag, DIE &Parent, const DINode *N = nullptr);

  bool useSegmentedStringOffsetsTable() const {
    return DD->useSegmentedStringOffsetsTable();
  }

  /// Compute the size of a header for this unit, not including the initial
  /// length field.
  virtual unsigned getHeaderSize() const {
    return sizeof(int16_t) +               // DWARF version number
           Asm->getDwarfOffsetByteSize() + // Offset Into Abbrev. Section
           sizeof(int8_t) +                // Pointer Size (in bytes)
           (DD->getDwarfVersion() >= 5 ? sizeof(int8_t)
                                       : 0); // DWARF v5 unit type
  }

  /// Emit the header for this unit, not including the initial length field.
  virtual void emitHeader(bool UseOffsets) = 0;

  /// Add the DW_AT_str_offsets_base attribute to the unit DIE.
  void addStringOffsetsStart();

  /// Add the DW_AT_rnglists_base attribute to the unit DIE.
  void addRnglistsBase();

  virtual DwarfCompileUnit &getCU() = 0;

  void constructTypeDIE(DIE &Buffer, const DICompositeType *CTy);

  /// addSectionDelta - Add a label delta attribute data and value.
  void addSectionDelta(DIE &Die, dwarf::Attribute Attribute, const MCSymbol *Hi,
                       const MCSymbol *Lo);

  /// Add a Dwarf section label attribute data and value.
  void addSectionLabel(DIE &Die, dwarf::Attribute Attribute,
                       const MCSymbol *Label, const MCSymbol *Sec);

  /// Add DW_TAG_LLVM_annotation.
  void addAnnotation(DIE &Buffer, DINodeArray Annotations);

  /// Get context owner's DIE.
  DIE *createTypeDIE(const DICompositeType *Ty);

protected:
  ~DwarfUnit();

  /// Create new static data member DIE.
  DIE *getOrCreateStaticMemberDIE(const DIDerivedType *DT);

  /// Look up the source ID for the given file. If none currently exists,
  /// create a new ID and insert it in the line table.
  virtual unsigned getOrCreateSourceID(const DIFile *File) = 0;

  /// Emit the common part of the header for this unit.
  void emitCommonHeader(bool UseOffsets, dwarf::UnitType UT);

private:
  void constructTypeDIE(DIE &Buffer, const DIBasicType *BTy);
  void constructTypeDIE(DIE &Buffer, const DIStringType *BTy);
  void constructTypeDIE(DIE &Buffer, const DIDerivedType *DTy);
  void constructTypeDIE(DIE &Buffer, const DISubroutineType *CTy);
  void constructSubrangeDIE(DIE &Buffer, const DISubrange *SR, DIE *IndexTy);
  void constructGenericSubrangeDIE(DIE &Buffer, const DIGenericSubrange *SR,
                                   DIE *IndexTy);
  void constructArrayTypeDIE(DIE &Buffer, const DICompositeType *CTy);
  void constructEnumTypeDIE(DIE &Buffer, const DICompositeType *CTy);
  DIE &constructMemberDIE(DIE &Buffer, const DIDerivedType *DT);
  void constructTemplateTypeParameterDIE(DIE &Buffer,
                                         const DITemplateTypeParameter *TP);
  void constructTemplateValueParameterDIE(DIE &Buffer,
                                          const DITemplateValueParameter *TVP);

  /// Return the default lower bound for an array.
  ///
  /// If the DWARF version doesn't handle the language, return -1.
  int64_t getDefaultLowerBound() const;

  /// Get an anonymous type for index type.
  DIE *getIndexTyDie();

  /// Set D as anonymous type for index which can be reused later.
  void setIndexTyDie(DIE *D) { IndexTyDie = D; }

  virtual void finishNonUnitTypeDIE(DIE& D, const DICompositeType *CTy) = 0;

  /// If this is a named finished type then include it in the list of types for
  /// the accelerator tables.
  void updateAcceleratorTables(const DIScope *Context, const DIType *Ty,
                               const DIE &TyDIE);

  virtual bool isDwoUnit() const = 0;
  const MCSymbol *getCrossSectionRelativeBaseAddress() const override;

  /// Returns 'true' if the current DwarfVersion is compatible
  /// with the specified \p Version.
  bool isCompatibleWithVersion(uint16_t Version) const;
};

class DwarfTypeUnit final : public DwarfUnit {
  uint64_t TypeSignature;
  const DIE *Ty;
  DwarfCompileUnit &CU;
  MCDwarfDwoLineTable *SplitLineTable;
  bool UsedLineTable = false;

  unsigned getOrCreateSourceID(const DIFile *File) override;
  void finishNonUnitTypeDIE(DIE& D, const DICompositeType *CTy) override;
  bool isDwoUnit() const override;

public:
  DwarfTypeUnit(DwarfCompileUnit &CU, AsmPrinter *A, DwarfDebug *DW,
                DwarfFile *DWU, unsigned UniqueID,
                MCDwarfDwoLineTable *SplitLineTable = nullptr);

  void setTypeSignature(uint64_t Signature) { TypeSignature = Signature; }
  /// Returns Type Signature.
  uint64_t getTypeSignature() const { return TypeSignature; }
  void setType(const DIE *Ty) { this->Ty = Ty; }

  /// Emit the header for this unit, not including the initial length field.
  void emitHeader(bool UseOffsets) override;
  unsigned getHeaderSize() const override {
    return DwarfUnit::getHeaderSize() + sizeof(uint64_t) + // Type Signature
           Asm->getDwarfOffsetByteSize();                  // Type DIE Offset
  }
  void addGlobalName(StringRef Name, const DIE &Die,
                     const DIScope *Context) override;
  void addGlobalTypeImpl(const DIType *Ty, const DIE &Die,
                         const DIScope *Context) override;
  DwarfCompileUnit &getCU() override { return CU; }
};
} // end llvm namespace
#endif
