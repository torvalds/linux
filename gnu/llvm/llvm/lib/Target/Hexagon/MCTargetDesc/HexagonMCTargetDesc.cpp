//===-- HexagonMCTargetDesc.cpp - Hexagon Target Descriptions -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides Hexagon specific target descriptions.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/HexagonMCTargetDesc.h"
#include "HexagonDepArch.h"
#include "HexagonTargetStreamer.h"
#include "MCTargetDesc/HexagonInstPrinter.h"
#include "MCTargetDesc/HexagonMCAsmInfo.h"
#include "MCTargetDesc/HexagonMCELFStreamer.h"
#include "MCTargetDesc/HexagonMCInstrInfo.h"
#include "TargetInfo/HexagonTargetInfo.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/HexagonAttributes.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>

using namespace llvm;

#define GET_INSTRINFO_MC_DESC
#define ENABLE_INSTR_PREDICATE_VERIFIER
#include "HexagonGenInstrInfo.inc"

#define GET_SUBTARGETINFO_MC_DESC
#include "HexagonGenSubtargetInfo.inc"

#define GET_REGINFO_MC_DESC
#include "HexagonGenRegisterInfo.inc"

cl::opt<bool> llvm::HexagonDisableCompound
  ("mno-compound",
   cl::desc("Disable looking for compound instructions for Hexagon"));

cl::opt<bool> llvm::HexagonDisableDuplex
  ("mno-pairing",
   cl::desc("Disable looking for duplex instructions for Hexagon"));

namespace { // These flags are to be deprecated
cl::opt<bool> MV5("mv5", cl::Hidden, cl::desc("Build for Hexagon V5"),
                  cl::init(false));
cl::opt<bool> MV55("mv55", cl::Hidden, cl::desc("Build for Hexagon V55"),
                   cl::init(false));
cl::opt<bool> MV60("mv60", cl::Hidden, cl::desc("Build for Hexagon V60"),
                   cl::init(false));
cl::opt<bool> MV62("mv62", cl::Hidden, cl::desc("Build for Hexagon V62"),
                   cl::init(false));
cl::opt<bool> MV65("mv65", cl::Hidden, cl::desc("Build for Hexagon V65"),
                   cl::init(false));
cl::opt<bool> MV66("mv66", cl::Hidden, cl::desc("Build for Hexagon V66"),
                   cl::init(false));
cl::opt<bool> MV67("mv67", cl::Hidden, cl::desc("Build for Hexagon V67"),
                   cl::init(false));
cl::opt<bool> MV67T("mv67t", cl::Hidden, cl::desc("Build for Hexagon V67T"),
                    cl::init(false));
cl::opt<bool> MV68("mv68", cl::Hidden, cl::desc("Build for Hexagon V68"),
                   cl::init(false));
cl::opt<bool> MV69("mv69", cl::Hidden, cl::desc("Build for Hexagon V69"),
                   cl::init(false));
cl::opt<bool> MV71("mv71", cl::Hidden, cl::desc("Build for Hexagon V71"),
                   cl::init(false));
cl::opt<bool> MV71T("mv71t", cl::Hidden, cl::desc("Build for Hexagon V71T"),
                    cl::init(false));
cl::opt<bool> MV73("mv73", cl::Hidden, cl::desc("Build for Hexagon V73"),
                   cl::init(false));
} // namespace

