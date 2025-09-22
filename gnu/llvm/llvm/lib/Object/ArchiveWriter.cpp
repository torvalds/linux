//===- ArchiveWriter.cpp - ar File Format implementation --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the writeArchive function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/ArchiveWriter.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/IRObjectFile.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"

#include <cerrno>
#include <map>

#if !defined(_MSC_VER) && !defined(__MINGW32__)
#include <unistd.h>
#else
#include <io.h>
#endif

using namespace llvm;
using namespace llvm::object;

struct SymMap {
  bool UseECMap = false;
  std::map<std::string, uint16_t> Map;
  std::map<std::string, uint16_t> ECMap;
};

NewArchiveMember::NewArchiveMember(MemoryBufferRef BufRef)
    : Buf(MemoryBuffer::getMemBuffer(BufRef, false)),
      MemberName(BufRef.getBufferIdentifier()) {}

object::Archive::Kind NewArchiveMember::detectKindFromObject() const {
  auto MemBufferRef = this->Buf->getMemBufferRef();
  Expected<std::unique_ptr<object::ObjectFile>> OptionalObject =
      object::ObjectFile::createObjectFile(MemBufferRef);

  if (OptionalObject) {
    if (isa<object::MachOObjectFile>(**OptionalObject))
      return object::Archive::K_DARWIN;
    if (isa<object::XCOFFObjectFile>(**OptionalObject))
      return object::Archive::K_AIXBIG;
    if (isa<object::COFFObjectFile>(**OptionalObject) ||
        isa<object::COFFImportFile>(**OptionalObject))
      return object::Archive::K_COFF;
    return object::Archive::K_GNU;
  }

  // Squelch the error in case we had a non-object file.
  consumeError(OptionalObject.takeError());

  // If we're adding a bitcode file to the archive, detect the Archive kind
  // based on the target triple.
  LLVMContext Context;
  if (identify_magic(MemBufferRef.getBuffer()) == file_magic::bitcode) {
    if (auto ObjOrErr = object::SymbolicFile::createSymbolicFile(
            MemBufferRef, file_magic::bitcode, &Context)) {
      auto &IRObject = cast<object::IRObjectFile>(**ObjOrErr);
      auto TargetTriple = Triple(IRObject.getTargetTriple());
      return object::Archive::getDefaultKindForTriple(TargetTriple);
    } else {
      // Squelch the error in case this was not a SymbolicFile.
      consumeError(ObjOrErr.takeError());
    }
  }

  return object::Archive::getDefaultKind();
}

Expected<NewArchiveMember>
NewArchiveMember::getOldMember(const object::Archive::Child &OldMember,
                               bool Deterministic) {
  Expected<llvm::MemoryBufferRef> BufOrErr = OldMember.getMemoryBufferRef();
  if (!BufOrErr)
    return BufOrErr.takeError();

  NewArchiveMember M;
  M.Buf = MemoryBuffer::getMemBuffer(*BufOrErr, false);
  M.MemberName = M.Buf->getBufferIdentifier();
  if (!Deterministic) {
    auto ModTimeOrErr = OldMember.getLastModified();
    if (!ModTimeOrErr)
      return ModTimeOrErr.takeError();
    M.ModTime = ModTimeOrErr.get();
    Expected<unsigned> UIDOrErr = OldMember.getUID();
    if (!UIDOrErr)
      return UIDOrErr.takeError();
    M.UID = UIDOrErr.get();
    Expected<unsigned> GIDOrErr = OldMember.getGID();
    if (!GIDOrErr)
      return GIDOrErr.takeError();
    M.GID = GIDOrErr.get();
    Expected<sys::fs::perms> AccessModeOrErr = OldMember.getAccessMode();
    if (!AccessModeOrErr)
      return AccessModeOrErr.takeError();
    M.Perms = AccessModeOrErr.get();
  }
  return std::move(M);
}

Expected<NewArchiveMember> NewArchiveMember::getFile(StringRef FileName,
                                                     bool Deterministic) {
  sys::fs::file_status Status;
  auto FDOrErr = sys::fs::openNativeFileForRead(FileName);
  if (!FDOrErr)
    return FDOrErr.takeError();
  sys::fs::file_t FD = *FDOrErr;
  assert(FD != sys::fs::kInvalidFile);

  if (auto EC = sys::fs::status(FD, Status))
    return errorCodeToError(EC);

  // Opening a directory doesn't make sense. Let it fail.
  // Linux cannot open directories with open(2), although
  // cygwin and *bsd can.
  if (Status.type() == sys::fs::file_type::directory_file)
    return errorCodeToError(make_error_code(errc::is_a_directory));

  ErrorOr<std::unique_ptr<MemoryBuffer>> MemberBufferOrErr =
      MemoryBuffer::getOpenFile(FD, FileName, Status.getSize(), false);
  if (!MemberBufferOrErr)
    return errorCodeToError(MemberBufferOrErr.getError());

  if (auto EC = sys::fs::closeFile(FD))
    return errorCodeToError(EC);

  NewArchiveMember M;
  M.Buf = std::move(*MemberBufferOrErr);
  M.MemberName = M.Buf->getBufferIdentifier();
  if (!Deterministic) {
    M.ModTime = std::chrono::time_point_cast<std::chrono::seconds>(
        Status.getLastModificationTime());
    M.UID = Status.getUser();
    M.GID = Status.getGroup();
    M.Perms = Status.permissions();
  }
  return std::move(M);
}

template <typename T>
static void printWithSpacePadding(raw_ostream &OS, T Data, unsigned Size) {
  uint64_t OldPos = OS.tell();
  OS << Data;
  unsigned SizeSoFar = OS.tell() - OldPos;
  assert(SizeSoFar <= Size && "Data doesn't fit in Size");
  OS.indent(Size - SizeSoFar);
}

static bool isDarwin(object::Archive::Kind Kind) {
  return Kind == object::Archive::K_DARWIN ||
         Kind == object::Archive::K_DARWIN64;
}

static bool isAIXBigArchive(object::Archive::Kind Kind) {
  return Kind == object::Archive::K_AIXBIG;
}

static bool isCOFFArchive(object::Archive::Kind Kind) {
  return Kind == object::Archive::K_COFF;
}

static bool isBSDLike(object::Archive::Kind Kind) {
  switch (Kind) {
  case object::Archive::K_GNU:
  case object::Archive::K_GNU64:
  case object::Archive::K_AIXBIG:
  case object::Archive::K_COFF:
    return false;
  case object::Archive::K_BSD:
  case object::Archive::K_DARWIN:
  case object::Archive::K_DARWIN64:
    return true;
  }
  llvm_unreachable("not supported for writting");
}

template <class T>
static void print(raw_ostream &Out, object::Archive::Kind Kind, T Val) {
  support::endian::write(Out, Val,
                         isBSDLike(Kind) ? llvm::endianness::little
                                         : llvm::endianness::big);
}

template <class T> static void printLE(raw_ostream &Out, T Val) {
  support::endian::write(Out, Val, llvm::endianness::little);
}

