//===--- TargetRegistry.cpp - Target registration -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/TargetRegistry.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <vector>
using namespace llvm;

// Clients are responsible for avoid race conditions in registration.
static Target *FirstTarget = nullptr;

MCStreamer *Target::createMCObjectStreamer(
    const Triple &T, MCContext &Ctx, std::unique_ptr<MCAsmBackend> TAB,
    std::unique_ptr<MCObjectWriter> OW, std::unique_ptr<MCCodeEmitter> Emitter,
    const MCSubtargetInfo &STI) const {
  MCStreamer *S = nullptr;
  switch (T.getObjectFormat()) {
  case Triple::UnknownObjectFormat:
    llvm_unreachable("Unknown object format");
  case Triple::COFF:
    assert((T.isOSWindows() || T.isUEFI()) &&
           "only Windows and UEFI COFF are supported");
    S = COFFStreamerCtorFn(Ctx, std::move(TAB), std::move(OW),
                           std::move(Emitter));
    break;
  case Triple::MachO:
    if (MachOStreamerCtorFn)
      S = MachOStreamerCtorFn(Ctx, std::move(TAB), std::move(OW),
                              std::move(Emitter));
    else
      S = createMachOStreamer(Ctx, std::move(TAB), std::move(OW),
                              std::move(Emitter), false);
    break;
  case Triple::ELF:
    if (ELFStreamerCtorFn)
      S = ELFStreamerCtorFn(T, Ctx, std::move(TAB), std::move(OW),
                            std::move(Emitter));
    else
      S = createELFStreamer(Ctx, std::move(TAB), std::move(OW),
                            std::move(Emitter));
    break;
  case Triple::Wasm:
    S = createWasmStreamer(Ctx, std::move(TAB), std::move(OW),
                           std::move(Emitter));
    break;
  case Triple::GOFF:
    S = createGOFFStreamer(Ctx, std::move(TAB), std::move(OW),
                           std::move(Emitter));
    break;
  case Triple::XCOFF:
    S = XCOFFStreamerCtorFn(T, Ctx, std::move(TAB), std::move(OW),
                            std::move(Emitter));
    break;
  case Triple::SPIRV:
    S = createSPIRVStreamer(Ctx, std::move(TAB), std::move(OW),
                            std::move(Emitter));
    break;
  case Triple::DXContainer:
    S = createDXContainerStreamer(Ctx, std::move(TAB), std::move(OW),
                                  std::move(Emitter));
    break;
  }
  if (ObjectTargetStreamerCtorFn)
    ObjectTargetStreamerCtorFn(*S, STI);
  return S;
}

MCStreamer *Target::createMCObjectStreamer(
    const Triple &T, MCContext &Ctx, std::unique_ptr<MCAsmBackend> &&TAB,
    std::unique_ptr<MCObjectWriter> &&OW,
    std::unique_ptr<MCCodeEmitter> &&Emitter, const MCSubtargetInfo &STI, bool,
    bool, bool) const {
  return createMCObjectStreamer(T, Ctx, std::move(TAB), std::move(OW),
                                std::move(Emitter), STI);
}

MCStreamer *Target::createAsmStreamer(MCContext &Ctx,
                                      std::unique_ptr<formatted_raw_ostream> OS,
                                      MCInstPrinter *IP,
                                      std::unique_ptr<MCCodeEmitter> CE,
                                      std::unique_ptr<MCAsmBackend> TAB) const {
  formatted_raw_ostream &OSRef = *OS;
  MCStreamer *S = llvm::createAsmStreamer(Ctx, std::move(OS), IP,
                                          std::move(CE), std::move(TAB));
  createAsmTargetStreamer(*S, OSRef, IP);
  return S;
}

MCStreamer *Target::createAsmStreamer(MCContext &Ctx,
                                      std::unique_ptr<formatted_raw_ostream> OS,
                                      bool IsVerboseAsm, bool UseDwarfDirectory,
                                      MCInstPrinter *IP,
                                      std::unique_ptr<MCCodeEmitter> &&CE,
                                      std::unique_ptr<MCAsmBackend> &&TAB,
                                      bool ShowInst) const {
  return createAsmStreamer(Ctx, std::move(OS), IP, std::move(CE),
                           std::move(TAB));
}