cl::opt<Hexagon::ArchEnum> EnableHVX(
    "mhvx", cl::desc("Enable Hexagon Vector eXtensions"),
    cl::values(clEnumValN(Hexagon::ArchEnum::V60, "v60", "Build for HVX v60"),
               clEnumValN(Hexagon::ArchEnum::V62, "v62", "Build for HVX v62"),
               clEnumValN(Hexagon::ArchEnum::V65, "v65", "Build for HVX v65"),
               clEnumValN(Hexagon::ArchEnum::V66, "v66", "Build for HVX v66"),
               clEnumValN(Hexagon::ArchEnum::V67, "v67", "Build for HVX v67"),
               clEnumValN(Hexagon::ArchEnum::V68, "v68", "Build for HVX v68"),
               clEnumValN(Hexagon::ArchEnum::V69, "v69", "Build for HVX v69"),
               clEnumValN(Hexagon::ArchEnum::V71, "v71", "Build for HVX v71"),
               clEnumValN(Hexagon::ArchEnum::V73, "v73", "Build for HVX v73"),
               // Sentinel for no value specified.
               clEnumValN(Hexagon::ArchEnum::Generic, "", "")),
    // Sentinel for flag not present.
    cl::init(Hexagon::ArchEnum::NoArch), cl::ValueOptional);

static cl::opt<bool>
  DisableHVX("mno-hvx", cl::Hidden,
             cl::desc("Disable Hexagon Vector eXtensions"));

static cl::opt<bool>
    EnableHvxIeeeFp("mhvx-ieee-fp", cl::Hidden,
                    cl::desc("Enable HVX IEEE floating point extensions"));
static cl::opt<bool> EnableHexagonCabac
  ("mcabac", cl::desc("tbd"), cl::init(false));

static StringRef DefaultArch = "hexagonv60";

static StringRef HexagonGetArchVariant() {
  if (MV5)
    return "hexagonv5";
  if (MV55)
    return "hexagonv55";
  if (MV60)
    return "hexagonv60";
  if (MV62)
    return "hexagonv62";
  if (MV65)
    return "hexagonv65";
  if (MV66)
    return "hexagonv66";
  if (MV67)
    return "hexagonv67";
  if (MV67T)
    return "hexagonv67t";
  if (MV68)
    return "hexagonv68";
  if (MV69)
    return "hexagonv69";
  if (MV71)
    return "hexagonv71";
  if (MV71T)
    return "hexagonv71t";
  if (MV73)
    return "hexagonv73";
  return "";
}

StringRef Hexagon_MC::selectHexagonCPU(StringRef CPU) {
  StringRef ArchV = HexagonGetArchVariant();
  if (!ArchV.empty() && !CPU.empty()) {
    // Tiny cores have a "t" suffix that is discarded when creating a secondary
    // non-tiny subtarget.  See: addArchSubtarget
    std::pair<StringRef, StringRef> ArchP = ArchV.split('t');
    std::pair<StringRef, StringRef> CPUP = CPU.split('t');
    if (ArchP.first != CPUP.first)
      report_fatal_error("conflicting architectures specified.");
    return CPU;
  }
  if (ArchV.empty()) {
    if (CPU.empty())
      CPU = DefaultArch;
    return CPU;
  }
  return ArchV;
}

unsigned llvm::HexagonGetLastSlot() { return HexagonItinerariesV5FU::SLOT3; }

