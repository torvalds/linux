//===- Archive.h - ar archive file format -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ar archive file format class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVE_H
#define LLVM_OBJECT_ARCHIVE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/fallible_iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Chrono.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace llvm {
namespace object {

const char ArchiveMagic[] = "!<arch>\n";
const char ThinArchiveMagic[] = "!<thin>\n";
const char BigArchiveMagic[] = "<bigaf>\n";

class Archive;

class AbstractArchiveMemberHeader {
protected:
  AbstractArchiveMemberHeader(const Archive *Parent) : Parent(Parent){};

public:
  friend class Archive;
  virtual std::unique_ptr<AbstractArchiveMemberHeader> clone() const = 0;
  virtual ~AbstractArchiveMemberHeader() = default;

  /// Get the name without looking up long names.
  virtual Expected<StringRef> getRawName() const = 0;
  virtual StringRef getRawAccessMode() const = 0;
  virtual StringRef getRawLastModified() const = 0;
  virtual StringRef getRawUID() const = 0;
  virtual StringRef getRawGID() const = 0;

  /// Get the name looking up long names.
  virtual Expected<StringRef> getName(uint64_t Size) const = 0;
  virtual Expected<uint64_t> getSize() const = 0;
  virtual uint64_t getOffset() const = 0;

  /// Get next file member location.
  virtual Expected<const char *> getNextChildLoc() const = 0;
  virtual Expected<bool> isThin() const = 0;

  Expected<sys::fs::perms> getAccessMode() const;
  Expected<sys::TimePoint<std::chrono::seconds>> getLastModified() const;
  Expected<unsigned> getUID() const;
  Expected<unsigned> getGID() const;

  /// Returns the size in bytes of the format-defined member header of the
  /// concrete archive type.
  virtual uint64_t getSizeOf() const = 0;

  const Archive *Parent;
};

template <typename T>
class CommonArchiveMemberHeader : public AbstractArchiveMemberHeader {
public:
  CommonArchiveMemberHeader(const Archive *Parent, const T *RawHeaderPtr)
      : AbstractArchiveMemberHeader(Parent), ArMemHdr(RawHeaderPtr){};
  StringRef getRawAccessMode() const override;
  StringRef getRawLastModified() const override;
  StringRef getRawUID() const override;
  StringRef getRawGID() const override;

  uint64_t getOffset() const override;
  uint64_t getSizeOf() const override { return sizeof(T); }

  T const *ArMemHdr;
};

struct UnixArMemHdrType {
  char Name[16];
  char LastModified[12];
  char UID[6];
  char GID[6];
  char AccessMode[8];
  char Size[10]; ///< Size of data, not including header or padding.
  char Terminator[2];
};

class ArchiveMemberHeader : public CommonArchiveMemberHeader<UnixArMemHdrType> {
public:
  ArchiveMemberHeader(const Archive *Parent, const char *RawHeaderPtr,
                      uint64_t Size, Error *Err);

  std::unique_ptr<AbstractArchiveMemberHeader> clone() const override {
    return std::make_unique<ArchiveMemberHeader>(*this);
  }

  Expected<StringRef> getRawName() const override;

  Expected<StringRef> getName(uint64_t Size) const override;
  Expected<uint64_t> getSize() const override;
  Expected<const char *> getNextChildLoc() const override;
  Expected<bool> isThin() const override;
};

// File Member Header
struct BigArMemHdrType {
  char Size[20];       // File member size in decimal
  char NextOffset[20]; // Next member offset in decimal
  char PrevOffset[20]; // Previous member offset in decimal
  char LastModified[12];
  char UID[12];
  char GID[12];
  char AccessMode[12];
  char NameLen[4]; // File member name length in decimal
  union {
    char Name[2]; // Start of member name
    char Terminator[2];
  };
};

// Define file member header of AIX big archive.
class BigArchiveMemberHeader
    : public CommonArchiveMemberHeader<BigArMemHdrType> {

public:
  BigArchiveMemberHeader(Archive const *Parent, const char *RawHeaderPtr,
                         uint64_t Size, Error *Err);
  std::unique_ptr<AbstractArchiveMemberHeader> clone() const override {
    return std::make_unique<BigArchiveMemberHeader>(*this);
  }

  Expected<StringRef> getRawName() const override;
  Expected<uint64_t> getRawNameSize() const;

  Expected<StringRef> getName(uint64_t Size) const override;
  Expected<uint64_t> getSize() const override;
  Expected<const char *> getNextChildLoc() const override;
  Expected<uint64_t> getNextOffset() const;
  Expected<bool> isThin() const override { return false; }
};

class Archive : public Binary {
  virtual void anchor();

public:
  class Child {
    friend Archive;
    friend AbstractArchiveMemberHeader;

