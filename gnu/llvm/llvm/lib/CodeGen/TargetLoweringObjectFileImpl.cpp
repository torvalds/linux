//===- llvm/CodeGen/TargetLoweringObjectFileImpl.cpp - Object File Info ---===//
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

#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/CodeGen/BasicBlockSectionUtils.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineModuleInfoImpls.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalObject.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Mangler.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PseudoProbe.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAsmInfoDarwin.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionGOFF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/ProfileData/InstrProf.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <string>

using namespace llvm;
using namespace dwarf;

static cl::opt<bool> JumpTableInFunctionSection(
    "jumptable-in-function-section", cl::Hidden, cl::init(false),
    cl::desc("Putting Jump Table in function section"));

static void GetObjCImageInfo(Module &M, unsigned &Version, unsigned &Flags,
                             StringRef &Section) {
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);

  for (const auto &MFE: ModuleFlags) {
    // Ignore flags with 'Require' behaviour.
    if (MFE.Behavior == Module::Require)
      continue;

    StringRef Key = MFE.Key->getString();
    if (Key == "Objective-C Image Info Version") {
      Version = mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue();
    } else if (Key == "Objective-C Garbage Collection" ||
               Key == "Objective-C GC Only" ||
               Key == "Objective-C Is Simulated" ||
               Key == "Objective-C Class Properties" ||
               Key == "Objective-C Image Swift Version") {
      Flags |= mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue();
    } else if (Key == "Objective-C Image Info Section") {
      Section = cast<MDString>(MFE.Val)->getString();
    }
    // Backend generates L_OBJC_IMAGE_INFO from Swift ABI version + major + minor +
    // "Objective-C Garbage Collection".
    else if (Key == "Swift ABI Version") {
      Flags |= (mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue()) << 8;
    } else if (Key == "Swift Major Version") {
      Flags |= (mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue()) << 24;
    } else if (Key == "Swift Minor Version") {
      Flags |= (mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue()) << 16;
    }
  }
}

//===----------------------------------------------------------------------===//
//                                  ELF
//===----------------------------------------------------------------------===//

TargetLoweringObjectFileELF::TargetLoweringObjectFileELF() {
  SupportDSOLocalEquivalentLowering = true;
}

void TargetLoweringObjectFileELF::Initialize(MCContext &Ctx,
                                             const TargetMachine &TgtM) {
  TargetLoweringObjectFile::Initialize(Ctx, TgtM);

  CodeModel::Model CM = TgtM.getCodeModel();
  InitializeELF(TgtM.Options.UseInitArray);

  switch (TgtM.getTargetTriple().getArch()) {
  case Triple::arm:
  case Triple::armeb:
  case Triple::thumb:
  case Triple::thumbeb:
    if (Ctx.getAsmInfo()->getExceptionHandlingType() == ExceptionHandling::ARM)
      break;
    // Fallthrough if not using EHABI
    [[fallthrough]];
  case Triple::ppc:
  case Triple::ppcle:
  case Triple::x86:
    PersonalityEncoding = isPositionIndependent()
                              ? dwarf::DW_EH_PE_indirect |
                                    dwarf::DW_EH_PE_pcrel |
                                    dwarf::DW_EH_PE_sdata4
                              : dwarf::DW_EH_PE_absptr;
    LSDAEncoding = isPositionIndependent()
                       ? dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4
                       : dwarf::DW_EH_PE_absptr;
    TTypeEncoding = isPositionIndependent()
                        ? dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                              dwarf::DW_EH_PE_sdata4
                        : dwarf::DW_EH_PE_absptr;
    break;
  case Triple::x86_64:
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        ((CM == CodeModel::Small || CM == CodeModel::Medium)
         ? dwarf::DW_EH_PE_sdata4 : dwarf::DW_EH_PE_sdata8);
      LSDAEncoding = dwarf::DW_EH_PE_pcrel |
        (CM == CodeModel::Small
         ? dwarf::DW_EH_PE_sdata4 : dwarf::DW_EH_PE_sdata8);
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        ((CM == CodeModel::Small || CM == CodeModel::Medium)
         ? dwarf::DW_EH_PE_sdata4 : dwarf::DW_EH_PE_sdata8);
    } else {
      PersonalityEncoding =
        (CM == CodeModel::Small || CM == CodeModel::Medium)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
      LSDAEncoding = (CM == CodeModel::Small)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
      TTypeEncoding = (CM == CodeModel::Small)
        ? dwarf::DW_EH_PE_udata4 : dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::hexagon:
    PersonalityEncoding = dwarf::DW_EH_PE_absptr;
    LSDAEncoding = dwarf::DW_EH_PE_absptr;
    TTypeEncoding = dwarf::DW_EH_PE_absptr;
    if (isPositionIndependent()) {
      PersonalityEncoding |= dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel;
      LSDAEncoding |= dwarf::DW_EH_PE_pcrel;
      TTypeEncoding |= dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel;
    }
    break;
  case Triple::aarch64:
  case Triple::aarch64_be:
  case Triple::aarch64_32:
    // The small model guarantees static code/data size < 4GB, but not where it
    // will be in memory. Most of these could end up >2GB away so even a signed
    // pc-relative 32-bit address is insufficient, theoretically.
    //
    // Use DW_EH_PE_indirect even for -fno-pic to avoid copy relocations.
    LSDAEncoding = dwarf::DW_EH_PE_pcrel |
                   (TgtM.getTargetTriple().getEnvironment() == Triple::GNUILP32
                        ? dwarf::DW_EH_PE_sdata4
                        : dwarf::DW_EH_PE_sdata8);
    PersonalityEncoding = LSDAEncoding | dwarf::DW_EH_PE_indirect;
    TTypeEncoding = LSDAEncoding | dwarf::DW_EH_PE_indirect;
    break;
  case Triple::lanai:
    LSDAEncoding = dwarf::DW_EH_PE_absptr;
    PersonalityEncoding = dwarf::DW_EH_PE_absptr;
    TTypeEncoding = dwarf::DW_EH_PE_absptr;
    break;
  case Triple::mips:
  case Triple::mipsel:
  case Triple::mips64:
  case Triple::mips64el:
    // MIPS uses indirect pointer to refer personality functions and types, so
    // that the eh_frame section can be read-only. DW.ref.personality will be
    // generated for relocation.
    PersonalityEncoding = dwarf::DW_EH_PE_indirect;
    // FIXME: The N64 ABI probably ought to use DW_EH_PE_sdata8 but we can't
    //        identify N64 from just a triple.
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                    dwarf::DW_EH_PE_sdata4;

    // FreeBSD must be explicit about the data size and using pcrel since it's
    // assembler/linker won't do the automatic conversion that the Linux tools
    // do.
    if (isPositionIndependent() || TgtM.getTargetTriple().isOSFreeBSD()) {
      PersonalityEncoding |= dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    }
    break;
  case Triple::ppc64:
  case Triple::ppc64le:
    PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
      dwarf::DW_EH_PE_udata8;
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_udata8;
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
      dwarf::DW_EH_PE_udata8;
    break;
  case Triple::sparcel:
  case Triple::sparc:
    if (isPositionIndependent()) {
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      LSDAEncoding = dwarf::DW_EH_PE_absptr;
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    CallSiteEncoding = dwarf::DW_EH_PE_udata4;
    break;
  case Triple::riscv32:
  case Triple::riscv64:
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                          dwarf::DW_EH_PE_sdata4;
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                    dwarf::DW_EH_PE_sdata4;
    CallSiteEncoding = dwarf::DW_EH_PE_udata4;
    break;
  case Triple::sparcv9:
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::systemz:
    // All currently-defined code models guarantee that 4-byte PC-relative
    // values will be in range.
    if (isPositionIndependent()) {
      PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
      LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
      TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
        dwarf::DW_EH_PE_sdata4;
    } else {
      PersonalityEncoding = dwarf::DW_EH_PE_absptr;
      LSDAEncoding = dwarf::DW_EH_PE_absptr;
      TTypeEncoding = dwarf::DW_EH_PE_absptr;
    }
    break;
  case Triple::loongarch32:
  case Triple::loongarch64:
    LSDAEncoding = dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
    PersonalityEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                          dwarf::DW_EH_PE_sdata4;
    TTypeEncoding = dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel |
                    dwarf::DW_EH_PE_sdata4;
    break;
  default:
    break;
  }
}

void TargetLoweringObjectFileELF::getModuleMetadata(Module &M) {
  SmallVector<GlobalValue *, 4> Vec;
  collectUsedGlobalVariables(M, Vec, false);
  for (GlobalValue *GV : Vec)
    if (auto *GO = dyn_cast<GlobalObject>(GV))
      Used.insert(GO);
}

void TargetLoweringObjectFileELF::emitModuleMetadata(MCStreamer &Streamer,
                                                     Module &M) const {
  auto &C = getContext();

  if (NamedMDNode *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    auto *S = C.getELFSection(".linker-options", ELF::SHT_LLVM_LINKER_OPTIONS,
                              ELF::SHF_EXCLUDE);

    Streamer.switchSection(S);

    for (const auto *Operand : LinkerOptions->operands()) {
      if (cast<MDNode>(Operand)->getNumOperands() != 2)
        report_fatal_error("invalid llvm.linker.options");
      for (const auto &Option : cast<MDNode>(Operand)->operands()) {
        Streamer.emitBytes(cast<MDString>(Option)->getString());
        Streamer.emitInt8(0);
      }
    }
  }

  if (NamedMDNode *DependentLibraries = M.getNamedMetadata("llvm.dependent-libraries")) {
    auto *S = C.getELFSection(".deplibs", ELF::SHT_LLVM_DEPENDENT_LIBRARIES,
                              ELF::SHF_MERGE | ELF::SHF_STRINGS, 1);

    Streamer.switchSection(S);

    for (const auto *Operand : DependentLibraries->operands()) {
      Streamer.emitBytes(
          cast<MDString>(cast<MDNode>(Operand)->getOperand(0))->getString());
      Streamer.emitInt8(0);
    }
  }

  if (NamedMDNode *FuncInfo = M.getNamedMetadata(PseudoProbeDescMetadataName)) {
    // Emit a descriptor for every function including functions that have an
    // available external linkage. We may not want this for imported functions
    // that has code in another thinLTO module but we don't have a good way to
    // tell them apart from inline functions defined in header files. Therefore
    // we put each descriptor in a separate comdat section and rely on the
    // linker to deduplicate.
    for (const auto *Operand : FuncInfo->operands()) {
      const auto *MD = cast<MDNode>(Operand);
      auto *GUID = mdconst::dyn_extract<ConstantInt>(MD->getOperand(0));
      auto *Hash = mdconst::dyn_extract<ConstantInt>(MD->getOperand(1));
      auto *Name = cast<MDString>(MD->getOperand(2));
      auto *S = C.getObjectFileInfo()->getPseudoProbeDescSection(
          TM->getFunctionSections() ? Name->getString() : StringRef());

      Streamer.switchSection(S);
      Streamer.emitInt64(GUID->getZExtValue());
      Streamer.emitInt64(Hash->getZExtValue());
      Streamer.emitULEB128IntValue(Name->getString().size());
      Streamer.emitBytes(Name->getString());
    }
  }

  if (NamedMDNode *LLVMStats = M.getNamedMetadata("llvm.stats")) {
    // Emit the metadata for llvm statistics into .llvm_stats section, which is
    // formatted as a list of key/value pair, the value is base64 encoded.
    auto *S = C.getObjectFileInfo()->getLLVMStatsSection();
    Streamer.switchSection(S);
    for (const auto *Operand : LLVMStats->operands()) {
      const auto *MD = cast<MDNode>(Operand);
      assert(MD->getNumOperands() % 2 == 0 &&
             ("Operand num should be even for a list of key/value pair"));
      for (size_t I = 0; I < MD->getNumOperands(); I += 2) {
        // Encode the key string size.
        auto *Key = cast<MDString>(MD->getOperand(I));
        Streamer.emitULEB128IntValue(Key->getString().size());
        Streamer.emitBytes(Key->getString());
        // Encode the value into a Base64 string.
        std::string Value = encodeBase64(
            Twine(mdconst::dyn_extract<ConstantInt>(MD->getOperand(I + 1))
                      ->getZExtValue())
                .str());
        Streamer.emitULEB128IntValue(Value.size());
        Streamer.emitBytes(Value);
      }
    }
  }

  unsigned Version = 0;
  unsigned Flags = 0;
  StringRef Section;

  GetObjCImageInfo(M, Version, Flags, Section);
  if (!Section.empty()) {
    auto *S = C.getELFSection(Section, ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
    Streamer.switchSection(S);
    Streamer.emitLabel(C.getOrCreateSymbol(StringRef("OBJC_IMAGE_INFO")));
    Streamer.emitInt32(Version);
    Streamer.emitInt32(Flags);
    Streamer.addBlankLine();
  }

  emitCGProfileMetadata(Streamer, M);
}

MCSymbol *TargetLoweringObjectFileELF::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  unsigned Encoding = getPersonalityEncoding();
  if ((Encoding & 0x80) == DW_EH_PE_indirect)
    return getContext().getOrCreateSymbol(StringRef("DW.ref.") +
                                          TM.getSymbol(GV)->getName());
  if ((Encoding & 0x70) == DW_EH_PE_absptr)
    return TM.getSymbol(GV);
  report_fatal_error("We do not support this DWARF encoding yet!");
}

void TargetLoweringObjectFileELF::emitPersonalityValue(
    MCStreamer &Streamer, const DataLayout &DL, const MCSymbol *Sym) const {
  SmallString<64> NameData("DW.ref.");
  NameData += Sym->getName();
  MCSymbolELF *Label =
      cast<MCSymbolELF>(getContext().getOrCreateSymbol(NameData));
  Streamer.emitSymbolAttribute(Label, MCSA_Hidden);
  Streamer.emitSymbolAttribute(Label, MCSA_Weak);
  unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE | ELF::SHF_GROUP;
  MCSection *Sec = getContext().getELFNamedSection(".data", Label->getName(),
                                                   ELF::SHT_PROGBITS, Flags, 0);
  unsigned Size = DL.getPointerSize();
  Streamer.switchSection(Sec);
  Streamer.emitValueToAlignment(DL.getPointerABIAlignment(0));
  Streamer.emitSymbolAttribute(Label, MCSA_ELF_TypeObject);
  const MCExpr *E = MCConstantExpr::create(Size, getContext());
  Streamer.emitELFSize(Label, E);
  Streamer.emitLabel(Label);

  Streamer.emitSymbolValue(Sym, Size);
}

const MCExpr *TargetLoweringObjectFileELF::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  if (Encoding & DW_EH_PE_indirect) {
    MachineModuleInfoELF &ELFMMI = MMI->getObjFileInfo<MachineModuleInfoELF>();

    MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, ".DW.stub", TM);

    // Add information about the stub reference to ELFMMI so that the stub
    // gets emitted by the asmprinter.
    MachineModuleInfoImpl::StubValueTy &StubSym = ELFMMI.getGVStubEntry(SSym);
    if (!StubSym.getPointer()) {
      MCSymbol *Sym = TM.getSymbol(GV);
      StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
    }

    return TargetLoweringObjectFile::
      getTTypeReference(MCSymbolRefExpr::create(SSym, getContext()),
                        Encoding & ~DW_EH_PE_indirect, Streamer);
  }

  return TargetLoweringObjectFile::getTTypeGlobalReference(GV, Encoding, TM,
                                                           MMI, Streamer);
}

