//===- Archive.cpp - ar File Format implementation ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ArchiveObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/Archive.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

using namespace llvm;
using namespace object;
using namespace llvm::support::endian;

void Archive::anchor() {}

static Error malformedError(Twine Msg) {
  std::string StringMsg = "truncated or malformed archive (" + Msg.str() + ")";
  return make_error<GenericBinaryError>(std::move(StringMsg),
                                        object_error::parse_failed);
}

static Error
createMemberHeaderParseError(const AbstractArchiveMemberHeader *ArMemHeader,
                             const char *RawHeaderPtr, uint64_t Size) {
  StringRef Msg("remaining size of archive too small for next archive "
                "member header ");

  Expected<StringRef> NameOrErr = ArMemHeader->getName(Size);
  if (NameOrErr)
    return malformedError(Msg + "for " + *NameOrErr);

  consumeError(NameOrErr.takeError());
  uint64_t Offset = RawHeaderPtr - ArMemHeader->Parent->getData().data();
  return malformedError(Msg + "at offset " + Twine(Offset));
}

template <class T, std::size_t N>
StringRef getFieldRawString(const T (&Field)[N]) {
  return StringRef(Field, N).rtrim(" ");
}

template <class T>
StringRef CommonArchiveMemberHeader<T>::getRawAccessMode() const {
  return getFieldRawString(ArMemHdr->AccessMode);
}

template <class T>
StringRef CommonArchiveMemberHeader<T>::getRawLastModified() const {
  return getFieldRawString(ArMemHdr->LastModified);
}

template <class T> StringRef CommonArchiveMemberHeader<T>::getRawUID() const {
  return getFieldRawString(ArMemHdr->UID);
}

template <class T> StringRef CommonArchiveMemberHeader<T>::getRawGID() const {
  return getFieldRawString(ArMemHdr->GID);
}

template <class T> uint64_t CommonArchiveMemberHeader<T>::getOffset() const {
  return reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
}

template class object::CommonArchiveMemberHeader<UnixArMemHdrType>;
template class object::CommonArchiveMemberHeader<BigArMemHdrType>;

ArchiveMemberHeader::ArchiveMemberHeader(const Archive *Parent,
                                         const char *RawHeaderPtr,
                                         uint64_t Size, Error *Err)
    : CommonArchiveMemberHeader<UnixArMemHdrType>(
          Parent, reinterpret_cast<const UnixArMemHdrType *>(RawHeaderPtr)) {
  if (RawHeaderPtr == nullptr)
    return;
  ErrorAsOutParameter ErrAsOutParam(Err);

  if (Size < getSizeOf()) {
    *Err = createMemberHeaderParseError(this, RawHeaderPtr, Size);
    return;
  }
  if (ArMemHdr->Terminator[0] != '`' || ArMemHdr->Terminator[1] != '\n') {
    if (Err) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      OS.write_escaped(
          StringRef(ArMemHdr->Terminator, sizeof(ArMemHdr->Terminator)));
      OS.flush();
      std::string Msg("terminator characters in archive member \"" + Buf +
                      "\" not the correct \"`\\n\" values for the archive "
                      "member header ");
      Expected<StringRef> NameOrErr = getName(Size);
      if (!NameOrErr) {
        consumeError(NameOrErr.takeError());
        uint64_t Offset = RawHeaderPtr - Parent->getData().data();
        *Err = malformedError(Msg + "at offset " + Twine(Offset));
      } else
        *Err = malformedError(Msg + "for " + NameOrErr.get());
    }
    return;
  }
}

BigArchiveMemberHeader::BigArchiveMemberHeader(const Archive *Parent,
                                               const char *RawHeaderPtr,
                                               uint64_t Size, Error *Err)
    : CommonArchiveMemberHeader<BigArMemHdrType>(
          Parent, reinterpret_cast<const BigArMemHdrType *>(RawHeaderPtr)) {
  if (RawHeaderPtr == nullptr)
    return;
  ErrorAsOutParameter ErrAsOutParam(Err);

  if (RawHeaderPtr + getSizeOf() >= Parent->getData().end()) {
    if (Err)
      *Err = malformedError("malformed AIX big archive: remaining buffer is "
                            "unable to contain next archive member");
    return;
  }

  if (Size < getSizeOf()) {
    Error SubErr = createMemberHeaderParseError(this, RawHeaderPtr, Size);
    if (Err)
      *Err = std::move(SubErr);
  }
}

// This gets the raw name from the ArMemHdr->Name field and checks that it is
// valid for the kind of archive.  If it is not valid it returns an Error.
Expected<StringRef> ArchiveMemberHeader::getRawName() const {
  char EndCond;
  auto Kind = Parent->kind();
  if (Kind == Archive::K_BSD || Kind == Archive::K_DARWIN64) {
    if (ArMemHdr->Name[0] == ' ') {
      uint64_t Offset =
          reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
      return malformedError("name contains a leading space for archive member "
                            "header at offset " +
                            Twine(Offset));
    }
    EndCond = ' ';
  } else if (ArMemHdr->Name[0] == '/' || ArMemHdr->Name[0] == '#')
    EndCond = ' ';
  else
    EndCond = '/';
  StringRef::size_type end =
      StringRef(ArMemHdr->Name, sizeof(ArMemHdr->Name)).find(EndCond);
  if (end == StringRef::npos)
    end = sizeof(ArMemHdr->Name);
  assert(end <= sizeof(ArMemHdr->Name) && end > 0);
  // Don't include the EndCond if there is one.
  return StringRef(ArMemHdr->Name, end);
}

Expected<uint64_t>
getArchiveMemberDecField(Twine FieldName, const StringRef RawField,
                         const Archive *Parent,
                         const AbstractArchiveMemberHeader *MemHeader) {
  uint64_t Value;
  if (RawField.getAsInteger(10, Value)) {
    uint64_t Offset = MemHeader->getOffset();
    return malformedError("characters in " + FieldName +
                          " field in archive member header are not "
                          "all decimal numbers: '" +
                          RawField +
                          "' for the archive "
                          "member header at offset " +
                          Twine(Offset));
  }
  return Value;
}

Expected<uint64_t>
getArchiveMemberOctField(Twine FieldName, const StringRef RawField,
                         const Archive *Parent,
                         const AbstractArchiveMemberHeader *MemHeader) {
  uint64_t Value;
  if (RawField.getAsInteger(8, Value)) {
    uint64_t Offset = MemHeader->getOffset();
    return malformedError("characters in " + FieldName +
                          " field in archive member header are not "
                          "all octal numbers: '" +
                          RawField +
                          "' for the archive "
                          "member header at offset " +
                          Twine(Offset));
  }
  return Value;
}