iterator_range<TargetRegistry::iterator> TargetRegistry::targets() {
  return make_range(iterator(FirstTarget), iterator());
}

const Target *TargetRegistry::lookupTarget(StringRef ArchName,
                                           Triple &TheTriple,
                                           std::string &Error) {
  // Allocate target machine.  First, check whether the user has explicitly
  // specified an architecture to compile for. If so we have to look it up by
  // name, because it might be a backend that has no mapping to a target triple.
  const Target *TheTarget = nullptr;
  if (!ArchName.empty()) {
    auto I = find_if(targets(),
                     [&](const Target &T) { return ArchName == T.getName(); });

    if (I == targets().end()) {
      Error = ("invalid target '" + ArchName + "'.\n").str();
      return nullptr;
    }

    TheTarget = &*I;

    // Adjust the triple to match (if known), otherwise stick with the
    // given triple.
    Triple::ArchType Type = Triple::getArchTypeForLLVMName(ArchName);
    if (Type != Triple::UnknownArch)
      TheTriple.setArch(Type);
  } else {
    // Get the target specific parser.
    std::string TempError;
    TheTarget = TargetRegistry::lookupTarget(TheTriple.getTriple(), TempError);
    if (!TheTarget) {
      Error = "unable to get target for '" + TheTriple.getTriple() +
              "', see --version and --triple.";
      return nullptr;
    }
  }

  return TheTarget;
}

const Target *TargetRegistry::lookupTarget(StringRef TT, std::string &Error) {
  // Provide special warning when no targets are initialized.
  if (targets().begin() == targets().end()) {
    Error = "Unable to find target for this triple (no targets are registered)";
    return nullptr;
  }
  Triple::ArchType Arch = Triple(TT).getArch();
  auto ArchMatch = [&](const Target &T) { return T.ArchMatchFn(Arch); };
  auto I = find_if(targets(), ArchMatch);

  if (I == targets().end()) {
    Error = ("No available targets are compatible with triple \"" + TT + "\"")
                .str();
    return nullptr;
  }

  auto J = std::find_if(std::next(I), targets().end(), ArchMatch);
  if (J != targets().end()) {
    Error = std::string("Cannot choose between targets \"") + I->Name +
            "\" and \"" + J->Name + "\"";
    return nullptr;
  }

  return &*I;
}

void TargetRegistry::RegisterTarget(Target &T, const char *Name,
                                    const char *ShortDesc,
                                    const char *BackendName,
                                    Target::ArchMatchFnTy ArchMatchFn,
                                    bool HasJIT) {
  assert(Name && ShortDesc && ArchMatchFn &&
         "Missing required target information!");

  // Check if this target has already been initialized, we allow this as a
  // convenience to some clients.
  if (T.Name)
    return;

  // Add to the list of targets.
  T.Next = FirstTarget;
  FirstTarget = &T;

  T.Name = Name;
  T.ShortDesc = ShortDesc;
  T.BackendName = BackendName;
  T.ArchMatchFn = ArchMatchFn;
  T.HasJIT = HasJIT;
}

static int TargetArraySortFn(const std::pair<StringRef, const Target *> *LHS,
                             const std::pair<StringRef, const Target *> *RHS) {
  return LHS->first.compare(RHS->first);
}

void TargetRegistry::printRegisteredTargetsForVersion(raw_ostream &OS) {
  std::vector<std::pair<StringRef, const Target*> > Targets;
  size_t Width = 0;
  for (const auto &T : TargetRegistry::targets()) {
    Targets.push_back(std::make_pair(T.getName(), &T));
    Width = std::max(Width, Targets.back().first.size());
  }
  array_pod_sort(Targets.begin(), Targets.end(), TargetArraySortFn);

  OS << "\n";
  OS << "  Registered Targets:\n";
  for (const auto &Target : Targets) {
    OS << "    " << Target.first;
    OS.indent(Width - Target.first.size())
        << " - " << Target.second->getShortDescription() << '\n';
  }
  if (Targets.empty())
    OS << "    (none)\n";
}