static SectionKind getELFKindForNamedSection(StringRef Name, SectionKind K) {
  // N.B.: The defaults used in here are not the same ones used in MC.
  // We follow gcc, MC follows gas. For example, given ".section .eh_frame",
  // both gas and MC will produce a section with no flags. Given
  // section(".eh_frame") gcc will produce:
  //
  //   .section   .eh_frame,"a",@progbits

  if (Name == getInstrProfSectionName(IPSK_covmap, Triple::ELF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covfun, Triple::ELF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covdata, Triple::ELF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covname, Triple::ELF,
                                      /*AddSegmentInfo=*/false) ||
      Name == ".llvmbc" || Name == ".llvmcmd")
    return SectionKind::getMetadata();

  if (!Name.starts_with(".")) return K;

  // Default implementation based on some magic section names.
  if (Name == ".bss" || Name.starts_with(".bss.") ||
      Name.starts_with(".gnu.linkonce.b.") ||
      Name.starts_with(".llvm.linkonce.b.") || Name == ".sbss" ||
      Name.starts_with(".sbss.") || Name.starts_with(".gnu.linkonce.sb.") ||
      Name.starts_with(".llvm.linkonce.sb."))
    return SectionKind::getBSS();

  if (Name == ".tdata" || Name.starts_with(".tdata.") ||
      Name.starts_with(".gnu.linkonce.td.") ||
      Name.starts_with(".llvm.linkonce.td."))
    return SectionKind::getThreadData();

  if (Name == ".tbss" || Name.starts_with(".tbss.") ||
      Name.starts_with(".gnu.linkonce.tb.") ||
      Name.starts_with(".llvm.linkonce.tb."))
    return SectionKind::getThreadBSS();

  return K;
}

static bool hasPrefix(StringRef SectionName, StringRef Prefix) {
  return SectionName.consume_front(Prefix) &&
         (SectionName.empty() || SectionName[0] == '.');
}

static unsigned getELFSectionType(StringRef Name, SectionKind K) {
  // Use SHT_NOTE for section whose name starts with ".note" to allow
  // emitting ELF notes from C variable declaration.
  // See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77609
  if (Name.starts_with(".note"))
    return ELF::SHT_NOTE;

  if (hasPrefix(Name, ".init_array"))
    return ELF::SHT_INIT_ARRAY;

  if (hasPrefix(Name, ".fini_array"))
    return ELF::SHT_FINI_ARRAY;

  if (hasPrefix(Name, ".preinit_array"))
    return ELF::SHT_PREINIT_ARRAY;

  if (hasPrefix(Name, ".llvm.offloading"))
    return ELF::SHT_LLVM_OFFLOADING;
  if (Name == ".llvm.lto")
    return ELF::SHT_LLVM_LTO;

  if (K.isBSS() || K.isThreadBSS())
    return ELF::SHT_NOBITS;

  return ELF::SHT_PROGBITS;
}

static unsigned getELFSectionFlags(SectionKind K) {
  unsigned Flags = 0;

  if (!K.isMetadata() && !K.isExclude())
    Flags |= ELF::SHF_ALLOC;

  if (K.isExclude())
    Flags |= ELF::SHF_EXCLUDE;

  if (K.isText())
    Flags |= ELF::SHF_EXECINSTR;

  if (K.isExecuteOnly())
    Flags |= ELF::SHF_ARM_PURECODE;

  if (K.isWriteable())
    Flags |= ELF::SHF_WRITE;

  if (K.isThreadLocal())
    Flags |= ELF::SHF_TLS;

  if (K.isMergeableCString() || K.isMergeableConst())
    Flags |= ELF::SHF_MERGE;

  if (K.isMergeableCString())
    Flags |= ELF::SHF_STRINGS;

  return Flags;
}

static const Comdat *getELFComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return nullptr;

  if (C->getSelectionKind() != Comdat::Any &&
      C->getSelectionKind() != Comdat::NoDeduplicate)
    report_fatal_error("ELF COMDATs only support SelectionKind::Any and "
                       "SelectionKind::NoDeduplicate, '" +
                       C->getName() + "' cannot be lowered.");

  return C;
}

static const MCSymbolELF *getLinkedToSymbol(const GlobalObject *GO,
                                            const TargetMachine &TM) {
  MDNode *MD = GO->getMetadata(LLVMContext::MD_associated);
  if (!MD)
    return nullptr;

  auto *VM = cast<ValueAsMetadata>(MD->getOperand(0).get());
  auto *OtherGV = dyn_cast<GlobalValue>(VM->getValue());
  return OtherGV ? dyn_cast<MCSymbolELF>(TM.getSymbol(OtherGV)) : nullptr;
}

static unsigned getEntrySizeForKind(SectionKind Kind) {
  if (Kind.isMergeable1ByteCString())
    return 1;
  else if (Kind.isMergeable2ByteCString())
    return 2;
  else if (Kind.isMergeable4ByteCString())
    return 4;
  else if (Kind.isMergeableConst4())
    return 4;
  else if (Kind.isMergeableConst8())
    return 8;
  else if (Kind.isMergeableConst16())
    return 16;
  else if (Kind.isMergeableConst32())
    return 32;
  else {
    // We shouldn't have mergeable C strings or mergeable constants that we
    // didn't handle above.
    assert(!Kind.isMergeableCString() && "unknown string width");
    assert(!Kind.isMergeableConst() && "unknown data width");
    return 0;
  }
}

/// Return the section prefix name used by options FunctionsSections and
/// DataSections.
static StringRef getSectionPrefixForGlobal(SectionKind Kind, bool IsLarge) {
  if (Kind.isText())
    return IsLarge ? ".ltext" : ".text";
  if (Kind.isReadOnly())
    return IsLarge ? ".lrodata" : ".rodata";
  if (Kind.isBSS())
    return IsLarge ? ".lbss" : ".bss";
  if (Kind.isThreadData())
    return ".tdata";
  if (Kind.isThreadBSS())
    return ".tbss";
  if (Kind.isData())
    return IsLarge ? ".ldata" : ".data";
  if (Kind.isReadOnlyWithRel())
    return IsLarge ? ".ldata.rel.ro" : ".data.rel.ro";
  llvm_unreachable("Unknown section kind");
}

static SmallString<128>
getELFSectionNameForGlobal(const GlobalObject *GO, SectionKind Kind,
                           Mangler &Mang, const TargetMachine &TM,
                           unsigned EntrySize, bool UniqueSectionName) {
  SmallString<128> Name =
      getSectionPrefixForGlobal(Kind, TM.isLargeGlobalValue(GO));
  if (Kind.isMergeableCString()) {
    // We also need alignment here.
    // FIXME: this is getting the alignment of the character, not the
    // alignment of the global!
    Align Alignment = GO->getDataLayout().getPreferredAlign(
        cast<GlobalVariable>(GO));

    Name += ".str";
    Name += utostr(EntrySize);
    Name += ".";
    Name += utostr(Alignment.value());
  } else if (Kind.isMergeableConst()) {
    Name += ".cst";
    Name += utostr(EntrySize);
  }

  bool HasPrefix = false;
  if (const auto *F = dyn_cast<Function>(GO)) {
    if (std::optional<StringRef> Prefix = F->getSectionPrefix()) {
      raw_svector_ostream(Name) << '.' << *Prefix;
      HasPrefix = true;
    }
  }

  if (UniqueSectionName) {
    Name.push_back('.');
    TM.getNameWithPrefix(Name, GO, Mang, /*MayAlwaysUsePrivate*/true);
  } else if (HasPrefix)
    // For distinguishing between .text.${text-section-prefix}. (with trailing
    // dot) and .text.${function-name}
    Name.push_back('.');
  return Name;
}

namespace {
class LoweringDiagnosticInfo : public DiagnosticInfo {
  const Twine &Msg;

public:
  LoweringDiagnosticInfo(const Twine &DiagMsg,
                         DiagnosticSeverity Severity = DS_Error)
      : DiagnosticInfo(DK_Lowering, Severity), Msg(DiagMsg) {}
  void print(DiagnosticPrinter &DP) const override { DP << Msg; }
};
}

/// Calculate an appropriate unique ID for a section, and update Flags,
/// EntrySize and NextUniqueID where appropriate.
static unsigned
calcUniqueIDUpdateFlagsAndSize(const GlobalObject *GO, StringRef SectionName,
                               SectionKind Kind, const TargetMachine &TM,
                               MCContext &Ctx, Mangler &Mang, unsigned &Flags,
                               unsigned &EntrySize, unsigned &NextUniqueID,
                               const bool Retain, const bool ForceUnique) {
  // Increment uniqueID if we are forced to emit a unique section.
  // This works perfectly fine with section attribute or pragma section as the
  // sections with the same name are grouped together by the assembler.
  if (ForceUnique)
    return NextUniqueID++;

  // A section can have at most one associated section. Put each global with
  // MD_associated in a unique section.
  const bool Associated = GO->getMetadata(LLVMContext::MD_associated);
  if (Associated) {
    Flags |= ELF::SHF_LINK_ORDER;
    return NextUniqueID++;
  }

  if (Retain) {
    if (TM.getTargetTriple().isOSSolaris())
      Flags |= ELF::SHF_SUNW_NODISCARD;
    else if (Ctx.getAsmInfo()->useIntegratedAssembler() ||
             Ctx.getAsmInfo()->binutilsIsAtLeast(2, 36))
      Flags |= ELF::SHF_GNU_RETAIN;
    return NextUniqueID++;
  }

  // If two symbols with differing sizes end up in the same mergeable section
  // that section can be assigned an incorrect entry size. To avoid this we
  // usually put symbols of the same size into distinct mergeable sections with
  // the same name. Doing so relies on the ",unique ," assembly feature. This
  // feature is not avalible until bintuils version 2.35
  // (https://sourceware.org/bugzilla/show_bug.cgi?id=25380).
  const bool SupportsUnique = Ctx.getAsmInfo()->useIntegratedAssembler() ||
                              Ctx.getAsmInfo()->binutilsIsAtLeast(2, 35);
  if (!SupportsUnique) {
    Flags &= ~ELF::SHF_MERGE;
    EntrySize = 0;
    return MCContext::GenericSectionID;
  }

  const bool SymbolMergeable = Flags & ELF::SHF_MERGE;
  const bool SeenSectionNameBefore =
      Ctx.isELFGenericMergeableSection(SectionName);
  // If this is the first ocurrence of this section name, treat it as the
  // generic section
  if (!SymbolMergeable && !SeenSectionNameBefore) {
    if (TM.getSeparateNamedSections())
      return NextUniqueID++;
    else
      return MCContext::GenericSectionID;
  }

  // Symbols must be placed into sections with compatible entry sizes. Generate
  // unique sections for symbols that have not been assigned to compatible
  // sections.
  const auto PreviousID =
      Ctx.getELFUniqueIDForEntsize(SectionName, Flags, EntrySize);
  if (PreviousID && (!TM.getSeparateNamedSections() ||
                     *PreviousID == MCContext::GenericSectionID))
    return *PreviousID;

  // If the user has specified the same section name as would be created
  // implicitly for this symbol e.g. .rodata.str1.1, then we don't need
  // to unique the section as the entry size for this symbol will be
  // compatible with implicitly created sections.
  SmallString<128> ImplicitSectionNameStem =
      getELFSectionNameForGlobal(GO, Kind, Mang, TM, EntrySize, false);
  if (SymbolMergeable &&
      Ctx.isELFImplicitMergeableSectionNamePrefix(SectionName) &&
      SectionName.starts_with(ImplicitSectionNameStem))
    return MCContext::GenericSectionID;

  // We have seen this section name before, but with different flags or entity
  // size. Create a new unique ID.
  return NextUniqueID++;
}