Expected<StringRef> BigArchiveMemberHeader::getRawName() const {
  Expected<uint64_t> NameLenOrErr = getArchiveMemberDecField(
      "NameLen", getFieldRawString(ArMemHdr->NameLen), Parent, this);
  if (!NameLenOrErr)
    // TODO: Out-of-line.
    return NameLenOrErr.takeError();
  uint64_t NameLen = NameLenOrErr.get();

  // If the name length is odd, pad with '\0' to get an even length. After
  // padding, there is the name terminator "`\n".
  uint64_t NameLenWithPadding = alignTo(NameLen, 2);
  StringRef NameTerminator = "`\n";
  StringRef NameStringWithNameTerminator =
      StringRef(ArMemHdr->Name, NameLenWithPadding + NameTerminator.size());
  if (!NameStringWithNameTerminator.ends_with(NameTerminator)) {
    uint64_t Offset =
        reinterpret_cast<const char *>(ArMemHdr->Name + NameLenWithPadding) -
        Parent->getData().data();
    // TODO: Out-of-line.
    return malformedError(
        "name does not have name terminator \"`\\n\" for archive member"
        "header at offset " +
        Twine(Offset));
  }
  return StringRef(ArMemHdr->Name, NameLen);
}

// member including the header, so the size of any name following the header
// is checked to make sure it does not overflow.
Expected<StringRef> ArchiveMemberHeader::getName(uint64_t Size) const {

  // This can be called from the ArchiveMemberHeader constructor when the
  // archive header is truncated to produce an error message with the name.
  // Make sure the name field is not truncated.
  if (Size < offsetof(UnixArMemHdrType, Name) + sizeof(ArMemHdr->Name)) {
    uint64_t ArchiveOffset =
        reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
    return malformedError("archive header truncated before the name field "
                          "for archive member header at offset " +
                          Twine(ArchiveOffset));
  }

  // The raw name itself can be invalid.
  Expected<StringRef> NameOrErr = getRawName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = NameOrErr.get();

  // Check if it's a special name.
  if (Name[0] == '/') {
    if (Name.size() == 1) // Linker member.
      return Name;
    if (Name.size() == 2 && Name[1] == '/') // String table.
      return Name;
    // System libraries from the Windows SDK for Windows 11 contain this symbol.
    // It looks like a CFG guard: we just skip it for now.
    if (Name == "/<XFGHASHMAP>/")
      return Name;
    // Some libraries (e.g., arm64rt.lib) from the Windows WDK
    // (version 10.0.22000.0) contain this undocumented special member.
    if (Name == "/<ECSYMBOLS>/")
      return Name;
    // It's a long name.
    // Get the string table offset.
    std::size_t StringOffset;
    if (Name.substr(1).rtrim(' ').getAsInteger(10, StringOffset)) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      OS.write_escaped(Name.substr(1).rtrim(' '));
      OS.flush();
      uint64_t ArchiveOffset =
          reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
      return malformedError("long name offset characters after the '/' are "
                            "not all decimal numbers: '" +
                            Buf + "' for archive member header at offset " +
                            Twine(ArchiveOffset));
    }

    // Verify it.
    if (StringOffset >= Parent->getStringTable().size()) {
      uint64_t ArchiveOffset =
          reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
      return malformedError("long name offset " + Twine(StringOffset) +
                            " past the end of the string table for archive "
                            "member header at offset " +
                            Twine(ArchiveOffset));
    }

    // GNU long file names end with a "/\n".
    if (Parent->kind() == Archive::K_GNU ||
        Parent->kind() == Archive::K_GNU64) {
      size_t End = Parent->getStringTable().find('\n', /*From=*/StringOffset);
      if (End == StringRef::npos || End < 1 ||
          Parent->getStringTable()[End - 1] != '/') {
        return malformedError("string table at long name offset " +
                              Twine(StringOffset) + "not terminated");
      }
      return Parent->getStringTable().slice(StringOffset, End - 1);
    }
    return Parent->getStringTable().begin() + StringOffset;
  }

  if (Name.starts_with("#1/")) {
    uint64_t NameLength;
    if (Name.substr(3).rtrim(' ').getAsInteger(10, NameLength)) {
      std::string Buf;
      raw_string_ostream OS(Buf);
      OS.write_escaped(Name.substr(3).rtrim(' '));
      OS.flush();
      uint64_t ArchiveOffset =
          reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
      return malformedError("long name length characters after the #1/ are "
                            "not all decimal numbers: '" +
                            Buf + "' for archive member header at offset " +
                            Twine(ArchiveOffset));
    }
    if (getSizeOf() + NameLength > Size) {
      uint64_t ArchiveOffset =
          reinterpret_cast<const char *>(ArMemHdr) - Parent->getData().data();
      return malformedError("long name length: " + Twine(NameLength) +
                            " extends past the end of the member or archive "
                            "for archive member header at offset " +
                            Twine(ArchiveOffset));
    }
    return StringRef(reinterpret_cast<const char *>(ArMemHdr) + getSizeOf(),
                     NameLength)
        .rtrim('\0');
  }

  // It is not a long name so trim the blanks at the end of the name.
  if (Name[Name.size() - 1] != '/')
    return Name.rtrim(' ');

  // It's a simple name.
  return Name.drop_back(1);
}

Expected<StringRef> BigArchiveMemberHeader::getName(uint64_t Size) const {
  return getRawName();
}

Expected<uint64_t> ArchiveMemberHeader::getSize() const {
  return getArchiveMemberDecField("size", getFieldRawString(ArMemHdr->Size),
                                  Parent, this);
}

Expected<uint64_t> BigArchiveMemberHeader::getSize() const {
  Expected<uint64_t> SizeOrErr = getArchiveMemberDecField(
      "size", getFieldRawString(ArMemHdr->Size), Parent, this);
  if (!SizeOrErr)
    return SizeOrErr.takeError();

  Expected<uint64_t> NameLenOrErr = getRawNameSize();
  if (!NameLenOrErr)
    return NameLenOrErr.takeError();

  return *SizeOrErr + alignTo(*NameLenOrErr, 2);
}

Expected<uint64_t> BigArchiveMemberHeader::getRawNameSize() const {
  return getArchiveMemberDecField(
      "NameLen", getFieldRawString(ArMemHdr->NameLen), Parent, this);
}

Expected<uint64_t> BigArchiveMemberHeader::getNextOffset() const {
  return getArchiveMemberDecField(
      "NextOffset", getFieldRawString(ArMemHdr->NextOffset), Parent, this);
}

