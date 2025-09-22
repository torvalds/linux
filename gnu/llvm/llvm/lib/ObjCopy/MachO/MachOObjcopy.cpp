//===- MachOObjcopy.cpp -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/MachO/MachOObjcopy.h"
#include "Archive.h"
#include "MachOReader.h"
#include "MachOWriter.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/ObjCopy/MachO/MachOConfig.h"
#include "llvm/ObjCopy/MultiFormatConfig.h"
#include "llvm/ObjCopy/ObjCopy.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/MachOUniversalWriter.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"

using namespace llvm;
using namespace llvm::objcopy;
using namespace llvm::objcopy::macho;
using namespace llvm::object;

using SectionPred = std::function<bool(const std::unique_ptr<Section> &Sec)>;
using LoadCommandPred = std::function<bool(const LoadCommand &LC)>;

#ifndef NDEBUG
static bool isLoadCommandWithPayloadString(const LoadCommand &LC) {
  // TODO: Add support for LC_REEXPORT_DYLIB, LC_LOAD_UPWARD_DYLIB and
  // LC_LAZY_LOAD_DYLIB
  return LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_RPATH ||
         LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_ID_DYLIB ||
         LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_LOAD_DYLIB ||
         LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_LOAD_WEAK_DYLIB;
}
#endif

static StringRef getPayloadString(const LoadCommand &LC) {
  assert(isLoadCommandWithPayloadString(LC) &&
         "unsupported load command encountered");

  return StringRef(reinterpret_cast<const char *>(LC.Payload.data()),
                   LC.Payload.size())
      .rtrim('\0');
}

static Error removeSections(const CommonConfig &Config, Object &Obj) {
  SectionPred RemovePred = [](const std::unique_ptr<Section> &) {
    return false;
  };

  if (!Config.ToRemove.empty()) {
    RemovePred = [&Config, RemovePred](const std::unique_ptr<Section> &Sec) {
      return Config.ToRemove.matches(Sec->CanonicalName);
    };
  }

  if (Config.StripAll || Config.StripDebug) {
    // Remove all debug sections.
    RemovePred = [RemovePred](const std::unique_ptr<Section> &Sec) {
      if (Sec->Segname == "__DWARF")
        return true;

      return RemovePred(Sec);
    };
  }

  if (!Config.OnlySection.empty()) {
    // Overwrite RemovePred because --only-section takes priority.
    RemovePred = [&Config](const std::unique_ptr<Section> &Sec) {
      return !Config.OnlySection.matches(Sec->CanonicalName);
    };
  }

  return Obj.removeSections(RemovePred);
}

static void markSymbols(const CommonConfig &, Object &Obj) {
  // Symbols referenced from the indirect symbol table must not be removed.
  for (IndirectSymbolEntry &ISE : Obj.IndirectSymTable.Symbols)
    if (ISE.Symbol)
      (*ISE.Symbol)->Referenced = true;
}

static void updateAndRemoveSymbols(const CommonConfig &Config,
                                   const MachOConfig &MachOConfig,
                                   Object &Obj) {
  for (SymbolEntry &Sym : Obj.SymTable) {
    // Weaken symbols first to match ELFObjcopy behavior.
    bool IsExportedAndDefined =
        (Sym.n_type & llvm::MachO::N_EXT) &&
        (Sym.n_type & llvm::MachO::N_TYPE) != llvm::MachO::N_UNDF;
    if (IsExportedAndDefined &&
        (Config.Weaken || Config.SymbolsToWeaken.matches(Sym.Name)))
      Sym.n_desc |= llvm::MachO::N_WEAK_DEF;

    auto I = Config.SymbolsToRename.find(Sym.Name);
    if (I != Config.SymbolsToRename.end())
      Sym.Name = std::string(I->getValue());
  }

  auto RemovePred = [&Config, &MachOConfig,
                     &Obj](const std::unique_ptr<SymbolEntry> &N) {
    if (N->Referenced)
      return false;
    if (MachOConfig.KeepUndefined && N->isUndefinedSymbol())
      return false;
    if (N->n_desc & MachO::REFERENCED_DYNAMICALLY)
      return false;
    if (Config.StripAll)
      return true;
    if (Config.DiscardMode == DiscardType::All && !(N->n_type & MachO::N_EXT))
      return true;
    // This behavior is consistent with cctools' strip.
    if (Config.StripDebug && (N->n_type & MachO::N_STAB))
      return true;
    // This behavior is consistent with cctools' strip.
    if (MachOConfig.StripSwiftSymbols &&
        (Obj.Header.Flags & MachO::MH_DYLDLINK) && Obj.SwiftVersion &&
        *Obj.SwiftVersion && N->isSwiftSymbol())
      return true;
    return false;
  };

  Obj.SymTable.removeSymbols(RemovePred);
}

