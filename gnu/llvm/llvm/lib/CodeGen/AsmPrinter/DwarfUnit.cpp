//===-- llvm/CodeGen/DwarfUnit.cpp - Dwarf Type and Compile Units ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for constructing a dwarf compile unit.
//
//===----------------------------------------------------------------------===//

#include "DwarfUnit.h"
#include "AddressPool.h"
#include "DwarfCompileUnit.h"
#include "DwarfExpression.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Metadata.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Support/Casting.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

using namespace llvm;

#define DEBUG_TYPE "dwarfdebug"

DIEDwarfExpression::DIEDwarfExpression(const AsmPrinter &AP,
                                       DwarfCompileUnit &CU, DIELoc &DIE)
    : DwarfExpression(AP.getDwarfVersion(), CU), AP(AP), OutDIE(DIE) {}

void DIEDwarfExpression::emitOp(uint8_t Op, const char* Comment) {
  CU.addUInt(getActiveDIE(), dwarf::DW_FORM_data1, Op);
}

void DIEDwarfExpression::emitSigned(int64_t Value) {
  CU.addSInt(getActiveDIE(), dwarf::DW_FORM_sdata, Value);
}

void DIEDwarfExpression::emitUnsigned(uint64_t Value) {
  CU.addUInt(getActiveDIE(), dwarf::DW_FORM_udata, Value);
}

void DIEDwarfExpression::emitData1(uint8_t Value) {
  CU.addUInt(getActiveDIE(), dwarf::DW_FORM_data1, Value);
}

void DIEDwarfExpression::emitBaseTypeRef(uint64_t Idx) {
  CU.addBaseTypeRef(getActiveDIE(), Idx);
}

void DIEDwarfExpression::enableTemporaryBuffer() {
  assert(!IsBuffering && "Already buffering?");
  IsBuffering = true;
}

void DIEDwarfExpression::disableTemporaryBuffer() { IsBuffering = false; }

unsigned DIEDwarfExpression::getTemporaryBufferSize() {
  return TmpDIE.computeSize(AP.getDwarfFormParams());
}

void DIEDwarfExpression::commitTemporaryBuffer() { OutDIE.takeValues(TmpDIE); }

bool DIEDwarfExpression::isFrameRegister(const TargetRegisterInfo &TRI,
                                         llvm::Register MachineReg) {
  return MachineReg == TRI.getFrameRegister(*AP.MF);
}

DwarfUnit::DwarfUnit(dwarf::Tag UnitTag, const DICompileUnit *Node,
                     AsmPrinter *A, DwarfDebug *DW, DwarfFile *DWU,
                     unsigned UniqueID)
    : DIEUnit(UnitTag), UniqueID(UniqueID), CUNode(Node), Asm(A), DD(DW),
      DU(DWU) {}

DwarfTypeUnit::DwarfTypeUnit(DwarfCompileUnit &CU, AsmPrinter *A,
                             DwarfDebug *DW, DwarfFile *DWU, unsigned UniqueID,
                             MCDwarfDwoLineTable *SplitLineTable)
    : DwarfUnit(dwarf::DW_TAG_type_unit, CU.getCUNode(), A, DW, DWU, UniqueID),
      CU(CU), SplitLineTable(SplitLineTable) {}

DwarfUnit::~DwarfUnit() {
  for (DIEBlock *B : DIEBlocks)
    B->~DIEBlock();
  for (DIELoc *L : DIELocs)
    L->~DIELoc();
}

int64_t DwarfUnit::getDefaultLowerBound() const {
  switch (getLanguage()) {
  default:
    break;

  // The languages below have valid values in all DWARF versions.
  case dwarf::DW_LANG_C:
  case dwarf::DW_LANG_C89:
  case dwarf::DW_LANG_C_plus_plus:
    return 0;

  case dwarf::DW_LANG_Fortran77:
  case dwarf::DW_LANG_Fortran90:
    return 1;

  // The languages below have valid values only if the DWARF version >= 3.
  case dwarf::DW_LANG_C99:
  case dwarf::DW_LANG_ObjC:
  case dwarf::DW_LANG_ObjC_plus_plus:
    if (DD->getDwarfVersion() >= 3)
      return 0;
    break;

  case dwarf::DW_LANG_Fortran95:
    if (DD->getDwarfVersion() >= 3)
      return 1;
    break;

  // Starting with DWARF v4, all defined languages have valid values.
  case dwarf::DW_LANG_D:
  case dwarf::DW_LANG_Java:
  case dwarf::DW_LANG_Python:
  case dwarf::DW_LANG_UPC:
    if (DD->getDwarfVersion() >= 4)
      return 0;
    break;

  case dwarf::DW_LANG_Ada83:
  case dwarf::DW_LANG_Ada95:
  case dwarf::DW_LANG_Cobol74:
  case dwarf::DW_LANG_Cobol85:
  case dwarf::DW_LANG_Modula2:
  case dwarf::DW_LANG_Pascal83:
  case dwarf::DW_LANG_PLI:
    if (DD->getDwarfVersion() >= 4)
      return 1;
    break;

  // The languages below are new in DWARF v5.
  case dwarf::DW_LANG_BLISS:
  case dwarf::DW_LANG_C11:
  case dwarf::DW_LANG_C_plus_plus_03:
  case dwarf::DW_LANG_C_plus_plus_11:
  case dwarf::DW_LANG_C_plus_plus_14:
  case dwarf::DW_LANG_Dylan:
  case dwarf::DW_LANG_Go:
  case dwarf::DW_LANG_Haskell:
  case dwarf::DW_LANG_OCaml:
  case dwarf::DW_LANG_OpenCL:
  case dwarf::DW_LANG_RenderScript:
  case dwarf::DW_LANG_Rust:
  case dwarf::DW_LANG_Swift:
    if (DD->getDwarfVersion() >= 5)
      return 0;
    break;

  case dwarf::DW_LANG_Fortran03:
  case dwarf::DW_LANG_Fortran08:
  case dwarf::DW_LANG_Julia:
  case dwarf::DW_LANG_Modula3:
    if (DD->getDwarfVersion() >= 5)
      return 1;
    break;
  }

  return -1;
}

/// Check whether the DIE for this MDNode can be shared across CUs.
bool DwarfUnit::isShareableAcrossCUs(const DINode *D) const {
  // When the MDNode can be part of the type system, the DIE can be shared
  // across CUs.
  // Combining type units and cross-CU DIE sharing is lower value (since
  // cross-CU DIE sharing is used in LTO and removes type redundancy at that
  // level already) but may be implementable for some value in projects
  // building multiple independent libraries with LTO and then linking those
  // together.
  if (isDwoUnit() && !DD->shareAcrossDWOCUs())
    return false;
  return (isa<DIType>(D) ||
          (isa<DISubprogram>(D) && !cast<DISubprogram>(D)->isDefinition())) &&
         !DD->generateTypeUnits();
}

DIE *DwarfUnit::getDIE(const DINode *D) const {
  if (isShareableAcrossCUs(D))
    return DU->getDIE(D);
  return MDNodeToDieMap.lookup(D);
}

void DwarfUnit::insertDIE(const DINode *Desc, DIE *D) {
  if (isShareableAcrossCUs(Desc)) {
    DU->insertDIE(Desc, D);
    return;
  }
  MDNodeToDieMap.insert(std::make_pair(Desc, D));
}

void DwarfUnit::insertDIE(DIE *D) {
  MDNodeToDieMap.insert(std::make_pair(nullptr, D));
}

void DwarfUnit::addFlag(DIE &Die, dwarf::Attribute Attribute) {
  if (DD->getDwarfVersion() >= 4)
    addAttribute(Die, Attribute, dwarf::DW_FORM_flag_present, DIEInteger(1));
  else
    addAttribute(Die, Attribute, dwarf::DW_FORM_flag, DIEInteger(1));
}

void DwarfUnit::addUInt(DIEValueList &Die, dwarf::Attribute Attribute,
                        std::optional<dwarf::Form> Form, uint64_t Integer) {
  if (!Form)
    Form = DIEInteger::BestForm(false, Integer);
  assert(Form != dwarf::DW_FORM_implicit_const &&
         "DW_FORM_implicit_const is used only for signed integers");
  addAttribute(Die, Attribute, *Form, DIEInteger(Integer));
}

void DwarfUnit::addUInt(DIEValueList &Block, dwarf::Form Form,
                        uint64_t Integer) {
  addUInt(Block, (dwarf::Attribute)0, Form, Integer);
}

void DwarfUnit::addSInt(DIEValueList &Die, dwarf::Attribute Attribute,
                        std::optional<dwarf::Form> Form, int64_t Integer) {
  if (!Form)
    Form = DIEInteger::BestForm(true, Integer);
  addAttribute(Die, Attribute, *Form, DIEInteger(Integer));
}

void DwarfUnit::addSInt(DIELoc &Die, std::optional<dwarf::Form> Form,
                        int64_t Integer) {
  addSInt(Die, (dwarf::Attribute)0, Form, Integer);
}

void DwarfUnit::addString(DIE &Die, dwarf::Attribute Attribute,
                          StringRef String) {
  if (CUNode->isDebugDirectivesOnly())
    return;

  if (DD->useInlineStrings()) {
    addAttribute(Die, Attribute, dwarf::DW_FORM_string,
                 new (DIEValueAllocator)
                     DIEInlineString(String, DIEValueAllocator));
    return;
  }
  dwarf::Form IxForm =
      isDwoUnit() ? dwarf::DW_FORM_GNU_str_index : dwarf::DW_FORM_strp;

  auto StringPoolEntry =
      useSegmentedStringOffsetsTable() || IxForm == dwarf::DW_FORM_GNU_str_index
          ? DU->getStringPool().getIndexedEntry(*Asm, String)
          : DU->getStringPool().getEntry(*Asm, String);

  // For DWARF v5 and beyond, use the smallest strx? form possible.
  if (useSegmentedStringOffsetsTable()) {
    IxForm = dwarf::DW_FORM_strx1;
    unsigned Index = StringPoolEntry.getIndex();
    if (Index > 0xffffff)
      IxForm = dwarf::DW_FORM_strx4;
    else if (Index > 0xffff)
      IxForm = dwarf::DW_FORM_strx3;
    else if (Index > 0xff)
      IxForm = dwarf::DW_FORM_strx2;
  }
  addAttribute(Die, Attribute, IxForm, DIEString(StringPoolEntry));
}

