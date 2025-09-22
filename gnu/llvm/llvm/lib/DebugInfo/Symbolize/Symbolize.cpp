//===-- LLVMSymbolize.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation for LLVM symbolization library.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/Symbolize.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/DebugInfo/BTF/BTFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/DebugInfo/PDB/PDBContext.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableObjectFile.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/BuildID.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace llvm {
namespace codeview {
union DebugInfo;
}
namespace symbolize {

LLVMSymbolizer::LLVMSymbolizer() = default;

LLVMSymbolizer::LLVMSymbolizer(const Options &Opts)
    : Opts(Opts),
      BIDFetcher(std::make_unique<BuildIDFetcher>(Opts.DebugFileDirectory)) {}

LLVMSymbolizer::~LLVMSymbolizer() = default;

template <typename T>
Expected<DILineInfo>
LLVMSymbolizer::symbolizeCodeCommon(const T &ModuleSpecifier,
                                    object::SectionedAddress ModuleOffset) {

  auto InfoOrErr = getOrCreateModuleInfo(ModuleSpecifier);
  if (!InfoOrErr)
    return InfoOrErr.takeError();

  SymbolizableModule *Info = *InfoOrErr;

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DILineInfo();

  // If the user is giving us relative addresses, add the preferred base of the
  // object to the offset before we do the query. It's what DIContext expects.
  if (Opts.RelativeAddresses)
    ModuleOffset.Address += Info->getModulePreferredBase();

  DILineInfo LineInfo = Info->symbolizeCode(
      ModuleOffset, DILineInfoSpecifier(Opts.PathStyle, Opts.PrintFunctions),
      Opts.UseSymbolTable);
  if (Opts.Demangle)
    LineInfo.FunctionName = DemangleName(LineInfo.FunctionName, Info);
  return LineInfo;
}

Expected<DILineInfo>
LLVMSymbolizer::symbolizeCode(const ObjectFile &Obj,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeCodeCommon(Obj, ModuleOffset);
}

Expected<DILineInfo>
LLVMSymbolizer::symbolizeCode(const std::string &ModuleName,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeCodeCommon(ModuleName, ModuleOffset);
}

Expected<DILineInfo>
LLVMSymbolizer::symbolizeCode(ArrayRef<uint8_t> BuildID,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeCodeCommon(BuildID, ModuleOffset);
}

template <typename T>
Expected<DIInliningInfo> LLVMSymbolizer::symbolizeInlinedCodeCommon(
    const T &ModuleSpecifier, object::SectionedAddress ModuleOffset) {
  auto InfoOrErr = getOrCreateModuleInfo(ModuleSpecifier);
  if (!InfoOrErr)
    return InfoOrErr.takeError();

  SymbolizableModule *Info = *InfoOrErr;

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DIInliningInfo();

  // If the user is giving us relative addresses, add the preferred base of the
  // object to the offset before we do the query. It's what DIContext expects.
  if (Opts.RelativeAddresses)
    ModuleOffset.Address += Info->getModulePreferredBase();

  DIInliningInfo InlinedContext = Info->symbolizeInlinedCode(
      ModuleOffset, DILineInfoSpecifier(Opts.PathStyle, Opts.PrintFunctions),
      Opts.UseSymbolTable);
  if (Opts.Demangle) {
    for (int i = 0, n = InlinedContext.getNumberOfFrames(); i < n; i++) {
      auto *Frame = InlinedContext.getMutableFrame(i);
      Frame->FunctionName = DemangleName(Frame->FunctionName, Info);
    }
  }
  return InlinedContext;
}

Expected<DIInliningInfo>
LLVMSymbolizer::symbolizeInlinedCode(const ObjectFile &Obj,
                                     object::SectionedAddress ModuleOffset) {
  return symbolizeInlinedCodeCommon(Obj, ModuleOffset);
}

Expected<DIInliningInfo>
LLVMSymbolizer::symbolizeInlinedCode(const std::string &ModuleName,
                                     object::SectionedAddress ModuleOffset) {
  return symbolizeInlinedCodeCommon(ModuleName, ModuleOffset);
}

Expected<DIInliningInfo>
LLVMSymbolizer::symbolizeInlinedCode(ArrayRef<uint8_t> BuildID,
                                     object::SectionedAddress ModuleOffset) {
  return symbolizeInlinedCodeCommon(BuildID, ModuleOffset);
}

template <typename T>
Expected<DIGlobal>
LLVMSymbolizer::symbolizeDataCommon(const T &ModuleSpecifier,
                                    object::SectionedAddress ModuleOffset) {

