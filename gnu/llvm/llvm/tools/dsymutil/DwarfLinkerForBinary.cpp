//===- tools/dsymutil/DwarfLinkerForBinary.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DwarfLinkerForBinary.h"
#include "BinaryHolder.h"
#include "DebugMap.h"
#include "MachOUtils.h"
#include "dsymutil.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/BinaryFormat/Swift.h"
#include "llvm/CodeGen/AccelTable.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/NonRelocatableStringpool.h"
#include "llvm/Config/config.h"
#include "llvm/DWARFLinker/Classic/DWARFLinker.h"
#include "llvm/DWARFLinker/Classic/DWARFStreamer.h"
#include "llvm/DWARFLinker/Parallel/DWARFLinker.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/DebugInfo/DWARF/DWARFAbbreviationDeclaration.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugLine.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugRangeList.h"
#include "llvm/DebugInfo/DWARF/DWARFDie.h"
#include "llvm/DebugInfo/DWARF/DWARFExpression.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace llvm {

static mc::RegisterMCTargetOptionsFlags MOF;

using namespace dwarf_linker;

namespace dsymutil {

static void dumpDIE(const DWARFDie *DIE, bool Verbose) {
  if (!DIE || !Verbose)
    return;

  DIDumpOptions DumpOpts;
  DumpOpts.ChildRecurseDepth = 0;
  DumpOpts.Verbose = Verbose;

  WithColor::note() << "    in DIE:\n";
  DIE->dump(errs(), 6 /* Indent */, DumpOpts);
}

/// Report a warning to the user, optionally including information about a
/// specific \p DIE related to the warning.
void DwarfLinkerForBinary::reportWarning(Twine Warning, Twine Context,
                                         const DWARFDie *DIE) const {
  // FIXME: implement warning logging which does not block other threads.
  if (ErrorHandlerMutex.try_lock()) {
    warn(Warning, Context);
    dumpDIE(DIE, Options.Verbose);
    ErrorHandlerMutex.unlock();
  }
}

void DwarfLinkerForBinary::reportError(Twine Error, Twine Context,
                                       const DWARFDie *DIE) const {
  // FIXME: implement error logging which does not block other threads.
  if (ErrorHandlerMutex.try_lock()) {
    error(Error, Context);
    dumpDIE(DIE, Options.Verbose);
    ErrorHandlerMutex.unlock();
  }
}

ErrorOr<const object::ObjectFile &>
DwarfLinkerForBinary::loadObject(const DebugMapObject &Obj,
                                 const Triple &Triple) {
  auto ObjectEntry =
      BinHolder.getObjectEntry(Obj.getObjectFilename(), Obj.getTimestamp());
  if (!ObjectEntry) {
    auto Err = ObjectEntry.takeError();
    reportWarning(Twine(Obj.getObjectFilename()) + ": " +
                      toStringWithoutConsuming(Err),
                  Obj.getObjectFilename());
    return errorToErrorCode(std::move(Err));
  }

  auto Object = ObjectEntry->getObject(Triple);
  if (!Object) {
    auto Err = Object.takeError();
    reportWarning(Twine(Obj.getObjectFilename()) + ": " +
                      toStringWithoutConsuming(Err),
                  Obj.getObjectFilename());
    return errorToErrorCode(std::move(Err));
  }

  return *Object;
}

static Error remarksErrorHandler(const DebugMapObject &DMO,
                                 DwarfLinkerForBinary &Linker,
                                 std::unique_ptr<FileError> FE) {
  bool IsArchive = DMO.getObjectFilename().ends_with(")");
  // Don't report errors for missing remark files from static
  // archives.
  if (!IsArchive)
    return Error(std::move(FE));

  std::string Message = FE->message();
  Error E = FE->takeError();
  Error NewE = handleErrors(std::move(E), [&](std::unique_ptr<ECError> EC) {
    if (EC->convertToErrorCode() != std::errc::no_such_file_or_directory)
      return Error(std::move(EC));

    Linker.reportWarning(Message, DMO.getObjectFilename());
    return Error(Error::success());
  });

  if (!NewE)
    return Error::success();

  return createFileError(FE->getFileName(), std::move(NewE));
}
Error DwarfLinkerForBinary::emitRelocations(
    const DebugMap &DM, std::vector<ObjectWithRelocMap> &ObjectsForLinking) {
  // Return early if the "Resources" directory is not being written to.
  if (!Options.ResourceDir)
    return Error::success();

  RelocationMap RM(DM.getTriple(), DM.getBinaryPath());
  for (auto &Obj : ObjectsForLinking) {
    if (!Obj.OutRelocs->isInitialized())
      continue;
    Obj.OutRelocs->addValidRelocs(RM);
  }

  SmallString<128> InputPath;
  SmallString<128> Path;
  // Create the "Relocations" directory in the "Resources" directory, and
  // create an architecture-specific directory in the "Relocations" directory.
  StringRef ArchName = Triple::getArchName(RM.getTriple().getArch(),
                                           RM.getTriple().getSubArch());
  sys::path::append(Path, *Options.ResourceDir, "Relocations", ArchName);
  if (std::error_code EC = sys::fs::create_directories(Path.str(), true,
                                                       sys::fs::perms::all_all))
    return errorCodeToError(EC);

  // Append the file name.
  sys::path::append(Path, sys::path::filename(DM.getBinaryPath()));
  Path.append(".yml");

  std::error_code EC;
  raw_fd_ostream OS(Path.str(), EC, sys::fs::OF_Text);
  if (EC)
    return errorCodeToError(EC);

  RM.print(OS);
  return Error::success();
}

static Error emitRemarks(const LinkOptions &Options, StringRef BinaryPath,
                         StringRef ArchName, const remarks::RemarkLinker &RL) {
  // Make sure we don't create the directories and the file if there is nothing
  // to serialize.
  if (RL.empty())
    return Error::success();

  SmallString<128> InputPath;
  SmallString<128> Path;
  // Create the "Remarks" directory in the "Resources" directory.
  sys::path::append(Path, *Options.ResourceDir, "Remarks");
  if (std::error_code EC = sys::fs::create_directories(Path.str(), true,
                                                       sys::fs::perms::all_all))
    return errorCodeToError(EC);

  // Append the file name.
  // For fat binaries, also append a dash and the architecture name.
  sys::path::append(Path, sys::path::filename(BinaryPath));
  if (Options.NumDebugMaps > 1) {
    // More than one debug map means we have a fat binary.
    Path += '-';
    Path += ArchName;
  }

  std::error_code EC;
  raw_fd_ostream OS(Options.NoOutput ? "-" : Path.str(), EC,
                    Options.RemarksFormat == remarks::Format::Bitstream
                        ? sys::fs::OF_None
                        : sys::fs::OF_Text);
  if (EC)
    return errorCodeToError(EC);

  if (Error E = RL.serialize(OS, Options.RemarksFormat))
    return E;

  return Error::success();
}

ErrorOr<std::unique_ptr<DWARFFile>> DwarfLinkerForBinary::loadObject(
    const DebugMapObject &Obj, const DebugMap &DebugMap,
    remarks::RemarkLinker &RL,
    std::shared_ptr<DwarfLinkerForBinaryRelocationMap> DLBRM) {
  auto ErrorOrObj = loadObject(Obj, DebugMap.getTriple());
  std::unique_ptr<DWARFFile> Res;

  if (ErrorOrObj) {
    auto Context = DWARFContext::create(
        *ErrorOrObj, DWARFContext::ProcessDebugRelocations::Process, nullptr,
        "",
        [&](Error Err) {
          handleAllErrors(std::move(Err), [&](ErrorInfoBase &Info) {
            reportError(Info.message());
          });
        },
        [&](Error Warning) {
          handleAllErrors(std::move(Warning), [&](ErrorInfoBase &Info) {
            reportWarning(Info.message());
          });
        });
    DLBRM->init(*Context);
    Res = std::make_unique<DWARFFile>(
        Obj.getObjectFilename(), std::move(Context),
        std::make_unique<AddressManager>(*this, *ErrorOrObj, Obj, DLBRM),
        [&](StringRef FileName) { BinHolder.eraseObjectEntry(FileName); });

    Error E = RL.link(*ErrorOrObj);
    if (Error NewE = handleErrors(
            std::move(E), [&](std::unique_ptr<FileError> EC) -> Error {
              return remarksErrorHandler(Obj, *this, std::move(EC));
            }))
      return errorToErrorCode(std::move(NewE));

    return std::move(Res);
  }

  return ErrorOrObj.getError();
}

static bool binaryHasStrippableSwiftReflectionSections(
    const DebugMap &Map, const LinkOptions &Options, BinaryHolder &BinHolder) {
  // If the input binary has strippable swift5 reflection sections, there is no
  // need to copy them to the .dSYM. Only copy them for binaries where the
  // linker omitted the reflection metadata.
  if (!Map.getBinaryPath().empty() &&
      Options.FileType == DWARFLinkerBase::OutputFileType::Object) {

    auto ObjectEntry = BinHolder.getObjectEntry(Map.getBinaryPath());
    // If ObjectEntry or Object has an error, no binary exists, therefore no
    // reflection sections exist.
    if (!ObjectEntry) {
      // Any errors will be diagnosed later in the main loop, ignore them here.
      llvm::consumeError(ObjectEntry.takeError());
      return false;
    }

    auto Object =
        ObjectEntry->getObjectAs<object::MachOObjectFile>(Map.getTriple());
    if (!Object) {
      // Any errors will be diagnosed later in the main loop, ignore them here.
      llvm::consumeError(Object.takeError());
      return false;
    }

    for (auto &Section : Object->sections()) {
      llvm::Expected<llvm::StringRef> NameOrErr =
          Object->getSectionName(Section.getRawDataRefImpl());
      if (!NameOrErr) {
        llvm::consumeError(NameOrErr.takeError());
        continue;
      }
      NameOrErr->consume_back("__TEXT");
      auto ReflectionSectionKind =
          Object->mapReflectionSectionNameToEnumValue(*NameOrErr);
      if (Object->isReflectionSectionStrippable(ReflectionSectionKind)) {
        return true;
      }
    }
  }
  return false;
}

/// Calculate the start of the strippable swift reflection sections in Dwarf.
/// Note that there's an assumption that the reflection sections will appear
/// in alphabetic order.
static std::vector<uint64_t>
calculateStartOfStrippableReflectionSections(const DebugMap &Map) {
  using llvm::binaryformat::Swift5ReflectionSectionKind;
  uint64_t AssocTySize = 0;
  uint64_t FieldMdSize = 0;
  for (const auto &Obj : Map.objects()) {
    auto OF =
        llvm::object::ObjectFile::createObjectFile(Obj->getObjectFilename());
    if (!OF) {
      llvm::consumeError(OF.takeError());
      continue;
    }
    if (auto *MO = dyn_cast<llvm::object::MachOObjectFile>(OF->getBinary())) {
      for (auto &Section : MO->sections()) {
        llvm::Expected<llvm::StringRef> NameOrErr =
            MO->getSectionName(Section.getRawDataRefImpl());
        if (!NameOrErr) {
          llvm::consumeError(NameOrErr.takeError());
          continue;
        }
        NameOrErr->consume_back("__TEXT");
        auto ReflSectionKind =
            MO->mapReflectionSectionNameToEnumValue(*NameOrErr);
        switch (ReflSectionKind) {
        case Swift5ReflectionSectionKind::assocty:
          AssocTySize += Section.getSize();
          break;
        case Swift5ReflectionSectionKind::fieldmd:
          FieldMdSize += Section.getSize();
          break;
        default:
          break;
        }
      }
    }
  }
  // Initialize the vector with enough space to fit every reflection section
  // kind.
  std::vector<uint64_t> SectionToOffset(Swift5ReflectionSectionKind::last, 0);
  SectionToOffset[Swift5ReflectionSectionKind::assocty] = 0;
  SectionToOffset[Swift5ReflectionSectionKind::fieldmd] =
      llvm::alignTo(AssocTySize, 4);
  SectionToOffset[Swift5ReflectionSectionKind::reflstr] = llvm::alignTo(
      SectionToOffset[Swift5ReflectionSectionKind::fieldmd] + FieldMdSize, 4);

  return SectionToOffset;
}

void DwarfLinkerForBinary::collectRelocationsToApplyToSwiftReflectionSections(
    const object::SectionRef &Section, StringRef &Contents,
    const llvm::object::MachOObjectFile *MO,
    const std::vector<uint64_t> &SectionToOffsetInDwarf,
    const llvm::dsymutil::DebugMapObject *Obj,
    std::vector<MachOUtils::DwarfRelocationApplicationInfo> &RelocationsToApply)
    const {
  for (auto It = Section.relocation_begin(); It != Section.relocation_end();
       ++It) {
    object::DataRefImpl RelocDataRef = It->getRawDataRefImpl();
    MachO::any_relocation_info MachOReloc = MO->getRelocation(RelocDataRef);

    if (!object::MachOObjectFile::isMachOPairedReloc(
            MO->getAnyRelocationType(MachOReloc), MO->getArch())) {
      reportWarning(
          "Unimplemented relocation type in strippable reflection section ",
          Obj->getObjectFilename());
      continue;
    }

    auto CalculateAddressOfSymbolInDwarfSegment =
        [&]() -> std::optional<int64_t> {
      auto Symbol = It->getSymbol();
      auto SymbolAbsoluteAddress = Symbol->getAddress();
      if (!SymbolAbsoluteAddress)
        return {};
      auto Section = Symbol->getSection();
      if (!Section) {
        llvm::consumeError(Section.takeError());
        return {};
      }

      if ((*Section)->getObject()->section_end() == *Section)
        return {};

      auto SectionStart = (*Section)->getAddress();
      auto SymbolAddressInSection = *SymbolAbsoluteAddress - SectionStart;
      auto SectionName = (*Section)->getName();
      if (!SectionName)
        return {};
      auto ReflSectionKind =
          MO->mapReflectionSectionNameToEnumValue(*SectionName);

      int64_t SectionStartInLinkedBinary =
          SectionToOffsetInDwarf[ReflSectionKind];

      auto Addr = SectionStartInLinkedBinary + SymbolAddressInSection;
      return Addr;
    };

    // The first symbol should always be in the section we're currently
    // iterating over.
    auto FirstSymbolAddress = CalculateAddressOfSymbolInDwarfSegment();
    ++It;

    bool ShouldSubtractDwarfVM = false;
    // For the second symbol there are two possibilities.
    std::optional<int64_t> SecondSymbolAddress;
    auto Sym = It->getSymbol();
    if (Sym != MO->symbol_end()) {
      Expected<StringRef> SymbolName = Sym->getName();
      if (SymbolName) {
        if (const auto *Mapping = Obj->lookupSymbol(*SymbolName)) {
          // First possibility: the symbol exists in the binary, and exists in a
          // non-strippable section (for example, typeref, or __TEXT,__const),
          // in which case we look up its address in the  binary, which dsymutil
          // will copy verbatim.
          SecondSymbolAddress = Mapping->getValue().BinaryAddress;
          // Since the symbols live in different segments, we have to substract
          // the start of the Dwarf's vmaddr so the value calculated points to
          // the correct place.
          ShouldSubtractDwarfVM = true;
        }
      }
    }

    if (!SecondSymbolAddress) {
      // Second possibility, this symbol is not present in the main binary, and
      // must be in one of the strippable sections (for example, reflstr).
      // Calculate its address in the same way as we did the first one.
      SecondSymbolAddress = CalculateAddressOfSymbolInDwarfSegment();
    }

    if (!FirstSymbolAddress || !SecondSymbolAddress)
      continue;

    auto SectionName = Section.getName();
    if (!SectionName)
      continue;

    int32_t Addend;
    memcpy(&Addend, Contents.data() + It->getOffset(), sizeof(int32_t));
    int32_t Value = (*SecondSymbolAddress + Addend) - *FirstSymbolAddress;
    auto ReflSectionKind =
        MO->mapReflectionSectionNameToEnumValue(*SectionName);
    uint64_t AddressFromDwarfVM =
        SectionToOffsetInDwarf[ReflSectionKind] + It->getOffset();
    RelocationsToApply.emplace_back(AddressFromDwarfVM, Value,
                                    ShouldSubtractDwarfVM);
  }
}

Error DwarfLinkerForBinary::copySwiftInterfaces(StringRef Architecture) const {
  std::error_code EC;
  SmallString<128> InputPath;
  SmallString<128> Path;
  sys::path::append(Path, *Options.ResourceDir, "Swift", Architecture);
  if ((EC = sys::fs::create_directories(Path.str(), true,
                                        sys::fs::perms::all_all)))
    return make_error<StringError>(
        "cannot create directory: " + toString(errorCodeToError(EC)), EC);
  unsigned BaseLength = Path.size();

  for (auto &I : ParseableSwiftInterfaces) {
    StringRef ModuleName = I.first;
    StringRef InterfaceFile = I.second;
    if (!Options.PrependPath.empty()) {
      InputPath.clear();
      sys::path::append(InputPath, Options.PrependPath, InterfaceFile);
      InterfaceFile = InputPath;
    }
    sys::path::append(Path, ModuleName);
    Path.append(".swiftinterface");
    if (Options.Verbose)
      outs() << "copy parseable Swift interface " << InterfaceFile << " -> "
             << Path.str() << '\n';

    // copy_file attempts an APFS clone first, so this should be cheap.
    if ((EC = sys::fs::copy_file(InterfaceFile, Path.str())))
      reportWarning(Twine("cannot copy parseable Swift interface ") +
                    InterfaceFile + ": " + toString(errorCodeToError(EC)));
    Path.resize(BaseLength);
  }
  return Error::success();
}

void DwarfLinkerForBinary::copySwiftReflectionMetadata(
    const llvm::dsymutil::DebugMapObject *Obj, classic::DwarfStreamer *Streamer,
    std::vector<uint64_t> &SectionToOffsetInDwarf,
    std::vector<MachOUtils::DwarfRelocationApplicationInfo>
        &RelocationsToApply) {
  using binaryformat::Swift5ReflectionSectionKind;
  auto OF =
      llvm::object::ObjectFile::createObjectFile(Obj->getObjectFilename());
  if (!OF) {
    llvm::consumeError(OF.takeError());
    return;
  }
  if (auto *MO = dyn_cast<llvm::object::MachOObjectFile>(OF->getBinary())) {
    // Collect the swift reflection sections before emitting them. This is
    // done so we control the order they're emitted.
    std::array<std::optional<object::SectionRef>,
               Swift5ReflectionSectionKind::last + 1>
        SwiftSections;
    for (auto &Section : MO->sections()) {
      llvm::Expected<llvm::StringRef> NameOrErr =
          MO->getSectionName(Section.getRawDataRefImpl());
      if (!NameOrErr) {
        llvm::consumeError(NameOrErr.takeError());
        continue;
      }
      NameOrErr->consume_back("__TEXT");
      auto ReflSectionKind =
          MO->mapReflectionSectionNameToEnumValue(*NameOrErr);
      if (MO->isReflectionSectionStrippable(ReflSectionKind))
        SwiftSections[ReflSectionKind] = Section;
    }
    // Make sure we copy the sections in alphabetic order.
    auto SectionKindsToEmit = {Swift5ReflectionSectionKind::assocty,
                               Swift5ReflectionSectionKind::fieldmd,
                               Swift5ReflectionSectionKind::reflstr};
    for (auto SectionKind : SectionKindsToEmit) {
      if (!SwiftSections[SectionKind])
        continue;
      auto &Section = *SwiftSections[SectionKind];
      llvm::Expected<llvm::StringRef> SectionContents = Section.getContents();
      if (!SectionContents)
        continue;
      const auto *MO =
          llvm::cast<llvm::object::MachOObjectFile>(Section.getObject());
      collectRelocationsToApplyToSwiftReflectionSections(
          Section, *SectionContents, MO, SectionToOffsetInDwarf, Obj,
          RelocationsToApply);
      // Update the section start with the current section's contribution, so
      // the next section we copy from a different .o file points to the correct
      // place.
      SectionToOffsetInDwarf[SectionKind] += Section.getSize();
      Streamer->emitSwiftReflectionSection(SectionKind, *SectionContents,
                                           Section.getAlignment().value(),
                                           Section.getSize());
    }
  }
}

bool DwarfLinkerForBinary::link(const DebugMap &Map) {
  if (Options.DWARFLinkerType == DsymutilDWARFLinkerType::Parallel)
    return linkImpl<parallel::DWARFLinker>(Map, Options.FileType);

  return linkImpl<classic::DWARFLinker>(Map, Options.FileType);
}

template <typename Linker>
void setAcceleratorTables(Linker &GeneralLinker,
                          DsymutilAccelTableKind TableKind,
                          uint16_t MaxDWARFVersion) {
  switch (TableKind) {
  case DsymutilAccelTableKind::Apple:
    GeneralLinker.addAccelTableKind(Linker::AccelTableKind::Apple);
    return;
  case DsymutilAccelTableKind::Dwarf:
    GeneralLinker.addAccelTableKind(Linker::AccelTableKind::DebugNames);
    return;
  case DsymutilAccelTableKind::Pub:
    GeneralLinker.addAccelTableKind(Linker::AccelTableKind::Pub);
    return;
  case DsymutilAccelTableKind::Default:
    if (MaxDWARFVersion >= 5)
      GeneralLinker.addAccelTableKind(Linker::AccelTableKind::DebugNames);
    else
      GeneralLinker.addAccelTableKind(Linker::AccelTableKind::Apple);
    return;
  case DsymutilAccelTableKind::None:
    // Nothing to do.
    return;
  }

  llvm_unreachable("All cases handled above!");
}

template <typename Linker>
bool DwarfLinkerForBinary::linkImpl(
    const DebugMap &Map, typename Linker::OutputFileType ObjectType) {

  std::vector<ObjectWithRelocMap> ObjectsForLinking;

  DebugMap DebugMap(Map.getTriple(), Map.getBinaryPath());

  std::unique_ptr<Linker> GeneralLinker = Linker::createLinker(
      [&](const Twine &Error, StringRef Context, const DWARFDie *DIE) {
        reportError(Error, Context, DIE);
      },
      [&](const Twine &Warning, StringRef Context, const DWARFDie *DIE) {
        reportWarning(Warning, Context, DIE);
      });

  std::unique_ptr<classic::DwarfStreamer> Streamer;
  if (!Options.NoOutput) {
    if (Expected<std::unique_ptr<classic::DwarfStreamer>> StreamerOrErr =
            classic::DwarfStreamer::createStreamer(
                Map.getTriple(), ObjectType, OutFile,
                [&](const Twine &Warning, StringRef Context,
                    const DWARFDie *DIE) {
                  reportWarning(Warning, Context, DIE);
                }))
      Streamer = std::move(*StreamerOrErr);
    else {
      handleAllErrors(StreamerOrErr.takeError(), [&](const ErrorInfoBase &EI) {
        reportError(EI.message(), "dwarf streamer init");
      });
      return false;
    }

    if constexpr (std::is_same<Linker, parallel::DWARFLinker>::value) {
      GeneralLinker->setOutputDWARFHandler(
          Map.getTriple(),
          [&](std::shared_ptr<parallel::SectionDescriptorBase> Section) {
            Streamer->emitSectionContents(Section->getContents(),
                                          Section->getKind());
          });
    } else
      GeneralLinker->setOutputDWARFEmitter(Streamer.get());
  }

  remarks::RemarkLinker RL;
  if (!Options.RemarksPrependPath.empty())
    RL.setExternalFilePrependPath(Options.RemarksPrependPath);
  RL.setKeepAllRemarks(Options.RemarksKeepAll);
  GeneralLinker->setObjectPrefixMap(&Options.ObjectPrefixMap);

  GeneralLinker->setVerbosity(Options.Verbose);
  GeneralLinker->setStatistics(Options.Statistics);
  GeneralLinker->setVerifyInputDWARF(Options.VerifyInputDWARF);
  GeneralLinker->setNoODR(Options.NoODR);
  GeneralLinker->setUpdateIndexTablesOnly(Options.Update);
  GeneralLinker->setNumThreads(Options.Threads);
  GeneralLinker->setPrependPath(Options.PrependPath);
  GeneralLinker->setKeepFunctionForStatic(Options.KeepFunctionForStatic);
  GeneralLinker->setInputVerificationHandler(
      [&](const DWARFFile &File, llvm::StringRef Output) {
        std::lock_guard<std::mutex> Guard(ErrorHandlerMutex);
        if (Options.Verbose)
          errs() << Output;
        warn("input verification failed", File.FileName);
        HasVerificationErrors = true;
      });
  auto Loader = [&](StringRef ContainerName,
                    StringRef Path) -> ErrorOr<DWARFFile &> {
    auto &Obj = DebugMap.addDebugMapObject(
        Path, sys::TimePoint<std::chrono::seconds>(), MachO::N_OSO);

    auto DLBRelocMap = std::make_shared<DwarfLinkerForBinaryRelocationMap>();
    if (ErrorOr<std::unique_ptr<DWARFFile>> ErrorOrObj =
            loadObject(Obj, DebugMap, RL, DLBRelocMap)) {
      ObjectsForLinking.emplace_back(std::move(*ErrorOrObj), DLBRelocMap);
      return *ObjectsForLinking.back().Object;
    } else {
      // Try and emit more helpful warnings by applying some heuristics.
      StringRef ObjFile = ContainerName;
      bool IsClangModule = sys::path::extension(Path) == ".pcm";
      bool IsArchive = ObjFile.ends_with(")");

      if (IsClangModule) {
        StringRef ModuleCacheDir = sys::path::parent_path(Path);
        if (sys::fs::exists(ModuleCacheDir)) {
          // If the module's parent directory exists, we assume that the
          // module cache has expired and was pruned by clang.  A more
          // adventurous dsymutil would invoke clang to rebuild the module
          // now.
          if (!ModuleCacheHintDisplayed) {
            WithColor::note()
                << "The clang module cache may have expired since "
                   "this object file was built. Rebuilding the "
                   "object file will rebuild the module cache.\n";
            ModuleCacheHintDisplayed = true;
          }
        } else if (IsArchive) {
          // If the module cache directory doesn't exist at all and the
          // object file is inside a static library, we assume that the
          // static library was built on a different machine. We don't want
          // to discourage module debugging for convenience libraries within
          // a project though.
          if (!ArchiveHintDisplayed) {
            WithColor::note()
                << "Linking a static library that was built with "
                   "-gmodules, but the module cache was not found.  "
                   "Redistributable static libraries should never be "
                   "built with module debugging enabled.  The debug "
                   "experience will be degraded due to incomplete "
                   "debug information.\n";
            ArchiveHintDisplayed = true;
          }
        }
      }

      return ErrorOrObj.getError();
    }

    llvm_unreachable("Unhandled DebugMap object");
  };
  GeneralLinker->setSwiftInterfacesMap(&ParseableSwiftInterfaces);
  bool ReflectionSectionsPresentInBinary = false;
  // If there is no output specified, no point in checking the binary for swift5
  // reflection sections.
  if (!Options.NoOutput) {
    ReflectionSectionsPresentInBinary =
        binaryHasStrippableSwiftReflectionSections(Map, Options, BinHolder);
  }

  std::vector<MachOUtils::DwarfRelocationApplicationInfo> RelocationsToApply;
  if (!Options.NoOutput && !ReflectionSectionsPresentInBinary) {
    auto SectionToOffsetInDwarf =
        calculateStartOfStrippableReflectionSections(Map);
    for (const auto &Obj : Map.objects())
      copySwiftReflectionMetadata(Obj.get(), Streamer.get(),
                                  SectionToOffsetInDwarf, RelocationsToApply);
  }

  uint16_t MaxDWARFVersion = 0;
  std::function<void(const DWARFUnit &Unit)> OnCUDieLoaded =
      [&MaxDWARFVersion](const DWARFUnit &Unit) {
        MaxDWARFVersion = std::max(Unit.getVersion(), MaxDWARFVersion);
      };

  for (const auto &Obj : Map.objects()) {
    // N_AST objects (swiftmodule files) should get dumped directly into the
    // appropriate DWARF section.
    if (Obj->getType() == MachO::N_AST) {
      if (Options.Verbose)
        outs() << "DEBUG MAP OBJECT: " << Obj->getObjectFilename() << "\n";

      StringRef File = Obj->getObjectFilename();
      auto ErrorOrMem = MemoryBuffer::getFile(File);
      if (!ErrorOrMem) {
        reportWarning("Could not open '" + File + "'");
        continue;
      }
      sys::fs::file_status Stat;
      if (auto Err = sys::fs::status(File, Stat)) {
        reportWarning(Err.message());
        continue;
      }
      if (!Options.NoTimestamp) {
        // The modification can have sub-second precision so we need to cast
        // away the extra precision that's not present in the debug map.
        auto ModificationTime =
            std::chrono::time_point_cast<std::chrono::seconds>(
                Stat.getLastModificationTime());
        if (Obj->getTimestamp() != sys::TimePoint<>() &&
            ModificationTime != Obj->getTimestamp()) {
          // Not using the helper here as we can easily stream TimePoint<>.
          WithColor::warning()
              << File << ": timestamp mismatch between swift interface file ("
              << sys::TimePoint<>(ModificationTime) << ") and debug map ("
              << sys::TimePoint<>(Obj->getTimestamp()) << ")\n";
          continue;
        }
      }

      // Copy the module into the .swift_ast section.
      if (!Options.NoOutput)
        Streamer->emitSwiftAST((*ErrorOrMem)->getBuffer());

      continue;
    }

    auto DLBRelocMap = std::make_shared<DwarfLinkerForBinaryRelocationMap>();
    if (ErrorOr<std::unique_ptr<DWARFFile>> ErrorOrObj =
            loadObject(*Obj, Map, RL, DLBRelocMap)) {
      ObjectsForLinking.emplace_back(std::move(*ErrorOrObj), DLBRelocMap);
      GeneralLinker->addObjectFile(*ObjectsForLinking.back().Object, Loader,
                                   OnCUDieLoaded);
    } else {
      ObjectsForLinking.push_back(
          {std::make_unique<DWARFFile>(Obj->getObjectFilename(), nullptr,
                                       nullptr),
           DLBRelocMap});
      GeneralLinker->addObjectFile(*ObjectsForLinking.back().Object);
    }
  }

  // If we haven't seen any CUs, pick an arbitrary valid Dwarf version anyway.
  if (MaxDWARFVersion == 0)
    MaxDWARFVersion = 3;

  if (Error E = GeneralLinker->setTargetDWARFVersion(MaxDWARFVersion))
    return error(toString(std::move(E)));

  setAcceleratorTables<Linker>(*GeneralLinker, Options.TheAccelTableKind,
                               MaxDWARFVersion);

  // link debug info for loaded object files.
  if (Error E = GeneralLinker->link())
    return error(toString(std::move(E)));

  StringRef ArchName = Map.getTriple().getArchName();
  if (Error E = emitRemarks(Options, Map.getBinaryPath(), ArchName, RL))
    return error(toString(std::move(E)));

  if (Options.NoOutput)
    return true;

  if (Error E = emitRelocations(Map, ObjectsForLinking))
    return error(toString(std::move(E)));

  if (Options.ResourceDir && !ParseableSwiftInterfaces.empty()) {
    StringRef ArchName = Triple::getArchTypeName(Map.getTriple().getArch());
    if (auto E = copySwiftInterfaces(ArchName))
      return error(toString(std::move(E)));
  }

  auto MapTriple = Map.getTriple();
  if ((MapTriple.isOSDarwin() || MapTriple.isOSBinFormatMachO()) &&
      !Map.getBinaryPath().empty() &&
      ObjectType == Linker::OutputFileType::Object)
    return MachOUtils::generateDsymCompanion(
        Options.VFS, Map, *Streamer->getAsmPrinter().OutStreamer, OutFile,
        RelocationsToApply);

  Streamer->finish();
  return true;
}

/// Iterate over the relocations of the given \p Section and
/// store the ones that correspond to debug map entries into the
/// ValidRelocs array.
void DwarfLinkerForBinary::AddressManager::findValidRelocsMachO(
    const object::SectionRef &Section, const object::MachOObjectFile &Obj,
    const DebugMapObject &DMO, std::vector<ValidReloc> &ValidRelocs) {
  Expected<StringRef> ContentsOrErr = Section.getContents();
  if (!ContentsOrErr) {
    consumeError(ContentsOrErr.takeError());
    Linker.reportWarning("error reading section", DMO.getObjectFilename());
    return;
  }
  DataExtractor Data(*ContentsOrErr, Obj.isLittleEndian(), 0);
  bool SkipNext = false;

  for (const object::RelocationRef &Reloc : Section.relocations()) {
    if (SkipNext) {
      SkipNext = false;
      continue;
    }

    object::DataRefImpl RelocDataRef = Reloc.getRawDataRefImpl();
    MachO::any_relocation_info MachOReloc = Obj.getRelocation(RelocDataRef);

    if (object::MachOObjectFile::isMachOPairedReloc(Obj.getAnyRelocationType(MachOReloc),
                           Obj.getArch())) {
      SkipNext = true;
      Linker.reportWarning("unsupported relocation in " + *Section.getName() +
                               " section.",
                           DMO.getObjectFilename());
      continue;
    }

    unsigned RelocSize = 1 << Obj.getAnyRelocationLength(MachOReloc);
    uint64_t Offset64 = Reloc.getOffset();
    if ((RelocSize != 4 && RelocSize != 8)) {
      Linker.reportWarning("unsupported relocation in " + *Section.getName() +
                               " section.",
                           DMO.getObjectFilename());
      continue;
    }
    uint64_t OffsetCopy = Offset64;
    // Mach-o uses REL relocations, the addend is at the relocation offset.
    uint64_t Addend = Data.getUnsigned(&OffsetCopy, RelocSize);
    uint64_t SymAddress;
    int64_t SymOffset;

    if (Obj.isRelocationScattered(MachOReloc)) {
      // The address of the base symbol for scattered relocations is
      // stored in the reloc itself. The actual addend will store the
      // base address plus the offset.
      SymAddress = Obj.getScatteredRelocationValue(MachOReloc);
      SymOffset = int64_t(Addend) - SymAddress;
    } else {
      SymAddress = Addend;
      SymOffset = 0;
    }

    auto Sym = Reloc.getSymbol();
    if (Sym != Obj.symbol_end()) {
      Expected<StringRef> SymbolName = Sym->getName();
      if (!SymbolName) {
        consumeError(SymbolName.takeError());
        Linker.reportWarning("error getting relocation symbol name.",
                             DMO.getObjectFilename());
        continue;
      }
      if (const auto *Mapping = DMO.lookupSymbol(*SymbolName))
        ValidRelocs.emplace_back(Offset64, RelocSize, Addend, Mapping->getKey(),
                                 Mapping->getValue());
    } else if (const auto *Mapping = DMO.lookupObjectAddress(SymAddress)) {
      // Do not store the addend. The addend was the address of the symbol in
      // the object file, the address in the binary that is stored in the debug
      // map doesn't need to be offset.
      ValidRelocs.emplace_back(Offset64, RelocSize, SymOffset,
                               Mapping->getKey(), Mapping->getValue());
    }
  }
}

/// Dispatch the valid relocation finding logic to the
/// appropriate handler depending on the object file format.
bool DwarfLinkerForBinary::AddressManager::findValidRelocs(
    const object::SectionRef &Section, const object::ObjectFile &Obj,
    const DebugMapObject &DMO, std::vector<ValidReloc> &Relocs) {
  // Dispatch to the right handler depending on the file type.
  if (auto *MachOObj = dyn_cast<object::MachOObjectFile>(&Obj))
    findValidRelocsMachO(Section, *MachOObj, DMO, Relocs);
  else
    Linker.reportWarning(Twine("unsupported object file type: ") +
                             Obj.getFileName(),
                         DMO.getObjectFilename());
  if (Relocs.empty())
    return false;

  // Sort the relocations by offset. We will walk the DIEs linearly in
  // the file, this allows us to just keep an index in the relocation
  // array that we advance during our walk, rather than resorting to
  // some associative container. See DwarfLinkerForBinary::NextValidReloc.
  llvm::sort(Relocs);
  return true;
}

/// Look for relocations in the debug_info and debug_addr section that match
/// entries in the debug map. These relocations will drive the Dwarf link by
/// indicating which DIEs refer to symbols present in the linked binary.
/// \returns whether there are any valid relocations in the debug info.
bool DwarfLinkerForBinary::AddressManager::findValidRelocsInDebugSections(
    const object::ObjectFile &Obj, const DebugMapObject &DMO) {
  // Find the debug_info section.
  bool FoundValidRelocs = false;
  for (const object::SectionRef &Section : Obj.sections()) {
    StringRef SectionName;
    if (Expected<StringRef> NameOrErr = Section.getName())
      SectionName = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    SectionName = SectionName.substr(SectionName.find_first_not_of("._"));
    if (SectionName == "debug_info")
      FoundValidRelocs |=
          findValidRelocs(Section, Obj, DMO, ValidDebugInfoRelocs);
    if (SectionName == "debug_addr")
      FoundValidRelocs |=
          findValidRelocs(Section, Obj, DMO, ValidDebugAddrRelocs);
  }
  return FoundValidRelocs;
}

std::vector<ValidReloc> DwarfLinkerForBinary::AddressManager::getRelocations(
    const std::vector<ValidReloc> &Relocs, uint64_t StartPos, uint64_t EndPos) {
  std::vector<ValidReloc> Res;

  auto CurReloc = partition_point(Relocs, [StartPos](const ValidReloc &Reloc) {
    return (uint64_t)Reloc.Offset < StartPos;
  });

  while (CurReloc != Relocs.end() && CurReloc->Offset >= StartPos &&
         (uint64_t)CurReloc->Offset < EndPos) {
    Res.push_back(*CurReloc);
    CurReloc++;
  }

  return Res;
}

void DwarfLinkerForBinary::AddressManager::printReloc(const ValidReloc &Reloc) {
  const auto &Mapping = Reloc.SymbolMapping;
  const uint64_t ObjectAddress = Mapping.ObjectAddress
                                     ? uint64_t(*Mapping.ObjectAddress)
                                     : std::numeric_limits<uint64_t>::max();

  outs() << "Found valid debug map entry: " << Reloc.SymbolName << "\t"
         << format("0x%016" PRIx64 " => 0x%016" PRIx64 "\n", ObjectAddress,
                   uint64_t(Mapping.BinaryAddress));
}

int64_t
DwarfLinkerForBinary::AddressManager::getRelocValue(const ValidReloc &Reloc) {
  int64_t AddrAdjust = relocate(Reloc);
  if (Reloc.SymbolMapping.ObjectAddress)
    AddrAdjust -= uint64_t(*Reloc.SymbolMapping.ObjectAddress);
  return AddrAdjust;
}

std::optional<int64_t>
DwarfLinkerForBinary::AddressManager::hasValidRelocationAt(
    const std::vector<ValidReloc> &AllRelocs, uint64_t StartOffset,
    uint64_t EndOffset, bool Verbose) {
  std::vector<ValidReloc> Relocs =
      getRelocations(AllRelocs, StartOffset, EndOffset);
  if (Relocs.size() == 0)
    return std::nullopt;

  if (Verbose)
    printReloc(Relocs[0]);

  return getRelocValue(Relocs[0]);
}

/// Get the starting and ending (exclusive) offset for the
/// attribute with index \p Idx descibed by \p Abbrev. \p Offset is
/// supposed to point to the position of the first attribute described
/// by \p Abbrev.
/// \return [StartOffset, EndOffset) as a pair.
static std::pair<uint64_t, uint64_t>
getAttributeOffsets(const DWARFAbbreviationDeclaration *Abbrev, unsigned Idx,
                    uint64_t Offset, const DWARFUnit &Unit) {
  DataExtractor Data = Unit.getDebugInfoExtractor();

  for (unsigned I = 0; I < Idx; ++I)
    DWARFFormValue::skipValue(Abbrev->getFormByIndex(I), Data, &Offset,
                              Unit.getFormParams());

  uint64_t End = Offset;
  DWARFFormValue::skipValue(Abbrev->getFormByIndex(Idx), Data, &End,
                            Unit.getFormParams());

  return std::make_pair(Offset, End);
}

std::optional<int64_t>
DwarfLinkerForBinary::AddressManager::getExprOpAddressRelocAdjustment(
    DWARFUnit &U, const DWARFExpression::Operation &Op, uint64_t StartOffset,
    uint64_t EndOffset, bool Verbose) {
  switch (Op.getCode()) {
  default: {
    assert(false && "Specified operation does not have address operand");
  } break;
  case dwarf::DW_OP_const2u:
  case dwarf::DW_OP_const4u:
  case dwarf::DW_OP_const8u:
  case dwarf::DW_OP_const2s:
  case dwarf::DW_OP_const4s:
  case dwarf::DW_OP_const8s:
  case dwarf::DW_OP_addr: {
    return hasValidRelocationAt(ValidDebugInfoRelocs, StartOffset, EndOffset,
                                Verbose);
  } break;
  case dwarf::DW_OP_constx:
  case dwarf::DW_OP_addrx: {
    return hasValidRelocationAt(ValidDebugAddrRelocs, StartOffset, EndOffset,
                                Verbose);
  } break;
  }

  return std::nullopt;
}

std::optional<int64_t>
DwarfLinkerForBinary::AddressManager::getSubprogramRelocAdjustment(
    const DWARFDie &DIE, bool Verbose) {
  const auto *Abbrev = DIE.getAbbreviationDeclarationPtr();

  std::optional<uint32_t> LowPcIdx =
      Abbrev->findAttributeIndex(dwarf::DW_AT_low_pc);
  if (!LowPcIdx)
    return std::nullopt;

  dwarf::Form Form = Abbrev->getFormByIndex(*LowPcIdx);

  switch (Form) {
  case dwarf::DW_FORM_addr: {
    uint64_t Offset = DIE.getOffset() + getULEB128Size(Abbrev->getCode());
    uint64_t LowPcOffset, LowPcEndOffset;
    std::tie(LowPcOffset, LowPcEndOffset) =
        getAttributeOffsets(Abbrev, *LowPcIdx, Offset, *DIE.getDwarfUnit());
    return hasValidRelocationAt(ValidDebugInfoRelocs, LowPcOffset,
                                LowPcEndOffset, Verbose);
  }
  case dwarf::DW_FORM_addrx:
  case dwarf::DW_FORM_addrx1:
  case dwarf::DW_FORM_addrx2:
  case dwarf::DW_FORM_addrx3:
  case dwarf::DW_FORM_addrx4: {
    std::optional<DWARFFormValue> AddrValue = DIE.find(dwarf::DW_AT_low_pc);
    if (std::optional<uint64_t> AddressOffset =
            DIE.getDwarfUnit()->getIndexedAddressOffset(
                AddrValue->getRawUValue()))
      return hasValidRelocationAt(
          ValidDebugAddrRelocs, *AddressOffset,
          *AddressOffset + DIE.getDwarfUnit()->getAddressByteSize(), Verbose);

    Linker.reportWarning("no base offset for address table", SrcFileName);
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }
}

std::optional<StringRef>
DwarfLinkerForBinary::AddressManager::getLibraryInstallName() {
  return LibInstallName;
}

uint64_t
DwarfLinkerForBinary::AddressManager::relocate(const ValidReloc &Reloc) const {
  return Reloc.SymbolMapping.BinaryAddress + Reloc.Addend;
}

void DwarfLinkerForBinary::AddressManager::updateAndSaveValidRelocs(
    bool IsDWARF5, uint64_t OriginalUnitOffset, int64_t LinkedOffset,
    uint64_t StartOffset, uint64_t EndOffset) {
  std::vector<ValidReloc> InRelocs =
      getRelocations(ValidDebugInfoRelocs, StartOffset, EndOffset);
  if (IsDWARF5)
    InRelocs = getRelocations(ValidDebugAddrRelocs, StartOffset, EndOffset);
  DwarfLinkerRelocMap->updateAndSaveValidRelocs(
      IsDWARF5, InRelocs, OriginalUnitOffset, LinkedOffset);
}

void DwarfLinkerForBinary::AddressManager::updateRelocationsWithUnitOffset(
    uint64_t OriginalUnitOffset, uint64_t OutputUnitOffset) {
  DwarfLinkerRelocMap->updateRelocationsWithUnitOffset(OriginalUnitOffset,
                                                       OutputUnitOffset);
}
/// Apply the valid relocations found by findValidRelocs() to
/// the buffer \p Data, taking into account that Data is at \p BaseOffset
/// in the debug_info section.
///
/// Like for findValidRelocs(), this function must be called with
/// monotonic \p BaseOffset values.
///
/// \returns whether any reloc has been applied.
bool DwarfLinkerForBinary::AddressManager::applyValidRelocs(
    MutableArrayRef<char> Data, uint64_t BaseOffset, bool IsLittleEndian) {

  std::vector<ValidReloc> Relocs = getRelocations(
      ValidDebugInfoRelocs, BaseOffset, BaseOffset + Data.size());

  for (const ValidReloc &CurReloc : Relocs) {
    assert(CurReloc.Offset - BaseOffset < Data.size());
    assert(CurReloc.Offset - BaseOffset + CurReloc.Size <= Data.size());
    char Buf[8];
    uint64_t Value = relocate(CurReloc);
    for (unsigned I = 0; I != CurReloc.Size; ++I) {
      unsigned Index = IsLittleEndian ? I : (CurReloc.Size - I - 1);
      Buf[I] = uint8_t(Value >> (Index * 8));
    }
    assert(CurReloc.Size <= sizeof(Buf));
    memcpy(&Data[CurReloc.Offset - BaseOffset], Buf, CurReloc.Size);
  }
  return Relocs.size() > 0;
}

void DwarfLinkerForBinaryRelocationMap::init(DWARFContext &Context) {
  for (const std::unique_ptr<DWARFUnit> &CU : Context.compile_units())
    StoredValidDebugInfoRelocsMap.insert(
        std::make_pair(CU->getOffset(), std::vector<ValidReloc>()));
  // FIXME: Support relocations debug_addr (DWARF5).
}

void DwarfLinkerForBinaryRelocationMap::addValidRelocs(RelocationMap &RM) {
  for (const auto &DebugInfoRelocs : StoredValidDebugInfoRelocsMap) {
    for (const auto &InfoReloc : DebugInfoRelocs.second)
      RM.addRelocationMapEntry(InfoReloc);
  }
  // FIXME: Support relocations debug_addr (DWARF5).
}

void DwarfLinkerForBinaryRelocationMap::updateRelocationsWithUnitOffset(
    uint64_t OriginalUnitOffset, uint64_t OutputUnitOffset) {
  std::vector<ValidReloc> &StoredValidDebugInfoRelocs =
      StoredValidDebugInfoRelocsMap[OriginalUnitOffset];
  for (ValidReloc &R : StoredValidDebugInfoRelocs) {
    R.Offset = (uint64_t)R.Offset + OutputUnitOffset;
  }
  // FIXME: Support relocations debug_addr (DWARF5).
}

void DwarfLinkerForBinaryRelocationMap::updateAndSaveValidRelocs(
    bool IsDWARF5, std::vector<ValidReloc> &InRelocs, uint64_t UnitOffset,
    int64_t LinkedOffset) {
  std::vector<ValidReloc> &OutRelocs =
      StoredValidDebugInfoRelocsMap[UnitOffset];
  if (IsDWARF5)
    OutRelocs = StoredValidDebugAddrRelocsMap[UnitOffset];

  for (ValidReloc &R : InRelocs) {
    OutRelocs.emplace_back(R.Offset + LinkedOffset, R.Size, R.Addend,
                           R.SymbolName, R.SymbolMapping);
  }
}

} // namespace dsymutil
} // namespace llvm