void DwarfUnit::addLabel(DIEValueList &Die, dwarf::Attribute Attribute,
                         dwarf::Form Form, const MCSymbol *Label) {
  addAttribute(Die, Attribute, Form, DIELabel(Label));
}

void DwarfUnit::addLabel(DIELoc &Die, dwarf::Form Form, const MCSymbol *Label) {
  addLabel(Die, (dwarf::Attribute)0, Form, Label);
}

void DwarfUnit::addSectionOffset(DIE &Die, dwarf::Attribute Attribute,
                                 uint64_t Integer) {
  addUInt(Die, Attribute, DD->getDwarfSectionOffsetForm(), Integer);
}

unsigned DwarfTypeUnit::getOrCreateSourceID(const DIFile *File) {
  if (!SplitLineTable)
    return getCU().getOrCreateSourceID(File);
  if (!UsedLineTable) {
    UsedLineTable = true;
    // This is a split type unit that needs a line table.
    addSectionOffset(getUnitDie(), dwarf::DW_AT_stmt_list, 0);
  }
  return SplitLineTable->getFile(
      File->getDirectory(), File->getFilename(), DD->getMD5AsBytes(File),
      Asm->OutContext.getDwarfVersion(), File->getSource());
}

void DwarfUnit::addPoolOpAddress(DIEValueList &Die, const MCSymbol *Label) {
  bool UseAddrOffsetFormOrExpressions =
      DD->useAddrOffsetForm() || DD->useAddrOffsetExpressions();

  const MCSymbol *Base = nullptr;
  if (Label->isInSection() && UseAddrOffsetFormOrExpressions)
    Base = DD->getSectionLabel(&Label->getSection());

  uint32_t Index = DD->getAddressPool().getIndex(Base ? Base : Label);

  if (DD->getDwarfVersion() >= 5) {
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_addrx);
    addUInt(Die, dwarf::DW_FORM_addrx, Index);
  } else {
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_GNU_addr_index);
    addUInt(Die, dwarf::DW_FORM_GNU_addr_index, Index);
  }

  if (Base && Base != Label) {
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_const4u);
    addLabelDelta(Die, (dwarf::Attribute)0, Label, Base);
    addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_plus);
  }
}

void DwarfUnit::addOpAddress(DIELoc &Die, const MCSymbol *Sym) {
  if (DD->getDwarfVersion() >= 5) {
    addPoolOpAddress(Die, Sym);
    return;
  }

  if (DD->useSplitDwarf()) {
    addPoolOpAddress(Die, Sym);
    return;
  }

  addUInt(Die, dwarf::DW_FORM_data1, dwarf::DW_OP_addr);
  addLabel(Die, dwarf::DW_FORM_addr, Sym);
}

void DwarfUnit::addLabelDelta(DIEValueList &Die, dwarf::Attribute Attribute,
                              const MCSymbol *Hi, const MCSymbol *Lo) {
  addAttribute(Die, Attribute, dwarf::DW_FORM_data4,
               new (DIEValueAllocator) DIEDelta(Hi, Lo));
}

void DwarfUnit::addDIEEntry(DIE &Die, dwarf::Attribute Attribute, DIE &Entry) {
  addDIEEntry(Die, Attribute, DIEEntry(Entry));
}

void DwarfUnit::addDIETypeSignature(DIE &Die, uint64_t Signature) {
  // Flag the type unit reference as a declaration so that if it contains
  // members (implicit special members, static data member definitions, member
  // declarations for definitions in this CU, etc) consumers don't get confused
  // and think this is a full definition.
  addFlag(Die, dwarf::DW_AT_declaration);

  addAttribute(Die, dwarf::DW_AT_signature, dwarf::DW_FORM_ref_sig8,
               DIEInteger(Signature));
}

void DwarfUnit::addDIEEntry(DIE &Die, dwarf::Attribute Attribute,
                            DIEEntry Entry) {
  const DIEUnit *CU = Die.getUnit();
  const DIEUnit *EntryCU = Entry.getEntry().getUnit();
  if (!CU)
    // We assume that Die belongs to this CU, if it is not linked to any CU yet.
    CU = getUnitDie().getUnit();
  if (!EntryCU)
    EntryCU = getUnitDie().getUnit();
  assert(EntryCU == CU || !DD->useSplitDwarf() || DD->shareAcrossDWOCUs() ||
         !static_cast<const DwarfUnit*>(CU)->isDwoUnit());
  addAttribute(Die, Attribute,
               EntryCU == CU ? dwarf::DW_FORM_ref4 : dwarf::DW_FORM_ref_addr,
               Entry);
}

DIE &DwarfUnit::createAndAddDIE(dwarf::Tag Tag, DIE &Parent, const DINode *N) {
  DIE &Die = Parent.addChild(DIE::get(DIEValueAllocator, Tag));
  if (N)
    insertDIE(N, &Die);
  return Die;
}

void DwarfUnit::addBlock(DIE &Die, dwarf::Attribute Attribute, DIELoc *Loc) {
  Loc->computeSize(Asm->getDwarfFormParams());
  DIELocs.push_back(Loc); // Memoize so we can call the destructor later on.
  addAttribute(Die, Attribute, Loc->BestForm(DD->getDwarfVersion()), Loc);
}

void DwarfUnit::addBlock(DIE &Die, dwarf::Attribute Attribute, dwarf::Form Form,
                         DIEBlock *Block) {
  Block->computeSize(Asm->getDwarfFormParams());
  DIEBlocks.push_back(Block); // Memoize so we can call the destructor later on.
  addAttribute(Die, Attribute, Form, Block);
}

void DwarfUnit::addBlock(DIE &Die, dwarf::Attribute Attribute,
                         DIEBlock *Block) {
  addBlock(Die, Attribute, Block->BestForm(), Block);
}

void DwarfUnit::addSourceLine(DIE &Die, unsigned Line, const DIFile *File) {
  if (Line == 0)
    return;

  unsigned FileID = getOrCreateSourceID(File);
  addUInt(Die, dwarf::DW_AT_decl_file, std::nullopt, FileID);
  addUInt(Die, dwarf::DW_AT_decl_line, std::nullopt, Line);
}

