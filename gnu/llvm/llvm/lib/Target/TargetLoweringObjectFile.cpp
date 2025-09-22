//===-- llvm/Target/TargetLoweringObjectFile.cpp - Object File Info -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements classes used to handle lowerings specific to common
// object file formats.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

//===----------------------------------------------------------------------===//
//                              Generic Code
//===----------------------------------------------------------------------===//

/// Initialize - this method must be called before any actual lowering is
/// done.  This specifies the current context for codegen, and gives the
/// lowering implementations a chance to set up their default sections.
void TargetLoweringObjectFile::Initialize(MCContext &ctx,
                                          const TargetMachine &TM) {
  // `Initialize` can be called more than once.
  delete Mang;
  Mang = new Mangler();
  initMCObjectFileInfo(ctx, TM.isPositionIndependent(),
                       TM.getCodeModel() == CodeModel::Large);

  // Reset various EH DWARF encodings.
  PersonalityEncoding = LSDAEncoding = TTypeEncoding = dwarf::DW_EH_PE_absptr;
  CallSiteEncoding = dwarf::DW_EH_PE_uleb128;

  this->TM = &TM;
}

TargetLoweringObjectFile::~TargetLoweringObjectFile() {
  delete Mang;
}

unsigned TargetLoweringObjectFile::getCallSiteEncoding() const {
  // If target does not have LEB128 directives, we would need the
  // call site encoding to be udata4 so that the alternative path
  // for not having LEB128 directives could work.
  if (!getContext().getAsmInfo()->hasLEB128Directives())
    return dwarf::DW_EH_PE_udata4;
  return CallSiteEncoding;
}

static bool isNullOrUndef(const Constant *C) {
  // Check that the constant isn't all zeros or undefs.
  if (C->isNullValue() || isa<UndefValue>(C))
    return true;
  if (!isa<ConstantAggregate>(C))
    return false;
  for (const auto *Operand : C->operand_values()) {
    if (!isNullOrUndef(cast<Constant>(Operand)))
      return false;
  }
  return true;
}

static bool isSuitableForBSS(const GlobalVariable *GV) {
  const Constant *C = GV->getInitializer();

  // Must have zero initializer.
  if (!isNullOrUndef(C))
    return false;

  // Leave constant zeros in readonly constant sections, so they can be shared.
  if (GV->isConstant())
    return false;

  // If the global has an explicit section specified, don't put it in BSS.
  if (GV->hasSection())
    return false;

  // Otherwise, put it in BSS!
  return true;
}

/// IsNullTerminatedString - Return true if the specified constant (which is
/// known to have a type that is an array of 1/2/4 byte elements) ends with a
/// nul value and contains no other nuls in it.  Note that this is more general
/// than ConstantDataSequential::isString because we allow 2 & 4 byte strings.
static bool IsNullTerminatedString(const Constant *C) {
  // First check: is we have constant array terminated with zero
  if (const ConstantDataSequential *CDS = dyn_cast<ConstantDataSequential>(C)) {
    unsigned NumElts = CDS->getNumElements();
    assert(NumElts != 0 && "Can't have an empty CDS");

    if (CDS->getElementAsInteger(NumElts-1) != 0)
      return false; // Not null terminated.

    // Verify that the null doesn't occur anywhere else in the string.
    for (unsigned i = 0; i != NumElts-1; ++i)
      if (CDS->getElementAsInteger(i) == 0)
        return false;
    return true;
  }

  // Another possibility: [1 x i8] zeroinitializer
  if (isa<ConstantAggregateZero>(C))
    return cast<ArrayType>(C->getType())->getNumElements() == 1;

  return false;
}

MCSymbol *TargetLoweringObjectFile::getSymbolWithGlobalValueBase(
    const GlobalValue *GV, StringRef Suffix, const TargetMachine &TM) const {
  assert(!Suffix.empty());

  SmallString<60> NameStr;
  NameStr += GV->getDataLayout().getPrivateGlobalPrefix();
  TM.getNameWithPrefix(NameStr, GV, *Mang);
  NameStr.append(Suffix.begin(), Suffix.end());
  return getContext().getOrCreateSymbol(NameStr);
}

MCSymbol *TargetLoweringObjectFile::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  return TM.getSymbol(GV);
}

void TargetLoweringObjectFile::emitPersonalityValue(MCStreamer &Streamer,
                                                    const DataLayout &,
                                                    const MCSymbol *Sym) const {
}