static void printRestOfMemberHeader(
    raw_ostream &Out, const sys::TimePoint<std::chrono::seconds> &ModTime,
    unsigned UID, unsigned GID, unsigned Perms, uint64_t Size) {
  printWithSpacePadding(Out, sys::toTimeT(ModTime), 12);

  // The format has only 6 chars for uid and gid. Truncate if the provided
  // values don't fit.
  printWithSpacePadding(Out, UID % 1000000, 6);
  printWithSpacePadding(Out, GID % 1000000, 6);

  printWithSpacePadding(Out, format("%o", Perms), 8);
  printWithSpacePadding(Out, Size, 10);
  Out << "`\n";
}

static void
printGNUSmallMemberHeader(raw_ostream &Out, StringRef Name,
                          const sys::TimePoint<std::chrono::seconds> &ModTime,
                          unsigned UID, unsigned GID, unsigned Perms,
                          uint64_t Size) {
  printWithSpacePadding(Out, Twine(Name) + "/", 16);
  printRestOfMemberHeader(Out, ModTime, UID, GID, Perms, Size);
}

static void
printBSDMemberHeader(raw_ostream &Out, uint64_t Pos, StringRef Name,
                     const sys::TimePoint<std::chrono::seconds> &ModTime,
                     unsigned UID, unsigned GID, unsigned Perms, uint64_t Size) {
  uint64_t PosAfterHeader = Pos + 60 + Name.size();
  // Pad so that even 64 bit object files are aligned.
  unsigned Pad = offsetToAlignment(PosAfterHeader, Align(8));
  unsigned NameWithPadding = Name.size() + Pad;
  printWithSpacePadding(Out, Twine("#1/") + Twine(NameWithPadding), 16);
  printRestOfMemberHeader(Out, ModTime, UID, GID, Perms,
                          NameWithPadding + Size);
  Out << Name;
  while (Pad--)
    Out.write(uint8_t(0));
}

static void
printBigArchiveMemberHeader(raw_ostream &Out, StringRef Name,
                            const sys::TimePoint<std::chrono::seconds> &ModTime,
                            unsigned UID, unsigned GID, unsigned Perms,
                            uint64_t Size, uint64_t PrevOffset,
                            uint64_t NextOffset) {
  unsigned NameLen = Name.size();

  printWithSpacePadding(Out, Size, 20);           // File member size
  printWithSpacePadding(Out, NextOffset, 20);     // Next member header offset
  printWithSpacePadding(Out, PrevOffset, 20); // Previous member header offset
  printWithSpacePadding(Out, sys::toTimeT(ModTime), 12); // File member date
  // The big archive format has 12 chars for uid and gid.
  printWithSpacePadding(Out, UID % 1000000000000, 12);   // UID
  printWithSpacePadding(Out, GID % 1000000000000, 12);   // GID
  printWithSpacePadding(Out, format("%o", Perms), 12);   // Permission
  printWithSpacePadding(Out, NameLen, 4);                // Name length
  if (NameLen) {
    printWithSpacePadding(Out, Name, NameLen); // Name
    if (NameLen % 2)
      Out.write(uint8_t(0)); // Null byte padding
  }
  Out << "`\n"; // Terminator
}

static bool useStringTable(bool Thin, StringRef Name) {
  return Thin || Name.size() >= 16 || Name.contains('/');
}

static bool is64BitKind(object::Archive::Kind Kind) {
  switch (Kind) {
  case object::Archive::K_GNU:
  case object::Archive::K_BSD:
  case object::Archive::K_DARWIN:
  case object::Archive::K_COFF:
    return false;
  case object::Archive::K_AIXBIG:
  case object::Archive::K_DARWIN64:
  case object::Archive::K_GNU64:
    return true;
  }
  llvm_unreachable("not supported for writting");
}

static void
printMemberHeader(raw_ostream &Out, uint64_t Pos, raw_ostream &StringTable,
                  StringMap<uint64_t> &MemberNames, object::Archive::Kind Kind,
                  bool Thin, const NewArchiveMember &M,
                  sys::TimePoint<std::chrono::seconds> ModTime, uint64_t Size) {
  if (isBSDLike(Kind))
    return printBSDMemberHeader(Out, Pos, M.MemberName, ModTime, M.UID, M.GID,
                                M.Perms, Size);
  if (!useStringTable(Thin, M.MemberName))
    return printGNUSmallMemberHeader(Out, M.MemberName, ModTime, M.UID, M.GID,
                                     M.Perms, Size);
  Out << '/';
  uint64_t NamePos;
  if (Thin) {
    NamePos = StringTable.tell();
    StringTable << M.MemberName << "/\n";
  } else {
    auto Insertion = MemberNames.insert({M.MemberName, uint64_t(0)});
    if (Insertion.second) {
      Insertion.first->second = StringTable.tell();
      StringTable << M.MemberName;
      if (isCOFFArchive(Kind))
        StringTable << '\0';
      else
        StringTable << "/\n";
    }
    NamePos = Insertion.first->second;
  }
  printWithSpacePadding(Out, NamePos, 15);
  printRestOfMemberHeader(Out, ModTime, M.UID, M.GID, M.Perms, Size);
}

namespace {
struct MemberData {
  std::vector<unsigned> Symbols;
  std::string Header;
  StringRef Data;
  StringRef Padding;
  uint64_t PreHeadPadSize = 0;
  std::unique_ptr<SymbolicFile> SymFile = nullptr;
};
} // namespace

static MemberData computeStringTable(StringRef Names) {
  unsigned Size = Names.size();
  unsigned Pad = offsetToAlignment(Size, Align(2));
  std::string Header;
  raw_string_ostream Out(Header);
  printWithSpacePadding(Out, "//", 48);
  printWithSpacePadding(Out, Size + Pad, 10);
  Out << "`\n";
  Out.flush();
  return {{}, std::move(Header), Names, Pad ? "\n" : ""};
}

static sys::TimePoint<std::chrono::seconds> now(bool Deterministic) {
  using namespace std::chrono;

  if (!Deterministic)
    return time_point_cast<seconds>(system_clock::now());
  return sys::TimePoint<seconds>();
}

static bool isArchiveSymbol(const object::BasicSymbolRef &S) {
  Expected<uint32_t> SymFlagsOrErr = S.getFlags();
  if (!SymFlagsOrErr)
    // TODO: Actually report errors helpfully.
    report_fatal_error(SymFlagsOrErr.takeError());
  if (*SymFlagsOrErr & object::SymbolRef::SF_FormatSpecific)
    return false;
  if (!(*SymFlagsOrErr & object::SymbolRef::SF_Global))
    return false;
  if (*SymFlagsOrErr & object::SymbolRef::SF_Undefined)
    return false;
  return true;
}

static void printNBits(raw_ostream &Out, object::Archive::Kind Kind,
                       uint64_t Val) {
  if (is64BitKind(Kind))
    print<uint64_t>(Out, Kind, Val);
  else
    print<uint32_t>(Out, Kind, Val);
}