void DwarfUnit::addSourceLine(DIE &Die, const DILocalVariable *V) {
  assert(V);

  addSourceLine(Die, V->getLine(), V->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIGlobalVariable *G) {
  assert(G);

  addSourceLine(Die, G->getLine(), G->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DISubprogram *SP) {
  assert(SP);

  addSourceLine(Die, SP->getLine(), SP->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DILabel *L) {
  assert(L);

  addSourceLine(Die, L->getLine(), L->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIType *Ty) {
  assert(Ty);

  addSourceLine(Die, Ty->getLine(), Ty->getFile());
}

void DwarfUnit::addSourceLine(DIE &Die, const DIObjCProperty *Ty) {
  assert(Ty);

  addSourceLine(Die, Ty->getLine(), Ty->getFile());
}

void DwarfUnit::addConstantFPValue(DIE &Die, const ConstantFP *CFP) {
  // Pass this down to addConstantValue as an unsigned bag of bits.
  addConstantValue(Die, CFP->getValueAPF().bitcastToAPInt(), true);
}

void DwarfUnit::addConstantValue(DIE &Die, const ConstantInt *CI,
                                 const DIType *Ty) {
  addConstantValue(Die, CI->getValue(), Ty);
}

void DwarfUnit::addConstantValue(DIE &Die, uint64_t Val, const DIType *Ty) {
  addConstantValue(Die, DD->isUnsignedDIType(Ty), Val);
}

void DwarfUnit::addConstantValue(DIE &Die, bool Unsigned, uint64_t Val) {
  // FIXME: This is a bit conservative/simple - it emits negative values always
  // sign extended to 64 bits rather than minimizing the number of bytes.
  addUInt(Die, dwarf::DW_AT_const_value,
          Unsigned ? dwarf::DW_FORM_udata : dwarf::DW_FORM_sdata, Val);
}

void DwarfUnit::addConstantValue(DIE &Die, const APInt &Val, const DIType *Ty) {
  addConstantValue(Die, Val, DD->isUnsignedDIType(Ty));
}

void DwarfUnit::addConstantValue(DIE &Die, const APInt &Val, bool Unsigned) {
  unsigned CIBitWidth = Val.getBitWidth();
  if (CIBitWidth <= 64) {
    addConstantValue(Die, Unsigned,
                     Unsigned ? Val.getZExtValue() : Val.getSExtValue());
    return;
  }

  DIEBlock *Block = new (DIEValueAllocator) DIEBlock;

  // Get the raw data form of the large APInt.
  const uint64_t *Ptr64 = Val.getRawData();

  int NumBytes = Val.getBitWidth() / 8; // 8 bits per byte.
  bool LittleEndian = Asm->getDataLayout().isLittleEndian();

  // Output the constant to DWARF one byte at a time.
  for (int i = 0; i < NumBytes; i++) {
    uint8_t c;
    if (LittleEndian)
      c = Ptr64[i / 8] >> (8 * (i & 7));
    else
      c = Ptr64[(NumBytes - 1 - i) / 8] >> (8 * ((NumBytes - 1 - i) & 7));
    addUInt(*Block, dwarf::DW_FORM_data1, c);
  }

  addBlock(Die, dwarf::DW_AT_const_value, Block);
}

void DwarfUnit::addLinkageName(DIE &Die, StringRef LinkageName) {
  if (!LinkageName.empty())
    addString(Die,
              DD->getDwarfVersion() >= 4 ? dwarf::DW_AT_linkage_name
                                         : dwarf::DW_AT_MIPS_linkage_name,
              GlobalValue::dropLLVMManglingEscape(LinkageName));
}

void DwarfUnit::addTemplateParams(DIE &Buffer, DINodeArray TParams) {
  // Add template parameters.
  for (const auto *Element : TParams) {
    if (auto *TTP = dyn_cast<DITemplateTypeParameter>(Element))
      constructTemplateTypeParameterDIE(Buffer, TTP);
    else if (auto *TVP = dyn_cast<DITemplateValueParameter>(Element))
      constructTemplateValueParameterDIE(Buffer, TVP);
  }
}

/// Add thrown types.
void DwarfUnit::addThrownTypes(DIE &Die, DINodeArray ThrownTypes) {
  for (const auto *Ty : ThrownTypes) {
    DIE &TT = createAndAddDIE(dwarf::DW_TAG_thrown_type, Die);
    addType(TT, cast<DIType>(Ty));
  }
}

void DwarfUnit::addAccess(DIE &Die, DINode::DIFlags Flags) {
  if ((Flags & DINode::FlagAccessibility) == DINode::FlagProtected)
    addUInt(Die, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_protected);
  else if ((Flags & DINode::FlagAccessibility) == DINode::FlagPrivate)
    addUInt(Die, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_private);
  else if ((Flags & DINode::FlagAccessibility) == DINode::FlagPublic)
    addUInt(Die, dwarf::DW_AT_accessibility, dwarf::DW_FORM_data1,
            dwarf::DW_ACCESS_public);
}

DIE *DwarfUnit::getOrCreateContextDIE(const DIScope *Context) {
  if (!Context || isa<DIFile>(Context) || isa<DICompileUnit>(Context))
    return &getUnitDie();
  if (auto *T = dyn_cast<DIType>(Context))
    return getOrCreateTypeDIE(T);
  if (auto *NS = dyn_cast<DINamespace>(Context))
    return getOrCreateNameSpace(NS);
  if (auto *SP = dyn_cast<DISubprogram>(Context))
    return getOrCreateSubprogramDIE(SP);
  if (auto *M = dyn_cast<DIModule>(Context))
    return getOrCreateModule(M);
  return getDIE(Context);
}

DIE *DwarfUnit::createTypeDIE(const DICompositeType *Ty) {
  auto *Context = Ty->getScope();
  DIE *ContextDIE = getOrCreateContextDIE(Context);

  if (DIE *TyDIE = getDIE(Ty))
    return TyDIE;

  // Create new type.
  DIE &TyDIE = createAndAddDIE(Ty->getTag(), *ContextDIE, Ty);

  constructTypeDIE(TyDIE, cast<DICompositeType>(Ty));

  updateAcceleratorTables(Context, Ty, TyDIE);
  return &TyDIE;
}

DIE *DwarfUnit::createTypeDIE(const DIScope *Context, DIE &ContextDIE,
                              const DIType *Ty) {
  // Create new type.
  DIE &TyDIE = createAndAddDIE(Ty->getTag(), ContextDIE, Ty);

  auto construct = [&](const auto *Ty) {
    updateAcceleratorTables(Context, Ty, TyDIE);
    constructTypeDIE(TyDIE, Ty);
  };

  if (auto *CTy = dyn_cast<DICompositeType>(Ty)) {
    if (DD->generateTypeUnits() && !Ty->isForwardDecl() &&
        (Ty->getRawName() || CTy->getRawIdentifier())) {
      // Skip updating the accelerator tables since this is not the full type.
      if (MDString *TypeId = CTy->getRawIdentifier()) {
        addGlobalType(Ty, TyDIE, Context);
        DD->addDwarfTypeUnitType(getCU(), TypeId->getString(), TyDIE, CTy);
      } else {
        updateAcceleratorTables(Context, Ty, TyDIE);
        finishNonUnitTypeDIE(TyDIE, CTy);
      }
      return &TyDIE;
    }
    construct(CTy);
  } else if (auto *BT = dyn_cast<DIBasicType>(Ty))
    construct(BT);
  else if (auto *ST = dyn_cast<DIStringType>(Ty))
    construct(ST);
  else if (auto *STy = dyn_cast<DISubroutineType>(Ty))
    construct(STy);
  else
    construct(cast<DIDerivedType>(Ty));

  return &TyDIE;
}

DIE *DwarfUnit::getOrCreateTypeDIE(const MDNode *TyNode) {
  if (!TyNode)
    return nullptr;

  auto *Ty = cast<DIType>(TyNode);

  // DW_TAG_restrict_type is not supported in DWARF2
  if (Ty->getTag() == dwarf::DW_TAG_restrict_type && DD->getDwarfVersion() <= 2)
    return getOrCreateTypeDIE(cast<DIDerivedType>(Ty)->getBaseType());

  // DW_TAG_atomic_type is not supported in DWARF < 5
  if (Ty->getTag() == dwarf::DW_TAG_atomic_type && DD->getDwarfVersion() < 5)
    return getOrCreateTypeDIE(cast<DIDerivedType>(Ty)->getBaseType());

  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  auto *Context = Ty->getScope();
  DIE *ContextDIE = getOrCreateContextDIE(Context);
  assert(ContextDIE);

  if (DIE *TyDIE = getDIE(Ty))
    return TyDIE;

  return static_cast<DwarfUnit *>(ContextDIE->getUnit())
      ->createTypeDIE(Context, *ContextDIE, Ty);
}

void DwarfUnit::updateAcceleratorTables(const DIScope *Context,
                                        const DIType *Ty, const DIE &TyDIE) {
  if (Ty->getName().empty())
    return;
  if (Ty->isForwardDecl())
    return;

  // add temporary record for this type to be added later

  bool IsImplementation = false;
  if (auto *CT = dyn_cast<DICompositeType>(Ty)) {
    // A runtime language of 0 actually means C/C++ and that any
    // non-negative value is some version of Objective-C/C++.
    IsImplementation = CT->getRuntimeLang() == 0 || CT->isObjcClassComplete();
  }
  unsigned Flags = IsImplementation ? dwarf::DW_FLAG_type_implementation : 0;
  DD->addAccelType(*this, CUNode->getNameTableKind(), Ty->getName(), TyDIE,
                   Flags);

  addGlobalType(Ty, TyDIE, Context);
}

void DwarfUnit::addGlobalType(const DIType *Ty, const DIE &TyDIE,
                              const DIScope *Context) {
  if (!Context || isa<DICompileUnit>(Context) || isa<DIFile>(Context) ||
      isa<DINamespace>(Context) || isa<DICommonBlock>(Context))
    addGlobalTypeImpl(Ty, TyDIE, Context);
}

void DwarfUnit::addType(DIE &Entity, const DIType *Ty,
                        dwarf::Attribute Attribute) {
  assert(Ty && "Trying to add a type that doesn't exist?");
  addDIEEntry(Entity, Attribute, DIEEntry(*getOrCreateTypeDIE(Ty)));
}

std::string DwarfUnit::getParentContextString(const DIScope *Context) const {
  if (!Context)
    return "";

  // FIXME: Decide whether to implement this for non-C++ languages.
  if (!dwarf::isCPlusPlus((dwarf::SourceLanguage)getLanguage()))
    return "";

  std::string CS;
  SmallVector<const DIScope *, 1> Parents;
  while (!isa<DICompileUnit>(Context)) {
    Parents.push_back(Context);
    if (const DIScope *S = Context->getScope())
      Context = S;
    else
      // Structure, etc types will have a NULL context if they're at the top
      // level.
      break;
  }

  // Reverse iterate over our list to go from the outermost construct to the
  // innermost.
  for (const DIScope *Ctx : llvm::reverse(Parents)) {
    StringRef Name = Ctx->getName();
    if (Name.empty() && isa<DINamespace>(Ctx))
      Name = "(anonymous namespace)";
    if (!Name.empty()) {
      CS += Name;
      CS += "::";
    }
  }
  return CS;
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DIBasicType *BTy) {
  // Get core information.
  StringRef Name = BTy->getName();
  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  // An unspecified type only has a name attribute.
  if (BTy->getTag() == dwarf::DW_TAG_unspecified_type)
    return;

  if (BTy->getTag() != dwarf::DW_TAG_string_type)
    addUInt(Buffer, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
            BTy->getEncoding());

  uint64_t Size = BTy->getSizeInBits() >> 3;
  addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt, Size);

  if (BTy->isBigEndian())
    addUInt(Buffer, dwarf::DW_AT_endianity, std::nullopt, dwarf::DW_END_big);
  else if (BTy->isLittleEndian())
    addUInt(Buffer, dwarf::DW_AT_endianity, std::nullopt, dwarf::DW_END_little);
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DIStringType *STy) {
  // Get core information.
  StringRef Name = STy->getName();
  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  if (DIVariable *Var = STy->getStringLength()) {
    if (auto *VarDIE = getDIE(Var))
      addDIEEntry(Buffer, dwarf::DW_AT_string_length, *VarDIE);
  } else if (DIExpression *Expr = STy->getStringLengthExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    // This is to describe the memory location of the
    // length of a Fortran deferred length string, so
    // lock it down as such.
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(Expr);
    addBlock(Buffer, dwarf::DW_AT_string_length, DwarfExpr.finalize());
  } else {
    uint64_t Size = STy->getSizeInBits() >> 3;
    addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt, Size);
  }

  if (DIExpression *Expr = STy->getStringLocationExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    // This is to describe the memory location of the
    // string, so lock it down as such.
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(Expr);
    addBlock(Buffer, dwarf::DW_AT_data_location, DwarfExpr.finalize());
  }

  if (STy->getEncoding()) {
    // For eventual Unicode support.
    addUInt(Buffer, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
            STy->getEncoding());
  }
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DIDerivedType *DTy) {
  // Get core information.
  StringRef Name = DTy->getName();
  uint64_t Size = DTy->getSizeInBits() >> 3;
  uint16_t Tag = Buffer.getTag();

  // Map to main type, void will not have a type.
  const DIType *FromTy = DTy->getBaseType();
  if (FromTy)
    addType(Buffer, FromTy);

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  addAnnotation(Buffer, DTy->getAnnotations());

  // If alignment is specified for a typedef , create and insert DW_AT_alignment
  // attribute in DW_TAG_typedef DIE.
  if (Tag == dwarf::DW_TAG_typedef && DD->getDwarfVersion() >= 5) {
    uint32_t AlignInBytes = DTy->getAlignInBytes();
    if (AlignInBytes > 0)
      addUInt(Buffer, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
              AlignInBytes);
  }

  // Add size if non-zero (derived types might be zero-sized.)
  if (Size && Tag != dwarf::DW_TAG_pointer_type
           && Tag != dwarf::DW_TAG_ptr_to_member_type
           && Tag != dwarf::DW_TAG_reference_type
           && Tag != dwarf::DW_TAG_rvalue_reference_type)
    addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt, Size);

  if (Tag == dwarf::DW_TAG_ptr_to_member_type)
    addDIEEntry(Buffer, dwarf::DW_AT_containing_type,
                *getOrCreateTypeDIE(cast<DIDerivedType>(DTy)->getClassType()));

  addAccess(Buffer, DTy->getFlags());

  // Add source line info if available and TyDesc is not a forward declaration.
  if (!DTy->isForwardDecl())
    addSourceLine(Buffer, DTy);

  // If DWARF address space value is other than None, add it.  The IR
  // verifier checks that DWARF address space only exists for pointer
  // or reference types.
  if (DTy->getDWARFAddressSpace())
    addUInt(Buffer, dwarf::DW_AT_address_class, dwarf::DW_FORM_data4,
            *DTy->getDWARFAddressSpace());

  // Add template alias template parameters.
  if (Tag == dwarf::DW_TAG_template_alias)
    addTemplateParams(Buffer, DTy->getTemplateParams());

  if (auto PtrAuthData = DTy->getPtrAuthData()) {
    addUInt(Buffer, dwarf::DW_AT_LLVM_ptrauth_key, dwarf::DW_FORM_data1,
            PtrAuthData->key());
    if (PtrAuthData->isAddressDiscriminated())
      addFlag(Buffer, dwarf::DW_AT_LLVM_ptrauth_address_discriminated);
    addUInt(Buffer, dwarf::DW_AT_LLVM_ptrauth_extra_discriminator,
            dwarf::DW_FORM_data2, PtrAuthData->extraDiscriminator());
    if (PtrAuthData->isaPointer())
      addFlag(Buffer, dwarf::DW_AT_LLVM_ptrauth_isa_pointer);
    if (PtrAuthData->authenticatesNullValues())
      addFlag(Buffer, dwarf::DW_AT_LLVM_ptrauth_authenticates_null_values);
  }
}

void DwarfUnit::constructSubprogramArguments(DIE &Buffer, DITypeRefArray Args) {
  for (unsigned i = 1, N = Args.size(); i < N; ++i) {
    const DIType *Ty = Args[i];
    if (!Ty) {
      assert(i == N-1 && "Unspecified parameter must be the last argument");
      createAndAddDIE(dwarf::DW_TAG_unspecified_parameters, Buffer);
    } else {
      DIE &Arg = createAndAddDIE(dwarf::DW_TAG_formal_parameter, Buffer);
      addType(Arg, Ty);
      if (Ty->isArtificial())
        addFlag(Arg, dwarf::DW_AT_artificial);
    }
  }
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DISubroutineType *CTy) {
  // Add return type.  A void return won't have a type.
  auto Elements = cast<DISubroutineType>(CTy)->getTypeArray();
  if (Elements.size())
    if (auto RTy = Elements[0])
      addType(Buffer, RTy);

  bool isPrototyped = true;
  if (Elements.size() == 2 && !Elements[1])
    isPrototyped = false;

  constructSubprogramArguments(Buffer, Elements);

  // Add prototype flag if we're dealing with a C language and the function has
  // been prototyped.
  if (isPrototyped && dwarf::isC((dwarf::SourceLanguage)getLanguage()))
    addFlag(Buffer, dwarf::DW_AT_prototyped);

  // Add a DW_AT_calling_convention if this has an explicit convention.
  if (CTy->getCC() && CTy->getCC() != dwarf::DW_CC_normal)
    addUInt(Buffer, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1,
            CTy->getCC());

  if (CTy->isLValueReference())
    addFlag(Buffer, dwarf::DW_AT_reference);

  if (CTy->isRValueReference())
    addFlag(Buffer, dwarf::DW_AT_rvalue_reference);
}

void DwarfUnit::addAnnotation(DIE &Buffer, DINodeArray Annotations) {
  if (!Annotations)
    return;

  for (const Metadata *Annotation : Annotations->operands()) {
    const MDNode *MD = cast<MDNode>(Annotation);
    const MDString *Name = cast<MDString>(MD->getOperand(0));
    const auto &Value = MD->getOperand(1);

    DIE &AnnotationDie = createAndAddDIE(dwarf::DW_TAG_LLVM_annotation, Buffer);
    addString(AnnotationDie, dwarf::DW_AT_name, Name->getString());
    if (const auto *Data = dyn_cast<MDString>(Value))
      addString(AnnotationDie, dwarf::DW_AT_const_value, Data->getString());
    else if (const auto *Data = dyn_cast<ConstantAsMetadata>(Value))
      addConstantValue(AnnotationDie, Data->getValue()->getUniqueInteger(),
                       /*Unsigned=*/true);
    else
      assert(false && "Unsupported annotation value type");
  }
}

void DwarfUnit::constructTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  // Add name if not anonymous or intermediate type.
  StringRef Name = CTy->getName();

  uint64_t Size = CTy->getSizeInBits() >> 3;
  uint16_t Tag = Buffer.getTag();

  switch (Tag) {
  case dwarf::DW_TAG_array_type:
    constructArrayTypeDIE(Buffer, CTy);
    break;
  case dwarf::DW_TAG_enumeration_type:
    constructEnumTypeDIE(Buffer, CTy);
    break;
  case dwarf::DW_TAG_variant_part:
  case dwarf::DW_TAG_structure_type:
  case dwarf::DW_TAG_union_type:
  case dwarf::DW_TAG_class_type:
  case dwarf::DW_TAG_namelist: {
    // Emit the discriminator for a variant part.
    DIDerivedType *Discriminator = nullptr;
    if (Tag == dwarf::DW_TAG_variant_part) {
      Discriminator = CTy->getDiscriminator();
      if (Discriminator) {
        // DWARF says:
        //    If the variant part has a discriminant, the discriminant is
        //    represented by a separate debugging information entry which is
        //    a child of the variant part entry.
        DIE &DiscMember = constructMemberDIE(Buffer, Discriminator);
        addDIEEntry(Buffer, dwarf::DW_AT_discr, DiscMember);
      }
    }

    // Add template parameters to a class, structure or union types.
    if (Tag == dwarf::DW_TAG_class_type ||
        Tag == dwarf::DW_TAG_structure_type || Tag == dwarf::DW_TAG_union_type)
      addTemplateParams(Buffer, CTy->getTemplateParams());

    // Add elements to structure type.
    DINodeArray Elements = CTy->getElements();
    for (const auto *Element : Elements) {
      if (!Element)
        continue;
      if (auto *SP = dyn_cast<DISubprogram>(Element))
        getOrCreateSubprogramDIE(SP);
      else if (auto *DDTy = dyn_cast<DIDerivedType>(Element)) {
        if (DDTy->getTag() == dwarf::DW_TAG_friend) {
          DIE &ElemDie = createAndAddDIE(dwarf::DW_TAG_friend, Buffer);
          addType(ElemDie, DDTy->getBaseType(), dwarf::DW_AT_friend);
        } else if (DDTy->isStaticMember()) {
          getOrCreateStaticMemberDIE(DDTy);
        } else if (Tag == dwarf::DW_TAG_variant_part) {
          // When emitting a variant part, wrap each member in
          // DW_TAG_variant.
          DIE &Variant = createAndAddDIE(dwarf::DW_TAG_variant, Buffer);
          if (const ConstantInt *CI =
              dyn_cast_or_null<ConstantInt>(DDTy->getDiscriminantValue())) {
            if (DD->isUnsignedDIType(Discriminator->getBaseType()))
              addUInt(Variant, dwarf::DW_AT_discr_value, std::nullopt,
                      CI->getZExtValue());
            else
              addSInt(Variant, dwarf::DW_AT_discr_value, std::nullopt,
                      CI->getSExtValue());
          }
          constructMemberDIE(Variant, DDTy);
        } else {
          constructMemberDIE(Buffer, DDTy);
        }
      } else if (auto *Property = dyn_cast<DIObjCProperty>(Element)) {
        DIE &ElemDie = createAndAddDIE(Property->getTag(), Buffer);
        StringRef PropertyName = Property->getName();
        addString(ElemDie, dwarf::DW_AT_APPLE_property_name, PropertyName);
        if (Property->getType())
          addType(ElemDie, Property->getType());
        addSourceLine(ElemDie, Property);
        StringRef GetterName = Property->getGetterName();
        if (!GetterName.empty())
          addString(ElemDie, dwarf::DW_AT_APPLE_property_getter, GetterName);
        StringRef SetterName = Property->getSetterName();
        if (!SetterName.empty())
          addString(ElemDie, dwarf::DW_AT_APPLE_property_setter, SetterName);
        if (unsigned PropertyAttributes = Property->getAttributes())
          addUInt(ElemDie, dwarf::DW_AT_APPLE_property_attribute, std::nullopt,
                  PropertyAttributes);
      } else if (auto *Composite = dyn_cast<DICompositeType>(Element)) {
        if (Composite->getTag() == dwarf::DW_TAG_variant_part) {
          DIE &VariantPart = createAndAddDIE(Composite->getTag(), Buffer);
          constructTypeDIE(VariantPart, Composite);
        }
      } else if (Tag == dwarf::DW_TAG_namelist) {
        auto *Var = dyn_cast<DINode>(Element);
        auto *VarDIE = getDIE(Var);
        if (VarDIE) {
          DIE &ItemDie = createAndAddDIE(dwarf::DW_TAG_namelist_item, Buffer);
          addDIEEntry(ItemDie, dwarf::DW_AT_namelist_item, *VarDIE);
        }
      }
    }

    if (CTy->isAppleBlockExtension())
      addFlag(Buffer, dwarf::DW_AT_APPLE_block);

    if (CTy->getExportSymbols())
      addFlag(Buffer, dwarf::DW_AT_export_symbols);

    // This is outside the DWARF spec, but GDB expects a DW_AT_containing_type
    // inside C++ composite types to point to the base class with the vtable.
    // Rust uses DW_AT_containing_type to link a vtable to the type
    // for which it was created.
    if (auto *ContainingType = CTy->getVTableHolder())
      addDIEEntry(Buffer, dwarf::DW_AT_containing_type,
                  *getOrCreateTypeDIE(ContainingType));

    if (CTy->isObjcClassComplete())
      addFlag(Buffer, dwarf::DW_AT_APPLE_objc_complete_type);

    // Add the type's non-standard calling convention.
    // DW_CC_pass_by_value/DW_CC_pass_by_reference are introduced in DWARF 5.
    if (!Asm->TM.Options.DebugStrictDwarf || DD->getDwarfVersion() >= 5) {
      uint8_t CC = 0;
      if (CTy->isTypePassByValue())
        CC = dwarf::DW_CC_pass_by_value;
      else if (CTy->isTypePassByReference())
        CC = dwarf::DW_CC_pass_by_reference;
      if (CC)
        addUInt(Buffer, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1,
                CC);
    }
    break;
  }
  default:
    break;
  }

  // Add name if not anonymous or intermediate type.
  if (!Name.empty())
    addString(Buffer, dwarf::DW_AT_name, Name);

  addAnnotation(Buffer, CTy->getAnnotations());

  if (Tag == dwarf::DW_TAG_enumeration_type ||
      Tag == dwarf::DW_TAG_class_type || Tag == dwarf::DW_TAG_structure_type ||
      Tag == dwarf::DW_TAG_union_type) {
    // Add size if non-zero (derived types might be zero-sized.)
    // Ignore the size if it's a non-enum forward decl.
    // TODO: Do we care about size for enum forward declarations?
    if (Size &&
        (!CTy->isForwardDecl() || Tag == dwarf::DW_TAG_enumeration_type))
      addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt, Size);
    else if (!CTy->isForwardDecl())
      // Add zero size if it is not a forward declaration.
      addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt, 0);

    // If we're a forward decl, say so.
    if (CTy->isForwardDecl())
      addFlag(Buffer, dwarf::DW_AT_declaration);

    // Add accessibility info if available.
    addAccess(Buffer, CTy->getFlags());

    // Add source line info if available.
    if (!CTy->isForwardDecl())
      addSourceLine(Buffer, CTy);

    // No harm in adding the runtime language to the declaration.
    unsigned RLang = CTy->getRuntimeLang();
    if (RLang)
      addUInt(Buffer, dwarf::DW_AT_APPLE_runtime_class, dwarf::DW_FORM_data1,
              RLang);

    // Add align info if available.
    if (uint32_t AlignInBytes = CTy->getAlignInBytes())
      addUInt(Buffer, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
              AlignInBytes);
  }
}