template <typename LCType>
static void updateLoadCommandPayloadString(LoadCommand &LC, StringRef S) {
  assert(isLoadCommandWithPayloadString(LC) &&
         "unsupported load command encountered");

  uint32_t NewCmdsize = alignTo(sizeof(LCType) + S.size() + 1, 8);

  LC.MachOLoadCommand.load_command_data.cmdsize = NewCmdsize;
  LC.Payload.assign(NewCmdsize - sizeof(LCType), 0);
  std::copy(S.begin(), S.end(), LC.Payload.begin());
}

static LoadCommand buildRPathLoadCommand(StringRef Path) {
  LoadCommand LC;
  MachO::rpath_command RPathLC;
  RPathLC.cmd = MachO::LC_RPATH;
  RPathLC.path = sizeof(MachO::rpath_command);
  RPathLC.cmdsize = alignTo(sizeof(MachO::rpath_command) + Path.size() + 1, 8);
  LC.MachOLoadCommand.rpath_command_data = RPathLC;
  LC.Payload.assign(RPathLC.cmdsize - sizeof(MachO::rpath_command), 0);
  std::copy(Path.begin(), Path.end(), LC.Payload.begin());
  return LC;
}

static Error processLoadCommands(const MachOConfig &MachOConfig, Object &Obj) {
  // Remove RPaths.
  DenseSet<StringRef> RPathsToRemove(MachOConfig.RPathsToRemove.begin(),
                                     MachOConfig.RPathsToRemove.end());

  LoadCommandPred RemovePred = [&RPathsToRemove,
                                &MachOConfig](const LoadCommand &LC) {
    if (LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_RPATH) {
      // When removing all RPaths we don't need to care
      // about what it contains
      if (MachOConfig.RemoveAllRpaths)
        return true;

      StringRef RPath = getPayloadString(LC);
      if (RPathsToRemove.count(RPath)) {
        RPathsToRemove.erase(RPath);
        return true;
      }
    }
    return false;
  };

  if (Error E = Obj.removeLoadCommands(RemovePred))
    return E;

  // Emit an error if the Mach-O binary does not contain an rpath path name
  // specified in -delete_rpath.
  for (StringRef RPath : MachOConfig.RPathsToRemove) {
    if (RPathsToRemove.count(RPath))
      return createStringError(errc::invalid_argument,
                               "no LC_RPATH load command with path: %s",
                               RPath.str().c_str());
  }

  DenseSet<StringRef> RPaths;

  // Get all existing RPaths.
  for (LoadCommand &LC : Obj.LoadCommands) {
    if (LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_RPATH)
      RPaths.insert(getPayloadString(LC));
  }

  // Throw errors for invalid RPaths.
  for (const auto &OldNew : MachOConfig.RPathsToUpdate) {
    StringRef Old = OldNew.getFirst();
    StringRef New = OldNew.getSecond();
    if (!RPaths.contains(Old))
      return createStringError(errc::invalid_argument,
                               "no LC_RPATH load command with path: " + Old);
    if (RPaths.contains(New))
      return createStringError(errc::invalid_argument,
                               "rpath '" + New +
                                   "' would create a duplicate load command");
  }

  // Update load commands.
  for (LoadCommand &LC : Obj.LoadCommands) {
    switch (LC.MachOLoadCommand.load_command_data.cmd) {
    case MachO::LC_ID_DYLIB:
      if (MachOConfig.SharedLibId)
        updateLoadCommandPayloadString<MachO::dylib_command>(
            LC, *MachOConfig.SharedLibId);
      break;

    case MachO::LC_RPATH: {
      StringRef RPath = getPayloadString(LC);
      StringRef NewRPath = MachOConfig.RPathsToUpdate.lookup(RPath);
      if (!NewRPath.empty())
        updateLoadCommandPayloadString<MachO::rpath_command>(LC, NewRPath);
      break;
    }

    // TODO: Add LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB, and LC_LOAD_UPWARD_DYLIB
    // here once llvm-objcopy supports them.
    case MachO::LC_LOAD_DYLIB:
    case MachO::LC_LOAD_WEAK_DYLIB:
      StringRef InstallName = getPayloadString(LC);
      StringRef NewInstallName =
          MachOConfig.InstallNamesToUpdate.lookup(InstallName);
      if (!NewInstallName.empty())
        updateLoadCommandPayloadString<MachO::dylib_command>(LC,
                                                             NewInstallName);
      break;
    }
  }

  // Add new RPaths.
  for (StringRef RPath : MachOConfig.RPathToAdd) {
    if (RPaths.contains(RPath))
      return createStringError(errc::invalid_argument,
                               "rpath '" + RPath +
                                   "' would create a duplicate load command");
    RPaths.insert(RPath);
    Obj.LoadCommands.push_back(buildRPathLoadCommand(RPath));
  }

  for (StringRef RPath : MachOConfig.RPathToPrepend) {
    if (RPaths.contains(RPath))
      return createStringError(errc::invalid_argument,
                               "rpath '" + RPath +
                                   "' would create a duplicate load command");

    RPaths.insert(RPath);
    Obj.LoadCommands.insert(Obj.LoadCommands.begin(),
                            buildRPathLoadCommand(RPath));
  }

  // Unlike appending rpaths, the indexes of subsequent load commands must
  // be recalculated after prepending one.
  if (!MachOConfig.RPathToPrepend.empty())
    Obj.updateLoadCommandIndexes();

  // Remove any empty segments if required.
  if (!MachOConfig.EmptySegmentsToRemove.empty()) {
    auto RemovePred = [&MachOConfig](const LoadCommand &LC) {
      if (LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_SEGMENT_64 ||
          LC.MachOLoadCommand.load_command_data.cmd == MachO::LC_SEGMENT) {
        return LC.Sections.empty() &&
               MachOConfig.EmptySegmentsToRemove.contains(*LC.getSegmentName());
      }
      return false;
    };
    if (Error E = Obj.removeLoadCommands(RemovePred))
      return E;
  }

  return Error::success();
}