    const Archive *Parent;
    std::unique_ptr<AbstractArchiveMemberHeader> Header;
    /// Includes header but not padding byte.
    StringRef Data;
    /// Offset from Data to the start of the file.
    uint16_t StartOfFile;

    Expected<bool> isThinMember() const;

  public:
    Child(const Archive *Parent, const char *Start, Error *Err);
    Child(const Archive *Parent, StringRef Data, uint16_t StartOfFile);

    Child(const Child &C)
        : Parent(C.Parent), Data(C.Data), StartOfFile(C.StartOfFile) {
      if (C.Header)
        Header = C.Header->clone();
    }

    Child(Child &&C) {
      Parent = std::move(C.Parent);
      Header = std::move(C.Header);
      Data = C.Data;
      StartOfFile = C.StartOfFile;
    }

    Child &operator=(Child &&C) noexcept {
      if (&C == this)
        return *this;

      Parent = std::move(C.Parent);
      Header = std::move(C.Header);
      Data = C.Data;
      StartOfFile = C.StartOfFile;

      return *this;
    }

    Child &operator=(const Child &C) {
      if (&C == this)
        return *this;

      Parent = C.Parent;
      if (C.Header)
        Header = C.Header->clone();
      Data = C.Data;
      StartOfFile = C.StartOfFile;

      return *this;
    }

    bool operator==(const Child &other) const {
      assert(!Parent || !other.Parent || Parent == other.Parent);
      return Data.begin() == other.Data.begin();
    }

    const Archive *getParent() const { return Parent; }
    Expected<Child> getNext() const;

    Expected<StringRef> getName() const;
    Expected<std::string> getFullName() const;
    Expected<StringRef> getRawName() const { return Header->getRawName(); }

    Expected<sys::TimePoint<std::chrono::seconds>> getLastModified() const {
      return Header->getLastModified();
    }

    StringRef getRawLastModified() const {
      return Header->getRawLastModified();
    }

    Expected<unsigned> getUID() const { return Header->getUID(); }
    Expected<unsigned> getGID() const { return Header->getGID(); }

    Expected<sys::fs::perms> getAccessMode() const {
      return Header->getAccessMode();
    }

    /// \return the size of the archive member without the header or padding.
    Expected<uint64_t> getSize() const;
    /// \return the size in the archive header for this member.
    Expected<uint64_t> getRawSize() const;

    Expected<StringRef> getBuffer() const;
    uint64_t getChildOffset() const;
    uint64_t getDataOffset() const { return getChildOffset() + StartOfFile; }

    Expected<MemoryBufferRef> getMemoryBufferRef() const;

    Expected<std::unique_ptr<Binary>>
    getAsBinary(LLVMContext *Context = nullptr) const;
  };

  class ChildFallibleIterator {
    Child C;

  public:
    ChildFallibleIterator() : C(Child(nullptr, nullptr, nullptr)) {}
    ChildFallibleIterator(const Child &C) : C(C) {}

    const Child *operator->() const { return &C; }
    const Child &operator*() const { return C; }

    bool operator==(const ChildFallibleIterator &other) const {
      // Ignore errors here: If an error occurred during increment then getNext
      // will have been set to child_end(), and the following comparison should
      // do the right thing.
      return C == other.C;
    }

    bool operator!=(const ChildFallibleIterator &other) const {
      return !(*this == other);
    }

    Error inc() {
      auto NextChild = C.getNext();
      if (!NextChild)
        return NextChild.takeError();
      C = std::move(*NextChild);
      return Error::success();
    }
  };

  using child_iterator = fallible_iterator<ChildFallibleIterator>;

  class Symbol {
    const Archive *Parent;
    uint32_t SymbolIndex;
    uint32_t StringIndex; // Extra index to the string.

  public:
    Symbol(const Archive *p, uint32_t symi, uint32_t stri)
        : Parent(p), SymbolIndex(symi), StringIndex(stri) {}

    bool operator==(const Symbol &other) const {
      return (Parent == other.Parent) && (SymbolIndex == other.SymbolIndex);
    }

    StringRef getName() const;
    Expected<Child> getMember() const;
    Symbol getNext() const;
    bool isECSymbol() const;
  };