void DwarfUnit::constructTemplateTypeParameterDIE(
    DIE &Buffer, const DITemplateTypeParameter *TP) {
  DIE &ParamDIE =
      createAndAddDIE(dwarf::DW_TAG_template_type_parameter, Buffer);
  // Add the type if it exists, it could be void and therefore no type.
  if (TP->getType())
    addType(ParamDIE, TP->getType());
  if (!TP->getName().empty())
    addString(ParamDIE, dwarf::DW_AT_name, TP->getName());
  if (TP->isDefault() && isCompatibleWithVersion(5))
    addFlag(ParamDIE, dwarf::DW_AT_default_value);
}

void DwarfUnit::constructTemplateValueParameterDIE(
    DIE &Buffer, const DITemplateValueParameter *VP) {
  DIE &ParamDIE = createAndAddDIE(VP->getTag(), Buffer);

  // Add the type if there is one, template template and template parameter
  // packs will not have a type.
  if (VP->getTag() == dwarf::DW_TAG_template_value_parameter)
    addType(ParamDIE, VP->getType());
  if (!VP->getName().empty())
    addString(ParamDIE, dwarf::DW_AT_name, VP->getName());
  if (VP->isDefault() && isCompatibleWithVersion(5))
    addFlag(ParamDIE, dwarf::DW_AT_default_value);
  if (Metadata *Val = VP->getValue()) {
    if (ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(Val))
      addConstantValue(ParamDIE, CI, VP->getType());
    else if (GlobalValue *GV = mdconst::dyn_extract<GlobalValue>(Val)) {
      // We cannot describe the location of dllimport'd entities: the
      // computation of their address requires loads from the IAT.
      if (!GV->hasDLLImportStorageClass()) {
        // For declaration non-type template parameters (such as global values
        // and functions)
        DIELoc *Loc = new (DIEValueAllocator) DIELoc;
        addOpAddress(*Loc, Asm->getSymbol(GV));
        // Emit DW_OP_stack_value to use the address as the immediate value of
        // the parameter, rather than a pointer to it.
        addUInt(*Loc, dwarf::DW_FORM_data1, dwarf::DW_OP_stack_value);
        addBlock(ParamDIE, dwarf::DW_AT_location, Loc);
      }
    } else if (VP->getTag() == dwarf::DW_TAG_GNU_template_template_param) {
      assert(isa<MDString>(Val));
      addString(ParamDIE, dwarf::DW_AT_GNU_template_name,
                cast<MDString>(Val)->getString());
    } else if (VP->getTag() == dwarf::DW_TAG_GNU_template_parameter_pack) {
      addTemplateParams(ParamDIE, cast<MDTuple>(Val));
    }
  }
}