static uint64_t computeSymbolTableSize(object::Archive::Kind Kind,
                                       uint64_t NumSyms, uint64_t OffsetSize,
                                       uint64_t StringTableSize,
                                       uint32_t *Padding = nullptr) {
  assert((OffsetSize == 4 || OffsetSize == 8) && "Unsupported OffsetSize");
  uint64_t Size = OffsetSize; // Number of entries
  if (isBSDLike(Kind))
    Size += NumSyms * OffsetSize * 2; // Table
  else
    Size += NumSyms * OffsetSize; // Table
  if (isBSDLike(Kind))
    Size += OffsetSize; // byte count
  Size += StringTableSize;
  // ld64 expects the members to be 8-byte aligned for 64-bit content and at
  // least 4-byte aligned for 32-bit content.  Opt for the larger encoding
  // uniformly.
  // We do this for all bsd formats because it simplifies aligning members.
  // For the big archive format, the symbol table is the last member, so there
  // is no need to align.
  uint32_t Pad = isAIXBigArchive(Kind)
                     ? 0
                     : offsetToAlignment(Size, Align(isBSDLike(Kind) ? 8 : 2));

  Size += Pad;
  if (Padding)
    *Padding = Pad;
  return Size;
}

static uint64_t computeSymbolMapSize(uint64_t NumObj, SymMap &SymMap,
                                     uint32_t *Padding = nullptr) {
  uint64_t Size = sizeof(uint32_t) * 2; // Number of symbols and objects entries
  Size += NumObj * sizeof(uint32_t);    // Offset table

  for (auto S : SymMap.Map)
    Size += sizeof(uint16_t) + S.first.length() + 1;

  uint32_t Pad = offsetToAlignment(Size, Align(2));
  Size += Pad;
  if (Padding)
    *Padding = Pad;
  return Size;
}

static uint64_t computeECSymbolsSize(SymMap &SymMap,
                                     uint32_t *Padding = nullptr) {
  uint64_t Size = sizeof(uint32_t); // Number of symbols

  for (auto S : SymMap.ECMap)
    Size += sizeof(uint16_t) + S.first.length() + 1;

  uint32_t Pad = offsetToAlignment(Size, Align(2));
  Size += Pad;
  if (Padding)
    *Padding = Pad;
  return Size;
}

static void writeSymbolTableHeader(raw_ostream &Out, object::Archive::Kind Kind,
                                   bool Deterministic, uint64_t Size,
                                   uint64_t PrevMemberOffset = 0,
                                   uint64_t NextMemberOffset = 0) {
  if (isBSDLike(Kind)) {
    const char *Name = is64BitKind(Kind) ? "__.SYMDEF_64" : "__.SYMDEF";
    printBSDMemberHeader(Out, Out.tell(), Name, now(Deterministic), 0, 0, 0,
                         Size);
  } else if (isAIXBigArchive(Kind)) {
    printBigArchiveMemberHeader(Out, "", now(Deterministic), 0, 0, 0, Size,
                                PrevMemberOffset, NextMemberOffset);
  } else {
    const char *Name = is64BitKind(Kind) ? "/SYM64" : "";
    printGNUSmallMemberHeader(Out, Name, now(Deterministic), 0, 0, 0, Size);
  }
}

static uint64_t computeHeadersSize(object::Archive::Kind Kind,
                                   uint64_t NumMembers,
                                   uint64_t StringMemberSize, uint64_t NumSyms,
                                   uint64_t SymNamesSize, SymMap *SymMap) {
  uint32_t OffsetSize = is64BitKind(Kind) ? 8 : 4;
  uint64_t SymtabSize =
      computeSymbolTableSize(Kind, NumSyms, OffsetSize, SymNamesSize);
  auto computeSymbolTableHeaderSize = [=] {
    SmallString<0> TmpBuf;
    raw_svector_ostream Tmp(TmpBuf);
    writeSymbolTableHeader(Tmp, Kind, true, SymtabSize);
    return TmpBuf.size();
  };
  uint32_t HeaderSize = computeSymbolTableHeaderSize();
  uint64_t Size = strlen("!<arch>\n") + HeaderSize + SymtabSize;

  if (SymMap) {
    Size += HeaderSize + computeSymbolMapSize(NumMembers, *SymMap);
    if (SymMap->ECMap.size())
      Size += HeaderSize + computeECSymbolsSize(*SymMap);
  }

  return Size + StringMemberSize;
}

static Expected<std::unique_ptr<SymbolicFile>>
getSymbolicFile(MemoryBufferRef Buf, LLVMContext &Context,
                object::Archive::Kind Kind, function_ref<void(Error)> Warn) {
  const file_magic Type = identify_magic(Buf.getBuffer());
  // Don't attempt to read non-symbolic file types.
  if (!object::SymbolicFile::isSymbolicFile(Type, &Context))
    return nullptr;
  if (Type == file_magic::bitcode) {
    auto ObjOrErr = object::SymbolicFile::createSymbolicFile(
        Buf, file_magic::bitcode, &Context);
    // An error reading a bitcode file most likely indicates that the file
    // was created by a compiler from the future. Normally we don't try to
    // implement forwards compatibility for bitcode files, but when creating an
    // archive we can implement best-effort forwards compatibility by treating
    // the file as a blob and not creating symbol index entries for it. lld and
    // mold ignore the archive symbol index, so provided that you use one of
    // these linkers, LTO will work as long as lld or the gold plugin is newer
    // than the compiler. We only ignore errors if the archive format is one
    // that is supported by a linker that is known to ignore the index,
    // otherwise there's no chance of this working so we may as well error out.
    // We print a warning on read failure so that users of linkers that rely on
    // the symbol index can diagnose the issue.
    //
    // This is the same behavior as GNU ar when the linker plugin returns an
    // error when reading the input file. If the bitcode file is actually
    // malformed, it will be diagnosed at link time.
    if (!ObjOrErr) {
      switch (Kind) {
      case object::Archive::K_BSD:
      case object::Archive::K_GNU:
      case object::Archive::K_GNU64:
        Warn(ObjOrErr.takeError());
        return nullptr;
      case object::Archive::K_AIXBIG:
      case object::Archive::K_COFF:
      case object::Archive::K_DARWIN:
      case object::Archive::K_DARWIN64:
        return ObjOrErr.takeError();
      }
    }
    return std::move(*ObjOrErr);
  } else {
    auto ObjOrErr = object::SymbolicFile::createSymbolicFile(Buf);
    if (!ObjOrErr)
      return ObjOrErr.takeError();
    return std::move(*ObjOrErr);
  }
}

static bool is64BitSymbolicFile(const SymbolicFile *SymObj) {
  return SymObj != nullptr ? SymObj->is64Bit() : false;
}

// Log2 of PAGESIZE(4096) on an AIX system.
static const uint32_t Log2OfAIXPageSize = 12;

// In the AIX big archive format, since the data content follows the member file
// name, if the name ends on an odd byte, an extra byte will be added for
// padding. This ensures that the data within the member file starts at an even
// byte.
static const uint32_t MinBigArchiveMemDataAlign = 2;