  auto InfoOrErr = getOrCreateModuleInfo(ModuleSpecifier);
  if (!InfoOrErr)
    return InfoOrErr.takeError();

  SymbolizableModule *Info = *InfoOrErr;
  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return DIGlobal();

  // If the user is giving us relative addresses, add the preferred base of
  // the object to the offset before we do the query. It's what DIContext
  // expects.
  if (Opts.RelativeAddresses)
    ModuleOffset.Address += Info->getModulePreferredBase();

  DIGlobal Global = Info->symbolizeData(ModuleOffset);
  if (Opts.Demangle)
    Global.Name = DemangleName(Global.Name, Info);
  return Global;
}

Expected<DIGlobal>
LLVMSymbolizer::symbolizeData(const ObjectFile &Obj,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeDataCommon(Obj, ModuleOffset);
}

Expected<DIGlobal>
LLVMSymbolizer::symbolizeData(const std::string &ModuleName,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeDataCommon(ModuleName, ModuleOffset);
}

Expected<DIGlobal>
LLVMSymbolizer::symbolizeData(ArrayRef<uint8_t> BuildID,
                              object::SectionedAddress ModuleOffset) {
  return symbolizeDataCommon(BuildID, ModuleOffset);
}

template <typename T>
Expected<std::vector<DILocal>>
LLVMSymbolizer::symbolizeFrameCommon(const T &ModuleSpecifier,
                                     object::SectionedAddress ModuleOffset) {
  auto InfoOrErr = getOrCreateModuleInfo(ModuleSpecifier);
  if (!InfoOrErr)
    return InfoOrErr.takeError();

  SymbolizableModule *Info = *InfoOrErr;
  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return std::vector<DILocal>();

  // If the user is giving us relative addresses, add the preferred base of
  // the object to the offset before we do the query. It's what DIContext
  // expects.
  if (Opts.RelativeAddresses)
    ModuleOffset.Address += Info->getModulePreferredBase();

