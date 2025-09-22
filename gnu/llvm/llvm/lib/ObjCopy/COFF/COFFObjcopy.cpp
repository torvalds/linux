//===- COFFObjcopy.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjCopy/COFF/COFFObjcopy.h"
#include "COFFObject.h"
#include "COFFReader.h"
#include "COFFWriter.h"
#include "llvm/ObjCopy/COFF/COFFConfig.h"
#include "llvm/ObjCopy/CommonConfig.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/CRC.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Path.h"
#include <cassert>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;
using namespace COFF;

static bool isDebugSection(const Section &Sec) {
  return Sec.Name.starts_with(".debug");
}

static uint64_t getNextRVA(const Object &Obj) {
  if (Obj.getSections().empty())
    return 0;
  const Section &Last = Obj.getSections().back();
  return alignTo(Last.Header.VirtualAddress + Last.Header.VirtualSize,
                 Obj.IsPE ? Obj.PeHeader.SectionAlignment : 1);
}

static Expected<std::vector<uint8_t>>
createGnuDebugLinkSectionContents(StringRef File) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> LinkTargetOrErr =
      MemoryBuffer::getFile(File);
  if (!LinkTargetOrErr)
    return createFileError(File, LinkTargetOrErr.getError());
  auto LinkTarget = std::move(*LinkTargetOrErr);
  uint32_t CRC32 = llvm::crc32(arrayRefFromStringRef(LinkTarget->getBuffer()));

  StringRef FileName = sys::path::filename(File);
  size_t CRCPos = alignTo(FileName.size() + 1, 4);
  std::vector<uint8_t> Data(CRCPos + 4);
  memcpy(Data.data(), FileName.data(), FileName.size());
  support::endian::write32le(Data.data() + CRCPos, CRC32);
  return Data;
}

// Adds named section with given contents to the object.
static void addSection(Object &Obj, StringRef Name, ArrayRef<uint8_t> Contents,
                       uint32_t Characteristics) {
  bool NeedVA = Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ |
                                   IMAGE_SCN_MEM_WRITE);

  Section Sec;
  Sec.setOwnedContents(Contents);
  Sec.Name = Name;
  Sec.Header.VirtualSize = NeedVA ? Sec.getContents().size() : 0u;
  Sec.Header.VirtualAddress = NeedVA ? getNextRVA(Obj) : 0u;
  Sec.Header.SizeOfRawData =
      NeedVA ? alignTo(Sec.Header.VirtualSize,
                       Obj.IsPE ? Obj.PeHeader.FileAlignment : 1)
             : Sec.getContents().size();
  // Sec.Header.PointerToRawData is filled in by the writer.
  Sec.Header.PointerToRelocations = 0;
  Sec.Header.PointerToLinenumbers = 0;
  // Sec.Header.NumberOfRelocations is filled in by the writer.
  Sec.Header.NumberOfLinenumbers = 0;
  Sec.Header.Characteristics = Characteristics;

  Obj.addSections(Sec);
}

static Error addGnuDebugLink(Object &Obj, StringRef DebugLinkFile) {
  Expected<std::vector<uint8_t>> Contents =
      createGnuDebugLinkSectionContents(DebugLinkFile);
  if (!Contents)
    return Contents.takeError();

  addSection(Obj, ".gnu_debuglink", *Contents,
             IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
                 IMAGE_SCN_MEM_DISCARDABLE);

  return Error::success();
}

static uint32_t flagsToCharacteristics(SectionFlag AllFlags, uint32_t OldChar) {
  // Need to preserve alignment flags.
  const uint32_t PreserveMask =
      IMAGE_SCN_ALIGN_1BYTES | IMAGE_SCN_ALIGN_2BYTES | IMAGE_SCN_ALIGN_4BYTES |
      IMAGE_SCN_ALIGN_8BYTES | IMAGE_SCN_ALIGN_16BYTES |
      IMAGE_SCN_ALIGN_32BYTES | IMAGE_SCN_ALIGN_64BYTES |
      IMAGE_SCN_ALIGN_128BYTES | IMAGE_SCN_ALIGN_256BYTES |
      IMAGE_SCN_ALIGN_512BYTES | IMAGE_SCN_ALIGN_1024BYTES |
      IMAGE_SCN_ALIGN_2048BYTES | IMAGE_SCN_ALIGN_4096BYTES |
      IMAGE_SCN_ALIGN_8192BYTES;

  // Setup new section characteristics based on the flags provided in command
  // line.
  uint32_t NewCharacteristics = (OldChar & PreserveMask) | IMAGE_SCN_MEM_READ;

  if ((AllFlags & SectionFlag::SecAlloc) && !(AllFlags & SectionFlag::SecLoad))
    NewCharacteristics |= IMAGE_SCN_CNT_UNINITIALIZED_DATA;
  if (AllFlags & SectionFlag::SecNoload)
    NewCharacteristics |= IMAGE_SCN_LNK_REMOVE;
  if (!(AllFlags & SectionFlag::SecReadonly))
    NewCharacteristics |= IMAGE_SCN_MEM_WRITE;
  if (AllFlags & SectionFlag::SecDebug)
    NewCharacteristics |=
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_DISCARDABLE;
  if (AllFlags & SectionFlag::SecCode)
    NewCharacteristics |= IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE;
  if (AllFlags & SectionFlag::SecData)
    NewCharacteristics |= IMAGE_SCN_CNT_INITIALIZED_DATA;
  if (AllFlags & SectionFlag::SecShare)
    NewCharacteristics |= IMAGE_SCN_MEM_SHARED;
  if (AllFlags & SectionFlag::SecExclude)
    NewCharacteristics |= IMAGE_SCN_LNK_REMOVE;

  return NewCharacteristics;
}