Expected<sys::fs::perms> AbstractArchiveMemberHeader::getAccessMode() const {
  Expected<uint64_t> AccessModeOrErr =
      getArchiveMemberOctField("AccessMode", getRawAccessMode(), Parent, this);
  if (!AccessModeOrErr)
    return AccessModeOrErr.takeError();
  return static_cast<sys::fs::perms>(*AccessModeOrErr);
}

Expected<sys::TimePoint<std::chrono::seconds>>
AbstractArchiveMemberHeader::getLastModified() const {
  Expected<uint64_t> SecondsOrErr = getArchiveMemberDecField(
      "LastModified", getRawLastModified(), Parent, this);

  if (!SecondsOrErr)
    return SecondsOrErr.takeError();

  return sys::toTimePoint(*SecondsOrErr);
}

Expected<unsigned> AbstractArchiveMemberHeader::getUID() const {
  StringRef User = getRawUID();
  if (User.empty())
    return 0;
  return getArchiveMemberDecField("UID", User, Parent, this);
}

Expected<unsigned> AbstractArchiveMemberHeader::getGID() const {
  StringRef Group = getRawGID();
  if (Group.empty())
    return 0;
  return getArchiveMemberDecField("GID", Group, Parent, this);
}

Expected<bool> ArchiveMemberHeader::isThin() const {
  Expected<StringRef> NameOrErr = getRawName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = NameOrErr.get();
  return Parent->isThin() && Name != "/" && Name != "//" && Name != "/SYM64/";
}

Expected<const char *> ArchiveMemberHeader::getNextChildLoc() const {
  uint64_t Size = getSizeOf();
  Expected<bool> isThinOrErr = isThin();
  if (!isThinOrErr)
    return isThinOrErr.takeError();

  bool isThin = isThinOrErr.get();
  if (!isThin) {
    Expected<uint64_t> MemberSize = getSize();
    if (!MemberSize)
      return MemberSize.takeError();

    Size += MemberSize.get();
  }

  // If Size is odd, add 1 to make it even.
  const char *NextLoc =
      reinterpret_cast<const char *>(ArMemHdr) + alignTo(Size, 2);

  if (NextLoc == Parent->getMemoryBufferRef().getBufferEnd())
    return nullptr;

  return NextLoc;
}

Expected<const char *> BigArchiveMemberHeader::getNextChildLoc() const {
  if (getOffset() ==
      static_cast<const BigArchive *>(Parent)->getLastChildOffset())
    return nullptr;

  Expected<uint64_t> NextOffsetOrErr = getNextOffset();
  if (!NextOffsetOrErr)
    return NextOffsetOrErr.takeError();
  return Parent->getData().data() + NextOffsetOrErr.get();
}

Archive::Child::Child(const Archive *Parent, StringRef Data,
                      uint16_t StartOfFile)
    : Parent(Parent), Data(Data), StartOfFile(StartOfFile) {
  Header = Parent->createArchiveMemberHeader(Data.data(), Data.size(), nullptr);
}

Archive::Child::Child(const Archive *Parent, const char *Start, Error *Err)
    : Parent(Parent) {
  if (!Start) {
    Header = nullptr;
    StartOfFile = -1;
    return;
  }

  Header = Parent->createArchiveMemberHeader(
      Start,
      Parent ? Parent->getData().size() - (Start - Parent->getData().data())
             : 0,
      Err);

  // If we are pointed to real data, Start is not a nullptr, then there must be
  // a non-null Err pointer available to report malformed data on.  Only in
  // the case sentinel value is being constructed is Err is permitted to be a
  // nullptr.
  assert(Err && "Err can't be nullptr if Start is not a nullptr");

  ErrorAsOutParameter ErrAsOutParam(Err);

  // If there was an error in the construction of the Header
  // then just return with the error now set.
  if (*Err)
    return;

  uint64_t Size = Header->getSizeOf();
  Data = StringRef(Start, Size);
  Expected<bool> isThinOrErr = isThinMember();
  if (!isThinOrErr) {
    *Err = isThinOrErr.takeError();
    return;
  }
  bool isThin = isThinOrErr.get();
  if (!isThin) {
    Expected<uint64_t> MemberSize = getRawSize();
    if (!MemberSize) {
      *Err = MemberSize.takeError();
      return;
    }
    Size += MemberSize.get();
    Data = StringRef(Start, Size);
  }

  // Setup StartOfFile and PaddingBytes.
  StartOfFile = Header->getSizeOf();
  // Don't include attached name.
  Expected<StringRef> NameOrErr = getRawName();
  if (!NameOrErr) {
    *Err = NameOrErr.takeError();
    return;
  }
  StringRef Name = NameOrErr.get();

  if (Parent->kind() == Archive::K_AIXBIG) {
    // The actual start of the file is after the name and any necessary
    // even-alignment padding.
    StartOfFile += ((Name.size() + 1) >> 1) << 1;
  } else if (Name.starts_with("#1/")) {
    uint64_t NameSize;
    StringRef RawNameSize = Name.substr(3).rtrim(' ');
    if (RawNameSize.getAsInteger(10, NameSize)) {
      uint64_t Offset = Start - Parent->getData().data();
      *Err = malformedError("long name length characters after the #1/ are "
                            "not all decimal numbers: '" +
                            RawNameSize +
                            "' for archive member header at offset " +
                            Twine(Offset));
      return;
    }
    StartOfFile += NameSize;
  }
}

Expected<uint64_t> Archive::Child::getSize() const {
  if (Parent->IsThin)
    return Header->getSize();
  return Data.size() - StartOfFile;
}

Expected<uint64_t> Archive::Child::getRawSize() const {
  return Header->getSize();
}

Expected<bool> Archive::Child::isThinMember() const { return Header->isThin(); }

Expected<std::string> Archive::Child::getFullName() const {
  Expected<bool> isThin = isThinMember();
  if (!isThin)
    return isThin.takeError();
  assert(isThin.get());
  Expected<StringRef> NameOrErr = getName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = *NameOrErr;
  if (sys::path::is_absolute(Name))
    return std::string(Name);

  SmallString<128> FullName = sys::path::parent_path(
      Parent->getMemoryBufferRef().getBufferIdentifier());
  sys::path::append(FullName, Name);
  return std::string(FullName);
}