static Error dumpSectionToFile(StringRef SecName, StringRef Filename,
                               Object &Obj) {
  for (LoadCommand &LC : Obj.LoadCommands)
    for (const std::unique_ptr<Section> &Sec : LC.Sections) {
      if (Sec->CanonicalName == SecName) {
        Expected<std::unique_ptr<FileOutputBuffer>> BufferOrErr =
            FileOutputBuffer::create(Filename, Sec->Content.size());
        if (!BufferOrErr)
          return BufferOrErr.takeError();
        std::unique_ptr<FileOutputBuffer> Buf = std::move(*BufferOrErr);
        llvm::copy(Sec->Content, Buf->getBufferStart());

        if (Error E = Buf->commit())
          return E;
        return Error::success();
      }
    }

  return createStringError(object_error::parse_failed, "section '%s' not found",
                           SecName.str().c_str());
}

static Error addSection(const NewSectionInfo &NewSection, Object &Obj) {
  std::pair<StringRef, StringRef> Pair = NewSection.SectionName.split(',');
  StringRef TargetSegName = Pair.first;
  Section Sec(TargetSegName, Pair.second);
  Sec.Content =
      Obj.NewSectionsContents.save(NewSection.SectionData->getBuffer());
  Sec.Size = Sec.Content.size();

  // Add the a section into an existing segment.
  for (LoadCommand &LC : Obj.LoadCommands) {
    std::optional<StringRef> SegName = LC.getSegmentName();
    if (SegName && SegName == TargetSegName) {
      uint64_t Addr = *LC.getSegmentVMAddr();
      for (const std::unique_ptr<Section> &S : LC.Sections)
        Addr = std::max(Addr, S->Addr + S->Size);
      LC.Sections.push_back(std::make_unique<Section>(Sec));
      LC.Sections.back()->Addr = Addr;
      return Error::success();
    }
  }

  // There's no segment named TargetSegName. Create a new load command and
  // Insert a new section into it.
  LoadCommand &NewSegment =
      Obj.addSegment(TargetSegName, alignTo(Sec.Size, 16384));
  NewSegment.Sections.push_back(std::make_unique<Section>(Sec));
  NewSegment.Sections.back()->Addr = *NewSegment.getSegmentVMAddr();
  return Error::success();
}