template <typename AuxiliaryHeader>
uint16_t getAuxMaxAlignment(uint16_t AuxHeaderSize, AuxiliaryHeader *AuxHeader,
                            uint16_t Log2OfMaxAlign) {
  // If the member doesn't have an auxiliary header, it isn't a loadable object
  // and so it just needs aligning at the minimum value.
  if (AuxHeader == nullptr)
    return MinBigArchiveMemDataAlign;

  // If the auxiliary header does not have both MaxAlignOfData and
  // MaxAlignOfText field, it is not a loadable shared object file, so align at
  // the minimum value. The 'ModuleType' member is located right after
  // 'MaxAlignOfData' in the AuxiliaryHeader.
  if (AuxHeaderSize < offsetof(AuxiliaryHeader, ModuleType))
    return MinBigArchiveMemDataAlign;

  // If the XCOFF object file does not have a loader section, it is not
  // loadable, so align at the minimum value.
  if (AuxHeader->SecNumOfLoader == 0)
    return MinBigArchiveMemDataAlign;

  // The content of the loadable member file needs to be aligned at MAX(maximum
  // alignment of .text, maximum alignment of .data) if there are both fields.
  // If the desired alignment is > PAGESIZE, 32-bit members are aligned on a
  // word boundary, while 64-bit members are aligned on a PAGESIZE(2^12=4096)
  // boundary.
  uint16_t Log2OfAlign =
      std::max(AuxHeader->MaxAlignOfText, AuxHeader->MaxAlignOfData);
  return 1 << (Log2OfAlign > Log2OfAIXPageSize ? Log2OfMaxAlign : Log2OfAlign);
}

// AIX big archives may contain shared object members. The AIX OS requires these
// members to be aligned if they are 64-bit and recommends it for 32-bit
// members. This ensures that when these members are loaded they are aligned in
// memory.
static uint32_t getMemberAlignment(SymbolicFile *SymObj) {
  XCOFFObjectFile *XCOFFObj = dyn_cast_or_null<XCOFFObjectFile>(SymObj);
  if (!XCOFFObj)
    return MinBigArchiveMemDataAlign;

  // If the desired alignment is > PAGESIZE, 32-bit members are aligned on a
  // word boundary, while 64-bit members are aligned on a PAGESIZE boundary.
  return XCOFFObj->is64Bit()
             ? getAuxMaxAlignment(XCOFFObj->fileHeader64()->AuxHeaderSize,
                                  XCOFFObj->auxiliaryHeader64(),
                                  Log2OfAIXPageSize)
             : getAuxMaxAlignment(XCOFFObj->fileHeader32()->AuxHeaderSize,
                                  XCOFFObj->auxiliaryHeader32(), 2);
}

static void writeSymbolTable(raw_ostream &Out, object::Archive::Kind Kind,
                             bool Deterministic, ArrayRef<MemberData> Members,
                             StringRef StringTable, uint64_t MembersOffset,
                             unsigned NumSyms, uint64_t PrevMemberOffset = 0,
                             uint64_t NextMemberOffset = 0,
                             bool Is64Bit = false) {
  // We don't write a symbol table on an archive with no members -- except on
  // Darwin, where the linker will abort unless the archive has a symbol table.
  if (StringTable.empty() && !isDarwin(Kind) && !isCOFFArchive(Kind))
    return;

  uint64_t OffsetSize = is64BitKind(Kind) ? 8 : 4;
  uint32_t Pad;
  uint64_t Size = computeSymbolTableSize(Kind, NumSyms, OffsetSize,
                                         StringTable.size(), &Pad);
  writeSymbolTableHeader(Out, Kind, Deterministic, Size, PrevMemberOffset,
                         NextMemberOffset);

  if (isBSDLike(Kind))
    printNBits(Out, Kind, NumSyms * 2 * OffsetSize);
  else
    printNBits(Out, Kind, NumSyms);

  uint64_t Pos = MembersOffset;
  for (const MemberData &M : Members) {
    if (isAIXBigArchive(Kind)) {
      Pos += M.PreHeadPadSize;
      if (is64BitSymbolicFile(M.SymFile.get()) != Is64Bit) {
        Pos += M.Header.size() + M.Data.size() + M.Padding.size();
        continue;
      }
    }

    for (unsigned StringOffset : M.Symbols) {
      if (isBSDLike(Kind))
        printNBits(Out, Kind, StringOffset);
      printNBits(Out, Kind, Pos); // member offset
    }
    Pos += M.Header.size() + M.Data.size() + M.Padding.size();
  }

  if (isBSDLike(Kind))
    // byte count of the string table
    printNBits(Out, Kind, StringTable.size());
  Out << StringTable;

  while (Pad--)
    Out.write(uint8_t(0));
}

static void writeSymbolMap(raw_ostream &Out, object::Archive::Kind Kind,
                           bool Deterministic, ArrayRef<MemberData> Members,
                           SymMap &SymMap, uint64_t MembersOffset) {
  uint32_t Pad;
  uint64_t Size = computeSymbolMapSize(Members.size(), SymMap, &Pad);
  writeSymbolTableHeader(Out, Kind, Deterministic, Size, 0);

  uint32_t Pos = MembersOffset;

  printLE<uint32_t>(Out, Members.size());
  for (const MemberData &M : Members) {
    printLE(Out, Pos); // member offset
    Pos += M.Header.size() + M.Data.size() + M.Padding.size();
  }

  printLE<uint32_t>(Out, SymMap.Map.size());

  for (auto S : SymMap.Map)
    printLE(Out, S.second);
  for (auto S : SymMap.Map)
    Out << S.first << '\0';

  while (Pad--)
    Out.write(uint8_t(0));
}

static void writeECSymbols(raw_ostream &Out, object::Archive::Kind Kind,
                           bool Deterministic, ArrayRef<MemberData> Members,
                           SymMap &SymMap) {
  uint32_t Pad;
  uint64_t Size = computeECSymbolsSize(SymMap, &Pad);
  printGNUSmallMemberHeader(Out, "/<ECSYMBOLS>", now(Deterministic), 0, 0, 0,
                            Size);

  printLE<uint32_t>(Out, SymMap.ECMap.size());

  for (auto S : SymMap.ECMap)
    printLE(Out, S.second);
  for (auto S : SymMap.ECMap)
    Out << S.first << '\0';
  while (Pad--)
    Out.write(uint8_t(0));
}

static bool isECObject(object::SymbolicFile &Obj) {
  if (Obj.isCOFF())
    return cast<llvm::object::COFFObjectFile>(&Obj)->getMachine() !=
           COFF::IMAGE_FILE_MACHINE_ARM64;

  if (Obj.isCOFFImportFile())
    return cast<llvm::object::COFFImportFile>(&Obj)->getMachine() !=
           COFF::IMAGE_FILE_MACHINE_ARM64;

  if (Obj.isIR()) {
    Expected<std::string> TripleStr =
        getBitcodeTargetTriple(Obj.getMemoryBufferRef());
    if (!TripleStr)
      return false;
    Triple T(*TripleStr);
    return T.isWindowsArm64EC() || T.getArch() == Triple::x86_64;
  }

  return false;
}