unsigned llvm::HexagonConvertUnits(unsigned ItinUnits, unsigned *Lanes) {
  enum {
    CVI_NONE = 0,
    CVI_XLANE = 1 << 0,
    CVI_SHIFT = 1 << 1,
    CVI_MPY0 = 1 << 2,
    CVI_MPY1 = 1 << 3,
    CVI_ZW = 1 << 4
  };

  if (ItinUnits == HexagonItinerariesV62FU::CVI_ALL ||
      ItinUnits == HexagonItinerariesV62FU::CVI_ALL_NOMEM)
    return (*Lanes = 4, CVI_XLANE);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_MPY01 &&
           ItinUnits & HexagonItinerariesV62FU::CVI_XLSHF)
    return (*Lanes = 2, CVI_XLANE | CVI_MPY0);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_MPY01)
    return (*Lanes = 2, CVI_MPY0);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_XLSHF)
    return (*Lanes = 2, CVI_XLANE);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_XLANE &&
           ItinUnits & HexagonItinerariesV62FU::CVI_SHIFT &&
           ItinUnits & HexagonItinerariesV62FU::CVI_MPY0 &&
           ItinUnits & HexagonItinerariesV62FU::CVI_MPY1)
    return (*Lanes = 1, CVI_XLANE | CVI_SHIFT | CVI_MPY0 | CVI_MPY1);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_XLANE &&
           ItinUnits & HexagonItinerariesV62FU::CVI_SHIFT)
    return (*Lanes = 1, CVI_XLANE | CVI_SHIFT);
  else if (ItinUnits & HexagonItinerariesV62FU::CVI_MPY0 &&
           ItinUnits & HexagonItinerariesV62FU::CVI_MPY1)
    return (*Lanes = 1, CVI_MPY0 | CVI_MPY1);
  else if (ItinUnits == HexagonItinerariesV62FU::CVI_ZW)
    return (*Lanes = 1, CVI_ZW);
  else if (ItinUnits == HexagonItinerariesV62FU::CVI_XLANE)
    return (*Lanes = 1, CVI_XLANE);
  else if (ItinUnits == HexagonItinerariesV62FU::CVI_SHIFT)
    return (*Lanes = 1, CVI_SHIFT);

  return (*Lanes = 0, CVI_NONE);
}


namespace llvm {
namespace HexagonFUnits {
bool isSlot0Only(unsigned units) {
  return HexagonItinerariesV62FU::SLOT0 == units;
}
} // namespace HexagonFUnits
} // namespace llvm

namespace {

class HexagonTargetAsmStreamer : public HexagonTargetStreamer {
  formatted_raw_ostream &OS;

public:
  HexagonTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS,
                           MCInstPrinter &IP)
      : HexagonTargetStreamer(S), OS(OS) {}

  void prettyPrintAsm(MCInstPrinter &InstPrinter, uint64_t Address,
                      const MCInst &Inst, const MCSubtargetInfo &STI,
                      raw_ostream &OS) override {
    assert(HexagonMCInstrInfo::isBundle(Inst));
    assert(HexagonMCInstrInfo::bundleSize(Inst) <= HEXAGON_PACKET_SIZE);
    std::string Buffer;
    {
      raw_string_ostream TempStream(Buffer);
      InstPrinter.printInst(&Inst, Address, "", STI, TempStream);
    }
    StringRef Contents(Buffer);
    auto PacketBundle = Contents.rsplit('\n');
    auto HeadTail = PacketBundle.first.split('\n');
    StringRef Separator = "\n";
    StringRef Indent = "\t";
    OS << "\t{\n";
    while (!HeadTail.first.empty()) {
      StringRef InstTxt;
      auto Duplex = HeadTail.first.split('\v');
      if (!Duplex.second.empty()) {
        OS << Indent << Duplex.first << Separator;
        InstTxt = Duplex.second;
      } else if (!HeadTail.first.trim().starts_with("immext")) {
        InstTxt = Duplex.first;
      }
      if (!InstTxt.empty())
        OS << Indent << InstTxt << Separator;
      HeadTail = HeadTail.second.split('\n');
    }

    if (HexagonMCInstrInfo::isMemReorderDisabled(Inst))
      OS << "\n\t} :mem_noshuf" << PacketBundle.second;
    else
      OS << "\t}" << PacketBundle.second;
  }

  void finish() override { finishAttributeSection(); }

  void finishAttributeSection() override {}

  void emitAttribute(unsigned Attribute, unsigned Value) override {
    OS << "\t.attribute\t" << Attribute << ", " << Twine(Value);
    if (getStreamer().isVerboseAsm()) {
      StringRef Name = ELFAttrs::attrTypeAsString(
          Attribute, HexagonAttrs::getHexagonAttributeTags());
      if (!Name.empty())
        OS << "\t// " << Name;
    }
    OS << "\n";
  }
};