  return Info->symbolizeFrame(ModuleOffset);
}

Expected<std::vector<DILocal>>
LLVMSymbolizer::symbolizeFrame(const ObjectFile &Obj,
                               object::SectionedAddress ModuleOffset) {
  return symbolizeFrameCommon(Obj, ModuleOffset);
}

Expected<std::vector<DILocal>>
LLVMSymbolizer::symbolizeFrame(const std::string &ModuleName,
                               object::SectionedAddress ModuleOffset) {
  return symbolizeFrameCommon(ModuleName, ModuleOffset);
}

Expected<std::vector<DILocal>>
LLVMSymbolizer::symbolizeFrame(ArrayRef<uint8_t> BuildID,
                               object::SectionedAddress ModuleOffset) {
  return symbolizeFrameCommon(BuildID, ModuleOffset);
}

template <typename T>
Expected<std::vector<DILineInfo>>
LLVMSymbolizer::findSymbolCommon(const T &ModuleSpecifier, StringRef Symbol,
                                 uint64_t Offset) {
  auto InfoOrErr = getOrCreateModuleInfo(ModuleSpecifier);
  if (!InfoOrErr)
    return InfoOrErr.takeError();

  SymbolizableModule *Info = *InfoOrErr;
  std::vector<DILineInfo> Result;

  // A null module means an error has already been reported. Return an empty
  // result.
  if (!Info)
    return Result;

  for (object::SectionedAddress A : Info->findSymbol(Symbol, Offset)) {
    DILineInfo LineInfo = Info->symbolizeCode(
        A, DILineInfoSpecifier(Opts.PathStyle, Opts.PrintFunctions),
        Opts.UseSymbolTable);
    if (LineInfo.FileName != DILineInfo::BadString) {
      if (Opts.Demangle)
        LineInfo.FunctionName = DemangleName(LineInfo.FunctionName, Info);
      Result.push_back(LineInfo);
    }
  }

  return Result;
}

Expected<std::vector<DILineInfo>>
LLVMSymbolizer::findSymbol(const ObjectFile &Obj, StringRef Symbol,
                           uint64_t Offset) {
  return findSymbolCommon(Obj, Symbol, Offset);
}

Expected<std::vector<DILineInfo>>
LLVMSymbolizer::findSymbol(const std::string &ModuleName, StringRef Symbol,
                           uint64_t Offset) {
  return findSymbolCommon(ModuleName, Symbol, Offset);
}

Expected<std::vector<DILineInfo>>
LLVMSymbolizer::findSymbol(ArrayRef<uint8_t> BuildID, StringRef Symbol,
                           uint64_t Offset) {
  return findSymbolCommon(BuildID, Symbol, Offset);
}

void LLVMSymbolizer::flush() {
  ObjectForUBPathAndArch.clear();
  LRUBinaries.clear();
  CacheSize = 0;
  BinaryForPath.clear();
  ObjectPairForPathArch.clear();
  Modules.clear();
  BuildIDPaths.clear();
}

namespace {

// For Path="/path/to/foo" and Basename="foo" assume that debug info is in
// /path/to/foo.dSYM/Contents/Resources/DWARF/foo.
// For Path="/path/to/bar.dSYM" and Basename="foo" assume that debug info is in
// /path/to/bar.dSYM/Contents/Resources/DWARF/foo.
std::string getDarwinDWARFResourceForPath(const std::string &Path,
                                          const std::string &Basename) {
  SmallString<16> ResourceName = StringRef(Path);
  if (sys::path::extension(Path) != ".dSYM") {
    ResourceName += ".dSYM";
  }
  sys::path::append(ResourceName, "Contents", "Resources", "DWARF");
  sys::path::append(ResourceName, Basename);
  return std::string(ResourceName);
}

bool checkFileCRC(StringRef Path, uint32_t CRCHash) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> MB =
      MemoryBuffer::getFileOrSTDIN(Path);
  if (!MB)
    return false;
  return CRCHash == llvm::crc32(arrayRefFromStringRef(MB.get()->getBuffer()));
}

bool getGNUDebuglinkContents(const ObjectFile *Obj, std::string &DebugName,
                             uint32_t &CRCHash) {
  if (!Obj)
    return false;
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name;
    consumeError(Section.getName().moveInto(Name));

    Name = Name.substr(Name.find_first_not_of("._"));
    if (Name == "gnu_debuglink") {
      Expected<StringRef> ContentsOrErr = Section.getContents();
      if (!ContentsOrErr) {
        consumeError(ContentsOrErr.takeError());
        return false;
      }
      DataExtractor DE(*ContentsOrErr, Obj->isLittleEndian(), 0);
      uint64_t Offset = 0;
      if (const char *DebugNameStr = DE.getCStr(&Offset)) {
        // 4-byte align the offset.
        Offset = (Offset + 3) & ~0x3;
        if (DE.isValidOffsetForDataOfSize(Offset, 4)) {
          DebugName = DebugNameStr;
          CRCHash = DE.getU32(&Offset);
          return true;
        }
      }
      break;
    }
  }
  return false;
}

bool darwinDsymMatchesBinary(const MachOObjectFile *DbgObj,
                             const MachOObjectFile *Obj) {
  ArrayRef<uint8_t> dbg_uuid = DbgObj->getUuid();
  ArrayRef<uint8_t> bin_uuid = Obj->getUuid();
  if (dbg_uuid.empty() || bin_uuid.empty())
    return false;
  return !memcmp(dbg_uuid.data(), bin_uuid.data(), dbg_uuid.size());
}

} // end anonymous namespace

ObjectFile *LLVMSymbolizer::lookUpDsymFile(const std::string &ExePath,
                                           const MachOObjectFile *MachExeObj,
                                           const std::string &ArchName) {
  // On Darwin we may find DWARF in separate object file in
  // resource directory.
  std::vector<std::string> DsymPaths;
  StringRef Filename = sys::path::filename(ExePath);
  DsymPaths.push_back(
      getDarwinDWARFResourceForPath(ExePath, std::string(Filename)));
  for (const auto &Path : Opts.DsymHints) {
    DsymPaths.push_back(
        getDarwinDWARFResourceForPath(Path, std::string(Filename)));
  }
  for (const auto &Path : DsymPaths) {
    auto DbgObjOrErr = getOrCreateObject(Path, ArchName);
    if (!DbgObjOrErr) {
      // Ignore errors, the file might not exist.
      consumeError(DbgObjOrErr.takeError());
      continue;
    }
    ObjectFile *DbgObj = DbgObjOrErr.get();
    if (!DbgObj)
      continue;
    const MachOObjectFile *MachDbgObj = dyn_cast<const MachOObjectFile>(DbgObj);
    if (!MachDbgObj)
      continue;
    if (darwinDsymMatchesBinary(MachDbgObj, MachExeObj))
      return DbgObj;
  }
  return nullptr;
}