  class symbol_iterator {
    Symbol symbol;

  public:
    symbol_iterator(const Symbol &s) : symbol(s) {}

    const Symbol *operator->() const { return &symbol; }
    const Symbol &operator*() const { return symbol; }

    bool operator==(const symbol_iterator &other) const {
      return symbol == other.symbol;
    }

    bool operator!=(const symbol_iterator &other) const {
      return !(*this == other);
    }

    symbol_iterator &operator++() { // Preincrement
      symbol = symbol.getNext();
      return *this;
    }
  };

  Archive(MemoryBufferRef Source, Error &Err);
  static Expected<std::unique_ptr<Archive>> create(MemoryBufferRef Source);

  /// Size field is 10 decimal digits long
  static const uint64_t MaxMemberSize = 9999999999;

  enum Kind { K_GNU, K_GNU64, K_BSD, K_DARWIN, K_DARWIN64, K_COFF, K_AIXBIG };

  Kind kind() const { return (Kind)Format; }
  bool isThin() const { return IsThin; }
  static object::Archive::Kind getDefaultKind();
  static object::Archive::Kind getDefaultKindForTriple(Triple &T);

  child_iterator child_begin(Error &Err, bool SkipInternal = true) const;
  child_iterator child_end() const;
  iterator_range<child_iterator> children(Error &Err,
                                          bool SkipInternal = true) const {
    return make_range(child_begin(Err, SkipInternal), child_end());
  }

  symbol_iterator symbol_begin() const;
  symbol_iterator symbol_end() const;
  iterator_range<symbol_iterator> symbols() const {
    return make_range(symbol_begin(), symbol_end());
  }

  Expected<iterator_range<symbol_iterator>> ec_symbols() const;

  static bool classof(Binary const *v) { return v->isArchive(); }

  // check if a symbol is in the archive
  Expected<std::optional<Child>> findSym(StringRef name) const;

  virtual bool isEmpty() const;
  bool hasSymbolTable() const;
  StringRef getSymbolTable() const { return SymbolTable; }
  StringRef getStringTable() const { return StringTable; }
  uint32_t getNumberOfSymbols() const;
  uint32_t getNumberOfECSymbols() const;
  virtual uint64_t getFirstChildOffset() const { return getArchiveMagicLen(); }

  std::vector<std::unique_ptr<MemoryBuffer>> takeThinBuffers() {
    return std::move(ThinBuffers);
  }

  std::unique_ptr<AbstractArchiveMemberHeader>
  createArchiveMemberHeader(const char *RawHeaderPtr, uint64_t Size,
                            Error *Err) const;

protected:
  uint64_t getArchiveMagicLen() const;
  void setFirstRegular(const Child &C);

  StringRef SymbolTable;
  StringRef ECSymbolTable;
  StringRef StringTable;

private:
  StringRef FirstRegularData;
  uint16_t FirstRegularStartOfFile = -1;

  unsigned Format : 3;
  unsigned IsThin : 1;
  mutable std::vector<std::unique_ptr<MemoryBuffer>> ThinBuffers;
};

class BigArchive : public Archive {
public:
  /// Fixed-Length Header.
  struct FixLenHdr {
    char Magic[sizeof(BigArchiveMagic) - 1]; ///< Big archive magic string.
    char MemOffset[20];                      ///< Offset to member table.
    char GlobSymOffset[20];                  ///< Offset to global symbol table.
    char
        GlobSym64Offset[20]; ///< Offset global symbol table for 64-bit objects.
    char FirstChildOffset[20]; ///< Offset to first archive member.
    char LastChildOffset[20];  ///< Offset to last archive member.
    char FreeOffset[20];       ///< Offset to first mem on free list.
  };

  const FixLenHdr *ArFixLenHdr;
  uint64_t FirstChildOffset = 0;
  uint64_t LastChildOffset = 0;
  std::string MergedGlobalSymtabBuf;
  bool Has32BitGlobalSymtab = false;
  bool Has64BitGlobalSymtab = false;

public:
  BigArchive(MemoryBufferRef Source, Error &Err);
  uint64_t getFirstChildOffset() const override { return FirstChildOffset; }
  uint64_t getLastChildOffset() const { return LastChildOffset; }
  bool isEmpty() const override { return getFirstChildOffset() == 0; }

  bool has32BitGlobalSymtab() { return Has32BitGlobalSymtab; }
  bool has64BitGlobalSymtab() { return Has64BitGlobalSymtab; }
};

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_ARCHIVE_H