class HexagonTargetELFStreamer : public HexagonTargetStreamer {
public:
  MCELFStreamer &getStreamer() {
    return static_cast<MCELFStreamer &>(Streamer);
  }
  HexagonTargetELFStreamer(MCStreamer &S, MCSubtargetInfo const &STI)
      : HexagonTargetStreamer(S) {
    getStreamer().getWriter().setELFHeaderEFlags(Hexagon_MC::GetELFFlags(STI));
  }

  void emitCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                              unsigned ByteAlignment,
                              unsigned AccessSize) override {
    HexagonMCELFStreamer &HexagonELFStreamer =
        static_cast<HexagonMCELFStreamer &>(getStreamer());
    HexagonELFStreamer.HexagonMCEmitCommonSymbol(
        Symbol, Size, Align(ByteAlignment), AccessSize);
  }

  void emitLocalCommonSymbolSorted(MCSymbol *Symbol, uint64_t Size,
                                   unsigned ByteAlignment,
                                   unsigned AccessSize) override {
    HexagonMCELFStreamer &HexagonELFStreamer =
        static_cast<HexagonMCELFStreamer &>(getStreamer());
    HexagonELFStreamer.HexagonMCEmitLocalCommonSymbol(
        Symbol, Size, Align(ByteAlignment), AccessSize);
  }

  void finish() override { finishAttributeSection(); }

  void reset() override { AttributeSection = nullptr; }

private:
  MCSection *AttributeSection = nullptr;

  void finishAttributeSection() override {
    MCELFStreamer &S = getStreamer();
    if (S.Contents.empty())
      return;

    S.emitAttributesSection("hexagon", ".hexagon.attributes",
                            ELF::SHT_HEXAGON_ATTRIBUTES, AttributeSection);
  }

  void emitAttribute(uint32_t Attribute, uint32_t Value) override {
    getStreamer().setAttributeItem(Attribute, Value,
                                   /*OverwriteExisting=*/true);
  }
};

} // end anonymous namespace

llvm::MCInstrInfo *llvm::createHexagonMCInstrInfo() {
  MCInstrInfo *X = new MCInstrInfo();
  InitHexagonMCInstrInfo(X);
  return X;
}

static MCRegisterInfo *createHexagonMCRegisterInfo(const Triple &TT) {
  MCRegisterInfo *X = new MCRegisterInfo();
  InitHexagonMCRegisterInfo(X, Hexagon::R31, /*DwarfFlavour=*/0,
                            /*EHFlavour=*/0, /*PC=*/Hexagon::PC);
  return X;
}

static MCAsmInfo *createHexagonMCAsmInfo(const MCRegisterInfo &MRI,
                                         const Triple &TT,
                                         const MCTargetOptions &Options) {
  MCAsmInfo *MAI = new HexagonMCAsmInfo(TT);

  // VirtualFP = (R30 + #0).
  MCCFIInstruction Inst = MCCFIInstruction::cfiDefCfa(
      nullptr, MRI.getDwarfRegNum(Hexagon::R30, true), 0);
  MAI->addInitialFrameState(Inst);

  return MAI;
}

static MCInstPrinter *createHexagonMCInstPrinter(const Triple &T,
                                                 unsigned SyntaxVariant,
                                                 const MCAsmInfo &MAI,
                                                 const MCInstrInfo &MII,
                                                 const MCRegisterInfo &MRI)
{
  if (SyntaxVariant == 0)
    return new HexagonInstPrinter(MAI, MII, MRI);
  else
    return nullptr;
}

static MCTargetStreamer *createMCAsmTargetStreamer(MCStreamer &S,
                                                   formatted_raw_ostream &OS,
                                                   MCInstPrinter *IP) {
  return new HexagonTargetAsmStreamer(S, OS, *IP);
}

