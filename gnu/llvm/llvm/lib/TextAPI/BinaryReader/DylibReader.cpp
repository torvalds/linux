//===- DylibReader.cpp -------------- TAPI MachO Dylib Reader --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// Implements the TAPI Reader for Mach-O dynamic libraries.
///
//===----------------------------------------------------------------------===//

#include "llvm/TextAPI/DylibReader.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/DWARF/DWARFCompileUnit.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Support/Endian.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/RecordsSlice.h"
#include "llvm/TextAPI/TextAPIError.h"
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <tuple>

using namespace llvm;
using namespace llvm::object;
using namespace llvm::MachO;
using namespace llvm::MachO::DylibReader;

using TripleVec = std::vector<Triple>;
static typename TripleVec::iterator emplace(TripleVec &Container, Triple &&T) {
  auto I = partition_point(Container, [=](const Triple &CT) {
    return std::forward_as_tuple(CT.getArch(), CT.getOS(),
                                 CT.getEnvironment()) <
           std::forward_as_tuple(T.getArch(), T.getOS(), T.getEnvironment());
  });

  if (I != Container.end() && *I == T)
    return I;
  return Container.emplace(I, T);
}

static TripleVec constructTriples(MachOObjectFile *Obj,
                                  const Architecture ArchT) {
  auto getOSVersionStr = [](uint32_t V) {
    PackedVersion OSVersion(V);
    std::string Vers;
    raw_string_ostream VStream(Vers);
    VStream << OSVersion;
    return VStream.str();
  };
  auto getOSVersion = [&](const MachOObjectFile::LoadCommandInfo &cmd) {
    auto Vers = Obj->getVersionMinLoadCommand(cmd);
    return getOSVersionStr(Vers.version);
  };

  TripleVec Triples;
  bool IsIntel = ArchitectureSet(ArchT).hasX86();
  auto Arch = getArchitectureName(ArchT);

  for (const auto &cmd : Obj->load_commands()) {
    std::string OSVersion;
    switch (cmd.C.cmd) {
    case MachO::LC_VERSION_MIN_MACOSX:
      OSVersion = getOSVersion(cmd);
      emplace(Triples, {Arch, "apple", "macos" + OSVersion});
      break;
    case MachO::LC_VERSION_MIN_IPHONEOS:
      OSVersion = getOSVersion(cmd);
      if (IsIntel)
        emplace(Triples, {Arch, "apple", "ios" + OSVersion, "simulator"});
      else
        emplace(Triples, {Arch, "apple", "ios" + OSVersion});
      break;
    case MachO::LC_VERSION_MIN_TVOS:
      OSVersion = getOSVersion(cmd);
      if (IsIntel)
        emplace(Triples, {Arch, "apple", "tvos" + OSVersion, "simulator"});
      else
        emplace(Triples, {Arch, "apple", "tvos" + OSVersion});
      break;
    case MachO::LC_VERSION_MIN_WATCHOS:
      OSVersion = getOSVersion(cmd);
      if (IsIntel)
        emplace(Triples, {Arch, "apple", "watchos" + OSVersion, "simulator"});
      else
        emplace(Triples, {Arch, "apple", "watchos" + OSVersion});
      break;
    case MachO::LC_BUILD_VERSION: {
      OSVersion = getOSVersionStr(Obj->getBuildVersionLoadCommand(cmd).minos);
      switch (Obj->getBuildVersionLoadCommand(cmd).platform) {
      case MachO::PLATFORM_MACOS:
        emplace(Triples, {Arch, "apple", "macos" + OSVersion});
        break;
      case MachO::PLATFORM_IOS:
        emplace(Triples, {Arch, "apple", "ios" + OSVersion});
        break;
      case MachO::PLATFORM_TVOS:
        emplace(Triples, {Arch, "apple", "tvos" + OSVersion});
        break;
      case MachO::PLATFORM_WATCHOS:
        emplace(Triples, {Arch, "apple", "watchos" + OSVersion});
        break;
      case MachO::PLATFORM_BRIDGEOS:
        emplace(Triples, {Arch, "apple", "bridgeos" + OSVersion});
        break;
      case MachO::PLATFORM_MACCATALYST:
        emplace(Triples, {Arch, "apple", "ios" + OSVersion, "macabi"});
        break;
      case MachO::PLATFORM_IOSSIMULATOR:
        emplace(Triples, {Arch, "apple", "ios" + OSVersion, "simulator"});
        break;
      case MachO::PLATFORM_TVOSSIMULATOR:
        emplace(Triples, {Arch, "apple", "tvos" + OSVersion, "simulator"});
        break;
      case MachO::PLATFORM_WATCHOSSIMULATOR:
        emplace(Triples, {Arch, "apple", "watchos" + OSVersion, "simulator"});
        break;
      case MachO::PLATFORM_DRIVERKIT:
        emplace(Triples, {Arch, "apple", "driverkit" + OSVersion});
        break;
      default:
        break; // Skip any others.
      }
      break;
    }
    default:
      break;
    }
  }

  // Record unknown platform for older binaries that don't enforce platform
  // load commands.
  if (Triples.empty())
    emplace(Triples, {Arch, "apple", "unknown"});

  return Triples;
}