Expected<StringRef> Archive::Child::getBuffer() const {
  Expected<bool> isThinOrErr = isThinMember();
  if (!isThinOrErr)
    return isThinOrErr.takeError();
  bool isThin = isThinOrErr.get();
  if (!isThin) {
    Expected<uint64_t> Size = getSize();
    if (!Size)
      return Size.takeError();
    return StringRef(Data.data() + StartOfFile, Size.get());
  }
  Expected<std::string> FullNameOrErr = getFullName();
  if (!FullNameOrErr)
    return FullNameOrErr.takeError();
  const std::string &FullName = *FullNameOrErr;
  ErrorOr<std::unique_ptr<MemoryBuffer>> Buf = MemoryBuffer::getFile(FullName);
  if (std::error_code EC = Buf.getError())
    return errorCodeToError(EC);
  Parent->ThinBuffers.push_back(std::move(*Buf));
  return Parent->ThinBuffers.back()->getBuffer();
}

Expected<Archive::Child> Archive::Child::getNext() const {
  Expected<const char *> NextLocOrErr = Header->getNextChildLoc();
  if (!NextLocOrErr)
    return NextLocOrErr.takeError();

  const char *NextLoc = *NextLocOrErr;

  // Check to see if this is at the end of the archive.
  if (NextLoc == nullptr)
    return Child(nullptr, nullptr, nullptr);

  // Check to see if this is past the end of the archive.
  if (NextLoc > Parent->Data.getBufferEnd()) {
    std::string Msg("offset to next archive member past the end of the archive "
                    "after member ");
    Expected<StringRef> NameOrErr = getName();
    if (!NameOrErr) {
      consumeError(NameOrErr.takeError());
      uint64_t Offset = Data.data() - Parent->getData().data();
      return malformedError(Msg + "at offset " + Twine(Offset));
    } else
      return malformedError(Msg + NameOrErr.get());
  }

  Error Err = Error::success();
  Child Ret(Parent, NextLoc, &Err);
  if (Err)
    return std::move(Err);
  return Ret;
}

uint64_t Archive::Child::getChildOffset() const {
  const char *a = Parent->Data.getBuffer().data();
  const char *c = Data.data();
  uint64_t offset = c - a;
  return offset;
}

Expected<StringRef> Archive::Child::getName() const {
  Expected<uint64_t> RawSizeOrErr = getRawSize();
  if (!RawSizeOrErr)
    return RawSizeOrErr.takeError();
  uint64_t RawSize = RawSizeOrErr.get();
  Expected<StringRef> NameOrErr =
      Header->getName(Header->getSizeOf() + RawSize);
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = NameOrErr.get();
  return Name;
}

Expected<MemoryBufferRef> Archive::Child::getMemoryBufferRef() const {
  Expected<StringRef> NameOrErr = getName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = NameOrErr.get();
  Expected<StringRef> Buf = getBuffer();
  if (!Buf)
    return createFileError(Name, Buf.takeError());
  return MemoryBufferRef(*Buf, Name);
}

Expected<std::unique_ptr<Binary>>
Archive::Child::getAsBinary(LLVMContext *Context) const {
  Expected<MemoryBufferRef> BuffOrErr = getMemoryBufferRef();
  if (!BuffOrErr)
    return BuffOrErr.takeError();

  auto BinaryOrErr = createBinary(BuffOrErr.get(), Context);
  if (BinaryOrErr)
    return std::move(*BinaryOrErr);
  return BinaryOrErr.takeError();
}

Expected<std::unique_ptr<Archive>> Archive::create(MemoryBufferRef Source) {
  Error Err = Error::success();
  std::unique_ptr<Archive> Ret;
  StringRef Buffer = Source.getBuffer();

  if (Buffer.starts_with(BigArchiveMagic))
    Ret = std::make_unique<BigArchive>(Source, Err);
  else
    Ret = std::make_unique<Archive>(Source, Err);

  if (Err)
    return std::move(Err);
  return std::move(Ret);
}

std::unique_ptr<AbstractArchiveMemberHeader>
Archive::createArchiveMemberHeader(const char *RawHeaderPtr, uint64_t Size,
                                   Error *Err) const {
  ErrorAsOutParameter ErrAsOutParam(Err);
  if (kind() != K_AIXBIG)
    return std::make_unique<ArchiveMemberHeader>(this, RawHeaderPtr, Size, Err);
  return std::make_unique<BigArchiveMemberHeader>(this, RawHeaderPtr, Size,
                                                  Err);
}

uint64_t Archive::getArchiveMagicLen() const {
  if (isThin())
    return sizeof(ThinArchiveMagic) - 1;

  if (Kind() == K_AIXBIG)
    return sizeof(BigArchiveMagic) - 1;

  return sizeof(ArchiveMagic) - 1;
}

void Archive::setFirstRegular(const Child &C) {
  FirstRegularData = C.Data;
  FirstRegularStartOfFile = C.StartOfFile;
}