static MCStreamer *createMCStreamer(Triple const &T, MCContext &Context,
                                    std::unique_ptr<MCAsmBackend> &&MAB,
                                    std::unique_ptr<MCObjectWriter> &&OW,
                                    std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return createHexagonELFStreamer(T, Context, std::move(MAB), std::move(OW),
                                  std::move(Emitter));
}

static MCTargetStreamer *
createHexagonObjectTargetStreamer(MCStreamer &S, const MCSubtargetInfo &STI) {
  return new HexagonTargetELFStreamer(S, STI);
}

static MCTargetStreamer *createHexagonNullTargetStreamer(MCStreamer &S) {
  return new HexagonTargetStreamer(S);
}

static void LLVM_ATTRIBUTE_UNUSED clearFeature(MCSubtargetInfo* STI, uint64_t F) {
  if (STI->hasFeature(F))
    STI->ToggleFeature(F);
}

static bool LLVM_ATTRIBUTE_UNUSED checkFeature(MCSubtargetInfo* STI, uint64_t F) {
  return STI->hasFeature(F);
}

namespace {
std::string selectHexagonFS(StringRef CPU, StringRef FS) {
  SmallVector<StringRef, 3> Result;
  if (!FS.empty())
    Result.push_back(FS);

  switch (EnableHVX) {
  case Hexagon::ArchEnum::V5:
  case Hexagon::ArchEnum::V55:
    break;
  case Hexagon::ArchEnum::V60:
    Result.push_back("+hvxv60");
    break;
  case Hexagon::ArchEnum::V62:
    Result.push_back("+hvxv62");
    break;
  case Hexagon::ArchEnum::V65:
    Result.push_back("+hvxv65");
    break;
  case Hexagon::ArchEnum::V66:
    Result.push_back("+hvxv66");
    break;
  case Hexagon::ArchEnum::V67:
    Result.push_back("+hvxv67");
    break;
  case Hexagon::ArchEnum::V68:
    Result.push_back("+hvxv68");
    break;
  case Hexagon::ArchEnum::V69:
    Result.push_back("+hvxv69");
    break;
  case Hexagon::ArchEnum::V71:
    Result.push_back("+hvxv71");
    break;
  case Hexagon::ArchEnum::V73:
    Result.push_back("+hvxv73");
    break;
  case Hexagon::ArchEnum::Generic:{
    Result.push_back(StringSwitch<StringRef>(CPU)
             .Case("hexagonv60", "+hvxv60")
             .Case("hexagonv62", "+hvxv62")
             .Case("hexagonv65", "+hvxv65")
             .Case("hexagonv66", "+hvxv66")
             .Case("hexagonv67", "+hvxv67")
             .Case("hexagonv67t", "+hvxv67")
             .Case("hexagonv68", "+hvxv68")
             .Case("hexagonv69", "+hvxv69")
             .Case("hexagonv71", "+hvxv71")
             .Case("hexagonv71t", "+hvxv71")
             .Case("hexagonv73", "+hvxv73"));
    break;
  }
  case Hexagon::ArchEnum::NoArch:
    // Sentinel if -mhvx isn't specified
    break;
  }
  if (EnableHvxIeeeFp)
    Result.push_back("+hvx-ieee-fp");
  if (EnableHexagonCabac)
    Result.push_back("+cabac");

  return join(Result.begin(), Result.end(), ",");
}
}

static bool isCPUValid(StringRef CPU) {
  return Hexagon::getCpu(CPU).has_value();
}

namespace {
std::pair<std::string, std::string> selectCPUAndFS(StringRef CPU,
                                                   StringRef FS) {
  std::pair<std::string, std::string> Result;
  Result.first = std::string(Hexagon_MC::selectHexagonCPU(CPU));
  Result.second = selectHexagonFS(Result.first, FS);
  return Result;
}
std::mutex ArchSubtargetMutex;
std::unordered_map<std::string, std::unique_ptr<MCSubtargetInfo const>>
    ArchSubtarget;
} // namespace