static std::tuple<StringRef, bool, unsigned>
getGlobalObjectInfo(const GlobalObject *GO, const TargetMachine &TM) {
  StringRef Group = "";
  bool IsComdat = false;
  unsigned Flags = 0;
  if (const Comdat *C = getELFComdat(GO)) {
    Flags |= ELF::SHF_GROUP;
    Group = C->getName();
    IsComdat = C->getSelectionKind() == Comdat::Any;
  }
  if (TM.isLargeGlobalValue(GO))
    Flags |= ELF::SHF_X86_64_LARGE;
  return {Group, IsComdat, Flags};
}

static MCSection *selectExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM,
    MCContext &Ctx, Mangler &Mang, unsigned &NextUniqueID,
    bool Retain, bool ForceUnique) {
  StringRef SectionName = GO->getSection();

  // Check if '#pragma clang section' name is applicable.
  // Note that pragma directive overrides -ffunction-section, -fdata-section
  // and so section name is exactly as user specified and not uniqued.
  const GlobalVariable *GV = dyn_cast<GlobalVariable>(GO);
  if (GV && GV->hasImplicitSection()) {
    auto Attrs = GV->getAttributes();
    if (Attrs.hasAttribute("bss-section") && Kind.isBSS()) {
      SectionName = Attrs.getAttribute("bss-section").getValueAsString();
    } else if (Attrs.hasAttribute("rodata-section") && Kind.isReadOnly()) {
      SectionName = Attrs.getAttribute("rodata-section").getValueAsString();
    } else if (Attrs.hasAttribute("relro-section") && Kind.isReadOnlyWithRel()) {
      SectionName = Attrs.getAttribute("relro-section").getValueAsString();
    } else if (Attrs.hasAttribute("data-section") && Kind.isData()) {
      SectionName = Attrs.getAttribute("data-section").getValueAsString();
    }
  }

  // Infer section flags from the section name if we can.
  Kind = getELFKindForNamedSection(SectionName, Kind);

  unsigned Flags = getELFSectionFlags(Kind);
  auto [Group, IsComdat, ExtraFlags] = getGlobalObjectInfo(GO, TM);
  Flags |= ExtraFlags;

  unsigned EntrySize = getEntrySizeForKind(Kind);
  const unsigned UniqueID = calcUniqueIDUpdateFlagsAndSize(
      GO, SectionName, Kind, TM, Ctx, Mang, Flags, EntrySize, NextUniqueID,
      Retain, ForceUnique);

  const MCSymbolELF *LinkedToSym = getLinkedToSymbol(GO, TM);
  MCSectionELF *Section = Ctx.getELFSection(
      SectionName, getELFSectionType(SectionName, Kind), Flags, EntrySize,
      Group, IsComdat, UniqueID, LinkedToSym);
  // Make sure that we did not get some other section with incompatible sh_link.
  // This should not be possible due to UniqueID code above.
  assert(Section->getLinkedToSymbol() == LinkedToSym &&
         "Associated symbol mismatch between sections");

  if (!(Ctx.getAsmInfo()->useIntegratedAssembler() ||
        Ctx.getAsmInfo()->binutilsIsAtLeast(2, 35))) {
    // If we are using GNU as before 2.35, then this symbol might have
    // been placed in an incompatible mergeable section. Emit an error if this
    // is the case to avoid creating broken output.
    if ((Section->getFlags() & ELF::SHF_MERGE) &&
        (Section->getEntrySize() != getEntrySizeForKind(Kind)))
      GO->getContext().diagnose(LoweringDiagnosticInfo(
          "Symbol '" + GO->getName() + "' from module '" +
          (GO->getParent() ? GO->getParent()->getSourceFileName() : "unknown") +
          "' required a section with entry-size=" +
          Twine(getEntrySizeForKind(Kind)) + " but was placed in section '" +
          SectionName + "' with entry-size=" + Twine(Section->getEntrySize()) +
          ": Explicit assignment by pragma or attribute of an incompatible "
          "symbol to this section?"));
  }

  return Section;
}

MCSection *TargetLoweringObjectFileELF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  return selectExplicitSectionGlobal(GO, Kind, TM, getContext(), getMangler(),
                                     NextUniqueID, Used.count(GO),
                                     /* ForceUnique = */false);
}

static MCSectionELF *selectELFSectionForGlobal(
    MCContext &Ctx, const GlobalObject *GO, SectionKind Kind, Mangler &Mang,
    const TargetMachine &TM, bool EmitUniqueSection, unsigned Flags,
    unsigned *NextUniqueID, const MCSymbolELF *AssociatedSymbol) {

  auto [Group, IsComdat, ExtraFlags] = getGlobalObjectInfo(GO, TM);
  Flags |= ExtraFlags;

  // Get the section entry size based on the kind.
  unsigned EntrySize = getEntrySizeForKind(Kind);

  bool UniqueSectionName = false;
  unsigned UniqueID = MCContext::GenericSectionID;
  if (EmitUniqueSection) {
    if (TM.getUniqueSectionNames()) {
      UniqueSectionName = true;
    } else {
      UniqueID = *NextUniqueID;
      (*NextUniqueID)++;
    }
  }
  SmallString<128> Name = getELFSectionNameForGlobal(
      GO, Kind, Mang, TM, EntrySize, UniqueSectionName);

  // Use 0 as the unique ID for execute-only text.
  if (Kind.isExecuteOnly())
    UniqueID = 0;
  return Ctx.getELFSection(Name, getELFSectionType(Name, Kind), Flags,
                           EntrySize, Group, IsComdat, UniqueID,
                           AssociatedSymbol);
}

static MCSection *selectELFSectionForGlobal(
    MCContext &Ctx, const GlobalObject *GO, SectionKind Kind, Mangler &Mang,
    const TargetMachine &TM, bool Retain, bool EmitUniqueSection,
    unsigned Flags, unsigned *NextUniqueID) {
  const MCSymbolELF *LinkedToSym = getLinkedToSymbol(GO, TM);
  if (LinkedToSym) {
    EmitUniqueSection = true;
    Flags |= ELF::SHF_LINK_ORDER;
  }
  if (Retain) {
    if (TM.getTargetTriple().isOSSolaris()) {
      EmitUniqueSection = true;
      Flags |= ELF::SHF_SUNW_NODISCARD;
    } else if (Ctx.getAsmInfo()->useIntegratedAssembler() ||
               Ctx.getAsmInfo()->binutilsIsAtLeast(2, 36)) {
      EmitUniqueSection = true;
      Flags |= ELF::SHF_GNU_RETAIN;
    }
  }

  MCSectionELF *Section = selectELFSectionForGlobal(
      Ctx, GO, Kind, Mang, TM, EmitUniqueSection, Flags,
      NextUniqueID, LinkedToSym);
  assert(Section->getLinkedToSymbol() == LinkedToSym);
  return Section;
}

MCSection *TargetLoweringObjectFileELF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  unsigned Flags = getELFSectionFlags(Kind);

  // If we have -ffunction-section or -fdata-section then we should emit the
  // global value to a uniqued section specifically for it.
  bool EmitUniqueSection = false;
  if (!(Flags & ELF::SHF_MERGE) && !Kind.isCommon()) {
    if (Kind.isText())
      EmitUniqueSection = TM.getFunctionSections();
    else
      EmitUniqueSection = TM.getDataSections();
  }
  EmitUniqueSection |= GO->hasComdat();
  return selectELFSectionForGlobal(getContext(), GO, Kind, getMangler(), TM,
                                   Used.count(GO), EmitUniqueSection, Flags,
                                   &NextUniqueID);
}

MCSection *TargetLoweringObjectFileELF::getUniqueSectionForFunction(
    const Function &F, const TargetMachine &TM) const {
  SectionKind Kind = SectionKind::getText();
  unsigned Flags = getELFSectionFlags(Kind);
  // If the function's section names is pre-determined via pragma or a
  // section attribute, call selectExplicitSectionGlobal.
  if (F.hasSection())
    return selectExplicitSectionGlobal(
        &F, Kind, TM, getContext(), getMangler(), NextUniqueID,
        Used.count(&F), /* ForceUnique = */true);
  else
    return selectELFSectionForGlobal(
        getContext(), &F, Kind, getMangler(), TM, Used.count(&F),
        /*EmitUniqueSection=*/true, Flags, &NextUniqueID);
}

MCSection *TargetLoweringObjectFileELF::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  // If the function can be removed, produce a unique section so that
  // the table doesn't prevent the removal.
  const Comdat *C = F.getComdat();
  bool EmitUniqueSection = TM.getFunctionSections() || C;
  if (!EmitUniqueSection)
    return ReadOnlySection;

  return selectELFSectionForGlobal(getContext(), &F, SectionKind::getReadOnly(),
                                   getMangler(), TM, EmitUniqueSection,
                                   ELF::SHF_ALLOC, &NextUniqueID,
                                   /* AssociatedSymbol */ nullptr);
}

MCSection *TargetLoweringObjectFileELF::getSectionForLSDA(
    const Function &F, const MCSymbol &FnSym, const TargetMachine &TM) const {
  // If neither COMDAT nor function sections, use the monolithic LSDA section.
  // Re-use this path if LSDASection is null as in the Arm EHABI.
  if (!LSDASection || (!F.hasComdat() && !TM.getFunctionSections()))
    return LSDASection;

  const auto *LSDA = cast<MCSectionELF>(LSDASection);
  unsigned Flags = LSDA->getFlags();
  const MCSymbolELF *LinkedToSym = nullptr;
  StringRef Group;
  bool IsComdat = false;
  if (const Comdat *C = getELFComdat(&F)) {
    Flags |= ELF::SHF_GROUP;
    Group = C->getName();
    IsComdat = C->getSelectionKind() == Comdat::Any;
  }
  // Use SHF_LINK_ORDER to facilitate --gc-sections if we can use GNU ld>=2.36
  // or LLD, which support mixed SHF_LINK_ORDER & non-SHF_LINK_ORDER.
  if (TM.getFunctionSections() &&
      (getContext().getAsmInfo()->useIntegratedAssembler() &&
       getContext().getAsmInfo()->binutilsIsAtLeast(2, 36))) {
    Flags |= ELF::SHF_LINK_ORDER;
    LinkedToSym = cast<MCSymbolELF>(&FnSym);
  }

  // Append the function name as the suffix like GCC, assuming
  // -funique-section-names applies to .gcc_except_table sections.
  return getContext().getELFSection(
      (TM.getUniqueSectionNames() ? LSDA->getName() + "." + F.getName()
                                  : LSDA->getName()),
      LSDA->getType(), Flags, 0, Group, IsComdat, MCSection::NonUniqueID,
      LinkedToSym);
}

bool TargetLoweringObjectFileELF::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  // We can always create relative relocations, so use another section
  // that can be marked non-executable.
  return false;
}

/// Given a mergeable constant with the specified size and relocation
/// information, return a section that it should be placed in.
MCSection *TargetLoweringObjectFileELF::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  if (Kind.isMergeableConst4() && MergeableConst4Section)
    return MergeableConst4Section;
  if (Kind.isMergeableConst8() && MergeableConst8Section)
    return MergeableConst8Section;
  if (Kind.isMergeableConst16() && MergeableConst16Section)
    return MergeableConst16Section;
  if (Kind.isMergeableConst32() && MergeableConst32Section)
    return MergeableConst32Section;
  if (Kind.isReadOnly())
    return ReadOnlySection;

  assert(Kind.isReadOnlyWithRel() && "Unknown section kind");
  return DataRelROSection;
}