Archive::Archive(MemoryBufferRef Source, Error &Err)
    : Binary(Binary::ID_Archive, Source) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  StringRef Buffer = Data.getBuffer();
  // Check for sufficient magic.
  if (Buffer.starts_with(ThinArchiveMagic)) {
    IsThin = true;
  } else if (Buffer.starts_with(ArchiveMagic)) {
    IsThin = false;
  } else if (Buffer.starts_with(BigArchiveMagic)) {
    Format = K_AIXBIG;
    IsThin = false;
    return;
  } else {
    Err = make_error<GenericBinaryError>("file too small to be an archive",
                                         object_error::invalid_file_type);
    return;
  }

  // Make sure Format is initialized before any call to
  // ArchiveMemberHeader::getName() is made.  This could be a valid empty
  // archive which is the same in all formats.  So claiming it to be gnu to is
  // fine if not totally correct before we look for a string table or table of
  // contents.
  Format = K_GNU;

  // Get the special members.
  child_iterator I = child_begin(Err, false);
  if (Err)
    return;
  child_iterator E = child_end();

  // See if this is a valid empty archive and if so return.
  if (I == E) {
    Err = Error::success();
    return;
  }
  const Child *C = &*I;

  auto Increment = [&]() {
    ++I;
    if (Err)
      return true;
    C = &*I;
    return false;
  };

  Expected<StringRef> NameOrErr = C->getRawName();
  if (!NameOrErr) {
    Err = NameOrErr.takeError();
    return;
  }
  StringRef Name = NameOrErr.get();

  // Below is the pattern that is used to figure out the archive format
  // GNU archive format
  //  First member : / (may exist, if it exists, points to the symbol table )
  //  Second member : // (may exist, if it exists, points to the string table)
  //  Note : The string table is used if the filename exceeds 15 characters
  // BSD archive format
  //  First member : __.SYMDEF or "__.SYMDEF SORTED" (the symbol table)
  //  There is no string table, if the filename exceeds 15 characters or has a
  //  embedded space, the filename has #1/<size>, The size represents the size
  //  of the filename that needs to be read after the archive header
  // COFF archive format
  //  First member : /
  //  Second member : / (provides a directory of symbols)
  //  Third member : // (may exist, if it exists, contains the string table)
  //  Note: Microsoft PE/COFF Spec 8.3 says that the third member is present
  //  even if the string table is empty. However, lib.exe does not in fact
  //  seem to create the third member if there's no member whose filename
  //  exceeds 15 characters. So the third member is optional.

  if (Name == "__.SYMDEF" || Name == "__.SYMDEF_64") {
    if (Name == "__.SYMDEF")
      Format = K_BSD;
    else // Name == "__.SYMDEF_64"
      Format = K_DARWIN64;
    // We know that the symbol table is not an external file, but we still must
    // check any Expected<> return value.
    Expected<StringRef> BufOrErr = C->getBuffer();
    if (!BufOrErr) {
      Err = BufOrErr.takeError();
      return;
    }
    SymbolTable = BufOrErr.get();
    if (Increment())
      return;
    setFirstRegular(*C);

    Err = Error::success();
    return;
  }

  if (Name.starts_with("#1/")) {
    Format = K_BSD;
    // We know this is BSD, so getName will work since there is no string table.
    Expected<StringRef> NameOrErr = C->getName();
    if (!NameOrErr) {
      Err = NameOrErr.takeError();
      return;
    }
    Name = NameOrErr.get();
    if (Name == "__.SYMDEF SORTED" || Name == "__.SYMDEF") {
      // We know that the symbol table is not an external file, but we still
      // must check any Expected<> return value.
      Expected<StringRef> BufOrErr = C->getBuffer();
      if (!BufOrErr) {
        Err = BufOrErr.takeError();
        return;
      }
      SymbolTable = BufOrErr.get();
      if (Increment())
        return;
    } else if (Name == "__.SYMDEF_64 SORTED" || Name == "__.SYMDEF_64") {
      Format = K_DARWIN64;
      // We know that the symbol table is not an external file, but we still
      // must check any Expected<> return value.
      Expected<StringRef> BufOrErr = C->getBuffer();
      if (!BufOrErr) {
        Err = BufOrErr.takeError();
        return;
      }
      SymbolTable = BufOrErr.get();
      if (Increment())
        return;
    }
    setFirstRegular(*C);
    return;
  }

  // MIPS 64-bit ELF archives use a special format of a symbol table.
  // This format is marked by `ar_name` field equals to "/SYM64/".
  // For detailed description see page 96 in the following document:
  // http://techpubs.sgi.com/library/manuals/4000/007-4658-001/pdf/007-4658-001.pdf

  bool has64SymTable = false;
  if (Name == "/" || Name == "/SYM64/") {
    // We know that the symbol table is not an external file, but we still
    // must check any Expected<> return value.
    Expected<StringRef> BufOrErr = C->getBuffer();
    if (!BufOrErr) {
      Err = BufOrErr.takeError();
      return;
    }
    SymbolTable = BufOrErr.get();
    if (Name == "/SYM64/")
      has64SymTable = true;

    if (Increment())
      return;
    if (I == E) {
      Err = Error::success();
      return;
    }
    Expected<StringRef> NameOrErr = C->getRawName();
    if (!NameOrErr) {
      Err = NameOrErr.takeError();
      return;
    }
    Name = NameOrErr.get();
  }

  if (Name == "//") {
    Format = has64SymTable ? K_GNU64 : K_GNU;
    // The string table is never an external member, but we still
    // must check any Expected<> return value.
    Expected<StringRef> BufOrErr = C->getBuffer();
    if (!BufOrErr) {
      Err = BufOrErr.takeError();
      return;
    }
    StringTable = BufOrErr.get();
    if (Increment())
      return;
    setFirstRegular(*C);
    Err = Error::success();
    return;
  }

  if (Name[0] != '/') {
    Format = has64SymTable ? K_GNU64 : K_GNU;
    setFirstRegular(*C);
    Err = Error::success();
    return;
  }

  if (Name != "/") {
    Err = errorCodeToError(object_error::parse_failed);
    return;
  }

  Format = K_COFF;
  // We know that the symbol table is not an external file, but we still
  // must check any Expected<> return value.
  Expected<StringRef> BufOrErr = C->getBuffer();
  if (!BufOrErr) {
    Err = BufOrErr.takeError();
    return;
  }
  SymbolTable = BufOrErr.get();

  if (Increment())
    return;

  if (I == E) {
    setFirstRegular(*C);
    Err = Error::success();
    return;
  }

  NameOrErr = C->getRawName();
  if (!NameOrErr) {
    Err = NameOrErr.takeError();
    return;
  }
  Name = NameOrErr.get();

  if (Name == "//") {
    // The string table is never an external member, but we still
    // must check any Expected<> return value.
    Expected<StringRef> BufOrErr = C->getBuffer();
    if (!BufOrErr) {
      Err = BufOrErr.takeError();
      return;
    }
    StringTable = BufOrErr.get();
    if (Increment())
      return;

    if (I == E) {
      setFirstRegular(*C);
      Err = Error::success();
      return;
    }

    NameOrErr = C->getRawName();
    if (!NameOrErr) {
      Err = NameOrErr.takeError();
      return;
    }
    Name = NameOrErr.get();
  }

  if (Name == "/<ECSYMBOLS>/") {
    // ARM64EC-aware libraries contain an additional special member with
    // an EC symbol map after the string table. Its format is similar to a
    // regular symbol map, except it doesn't contain member offsets. Its indexes
    // refer to member offsets from the regular symbol table instead.
    Expected<StringRef> BufOrErr = C->getBuffer();
    if (!BufOrErr) {
      Err = BufOrErr.takeError();
      return;
    }
    ECSymbolTable = BufOrErr.get();
    if (Increment())
      return;
  }

  setFirstRegular(*C);
  Err = Error::success();
}

object::Archive::Kind Archive::getDefaultKindForTriple(Triple &T) {
  if (T.isOSDarwin())
    return object::Archive::K_DARWIN;
  if (T.isOSAIX())
    return object::Archive::K_AIXBIG;
  if (T.isOSWindows())
    return object::Archive::K_COFF;
  return object::Archive::K_GNU;
}

object::Archive::Kind Archive::getDefaultKind() {
  Triple HostTriple(sys::getDefaultTargetTriple());
  return getDefaultKindForTriple(HostTriple);
}

Archive::child_iterator Archive::child_begin(Error &Err,
                                             bool SkipInternal) const {
  if (isEmpty())
    return child_end();

  if (SkipInternal)
    return child_iterator::itr(
        Child(this, FirstRegularData, FirstRegularStartOfFile), Err);

  const char *Loc = Data.getBufferStart() + getFirstChildOffset();
  Child C(this, Loc, &Err);
  if (Err)
    return child_end();
  return child_iterator::itr(C, Err);
}