DIE *DwarfUnit::getOrCreateNameSpace(const DINamespace *NS) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(NS->getScope());

  if (DIE *NDie = getDIE(NS))
    return NDie;
  DIE &NDie = createAndAddDIE(dwarf::DW_TAG_namespace, *ContextDIE, NS);

  StringRef Name = NS->getName();
  if (!Name.empty())
    addString(NDie, dwarf::DW_AT_name, NS->getName());
  else
    Name = "(anonymous namespace)";
  DD->addAccelNamespace(*this, CUNode->getNameTableKind(), Name, NDie);
  addGlobalName(Name, NDie, NS->getScope());
  if (NS->getExportSymbols())
    addFlag(NDie, dwarf::DW_AT_export_symbols);
  return &NDie;
}

DIE *DwarfUnit::getOrCreateModule(const DIModule *M) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(M->getScope());

  if (DIE *MDie = getDIE(M))
    return MDie;
  DIE &MDie = createAndAddDIE(dwarf::DW_TAG_module, *ContextDIE, M);

  if (!M->getName().empty()) {
    addString(MDie, dwarf::DW_AT_name, M->getName());
    addGlobalName(M->getName(), MDie, M->getScope());
  }
  if (!M->getConfigurationMacros().empty())
    addString(MDie, dwarf::DW_AT_LLVM_config_macros,
              M->getConfigurationMacros());
  if (!M->getIncludePath().empty())
    addString(MDie, dwarf::DW_AT_LLVM_include_path, M->getIncludePath());
  if (!M->getAPINotesFile().empty())
    addString(MDie, dwarf::DW_AT_LLVM_apinotes, M->getAPINotesFile());
  if (M->getFile())
    addUInt(MDie, dwarf::DW_AT_decl_file, std::nullopt,
            getOrCreateSourceID(M->getFile()));
  if (M->getLineNo())
    addUInt(MDie, dwarf::DW_AT_decl_line, std::nullopt, M->getLineNo());
  if (M->getIsDecl())
    addFlag(MDie, dwarf::DW_AT_declaration);

  return &MDie;
}