/// Returns a unique section for the given machine basic block.
MCSection *TargetLoweringObjectFileELF::getSectionForMachineBasicBlock(
    const Function &F, const MachineBasicBlock &MBB,
    const TargetMachine &TM) const {
  assert(MBB.isBeginSection() && "Basic block does not start a section!");
  unsigned UniqueID = MCContext::GenericSectionID;

  // For cold sections use the .text.split. prefix along with the parent
  // function name. All cold blocks for the same function go to the same
  // section. Similarly all exception blocks are grouped by symbol name
  // under the .text.eh prefix. For regular sections, we either use a unique
  // name, or a unique ID for the section.
  SmallString<128> Name;
  StringRef FunctionSectionName = MBB.getParent()->getSection()->getName();
  if (FunctionSectionName == ".text" ||
      FunctionSectionName.starts_with(".text.")) {
    // Function is in a regular .text section.
    StringRef FunctionName = MBB.getParent()->getName();
    if (MBB.getSectionID() == MBBSectionID::ColdSectionID) {
      Name += BBSectionsColdTextPrefix;
      Name += FunctionName;
    } else if (MBB.getSectionID() == MBBSectionID::ExceptionSectionID) {
      Name += ".text.eh.";
      Name += FunctionName;
    } else {
      Name += FunctionSectionName;
      if (TM.getUniqueBasicBlockSectionNames()) {
        if (!Name.ends_with("."))
          Name += ".";
        Name += MBB.getSymbol()->getName();
      } else {
        UniqueID = NextUniqueID++;
      }
    }
  } else {
    // If the original function has a custom non-dot-text section, then emit
    // all basic block sections into that section too, each with a unique id.
    Name = FunctionSectionName;
    UniqueID = NextUniqueID++;
  }

  unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_EXECINSTR;
  std::string GroupName;
  if (F.hasComdat()) {
    Flags |= ELF::SHF_GROUP;
    GroupName = F.getComdat()->getName().str();
  }
  return getContext().getELFSection(Name, ELF::SHT_PROGBITS, Flags,
                                    0 /* Entry Size */, GroupName,
                                    F.hasComdat(), UniqueID, nullptr);
}

static MCSectionELF *getStaticStructorSection(MCContext &Ctx, bool UseInitArray,
                                              bool IsCtor, unsigned Priority,
                                              const MCSymbol *KeySym) {
  std::string Name;
  unsigned Type;
  unsigned Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;
  StringRef Comdat = KeySym ? KeySym->getName() : "";

  if (KeySym)
    Flags |= ELF::SHF_GROUP;

  if (UseInitArray) {
    if (IsCtor) {
      Type = ELF::SHT_INIT_ARRAY;
      Name = ".init_array";
    } else {
      Type = ELF::SHT_FINI_ARRAY;
      Name = ".fini_array";
    }
    if (Priority != 65535) {
      Name += '.';
      Name += utostr(Priority);
    }
  } else {
    // The default scheme is .ctor / .dtor, so we have to invert the priority
    // numbering.
    if (IsCtor)
      Name = ".ctors";
    else
      Name = ".dtors";
    if (Priority != 65535)
      raw_string_ostream(Name) << format(".%05u", 65535 - Priority);
    Type = ELF::SHT_PROGBITS;
  }

  return Ctx.getELFSection(Name, Type, Flags, 0, Comdat, /*IsComdat=*/true);
}

MCSection *TargetLoweringObjectFileELF::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getStaticStructorSection(getContext(), UseInitArray, true, Priority,
                                  KeySym);
}

MCSection *TargetLoweringObjectFileELF::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getStaticStructorSection(getContext(), UseInitArray, false, Priority,
                                  KeySym);
}

const MCExpr *TargetLoweringObjectFileELF::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  // We may only use a PLT-relative relocation to refer to unnamed_addr
  // functions.
  if (!LHS->hasGlobalUnnamedAddr() || !LHS->getValueType()->isFunctionTy())
    return nullptr;

  // Basic correctness checks.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0 || LHS->isThreadLocal() ||
      RHS->isThreadLocal())
    return nullptr;

  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(TM.getSymbol(LHS), PLTRelativeVariantKind,
                              getContext()),
      MCSymbolRefExpr::create(TM.getSymbol(RHS), getContext()), getContext());
}

const MCExpr *TargetLoweringObjectFileELF::lowerDSOLocalEquivalent(
    const DSOLocalEquivalent *Equiv, const TargetMachine &TM) const {
  assert(supportDSOLocalEquivalentLowering());

  const auto *GV = Equiv->getGlobalValue();

  // A PLT entry is not needed for dso_local globals.
  if (GV->isDSOLocal() || GV->isImplicitDSOLocal())
    return MCSymbolRefExpr::create(TM.getSymbol(GV), getContext());

  return MCSymbolRefExpr::create(TM.getSymbol(GV), PLTRelativeVariantKind,
                                 getContext());
}

MCSection *TargetLoweringObjectFileELF::getSectionForCommandLines() const {
  // Use ".GCC.command.line" since this feature is to support clang's
  // -frecord-gcc-switches which in turn attempts to mimic GCC's switch of the
  // same name.
  return getContext().getELFSection(".GCC.command.line", ELF::SHT_PROGBITS,
                                    ELF::SHF_MERGE | ELF::SHF_STRINGS, 1);
}

void
TargetLoweringObjectFileELF::InitializeELF(bool UseInitArray_) {
  UseInitArray = UseInitArray_;
  MCContext &Ctx = getContext();
  if (!UseInitArray) {
    StaticCtorSection = Ctx.getELFSection(".ctors", ELF::SHT_PROGBITS,
                                          ELF::SHF_ALLOC | ELF::SHF_WRITE);

    StaticDtorSection = Ctx.getELFSection(".dtors", ELF::SHT_PROGBITS,
                                          ELF::SHF_ALLOC | ELF::SHF_WRITE);
    return;
  }

  StaticCtorSection = Ctx.getELFSection(".init_array", ELF::SHT_INIT_ARRAY,
                                        ELF::SHF_WRITE | ELF::SHF_ALLOC);
  StaticDtorSection = Ctx.getELFSection(".fini_array", ELF::SHT_FINI_ARRAY,
                                        ELF::SHF_WRITE | ELF::SHF_ALLOC);
}

//===----------------------------------------------------------------------===//
//                                 MachO
//===----------------------------------------------------------------------===//

TargetLoweringObjectFileMachO::TargetLoweringObjectFileMachO() {
  SupportIndirectSymViaGOTPCRel = true;
}

void TargetLoweringObjectFileMachO::Initialize(MCContext &Ctx,
                                               const TargetMachine &TM) {
  TargetLoweringObjectFile::Initialize(Ctx, TM);
  if (TM.getRelocationModel() == Reloc::Static) {
    StaticCtorSection = Ctx.getMachOSection("__TEXT", "__constructor", 0,
                                            SectionKind::getData());
    StaticDtorSection = Ctx.getMachOSection("__TEXT", "__destructor", 0,
                                            SectionKind::getData());
  } else {
    StaticCtorSection = Ctx.getMachOSection("__DATA", "__mod_init_func",
                                            MachO::S_MOD_INIT_FUNC_POINTERS,
                                            SectionKind::getData());
    StaticDtorSection = Ctx.getMachOSection("__DATA", "__mod_term_func",
                                            MachO::S_MOD_TERM_FUNC_POINTERS,
                                            SectionKind::getData());
  }

  PersonalityEncoding =
      dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
  LSDAEncoding = dwarf::DW_EH_PE_pcrel;
  TTypeEncoding =
      dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_pcrel | dwarf::DW_EH_PE_sdata4;
}

MCSection *TargetLoweringObjectFileMachO::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return StaticDtorSection;
  // In userspace, we lower global destructors via atexit(), but kernel/kext
  // environments do not provide this function so we still need to support the
  // legacy way here.
  // See the -disable-atexit-based-global-dtor-lowering CodeGen flag for more
  // context.
}

void TargetLoweringObjectFileMachO::emitModuleMetadata(MCStreamer &Streamer,
                                                       Module &M) const {
  // Emit the linker options if present.
  if (auto *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    for (const auto *Option : LinkerOptions->operands()) {
      SmallVector<std::string, 4> StrOptions;
      for (const auto &Piece : cast<MDNode>(Option)->operands())
        StrOptions.push_back(std::string(cast<MDString>(Piece)->getString()));
      Streamer.emitLinkerOptions(StrOptions);
    }
  }

  unsigned VersionVal = 0;
  unsigned ImageInfoFlags = 0;
  StringRef SectionVal;

  GetObjCImageInfo(M, VersionVal, ImageInfoFlags, SectionVal);
  emitCGProfileMetadata(Streamer, M);

  // The section is mandatory. If we don't have it, then we don't have GC info.
  if (SectionVal.empty())
    return;

  StringRef Segment, Section;
  unsigned TAA = 0, StubSize = 0;
  bool TAAParsed;
  if (Error E = MCSectionMachO::ParseSectionSpecifier(
          SectionVal, Segment, Section, TAA, TAAParsed, StubSize)) {
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Invalid section specifier '" + Section +
                       "': " + toString(std::move(E)) + ".");
  }

  // Get the section.
  MCSectionMachO *S = getContext().getMachOSection(
      Segment, Section, TAA, StubSize, SectionKind::getData());
  Streamer.switchSection(S);
  Streamer.emitLabel(getContext().
                     getOrCreateSymbol(StringRef("L_OBJC_IMAGE_INFO")));
  Streamer.emitInt32(VersionVal);
  Streamer.emitInt32(ImageInfoFlags);
  Streamer.addBlankLine();
}

static void checkMachOComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return;

  report_fatal_error("MachO doesn't support COMDATs, '" + C->getName() +
                     "' cannot be lowered.");
}

MCSection *TargetLoweringObjectFileMachO::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {

  StringRef SectionName = GO->getSection();

  const GlobalVariable *GV = dyn_cast<GlobalVariable>(GO);
  if (GV && GV->hasImplicitSection()) {
    auto Attrs = GV->getAttributes();
    if (Attrs.hasAttribute("bss-section") && Kind.isBSS()) {
      SectionName = Attrs.getAttribute("bss-section").getValueAsString();
    } else if (Attrs.hasAttribute("rodata-section") && Kind.isReadOnly()) {
      SectionName = Attrs.getAttribute("rodata-section").getValueAsString();
    } else if (Attrs.hasAttribute("relro-section") && Kind.isReadOnlyWithRel()) {
      SectionName = Attrs.getAttribute("relro-section").getValueAsString();
    } else if (Attrs.hasAttribute("data-section") && Kind.isData()) {
      SectionName = Attrs.getAttribute("data-section").getValueAsString();
    }
  }

  // Parse the section specifier and create it if valid.
  StringRef Segment, Section;
  unsigned TAA = 0, StubSize = 0;
  bool TAAParsed;

  checkMachOComdat(GO);

  if (Error E = MCSectionMachO::ParseSectionSpecifier(
          SectionName, Segment, Section, TAA, TAAParsed, StubSize)) {
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Global variable '" + GO->getName() +
                       "' has an invalid section specifier '" +
                       GO->getSection() + "': " + toString(std::move(E)) + ".");
  }

  // Get the section.
  MCSectionMachO *S =
      getContext().getMachOSection(Segment, Section, TAA, StubSize, Kind);

  // If TAA wasn't set by ParseSectionSpecifier() above,
  // use the value returned by getMachOSection() as a default.
  if (!TAAParsed)
    TAA = S->getTypeAndAttributes();

  // Okay, now that we got the section, verify that the TAA & StubSize agree.
  // If the user declared multiple globals with different section flags, we need
  // to reject it here.
  if (S->getTypeAndAttributes() != TAA || S->getStubSize() != StubSize) {
    // If invalid, report the error with report_fatal_error.
    report_fatal_error("Global variable '" + GO->getName() +
                       "' section type or attributes does not match previous"
                       " section specifier");
  }

  return S;
}

MCSection *TargetLoweringObjectFileMachO::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  checkMachOComdat(GO);

  // Handle thread local data.
  if (Kind.isThreadBSS()) return TLSBSSSection;
  if (Kind.isThreadData()) return TLSDataSection;

  if (Kind.isText())
    return GO->isWeakForLinker() ? TextCoalSection : TextSection;

  // If this is weak/linkonce, put this in a coalescable section, either in text
  // or data depending on if it is writable.
  if (GO->isWeakForLinker()) {
    if (Kind.isReadOnly())
      return ConstTextCoalSection;
    if (Kind.isReadOnlyWithRel())
      return ConstDataCoalSection;
    return DataCoalSection;
  }

  // FIXME: Alignment check should be handled by section classifier.
  if (Kind.isMergeable1ByteCString() &&
      GO->getDataLayout().getPreferredAlign(
          cast<GlobalVariable>(GO)) < Align(32))
    return CStringSection;

  // Do not put 16-bit arrays in the UString section if they have an
  // externally visible label, this runs into issues with certain linker
  // versions.
  if (Kind.isMergeable2ByteCString() && !GO->hasExternalLinkage() &&
      GO->getDataLayout().getPreferredAlign(
          cast<GlobalVariable>(GO)) < Align(32))
    return UStringSection;

  // With MachO only variables whose corresponding symbol starts with 'l' or
  // 'L' can be merged, so we only try merging GVs with private linkage.
  if (GO->hasPrivateLinkage() && Kind.isMergeableConst()) {
    if (Kind.isMergeableConst4())
      return FourByteConstantSection;
    if (Kind.isMergeableConst8())
      return EightByteConstantSection;
    if (Kind.isMergeableConst16())
      return SixteenByteConstantSection;
  }

  // Otherwise, if it is readonly, but not something we can specially optimize,
  // just drop it in .const.
  if (Kind.isReadOnly())
    return ReadOnlySection;

  // If this is marked const, put it into a const section.  But if the dynamic
  // linker needs to write to it, put it in the data segment.
  if (Kind.isReadOnlyWithRel())
    return ConstDataSection;

  // Put zero initialized globals with strong external linkage in the
  // DATA, __common section with the .zerofill directive.
  if (Kind.isBSSExtern())
    return DataCommonSection;

  // Put zero initialized globals with local linkage in __DATA,__bss directive
  // with the .zerofill directive (aka .lcomm).
  if (Kind.isBSSLocal())
    return DataBSSSection;

  // Otherwise, just drop the variable in the normal data section.
  return DataSection;
}