MCSubtargetInfo const *
Hexagon_MC::getArchSubtarget(MCSubtargetInfo const *STI) {
  std::lock_guard<std::mutex> Lock(ArchSubtargetMutex);
  auto Existing = ArchSubtarget.find(std::string(STI->getCPU()));
  if (Existing == ArchSubtarget.end())
    return nullptr;
  return Existing->second.get();
}

FeatureBitset Hexagon_MC::completeHVXFeatures(const FeatureBitset &S) {
  using namespace Hexagon;
  // Make sure that +hvx-length turns hvx on, and that "hvx" alone
  // turns on hvxvNN, corresponding to the existing ArchVNN.
  FeatureBitset FB = S;
  unsigned CpuArch = ArchV5;
  for (unsigned F : {ArchV73, ArchV71, ArchV69, ArchV68, ArchV67, ArchV66,
                     ArchV65, ArchV62, ArchV60, ArchV55, ArchV5}) {
    if (!FB.test(F))
      continue;
    CpuArch = F;
    break;
  }
  bool UseHvx = false;
  for (unsigned F : {ExtensionHVX, ExtensionHVX64B, ExtensionHVX128B}) {
    if (!FB.test(F))
      continue;
    UseHvx = true;
    break;
  }
  bool HasHvxVer = false;
  for (unsigned F : {ExtensionHVXV60, ExtensionHVXV62, ExtensionHVXV65,
                     ExtensionHVXV66, ExtensionHVXV67, ExtensionHVXV68,
                     ExtensionHVXV69, ExtensionHVXV71, ExtensionHVXV73}) {
    if (!FB.test(F))
      continue;
    HasHvxVer = true;
    UseHvx = true;
    break;
  }

  if (!UseHvx || HasHvxVer)
    return FB;

  // HasHvxVer is false, and UseHvx is true.
  switch (CpuArch) {
  case ArchV73:
    FB.set(ExtensionHVXV73);
    [[fallthrough]];
  case ArchV71:
    FB.set(ExtensionHVXV71);
    [[fallthrough]];
  case ArchV69:
    FB.set(ExtensionHVXV69);
    [[fallthrough]];
  case ArchV68:
    FB.set(ExtensionHVXV68);
    [[fallthrough]];
  case ArchV67:
    FB.set(ExtensionHVXV67);
    [[fallthrough]];
  case ArchV66:
    FB.set(ExtensionHVXV66);
    [[fallthrough]];
  case ArchV65:
    FB.set(ExtensionHVXV65);
    [[fallthrough]];
  case ArchV62:
    FB.set(ExtensionHVXV62);
    [[fallthrough]];
  case ArchV60:
    FB.set(ExtensionHVXV60);
    break;
  }
  return FB;
}

MCSubtargetInfo *Hexagon_MC::createHexagonMCSubtargetInfo(const Triple &TT,
                                                          StringRef CPU,
                                                          StringRef FS) {
  std::pair<std::string, std::string> Features = selectCPUAndFS(CPU, FS);
  StringRef CPUName = Features.first;
  StringRef ArchFS = Features.second;

  MCSubtargetInfo *X = createHexagonMCSubtargetInfoImpl(
      TT, CPUName, /*TuneCPU*/ CPUName, ArchFS);
  if (X != nullptr && (CPUName == "hexagonv67t" || CPUName == "hexagon71t"))
    addArchSubtarget(X, ArchFS);

  if (CPU == "help")
    exit(0);

  if (!isCPUValid(CPUName.str())) {
    errs() << "error: invalid CPU \"" << CPUName.str().c_str()
           << "\" specified\n";
    return nullptr;
  }

  // Add qfloat subtarget feature by default to v68 and above
  // unless explicitely disabled
  if (checkFeature(X, Hexagon::ExtensionHVXV68) &&
      !ArchFS.contains("-hvx-qfloat")) {
    llvm::FeatureBitset Features = X->getFeatureBits();
    X->setFeatureBits(Features.set(Hexagon::ExtensionHVXQFloat));
  }

  if (HexagonDisableDuplex) {
    llvm::FeatureBitset Features = X->getFeatureBits();
    X->setFeatureBits(Features.reset(Hexagon::FeatureDuplex));
  }

  X->setFeatureBits(completeHVXFeatures(X->getFeatureBits()));

  // The Z-buffer instructions are grandfathered in for current
  // architectures but omitted for new ones.  Future instruction
  // sets may introduce new/conflicting z-buffer instructions.
  const bool ZRegOnDefault =
      (CPUName == "hexagonv67") || (CPUName == "hexagonv66");
  if (ZRegOnDefault) {
    llvm::FeatureBitset Features = X->getFeatureBits();
    X->setFeatureBits(Features.set(Hexagon::ExtensionZReg));
  }

  return X;
}