static bool isAnyArm64COFF(object::SymbolicFile &Obj) {
  if (Obj.isCOFF())
    return COFF::isAnyArm64(cast<COFFObjectFile>(&Obj)->getMachine());

  if (Obj.isCOFFImportFile())
    return COFF::isAnyArm64(cast<COFFImportFile>(&Obj)->getMachine());

  if (Obj.isIR()) {
    Expected<std::string> TripleStr =
        getBitcodeTargetTriple(Obj.getMemoryBufferRef());
    if (!TripleStr)
      return false;
    Triple T(*TripleStr);
    return T.isOSWindows() && T.getArch() == Triple::aarch64;
  }

  return false;
}

bool isImportDescriptor(StringRef Name) {
  return Name.starts_with(ImportDescriptorPrefix) ||
         Name == StringRef{NullImportDescriptorSymbolName} ||
         (Name.starts_with(NullThunkDataPrefix) &&
          Name.ends_with(NullThunkDataSuffix));
}

static Expected<std::vector<unsigned>> getSymbols(SymbolicFile *Obj,
                                                  uint16_t Index,
                                                  raw_ostream &SymNames,
                                                  SymMap *SymMap) {
  std::vector<unsigned> Ret;

  if (Obj == nullptr)
    return Ret;

  std::map<std::string, uint16_t> *Map = nullptr;
  if (SymMap)
    Map = SymMap->UseECMap && isECObject(*Obj) ? &SymMap->ECMap : &SymMap->Map;

  for (const object::BasicSymbolRef &S : Obj->symbols()) {
    if (!isArchiveSymbol(S))
      continue;
    if (Map) {
      std::string Name;
      raw_string_ostream NameStream(Name);
      if (Error E = S.printName(NameStream))
        return std::move(E);
      if (Map->find(Name) != Map->end())
        continue; // ignore duplicated symbol
      (*Map)[Name] = Index;
      if (Map == &SymMap->Map) {
        Ret.push_back(SymNames.tell());
        SymNames << Name << '\0';
        // If EC is enabled, then the import descriptors are NOT put into EC
        // objects so we need to copy them to the EC map manually.
        if (SymMap->UseECMap && isImportDescriptor(Name))
          SymMap->ECMap[Name] = Index;
      }
    } else {
      Ret.push_back(SymNames.tell());
      if (Error E = S.printName(SymNames))
        return std::move(E);
      SymNames << '\0';
    }
  }
  return Ret;
}