static Error readMachOHeader(MachOObjectFile *Obj, RecordsSlice &Slice) {
  auto H = Obj->getHeader();
  auto &BA = Slice.getBinaryAttrs();

  switch (H.filetype) {
  default:
    llvm_unreachable("unsupported binary type");
  case MachO::MH_DYLIB:
    BA.File = FileType::MachO_DynamicLibrary;
    break;
  case MachO::MH_DYLIB_STUB:
    BA.File = FileType::MachO_DynamicLibrary_Stub;
    break;
  case MachO::MH_BUNDLE:
    BA.File = FileType::MachO_Bundle;
    break;
  }

  if (H.flags & MachO::MH_TWOLEVEL)
    BA.TwoLevelNamespace = true;
  if (H.flags & MachO::MH_APP_EXTENSION_SAFE)
    BA.AppExtensionSafe = true;

  for (const auto &LCI : Obj->load_commands()) {
    switch (LCI.C.cmd) {
    case MachO::LC_ID_DYLIB: {
      auto DLLC = Obj->getDylibIDLoadCommand(LCI);
      BA.InstallName = Slice.copyString(LCI.Ptr + DLLC.dylib.name);
      BA.CurrentVersion = DLLC.dylib.current_version;
      BA.CompatVersion = DLLC.dylib.compatibility_version;
      break;
    }
    case MachO::LC_REEXPORT_DYLIB: {
      auto DLLC = Obj->getDylibIDLoadCommand(LCI);
      BA.RexportedLibraries.emplace_back(
          Slice.copyString(LCI.Ptr + DLLC.dylib.name));
      break;
    }
    case MachO::LC_SUB_FRAMEWORK: {
      auto SFC = Obj->getSubFrameworkCommand(LCI);
      BA.ParentUmbrella = Slice.copyString(LCI.Ptr + SFC.umbrella);
      break;
    }
    case MachO::LC_SUB_CLIENT: {
      auto SCLC = Obj->getSubClientCommand(LCI);
      BA.AllowableClients.emplace_back(Slice.copyString(LCI.Ptr + SCLC.client));
      break;
    }
    case MachO::LC_UUID: {
      auto UUIDLC = Obj->getUuidCommand(LCI);
      std::stringstream Stream;
      for (unsigned I = 0; I < 16; ++I) {
        if (I == 4 || I == 6 || I == 8 || I == 10)
          Stream << '-';
        Stream << std::setfill('0') << std::setw(2) << std::uppercase
               << std::hex << static_cast<int>(UUIDLC.uuid[I]);
      }
      BA.UUID = Slice.copyString(Stream.str());
      break;
    }
    case MachO::LC_RPATH: {
      auto RPLC = Obj->getRpathCommand(LCI);
      BA.RPaths.emplace_back(Slice.copyString(LCI.Ptr + RPLC.path));
      break;
    }
    case MachO::LC_SEGMENT_SPLIT_INFO: {
      auto SSILC = Obj->getLinkeditDataLoadCommand(LCI);
      if (SSILC.datasize == 0)
        BA.OSLibNotForSharedCache = true;
      break;
    }
    default:
      break;
    }
  }

  for (auto &Sect : Obj->sections()) {
    auto SectName = Sect.getName();
    if (!SectName)
      return SectName.takeError();
    if (*SectName != "__objc_imageinfo" && *SectName != "__image_info")
      continue;

    auto Content = Sect.getContents();
    if (!Content)
      return Content.takeError();

    if ((Content->size() >= 8) && (Content->front() == 0)) {
      uint32_t Flags;
      if (Obj->isLittleEndian()) {
        auto *p =
            reinterpret_cast<const support::ulittle32_t *>(Content->data() + 4);
        Flags = *p;
      } else {
        auto *p =
            reinterpret_cast<const support::ubig32_t *>(Content->data() + 4);
        Flags = *p;
      }
      BA.SwiftABI = (Flags >> 8) & 0xFF;
    }
  }
  return Error::success();
}