void Hexagon_MC::addArchSubtarget(MCSubtargetInfo const *STI, StringRef FS) {
  assert(STI != nullptr);
  if (STI->getCPU().contains("t")) {
    auto ArchSTI = createHexagonMCSubtargetInfo(
        STI->getTargetTriple(),
        STI->getCPU().substr(0, STI->getCPU().size() - 1), FS);
    std::lock_guard<std::mutex> Lock(ArchSubtargetMutex);
    ArchSubtarget[std::string(STI->getCPU())] =
        std::unique_ptr<MCSubtargetInfo const>(ArchSTI);
  }
}

std::optional<unsigned>
Hexagon_MC::getHVXVersion(const FeatureBitset &Features) {
  for (auto Arch : {Hexagon::ExtensionHVXV73, Hexagon::ExtensionHVXV71,
                    Hexagon::ExtensionHVXV69, Hexagon::ExtensionHVXV68,
                    Hexagon::ExtensionHVXV67, Hexagon::ExtensionHVXV66,
                    Hexagon::ExtensionHVXV65, Hexagon::ExtensionHVXV62,
                    Hexagon::ExtensionHVXV60})
    if (Features.test(Arch))
      return Arch;
  return {};
}

unsigned Hexagon_MC::getArchVersion(const FeatureBitset &Features) {
  for (auto Arch :
       {Hexagon::ArchV73, Hexagon::ArchV71, Hexagon::ArchV69, Hexagon::ArchV68,
        Hexagon::ArchV67, Hexagon::ArchV66, Hexagon::ArchV65, Hexagon::ArchV62,
        Hexagon::ArchV60, Hexagon::ArchV55, Hexagon::ArchV5})
    if (Features.test(Arch))
      return Arch;
  llvm_unreachable("Expected arch v5-v73");
  return 0;
}

unsigned Hexagon_MC::GetELFFlags(const MCSubtargetInfo &STI) {
  return StringSwitch<unsigned>(STI.getCPU())
      .Case("generic", llvm::ELF::EF_HEXAGON_MACH_V5)
      .Case("hexagonv5", llvm::ELF::EF_HEXAGON_MACH_V5)
      .Case("hexagonv55", llvm::ELF::EF_HEXAGON_MACH_V55)
      .Case("hexagonv60", llvm::ELF::EF_HEXAGON_MACH_V60)
      .Case("hexagonv62", llvm::ELF::EF_HEXAGON_MACH_V62)
      .Case("hexagonv65", llvm::ELF::EF_HEXAGON_MACH_V65)
      .Case("hexagonv66", llvm::ELF::EF_HEXAGON_MACH_V66)
      .Case("hexagonv67", llvm::ELF::EF_HEXAGON_MACH_V67)
      .Case("hexagonv67t", llvm::ELF::EF_HEXAGON_MACH_V67T)
      .Case("hexagonv68", llvm::ELF::EF_HEXAGON_MACH_V68)
      .Case("hexagonv69", llvm::ELF::EF_HEXAGON_MACH_V69)
      .Case("hexagonv71", llvm::ELF::EF_HEXAGON_MACH_V71)
      .Case("hexagonv71t", llvm::ELF::EF_HEXAGON_MACH_V71T)
      .Case("hexagonv73", llvm::ELF::EF_HEXAGON_MACH_V73);
}