static Expected<Section &> findSection(StringRef SecName, Object &O) {
  StringRef SegName;
  std::tie(SegName, SecName) = SecName.split(",");
  auto FoundSeg =
      llvm::find_if(O.LoadCommands, [SegName](const LoadCommand &LC) {
        return LC.getSegmentName() == SegName;
      });
  if (FoundSeg == O.LoadCommands.end())
    return createStringError(errc::invalid_argument,
                             "could not find segment with name '%s'",
                             SegName.str().c_str());
  auto FoundSec = llvm::find_if(FoundSeg->Sections,
                                [SecName](const std::unique_ptr<Section> &Sec) {
                                  return Sec->Sectname == SecName;
                                });
  if (FoundSec == FoundSeg->Sections.end())
    return createStringError(errc::invalid_argument,
                             "could not find section with name '%s'",
                             SecName.str().c_str());

  assert(FoundSec->get()->CanonicalName == (SegName + "," + SecName).str());
  return **FoundSec;
}

static Error updateSection(const NewSectionInfo &NewSection, Object &O) {
  Expected<Section &> SecToUpdateOrErr = findSection(NewSection.SectionName, O);

  if (!SecToUpdateOrErr)
    return SecToUpdateOrErr.takeError();
  Section &Sec = *SecToUpdateOrErr;

  if (NewSection.SectionData->getBufferSize() > Sec.Size)
    return createStringError(
        errc::invalid_argument,
        "new section cannot be larger than previous section");
  Sec.Content = O.NewSectionsContents.save(NewSection.SectionData->getBuffer());
  Sec.Size = Sec.Content.size();
  return Error::success();
}

// isValidMachOCannonicalName returns success if Name is a MachO cannonical name
// ("<segment>,<section>") and lengths of both segment and section names are
// valid.
static Error isValidMachOCannonicalName(StringRef Name) {
  if (Name.count(',') != 1)
    return createStringError(errc::invalid_argument,
                             "invalid section name '%s' (should be formatted "
                             "as '<segment name>,<section name>')",
                             Name.str().c_str());

  std::pair<StringRef, StringRef> Pair = Name.split(',');
  if (Pair.first.size() > 16)
    return createStringError(errc::invalid_argument,
                             "too long segment name: '%s'",
                             Pair.first.str().c_str());
  if (Pair.second.size() > 16)
    return createStringError(errc::invalid_argument,
                             "too long section name: '%s'",
                             Pair.second.str().c_str());
  return Error::success();
}

static Error handleArgs(const CommonConfig &Config,
                        const MachOConfig &MachOConfig, Object &Obj) {
  // Dump sections before add/remove for compatibility with GNU objcopy.
  for (StringRef Flag : Config.DumpSection) {
    StringRef SectionName;
    StringRef FileName;
    std::tie(SectionName, FileName) = Flag.split('=');
    if (Error E = dumpSectionToFile(SectionName, FileName, Obj))
      return E;
  }

  if (Error E = removeSections(Config, Obj))
    return E;

  // Mark symbols to determine which symbols are still needed.
  if (Config.StripAll)
    markSymbols(Config, Obj);

  updateAndRemoveSymbols(Config, MachOConfig, Obj);

  if (Config.StripAll)
    for (LoadCommand &LC : Obj.LoadCommands)
      for (std::unique_ptr<Section> &Sec : LC.Sections)
        Sec->Relocations.clear();

  for (const NewSectionInfo &NewSection : Config.AddSection) {
    if (Error E = isValidMachOCannonicalName(NewSection.SectionName))
      return E;
    if (Error E = addSection(NewSection, Obj))
      return E;
  }

  for (const NewSectionInfo &NewSection : Config.UpdateSection) {
    if (Error E = isValidMachOCannonicalName(NewSection.SectionName))
      return E;
    if (Error E = updateSection(NewSection, Obj))
      return E;
  }

  if (Error E = processLoadCommands(MachOConfig, Obj))
    return E;

  return Error::success();
}

Error objcopy::macho::executeObjcopyOnBinary(const CommonConfig &Config,
                                             const MachOConfig &MachOConfig,
                                             object::MachOObjectFile &In,
                                             raw_ostream &Out) {
  MachOReader Reader(In);
  Expected<std::unique_ptr<Object>> O = Reader.create();
  if (!O)
    return createFileError(Config.InputFilename, O.takeError());

  if (O->get()->Header.FileType == MachO::HeaderFileType::MH_PRELOAD)
    return createStringError(std::errc::not_supported,
                             "%s: MH_PRELOAD files are not supported",
                             Config.InputFilename.str().c_str());

  if (Error E = handleArgs(Config, MachOConfig, **O))
    return createFileError(Config.InputFilename, std::move(E));

  // Page size used for alignment of segment sizes in Mach-O executables and
  // dynamic libraries.
  uint64_t PageSize;
  switch (In.getArch()) {
  case Triple::ArchType::arm:
  case Triple::ArchType::aarch64:
  case Triple::ArchType::aarch64_32:
    PageSize = 16384;
    break;
  default:
    PageSize = 4096;
  }

  MachOWriter Writer(**O, In.is64Bit(), In.isLittleEndian(),
                     sys::path::filename(Config.OutputFilename), PageSize, Out);
  if (auto E = Writer.finalize())
    return E;
  return Writer.write();
}