static Error readSymbols(MachOObjectFile *Obj, RecordsSlice &Slice,
                         const ParseOption &Opt) {

  auto parseExport = [](const auto ExportFlags,
                        auto Addr) -> std::tuple<SymbolFlags, RecordLinkage> {
    SymbolFlags Flags = SymbolFlags::None;
    switch (ExportFlags & MachO::EXPORT_SYMBOL_FLAGS_KIND_MASK) {
    case MachO::EXPORT_SYMBOL_FLAGS_KIND_REGULAR:
      if (ExportFlags & MachO::EXPORT_SYMBOL_FLAGS_WEAK_DEFINITION)
        Flags |= SymbolFlags::WeakDefined;
      break;
    case MachO::EXPORT_SYMBOL_FLAGS_KIND_THREAD_LOCAL:
      Flags |= SymbolFlags::ThreadLocalValue;
      break;
    }

    RecordLinkage Linkage = (ExportFlags & MachO::EXPORT_SYMBOL_FLAGS_REEXPORT)
                                ? RecordLinkage::Rexported
                                : RecordLinkage::Exported;
    return {Flags, Linkage};
  };

  Error Err = Error::success();

  StringMap<std::pair<SymbolFlags, RecordLinkage>> Exports;
  // Collect symbols from export trie first. Sometimes, there are more exports
  // in the trie than in n-list due to stripping. This is common for swift
  // mangled symbols.
  for (auto &Sym : Obj->exports(Err)) {
    auto [Flags, Linkage] = parseExport(Sym.flags(), Sym.address());
    Slice.addRecord(Sym.name(), Flags, GlobalRecord::Kind::Unknown, Linkage);
    Exports[Sym.name()] = {Flags, Linkage};
  }

  for (const auto &Sym : Obj->symbols()) {
    auto FlagsOrErr = Sym.getFlags();
    if (!FlagsOrErr)
      return FlagsOrErr.takeError();
    auto Flags = *FlagsOrErr;

    auto NameOrErr = Sym.getName();
    if (!NameOrErr)
      return NameOrErr.takeError();
    auto Name = *NameOrErr;

    RecordLinkage Linkage = RecordLinkage::Unknown;
    SymbolFlags RecordFlags = SymbolFlags::None;

    if (Flags & SymbolRef::SF_Undefined) {
      if (Opt.Undefineds)
        Linkage = RecordLinkage::Undefined;
      else
        continue;
      if (Flags & SymbolRef::SF_Weak)
        RecordFlags |= SymbolFlags::WeakReferenced;
    } else if (Flags & SymbolRef::SF_Exported) {
      auto Exp = Exports.find(Name);
      // This should never be possible when binaries are produced with Apple
      // linkers. However it is possible to craft dylibs where the export trie
      // is either malformed or has conflicting symbols compared to n_list.
      if (Exp != Exports.end())
        std::tie(RecordFlags, Linkage) = Exp->second;
      else
        Linkage = RecordLinkage::Exported;
    } else if (Flags & SymbolRef::SF_Hidden) {
      Linkage = RecordLinkage::Internal;
    } else
      continue;

    auto TypeOrErr = Sym.getType();
    if (!TypeOrErr)
      return TypeOrErr.takeError();
    auto Type = *TypeOrErr;

    GlobalRecord::Kind GV = (Type & SymbolRef::ST_Function)
                                ? GlobalRecord::Kind::Function
                                : GlobalRecord::Kind::Variable;

    if (GV == GlobalRecord::Kind::Function)
      RecordFlags |= SymbolFlags::Text;
    else
      RecordFlags |= SymbolFlags::Data;

    Slice.addRecord(Name, RecordFlags, GV, Linkage);
  }
  return Err;
}