ObjectFile *LLVMSymbolizer::lookUpDebuglinkObject(const std::string &Path,
                                                  const ObjectFile *Obj,
                                                  const std::string &ArchName) {
  std::string DebuglinkName;
  uint32_t CRCHash;
  std::string DebugBinaryPath;
  if (!getGNUDebuglinkContents(Obj, DebuglinkName, CRCHash))
    return nullptr;
  if (!findDebugBinary(Path, DebuglinkName, CRCHash, DebugBinaryPath))
    return nullptr;
  auto DbgObjOrErr = getOrCreateObject(DebugBinaryPath, ArchName);
  if (!DbgObjOrErr) {
    // Ignore errors, the file might not exist.
    consumeError(DbgObjOrErr.takeError());
    return nullptr;
  }
  return DbgObjOrErr.get();
}

ObjectFile *LLVMSymbolizer::lookUpBuildIDObject(const std::string &Path,
                                                const ELFObjectFileBase *Obj,
                                                const std::string &ArchName) {
  auto BuildID = getBuildID(Obj);
  if (BuildID.size() < 2)
    return nullptr;
  std::string DebugBinaryPath;
  if (!getOrFindDebugBinary(BuildID, DebugBinaryPath))
    return nullptr;
  auto DbgObjOrErr = getOrCreateObject(DebugBinaryPath, ArchName);
  if (!DbgObjOrErr) {
    consumeError(DbgObjOrErr.takeError());
    return nullptr;
  }
  return DbgObjOrErr.get();
}

bool LLVMSymbolizer::findDebugBinary(const std::string &OrigPath,
                                     const std::string &DebuglinkName,
                                     uint32_t CRCHash, std::string &Result) {
  SmallString<16> OrigDir(OrigPath);
  llvm::sys::path::remove_filename(OrigDir);
  SmallString<16> DebugPath = OrigDir;
  // Try relative/path/to/original_binary/debuglink_name
  llvm::sys::path::append(DebugPath, DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = std::string(DebugPath);
    return true;
  }
  // Try relative/path/to/original_binary/.debug/debuglink_name
  DebugPath = OrigDir;
  llvm::sys::path::append(DebugPath, ".debug", DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = std::string(DebugPath);
    return true;
  }
  // Make the path absolute so that lookups will go to
  // "/usr/lib/debug/full/path/to/debug", not
  // "/usr/lib/debug/to/debug"
  llvm::sys::fs::make_absolute(OrigDir);
  if (!Opts.FallbackDebugPath.empty()) {
    // Try <FallbackDebugPath>/absolute/path/to/original_binary/debuglink_name
    DebugPath = Opts.FallbackDebugPath;
  } else {
#if defined(__NetBSD__)
    // Try /usr/libdata/debug/absolute/path/to/original_binary/debuglink_name
    DebugPath = "/usr/libdata/debug";
#else
    // Try /usr/lib/debug/absolute/path/to/original_binary/debuglink_name
    DebugPath = "/usr/lib/debug";
#endif
  }
  llvm::sys::path::append(DebugPath, llvm::sys::path::relative_path(OrigDir),
                          DebuglinkName);
  if (checkFileCRC(DebugPath, CRCHash)) {
    Result = std::string(DebugPath);
    return true;
  }
  return false;
}

static StringRef getBuildIDStr(ArrayRef<uint8_t> BuildID) {
  return StringRef(reinterpret_cast<const char *>(BuildID.data()),
                   BuildID.size());
}

bool LLVMSymbolizer::getOrFindDebugBinary(const ArrayRef<uint8_t> BuildID,
                                          std::string &Result) {
  StringRef BuildIDStr = getBuildIDStr(BuildID);
  auto I = BuildIDPaths.find(BuildIDStr);
  if (I != BuildIDPaths.end()) {
    Result = I->second;
    return true;
  }
  if (!BIDFetcher)
    return false;
  if (std::optional<std::string> Path = BIDFetcher->fetch(BuildID)) {
    Result = *Path;
    auto InsertResult = BuildIDPaths.insert({BuildIDStr, Result});
    assert(InsertResult.second);
    (void)InsertResult;
    return true;
  }

  return false;
}