Archive::child_iterator Archive::child_end() const {
  return child_iterator::end(Child(nullptr, nullptr, nullptr));
}

bool Archive::Symbol::isECSymbol() const {
  // Symbols use SymbolCount..SymbolCount+getNumberOfECSymbols() for EC symbol
  // indexes.
  uint32_t SymbolCount = Parent->getNumberOfSymbols();
  return SymbolCount <= SymbolIndex &&
         SymbolIndex < SymbolCount + Parent->getNumberOfECSymbols();
}

StringRef Archive::Symbol::getName() const {
  if (isECSymbol())
    return Parent->ECSymbolTable.begin() + StringIndex;
  return Parent->getSymbolTable().begin() + StringIndex;
}

Expected<Archive::Child> Archive::Symbol::getMember() const {
  const char *Buf = Parent->getSymbolTable().begin();
  const char *Offsets = Buf;
  if (Parent->kind() == K_GNU64 || Parent->kind() == K_DARWIN64 ||
      Parent->kind() == K_AIXBIG)
    Offsets += sizeof(uint64_t);
  else
    Offsets += sizeof(uint32_t);
  uint64_t Offset = 0;
  if (Parent->kind() == K_GNU) {
    Offset = read32be(Offsets + SymbolIndex * 4);
  } else if (Parent->kind() == K_GNU64 || Parent->kind() == K_AIXBIG) {
    Offset = read64be(Offsets + SymbolIndex * 8);
  } else if (Parent->kind() == K_BSD) {
    // The SymbolIndex is an index into the ranlib structs that start at
    // Offsets (the first uint32_t is the number of bytes of the ranlib
    // structs).  The ranlib structs are a pair of uint32_t's the first
    // being a string table offset and the second being the offset into
    // the archive of the member that defines the symbol.  Which is what
    // is needed here.
    Offset = read32le(Offsets + SymbolIndex * 8 + 4);
  } else if (Parent->kind() == K_DARWIN64) {
    // The SymbolIndex is an index into the ranlib_64 structs that start at
    // Offsets (the first uint64_t is the number of bytes of the ranlib_64
    // structs).  The ranlib_64 structs are a pair of uint64_t's the first
    // being a string table offset and the second being the offset into
    // the archive of the member that defines the symbol.  Which is what
    // is needed here.
    Offset = read64le(Offsets + SymbolIndex * 16 + 8);
  } else {
    // Skip offsets.
    uint32_t MemberCount = read32le(Buf);
    Buf += MemberCount * 4 + 4;

    uint32_t SymbolCount = read32le(Buf);
    uint16_t OffsetIndex;
    if (SymbolIndex < SymbolCount) {
      // Skip SymbolCount to get to the indices table.
      const char *Indices = Buf + 4;

      // Get the index of the offset in the file member offset table for this
      // symbol.
      OffsetIndex = read16le(Indices + SymbolIndex * 2);
    } else if (isECSymbol()) {
      // Skip SymbolCount to get to the indices table.
      const char *Indices = Parent->ECSymbolTable.begin() + 4;

      // Get the index of the offset in the file member offset table for this
      // symbol.
      OffsetIndex = read16le(Indices + (SymbolIndex - SymbolCount) * 2);
    } else {
      return errorCodeToError(object_error::parse_failed);
    }
    // Subtract 1 since OffsetIndex is 1 based.
    --OffsetIndex;

    if (OffsetIndex >= MemberCount)
      return errorCodeToError(object_error::parse_failed);

    Offset = read32le(Offsets + OffsetIndex * 4);
  }

  const char *Loc = Parent->getData().begin() + Offset;
  Error Err = Error::success();
  Child C(Parent, Loc, &Err);
  if (Err)
    return std::move(Err);
  return C;
}

Archive::Symbol Archive::Symbol::getNext() const {
  Symbol t(*this);
  if (Parent->kind() == K_BSD) {
    // t.StringIndex is an offset from the start of the __.SYMDEF or
    // "__.SYMDEF SORTED" member into the string table for the ranlib
    // struct indexed by t.SymbolIndex .  To change t.StringIndex to the
    // offset in the string table for t.SymbolIndex+1 we subtract the
    // its offset from the start of the string table for t.SymbolIndex
    // and add the offset of the string table for t.SymbolIndex+1.

    // The __.SYMDEF or "__.SYMDEF SORTED" member starts with a uint32_t
    // which is the number of bytes of ranlib structs that follow.  The ranlib
    // structs are a pair of uint32_t's the first being a string table offset
    // and the second being the offset into the archive of the member that
    // define the symbol. After that the next uint32_t is the byte count of
    // the string table followed by the string table.
    const char *Buf = Parent->getSymbolTable().begin();
    uint32_t RanlibCount = 0;
    RanlibCount = read32le(Buf) / 8;
    // If t.SymbolIndex + 1 will be past the count of symbols (the RanlibCount)
    // don't change the t.StringIndex as we don't want to reference a ranlib
    // past RanlibCount.
    if (t.SymbolIndex + 1 < RanlibCount) {
      const char *Ranlibs = Buf + 4;
      uint32_t CurRanStrx = 0;
      uint32_t NextRanStrx = 0;
      CurRanStrx = read32le(Ranlibs + t.SymbolIndex * 8);
      NextRanStrx = read32le(Ranlibs + (t.SymbolIndex + 1) * 8);
      t.StringIndex -= CurRanStrx;
      t.StringIndex += NextRanStrx;
    }
  } else if (t.isECSymbol()) {
    // Go to one past next null.
    t.StringIndex = Parent->ECSymbolTable.find('\0', t.StringIndex) + 1;
  } else {
    // Go to one past next null.
    t.StringIndex = Parent->getSymbolTable().find('\0', t.StringIndex) + 1;
  }
  ++t.SymbolIndex;
  return t;
}