static Expected<std::vector<MemberData>>
computeMemberData(raw_ostream &StringTable, raw_ostream &SymNames,
                  object::Archive::Kind Kind, bool Thin, bool Deterministic,
                  SymtabWritingMode NeedSymbols, SymMap *SymMap,
                  LLVMContext &Context, ArrayRef<NewArchiveMember> NewMembers,
                  std::optional<bool> IsEC, function_ref<void(Error)> Warn) {
  static char PaddingData[8] = {'\n', '\n', '\n', '\n', '\n', '\n', '\n', '\n'};
  uint64_t MemHeadPadSize = 0;
  uint64_t Pos =
      isAIXBigArchive(Kind) ? sizeof(object::BigArchive::FixLenHdr) : 0;

  std::vector<MemberData> Ret;
  bool HasObject = false;

  // Deduplicate long member names in the string table and reuse earlier name
  // offsets. This especially saves space for COFF Import libraries where all
  // members have the same name.
  StringMap<uint64_t> MemberNames;

  // UniqueTimestamps is a special case to improve debugging on Darwin:
  //
  // The Darwin linker does not link debug info into the final
  // binary. Instead, it emits entries of type N_OSO in the output
  // binary's symbol table, containing references to the linked-in
  // object files. Using that reference, the debugger can read the
  // debug data directly from the object files. Alternatively, an
  // invocation of 'dsymutil' will link the debug data from the object
  // files into a dSYM bundle, which can be loaded by the debugger,
  // instead of the object files.
  //
  // For an object file, the N_OSO entries contain the absolute path
  // path to the file, and the file's timestamp. For an object
  // included in an archive, the path is formatted like
  // "/absolute/path/to/archive.a(member.o)", and the timestamp is the
  // archive member's timestamp, rather than the archive's timestamp.
  //
  // However, this doesn't always uniquely identify an object within
  // an archive -- an archive file can have multiple entries with the
  // same filename. (This will happen commonly if the original object
  // files started in different directories.) The only way they get
  // distinguished, then, is via the timestamp. But this process is
  // unable to find the correct object file in the archive when there
  // are two files of the same name and timestamp.
  //
  // Additionally, timestamp==0 is treated specially, and causes the
  // timestamp to be ignored as a match criteria.
  //
  // That will "usually" work out okay when creating an archive not in
  // deterministic timestamp mode, because the objects will probably
  // have been created at different timestamps.
  //
  // To ameliorate this problem, in deterministic archive mode (which
  // is the default), on Darwin we will emit a unique non-zero
  // timestamp for each entry with a duplicated name. This is still
  // deterministic: the only thing affecting that timestamp is the
  // order of the files in the resultant archive.
  //
  // See also the functions that handle the lookup:
  // in lldb: ObjectContainerBSDArchive::Archive::FindObject()
  // in llvm/tools/dsymutil: BinaryHolder::GetArchiveMemberBuffers().
  bool UniqueTimestamps = Deterministic && isDarwin(Kind);
  std::map<StringRef, unsigned> FilenameCount;
  if (UniqueTimestamps) {
    for (const NewArchiveMember &M : NewMembers)
      FilenameCount[M.MemberName]++;
    for (auto &Entry : FilenameCount)
      Entry.second = Entry.second > 1 ? 1 : 0;
  }

  std::vector<std::unique_ptr<SymbolicFile>> SymFiles;

  if (NeedSymbols != SymtabWritingMode::NoSymtab || isAIXBigArchive(Kind)) {
    for (const NewArchiveMember &M : NewMembers) {
      Expected<std::unique_ptr<SymbolicFile>> SymFileOrErr = getSymbolicFile(
          M.Buf->getMemBufferRef(), Context, Kind, [&](Error Err) {
            Warn(createFileError(M.MemberName, std::move(Err)));
          });
      if (!SymFileOrErr)
        return createFileError(M.MemberName, SymFileOrErr.takeError());
      SymFiles.push_back(std::move(*SymFileOrErr));
    }
  }

  if (SymMap) {
    if (IsEC) {
      SymMap->UseECMap = *IsEC;
    } else {
      // When IsEC is not specified by the caller, use it when we have both
      // any ARM64 object (ARM64 or ARM64EC) and any EC object (ARM64EC or
      // AMD64). This may be a single ARM64EC object, but may also be separate
      // ARM64 and AMD64 objects.
      bool HaveArm64 = false, HaveEC = false;
      for (std::unique_ptr<SymbolicFile> &SymFile : SymFiles) {
        if (!SymFile)
          continue;
        if (!HaveArm64)
          HaveArm64 = isAnyArm64COFF(*SymFile);
        if (!HaveEC)
          HaveEC = isECObject(*SymFile);
        if (HaveArm64 && HaveEC) {
          SymMap->UseECMap = true;
          break;
        }
      }
    }
  }

  // The big archive format needs to know the offset of the previous member
  // header.
  uint64_t PrevOffset = 0;
  uint64_t NextMemHeadPadSize = 0;

  for (uint32_t Index = 0; Index < NewMembers.size(); ++Index) {
    const NewArchiveMember *M = &NewMembers[Index];
    std::string Header;
    raw_string_ostream Out(Header);

    MemoryBufferRef Buf = M->Buf->getMemBufferRef();
    StringRef Data = Thin ? "" : Buf.getBuffer();

    // ld64 expects the members to be 8-byte aligned for 64-bit content and at
    // least 4-byte aligned for 32-bit content.  Opt for the larger encoding
    // uniformly.  This matches the behaviour with cctools and ensures that ld64
    // is happy with archives that we generate.
    unsigned MemberPadding =
        isDarwin(Kind) ? offsetToAlignment(Data.size(), Align(8)) : 0;
    unsigned TailPadding =
        offsetToAlignment(Data.size() + MemberPadding, Align(2));
    StringRef Padding = StringRef(PaddingData, MemberPadding + TailPadding);

    sys::TimePoint<std::chrono::seconds> ModTime;
    if (UniqueTimestamps)
      // Increment timestamp for each file of a given name.
      ModTime = sys::toTimePoint(FilenameCount[M->MemberName]++);
    else
      ModTime = M->ModTime;

    uint64_t Size = Buf.getBufferSize() + MemberPadding;
    if (Size > object::Archive::MaxMemberSize) {
      std::string StringMsg =
          "File " + M->MemberName.str() + " exceeds size limit";
      return make_error<object::GenericBinaryError>(
          std::move(StringMsg), object::object_error::parse_failed);
    }

    std::unique_ptr<SymbolicFile> CurSymFile;
    if (!SymFiles.empty())
      CurSymFile = std::move(SymFiles[Index]);

    // In the big archive file format, we need to calculate and include the next
    // member offset and previous member offset in the file member header.
    if (isAIXBigArchive(Kind)) {
      uint64_t OffsetToMemData = Pos + sizeof(object::BigArMemHdrType) +
                                 alignTo(M->MemberName.size(), 2);

      if (M == NewMembers.begin())
        NextMemHeadPadSize =
            alignToPowerOf2(OffsetToMemData,
                            getMemberAlignment(CurSymFile.get())) -
            OffsetToMemData;

      MemHeadPadSize = NextMemHeadPadSize;
      Pos += MemHeadPadSize;
      uint64_t NextOffset = Pos + sizeof(object::BigArMemHdrType) +
                            alignTo(M->MemberName.size(), 2) + alignTo(Size, 2);

      // If there is another member file after this, we need to calculate the
      // padding before the header.
      if (Index + 1 != SymFiles.size()) {
        uint64_t OffsetToNextMemData =
            NextOffset + sizeof(object::BigArMemHdrType) +
            alignTo(NewMembers[Index + 1].MemberName.size(), 2);
        NextMemHeadPadSize =
            alignToPowerOf2(OffsetToNextMemData,
                            getMemberAlignment(SymFiles[Index + 1].get())) -
            OffsetToNextMemData;
        NextOffset += NextMemHeadPadSize;
      }
      printBigArchiveMemberHeader(Out, M->MemberName, ModTime, M->UID, M->GID,
                                  M->Perms, Size, PrevOffset, NextOffset);
      PrevOffset = Pos;
    } else {
      printMemberHeader(Out, Pos, StringTable, MemberNames, Kind, Thin, *M,
                        ModTime, Size);
    }
    Out.flush();

    std::vector<unsigned> Symbols;
    if (NeedSymbols != SymtabWritingMode::NoSymtab) {
      Expected<std::vector<unsigned>> SymbolsOrErr =
          getSymbols(CurSymFile.get(), Index + 1, SymNames, SymMap);
      if (!SymbolsOrErr)
        return createFileError(M->MemberName, SymbolsOrErr.takeError());
      Symbols = std::move(*SymbolsOrErr);
      if (CurSymFile)
        HasObject = true;
    }

    Pos += Header.size() + Data.size() + Padding.size();
    Ret.push_back({std::move(Symbols), std::move(Header), Data, Padding,
                   MemHeadPadSize, std::move(CurSymFile)});
  }
  // If there are no symbols, emit an empty symbol table, to satisfy Solaris
  // tools, older versions of which expect a symbol table in a non-empty
  // archive, regardless of whether there are any symbols in it.
  if (HasObject && SymNames.tell() == 0 && !isCOFFArchive(Kind))
    SymNames << '\0' << '\0' << '\0';
  return std::move(Ret);
}