DIE *DwarfUnit::getOrCreateSubprogramDIE(const DISubprogram *SP, bool Minimal) {
  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE (as is the case for member function
  // declarations).
  DIE *ContextDIE =
      Minimal ? &getUnitDie() : getOrCreateContextDIE(SP->getScope());

  if (DIE *SPDie = getDIE(SP))
    return SPDie;

  if (auto *SPDecl = SP->getDeclaration()) {
    if (!Minimal) {
      // Add subprogram definitions to the CU die directly.
      ContextDIE = &getUnitDie();
      // Build the decl now to ensure it precedes the definition.
      getOrCreateSubprogramDIE(SPDecl);
    }
  }

  // DW_TAG_inlined_subroutine may refer to this DIE.
  DIE &SPDie = createAndAddDIE(dwarf::DW_TAG_subprogram, *ContextDIE, SP);

  // Stop here and fill this in later, depending on whether or not this
  // subprogram turns out to have inlined instances or not.
  if (SP->isDefinition())
    return &SPDie;

  static_cast<DwarfUnit *>(SPDie.getUnit())
      ->applySubprogramAttributes(SP, SPDie);
  return &SPDie;
}

bool DwarfUnit::applySubprogramDefinitionAttributes(const DISubprogram *SP,
                                                    DIE &SPDie, bool Minimal) {
  DIE *DeclDie = nullptr;
  StringRef DeclLinkageName;
  if (auto *SPDecl = SP->getDeclaration()) {
    if (!Minimal) {
      DITypeRefArray DeclArgs, DefinitionArgs;
      DeclArgs = SPDecl->getType()->getTypeArray();
      DefinitionArgs = SP->getType()->getTypeArray();

      if (DeclArgs.size() && DefinitionArgs.size())
        if (DefinitionArgs[0] != nullptr && DeclArgs[0] != DefinitionArgs[0])
          addType(SPDie, DefinitionArgs[0]);

      DeclDie = getDIE(SPDecl);
      assert(DeclDie && "This DIE should've already been constructed when the "
                        "definition DIE was created in "
                        "getOrCreateSubprogramDIE");
      // Look at the Decl's linkage name only if we emitted it.
      if (DD->useAllLinkageNames())
        DeclLinkageName = SPDecl->getLinkageName();
      unsigned DeclID = getOrCreateSourceID(SPDecl->getFile());
      unsigned DefID = getOrCreateSourceID(SP->getFile());
      if (DeclID != DefID)
        addUInt(SPDie, dwarf::DW_AT_decl_file, std::nullopt, DefID);

      if (SP->getLine() != SPDecl->getLine())
        addUInt(SPDie, dwarf::DW_AT_decl_line, std::nullopt, SP->getLine());
    }
  }

  // Add function template parameters.
  addTemplateParams(SPDie, SP->getTemplateParams());

  // Add the linkage name if we have one and it isn't in the Decl.
  StringRef LinkageName = SP->getLinkageName();
  assert(((LinkageName.empty() || DeclLinkageName.empty()) ||
          LinkageName == DeclLinkageName) &&
         "decl has a linkage name and it is different");
  if (DeclLinkageName.empty() &&
      // Always emit it for abstract subprograms.
      (DD->useAllLinkageNames() || DU->getAbstractScopeDIEs().lookup(SP)))
    addLinkageName(SPDie, LinkageName);

  if (!DeclDie)
    return false;

  // Refer to the function declaration where all the other attributes will be
  // found.
  addDIEEntry(SPDie, dwarf::DW_AT_specification, *DeclDie);
  return true;
}

void DwarfUnit::applySubprogramAttributes(const DISubprogram *SP, DIE &SPDie,
                                          bool SkipSPAttributes) {
  // If -fdebug-info-for-profiling is enabled, need to emit the subprogram
  // and its source location.
  bool SkipSPSourceLocation = SkipSPAttributes &&
                              !CUNode->getDebugInfoForProfiling();
  if (!SkipSPSourceLocation)
    if (applySubprogramDefinitionAttributes(SP, SPDie, SkipSPAttributes))
      return;

  // Constructors and operators for anonymous aggregates do not have names.
  if (!SP->getName().empty())
    addString(SPDie, dwarf::DW_AT_name, SP->getName());

  addAnnotation(SPDie, SP->getAnnotations());

  if (!SkipSPSourceLocation)
    addSourceLine(SPDie, SP);

  // Skip the rest of the attributes under -gmlt to save space.
  if (SkipSPAttributes)
    return;

  // Add the prototype if we have a prototype and we have a C like
  // language.
  if (SP->isPrototyped() && dwarf::isC((dwarf::SourceLanguage)getLanguage()))
    addFlag(SPDie, dwarf::DW_AT_prototyped);

  if (SP->isObjCDirect())
    addFlag(SPDie, dwarf::DW_AT_APPLE_objc_direct);

  unsigned CC = 0;
  DITypeRefArray Args;
  if (const DISubroutineType *SPTy = SP->getType()) {
    Args = SPTy->getTypeArray();
    CC = SPTy->getCC();
  }

  // Add a DW_AT_calling_convention if this has an explicit convention.
  if (CC && CC != dwarf::DW_CC_normal)
    addUInt(SPDie, dwarf::DW_AT_calling_convention, dwarf::DW_FORM_data1, CC);

  // Add a return type. If this is a type like a C/C++ void type we don't add a
  // return type.
  if (Args.size())
    if (auto Ty = Args[0])
      addType(SPDie, Ty);

  unsigned VK = SP->getVirtuality();
  if (VK) {
    addUInt(SPDie, dwarf::DW_AT_virtuality, dwarf::DW_FORM_data1, VK);
    if (SP->getVirtualIndex() != -1u) {
      DIELoc *Block = getDIELoc();
      addUInt(*Block, dwarf::DW_FORM_data1, dwarf::DW_OP_constu);
      addUInt(*Block, dwarf::DW_FORM_udata, SP->getVirtualIndex());
      addBlock(SPDie, dwarf::DW_AT_vtable_elem_location, Block);
    }
    ContainingTypeMap.insert(std::make_pair(&SPDie, SP->getContainingType()));
  }

  if (!SP->isDefinition()) {
    addFlag(SPDie, dwarf::DW_AT_declaration);

    // Add arguments. Do not add arguments for subprogram definition. They will
    // be handled while processing variables.
    constructSubprogramArguments(SPDie, Args);
  }

  addThrownTypes(SPDie, SP->getThrownTypes());

  if (SP->isArtificial())
    addFlag(SPDie, dwarf::DW_AT_artificial);

  if (!SP->isLocalToUnit())
    addFlag(SPDie, dwarf::DW_AT_external);

  if (DD->useAppleExtensionAttributes()) {
    if (SP->isOptimized())
      addFlag(SPDie, dwarf::DW_AT_APPLE_optimized);

    if (unsigned isa = Asm->getISAEncoding())
      addUInt(SPDie, dwarf::DW_AT_APPLE_isa, dwarf::DW_FORM_flag, isa);
  }

  if (SP->isLValueReference())
    addFlag(SPDie, dwarf::DW_AT_reference);

  if (SP->isRValueReference())
    addFlag(SPDie, dwarf::DW_AT_rvalue_reference);

  if (SP->isNoReturn())
    addFlag(SPDie, dwarf::DW_AT_noreturn);

  addAccess(SPDie, SP->getFlags());

  if (SP->isExplicit())
    addFlag(SPDie, dwarf::DW_AT_explicit);

  if (SP->isMainSubprogram())
    addFlag(SPDie, dwarf::DW_AT_main_subprogram);
  if (SP->isPure())
    addFlag(SPDie, dwarf::DW_AT_pure);
  if (SP->isElemental())
    addFlag(SPDie, dwarf::DW_AT_elemental);
  if (SP->isRecursive())
    addFlag(SPDie, dwarf::DW_AT_recursive);

  if (!SP->getTargetFuncName().empty())
    addString(SPDie, dwarf::DW_AT_trampoline, SP->getTargetFuncName());

  if (DD->getDwarfVersion() >= 5 && SP->isDeleted())
    addFlag(SPDie, dwarf::DW_AT_deleted);
}

void DwarfUnit::constructSubrangeDIE(DIE &Buffer, const DISubrange *SR,
                                     DIE *IndexTy) {
  DIE &DW_Subrange = createAndAddDIE(dwarf::DW_TAG_subrange_type, Buffer);
  addDIEEntry(DW_Subrange, dwarf::DW_AT_type, *IndexTy);

  // The LowerBound value defines the lower bounds which is typically zero for
  // C/C++. The Count value is the number of elements.  Values are 64 bit. If
  // Count == -1 then the array is unbounded and we do not emit
  // DW_AT_lower_bound and DW_AT_count attributes.
  int64_t DefaultLowerBound = getDefaultLowerBound();

  auto AddBoundTypeEntry = [&](dwarf::Attribute Attr,
                               DISubrange::BoundType Bound) -> void {
    if (auto *BV = dyn_cast_if_present<DIVariable *>(Bound)) {
      if (auto *VarDIE = getDIE(BV))
        addDIEEntry(DW_Subrange, Attr, *VarDIE);
    } else if (auto *BE = dyn_cast_if_present<DIExpression *>(Bound)) {
      DIELoc *Loc = new (DIEValueAllocator) DIELoc;
      DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
      DwarfExpr.setMemoryLocationKind();
      DwarfExpr.addExpression(BE);
      addBlock(DW_Subrange, Attr, DwarfExpr.finalize());
    } else if (auto *BI = dyn_cast_if_present<ConstantInt *>(Bound)) {
      if (Attr == dwarf::DW_AT_count) {
        if (BI->getSExtValue() != -1)
          addUInt(DW_Subrange, Attr, std::nullopt, BI->getSExtValue());
      } else if (Attr != dwarf::DW_AT_lower_bound || DefaultLowerBound == -1 ||
                 BI->getSExtValue() != DefaultLowerBound)
        addSInt(DW_Subrange, Attr, dwarf::DW_FORM_sdata, BI->getSExtValue());
    }
  };

  AddBoundTypeEntry(dwarf::DW_AT_lower_bound, SR->getLowerBound());

  AddBoundTypeEntry(dwarf::DW_AT_count, SR->getCount());

  AddBoundTypeEntry(dwarf::DW_AT_upper_bound, SR->getUpperBound());

  AddBoundTypeEntry(dwarf::DW_AT_byte_stride, SR->getStride());
}

void DwarfUnit::constructGenericSubrangeDIE(DIE &Buffer,
                                            const DIGenericSubrange *GSR,
                                            DIE *IndexTy) {
  DIE &DwGenericSubrange =
      createAndAddDIE(dwarf::DW_TAG_generic_subrange, Buffer);
  addDIEEntry(DwGenericSubrange, dwarf::DW_AT_type, *IndexTy);

  int64_t DefaultLowerBound = getDefaultLowerBound();

  auto AddBoundTypeEntry = [&](dwarf::Attribute Attr,
                               DIGenericSubrange::BoundType Bound) -> void {
    if (auto *BV = dyn_cast_if_present<DIVariable *>(Bound)) {
      if (auto *VarDIE = getDIE(BV))
        addDIEEntry(DwGenericSubrange, Attr, *VarDIE);
    } else if (auto *BE = dyn_cast_if_present<DIExpression *>(Bound)) {
      if (BE->isConstant() &&
          DIExpression::SignedOrUnsignedConstant::SignedConstant ==
              *BE->isConstant()) {
        if (Attr != dwarf::DW_AT_lower_bound || DefaultLowerBound == -1 ||
            static_cast<int64_t>(BE->getElement(1)) != DefaultLowerBound)
          addSInt(DwGenericSubrange, Attr, dwarf::DW_FORM_sdata,
                  BE->getElement(1));
      } else {
        DIELoc *Loc = new (DIEValueAllocator) DIELoc;
        DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
        DwarfExpr.setMemoryLocationKind();
        DwarfExpr.addExpression(BE);
        addBlock(DwGenericSubrange, Attr, DwarfExpr.finalize());
      }
    }
  };

  AddBoundTypeEntry(dwarf::DW_AT_lower_bound, GSR->getLowerBound());
  AddBoundTypeEntry(dwarf::DW_AT_count, GSR->getCount());
  AddBoundTypeEntry(dwarf::DW_AT_upper_bound, GSR->getUpperBound());
  AddBoundTypeEntry(dwarf::DW_AT_byte_stride, GSR->getStride());
}

DIE *DwarfUnit::getIndexTyDie() {
  if (IndexTyDie)
    return IndexTyDie;
  // Construct an integer type to use for indexes.
  IndexTyDie = &createAndAddDIE(dwarf::DW_TAG_base_type, getUnitDie());
  StringRef Name = "__ARRAY_SIZE_TYPE__";
  addString(*IndexTyDie, dwarf::DW_AT_name, Name);
  addUInt(*IndexTyDie, dwarf::DW_AT_byte_size, std::nullopt, sizeof(int64_t));
  addUInt(*IndexTyDie, dwarf::DW_AT_encoding, dwarf::DW_FORM_data1,
          dwarf::getArrayIndexTypeEncoding(
              (dwarf::SourceLanguage)getLanguage()));
  DD->addAccelType(*this, CUNode->getNameTableKind(), Name, *IndexTyDie,
                   /*Flags*/ 0);
  return IndexTyDie;
}

/// Returns true if the vector's size differs from the sum of sizes of elements
/// the user specified.  This can occur if the vector has been rounded up to
/// fit memory alignment constraints.
static bool hasVectorBeenPadded(const DICompositeType *CTy) {
  assert(CTy && CTy->isVector() && "Composite type is not a vector");
  const uint64_t ActualSize = CTy->getSizeInBits();

  // Obtain the size of each element in the vector.
  DIType *BaseTy = CTy->getBaseType();
  assert(BaseTy && "Unknown vector element type.");
  const uint64_t ElementSize = BaseTy->getSizeInBits();

  // Locate the number of elements in the vector.
  const DINodeArray Elements = CTy->getElements();
  assert(Elements.size() == 1 &&
         Elements[0]->getTag() == dwarf::DW_TAG_subrange_type &&
         "Invalid vector element array, expected one element of type subrange");
  const auto Subrange = cast<DISubrange>(Elements[0]);
  const auto NumVecElements =
      Subrange->getCount()
          ? cast<ConstantInt *>(Subrange->getCount())->getSExtValue()
          : 0;

  // Ensure we found the element count and that the actual size is wide
  // enough to contain the requested size.
  assert(ActualSize >= (NumVecElements * ElementSize) && "Invalid vector size");
  return ActualSize != (NumVecElements * ElementSize);
}

void DwarfUnit::constructArrayTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  if (CTy->isVector()) {
    addFlag(Buffer, dwarf::DW_AT_GNU_vector);
    if (hasVectorBeenPadded(CTy))
      addUInt(Buffer, dwarf::DW_AT_byte_size, std::nullopt,
              CTy->getSizeInBits() / CHAR_BIT);
  }

  if (DIVariable *Var = CTy->getDataLocation()) {
    if (auto *VarDIE = getDIE(Var))
      addDIEEntry(Buffer, dwarf::DW_AT_data_location, *VarDIE);
  } else if (DIExpression *Expr = CTy->getDataLocationExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(Expr);
    addBlock(Buffer, dwarf::DW_AT_data_location, DwarfExpr.finalize());
  }

  if (DIVariable *Var = CTy->getAssociated()) {
    if (auto *VarDIE = getDIE(Var))
      addDIEEntry(Buffer, dwarf::DW_AT_associated, *VarDIE);
  } else if (DIExpression *Expr = CTy->getAssociatedExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(Expr);
    addBlock(Buffer, dwarf::DW_AT_associated, DwarfExpr.finalize());
  }

  if (DIVariable *Var = CTy->getAllocated()) {
    if (auto *VarDIE = getDIE(Var))
      addDIEEntry(Buffer, dwarf::DW_AT_allocated, *VarDIE);
  } else if (DIExpression *Expr = CTy->getAllocatedExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(Expr);
    addBlock(Buffer, dwarf::DW_AT_allocated, DwarfExpr.finalize());
  }

  if (auto *RankConst = CTy->getRankConst()) {
    addSInt(Buffer, dwarf::DW_AT_rank, dwarf::DW_FORM_sdata,
            RankConst->getSExtValue());
  } else if (auto *RankExpr = CTy->getRankExp()) {
    DIELoc *Loc = new (DIEValueAllocator) DIELoc;
    DIEDwarfExpression DwarfExpr(*Asm, getCU(), *Loc);
    DwarfExpr.setMemoryLocationKind();
    DwarfExpr.addExpression(RankExpr);
    addBlock(Buffer, dwarf::DW_AT_rank, DwarfExpr.finalize());
  }

  // Emit the element type.
  addType(Buffer, CTy->getBaseType());

  // Get an anonymous type for index type.
  // FIXME: This type should be passed down from the front end
  // as different languages may have different sizes for indexes.
  DIE *IdxTy = getIndexTyDie();

  // Add subranges to array type.
  DINodeArray Elements = CTy->getElements();
  for (DINode *E : Elements) {
    // FIXME: Should this really be such a loose cast?
    if (auto *Element = dyn_cast_or_null<DINode>(E)) {
      if (Element->getTag() == dwarf::DW_TAG_subrange_type)
        constructSubrangeDIE(Buffer, cast<DISubrange>(Element), IdxTy);
      else if (Element->getTag() == dwarf::DW_TAG_generic_subrange)
        constructGenericSubrangeDIE(Buffer, cast<DIGenericSubrange>(Element),
                                    IdxTy);
    }
  }
}