Archive::symbol_iterator Archive::symbol_begin() const {
  if (!hasSymbolTable())
    return symbol_iterator(Symbol(this, 0, 0));

  const char *buf = getSymbolTable().begin();
  if (kind() == K_GNU) {
    uint32_t symbol_count = 0;
    symbol_count = read32be(buf);
    buf += sizeof(uint32_t) + (symbol_count * (sizeof(uint32_t)));
  } else if (kind() == K_GNU64) {
    uint64_t symbol_count = read64be(buf);
    buf += sizeof(uint64_t) + (symbol_count * (sizeof(uint64_t)));
  } else if (kind() == K_BSD) {
    // The __.SYMDEF or "__.SYMDEF SORTED" member starts with a uint32_t
    // which is the number of bytes of ranlib structs that follow.  The ranlib
    // structs are a pair of uint32_t's the first being a string table offset
    // and the second being the offset into the archive of the member that
    // define the symbol. After that the next uint32_t is the byte count of
    // the string table followed by the string table.
    uint32_t ranlib_count = 0;
    ranlib_count = read32le(buf) / 8;
    const char *ranlibs = buf + 4;
    uint32_t ran_strx = 0;
    ran_strx = read32le(ranlibs);
    buf += sizeof(uint32_t) + (ranlib_count * (2 * (sizeof(uint32_t))));
    // Skip the byte count of the string table.
    buf += sizeof(uint32_t);
    buf += ran_strx;
  } else if (kind() == K_DARWIN64) {
    // The __.SYMDEF_64 or "__.SYMDEF_64 SORTED" member starts with a uint64_t
    // which is the number of bytes of ranlib_64 structs that follow.  The
    // ranlib_64 structs are a pair of uint64_t's the first being a string
    // table offset and the second being the offset into the archive of the
    // member that define the symbol. After that the next uint64_t is the byte
    // count of the string table followed by the string table.
    uint64_t ranlib_count = 0;
    ranlib_count = read64le(buf) / 16;
    const char *ranlibs = buf + 8;
    uint64_t ran_strx = 0;
    ran_strx = read64le(ranlibs);
    buf += sizeof(uint64_t) + (ranlib_count * (2 * (sizeof(uint64_t))));
    // Skip the byte count of the string table.
    buf += sizeof(uint64_t);
    buf += ran_strx;
  } else if (kind() == K_AIXBIG) {
    buf = getStringTable().begin();
  } else {
    uint32_t member_count = 0;
    uint32_t symbol_count = 0;
    member_count = read32le(buf);
    buf += 4 + (member_count * 4); // Skip offsets.
    symbol_count = read32le(buf);
    buf += 4 + (symbol_count * 2); // Skip indices.
  }
  uint32_t string_start_offset = buf - getSymbolTable().begin();
  return symbol_iterator(Symbol(this, 0, string_start_offset));
}

Archive::symbol_iterator Archive::symbol_end() const {
  return symbol_iterator(Symbol(this, getNumberOfSymbols(), 0));
}

Expected<iterator_range<Archive::symbol_iterator>> Archive::ec_symbols() const {
  uint32_t Count = 0;

  // Validate EC symbol table.
  if (!ECSymbolTable.empty()) {
    if (ECSymbolTable.size() < sizeof(uint32_t))
      return malformedError("invalid EC symbols size (" +
                            Twine(ECSymbolTable.size()) + ")");
    if (SymbolTable.size() < sizeof(uint32_t))
      return malformedError("invalid symbols size (" +
                            Twine(ECSymbolTable.size()) + ")");

    Count = read32le(ECSymbolTable.begin());
    size_t StringIndex = sizeof(uint32_t) + Count * sizeof(uint16_t);
    if (ECSymbolTable.size() < StringIndex)
      return malformedError("invalid EC symbols size. Size was " +
                            Twine(ECSymbolTable.size()) + ", but expected " +
                            Twine(StringIndex));

    uint32_t MemberCount = read32le(SymbolTable.begin());
    const char *Indexes = ECSymbolTable.begin() + sizeof(uint32_t);

    for (uint32_t i = 0; i < Count; ++i) {
      uint16_t Index = read16le(Indexes + i * sizeof(uint16_t));
      if (!Index)
        return malformedError("invalid EC symbol index 0");
      if (Index > MemberCount)
        return malformedError("invalid EC symbol index " + Twine(Index) +
                              " is larger than member count " +
                              Twine(MemberCount));

      StringIndex = ECSymbolTable.find('\0', StringIndex);
      if (StringIndex == StringRef::npos)
        return malformedError("malformed EC symbol names: not null-terminated");
      ++StringIndex;
    }
  }

  uint32_t SymbolCount = getNumberOfSymbols();
  return make_range(
      symbol_iterator(Symbol(this, SymbolCount,
                             sizeof(uint32_t) + Count * sizeof(uint16_t))),
      symbol_iterator(Symbol(this, SymbolCount + Count, 0)));
}

uint32_t Archive::getNumberOfSymbols() const {
  if (!hasSymbolTable())
    return 0;
  const char *buf = getSymbolTable().begin();
  if (kind() == K_GNU)
    return read32be(buf);
  if (kind() == K_GNU64 || kind() == K_AIXBIG)
    return read64be(buf);
  if (kind() == K_BSD)
    return read32le(buf) / 8;
  if (kind() == K_DARWIN64)
    return read64le(buf) / 16;
  uint32_t member_count = 0;
  member_count = read32le(buf);
  buf += 4 + (member_count * 4); // Skip offsets.
  return read32le(buf);
}

uint32_t Archive::getNumberOfECSymbols() const {
  if (ECSymbolTable.size() < sizeof(uint32_t))
    return 0;
  return read32le(ECSymbolTable.begin());
}

Expected<std::optional<Archive::Child>> Archive::findSym(StringRef name) const {
  Archive::symbol_iterator bs = symbol_begin();
  Archive::symbol_iterator es = symbol_end();

  for (; bs != es; ++bs) {
    StringRef SymName = bs->getName();
    if (SymName == name) {
      if (auto MemberOrErr = bs->getMember())
        return Child(*MemberOrErr);
      else
        return MemberOrErr.takeError();
    }
  }
  return std::nullopt;
}

// Returns true if archive file contains no member file.
bool Archive::isEmpty() const {
  return Data.getBufferSize() == getArchiveMagicLen();
}

bool Archive::hasSymbolTable() const { return !SymbolTable.empty(); }

static Error getGlobalSymtabLocAndSize(const MemoryBufferRef &Data,
                                       uint64_t GlobalSymtabOffset,
                                       const char *&GlobalSymtabLoc,
                                       uint64_t &Size, const char *BitMessage) {
  uint64_t BufferSize = Data.getBufferSize();
  uint64_t GlobalSymtabContentOffset =
      GlobalSymtabOffset + sizeof(BigArMemHdrType);
  if (GlobalSymtabContentOffset > BufferSize)
    return malformedError(
        Twine(BitMessage) + " global symbol table header at offset 0x" +
        Twine::utohexstr(GlobalSymtabOffset) + " and size 0x" +
        Twine::utohexstr(sizeof(BigArMemHdrType)) +
        " goes past the end of file");

  GlobalSymtabLoc = Data.getBufferStart() + GlobalSymtabOffset;
  const BigArMemHdrType *GlobalSymHdr =
      reinterpret_cast<const BigArMemHdrType *>(GlobalSymtabLoc);
  StringRef RawOffset = getFieldRawString(GlobalSymHdr->Size);
  if (RawOffset.getAsInteger(10, Size))
    return malformedError(Twine(BitMessage) + " global symbol table size \"" +
                          RawOffset + "\" is not a number");

  if (GlobalSymtabContentOffset + Size > BufferSize)
    return malformedError(
        Twine(BitMessage) + " global symbol table content at offset 0x" +
        Twine::utohexstr(GlobalSymtabContentOffset) + " and size 0x" +
        Twine::utohexstr(Size) + " goes past the end of file");

  return Error::success();
}