namespace llvm {

static ErrorOr<SmallString<128>> canonicalizePath(StringRef P) {
  SmallString<128> Ret = P;
  std::error_code Err = sys::fs::make_absolute(Ret);
  if (Err)
    return Err;
  sys::path::remove_dots(Ret, /*removedotdot*/ true);
  return Ret;
}

// Compute the relative path from From to To.
Expected<std::string> computeArchiveRelativePath(StringRef From, StringRef To) {
  ErrorOr<SmallString<128>> PathToOrErr = canonicalizePath(To);
  ErrorOr<SmallString<128>> DirFromOrErr = canonicalizePath(From);
  if (!PathToOrErr || !DirFromOrErr)
    return errorCodeToError(errnoAsErrorCode());

  const SmallString<128> &PathTo = *PathToOrErr;
  const SmallString<128> &DirFrom = sys::path::parent_path(*DirFromOrErr);

  // Can't construct a relative path between different roots
  if (sys::path::root_name(PathTo) != sys::path::root_name(DirFrom))
    return sys::path::convert_to_slash(PathTo);

  // Skip common prefixes
  auto FromTo =
      std::mismatch(sys::path::begin(DirFrom), sys::path::end(DirFrom),
                    sys::path::begin(PathTo));
  auto FromI = FromTo.first;
  auto ToI = FromTo.second;

  // Construct relative path
  SmallString<128> Relative;
  for (auto FromE = sys::path::end(DirFrom); FromI != FromE; ++FromI)
    sys::path::append(Relative, sys::path::Style::posix, "..");

  for (auto ToE = sys::path::end(PathTo); ToI != ToE; ++ToI)
    sys::path::append(Relative, sys::path::Style::posix, *ToI);

  return std::string(Relative);
}

Error writeArchiveToStream(raw_ostream &Out,
                           ArrayRef<NewArchiveMember> NewMembers,
                           SymtabWritingMode WriteSymtab,
                           object::Archive::Kind Kind, bool Deterministic,
                           bool Thin, std::optional<bool> IsEC,
                           function_ref<void(Error)> Warn) {
  assert((!Thin || !isBSDLike(Kind)) && "Only the gnu format has a thin mode");

  SmallString<0> SymNamesBuf;
  raw_svector_ostream SymNames(SymNamesBuf);
  SmallString<0> StringTableBuf;
  raw_svector_ostream StringTable(StringTableBuf);
  SymMap SymMap;
  bool ShouldWriteSymtab = WriteSymtab != SymtabWritingMode::NoSymtab;

  // COFF symbol map uses 16-bit indexes, so we can't use it if there are too
  // many members. COFF format also requires symbol table presence, so use
  // GNU format when NoSymtab is requested.
  if (isCOFFArchive(Kind) && (NewMembers.size() > 0xfffe || !ShouldWriteSymtab))
    Kind = object::Archive::K_GNU;

  // In the scenario when LLVMContext is populated SymbolicFile will contain a
  // reference to it, thus SymbolicFile should be destroyed first.
  LLVMContext Context;

  Expected<std::vector<MemberData>> DataOrErr = computeMemberData(
      StringTable, SymNames, Kind, Thin, Deterministic, WriteSymtab,
      isCOFFArchive(Kind) ? &SymMap : nullptr, Context, NewMembers, IsEC, Warn);
  if (Error E = DataOrErr.takeError())
    return E;
  std::vector<MemberData> &Data = *DataOrErr;

  uint64_t StringTableSize = 0;
  MemberData StringTableMember;
  if (!StringTableBuf.empty() && !isAIXBigArchive(Kind)) {
    StringTableMember = computeStringTable(StringTableBuf);
    StringTableSize = StringTableMember.Header.size() +
                      StringTableMember.Data.size() +
                      StringTableMember.Padding.size();
  }

  // We would like to detect if we need to switch to a 64-bit symbol table.
  uint64_t LastMemberEndOffset = 0;
  uint64_t LastMemberHeaderOffset = 0;
  uint64_t NumSyms = 0;
  uint64_t NumSyms32 = 0; // Store symbol number of 32-bit member files.

  for (const auto &M : Data) {
    // Record the start of the member's offset
    LastMemberEndOffset += M.PreHeadPadSize;
    LastMemberHeaderOffset = LastMemberEndOffset;
    // Account for the size of each part associated with the member.
    LastMemberEndOffset += M.Header.size() + M.Data.size() + M.Padding.size();
    NumSyms += M.Symbols.size();

    // AIX big archive files may contain two global symbol tables. The
    // first global symbol table locates 32-bit file members that define global
    // symbols; the second global symbol table does the same for 64-bit file
    // members. As a big archive can have both 32-bit and 64-bit file members,
    // we need to know the number of symbols in each symbol table individually.
    if (isAIXBigArchive(Kind) && ShouldWriteSymtab) {
        if (!is64BitSymbolicFile(M.SymFile.get()))
          NumSyms32 += M.Symbols.size();
      }
  }

  std::optional<uint64_t> HeadersSize;

  // The symbol table is put at the end of the big archive file. The symbol
  // table is at the start of the archive file for other archive formats.
  if (ShouldWriteSymtab && !is64BitKind(Kind)) {
    // We assume 32-bit offsets to see if 32-bit symbols are possible or not.
    HeadersSize = computeHeadersSize(Kind, Data.size(), StringTableSize,
                                     NumSyms, SymNamesBuf.size(),
                                     isCOFFArchive(Kind) ? &SymMap : nullptr);

    // The SYM64 format is used when an archive's member offsets are larger than
    // 32-bits can hold. The need for this shift in format is detected by
    // writeArchive. To test this we need to generate a file with a member that
    // has an offset larger than 32-bits but this demands a very slow test. To
    // speed the test up we use this environment variable to pretend like the
    // cutoff happens before 32-bits and instead happens at some much smaller
    // value.
    uint64_t Sym64Threshold = 1ULL << 32;
    const char *Sym64Env = std::getenv("SYM64_THRESHOLD");
    if (Sym64Env)
      StringRef(Sym64Env).getAsInteger(10, Sym64Threshold);

    // If LastMemberHeaderOffset isn't going to fit in a 32-bit varible we need
    // to switch to 64-bit. Note that the file can be larger than 4GB as long as
    // the last member starts before the 4GB offset.
    if (*HeadersSize + LastMemberHeaderOffset >= Sym64Threshold) {
      if (Kind == object::Archive::K_DARWIN)
        Kind = object::Archive::K_DARWIN64;
      else
        Kind = object::Archive::K_GNU64;
      HeadersSize.reset();
    }
  }

  if (Thin)
    Out << "!<thin>\n";
  else if (isAIXBigArchive(Kind))
    Out << "<bigaf>\n";
  else
    Out << "!<arch>\n";

  if (!isAIXBigArchive(Kind)) {
    if (ShouldWriteSymtab) {
      if (!HeadersSize)
        HeadersSize = computeHeadersSize(
            Kind, Data.size(), StringTableSize, NumSyms, SymNamesBuf.size(),
            isCOFFArchive(Kind) ? &SymMap : nullptr);
      writeSymbolTable(Out, Kind, Deterministic, Data, SymNamesBuf,
                       *HeadersSize, NumSyms);

      if (isCOFFArchive(Kind))
        writeSymbolMap(Out, Kind, Deterministic, Data, SymMap, *HeadersSize);
    }

    if (StringTableSize)
      Out << StringTableMember.Header << StringTableMember.Data
          << StringTableMember.Padding;

    if (ShouldWriteSymtab && SymMap.ECMap.size())
      writeECSymbols(Out, Kind, Deterministic, Data, SymMap);

    for (const MemberData &M : Data)
      Out << M.Header << M.Data << M.Padding;
  } else {
    HeadersSize = sizeof(object::BigArchive::FixLenHdr);
    LastMemberEndOffset += *HeadersSize;
    LastMemberHeaderOffset += *HeadersSize;

    // For the big archive (AIX) format, compute a table of member names and
    // offsets, used in the member table.
    uint64_t MemberTableNameStrTblSize = 0;
    std::vector<size_t> MemberOffsets;
    std::vector<StringRef> MemberNames;
    // Loop across object to find offset and names.
    uint64_t MemberEndOffset = sizeof(object::BigArchive::FixLenHdr);
    for (size_t I = 0, Size = NewMembers.size(); I != Size; ++I) {
      const NewArchiveMember &Member = NewMembers[I];
      MemberTableNameStrTblSize += Member.MemberName.size() + 1;
      MemberEndOffset += Data[I].PreHeadPadSize;
      MemberOffsets.push_back(MemberEndOffset);
      MemberNames.push_back(Member.MemberName);
      // File member name ended with "`\n". The length is included in
      // BigArMemHdrType.
      MemberEndOffset += sizeof(object::BigArMemHdrType) +
                         alignTo(Data[I].Data.size(), 2) +
                         alignTo(Member.MemberName.size(), 2);
    }

    // AIX member table size.
    uint64_t MemberTableSize = 20 + // Number of members field
                               20 * MemberOffsets.size() +
                               MemberTableNameStrTblSize;

    SmallString<0> SymNamesBuf32;
    SmallString<0> SymNamesBuf64;
    raw_svector_ostream SymNames32(SymNamesBuf32);
    raw_svector_ostream SymNames64(SymNamesBuf64);

    if (ShouldWriteSymtab && NumSyms)
      // Generate the symbol names for the members.
      for (const auto &M : Data) {
        Expected<std::vector<unsigned>> SymbolsOrErr = getSymbols(
            M.SymFile.get(), 0,
            is64BitSymbolicFile(M.SymFile.get()) ? SymNames64 : SymNames32,
            nullptr);
        if (!SymbolsOrErr)
          return SymbolsOrErr.takeError();
      }

    uint64_t MemberTableEndOffset =
        LastMemberEndOffset +
        alignTo(sizeof(object::BigArMemHdrType) + MemberTableSize, 2);

    // In AIX OS, The 'GlobSymOffset' field in the fixed-length header contains
    // the offset to the 32-bit global symbol table, and the 'GlobSym64Offset'
    // contains the offset to the 64-bit global symbol table.
    uint64_t GlobalSymbolOffset =
        (ShouldWriteSymtab &&
         (WriteSymtab != SymtabWritingMode::BigArchive64) && NumSyms32 > 0)
            ? MemberTableEndOffset
            : 0;

    uint64_t GlobalSymbolOffset64 = 0;
    uint64_t NumSyms64 = NumSyms - NumSyms32;
    if (ShouldWriteSymtab && (WriteSymtab != SymtabWritingMode::BigArchive32) &&
        NumSyms64 > 0) {
      if (GlobalSymbolOffset == 0)
        GlobalSymbolOffset64 = MemberTableEndOffset;
      else
        // If there is a global symbol table for 32-bit members,
        // the 64-bit global symbol table is after the 32-bit one.
        GlobalSymbolOffset64 =
            GlobalSymbolOffset + sizeof(object::BigArMemHdrType) +
            (NumSyms32 + 1) * 8 + alignTo(SymNamesBuf32.size(), 2);
    }

    // Fixed Sized Header.
    printWithSpacePadding(Out, NewMembers.size() ? LastMemberEndOffset : 0,
                          20); // Offset to member table
    // If there are no file members in the archive, there will be no global
    // symbol table.
    printWithSpacePadding(Out, GlobalSymbolOffset, 20);
    printWithSpacePadding(Out, GlobalSymbolOffset64, 20);
    printWithSpacePadding(Out,
                          NewMembers.size()
                              ? sizeof(object::BigArchive::FixLenHdr) +
                                    Data[0].PreHeadPadSize
                              : 0,
                          20); // Offset to first archive member
    printWithSpacePadding(Out, NewMembers.size() ? LastMemberHeaderOffset : 0,
                          20); // Offset to last archive member
    printWithSpacePadding(
        Out, 0,
        20); // Offset to first member of free list - Not supported yet

    for (const MemberData &M : Data) {
      Out << std::string(M.PreHeadPadSize, '\0');
      Out << M.Header << M.Data;
      if (M.Data.size() % 2)
        Out << '\0';
    }

    if (NewMembers.size()) {
      // Member table.
      printBigArchiveMemberHeader(Out, "", sys::toTimePoint(0), 0, 0, 0,
                                  MemberTableSize, LastMemberHeaderOffset,
                                  GlobalSymbolOffset ? GlobalSymbolOffset
                                                     : GlobalSymbolOffset64);
      printWithSpacePadding(Out, MemberOffsets.size(), 20); // Number of members
      for (uint64_t MemberOffset : MemberOffsets)
        printWithSpacePadding(Out, MemberOffset,
                              20); // Offset to member file header.
      for (StringRef MemberName : MemberNames)
        Out << MemberName << '\0'; // Member file name, null byte padding.

      if (MemberTableNameStrTblSize % 2)
        Out << '\0'; // Name table must be tail padded to an even number of
                     // bytes.

      if (ShouldWriteSymtab) {
        // Write global symbol table for 32-bit file members.
        if (GlobalSymbolOffset) {
          writeSymbolTable(Out, Kind, Deterministic, Data, SymNamesBuf32,
                           *HeadersSize, NumSyms32, LastMemberEndOffset,
                           GlobalSymbolOffset64);
          // Add padding between the symbol tables, if needed.
          if (GlobalSymbolOffset64 && (SymNamesBuf32.size() % 2))
            Out << '\0';
        }

        // Write global symbol table for 64-bit file members.
        if (GlobalSymbolOffset64)
          writeSymbolTable(Out, Kind, Deterministic, Data, SymNamesBuf64,
                           *HeadersSize, NumSyms64,
                           GlobalSymbolOffset ? GlobalSymbolOffset
                                              : LastMemberEndOffset,
                           0, true);
      }
    }
  }
  Out.flush();
  return Error::success();
}

void warnToStderr(Error Err) {
  llvm::logAllUnhandledErrors(std::move(Err), llvm::errs(), "warning: ");
}

Error writeArchive(StringRef ArcName, ArrayRef<NewArchiveMember> NewMembers,
                   SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
                   bool Deterministic, bool Thin,
                   std::unique_ptr<MemoryBuffer> OldArchiveBuf,
                   std::optional<bool> IsEC, function_ref<void(Error)> Warn) {
  Expected<sys::fs::TempFile> Temp =
      sys::fs::TempFile::create(ArcName + ".temp-archive-%%%%%%%.a");
  if (!Temp)
    return Temp.takeError();
  raw_fd_ostream Out(Temp->FD, false);

  if (Error E = writeArchiveToStream(Out, NewMembers, WriteSymtab, Kind,
                                     Deterministic, Thin, IsEC, Warn)) {
    if (Error DiscardError = Temp->discard())
      return joinErrors(std::move(E), std::move(DiscardError));
    return E;
  }

  // At this point, we no longer need whatever backing memory
  // was used to generate the NewMembers. On Windows, this buffer
  // could be a mapped view of the file we want to replace (if
  // we're updating an existing archive, say). In that case, the
  // rename would still succeed, but it would leave behind a
  // temporary file (actually the original file renamed) because
  // a file cannot be deleted while there's a handle open on it,
  // only renamed. So by freeing this buffer, this ensures that
  // the last open handle on the destination file, if any, is
  // closed before we attempt to rename.
  OldArchiveBuf.reset();

  return Temp->keep(ArcName);
}

Expected<std::unique_ptr<MemoryBuffer>>
writeArchiveToBuffer(ArrayRef<NewArchiveMember> NewMembers,
                     SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
                     bool Deterministic, bool Thin,
                     function_ref<void(Error)> Warn) {
  SmallVector<char, 0> ArchiveBufferVector;
  raw_svector_ostream ArchiveStream(ArchiveBufferVector);

  if (Error E =
          writeArchiveToStream(ArchiveStream, NewMembers, WriteSymtab, Kind,
                               Deterministic, Thin, std::nullopt, Warn))
    return std::move(E);

  return std::make_unique<SmallVectorMemoryBuffer>(
      std::move(ArchiveBufferVector), /*RequiresNullTerminator=*/false);
}

} // namespace llvm