void TargetLoweringObjectFile::emitCGProfileMetadata(MCStreamer &Streamer,
                                                     Module &M) const {
  MCContext &C = getContext();
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);

  MDNode *CFGProfile = nullptr;

  for (const auto &MFE : ModuleFlags) {
    StringRef Key = MFE.Key->getString();
    if (Key == "CG Profile") {
      CFGProfile = cast<MDNode>(MFE.Val);
      break;
    }
  }

  if (!CFGProfile)
    return;

  auto GetSym = [this](const MDOperand &MDO) -> MCSymbol * {
    if (!MDO)
      return nullptr;
    auto *V = cast<ValueAsMetadata>(MDO);
    const Function *F = cast<Function>(V->getValue()->stripPointerCasts());
    if (F->hasDLLImportStorageClass())
      return nullptr;
    return TM->getSymbol(F);
  };

  for (const auto &Edge : CFGProfile->operands()) {
    MDNode *E = cast<MDNode>(Edge);
    const MCSymbol *From = GetSym(E->getOperand(0));
    const MCSymbol *To = GetSym(E->getOperand(1));
    // Skip null functions. This can happen if functions are dead stripped after
    // the CGProfile pass has been run.
    if (!From || !To)
      continue;
    uint64_t Count = cast<ConstantAsMetadata>(E->getOperand(2))
                         ->getValue()
                         ->getUniqueInteger()
                         .getZExtValue();
    Streamer.emitCGProfileEntry(
        MCSymbolRefExpr::create(From, MCSymbolRefExpr::VK_None, C),
        MCSymbolRefExpr::create(To, MCSymbolRefExpr::VK_None, C), Count);
  }
}

/// getKindForGlobal - This is a top-level target-independent classifier for
/// a global object.  Given a global variable and information from the TM, this
/// function classifies the global in a target independent manner. This function
/// may be overridden by the target implementation.
SectionKind TargetLoweringObjectFile::getKindForGlobal(const GlobalObject *GO,
                                                       const TargetMachine &TM){
  assert(!GO->isDeclarationForLinker() &&
         "Can only be used for global definitions");

  // Functions are classified as text sections.
  if (isa<Function>(GO))
    return SectionKind::getText();

  // Basic blocks are classified as text sections.
  if (isa<BasicBlock>(GO))
    return SectionKind::getText();

  // Global variables require more detailed analysis.
  const auto *GVar = cast<GlobalVariable>(GO);

  // Handle thread-local data first.
  if (GVar->isThreadLocal()) {
    if (isSuitableForBSS(GVar) && !TM.Options.NoZerosInBSS) {
      // Zero-initialized TLS variables with local linkage always get classified
      // as ThreadBSSLocal.
      if (GVar->hasLocalLinkage()) {
        return SectionKind::getThreadBSSLocal();
      }
      return SectionKind::getThreadBSS();
    }
    return SectionKind::getThreadData();
  }

  // Variables with common linkage always get classified as common.
  if (GVar->hasCommonLinkage())
    return SectionKind::getCommon();

  // Most non-mergeable zero data can be put in the BSS section unless otherwise
  // specified.
  if (isSuitableForBSS(GVar) && !TM.Options.NoZerosInBSS) {
    if (GVar->hasLocalLinkage())
      return SectionKind::getBSSLocal();
    else if (GVar->hasExternalLinkage())
      return SectionKind::getBSSExtern();
    return SectionKind::getBSS();
  }

  // Global variables with '!exclude' should get the exclude section kind if
  // they have an explicit section and no other metadata.
  if (GVar->hasSection())
    if (MDNode *MD = GVar->getMetadata(LLVMContext::MD_exclude))
      if (!MD->getNumOperands())
        return SectionKind::getExclude();

  // If the global is marked constant, we can put it into a mergable section,
  // a mergable string section, or general .data if it contains relocations.
  if (GVar->isConstant()) {
    // If the initializer for the global contains something that requires a
    // relocation, then we may have to drop this into a writable data section
    // even though it is marked const.
    const Constant *C = GVar->getInitializer();
    if (!C->needsRelocation()) {
      // If the global is required to have a unique address, it can't be put
      // into a mergable section: just drop it into the general read-only
      // section instead.
      if (!GVar->hasGlobalUnnamedAddr())
        return SectionKind::getReadOnly();

      // If initializer is a null-terminated string, put it in a "cstring"
      // section of the right width.
      if (ArrayType *ATy = dyn_cast<ArrayType>(C->getType())) {
        if (IntegerType *ITy =
              dyn_cast<IntegerType>(ATy->getElementType())) {
          if ((ITy->getBitWidth() == 8 || ITy->getBitWidth() == 16 ||
               ITy->getBitWidth() == 32) &&
              IsNullTerminatedString(C)) {
            if (ITy->getBitWidth() == 8)
              return SectionKind::getMergeable1ByteCString();
            if (ITy->getBitWidth() == 16)
              return SectionKind::getMergeable2ByteCString();

            assert(ITy->getBitWidth() == 32 && "Unknown width");
            return SectionKind::getMergeable4ByteCString();
          }
        }
      }

      // Otherwise, just drop it into a mergable constant section.  If we have
      // a section for this size, use it, otherwise use the arbitrary sized
      // mergable section.
      switch (
          GVar->getDataLayout().getTypeAllocSize(C->getType())) {
      case 4:  return SectionKind::getMergeableConst4();
      case 8:  return SectionKind::getMergeableConst8();
      case 16: return SectionKind::getMergeableConst16();
      case 32: return SectionKind::getMergeableConst32();
      default:
        return SectionKind::getReadOnly();
      }

    } else {
      // In static, ROPI and RWPI relocation models, the linker will resolve
      // all addresses, so the relocation entries will actually be constants by
      // the time the app starts up.  However, we can't put this into a
      // mergable section, because the linker doesn't take relocations into
      // consideration when it tries to merge entries in the section.
      Reloc::Model ReloModel = TM.getRelocationModel();
      if (ReloModel == Reloc::Static || ReloModel == Reloc::ROPI ||
          ReloModel == Reloc::RWPI || ReloModel == Reloc::ROPI_RWPI ||
          !C->needsDynamicRelocation())
        return SectionKind::getReadOnly();

      // Otherwise, the dynamic linker needs to fix it up, put it in the
      // writable data.rel section.
      return SectionKind::getReadOnlyWithRel();
    }
  }

  // Okay, this isn't a constant.
  return SectionKind::getData();
}