static Error load(MachOObjectFile *Obj, RecordsSlice &Slice,
                  const ParseOption &Opt, const Architecture Arch) {
  if (Arch == AK_unknown)
    return make_error<TextAPIError>(TextAPIErrorCode::UnsupportedTarget);

  if (Opt.MachOHeader)
    if (auto Err = readMachOHeader(Obj, Slice))
      return Err;

  if (Opt.SymbolTable)
    if (auto Err = readSymbols(Obj, Slice, Opt))
      return Err;

  return Error::success();
}

Expected<Records> DylibReader::readFile(MemoryBufferRef Buffer,
                                        const ParseOption &Opt) {
  Records Results;

  auto BinOrErr = createBinary(Buffer);
  if (!BinOrErr)
    return BinOrErr.takeError();

  Binary &Bin = *BinOrErr.get();
  if (auto *Obj = dyn_cast<MachOObjectFile>(&Bin)) {
    const auto Arch = getArchitectureFromCpuType(Obj->getHeader().cputype,
                                                 Obj->getHeader().cpusubtype);
    if (!Opt.Archs.has(Arch))
      return make_error<TextAPIError>(TextAPIErrorCode::NoSuchArchitecture);

    auto Triples = constructTriples(Obj, Arch);
    for (const auto &T : Triples) {
      if (mapToPlatformType(T) == PLATFORM_UNKNOWN)
        return make_error<TextAPIError>(TextAPIErrorCode::UnsupportedTarget);
      Results.emplace_back(std::make_shared<RecordsSlice>(RecordsSlice({T})));
      if (auto Err = load(Obj, *Results.back(), Opt, Arch))
        return std::move(Err);
      Results.back()->getBinaryAttrs().Path = Buffer.getBufferIdentifier();
    }
    return Results;
  }

  // Only expect MachO universal binaries at this point.
  assert(isa<MachOUniversalBinary>(&Bin) &&
         "Expected a MachO universal binary.");
  auto *UB = cast<MachOUniversalBinary>(&Bin);

  for (auto OI = UB->begin_objects(), OE = UB->end_objects(); OI != OE; ++OI) {
    // Skip architecture if not requested.
    auto Arch =
        getArchitectureFromCpuType(OI->getCPUType(), OI->getCPUSubType());
    if (!Opt.Archs.has(Arch))
      continue;

    // Skip unknown architectures.
    if (Arch == AK_unknown)
      continue;

    // This can fail if the object is an archive.
    auto ObjOrErr = OI->getAsObjectFile();

    // Skip the archive and consume the error.
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      continue;
    }

    auto &Obj = *ObjOrErr.get();
    switch (Obj.getHeader().filetype) {
    default:
      break;
    case MachO::MH_BUNDLE:
    case MachO::MH_DYLIB:
    case MachO::MH_DYLIB_STUB:
      for (const auto &T : constructTriples(&Obj, Arch)) {
        Results.emplace_back(std::make_shared<RecordsSlice>(RecordsSlice({T})));
        if (auto Err = load(&Obj, *Results.back(), Opt, Arch))
          return std::move(Err);
        Results.back()->getBinaryAttrs().Path = Buffer.getBufferIdentifier();
      }
      break;
    }
  }

  if (Results.empty())
    return make_error<TextAPIError>(TextAPIErrorCode::EmptyResults);
  return Results;
}

Expected<std::unique_ptr<InterfaceFile>>
DylibReader::get(MemoryBufferRef Buffer) {
  ParseOption Options;
  auto SlicesOrErr = readFile(Buffer, Options);
  if (!SlicesOrErr)
    return SlicesOrErr.takeError();

  return convertToInterfaceFile(*SlicesOrErr);
}