MCSection *TargetLoweringObjectFileMachO::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  // If this constant requires a relocation, we have to put it in the data
  // segment, not in the text segment.
  if (Kind.isData() || Kind.isReadOnlyWithRel())
    return ConstDataSection;

  if (Kind.isMergeableConst4())
    return FourByteConstantSection;
  if (Kind.isMergeableConst8())
    return EightByteConstantSection;
  if (Kind.isMergeableConst16())
    return SixteenByteConstantSection;
  return ReadOnlySection;  // .const
}

MCSection *TargetLoweringObjectFileMachO::getSectionForCommandLines() const {
  return getContext().getMachOSection("__TEXT", "__command_line", 0,
                                      SectionKind::getReadOnly());
}

const MCExpr *TargetLoweringObjectFileMachO::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // The mach-o version of this method defaults to returning a stub reference.

  if (Encoding & DW_EH_PE_indirect) {
    MachineModuleInfoMachO &MachOMMI =
      MMI->getObjFileInfo<MachineModuleInfoMachO>();

    MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr", TM);

    // Add information about the stub reference to MachOMMI so that the stub
    // gets emitted by the asmprinter.
    MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(SSym);
    if (!StubSym.getPointer()) {
      MCSymbol *Sym = TM.getSymbol(GV);
      StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
    }

    return TargetLoweringObjectFile::
      getTTypeReference(MCSymbolRefExpr::create(SSym, getContext()),
                        Encoding & ~DW_EH_PE_indirect, Streamer);
  }

  return TargetLoweringObjectFile::getTTypeGlobalReference(GV, Encoding, TM,
                                                           MMI, Streamer);
}

MCSymbol *TargetLoweringObjectFileMachO::getCFIPersonalitySymbol(
    const GlobalValue *GV, const TargetMachine &TM,
    MachineModuleInfo *MMI) const {
  // The mach-o version of this method defaults to returning a stub reference.
  MachineModuleInfoMachO &MachOMMI =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();

  MCSymbol *SSym = getSymbolWithGlobalValueBase(GV, "$non_lazy_ptr", TM);

  // Add information about the stub reference to MachOMMI so that the stub
  // gets emitted by the asmprinter.
  MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(SSym);
  if (!StubSym.getPointer()) {
    MCSymbol *Sym = TM.getSymbol(GV);
    StubSym = MachineModuleInfoImpl::StubValueTy(Sym, !GV->hasLocalLinkage());
  }

  return SSym;
}

const MCExpr *TargetLoweringObjectFileMachO::getIndirectSymViaGOTPCRel(
    const GlobalValue *GV, const MCSymbol *Sym, const MCValue &MV,
    int64_t Offset, MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  // Although MachO 32-bit targets do not explicitly have a GOTPCREL relocation
  // as 64-bit do, we replace the GOT equivalent by accessing the final symbol
  // through a non_lazy_ptr stub instead. One advantage is that it allows the
  // computation of deltas to final external symbols. Example:
  //
  //    _extgotequiv:
  //       .long   _extfoo
  //
  //    _delta:
  //       .long   _extgotequiv-_delta
  //
  // is transformed to:
  //
  //    _delta:
  //       .long   L_extfoo$non_lazy_ptr-(_delta+0)
  //
  //       .section        __IMPORT,__pointers,non_lazy_symbol_pointers
  //    L_extfoo$non_lazy_ptr:
  //       .indirect_symbol        _extfoo
  //       .long   0
  //
  // The indirect symbol table (and sections of non_lazy_symbol_pointers type)
  // may point to both local (same translation unit) and global (other
  // translation units) symbols. Example:
  //
  // .section __DATA,__pointers,non_lazy_symbol_pointers
  // L1:
  //    .indirect_symbol _myGlobal
  //    .long 0
  // L2:
  //    .indirect_symbol _myLocal
  //    .long _myLocal
  //
  // If the symbol is local, instead of the symbol's index, the assembler
  // places the constant INDIRECT_SYMBOL_LOCAL into the indirect symbol table.
  // Then the linker will notice the constant in the table and will look at the
  // content of the symbol.
  MachineModuleInfoMachO &MachOMMI =
    MMI->getObjFileInfo<MachineModuleInfoMachO>();
  MCContext &Ctx = getContext();

  // The offset must consider the original displacement from the base symbol
  // since 32-bit targets don't have a GOTPCREL to fold the PC displacement.
  Offset = -MV.getConstant();
  const MCSymbol *BaseSym = &MV.getSymB()->getSymbol();

  // Access the final symbol via sym$non_lazy_ptr and generate the appropriated
  // non_lazy_ptr stubs.
  SmallString<128> Name;
  StringRef Suffix = "$non_lazy_ptr";
  Name += MMI->getModule()->getDataLayout().getPrivateGlobalPrefix();
  Name += Sym->getName();
  Name += Suffix;
  MCSymbol *Stub = Ctx.getOrCreateSymbol(Name);

  MachineModuleInfoImpl::StubValueTy &StubSym = MachOMMI.getGVStubEntry(Stub);

  if (!StubSym.getPointer())
    StubSym = MachineModuleInfoImpl::StubValueTy(const_cast<MCSymbol *>(Sym),
                                                 !GV->hasLocalLinkage());

  const MCExpr *BSymExpr =
    MCSymbolRefExpr::create(BaseSym, MCSymbolRefExpr::VK_None, Ctx);
  const MCExpr *LHS =
    MCSymbolRefExpr::create(Stub, MCSymbolRefExpr::VK_None, Ctx);

  if (!Offset)
    return MCBinaryExpr::createSub(LHS, BSymExpr, Ctx);

  const MCExpr *RHS =
    MCBinaryExpr::createAdd(BSymExpr, MCConstantExpr::create(Offset, Ctx), Ctx);
  return MCBinaryExpr::createSub(LHS, RHS, Ctx);
}

static bool canUsePrivateLabel(const MCAsmInfo &AsmInfo,
                               const MCSection &Section) {
  if (!MCAsmInfoDarwin::isSectionAtomizableBySymbols(Section))
    return true;

  // FIXME: we should be able to use private labels for sections that can't be
  // dead-stripped (there's no issue with blocking atomization there), but `ld
  // -r` sometimes drops the no_dead_strip attribute from sections so for safety
  // we don't allow it.
  return false;
}

void TargetLoweringObjectFileMachO::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  bool CannotUsePrivateLabel = true;
  if (auto *GO = GV->getAliaseeObject()) {
    SectionKind GOKind = TargetLoweringObjectFile::getKindForGlobal(GO, TM);
    const MCSection *TheSection = SectionForGlobal(GO, GOKind, TM);
    CannotUsePrivateLabel =
        !canUsePrivateLabel(*TM.getMCAsmInfo(), *TheSection);
  }
  getMangler().getNameWithPrefix(OutName, GV, CannotUsePrivateLabel);
}

//===----------------------------------------------------------------------===//
//                                  COFF
//===----------------------------------------------------------------------===//

static unsigned
getCOFFSectionFlags(SectionKind K, const TargetMachine &TM) {
  unsigned Flags = 0;
  bool isThumb = TM.getTargetTriple().getArch() == Triple::thumb;

  if (K.isMetadata())
    Flags |=
      COFF::IMAGE_SCN_MEM_DISCARDABLE;
  else if (K.isExclude())
    Flags |=
      COFF::IMAGE_SCN_LNK_REMOVE | COFF::IMAGE_SCN_MEM_DISCARDABLE;
  else if (K.isText())
    Flags |=
      COFF::IMAGE_SCN_MEM_EXECUTE |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_CNT_CODE |
      (isThumb ? COFF::IMAGE_SCN_MEM_16BIT : (COFF::SectionCharacteristics)0);
  else if (K.isBSS())
    Flags |=
      COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;
  else if (K.isThreadLocal())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;
  else if (K.isReadOnly() || K.isReadOnlyWithRel())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ;
  else if (K.isWriteable())
    Flags |=
      COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
      COFF::IMAGE_SCN_MEM_READ |
      COFF::IMAGE_SCN_MEM_WRITE;

  return Flags;
}

static const GlobalValue *getComdatGVForCOFF(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  assert(C && "expected GV to have a Comdat!");

  StringRef ComdatGVName = C->getName();
  const GlobalValue *ComdatGV = GV->getParent()->getNamedValue(ComdatGVName);
  if (!ComdatGV)
    report_fatal_error("Associative COMDAT symbol '" + ComdatGVName +
                       "' does not exist.");

  if (ComdatGV->getComdat() != C)
    report_fatal_error("Associative COMDAT symbol '" + ComdatGVName +
                       "' is not a key for its COMDAT.");

  return ComdatGV;
}

static int getSelectionForCOFF(const GlobalValue *GV) {
  if (const Comdat *C = GV->getComdat()) {
    const GlobalValue *ComdatKey = getComdatGVForCOFF(GV);
    if (const auto *GA = dyn_cast<GlobalAlias>(ComdatKey))
      ComdatKey = GA->getAliaseeObject();
    if (ComdatKey == GV) {
      switch (C->getSelectionKind()) {
      case Comdat::Any:
        return COFF::IMAGE_COMDAT_SELECT_ANY;
      case Comdat::ExactMatch:
        return COFF::IMAGE_COMDAT_SELECT_EXACT_MATCH;
      case Comdat::Largest:
        return COFF::IMAGE_COMDAT_SELECT_LARGEST;
      case Comdat::NoDeduplicate:
        return COFF::IMAGE_COMDAT_SELECT_NODUPLICATES;
      case Comdat::SameSize:
        return COFF::IMAGE_COMDAT_SELECT_SAME_SIZE;
      }
    } else {
      return COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE;
    }
  }
  return 0;
}

MCSection *TargetLoweringObjectFileCOFF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  StringRef Name = GO->getSection();
  if (Name == getInstrProfSectionName(IPSK_covmap, Triple::COFF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covfun, Triple::COFF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covdata, Triple::COFF,
                                      /*AddSegmentInfo=*/false) ||
      Name == getInstrProfSectionName(IPSK_covname, Triple::COFF,
                                      /*AddSegmentInfo=*/false))
    Kind = SectionKind::getMetadata();
  int Selection = 0;
  unsigned Characteristics = getCOFFSectionFlags(Kind, TM);
  StringRef COMDATSymName = "";
  if (GO->hasComdat()) {
    Selection = getSelectionForCOFF(GO);
    const GlobalValue *ComdatGV;
    if (Selection == COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE)
      ComdatGV = getComdatGVForCOFF(GO);
    else
      ComdatGV = GO;

    if (!ComdatGV->hasPrivateLinkage()) {
      MCSymbol *Sym = TM.getSymbol(ComdatGV);
      COMDATSymName = Sym->getName();
      Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    } else {
      Selection = 0;
    }
  }

  return getContext().getCOFFSection(Name, Characteristics, COMDATSymName,
                                     Selection);
}

static StringRef getCOFFSectionNameForUniqueGlobal(SectionKind Kind) {
  if (Kind.isText())
    return ".text";
  if (Kind.isBSS())
    return ".bss";
  if (Kind.isThreadLocal())
    return ".tls$";
  if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
    return ".rdata";
  return ".data";
}

MCSection *TargetLoweringObjectFileCOFF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // If we have -ffunction-sections then we should emit the global value to a
  // uniqued section specifically for it.
  bool EmitUniquedSection;
  if (Kind.isText())
    EmitUniquedSection = TM.getFunctionSections();
  else
    EmitUniquedSection = TM.getDataSections();

  if ((EmitUniquedSection && !Kind.isCommon()) || GO->hasComdat()) {
    SmallString<256> Name = getCOFFSectionNameForUniqueGlobal(Kind);

    unsigned Characteristics = getCOFFSectionFlags(Kind, TM);

    Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    int Selection = getSelectionForCOFF(GO);
    if (!Selection)
      Selection = COFF::IMAGE_COMDAT_SELECT_NODUPLICATES;
    const GlobalValue *ComdatGV;
    if (GO->hasComdat())
      ComdatGV = getComdatGVForCOFF(GO);
    else
      ComdatGV = GO;

    unsigned UniqueID = MCContext::GenericSectionID;
    if (EmitUniquedSection)
      UniqueID = NextUniqueID++;

    if (!ComdatGV->hasPrivateLinkage()) {
      MCSymbol *Sym = TM.getSymbol(ComdatGV);
      StringRef COMDATSymName = Sym->getName();

      if (const auto *F = dyn_cast<Function>(GO))
        if (std::optional<StringRef> Prefix = F->getSectionPrefix())
          raw_svector_ostream(Name) << '$' << *Prefix;

      // Append "$symbol" to the section name *before* IR-level mangling is
      // applied when targetting mingw. This is what GCC does, and the ld.bfd
      // COFF linker will not properly handle comdats otherwise.
      if (getContext().getTargetTriple().isWindowsGNUEnvironment())
        raw_svector_ostream(Name) << '$' << ComdatGV->getName();

      return getContext().getCOFFSection(Name, Characteristics, COMDATSymName,
                                         Selection, UniqueID);
    } else {
      SmallString<256> TmpData;
      getMangler().getNameWithPrefix(TmpData, GO, /*CannotUsePrivateLabel=*/true);
      return getContext().getCOFFSection(Name, Characteristics, TmpData,
                                         Selection, UniqueID);
    }
  }

  if (Kind.isText())
    return TextSection;

  if (Kind.isThreadLocal())
    return TLSDataSection;

  if (Kind.isReadOnly() || Kind.isReadOnlyWithRel())
    return ReadOnlySection;

  // Note: we claim that common symbols are put in BSSSection, but they are
  // really emitted with the magic .comm directive, which creates a symbol table
  // entry but not a section.
  if (Kind.isBSS() || Kind.isCommon())
    return BSSSection;

  return DataSection;
}