void DwarfUnit::constructEnumTypeDIE(DIE &Buffer, const DICompositeType *CTy) {
  const DIType *DTy = CTy->getBaseType();
  bool IsUnsigned = DTy && DD->isUnsignedDIType(DTy);
  if (DTy) {
    if (!Asm->TM.Options.DebugStrictDwarf || DD->getDwarfVersion() >= 3)
      addType(Buffer, DTy);
    if (DD->getDwarfVersion() >= 4 && (CTy->getFlags() & DINode::FlagEnumClass))
      addFlag(Buffer, dwarf::DW_AT_enum_class);
  }

  auto *Context = CTy->getScope();
  bool IndexEnumerators = !Context || isa<DICompileUnit>(Context) || isa<DIFile>(Context) ||
      isa<DINamespace>(Context) || isa<DICommonBlock>(Context);
  DINodeArray Elements = CTy->getElements();

  // Add enumerators to enumeration type.
  for (const DINode *E : Elements) {
    auto *Enum = dyn_cast_or_null<DIEnumerator>(E);
    if (Enum) {
      DIE &Enumerator = createAndAddDIE(dwarf::DW_TAG_enumerator, Buffer);
      StringRef Name = Enum->getName();
      addString(Enumerator, dwarf::DW_AT_name, Name);
      addConstantValue(Enumerator, Enum->getValue(), IsUnsigned);
      if (IndexEnumerators)
        addGlobalName(Name, Enumerator, Context);
    }
  }
}

void DwarfUnit::constructContainingTypeDIEs() {
  for (auto &P : ContainingTypeMap) {
    DIE &SPDie = *P.first;
    const DINode *D = P.second;
    if (!D)
      continue;
    DIE *NDie = getDIE(D);
    if (!NDie)
      continue;
    addDIEEntry(SPDie, dwarf::DW_AT_containing_type, *NDie);
  }
}