Expected<LLVMSymbolizer::ObjectPair>
LLVMSymbolizer::getOrCreateObjectPair(const std::string &Path,
                                      const std::string &ArchName) {
  auto I = ObjectPairForPathArch.find(std::make_pair(Path, ArchName));
  if (I != ObjectPairForPathArch.end()) {
    recordAccess(BinaryForPath.find(Path)->second);
    return I->second;
  }

  auto ObjOrErr = getOrCreateObject(Path, ArchName);
  if (!ObjOrErr) {
    ObjectPairForPathArch.emplace(std::make_pair(Path, ArchName),
                                  ObjectPair(nullptr, nullptr));
    return ObjOrErr.takeError();
  }

  ObjectFile *Obj = ObjOrErr.get();
  assert(Obj != nullptr);
  ObjectFile *DbgObj = nullptr;

  if (auto MachObj = dyn_cast<const MachOObjectFile>(Obj))
    DbgObj = lookUpDsymFile(Path, MachObj, ArchName);
  else if (auto ELFObj = dyn_cast<const ELFObjectFileBase>(Obj))
    DbgObj = lookUpBuildIDObject(Path, ELFObj, ArchName);
  if (!DbgObj)
    DbgObj = lookUpDebuglinkObject(Path, Obj, ArchName);
  if (!DbgObj)
    DbgObj = Obj;
  ObjectPair Res = std::make_pair(Obj, DbgObj);
  std::string DbgObjPath = DbgObj->getFileName().str();
  auto Pair =
      ObjectPairForPathArch.emplace(std::make_pair(Path, ArchName), Res);
  BinaryForPath.find(DbgObjPath)->second.pushEvictor([this, I = Pair.first]() {
    ObjectPairForPathArch.erase(I);
  });
  return Res;
}

Expected<ObjectFile *>
LLVMSymbolizer::getOrCreateObject(const std::string &Path,
                                  const std::string &ArchName) {
  Binary *Bin;
  auto Pair = BinaryForPath.emplace(Path, OwningBinary<Binary>());
  if (!Pair.second) {
    Bin = Pair.first->second->getBinary();
    recordAccess(Pair.first->second);
  } else {
    Expected<OwningBinary<Binary>> BinOrErr = createBinary(Path);
    if (!BinOrErr)
      return BinOrErr.takeError();

    CachedBinary &CachedBin = Pair.first->second;
    CachedBin = std::move(BinOrErr.get());
    CachedBin.pushEvictor([this, I = Pair.first]() { BinaryForPath.erase(I); });
    LRUBinaries.push_back(CachedBin);
    CacheSize += CachedBin.size();
    Bin = CachedBin->getBinary();
  }

  if (!Bin)
    return static_cast<ObjectFile *>(nullptr);

  if (MachOUniversalBinary *UB = dyn_cast_or_null<MachOUniversalBinary>(Bin)) {
    auto I = ObjectForUBPathAndArch.find(std::make_pair(Path, ArchName));
    if (I != ObjectForUBPathAndArch.end())
      return I->second.get();

    Expected<std::unique_ptr<ObjectFile>> ObjOrErr =
        UB->getMachOObjectForArch(ArchName);
    if (!ObjOrErr) {
      ObjectForUBPathAndArch.emplace(std::make_pair(Path, ArchName),
                                     std::unique_ptr<ObjectFile>());
      return ObjOrErr.takeError();
    }
    ObjectFile *Res = ObjOrErr->get();
    auto Pair = ObjectForUBPathAndArch.emplace(std::make_pair(Path, ArchName),
                                               std::move(ObjOrErr.get()));
    BinaryForPath.find(Path)->second.pushEvictor(
        [this, Iter = Pair.first]() { ObjectForUBPathAndArch.erase(Iter); });
    return Res;
  }
  if (Bin->isObject()) {
    return cast<ObjectFile>(Bin);
  }
  return errorCodeToError(object_error::arch_not_found);
}