void TargetLoweringObjectFileCOFF::getNameWithPrefix(
    SmallVectorImpl<char> &OutName, const GlobalValue *GV,
    const TargetMachine &TM) const {
  bool CannotUsePrivateLabel = false;
  if (GV->hasPrivateLinkage() &&
      ((isa<Function>(GV) && TM.getFunctionSections()) ||
       (isa<GlobalVariable>(GV) && TM.getDataSections())))
    CannotUsePrivateLabel = true;

  getMangler().getNameWithPrefix(OutName, GV, CannotUsePrivateLabel);
}

MCSection *TargetLoweringObjectFileCOFF::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  // If the function can be removed, produce a unique section so that
  // the table doesn't prevent the removal.
  const Comdat *C = F.getComdat();
  bool EmitUniqueSection = TM.getFunctionSections() || C;
  if (!EmitUniqueSection)
    return ReadOnlySection;

  // FIXME: we should produce a symbol for F instead.
  if (F.hasPrivateLinkage())
    return ReadOnlySection;

  MCSymbol *Sym = TM.getSymbol(&F);
  StringRef COMDATSymName = Sym->getName();

  SectionKind Kind = SectionKind::getReadOnly();
  StringRef SecName = getCOFFSectionNameForUniqueGlobal(Kind);
  unsigned Characteristics = getCOFFSectionFlags(Kind, TM);
  Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
  unsigned UniqueID = NextUniqueID++;

  return getContext().getCOFFSection(SecName, Characteristics, COMDATSymName,
                                     COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE,
                                     UniqueID);
}

bool TargetLoweringObjectFileCOFF::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  if (TM->getTargetTriple().getArch() == Triple::x86_64) {
    if (!JumpTableInFunctionSection) {
      // We can always create relative relocations, so use another section
      // that can be marked non-executable.
      return false;
    }
  }
  return TargetLoweringObjectFile::shouldPutJumpTableInFunctionSection(
    UsesLabelDifference, F);
}

void TargetLoweringObjectFileCOFF::emitModuleMetadata(MCStreamer &Streamer,
                                                      Module &M) const {
  emitLinkerDirectives(Streamer, M);

  unsigned Version = 0;
  unsigned Flags = 0;
  StringRef Section;

  GetObjCImageInfo(M, Version, Flags, Section);
  if (!Section.empty()) {
    auto &C = getContext();
    auto *S = C.getCOFFSection(Section, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                            COFF::IMAGE_SCN_MEM_READ);
    Streamer.switchSection(S);
    Streamer.emitLabel(C.getOrCreateSymbol(StringRef("OBJC_IMAGE_INFO")));
    Streamer.emitInt32(Version);
    Streamer.emitInt32(Flags);
    Streamer.addBlankLine();
  }

  emitCGProfileMetadata(Streamer, M);
}

void TargetLoweringObjectFileCOFF::emitLinkerDirectives(
    MCStreamer &Streamer, Module &M) const {
  if (NamedMDNode *LinkerOptions = M.getNamedMetadata("llvm.linker.options")) {
    // Emit the linker options to the linker .drectve section.  According to the
    // spec, this section is a space-separated string containing flags for
    // linker.
    MCSection *Sec = getDrectveSection();
    Streamer.switchSection(Sec);
    for (const auto *Option : LinkerOptions->operands()) {
      for (const auto &Piece : cast<MDNode>(Option)->operands()) {
        // Lead with a space for consistency with our dllexport implementation.
        std::string Directive(" ");
        Directive.append(std::string(cast<MDString>(Piece)->getString()));
        Streamer.emitBytes(Directive);
      }
    }
  }

  // Emit /EXPORT: flags for each exported global as necessary.
  std::string Flags;
  for (const GlobalValue &GV : M.global_values()) {
    raw_string_ostream OS(Flags);
    emitLinkerFlagsForGlobalCOFF(OS, &GV, getContext().getTargetTriple(),
                                 getMangler());
    OS.flush();
    if (!Flags.empty()) {
      Streamer.switchSection(getDrectveSection());
      Streamer.emitBytes(Flags);
    }
    Flags.clear();
  }

  // Emit /INCLUDE: flags for each used global as necessary.
  if (const auto *LU = M.getNamedGlobal("llvm.used")) {
    assert(LU->hasInitializer() && "expected llvm.used to have an initializer");
    assert(isa<ArrayType>(LU->getValueType()) &&
           "expected llvm.used to be an array type");
    if (const auto *A = cast<ConstantArray>(LU->getInitializer())) {
      for (const Value *Op : A->operands()) {
        const auto *GV = cast<GlobalValue>(Op->stripPointerCasts());
        // Global symbols with internal or private linkage are not visible to
        // the linker, and thus would cause an error when the linker tried to
        // preserve the symbol due to the `/include:` directive.
        if (GV->hasLocalLinkage())
          continue;

        raw_string_ostream OS(Flags);
        emitLinkerFlagsForUsedCOFF(OS, GV, getContext().getTargetTriple(),
                                   getMangler());
        OS.flush();

        if (!Flags.empty()) {
          Streamer.switchSection(getDrectveSection());
          Streamer.emitBytes(Flags);
        }
        Flags.clear();
      }
    }
  }
}

void TargetLoweringObjectFileCOFF::Initialize(MCContext &Ctx,
                                              const TargetMachine &TM) {
  TargetLoweringObjectFile::Initialize(Ctx, TM);
  this->TM = &TM;
  const Triple &T = TM.getTargetTriple();
  if (T.isWindowsMSVCEnvironment() || T.isWindowsItaniumEnvironment()) {
    StaticCtorSection =
        Ctx.getCOFFSection(".CRT$XCU", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ);
    StaticDtorSection =
        Ctx.getCOFFSection(".CRT$XTX", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                           COFF::IMAGE_SCN_MEM_READ);
  } else {
    StaticCtorSection = Ctx.getCOFFSection(
        ".ctors", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE);
    StaticDtorSection = Ctx.getCOFFSection(
        ".dtors", COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                      COFF::IMAGE_SCN_MEM_READ | COFF::IMAGE_SCN_MEM_WRITE);
  }
}

static MCSectionCOFF *getCOFFStaticStructorSection(MCContext &Ctx,
                                                   const Triple &T, bool IsCtor,
                                                   unsigned Priority,
                                                   const MCSymbol *KeySym,
                                                   MCSectionCOFF *Default) {
  if (T.isWindowsMSVCEnvironment() || T.isWindowsItaniumEnvironment()) {
    // If the priority is the default, use .CRT$XCU, possibly associative.
    if (Priority == 65535)
      return Ctx.getAssociativeCOFFSection(Default, KeySym, 0);

    // Otherwise, we need to compute a new section name. Low priorities should
    // run earlier. The linker will sort sections ASCII-betically, and we need a
    // string that sorts between .CRT$XCA and .CRT$XCU. In the general case, we
    // make a name like ".CRT$XCT12345", since that runs before .CRT$XCU. Really
    // low priorities need to sort before 'L', since the CRT uses that
    // internally, so we use ".CRT$XCA00001" for them. We have a contract with
    // the frontend that "init_seg(compiler)" corresponds to priority 200 and
    // "init_seg(lib)" corresponds to priority 400, and those respectively use
    // 'C' and 'L' without the priority suffix. Priorities between 200 and 400
    // use 'C' with the priority as a suffix.
    SmallString<24> Name;
    char LastLetter = 'T';
    bool AddPrioritySuffix = Priority != 200 && Priority != 400;
    if (Priority < 200)
      LastLetter = 'A';
    else if (Priority < 400)
      LastLetter = 'C';
    else if (Priority == 400)
      LastLetter = 'L';
    raw_svector_ostream OS(Name);
    OS << ".CRT$X" << (IsCtor ? "C" : "T") << LastLetter;
    if (AddPrioritySuffix)
      OS << format("%05u", Priority);
    MCSectionCOFF *Sec = Ctx.getCOFFSection(
        Name, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA | COFF::IMAGE_SCN_MEM_READ);
    return Ctx.getAssociativeCOFFSection(Sec, KeySym, 0);
  }

  std::string Name = IsCtor ? ".ctors" : ".dtors";
  if (Priority != 65535)
    raw_string_ostream(Name) << format(".%05u", 65535 - Priority);

  return Ctx.getAssociativeCOFFSection(
      Ctx.getCOFFSection(Name, COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                   COFF::IMAGE_SCN_MEM_READ |
                                   COFF::IMAGE_SCN_MEM_WRITE),
      KeySym, 0);
}

MCSection *TargetLoweringObjectFileCOFF::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getCOFFStaticStructorSection(
      getContext(), getContext().getTargetTriple(), true, Priority, KeySym,
      cast<MCSectionCOFF>(StaticCtorSection));
}

MCSection *TargetLoweringObjectFileCOFF::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return getCOFFStaticStructorSection(
      getContext(), getContext().getTargetTriple(), false, Priority, KeySym,
      cast<MCSectionCOFF>(StaticDtorSection));
}

const MCExpr *TargetLoweringObjectFileCOFF::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  const Triple &T = TM.getTargetTriple();
  if (T.isOSCygMing())
    return nullptr;

  // Our symbols should exist in address space zero, cowardly no-op if
  // otherwise.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0)
    return nullptr;

  // Both ptrtoint instructions must wrap global objects:
  // - Only global variables are eligible for image relative relocations.
  // - The subtrahend refers to the special symbol __ImageBase, a GlobalVariable.
  // We expect __ImageBase to be a global variable without a section, externally
  // defined.
  //
  // It should look something like this: @__ImageBase = external constant i8
  if (!isa<GlobalObject>(LHS) || !isa<GlobalVariable>(RHS) ||
      LHS->isThreadLocal() || RHS->isThreadLocal() ||
      RHS->getName() != "__ImageBase" || !RHS->hasExternalLinkage() ||
      cast<GlobalVariable>(RHS)->hasInitializer() || RHS->hasSection())
    return nullptr;

  return MCSymbolRefExpr::create(TM.getSymbol(LHS),
                                 MCSymbolRefExpr::VK_COFF_IMGREL32,
                                 getContext());
}

static std::string APIntToHexString(const APInt &AI) {
  unsigned Width = (AI.getBitWidth() / 8) * 2;
  std::string HexString = toString(AI, 16, /*Signed=*/false);
  llvm::transform(HexString, HexString.begin(), tolower);
  unsigned Size = HexString.size();
  assert(Width >= Size && "hex string is too large!");
  HexString.insert(HexString.begin(), Width - Size, '0');

  return HexString;
}

static std::string scalarConstantToHexString(const Constant *C) {
  Type *Ty = C->getType();
  if (isa<UndefValue>(C)) {
    return APIntToHexString(APInt::getZero(Ty->getPrimitiveSizeInBits()));
  } else if (const auto *CFP = dyn_cast<ConstantFP>(C)) {
    return APIntToHexString(CFP->getValueAPF().bitcastToAPInt());
  } else if (const auto *CI = dyn_cast<ConstantInt>(C)) {
    return APIntToHexString(CI->getValue());
  } else {
    unsigned NumElements;
    if (auto *VTy = dyn_cast<VectorType>(Ty))
      NumElements = cast<FixedVectorType>(VTy)->getNumElements();
    else
      NumElements = Ty->getArrayNumElements();
    std::string HexString;
    for (int I = NumElements - 1, E = -1; I != E; --I)
      HexString += scalarConstantToHexString(C->getAggregateElement(I));
    return HexString;
  }
}