DIE &DwarfUnit::constructMemberDIE(DIE &Buffer, const DIDerivedType *DT) {
  DIE &MemberDie = createAndAddDIE(DT->getTag(), Buffer);
  StringRef Name = DT->getName();
  if (!Name.empty())
    addString(MemberDie, dwarf::DW_AT_name, Name);

  addAnnotation(MemberDie, DT->getAnnotations());

  if (DIType *Resolved = DT->getBaseType())
    addType(MemberDie, Resolved);

  addSourceLine(MemberDie, DT);

  if (DT->getTag() == dwarf::DW_TAG_inheritance && DT->isVirtual()) {

    // For C++, virtual base classes are not at fixed offset. Use following
    // expression to extract appropriate offset from vtable.
    // BaseAddr = ObAddr + *((*ObAddr) - Offset)

    DIELoc *VBaseLocationDie = new (DIEValueAllocator) DIELoc;
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_dup);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_constu);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_udata, DT->getOffsetInBits());
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_minus);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_deref);
    addUInt(*VBaseLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_plus);

    addBlock(MemberDie, dwarf::DW_AT_data_member_location, VBaseLocationDie);
  } else {
    uint64_t Size = DT->getSizeInBits();
    uint64_t FieldSize = DD->getBaseTypeSize(DT);
    uint32_t AlignInBytes = DT->getAlignInBytes();
    uint64_t OffsetInBytes;

    bool IsBitfield = DT->isBitField();
    if (IsBitfield) {
      // Handle bitfield, assume bytes are 8 bits.
      if (DD->useDWARF2Bitfields())
        addUInt(MemberDie, dwarf::DW_AT_byte_size, std::nullopt, FieldSize / 8);
      addUInt(MemberDie, dwarf::DW_AT_bit_size, std::nullopt, Size);

      assert(DT->getOffsetInBits() <=
             (uint64_t)std::numeric_limits<int64_t>::max());
      int64_t Offset = DT->getOffsetInBits();
      // We can't use DT->getAlignInBits() here: AlignInBits for member type
      // is non-zero if and only if alignment was forced (e.g. _Alignas()),
      // which can't be done with bitfields. Thus we use FieldSize here.
      uint32_t AlignInBits = FieldSize;
      uint32_t AlignMask = ~(AlignInBits - 1);
      // The bits from the start of the storage unit to the start of the field.
      uint64_t StartBitOffset = Offset - (Offset & AlignMask);
      // The byte offset of the field's aligned storage unit inside the struct.
      OffsetInBytes = (Offset - StartBitOffset) / 8;

      if (DD->useDWARF2Bitfields()) {
        uint64_t HiMark = (Offset + FieldSize) & AlignMask;
        uint64_t FieldOffset = (HiMark - FieldSize);
        Offset -= FieldOffset;

        // Maybe we need to work from the other end.
        if (Asm->getDataLayout().isLittleEndian())
          Offset = FieldSize - (Offset + Size);

        if (Offset < 0)
          addSInt(MemberDie, dwarf::DW_AT_bit_offset, dwarf::DW_FORM_sdata,
                  Offset);
        else
          addUInt(MemberDie, dwarf::DW_AT_bit_offset, std::nullopt,
                  (uint64_t)Offset);
        OffsetInBytes = FieldOffset >> 3;
      } else {
        addUInt(MemberDie, dwarf::DW_AT_data_bit_offset, std::nullopt, Offset);
      }
    } else {
      // This is not a bitfield.
      OffsetInBytes = DT->getOffsetInBits() / 8;
      if (AlignInBytes)
        addUInt(MemberDie, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
                AlignInBytes);
    }

    if (DD->getDwarfVersion() <= 2) {
      DIELoc *MemLocationDie = new (DIEValueAllocator) DIELoc;
      addUInt(*MemLocationDie, dwarf::DW_FORM_data1, dwarf::DW_OP_plus_uconst);
      addUInt(*MemLocationDie, dwarf::DW_FORM_udata, OffsetInBytes);
      addBlock(MemberDie, dwarf::DW_AT_data_member_location, MemLocationDie);
    } else if (!IsBitfield || DD->useDWARF2Bitfields()) {
      // In DWARF v3, DW_FORM_data4/8 in DW_AT_data_member_location are
      // interpreted as location-list pointers. Interpreting constants as
      // pointers is not expected, so we use DW_FORM_udata to encode the
      // constants here.
      if (DD->getDwarfVersion() == 3)
        addUInt(MemberDie, dwarf::DW_AT_data_member_location,
                dwarf::DW_FORM_udata, OffsetInBytes);
      else
        addUInt(MemberDie, dwarf::DW_AT_data_member_location, std::nullopt,
                OffsetInBytes);
    }
  }

  addAccess(MemberDie, DT->getFlags());

  if (DT->isVirtual())
    addUInt(MemberDie, dwarf::DW_AT_virtuality, dwarf::DW_FORM_data1,
            dwarf::DW_VIRTUALITY_virtual);

  // Objective-C properties.
  if (DINode *PNode = DT->getObjCProperty())
    if (DIE *PDie = getDIE(PNode))
      addAttribute(MemberDie, dwarf::DW_AT_APPLE_property,
                   dwarf::DW_FORM_ref4, DIEEntry(*PDie));

  if (DT->isArtificial())
    addFlag(MemberDie, dwarf::DW_AT_artificial);

  return MemberDie;
}

DIE *DwarfUnit::getOrCreateStaticMemberDIE(const DIDerivedType *DT) {
  if (!DT)
    return nullptr;

  // Construct the context before querying for the existence of the DIE in case
  // such construction creates the DIE.
  DIE *ContextDIE = getOrCreateContextDIE(DT->getScope());
  assert(dwarf::isType(ContextDIE->getTag()) &&
         "Static member should belong to a type.");

  if (DIE *StaticMemberDIE = getDIE(DT))
    return StaticMemberDIE;

  DIE &StaticMemberDIE = createAndAddDIE(DT->getTag(), *ContextDIE, DT);

  const DIType *Ty = DT->getBaseType();

  addString(StaticMemberDIE, dwarf::DW_AT_name, DT->getName());
  addType(StaticMemberDIE, Ty);
  addSourceLine(StaticMemberDIE, DT);
  addFlag(StaticMemberDIE, dwarf::DW_AT_external);
  addFlag(StaticMemberDIE, dwarf::DW_AT_declaration);

  // FIXME: We could omit private if the parent is a class_type, and
  // public if the parent is something else.
  addAccess(StaticMemberDIE, DT->getFlags());

  if (const ConstantInt *CI = dyn_cast_or_null<ConstantInt>(DT->getConstant()))
    addConstantValue(StaticMemberDIE, CI, Ty);
  if (const ConstantFP *CFP = dyn_cast_or_null<ConstantFP>(DT->getConstant()))
    addConstantFPValue(StaticMemberDIE, CFP);

  if (uint32_t AlignInBytes = DT->getAlignInBytes())
    addUInt(StaticMemberDIE, dwarf::DW_AT_alignment, dwarf::DW_FORM_udata,
            AlignInBytes);

  return &StaticMemberDIE;
}

void DwarfUnit::emitCommonHeader(bool UseOffsets, dwarf::UnitType UT) {
  // Emit size of content not including length itself
  if (!DD->useSectionsAsReferences())
    EndLabel = Asm->emitDwarfUnitLength(
        isDwoUnit() ? "debug_info_dwo" : "debug_info", "Length of Unit");
  else
    Asm->emitDwarfUnitLength(getHeaderSize() + getUnitDie().getSize(),
                             "Length of Unit");

  Asm->OutStreamer->AddComment("DWARF version number");
  unsigned Version = DD->getDwarfVersion();
  Asm->emitInt16(Version);

  // DWARF v5 reorders the address size and adds a unit type.
  if (Version >= 5) {
    Asm->OutStreamer->AddComment("DWARF Unit Type");
    Asm->emitInt8(UT);
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->emitInt8(Asm->MAI->getCodePointerSize());
  }

  // We share one abbreviations table across all units so it's always at the
  // start of the section. Use a relocatable offset where needed to ensure
  // linking doesn't invalidate that offset.
  Asm->OutStreamer->AddComment("Offset Into Abbrev. Section");
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  if (UseOffsets)
    Asm->emitDwarfLengthOrOffset(0);
  else
    Asm->emitDwarfSymbolReference(
        TLOF.getDwarfAbbrevSection()->getBeginSymbol(), false);

  if (Version <= 4) {
    Asm->OutStreamer->AddComment("Address Size (in bytes)");
    Asm->emitInt8(Asm->MAI->getCodePointerSize());
  }
}

void DwarfTypeUnit::emitHeader(bool UseOffsets) {
  if (!DD->useSplitDwarf()) {
    LabelBegin = Asm->createTempSymbol("tu_begin");
    Asm->OutStreamer->emitLabel(LabelBegin);
  }
  DwarfUnit::emitCommonHeader(UseOffsets,
                              DD->useSplitDwarf() ? dwarf::DW_UT_split_type
                                                  : dwarf::DW_UT_type);
  Asm->OutStreamer->AddComment("Type Signature");
  Asm->OutStreamer->emitIntValue(TypeSignature, sizeof(TypeSignature));
  Asm->OutStreamer->AddComment("Type DIE Offset");
  // In a skeleton type unit there is no type DIE so emit a zero offset.
  Asm->emitDwarfLengthOrOffset(Ty ? Ty->getOffset() : 0);
}

void DwarfUnit::addSectionDelta(DIE &Die, dwarf::Attribute Attribute,
                                const MCSymbol *Hi, const MCSymbol *Lo) {
  addAttribute(Die, Attribute, DD->getDwarfSectionOffsetForm(),
               new (DIEValueAllocator) DIEDelta(Hi, Lo));
}

void DwarfUnit::addSectionLabel(DIE &Die, dwarf::Attribute Attribute,
                                const MCSymbol *Label, const MCSymbol *Sec) {
  if (Asm->doesDwarfUseRelocationsAcrossSections())
    addLabel(Die, Attribute, DD->getDwarfSectionOffsetForm(), Label);
  else
    addSectionDelta(Die, Attribute, Label, Sec);
}

bool DwarfTypeUnit::isDwoUnit() const {
  // Since there are no skeleton type units, all type units are dwo type units
  // when split DWARF is being used.
  return DD->useSplitDwarf();
}

void DwarfTypeUnit::addGlobalName(StringRef Name, const DIE &Die,
                                  const DIScope *Context) {
  getCU().addGlobalNameForTypeUnit(Name, Context);
}

void DwarfTypeUnit::addGlobalTypeImpl(const DIType *Ty, const DIE &Die,
                                      const DIScope *Context) {
  getCU().addGlobalTypeUnitType(Ty, Context);
}

const MCSymbol *DwarfUnit::getCrossSectionRelativeBaseAddress() const {
  if (!Asm->doesDwarfUseRelocationsAcrossSections())
    return nullptr;
  if (isDwoUnit())
    return nullptr;
  return getSection()->getBeginSymbol();
}

void DwarfUnit::addStringOffsetsStart() {
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  addSectionLabel(getUnitDie(), dwarf::DW_AT_str_offsets_base,
                  DU->getStringOffsetsStartSym(),
                  TLOF.getDwarfStrOffSection()->getBeginSymbol());
}

void DwarfUnit::addRnglistsBase() {
  assert(DD->getDwarfVersion() >= 5 &&
         "DW_AT_rnglists_base requires DWARF version 5 or later");
  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  addSectionLabel(getUnitDie(), dwarf::DW_AT_rnglists_base,
                  DU->getRnglistsTableBaseSym(),
                  TLOF.getDwarfRnglistsSection()->getBeginSymbol());
}

void DwarfTypeUnit::finishNonUnitTypeDIE(DIE& D, const DICompositeType *CTy) {
  DD->getAddressPool().resetUsedFlag(true);
}

bool DwarfUnit::isCompatibleWithVersion(uint16_t Version) const {
  return !Asm->TM.Options.DebugStrictDwarf || DD->getDwarfVersion() >= Version;
}