static Error dumpSection(Object &O, StringRef SectionName, StringRef FileName) {
  for (const coff::Section &Section : O.getSections()) {
    if (Section.Name != SectionName)
      continue;

    ArrayRef<uint8_t> Contents = Section.getContents();

    std::unique_ptr<FileOutputBuffer> Buffer;
    if (auto B = FileOutputBuffer::create(FileName, Contents.size()))
      Buffer = std::move(*B);
    else
      return B.takeError();

    llvm::copy(Contents, Buffer->getBufferStart());
    if (Error E = Buffer->commit())
      return E;

    return Error::success();
  }
  return createStringError(object_error::parse_failed, "section '%s' not found",
                           SectionName.str().c_str());
}

static Error handleArgs(const CommonConfig &Config,
                        const COFFConfig &COFFConfig, Object &Obj) {
  for (StringRef Op : Config.DumpSection) {
    auto [Section, File] = Op.split('=');
    if (Error E = dumpSection(Obj, Section, File))
      return E;
  }

  // Perform the actual section removals.
  Obj.removeSections([&Config](const Section &Sec) {
    // Contrary to --only-keep-debug, --only-section fully removes sections that
    // aren't mentioned.
    if (!Config.OnlySection.empty() && !Config.OnlySection.matches(Sec.Name))
      return true;

    if (Config.StripDebug || Config.StripAll || Config.StripAllGNU ||
        Config.DiscardMode == DiscardType::All || Config.StripUnneeded) {
      if (isDebugSection(Sec) &&
          (Sec.Header.Characteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0)
        return true;
    }

    if (Config.ToRemove.matches(Sec.Name))
      return true;

    return false;
  });

  if (Config.OnlyKeepDebug) {
    // For --only-keep-debug, we keep all other sections, but remove their
    // content. The VirtualSize field in the section header is kept intact.
    Obj.truncateSections([](const Section &Sec) {
      return !isDebugSection(Sec) && Sec.Name != ".buildid" &&
             ((Sec.Header.Characteristics &
               (IMAGE_SCN_CNT_CODE | IMAGE_SCN_CNT_INITIALIZED_DATA)) != 0);
    });
  }

  // StripAll removes all symbols and thus also removes all relocations.
  if (Config.StripAll || Config.StripAllGNU)
    for (Section &Sec : Obj.getMutableSections())
      Sec.Relocs.clear();

  // If we need to do per-symbol removals, initialize the Referenced field.
  if (Config.StripUnneeded || Config.DiscardMode == DiscardType::All ||
      !Config.SymbolsToRemove.empty())
    if (Error E = Obj.markSymbols())
      return E;

  for (Symbol &Sym : Obj.getMutableSymbols()) {
    auto I = Config.SymbolsToRename.find(Sym.Name);
    if (I != Config.SymbolsToRename.end())
      Sym.Name = I->getValue();
  }

  auto ToRemove = [&](const Symbol &Sym) -> Expected<bool> {
    // For StripAll, all relocations have been stripped and we remove all
    // symbols.
    if (Config.StripAll || Config.StripAllGNU)
      return true;

    if (Config.SymbolsToRemove.matches(Sym.Name)) {
      // Explicitly removing a referenced symbol is an error.
      if (Sym.Referenced)
        return createStringError(
            llvm::errc::invalid_argument,
            "'" + Config.OutputFilename + "': not stripping symbol '" +
                Sym.Name.str() + "' because it is named in a relocation");
      return true;
    }

    if (!Sym.Referenced) {
      // With --strip-unneeded, GNU objcopy removes all unreferenced local
      // symbols, and any unreferenced undefined external.
      // With --strip-unneeded-symbol we strip only specific unreferenced
      // local symbol instead of removing all of such.
      if (Sym.Sym.StorageClass == IMAGE_SYM_CLASS_STATIC ||
          Sym.Sym.SectionNumber == 0)
        if (Config.StripUnneeded ||
            Config.UnneededSymbolsToRemove.matches(Sym.Name))
          return true;

      // GNU objcopy keeps referenced local symbols and external symbols
      // if --discard-all is set, similar to what --strip-unneeded does,
      // but undefined local symbols are kept when --discard-all is set.
      if (Config.DiscardMode == DiscardType::All &&
          Sym.Sym.StorageClass == IMAGE_SYM_CLASS_STATIC &&
          Sym.Sym.SectionNumber != 0)
        return true;
    }

    return false;
  };

  // Actually do removals of symbols.
  if (Error Err = Obj.removeSymbols(ToRemove))
    return Err;

  if (!Config.SetSectionFlags.empty())
    for (Section &Sec : Obj.getMutableSections()) {
      const auto It = Config.SetSectionFlags.find(Sec.Name);
      if (It != Config.SetSectionFlags.end())
        Sec.Header.Characteristics = flagsToCharacteristics(
            It->second.NewFlags, Sec.Header.Characteristics);
    }

  for (const NewSectionInfo &NewSection : Config.AddSection) {
    uint32_t Characteristics;
    const auto It = Config.SetSectionFlags.find(NewSection.SectionName);
    if (It != Config.SetSectionFlags.end())
      Characteristics = flagsToCharacteristics(It->second.NewFlags, 0);
    else
      Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_ALIGN_1BYTES;

    addSection(Obj, NewSection.SectionName,
               ArrayRef(reinterpret_cast<const uint8_t *>(
                            NewSection.SectionData->getBufferStart()),
                        NewSection.SectionData->getBufferSize()),
               Characteristics);
  }

  for (const NewSectionInfo &NewSection : Config.UpdateSection) {
    auto It = llvm::find_if(Obj.getMutableSections(), [&](auto &Sec) {
      return Sec.Name == NewSection.SectionName;
    });
    if (It == Obj.getMutableSections().end())
      return createStringError(errc::invalid_argument,
                               "could not find section with name '%s'",
                               NewSection.SectionName.str().c_str());
    size_t ContentSize = It->getContents().size();
    if (!ContentSize)
      return createStringError(
          errc::invalid_argument,
          "section '%s' cannot be updated because it does not have contents",
          NewSection.SectionName.str().c_str());
    if (ContentSize < NewSection.SectionData->getBufferSize())
      return createStringError(
          errc::invalid_argument,
          "new section cannot be larger than previous section");
    It->setOwnedContents({NewSection.SectionData->getBufferStart(),
                          NewSection.SectionData->getBufferEnd()});
  }

  if (!Config.AddGnuDebugLink.empty())
    if (Error E = addGnuDebugLink(Obj, Config.AddGnuDebugLink))
      return E;

  if (COFFConfig.Subsystem || COFFConfig.MajorSubsystemVersion ||
      COFFConfig.MinorSubsystemVersion) {
    if (!Obj.IsPE)
      return createStringError(
          errc::invalid_argument,
          "'" + Config.OutputFilename +
              "': unable to set subsystem on a relocatable object file");
    if (COFFConfig.Subsystem)
      Obj.PeHeader.Subsystem = *COFFConfig.Subsystem;
    if (COFFConfig.MajorSubsystemVersion)
      Obj.PeHeader.MajorSubsystemVersion = *COFFConfig.MajorSubsystemVersion;
    if (COFFConfig.MinorSubsystemVersion)
      Obj.PeHeader.MinorSubsystemVersion = *COFFConfig.MinorSubsystemVersion;
  }

  return Error::success();
}

Error executeObjcopyOnBinary(const CommonConfig &Config,
                             const COFFConfig &COFFConfig, COFFObjectFile &In,
                             raw_ostream &Out) {
  COFFReader Reader(In);
  Expected<std::unique_ptr<Object>> ObjOrErr = Reader.create();
  if (!ObjOrErr)
    return createFileError(Config.InputFilename, ObjOrErr.takeError());
  Object *Obj = ObjOrErr->get();
  assert(Obj && "Unable to deserialize COFF object");
  if (Error E = handleArgs(Config, COFFConfig, *Obj))
    return createFileError(Config.InputFilename, std::move(E));
  COFFWriter Writer(*Obj, Out);
  if (Error E = Writer.write())
    return createFileError(Config.OutputFilename, std::move(E));
  return Error::success();
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