MCSection *TargetLoweringObjectFileCOFF::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  if (Kind.isMergeableConst() && C &&
      getContext().getAsmInfo()->hasCOFFComdatConstants()) {
    // This creates comdat sections with the given symbol name, but unless
    // AsmPrinter::GetCPISymbol actually makes the symbol global, the symbol
    // will be created with a null storage class, which makes GNU binutils
    // error out.
    const unsigned Characteristics = COFF::IMAGE_SCN_CNT_INITIALIZED_DATA |
                                     COFF::IMAGE_SCN_MEM_READ |
                                     COFF::IMAGE_SCN_LNK_COMDAT;
    std::string COMDATSymName;
    if (Kind.isMergeableConst4()) {
      if (Alignment <= 4) {
        COMDATSymName = "__real@" + scalarConstantToHexString(C);
        Alignment = Align(4);
      }
    } else if (Kind.isMergeableConst8()) {
      if (Alignment <= 8) {
        COMDATSymName = "__real@" + scalarConstantToHexString(C);
        Alignment = Align(8);
      }
    } else if (Kind.isMergeableConst16()) {
      // FIXME: These may not be appropriate for non-x86 architectures.
      if (Alignment <= 16) {
        COMDATSymName = "__xmm@" + scalarConstantToHexString(C);
        Alignment = Align(16);
      }
    } else if (Kind.isMergeableConst32()) {
      if (Alignment <= 32) {
        COMDATSymName = "__ymm@" + scalarConstantToHexString(C);
        Alignment = Align(32);
      }
    }

    if (!COMDATSymName.empty())
      return getContext().getCOFFSection(".rdata", Characteristics,
                                         COMDATSymName,
                                         COFF::IMAGE_COMDAT_SELECT_ANY);
  }

  return TargetLoweringObjectFile::getSectionForConstant(DL, Kind, C,
                                                         Alignment);
}

//===----------------------------------------------------------------------===//
//                                  Wasm
//===----------------------------------------------------------------------===//

static const Comdat *getWasmComdat(const GlobalValue *GV) {
  const Comdat *C = GV->getComdat();
  if (!C)
    return nullptr;

  if (C->getSelectionKind() != Comdat::Any)
    report_fatal_error("WebAssembly COMDATs only support "
                       "SelectionKind::Any, '" + C->getName() + "' cannot be "
                       "lowered.");

  return C;
}

static unsigned getWasmSectionFlags(SectionKind K, bool Retain) {
  unsigned Flags = 0;

  if (K.isThreadLocal())
    Flags |= wasm::WASM_SEG_FLAG_TLS;

  if (K.isMergeableCString())
    Flags |= wasm::WASM_SEG_FLAG_STRINGS;

  if (Retain)
    Flags |= wasm::WASM_SEG_FLAG_RETAIN;

  // TODO(sbc): Add suport for K.isMergeableConst()

  return Flags;
}

void TargetLoweringObjectFileWasm::getModuleMetadata(Module &M) {
  SmallVector<GlobalValue *, 4> Vec;
  collectUsedGlobalVariables(M, Vec, false);
  for (GlobalValue *GV : Vec)
    if (auto *GO = dyn_cast<GlobalObject>(GV))
      Used.insert(GO);
}

MCSection *TargetLoweringObjectFileWasm::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // We don't support explict section names for functions in the wasm object
  // format.  Each function has to be in its own unique section.
  if (isa<Function>(GO)) {
    return SelectSectionForGlobal(GO, Kind, TM);
  }

  StringRef Name = GO->getSection();

  // Certain data sections we treat as named custom sections rather than
  // segments within the data section.
  // This could be avoided if all data segements (the wasm sense) were
  // represented as their own sections (in the llvm sense).
  // TODO(sbc): https://github.com/WebAssembly/tool-conventions/issues/138
  if (Name == ".llvmcmd" || Name == ".llvmbc")
    Kind = SectionKind::getMetadata();

  StringRef Group = "";
  if (const Comdat *C = getWasmComdat(GO)) {
    Group = C->getName();
  }

  unsigned Flags = getWasmSectionFlags(Kind, Used.count(GO));
  MCSectionWasm *Section = getContext().getWasmSection(
      Name, Kind, Flags, Group, MCContext::GenericSectionID);

  return Section;
}

static MCSectionWasm *
selectWasmSectionForGlobal(MCContext &Ctx, const GlobalObject *GO,
                           SectionKind Kind, Mangler &Mang,
                           const TargetMachine &TM, bool EmitUniqueSection,
                           unsigned *NextUniqueID, bool Retain) {
  StringRef Group = "";
  if (const Comdat *C = getWasmComdat(GO)) {
    Group = C->getName();
  }

  bool UniqueSectionNames = TM.getUniqueSectionNames();
  SmallString<128> Name = getSectionPrefixForGlobal(Kind, /*IsLarge=*/false);

  if (const auto *F = dyn_cast<Function>(GO)) {
    const auto &OptionalPrefix = F->getSectionPrefix();
    if (OptionalPrefix)
      raw_svector_ostream(Name) << '.' << *OptionalPrefix;
  }

  if (EmitUniqueSection && UniqueSectionNames) {
    Name.push_back('.');
    TM.getNameWithPrefix(Name, GO, Mang, true);
  }
  unsigned UniqueID = MCContext::GenericSectionID;
  if (EmitUniqueSection && !UniqueSectionNames) {
    UniqueID = *NextUniqueID;
    (*NextUniqueID)++;
  }

  unsigned Flags = getWasmSectionFlags(Kind, Retain);
  return Ctx.getWasmSection(Name, Kind, Flags, Group, UniqueID);
}

MCSection *TargetLoweringObjectFileWasm::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {

  if (Kind.isCommon())
    report_fatal_error("mergable sections not supported yet on wasm");

  // If we have -ffunction-section or -fdata-section then we should emit the
  // global value to a uniqued section specifically for it.
  bool EmitUniqueSection = false;
  if (Kind.isText())
    EmitUniqueSection = TM.getFunctionSections();
  else
    EmitUniqueSection = TM.getDataSections();
  EmitUniqueSection |= GO->hasComdat();
  bool Retain = Used.count(GO);
  EmitUniqueSection |= Retain;

  return selectWasmSectionForGlobal(getContext(), GO, Kind, getMangler(), TM,
                                    EmitUniqueSection, &NextUniqueID, Retain);
}

bool TargetLoweringObjectFileWasm::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  // We can always create relative relocations, so use another section
  // that can be marked non-executable.
  return false;
}

const MCExpr *TargetLoweringObjectFileWasm::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  // We may only use a PLT-relative relocation to refer to unnamed_addr
  // functions.
  if (!LHS->hasGlobalUnnamedAddr() || !LHS->getValueType()->isFunctionTy())
    return nullptr;

  // Basic correctness checks.
  if (LHS->getType()->getPointerAddressSpace() != 0 ||
      RHS->getType()->getPointerAddressSpace() != 0 || LHS->isThreadLocal() ||
      RHS->isThreadLocal())
    return nullptr;

  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(TM.getSymbol(LHS), MCSymbolRefExpr::VK_None,
                              getContext()),
      MCSymbolRefExpr::create(TM.getSymbol(RHS), getContext()), getContext());
}

void TargetLoweringObjectFileWasm::InitializeWasm() {
  StaticCtorSection =
      getContext().getWasmSection(".init_array", SectionKind::getData());

  // We don't use PersonalityEncoding and LSDAEncoding because we don't emit
  // .cfi directives. We use TTypeEncoding to encode typeinfo global variables.
  TTypeEncoding = dwarf::DW_EH_PE_absptr;
}

MCSection *TargetLoweringObjectFileWasm::getStaticCtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  return Priority == UINT16_MAX ?
         StaticCtorSection :
         getContext().getWasmSection(".init_array." + utostr(Priority),
                                     SectionKind::getData());
}

MCSection *TargetLoweringObjectFileWasm::getStaticDtorSection(
    unsigned Priority, const MCSymbol *KeySym) const {
  report_fatal_error("@llvm.global_dtors should have been lowered already");
}

//===----------------------------------------------------------------------===//
//                                  XCOFF
//===----------------------------------------------------------------------===//
bool TargetLoweringObjectFileXCOFF::ShouldEmitEHBlock(
    const MachineFunction *MF) {
  if (!MF->getLandingPads().empty())
    return true;

  const Function &F = MF->getFunction();
  if (!F.hasPersonalityFn() || !F.needsUnwindTableEntry())
    return false;

  const GlobalValue *Per =
      dyn_cast<GlobalValue>(F.getPersonalityFn()->stripPointerCasts());
  assert(Per && "Personality routine is not a GlobalValue type.");
  if (isNoOpWithoutInvoke(classifyEHPersonality(Per)))
    return false;

  return true;
}

bool TargetLoweringObjectFileXCOFF::ShouldSetSSPCanaryBitInTB(
    const MachineFunction *MF) {
  const Function &F = MF->getFunction();
  if (!F.hasStackProtectorFnAttr())
    return false;
  // FIXME: check presence of canary word
  // There are cases that the stack protectors are not really inserted even if
  // the attributes are on.
  return true;
}

MCSymbol *
TargetLoweringObjectFileXCOFF::getEHInfoTableSymbol(const MachineFunction *MF) {
  MCSymbol *EHInfoSym = MF->getContext().getOrCreateSymbol(
      "__ehinfo." + Twine(MF->getFunctionNumber()));
  cast<MCSymbolXCOFF>(EHInfoSym)->setEHInfo();
  return EHInfoSym;
}

MCSymbol *
TargetLoweringObjectFileXCOFF::getTargetSymbol(const GlobalValue *GV,
                                               const TargetMachine &TM) const {
  // We always use a qualname symbol for a GV that represents
  // a declaration, a function descriptor, or a common symbol.
  // If a GV represents a GlobalVariable and -fdata-sections is enabled, we
  // also return a qualname so that a label symbol could be avoided.
  // It is inherently ambiguous when the GO represents the address of a
  // function, as the GO could either represent a function descriptor or a
  // function entry point. We choose to always return a function descriptor
  // here.
  if (const GlobalObject *GO = dyn_cast<GlobalObject>(GV)) {
    if (GO->isDeclarationForLinker())
      return cast<MCSectionXCOFF>(getSectionForExternalReference(GO, TM))
          ->getQualNameSymbol();

    if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV))
      if (GVar->hasAttribute("toc-data"))
        return cast<MCSectionXCOFF>(
                   SectionForGlobal(GVar, SectionKind::getData(), TM))
            ->getQualNameSymbol();

    SectionKind GOKind = getKindForGlobal(GO, TM);
    if (GOKind.isText())
      return cast<MCSectionXCOFF>(
                 getSectionForFunctionDescriptor(cast<Function>(GO), TM))
          ->getQualNameSymbol();
    if ((TM.getDataSections() && !GO->hasSection()) || GO->hasCommonLinkage() ||
        GOKind.isBSSLocal() || GOKind.isThreadBSSLocal())
      return cast<MCSectionXCOFF>(SectionForGlobal(GO, GOKind, TM))
          ->getQualNameSymbol();
  }

  // For all other cases, fall back to getSymbol to return the unqualified name.
  return nullptr;
}

MCSection *TargetLoweringObjectFileXCOFF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  if (!GO->hasSection())
    report_fatal_error("#pragma clang section is not yet supported");

  StringRef SectionName = GO->getSection();

  // Handle the XCOFF::TD case first, then deal with the rest.
  if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GO))
    if (GVar->hasAttribute("toc-data"))
      return getContext().getXCOFFSection(
          SectionName, Kind,
          XCOFF::CsectProperties(/*MappingClass*/ XCOFF::XMC_TD, XCOFF::XTY_SD),
          /* MultiSymbolsAllowed*/ true);

  XCOFF::StorageMappingClass MappingClass;
  if (Kind.isText())
    MappingClass = XCOFF::XMC_PR;
  else if (Kind.isData() || Kind.isBSS())
    MappingClass = XCOFF::XMC_RW;
  else if (Kind.isReadOnlyWithRel())
    MappingClass =
        TM.Options.XCOFFReadOnlyPointers ? XCOFF::XMC_RO : XCOFF::XMC_RW;
  else if (Kind.isReadOnly())
    MappingClass = XCOFF::XMC_RO;
  else
    report_fatal_error("XCOFF other section types not yet implemented.");

  return getContext().getXCOFFSection(
      SectionName, Kind, XCOFF::CsectProperties(MappingClass, XCOFF::XTY_SD),
      /* MultiSymbolsAllowed*/ true);
}

MCSection *TargetLoweringObjectFileXCOFF::getSectionForExternalReference(
    const GlobalObject *GO, const TargetMachine &TM) const {
  assert(GO->isDeclarationForLinker() &&
         "Tried to get ER section for a defined global.");

  SmallString<128> Name;
  getNameWithPrefix(Name, GO, TM);

  // AIX TLS local-dynamic does not need the external reference for the
  // "_$TLSML" symbol.
  if (GO->getThreadLocalMode() == GlobalVariable::LocalDynamicTLSModel &&
      GO->hasName() && GO->getName() == "_$TLSML") {
    return getContext().getXCOFFSection(
        Name, SectionKind::getData(),
        XCOFF::CsectProperties(XCOFF::XMC_TC, XCOFF::XTY_SD));
  }

  XCOFF::StorageMappingClass SMC =
      isa<Function>(GO) ? XCOFF::XMC_DS : XCOFF::XMC_UA;
  if (GO->isThreadLocal())
    SMC = XCOFF::XMC_UL;

  if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GO))
    if (GVar->hasAttribute("toc-data"))
      SMC = XCOFF::XMC_TD;

  // Externals go into a csect of type ER.
  return getContext().getXCOFFSection(
      Name, SectionKind::getMetadata(),
      XCOFF::CsectProperties(SMC, XCOFF::XTY_ER));
}