static void DWARFErrorHandler(Error Err) { /**/ }

static SymbolToSourceLocMap
accumulateLocs(MachOObjectFile &Obj,
               const std::unique_ptr<DWARFContext> &DiCtx) {
  SymbolToSourceLocMap LocMap;
  for (const auto &Symbol : Obj.symbols()) {
    Expected<uint32_t> FlagsOrErr = Symbol.getFlags();
    if (!FlagsOrErr) {
      consumeError(FlagsOrErr.takeError());
      continue;
    }

    if (!(*FlagsOrErr & SymbolRef::SF_Exported))
      continue;

    Expected<uint64_t> AddressOrErr = Symbol.getAddress();
    if (!AddressOrErr) {
      consumeError(AddressOrErr.takeError());
      continue;
    }
    const uint64_t Address = *AddressOrErr;

    auto TypeOrErr = Symbol.getType();
    if (!TypeOrErr) {
      consumeError(TypeOrErr.takeError());
      continue;
    }
    const bool IsCode = (*TypeOrErr & SymbolRef::ST_Function);

    auto *DWARFCU = IsCode ? DiCtx->getCompileUnitForCodeAddress(Address)
                           : DiCtx->getCompileUnitForDataAddress(Address);
    if (!DWARFCU)
      continue;

    const DWARFDie &DIE = IsCode ? DWARFCU->getSubroutineForAddress(Address)
                                 : DWARFCU->getVariableForAddress(Address);
    const std::string File = DIE.getDeclFile(
        llvm::DILineInfoSpecifier::FileLineInfoKind::AbsoluteFilePath);
    const uint64_t Line = DIE.getDeclLine();

    auto NameOrErr = Symbol.getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      continue;
    }
    auto Name = *NameOrErr;
    auto Sym = parseSymbol(Name);

    if (!File.empty() && Line != 0)
      LocMap.insert({Sym.Name, RecordLoc(File, Line)});
  }

  return LocMap;
}

SymbolToSourceLocMap
DylibReader::accumulateSourceLocFromDSYM(const StringRef DSYM,
                                         const Target &T) {
  // Find sidecar file.
  auto DSYMsOrErr = MachOObjectFile::findDsymObjectMembers(DSYM);
  if (!DSYMsOrErr) {
    consumeError(DSYMsOrErr.takeError());
    return SymbolToSourceLocMap();
  }
  if (DSYMsOrErr->empty())
    return SymbolToSourceLocMap();

  const StringRef Path = DSYMsOrErr->front();
  auto BufOrErr = MemoryBuffer::getFile(Path);
  if (auto Err = BufOrErr.getError())
    return SymbolToSourceLocMap();

  auto BinOrErr = createBinary(*BufOrErr.get());
  if (!BinOrErr) {
    consumeError(BinOrErr.takeError());
    return SymbolToSourceLocMap();
  }
  // Handle single arch.
  if (auto *Single = dyn_cast<MachOObjectFile>(BinOrErr->get())) {
    auto DiCtx = DWARFContext::create(
        *Single, DWARFContext::ProcessDebugRelocations::Process, nullptr, "",
        DWARFErrorHandler, DWARFErrorHandler);

    return accumulateLocs(*Single, DiCtx);
  }
  // Handle universal companion file.
  if (auto *Fat = dyn_cast<MachOUniversalBinary>(BinOrErr->get())) {
    auto ObjForArch = Fat->getObjectForArch(getArchitectureName(T.Arch));
    if (!ObjForArch) {
      consumeError(ObjForArch.takeError());
      return SymbolToSourceLocMap();
    }
    auto MachOOrErr = ObjForArch->getAsObjectFile();
    if (!MachOOrErr) {
      consumeError(MachOOrErr.takeError());
      return SymbolToSourceLocMap();
    }
    auto &Obj = **MachOOrErr;
    auto DiCtx = DWARFContext::create(
        Obj, DWARFContext::ProcessDebugRelocations::Process, nullptr, "",
        DWARFErrorHandler, DWARFErrorHandler);

    return accumulateLocs(Obj, DiCtx);
  }
  return SymbolToSourceLocMap();
}