struct GlobalSymtabInfo {
  uint64_t SymNum;
  StringRef SymbolTable;
  StringRef SymbolOffsetTable;
  StringRef StringTable;
};

static void
appendGlobalSymbolTableInfo(SmallVector<GlobalSymtabInfo> &SymtabInfos,
                            const char *GlobalSymtabLoc, uint64_t Size) {
  // In a big archive, a global symbol table contains the following information:
  // - The number of symbols.
  // - The array of offsets into the archive file. The length is eight
  //   times the number of symbols.
  // - The name-string table. The size is:
  //   Size-(8*(the number of symbols + 1)).

  StringRef SymbolTable =
      StringRef(GlobalSymtabLoc + sizeof(BigArMemHdrType), Size);
  uint64_t SymNum = read64be(GlobalSymtabLoc + sizeof(BigArMemHdrType));
  StringRef SymbolOffsetTable = StringRef(SymbolTable.data() + 8, 8 * SymNum);
  unsigned SymOffsetsSize = 8 * (SymNum + 1);
  uint64_t SymbolTableStringSize = Size - SymOffsetsSize;
  StringRef StringTable =
      StringRef(SymbolTable.data() + SymOffsetsSize, SymbolTableStringSize);
  SymtabInfos.push_back({SymNum, SymbolTable, SymbolOffsetTable, StringTable});
}

BigArchive::BigArchive(MemoryBufferRef Source, Error &Err)
    : Archive(Source, Err) {
  ErrorAsOutParameter ErrAsOutParam(&Err);
  StringRef Buffer = Data.getBuffer();
  ArFixLenHdr = reinterpret_cast<const FixLenHdr *>(Buffer.data());
  uint64_t BufferSize = Data.getBufferSize();

  if (BufferSize < sizeof(FixLenHdr)) {
    Err = malformedError("malformed AIX big archive: incomplete fixed length "
                         "header, the archive is only" +
                         Twine(BufferSize) + " byte(s)");
    return;
  }

  StringRef RawOffset = getFieldRawString(ArFixLenHdr->FirstChildOffset);
  if (RawOffset.getAsInteger(10, FirstChildOffset))
    // TODO: Out-of-line.
    Err = malformedError("malformed AIX big archive: first member offset \"" +
                         RawOffset + "\" is not a number");

  RawOffset = getFieldRawString(ArFixLenHdr->LastChildOffset);
  if (RawOffset.getAsInteger(10, LastChildOffset))
    // TODO: Out-of-line.
    Err = malformedError("malformed AIX big archive: last member offset \"" +
                         RawOffset + "\" is not a number");

  uint64_t GlobSymtab32Offset = 0;
  RawOffset = getFieldRawString(ArFixLenHdr->GlobSymOffset);
  if (RawOffset.getAsInteger(10, GlobSymtab32Offset)) {
    Err = malformedError("global symbol table "
                         "offset of 32-bit members \"" +
                         RawOffset + "\" is not a number");
    return;
  }

  uint64_t GlobSymtab64Offset = 0;
  RawOffset = getFieldRawString(ArFixLenHdr->GlobSym64Offset);
  if (RawOffset.getAsInteger(10, GlobSymtab64Offset)) {
    Err = malformedError("global symbol table "
                         "offset of 64-bit members\"" +
                         RawOffset + "\" is not a number");
    return;
  }

  const char *GlobSymtab32Loc = nullptr;
  const char *GlobSymtab64Loc = nullptr;
  uint64_t GlobSymtab32Size = 0;
  uint64_t GlobSymtab64Size = 0;
  const MemoryBufferRef &MemBuffRef = getMemoryBufferRef();

  if (GlobSymtab32Offset) {
    Err =
        getGlobalSymtabLocAndSize(MemBuffRef, GlobSymtab32Offset,
                                  GlobSymtab32Loc, GlobSymtab32Size, "32-bit");
    if (Err)
      return;

    Has32BitGlobalSymtab = true;
  }

  if (GlobSymtab64Offset) {
    Err =
        getGlobalSymtabLocAndSize(MemBuffRef, GlobSymtab64Offset,
                                  GlobSymtab64Loc, GlobSymtab64Size, "64-bit");
    if (Err)
      return;

    Has64BitGlobalSymtab = true;
  }

  SmallVector<GlobalSymtabInfo> SymtabInfos;

  if (GlobSymtab32Offset)
    appendGlobalSymbolTableInfo(SymtabInfos, GlobSymtab32Loc, GlobSymtab32Size);
  if (GlobSymtab64Offset)
    appendGlobalSymbolTableInfo(SymtabInfos, GlobSymtab64Loc, GlobSymtab64Size);

  if (SymtabInfos.size() == 1) {
    SymbolTable = SymtabInfos[0].SymbolTable;
    StringTable = SymtabInfos[0].StringTable;
  } else if (SymtabInfos.size() == 2) {
    // In order to let the Archive::Symbol::getNext() work for both 32-bit and
    // 64-bit global symbol tables, we need to merge them into a single table.
    raw_string_ostream Out(MergedGlobalSymtabBuf);
    uint64_t SymNum = SymtabInfos[0].SymNum + SymtabInfos[1].SymNum;
    write(Out, SymNum, llvm::endianness::big);
    // Merge symbol offset.
    Out << SymtabInfos[0].SymbolOffsetTable;
    Out << SymtabInfos[1].SymbolOffsetTable;
    // Merge string table.
    Out << SymtabInfos[0].StringTable;
    Out << SymtabInfos[1].StringTable;
    SymbolTable = MergedGlobalSymtabBuf;
    // The size of the symbol offset to the member file is 8 bytes.
    StringTable = StringRef(SymbolTable.begin() + (SymNum + 1) * 8,
                            SymtabInfos[0].StringTable.size() +
                                SymtabInfos[1].StringTable.size());
  }

  child_iterator I = child_begin(Err, false);
  if (Err)
    return;
  child_iterator E = child_end();
  if (I == E) {
    Err = Error::success();
    return;
  }
  setFirstRegular(*I);
  Err = Error::success();
}