MCSection *TargetLoweringObjectFileXCOFF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Handle the XCOFF::TD case first, then deal with the rest.
  if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GO))
    if (GVar->hasAttribute("toc-data")) {
      SmallString<128> Name;
      getNameWithPrefix(Name, GO, TM);
      XCOFF::SymbolType symType =
          GO->hasCommonLinkage() ? XCOFF::XTY_CM : XCOFF::XTY_SD;
      return getContext().getXCOFFSection(
          Name, Kind, XCOFF::CsectProperties(XCOFF::XMC_TD, symType),
          /* MultiSymbolsAllowed*/ true);
    }

  // Common symbols go into a csect with matching name which will get mapped
  // into the .bss section.
  // Zero-initialized local TLS symbols go into a csect with matching name which
  // will get mapped into the .tbss section.
  if (Kind.isBSSLocal() || GO->hasCommonLinkage() || Kind.isThreadBSSLocal()) {
    SmallString<128> Name;
    getNameWithPrefix(Name, GO, TM);
    XCOFF::StorageMappingClass SMC = Kind.isBSSLocal() ? XCOFF::XMC_BS
                                     : Kind.isCommon() ? XCOFF::XMC_RW
                                                       : XCOFF::XMC_UL;
    return getContext().getXCOFFSection(
        Name, Kind, XCOFF::CsectProperties(SMC, XCOFF::XTY_CM));
  }

  if (Kind.isText()) {
    if (TM.getFunctionSections()) {
      return cast<MCSymbolXCOFF>(getFunctionEntryPointSymbol(GO, TM))
          ->getRepresentedCsect();
    }
    return TextSection;
  }

  if (TM.Options.XCOFFReadOnlyPointers && Kind.isReadOnlyWithRel()) {
    if (!TM.getDataSections())
      report_fatal_error(
          "ReadOnlyPointers is supported only if data sections is turned on");

    SmallString<128> Name;
    getNameWithPrefix(Name, GO, TM);
    return getContext().getXCOFFSection(
        Name, SectionKind::getReadOnly(),
        XCOFF::CsectProperties(XCOFF::XMC_RO, XCOFF::XTY_SD));
  }

  // For BSS kind, zero initialized data must be emitted to the .data section
  // because external linkage control sections that get mapped to the .bss
  // section will be linked as tentative defintions, which is only appropriate
  // for SectionKind::Common.
  if (Kind.isData() || Kind.isReadOnlyWithRel() || Kind.isBSS()) {
    if (TM.getDataSections()) {
      SmallString<128> Name;
      getNameWithPrefix(Name, GO, TM);
      return getContext().getXCOFFSection(
          Name, SectionKind::getData(),
          XCOFF::CsectProperties(XCOFF::XMC_RW, XCOFF::XTY_SD));
    }
    return DataSection;
  }

  if (Kind.isReadOnly()) {
    if (TM.getDataSections()) {
      SmallString<128> Name;
      getNameWithPrefix(Name, GO, TM);
      return getContext().getXCOFFSection(
          Name, SectionKind::getReadOnly(),
          XCOFF::CsectProperties(XCOFF::XMC_RO, XCOFF::XTY_SD));
    }
    return ReadOnlySection;
  }

  // External/weak TLS data and initialized local TLS data are not eligible
  // to be put into common csect. If data sections are enabled, thread
  // data are emitted into separate sections. Otherwise, thread data
  // are emitted into the .tdata section.
  if (Kind.isThreadLocal()) {
    if (TM.getDataSections()) {
      SmallString<128> Name;
      getNameWithPrefix(Name, GO, TM);
      return getContext().getXCOFFSection(
          Name, Kind, XCOFF::CsectProperties(XCOFF::XMC_TL, XCOFF::XTY_SD));
    }
    return TLSDataSection;
  }

  report_fatal_error("XCOFF other section types not yet implemented.");
}

MCSection *TargetLoweringObjectFileXCOFF::getSectionForJumpTable(
    const Function &F, const TargetMachine &TM) const {
  assert (!F.getComdat() && "Comdat not supported on XCOFF.");

  if (!TM.getFunctionSections())
    return ReadOnlySection;

  // If the function can be removed, produce a unique section so that
  // the table doesn't prevent the removal.
  SmallString<128> NameStr(".rodata.jmp..");
  getNameWithPrefix(NameStr, &F, TM);
  return getContext().getXCOFFSection(
      NameStr, SectionKind::getReadOnly(),
      XCOFF::CsectProperties(XCOFF::XMC_RO, XCOFF::XTY_SD));
}

bool TargetLoweringObjectFileXCOFF::shouldPutJumpTableInFunctionSection(
    bool UsesLabelDifference, const Function &F) const {
  return false;
}

/// Given a mergeable constant with the specified size and relocation
/// information, return a section that it should be placed in.
MCSection *TargetLoweringObjectFileXCOFF::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  // TODO: Enable emiting constant pool to unique sections when we support it.
  if (Alignment > Align(16))
    report_fatal_error("Alignments greater than 16 not yet supported.");

  if (Alignment == Align(8)) {
    assert(ReadOnly8Section && "Section should always be initialized.");
    return ReadOnly8Section;
  }

  if (Alignment == Align(16)) {
    assert(ReadOnly16Section && "Section should always be initialized.");
    return ReadOnly16Section;
  }

  return ReadOnlySection;
}

void TargetLoweringObjectFileXCOFF::Initialize(MCContext &Ctx,
                                               const TargetMachine &TgtM) {
  TargetLoweringObjectFile::Initialize(Ctx, TgtM);
  TTypeEncoding =
      dwarf::DW_EH_PE_indirect | dwarf::DW_EH_PE_datarel |
      (TgtM.getTargetTriple().isArch32Bit() ? dwarf::DW_EH_PE_sdata4
                                            : dwarf::DW_EH_PE_sdata8);
  PersonalityEncoding = 0;
  LSDAEncoding = 0;
  CallSiteEncoding = dwarf::DW_EH_PE_udata4;

  // AIX debug for thread local location is not ready. And for integrated as
  // mode, the relocatable address for the thread local variable will cause
  // linker error. So disable the location attribute generation for thread local
  // variables for now.
  // FIXME: when TLS debug on AIX is ready, remove this setting.
  SupportDebugThreadLocalLocation = false;
}

MCSection *TargetLoweringObjectFileXCOFF::getStaticCtorSection(
	unsigned Priority, const MCSymbol *KeySym) const {
  report_fatal_error("no static constructor section on AIX");
}

MCSection *TargetLoweringObjectFileXCOFF::getStaticDtorSection(
	unsigned Priority, const MCSymbol *KeySym) const {
  report_fatal_error("no static destructor section on AIX");
}

const MCExpr *TargetLoweringObjectFileXCOFF::lowerRelativeReference(
    const GlobalValue *LHS, const GlobalValue *RHS,
    const TargetMachine &TM) const {
  /* Not implemented yet, but don't crash, return nullptr. */
  return nullptr;
}

XCOFF::StorageClass
TargetLoweringObjectFileXCOFF::getStorageClassForGlobal(const GlobalValue *GV) {
  assert(!isa<GlobalIFunc>(GV) && "GlobalIFunc is not supported on AIX.");

  switch (GV->getLinkage()) {
  case GlobalValue::InternalLinkage:
  case GlobalValue::PrivateLinkage:
    return XCOFF::C_HIDEXT;
  case GlobalValue::ExternalLinkage:
  case GlobalValue::CommonLinkage:
  case GlobalValue::AvailableExternallyLinkage:
    return XCOFF::C_EXT;
  case GlobalValue::ExternalWeakLinkage:
  case GlobalValue::LinkOnceAnyLinkage:
  case GlobalValue::LinkOnceODRLinkage:
  case GlobalValue::WeakAnyLinkage:
  case GlobalValue::WeakODRLinkage:
    return XCOFF::C_WEAKEXT;
  case GlobalValue::AppendingLinkage:
    report_fatal_error(
        "There is no mapping that implements AppendingLinkage for XCOFF.");
  }
  llvm_unreachable("Unknown linkage type!");
}

MCSymbol *TargetLoweringObjectFileXCOFF::getFunctionEntryPointSymbol(
    const GlobalValue *Func, const TargetMachine &TM) const {
  assert((isa<Function>(Func) ||
          (isa<GlobalAlias>(Func) &&
           isa_and_nonnull<Function>(
               cast<GlobalAlias>(Func)->getAliaseeObject()))) &&
         "Func must be a function or an alias which has a function as base "
         "object.");

  SmallString<128> NameStr;
  NameStr.push_back('.');
  getNameWithPrefix(NameStr, Func, TM);

  // When -function-sections is enabled and explicit section is not specified,
  // it's not necessary to emit function entry point label any more. We will use
  // function entry point csect instead. And for function delcarations, the
  // undefined symbols gets treated as csect with XTY_ER property.
  if (((TM.getFunctionSections() && !Func->hasSection()) ||
       Func->isDeclarationForLinker()) &&
      isa<Function>(Func)) {
    return getContext()
        .getXCOFFSection(
            NameStr, SectionKind::getText(),
            XCOFF::CsectProperties(XCOFF::XMC_PR, Func->isDeclarationForLinker()
                                                      ? XCOFF::XTY_ER
                                                      : XCOFF::XTY_SD))
        ->getQualNameSymbol();
  }

  return getContext().getOrCreateSymbol(NameStr);
}

MCSection *TargetLoweringObjectFileXCOFF::getSectionForFunctionDescriptor(
    const Function *F, const TargetMachine &TM) const {
  SmallString<128> NameStr;
  getNameWithPrefix(NameStr, F, TM);
  return getContext().getXCOFFSection(
      NameStr, SectionKind::getData(),
      XCOFF::CsectProperties(XCOFF::XMC_DS, XCOFF::XTY_SD));
}

MCSection *TargetLoweringObjectFileXCOFF::getSectionForTOCEntry(
    const MCSymbol *Sym, const TargetMachine &TM) const {
  const XCOFF::StorageMappingClass SMC = [](const MCSymbol *Sym,
                                            const TargetMachine &TM) {
    const MCSymbolXCOFF *XSym = cast<MCSymbolXCOFF>(Sym);

    // The "_$TLSML" symbol for TLS local-dynamic mode requires XMC_TC,
    // otherwise the AIX assembler will complain.
    if (XSym->getSymbolTableName() == "_$TLSML")
      return XCOFF::XMC_TC;

    // Use large code model toc entries for ehinfo symbols as they are
    // never referenced directly. The runtime loads their TOC entry
    // addresses from the trace-back table.
    if (XSym->isEHInfo())
      return XCOFF::XMC_TE;

    // If the symbol does not have a code model specified use the module value.
    if (!XSym->hasPerSymbolCodeModel())
      return TM.getCodeModel() == CodeModel::Large ? XCOFF::XMC_TE
                                                   : XCOFF::XMC_TC;

    return XSym->getPerSymbolCodeModel() == MCSymbolXCOFF::CM_Large
               ? XCOFF::XMC_TE
               : XCOFF::XMC_TC;
  }(Sym, TM);

  return getContext().getXCOFFSection(
      cast<MCSymbolXCOFF>(Sym)->getSymbolTableName(), SectionKind::getData(),
      XCOFF::CsectProperties(SMC, XCOFF::XTY_SD));
}

MCSection *TargetLoweringObjectFileXCOFF::getSectionForLSDA(
    const Function &F, const MCSymbol &FnSym, const TargetMachine &TM) const {
  auto *LSDA = cast<MCSectionXCOFF>(LSDASection);
  if (TM.getFunctionSections()) {
    // If option -ffunction-sections is on, append the function name to the
    // name of the LSDA csect so that each function has its own LSDA csect.
    // This helps the linker to garbage-collect EH info of unused functions.
    SmallString<128> NameStr = LSDA->getName();
    raw_svector_ostream(NameStr) << '.' << F.getName();
    LSDA = getContext().getXCOFFSection(NameStr, LSDA->getKind(),
                                        LSDA->getCsectProp());
  }
  return LSDA;
}
//===----------------------------------------------------------------------===//
//                                  GOFF
//===----------------------------------------------------------------------===//
TargetLoweringObjectFileGOFF::TargetLoweringObjectFileGOFF() = default;

MCSection *TargetLoweringObjectFileGOFF::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  return SelectSectionForGlobal(GO, Kind, TM);
}

MCSection *TargetLoweringObjectFileGOFF::getSectionForLSDA(
    const Function &F, const MCSymbol &FnSym, const TargetMachine &TM) const {
  std::string Name = ".gcc_exception_table." + F.getName().str();
  return getContext().getGOFFSection(Name, SectionKind::getData(), nullptr, 0);
}

MCSection *TargetLoweringObjectFileGOFF::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  auto *Symbol = TM.getSymbol(GO);
  if (Kind.isBSS())
    return getContext().getGOFFSection(Symbol->getName(), SectionKind::getBSS(),
                                       nullptr, 0);

  return getContext().getObjectFileInfo()->getTextSection();
}