Expected<SymbolizableModule *>
LLVMSymbolizer::createModuleInfo(const ObjectFile *Obj,
                                 std::unique_ptr<DIContext> Context,
                                 StringRef ModuleName) {
  auto InfoOrErr = SymbolizableObjectFile::create(Obj, std::move(Context),
                                                  Opts.UntagAddresses);
  std::unique_ptr<SymbolizableModule> SymMod;
  if (InfoOrErr)
    SymMod = std::move(*InfoOrErr);
  auto InsertResult = Modules.insert(
      std::make_pair(std::string(ModuleName), std::move(SymMod)));
  assert(InsertResult.second);
  if (!InfoOrErr)
    return InfoOrErr.takeError();
  return InsertResult.first->second.get();
}

Expected<SymbolizableModule *>
LLVMSymbolizer::getOrCreateModuleInfo(const std::string &ModuleName) {
  std::string BinaryName = ModuleName;
  std::string ArchName = Opts.DefaultArch;
  size_t ColonPos = ModuleName.find_last_of(':');
  // Verify that substring after colon form a valid arch name.
  if (ColonPos != std::string::npos) {
    std::string ArchStr = ModuleName.substr(ColonPos + 1);
    if (Triple(ArchStr).getArch() != Triple::UnknownArch) {
      BinaryName = ModuleName.substr(0, ColonPos);
      ArchName = ArchStr;
    }
  }

  auto I = Modules.find(ModuleName);
  if (I != Modules.end()) {
    recordAccess(BinaryForPath.find(BinaryName)->second);
    return I->second.get();
  }

  auto ObjectsOrErr = getOrCreateObjectPair(BinaryName, ArchName);
  if (!ObjectsOrErr) {
    // Failed to find valid object file.
    Modules.emplace(ModuleName, std::unique_ptr<SymbolizableModule>());
    return ObjectsOrErr.takeError();
  }
  ObjectPair Objects = ObjectsOrErr.get();

  std::unique_ptr<DIContext> Context;
  // If this is a COFF object containing PDB info and not containing DWARF
  // section, use a PDBContext to symbolize. Otherwise, use DWARF.
  if (auto CoffObject = dyn_cast<COFFObjectFile>(Objects.first)) {
    const codeview::DebugInfo *DebugInfo;
    StringRef PDBFileName;
    auto EC = CoffObject->getDebugPDBInfo(DebugInfo, PDBFileName);
    // Use DWARF if there're DWARF sections.
    bool HasDwarf =
        llvm::any_of(Objects.first->sections(), [](SectionRef Section) -> bool {
          if (Expected<StringRef> SectionName = Section.getName())
            return SectionName.get() == ".debug_info";
          return false;
        });
    if (!EC && !HasDwarf && DebugInfo != nullptr && !PDBFileName.empty()) {
      using namespace pdb;
      std::unique_ptr<IPDBSession> Session;

      PDB_ReaderType ReaderType =
          Opts.UseDIA ? PDB_ReaderType::DIA : PDB_ReaderType::Native;
      if (auto Err = loadDataForEXE(ReaderType, Objects.first->getFileName(),
                                    Session)) {
        Modules.emplace(ModuleName, std::unique_ptr<SymbolizableModule>());
        // Return along the PDB filename to provide more context
        return createFileError(PDBFileName, std::move(Err));
      }
      Context.reset(new PDBContext(*CoffObject, std::move(Session)));
    }
  }
  if (!Context)
    Context = DWARFContext::create(
        *Objects.second, DWARFContext::ProcessDebugRelocations::Process,
        nullptr, Opts.DWPName);
  auto ModuleOrErr =
      createModuleInfo(Objects.first, std::move(Context), ModuleName);
  if (ModuleOrErr) {
    auto I = Modules.find(ModuleName);
    BinaryForPath.find(BinaryName)->second.pushEvictor([this, I]() {
      Modules.erase(I);
    });
  }
  return ModuleOrErr;
}

// For BPF programs .BTF.ext section contains line numbers information,
// use it if regular DWARF is not available (e.g. for stripped binary).
static bool useBTFContext(const ObjectFile &Obj) {
  return Obj.makeTriple().isBPF() && !Obj.hasDebugInfo() &&
         BTFParser::hasBTFSections(Obj);
}