Error objcopy::macho::executeObjcopyOnMachOUniversalBinary(
    const MultiFormatConfig &Config, const MachOUniversalBinary &In,
    raw_ostream &Out) {
  SmallVector<OwningBinary<Binary>, 2> Binaries;
  SmallVector<Slice, 2> Slices;
  for (const auto &O : In.objects()) {
    Expected<std::unique_ptr<Archive>> ArOrErr = O.getAsArchive();
    if (ArOrErr) {
      Expected<std::vector<NewArchiveMember>> NewArchiveMembersOrErr =
          createNewArchiveMembers(Config, **ArOrErr);
      if (!NewArchiveMembersOrErr)
        return NewArchiveMembersOrErr.takeError();
      auto Kind = (*ArOrErr)->kind();
      if (Kind == object::Archive::K_BSD)
        Kind = object::Archive::K_DARWIN;
      Expected<std::unique_ptr<MemoryBuffer>> OutputBufferOrErr =
          writeArchiveToBuffer(
              *NewArchiveMembersOrErr,
              (*ArOrErr)->hasSymbolTable() ? SymtabWritingMode::NormalSymtab
                                           : SymtabWritingMode::NoSymtab,
              Kind, Config.getCommonConfig().DeterministicArchives,
              (*ArOrErr)->isThin());
      if (!OutputBufferOrErr)
        return OutputBufferOrErr.takeError();
      Expected<std::unique_ptr<Binary>> BinaryOrErr =
          object::createBinary(**OutputBufferOrErr);
      if (!BinaryOrErr)
        return BinaryOrErr.takeError();
      Binaries.emplace_back(std::move(*BinaryOrErr),
                            std::move(*OutputBufferOrErr));
      Slices.emplace_back(*cast<Archive>(Binaries.back().getBinary()),
                          O.getCPUType(), O.getCPUSubType(),
                          O.getArchFlagName(), O.getAlign());
      continue;
    }
    // The methods getAsArchive, getAsObjectFile, getAsIRObject of the class
    // ObjectForArch return an Error in case of the type mismatch. We need to
    // check each in turn to see what kind of slice this is, so ignore errors
    // produced along the way.
    consumeError(ArOrErr.takeError());

    Expected<std::unique_ptr<MachOObjectFile>> ObjOrErr = O.getAsObjectFile();
    if (!ObjOrErr) {
      consumeError(ObjOrErr.takeError());
      return createStringError(
          std::errc::invalid_argument,
          "slice for '%s' of the universal Mach-O binary "
          "'%s' is not a Mach-O object or an archive",
          O.getArchFlagName().c_str(),
          Config.getCommonConfig().InputFilename.str().c_str());
    }
    std::string ArchFlagName = O.getArchFlagName();

    SmallVector<char, 0> Buffer;
    raw_svector_ostream MemStream(Buffer);

    Expected<const MachOConfig &> MachO = Config.getMachOConfig();
    if (!MachO)
      return MachO.takeError();

    if (Error E = executeObjcopyOnBinary(Config.getCommonConfig(), *MachO,
                                         **ObjOrErr, MemStream))
      return E;

    auto MB = std::make_unique<SmallVectorMemoryBuffer>(
        std::move(Buffer), ArchFlagName, /*RequiresNullTerminator=*/false);
    Expected<std::unique_ptr<Binary>> BinaryOrErr = object::createBinary(*MB);
    if (!BinaryOrErr)
      return BinaryOrErr.takeError();
    Binaries.emplace_back(std::move(*BinaryOrErr), std::move(MB));
    Slices.emplace_back(*cast<MachOObjectFile>(Binaries.back().getBinary()),
                        O.getAlign());
  }

  if (Error Err = writeUniversalBinaryToStream(Slices, Out))
    return Err;

  return Error::success();
}