/// This method computes the appropriate section to emit the specified global
/// variable or function definition.  This should not be passed external (or
/// available externally) globals.
MCSection *TargetLoweringObjectFile::SectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Select section name.
  if (GO->hasSection())
    return getExplicitSectionGlobal(GO, Kind, TM);

  if (auto *GVar = dyn_cast<GlobalVariable>(GO)) {
    auto Attrs = GVar->getAttributes();
    if ((Attrs.hasAttribute("bss-section") && Kind.isBSS()) ||
        (Attrs.hasAttribute("data-section") && Kind.isData()) ||
        (Attrs.hasAttribute("relro-section") && Kind.isReadOnlyWithRel()) ||
        (Attrs.hasAttribute("rodata-section") && Kind.isReadOnly()))  {
       return getExplicitSectionGlobal(GO, Kind, TM);
    }
  }

  // Use default section depending on the 'type' of global
  return SelectSectionForGlobal(GO, Kind, TM);
}

/// This method computes the appropriate section to emit the specified global
/// variable or function definition. This should not be passed external (or
/// available externally) globals.
MCSection *
TargetLoweringObjectFile::SectionForGlobal(const GlobalObject *GO,
                                           const TargetMachine &TM) const {
  return SectionForGlobal(GO, getKindForGlobal(GO, TM), TM);
}

MCSection *TargetLoweringObjectFile::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  Align Alignment(1);
  return getSectionForConstant(F.getDataLayout(),
                               SectionKind::getReadOnly(), /*C=*/nullptr,
                               Alignment);
}

bool TargetLoweringObjectFile::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  // In PIC mode, we need to emit the jump table to the same section as the
  // function body itself, otherwise the label differences won't make sense.
  // FIXME: Need a better predicate for this: what about custom entries?
  if (UsesLabelDifference)
    return true;

  // We should also do if the section name is NULL or function is declared
  // in discardable section
  // FIXME: this isn't the right predicate, should be based on the MCSection
  // for the function.
  return F.isWeakForLinker();
}

/// Given a mergable constant with the specified size and relocation
/// information, return a section that it should be placed in.
MCSection *TargetLoweringObjectFile::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  if (Kind.isReadOnly() && ReadOnlySection != nullptr)
    return ReadOnlySection;

  return DataSection;
}

MCSection *TargetLoweringObjectFile::getSectionForMachineBasicBlock(
    const Function &F, const MachineBasicBlock &MBB,
    const TargetMachine &TM) const {
  return nullptr;
}

MCSection *TargetLoweringObjectFile::getUniqueSectionForFunction(
    const Function &F, const TargetMachine &TM) const {
  return nullptr;
}

/// getTTypeGlobalReference - Return an MCExpr to use for a
/// reference to the specified global variable from exception
/// handling information.
const MCExpr *TargetLoweringObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  const MCSymbolRefExpr *Ref =
      MCSymbolRefExpr::create(TM.getSymbol(GV), getContext());

  return getTTypeReference(Ref, Encoding, Streamer);
}

const MCExpr *TargetLoweringObjectFile::
getTTypeReference(const MCSymbolRefExpr *Sym, unsigned Encoding,
                  MCStreamer &Streamer) const {
  switch (Encoding & 0x70) {
  default:
    report_fatal_error("We do not support this DWARF encoding yet!");
  case dwarf::DW_EH_PE_absptr:
    // Do nothing special
    return Sym;
  case dwarf::DW_EH_PE_pcrel: {
    // Emit a label to the streamer for the current position.  This gives us
    // .-foo addressing.
    MCSymbol *PCSym = getContext().createTempSymbol();
    Streamer.emitLabel(PCSym);
    const MCExpr *PC = MCSymbolRefExpr::create(PCSym, getContext());
    return MCBinaryExpr::createSub(Sym, PC, getContext());
  }
  }
}

const MCExpr *TargetLoweringObjectFile::getDebugThreadLocalSymbol(const MCSymbol *Sym) const {
  // FIXME: It's not clear what, if any, default this should have - perhaps a
  // null return could mean 'no location' & we should just do that here.
  return MCSymbolRefExpr::create(Sym, getContext());
}

void TargetLoweringObjectFile::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  Mang->getNameWithPrefix(OutName, GV, /*CannotUsePrivateLabel=*/false);
}