Expected<SymbolizableModule *>
LLVMSymbolizer::getOrCreateModuleInfo(const ObjectFile &Obj) {
  StringRef ObjName = Obj.getFileName();
  auto I = Modules.find(ObjName);
  if (I != Modules.end())
    return I->second.get();

  std::unique_ptr<DIContext> Context;
  if (useBTFContext(Obj))
    Context = BTFContext::create(Obj);
  else
    Context = DWARFContext::create(Obj);
  // FIXME: handle COFF object with PDB info to use PDBContext
  return createModuleInfo(&Obj, std::move(Context), ObjName);
}

Expected<SymbolizableModule *>
LLVMSymbolizer::getOrCreateModuleInfo(ArrayRef<uint8_t> BuildID) {
  std::string Path;
  if (!getOrFindDebugBinary(BuildID, Path)) {
    return createStringError(errc::no_such_file_or_directory,
                             "could not find build ID");
  }
  return getOrCreateModuleInfo(Path);
}

namespace {

// Undo these various manglings for Win32 extern "C" functions:
// cdecl       - _foo
// stdcall     - _foo@12
// fastcall    - @foo@12
// vectorcall  - foo@@12
// These are all different linkage names for 'foo'.
StringRef demanglePE32ExternCFunc(StringRef SymbolName) {
  char Front = SymbolName.empty() ? '\0' : SymbolName[0];

  // Remove any '@[0-9]+' suffix.
  bool HasAtNumSuffix = false;
  if (Front != '?') {
    size_t AtPos = SymbolName.rfind('@');
    if (AtPos != StringRef::npos &&
        all_of(drop_begin(SymbolName, AtPos + 1), isDigit)) {
      SymbolName = SymbolName.substr(0, AtPos);
      HasAtNumSuffix = true;
    }
  }

  // Remove any ending '@' for vectorcall.
  bool IsVectorCall = false;
  if (HasAtNumSuffix && SymbolName.ends_with("@")) {
    SymbolName = SymbolName.drop_back();
    IsVectorCall = true;
  }

  // If not vectorcall, remove any '_' or '@' prefix.
  if (!IsVectorCall && (Front == '_' || Front == '@'))
    SymbolName = SymbolName.drop_front();

  return SymbolName;
}

} // end anonymous namespace

std::string
LLVMSymbolizer::DemangleName(const std::string &Name,
                             const SymbolizableModule *DbiModuleDescriptor) {
  std::string Result;
  if (nonMicrosoftDemangle(Name, Result))
    return Result;

  if (!Name.empty() && Name.front() == '?') {
    // Only do MSVC C++ demangling on symbols starting with '?'.
    int status = 0;
    char *DemangledName = microsoftDemangle(
        Name, nullptr, &status,
        MSDemangleFlags(MSDF_NoAccessSpecifier | MSDF_NoCallingConvention |
                        MSDF_NoMemberType | MSDF_NoReturnType));
    if (status != 0)
      return Name;
    Result = DemangledName;
    free(DemangledName);
    return Result;
  }

  if (DbiModuleDescriptor && DbiModuleDescriptor->isWin32Module()) {
    std::string DemangledCName(demanglePE32ExternCFunc(Name));
    // On i386 Windows, the C name mangling for different calling conventions
    // may also be applied on top of the Itanium or Rust name mangling.
    if (nonMicrosoftDemangle(DemangledCName, Result))
      return Result;
    return DemangledCName;
  }
  return Name;
}

void LLVMSymbolizer::recordAccess(CachedBinary &Bin) {
  if (Bin->getBinary())
    LRUBinaries.splice(LRUBinaries.end(), LRUBinaries, Bin.getIterator());
}

void LLVMSymbolizer::pruneCache() {
  // Evict the LRU binary until the max cache size is reached or there's <= 1
  // item in the cache. The MRU binary is always kept to avoid thrashing if it's
  // larger than the cache size.
  while (CacheSize > Opts.MaxCacheSize && !LRUBinaries.empty() &&
         std::next(LRUBinaries.begin()) != LRUBinaries.end()) {
    CachedBinary &Bin = LRUBinaries.front();
    CacheSize -= Bin.size();
    LRUBinaries.pop_front();
    Bin.evict();
  }
}

void CachedBinary::pushEvictor(std::function<void()> NewEvictor) {
  if (Evictor) {
    this->Evictor = [OldEvictor = std::move(this->Evictor),
                     NewEvictor = std::move(NewEvictor)]() {
      NewEvictor();
      OldEvictor();
    };
  } else {
    this->Evictor = std::move(NewEvictor);
  }
}

} // namespace symbolize
} // namespace llvm