llvm::ArrayRef<MCPhysReg> Hexagon_MC::GetVectRegRev() {
  return ArrayRef(VectRegRev);
}

namespace {
class HexagonMCInstrAnalysis : public MCInstrAnalysis {
public:
  HexagonMCInstrAnalysis(MCInstrInfo const *Info) : MCInstrAnalysis(Info) {}

  bool isUnconditionalBranch(MCInst const &Inst) const override {
    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    return MCInstrAnalysis::isUnconditionalBranch(Inst);
  }

  bool isConditionalBranch(MCInst const &Inst) const override {
    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    return MCInstrAnalysis::isConditionalBranch(Inst);
  }

  bool evaluateBranch(MCInst const &Inst, uint64_t Addr,
                      uint64_t Size, uint64_t &Target) const override {
    if (!(isCall(Inst) || isUnconditionalBranch(Inst) ||
          isConditionalBranch(Inst)))
      return false;

    //assert(!HexagonMCInstrInfo::isBundle(Inst));
    if (!HexagonMCInstrInfo::isExtendable(*Info, Inst))
      return false;
    auto const &Extended(HexagonMCInstrInfo::getExtendableOperand(*Info, Inst));
    assert(Extended.isExpr());
    int64_t Value;
    if (!Extended.getExpr()->evaluateAsAbsolute(Value))
      return false;
    Target = Value;
    return true;
  }
};
}

static MCInstrAnalysis *createHexagonMCInstrAnalysis(const MCInstrInfo *Info) {
  return new HexagonMCInstrAnalysis(Info);
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHexagonTargetMC() {
  // Register the MC asm info.
  RegisterMCAsmInfoFn X(getTheHexagonTarget(), createHexagonMCAsmInfo);

  // Register the MC instruction info.
  TargetRegistry::RegisterMCInstrInfo(getTheHexagonTarget(),
                                      createHexagonMCInstrInfo);

  // Register the MC register info.
  TargetRegistry::RegisterMCRegInfo(getTheHexagonTarget(),
                                    createHexagonMCRegisterInfo);

  // Register the MC subtarget info.
  TargetRegistry::RegisterMCSubtargetInfo(
      getTheHexagonTarget(), Hexagon_MC::createHexagonMCSubtargetInfo);

  // Register the MC Code Emitter
  TargetRegistry::RegisterMCCodeEmitter(getTheHexagonTarget(),
                                        createHexagonMCCodeEmitter);

  // Register the asm backend
  TargetRegistry::RegisterMCAsmBackend(getTheHexagonTarget(),
                                       createHexagonAsmBackend);

  // Register the MC instruction analyzer.
  TargetRegistry::RegisterMCInstrAnalysis(getTheHexagonTarget(),
                                          createHexagonMCInstrAnalysis);

  // Register the obj streamer
  TargetRegistry::RegisterELFStreamer(getTheHexagonTarget(), createMCStreamer);

  // Register the obj target streamer
  TargetRegistry::RegisterObjectTargetStreamer(
      getTheHexagonTarget(), createHexagonObjectTargetStreamer);

  // Register the asm streamer
  TargetRegistry::RegisterAsmTargetStreamer(getTheHexagonTarget(),
                                            createMCAsmTargetStreamer);

  // Register the null streamer
  TargetRegistry::RegisterNullTargetStreamer(getTheHexagonTarget(),
                                             createHexagonNullTargetStreamer);

  // Register the MC Inst Printer
  TargetRegistry::RegisterMCInstPrinter(getTheHexagonTarget(),
                                        createHexagonMCInstPrinter);
}
