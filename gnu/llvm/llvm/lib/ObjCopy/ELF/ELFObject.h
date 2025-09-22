//===- ELFObject.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_OBJCOPY_ELF_ELFOBJECT_H
#define LLVM_LIB_OBJCOPY_ELF_ELFOBJECT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/ObjCopy/CommonConfig.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/MemoryBuffer.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <vector>

namespace llvm {
enum class DebugCompressionType;
namespace objcopy {
namespace elf {

class SectionBase;
class Section;
class OwnedDataSection;
class StringTableSection;
class SymbolTableSection;
class RelocationSection;
class DynamicRelocationSection;
class GnuDebugLinkSection;
class GroupSection;
class SectionIndexSection;
class CompressedSection;
class DecompressedSection;
class Segment;
class Object;
struct Symbol;

class SectionTableRef {
  ArrayRef<std::unique_ptr<SectionBase>> Sections;

public:
  using iterator = pointee_iterator<const std::unique_ptr<SectionBase> *>;

  explicit SectionTableRef(ArrayRef<std::unique_ptr<SectionBase>> Secs)
      : Sections(Secs) {}
  SectionTableRef(const SectionTableRef &) = default;

  iterator begin() const { return iterator(Sections.data()); }
  iterator end() const { return iterator(Sections.data() + Sections.size()); }
  size_t size() const { return Sections.size(); }

  Expected<SectionBase *> getSection(uint32_t Index, Twine ErrMsg);

  template <class T>
  Expected<T *> getSectionOfType(uint32_t Index, Twine IndexErrMsg,
                                 Twine TypeErrMsg);
};

enum ElfType { ELFT_ELF32LE, ELFT_ELF64LE, ELFT_ELF32BE, ELFT_ELF64BE };

class SectionVisitor {
public:
  virtual ~SectionVisitor() = default;

  virtual Error visit(const Section &Sec) = 0;
  virtual Error visit(const OwnedDataSection &Sec) = 0;
  virtual Error visit(const StringTableSection &Sec) = 0;
  virtual Error visit(const SymbolTableSection &Sec) = 0;
  virtual Error visit(const RelocationSection &Sec) = 0;
  virtual Error visit(const DynamicRelocationSection &Sec) = 0;
  virtual Error visit(const GnuDebugLinkSection &Sec) = 0;
  virtual Error visit(const GroupSection &Sec) = 0;
  virtual Error visit(const SectionIndexSection &Sec) = 0;
  virtual Error visit(const CompressedSection &Sec) = 0;
  virtual Error visit(const DecompressedSection &Sec) = 0;
};

class MutableSectionVisitor {
public:
  virtual ~MutableSectionVisitor() = default;

  virtual Error visit(Section &Sec) = 0;
  virtual Error visit(OwnedDataSection &Sec) = 0;
  virtual Error visit(StringTableSection &Sec) = 0;
  virtual Error visit(SymbolTableSection &Sec) = 0;
  virtual Error visit(RelocationSection &Sec) = 0;
  virtual Error visit(DynamicRelocationSection &Sec) = 0;
  virtual Error visit(GnuDebugLinkSection &Sec) = 0;
  virtual Error visit(GroupSection &Sec) = 0;
  virtual Error visit(SectionIndexSection &Sec) = 0;
  virtual Error visit(CompressedSection &Sec) = 0;
  virtual Error visit(DecompressedSection &Sec) = 0;
};

class SectionWriter : public SectionVisitor {
protected:
  WritableMemoryBuffer &Out;

public:
  virtual ~SectionWriter() = default;

  Error visit(const Section &Sec) override;
  Error visit(const OwnedDataSection &Sec) override;
  Error visit(const StringTableSection &Sec) override;
  Error visit(const DynamicRelocationSection &Sec) override;
  Error visit(const SymbolTableSection &Sec) override = 0;
  Error visit(const RelocationSection &Sec) override = 0;
  Error visit(const GnuDebugLinkSection &Sec) override = 0;
  Error visit(const GroupSection &Sec) override = 0;
  Error visit(const SectionIndexSection &Sec) override = 0;
  Error visit(const CompressedSection &Sec) override = 0;
  Error visit(const DecompressedSection &Sec) override = 0;

  explicit SectionWriter(WritableMemoryBuffer &Buf) : Out(Buf) {}
};

template <class ELFT> class ELFSectionWriter : public SectionWriter {
private:
  using Elf_Word = typename ELFT::Word;
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;
  using Elf_Sym = typename ELFT::Sym;

public:
  virtual ~ELFSectionWriter() {}
  Error visit(const SymbolTableSection &Sec) override;
  Error visit(const RelocationSection &Sec) override;
  Error visit(const GnuDebugLinkSection &Sec) override;
  Error visit(const GroupSection &Sec) override;
  Error visit(const SectionIndexSection &Sec) override;
  Error visit(const CompressedSection &Sec) override;
  Error visit(const DecompressedSection &Sec) override;

  explicit ELFSectionWriter(WritableMemoryBuffer &Buf) : SectionWriter(Buf) {}
};

template <class ELFT> class ELFSectionSizer : public MutableSectionVisitor {
private:
  using Elf_Rel = typename ELFT::Rel;
  using Elf_Rela = typename ELFT::Rela;
  using Elf_Sym = typename ELFT::Sym;
  using Elf_Word = typename ELFT::Word;
  using Elf_Xword = typename ELFT::Xword;

public:
  Error visit(Section &Sec) override;
  Error visit(OwnedDataSection &Sec) override;
  Error visit(StringTableSection &Sec) override;
  Error visit(DynamicRelocationSection &Sec) override;
  Error visit(SymbolTableSection &Sec) override;
  Error visit(RelocationSection &Sec) override;
  Error visit(GnuDebugLinkSection &Sec) override;
  Error visit(GroupSection &Sec) override;
  Error visit(SectionIndexSection &Sec) override;
  Error visit(CompressedSection &Sec) override;
  Error visit(DecompressedSection &Sec) override;
};

#define MAKE_SEC_WRITER_FRIEND                                                 \
  friend class SectionWriter;                                                  \
  friend class IHexSectionWriterBase;                                          \
  friend class IHexSectionWriter;                                              \
  friend class SRECSectionWriter;                                              \
  friend class SRECSectionWriterBase;                                          \
  friend class SRECSizeCalculator;                                             \
  template <class ELFT> friend class ELFSectionWriter;                         \
  template <class ELFT> friend class ELFSectionSizer;

class BinarySectionWriter : public SectionWriter {
public:
  virtual ~BinarySectionWriter() {}

  Error visit(const SymbolTableSection &Sec) override;
  Error visit(const RelocationSection &Sec) override;
  Error visit(const GnuDebugLinkSection &Sec) override;
  Error visit(const GroupSection &Sec) override;
  Error visit(const SectionIndexSection &Sec) override;
  Error visit(const CompressedSection &Sec) override;
  Error visit(const DecompressedSection &Sec) override;

  explicit BinarySectionWriter(WritableMemoryBuffer &Buf)
      : SectionWriter(Buf) {}
};

using IHexLineData = SmallVector<char, 64>;

struct IHexRecord {
  // Memory address of the record.
  uint16_t Addr;
  // Record type (see below).
  uint16_t Type;
  // Record data in hexadecimal form.
  StringRef HexData;

  // Helper method to get file length of the record
  // including newline character
  static size_t getLength(size_t DataSize) {
    // :LLAAAATT[DD...DD]CC'
    return DataSize * 2 + 11;
  }

  // Gets length of line in a file (getLength + CRLF).
  static size_t getLineLength(size_t DataSize) {
    return getLength(DataSize) + 2;
  }

  // Given type, address and data returns line which can
  // be written to output file.
  static IHexLineData getLine(uint8_t Type, uint16_t Addr,
                              ArrayRef<uint8_t> Data);

  // Parses the line and returns record if possible.
  // Line should be trimmed from whitespace characters.
  static Expected<IHexRecord> parse(StringRef Line);

  // Calculates checksum of stringified record representation
  // S must NOT contain leading ':' and trailing whitespace
  // characters
  static uint8_t getChecksum(StringRef S);

  enum Type {
    // Contains data and a 16-bit starting address for the data.
    // The byte count specifies number of data bytes in the record.
    Data = 0,
    // Must occur exactly once per file in the last line of the file.
    // The data field is empty (thus byte count is 00) and the address
    // field is typically 0000.
    EndOfFile = 1,
    // The data field contains a 16-bit segment base address (thus byte
    // count is always 02) compatible with 80x86 real mode addressing.
    // The address field (typically 0000) is ignored. The segment address
    // from the most recent 02 record is multiplied by 16 and added to each
    // subsequent data record address to form the physical starting address
    // for the data. This allows addressing up to one megabyte of address
    // space.
    SegmentAddr = 2,
    // or 80x86 processors, specifies the initial content of the CS:IP
    // registers. The address field is 0000, the byte count is always 04,
    // the first two data bytes are the CS value, the latter two are the
    // IP value.
    StartAddr80x86 = 3,
    // Allows for 32 bit addressing (up to 4GiB). The record's address field
    // is ignored (typically 0000) and its byte count is always 02. The two
    // data bytes (big endian) specify the upper 16 bits of the 32 bit
    // absolute address for all subsequent type 00 records
    ExtendedAddr = 4,
    // The address field is 0000 (not used) and the byte count is always 04.
    // The four data bytes represent a 32-bit address value. In the case of
    // 80386 and higher CPUs, this address is loaded into the EIP register.
    StartAddr = 5,
    // We have no other valid types
    InvalidType = 6
  };
};

// Base class for IHexSectionWriter. This class implements writing algorithm,
// but doesn't actually write records. It is used for output buffer size
// calculation in IHexWriter::finalize.
class IHexSectionWriterBase : public BinarySectionWriter {
  // 20-bit segment address
  uint32_t SegmentAddr = 0;
  // Extended linear address
  uint32_t BaseAddr = 0;

  // Write segment address corresponding to 'Addr'
  uint64_t writeSegmentAddr(uint64_t Addr);
  // Write extended linear (base) address corresponding to 'Addr'
  uint64_t writeBaseAddr(uint64_t Addr);

protected:
  // Offset in the output buffer
  uint64_t Offset = 0;

  void writeSection(const SectionBase *Sec, ArrayRef<uint8_t> Data);
  virtual void writeData(uint8_t Type, uint16_t Addr, ArrayRef<uint8_t> Data);

public:
  explicit IHexSectionWriterBase(WritableMemoryBuffer &Buf)
      : BinarySectionWriter(Buf) {}

  uint64_t getBufferOffset() const { return Offset; }
  Error visit(const Section &Sec) final;
  Error visit(const OwnedDataSection &Sec) final;
  Error visit(const StringTableSection &Sec) override;
  Error visit(const DynamicRelocationSection &Sec) final;
  using BinarySectionWriter::visit;
};

// Real IHEX section writer
class IHexSectionWriter : public IHexSectionWriterBase {
public:
  IHexSectionWriter(WritableMemoryBuffer &Buf) : IHexSectionWriterBase(Buf) {}

  void writeData(uint8_t Type, uint16_t Addr, ArrayRef<uint8_t> Data) override;
  Error visit(const StringTableSection &Sec) override;
};

class Writer {
protected:
  Object &Obj;
  std::unique_ptr<WritableMemoryBuffer> Buf;
  raw_ostream &Out;

public:
  virtual ~Writer();
  virtual Error finalize() = 0;
  virtual Error write() = 0;

  Writer(Object &O, raw_ostream &Out) : Obj(O), Out(Out) {}
};

template <class ELFT> class ELFWriter : public Writer {
private:
  using Elf_Addr = typename ELFT::Addr;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Phdr = typename ELFT::Phdr;
  using Elf_Ehdr = typename ELFT::Ehdr;

  void initEhdrSegment();

  void writeEhdr();
  void writePhdr(const Segment &Seg);
  void writeShdr(const SectionBase &Sec);

  void writePhdrs();
  void writeShdrs();
  Error writeSectionData();
  void writeSegmentData();

  void assignOffsets();

  std::unique_ptr<ELFSectionWriter<ELFT>> SecWriter;

  size_t totalSize() const;

public:
  virtual ~ELFWriter() {}
  bool WriteSectionHeaders;

  // For --only-keep-debug, select an alternative section/segment layout
  // algorithm.
  bool OnlyKeepDebug;

  Error finalize() override;
  Error write() override;
  ELFWriter(Object &Obj, raw_ostream &Out, bool WSH, bool OnlyKeepDebug);
};

class BinaryWriter : public Writer {
private:
  const uint8_t GapFill;
  const uint64_t PadTo;
  std::unique_ptr<BinarySectionWriter> SecWriter;

  uint64_t TotalSize = 0;

public:
  ~BinaryWriter() {}
  Error finalize() override;
  Error write() override;
  BinaryWriter(Object &Obj, raw_ostream &Out, const CommonConfig &Config)
      : Writer(Obj, Out), GapFill(Config.GapFill), PadTo(Config.PadTo) {}
};

// A base class for writing ascii hex formats such as srec and ihex.
class ASCIIHexWriter : public Writer {
public:
  ASCIIHexWriter(Object &Obj, raw_ostream &OS, StringRef OutputFile)
      : Writer(Obj, OS), OutputFileName(OutputFile) {}
  Error finalize() override;

protected:
  StringRef OutputFileName;
  size_t TotalSize = 0;
  std::vector<const SectionBase *> Sections;

  Error checkSection(const SectionBase &S) const;
  virtual Expected<size_t>
  getTotalSize(WritableMemoryBuffer &EmptyBuffer) const = 0;
};

class IHexWriter : public ASCIIHexWriter {
public:
  Error write() override;
  IHexWriter(Object &Obj, raw_ostream &Out, StringRef OutputFile)
      : ASCIIHexWriter(Obj, Out, OutputFile) {}

private:
  uint64_t writeEntryPointRecord(uint8_t *Buf);
  uint64_t writeEndOfFileRecord(uint8_t *Buf);
  Expected<size_t>
  getTotalSize(WritableMemoryBuffer &EmptyBuffer) const override;
};

class SRECWriter : public ASCIIHexWriter {
public:
  SRECWriter(Object &Obj, raw_ostream &OS, StringRef OutputFile)
      : ASCIIHexWriter(Obj, OS, OutputFile) {}
  Error write() override;

private:
  size_t writeHeader(uint8_t *Buf);
  size_t writeTerminator(uint8_t *Buf, uint8_t Type);
  Expected<size_t>
  getTotalSize(WritableMemoryBuffer &EmptyBuffer) const override;
};

using SRecLineData = SmallVector<char, 64>;
struct SRecord {
  uint8_t Type;
  uint32_t Address;
  ArrayRef<uint8_t> Data;
  SRecLineData toString() const;
  uint8_t getCount() const;
  // Get address size in characters.
  uint8_t getAddressSize() const;
  uint8_t getChecksum() const;
  size_t getSize() const;
  static SRecord getHeader(StringRef FileName);
  static uint8_t getType(uint32_t Address);

  enum Type : uint8_t {
    // Vendor specific text comment.
    S0 = 0,
    // Data that starts at a 16 bit address.
    S1 = 1,
    // Data that starts at a 24 bit address.
    S2 = 2,
    // Data that starts at a 32 bit address.
    S3 = 3,
    // Reserved.
    S4 = 4,
    // 16 bit count of S1/S2/S3 records (optional).
    S5 = 5,
    // 32 bit count of S1/S2/S3 records (optional).
    S6 = 6,
    // Terminates a series of S3 records.
    S7 = 7,
    // Terminates a series of S2 records.
    S8 = 8,
    // Terminates a series of S1 records.
    S9 = 9
  };
};

class SRECSectionWriterBase : public BinarySectionWriter {
public:
  explicit SRECSectionWriterBase(WritableMemoryBuffer &Buf,
                                 uint64_t StartOffset)
      : BinarySectionWriter(Buf), Offset(StartOffset), HeaderSize(StartOffset) {
  }

  using BinarySectionWriter::visit;

  void writeRecords(uint32_t Entry);
  uint64_t getBufferOffset() const { return Offset; }
  Error visit(const Section &S) override;
  Error visit(const OwnedDataSection &S) override;
  Error visit(const StringTableSection &S) override;
  Error visit(const DynamicRelocationSection &S) override;
  uint8_t getType() const { return Type; };

protected:
  // Offset in the output buffer.
  uint64_t Offset;
  // Sections start after the header.
  uint64_t HeaderSize;
  // Type of records to write.
  uint8_t Type = SRecord::S1;
  std::vector<SRecord> Records;

  void writeSection(const SectionBase &S, ArrayRef<uint8_t> Data);
  virtual void writeRecord(SRecord &Record, uint64_t Off) = 0;
};

// An SRECSectionWriterBase that visits sections but does not write anything.
// This class is only used to calculate the size of the output file.
class SRECSizeCalculator : public SRECSectionWriterBase {
public:
  SRECSizeCalculator(WritableMemoryBuffer &EmptyBuffer, uint64_t Offset)
      : SRECSectionWriterBase(EmptyBuffer, Offset) {}

protected:
  void writeRecord(SRecord &Record, uint64_t Off) override {}
};

class SRECSectionWriter : public SRECSectionWriterBase {
public:
  SRECSectionWriter(WritableMemoryBuffer &Buf, uint64_t Offset)
      : SRECSectionWriterBase(Buf, Offset) {}
  Error visit(const StringTableSection &Sec) override;

protected:
  void writeRecord(SRecord &Record, uint64_t Off) override;
};

class SectionBase {
public:
  std::string Name;
  Segment *ParentSegment = nullptr;
  uint64_t HeaderOffset = 0;
  uint32_t Index = 0;

  uint32_t OriginalIndex = 0;
  uint64_t OriginalFlags = 0;
  uint64_t OriginalType = ELF::SHT_NULL;
  uint64_t OriginalOffset = std::numeric_limits<uint64_t>::max();

  uint64_t Addr = 0;
  uint64_t Align = 1;
  uint32_t EntrySize = 0;
  uint64_t Flags = 0;
  uint64_t Info = 0;
  uint64_t Link = ELF::SHN_UNDEF;
  uint64_t NameIndex = 0;
  uint64_t Offset = 0;
  uint64_t Size = 0;
  uint64_t Type = ELF::SHT_NULL;
  ArrayRef<uint8_t> OriginalData;
  bool HasSymbol = false;

  SectionBase() = default;
  SectionBase(const SectionBase &) = default;

  virtual ~SectionBase() = default;

  virtual Error initialize(SectionTableRef SecTable);
  virtual void finalize();
  // Remove references to these sections. The list of sections must be sorted.
  virtual Error
  removeSectionReferences(bool AllowBrokenLinks,
                          function_ref<bool(const SectionBase *)> ToRemove);
  virtual Error removeSymbols(function_ref<bool(const Symbol &)> ToRemove);
  virtual Error accept(SectionVisitor &Visitor) const = 0;
  virtual Error accept(MutableSectionVisitor &Visitor) = 0;
  virtual void markSymbols();
  virtual void
  replaceSectionReferences(const DenseMap<SectionBase *, SectionBase *> &);
  virtual bool hasContents() const { return false; }
  // Notify the section that it is subject to removal.
  virtual void onRemove();

  virtual void restoreSymTabLink(SymbolTableSection &) {}
};

class Segment {
private:
  struct SectionCompare {
    bool operator()(const SectionBase *Lhs, const SectionBase *Rhs) const {
      // Some sections might have the same address if one of them is empty. To
      // fix this we can use the lexicographic ordering on ->Addr and the
      // original index.
      if (Lhs->OriginalOffset == Rhs->OriginalOffset)
        return Lhs->OriginalIndex < Rhs->OriginalIndex;
      return Lhs->OriginalOffset < Rhs->OriginalOffset;
    }
  };

public:
  uint32_t Type = 0;
  uint32_t Flags = 0;
  uint64_t Offset = 0;
  uint64_t VAddr = 0;
  uint64_t PAddr = 0;
  uint64_t FileSize = 0;
  uint64_t MemSize = 0;
  uint64_t Align = 0;

  uint32_t Index = 0;
  uint64_t OriginalOffset = 0;
  Segment *ParentSegment = nullptr;
  ArrayRef<uint8_t> Contents;
  std::set<const SectionBase *, SectionCompare> Sections;

  explicit Segment(ArrayRef<uint8_t> Data) : Contents(Data) {}
  Segment() = default;

  const SectionBase *firstSection() const {
    if (!Sections.empty())
      return *Sections.begin();
    return nullptr;
  }

  void removeSection(const SectionBase *Sec) { Sections.erase(Sec); }
  void addSection(const SectionBase *Sec) { Sections.insert(Sec); }

  ArrayRef<uint8_t> getContents() const { return Contents; }
};

class Section : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  ArrayRef<uint8_t> Contents;
  SectionBase *LinkSection = nullptr;
  bool HasSymTabLink = false;

public:
  explicit Section(ArrayRef<uint8_t> Data) : Contents(Data) {}

  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  Error removeSectionReferences(
      bool AllowBrokenLinks,
      function_ref<bool(const SectionBase *)> ToRemove) override;
  Error initialize(SectionTableRef SecTable) override;
  void finalize() override;
  bool hasContents() const override {
    return Type != ELF::SHT_NOBITS && Type != ELF::SHT_NULL;
  }
  void restoreSymTabLink(SymbolTableSection &SymTab) override;
};

class OwnedDataSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  std::vector<uint8_t> Data;

public:
  OwnedDataSection(StringRef SecName, ArrayRef<uint8_t> Data)
      : Data(std::begin(Data), std::end(Data)) {
    Name = SecName.str();
    Type = OriginalType = ELF::SHT_PROGBITS;
    Size = Data.size();
    OriginalOffset = std::numeric_limits<uint64_t>::max();
  }

  OwnedDataSection(const Twine &SecName, uint64_t SecAddr, uint64_t SecFlags,
                   uint64_t SecOff) {
    Name = SecName.str();
    Type = OriginalType = ELF::SHT_PROGBITS;
    Addr = SecAddr;
    Flags = OriginalFlags = SecFlags;
    OriginalOffset = SecOff;
  }

  OwnedDataSection(SectionBase &S, ArrayRef<uint8_t> Data)
      : SectionBase(S), Data(std::begin(Data), std::end(Data)) {
    Size = Data.size();
  }

  void appendHexData(StringRef HexData);
  Error accept(SectionVisitor &Sec) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  bool hasContents() const override { return true; }
};

class CompressedSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  uint32_t ChType = 0;
  DebugCompressionType CompressionType;
  uint64_t DecompressedSize;
  uint64_t DecompressedAlign;
  SmallVector<uint8_t, 128> CompressedData;

public:
  CompressedSection(const SectionBase &Sec,
    DebugCompressionType CompressionType, bool Is64Bits);
  CompressedSection(ArrayRef<uint8_t> CompressedData, uint32_t ChType,
                    uint64_t DecompressedSize, uint64_t DecompressedAlign);

  uint64_t getDecompressedSize() const { return DecompressedSize; }
  uint64_t getDecompressedAlign() const { return DecompressedAlign; }
  uint64_t getChType() const { return ChType; }

  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;

  static bool classof(const SectionBase *S) {
    return S->OriginalFlags & ELF::SHF_COMPRESSED;
  }
};

class DecompressedSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

public:
  uint32_t ChType;
  explicit DecompressedSection(const CompressedSection &Sec)
      : SectionBase(Sec), ChType(Sec.getChType()) {
    Size = Sec.getDecompressedSize();
    Align = Sec.getDecompressedAlign();
    Flags = OriginalFlags = (Flags & ~ELF::SHF_COMPRESSED);
  }

  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
};

// There are two types of string tables that can exist, dynamic and not dynamic.
// In the dynamic case the string table is allocated. Changing a dynamic string
// table would mean altering virtual addresses and thus the memory image. So
// dynamic string tables should not have an interface to modify them or
// reconstruct them. This type lets us reconstruct a string table. To avoid
// this class being used for dynamic string tables (which has happened) the
// classof method checks that the particular instance is not allocated. This
// then agrees with the makeSection method used to construct most sections.
class StringTableSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  StringTableBuilder StrTabBuilder;

public:
  StringTableSection() : StrTabBuilder(StringTableBuilder::ELF) {
    Type = OriginalType = ELF::SHT_STRTAB;
  }

  void addString(StringRef Name);
  uint32_t findIndex(StringRef Name) const;
  void prepareForLayout();
  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;

  static bool classof(const SectionBase *S) {
    if (S->OriginalFlags & ELF::SHF_ALLOC)
      return false;
    return S->OriginalType == ELF::SHT_STRTAB;
  }
};

// Symbols have a st_shndx field that normally stores an index but occasionally
// stores a different special value. This enum keeps track of what the st_shndx
// field means. Most of the values are just copies of the special SHN_* values.
// SYMBOL_SIMPLE_INDEX means that the st_shndx is just an index of a section.
enum SymbolShndxType {
  SYMBOL_SIMPLE_INDEX = 0,
  SYMBOL_ABS = ELF::SHN_ABS,
  SYMBOL_COMMON = ELF::SHN_COMMON,
  SYMBOL_LOPROC = ELF::SHN_LOPROC,
  SYMBOL_AMDGPU_LDS = ELF::SHN_AMDGPU_LDS,
  SYMBOL_HEXAGON_SCOMMON = ELF::SHN_HEXAGON_SCOMMON,
  SYMBOL_HEXAGON_SCOMMON_2 = ELF::SHN_HEXAGON_SCOMMON_2,
  SYMBOL_HEXAGON_SCOMMON_4 = ELF::SHN_HEXAGON_SCOMMON_4,
  SYMBOL_HEXAGON_SCOMMON_8 = ELF::SHN_HEXAGON_SCOMMON_8,
  SYMBOL_MIPS_ACOMMON = ELF::SHN_MIPS_ACOMMON,
  SYMBOL_MIPS_TEXT = ELF::SHN_MIPS_TEXT,
  SYMBOL_MIPS_DATA = ELF::SHN_MIPS_DATA,
  SYMBOL_MIPS_SCOMMON = ELF::SHN_MIPS_SCOMMON,
  SYMBOL_MIPS_SUNDEFINED = ELF::SHN_MIPS_SUNDEFINED,
  SYMBOL_HIPROC = ELF::SHN_HIPROC,
  SYMBOL_LOOS = ELF::SHN_LOOS,
  SYMBOL_HIOS = ELF::SHN_HIOS,
  SYMBOL_XINDEX = ELF::SHN_XINDEX,
};

struct Symbol {
  uint8_t Binding;
  SectionBase *DefinedIn = nullptr;
  SymbolShndxType ShndxType;
  uint32_t Index;
  std::string Name;
  uint32_t NameIndex;
  uint64_t Size;
  uint8_t Type;
  uint64_t Value;
  uint8_t Visibility;
  bool Referenced = false;

  uint16_t getShndx() const;
  bool isCommon() const;
};

class SectionIndexSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

private:
  std::vector<uint32_t> Indexes;
  SymbolTableSection *Symbols = nullptr;

public:
  virtual ~SectionIndexSection() {}
  void addIndex(uint32_t Index) {
    assert(Size > 0);
    Indexes.push_back(Index);
  }

  void reserve(size_t NumSymbols) {
    Indexes.reserve(NumSymbols);
    Size = NumSymbols * 4;
  }
  void setSymTab(SymbolTableSection *SymTab) { Symbols = SymTab; }
  Error initialize(SectionTableRef SecTable) override;
  void finalize() override;
  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;

  SectionIndexSection() {
    Name = ".symtab_shndx";
    Align = 4;
    EntrySize = 4;
    Type = OriginalType = ELF::SHT_SYMTAB_SHNDX;
  }
};

class SymbolTableSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

  void setStrTab(StringTableSection *StrTab) { SymbolNames = StrTab; }
  void assignIndices();

protected:
  std::vector<std::unique_ptr<Symbol>> Symbols;
  StringTableSection *SymbolNames = nullptr;
  SectionIndexSection *SectionIndexTable = nullptr;
  bool IndicesChanged = false;

  using SymPtr = std::unique_ptr<Symbol>;

public:
  SymbolTableSection() { Type = OriginalType = ELF::SHT_SYMTAB; }

  void addSymbol(Twine Name, uint8_t Bind, uint8_t Type, SectionBase *DefinedIn,
                 uint64_t Value, uint8_t Visibility, uint16_t Shndx,
                 uint64_t SymbolSize);
  void prepareForLayout();
  // An 'empty' symbol table still contains a null symbol.
  bool empty() const { return Symbols.size() == 1; }
  bool indicesChanged() const { return IndicesChanged; }
  void setShndxTable(SectionIndexSection *ShndxTable) {
    SectionIndexTable = ShndxTable;
  }
  const SectionIndexSection *getShndxTable() const { return SectionIndexTable; }
  void fillShndxTable();
  const SectionBase *getStrTab() const { return SymbolNames; }
  Expected<const Symbol *> getSymbolByIndex(uint32_t Index) const;
  Expected<Symbol *> getSymbolByIndex(uint32_t Index);
  void updateSymbols(function_ref<void(Symbol &)> Callable);

  Error removeSectionReferences(
      bool AllowBrokenLinks,
      function_ref<bool(const SectionBase *)> ToRemove) override;
  Error initialize(SectionTableRef SecTable) override;
  void finalize() override;
  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  Error removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;
  void replaceSectionReferences(
      const DenseMap<SectionBase *, SectionBase *> &FromTo) override;

  static bool classof(const SectionBase *S) {
    return S->OriginalType == ELF::SHT_SYMTAB;
  }
};

struct Relocation {
  Symbol *RelocSymbol = nullptr;
  uint64_t Offset;
  uint64_t Addend;
  uint32_t Type;
};

// All relocation sections denote relocations to apply to another section.
// However, some relocation sections use a dynamic symbol table and others use
// a regular symbol table. Because the types of the two symbol tables differ in
// our system (because they should behave differently) we can't uniformly
// represent all relocations with the same base class if we expose an interface
// that mentions the symbol table type. So we split the two base types into two
// different classes, one which handles the section the relocation is applied to
// and another which handles the symbol table type. The symbol table type is
// taken as a type parameter to the class (see RelocSectionWithSymtabBase).
class RelocationSectionBase : public SectionBase {
protected:
  SectionBase *SecToApplyRel = nullptr;

public:
  const SectionBase *getSection() const { return SecToApplyRel; }
  void setSection(SectionBase *Sec) { SecToApplyRel = Sec; }

  StringRef getNamePrefix() const;

  static bool classof(const SectionBase *S) {
    return is_contained({ELF::SHT_REL, ELF::SHT_RELA, ELF::SHT_CREL},
                        S->OriginalType);
  }
};

// Takes the symbol table type to use as a parameter so that we can deduplicate
// that code between the two symbol table types.
template <class SymTabType>
class RelocSectionWithSymtabBase : public RelocationSectionBase {
  void setSymTab(SymTabType *SymTab) { Symbols = SymTab; }

protected:
  RelocSectionWithSymtabBase() = default;

  SymTabType *Symbols = nullptr;

public:
  Error initialize(SectionTableRef SecTable) override;
  void finalize() override;
};

class RelocationSection
    : public RelocSectionWithSymtabBase<SymbolTableSection> {
  MAKE_SEC_WRITER_FRIEND

  std::vector<Relocation> Relocations;
  const Object &Obj;

public:
  RelocationSection(const Object &O) : Obj(O) {}
  void addRelocation(const Relocation &Rel) { Relocations.push_back(Rel); }
  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  Error removeSectionReferences(
      bool AllowBrokenLinks,
      function_ref<bool(const SectionBase *)> ToRemove) override;
  Error removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;
  void markSymbols() override;
  void replaceSectionReferences(
      const DenseMap<SectionBase *, SectionBase *> &FromTo) override;
  const Object &getObject() const { return Obj; }

  static bool classof(const SectionBase *S) {
    if (S->OriginalFlags & ELF::SHF_ALLOC)
      return false;
    return RelocationSectionBase::classof(S);
  }
};

// TODO: The way stripping and groups interact is complicated
// and still needs to be worked on.

class GroupSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND
  const SymbolTableSection *SymTab = nullptr;
  Symbol *Sym = nullptr;
  ELF::Elf32_Word FlagWord;
  SmallVector<SectionBase *, 3> GroupMembers;

public:
  template <class T>
  using ConstRange = iterator_range<
      pointee_iterator<typename llvm::SmallVector<T *, 3>::const_iterator>>;
  // TODO: Contents is present in several classes of the hierarchy.
  // This needs to be refactored to avoid duplication.
  ArrayRef<uint8_t> Contents;

  explicit GroupSection(ArrayRef<uint8_t> Data) : Contents(Data) {}

  void setSymTab(const SymbolTableSection *SymTabSec) { SymTab = SymTabSec; }
  void setSymbol(Symbol *S) { Sym = S; }
  void setFlagWord(ELF::Elf32_Word W) { FlagWord = W; }
  void addMember(SectionBase *Sec) { GroupMembers.push_back(Sec); }

  Error accept(SectionVisitor &) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  void finalize() override;
  Error removeSectionReferences(
      bool AllowBrokenLinks,
      function_ref<bool(const SectionBase *)> ToRemove) override;
  Error removeSymbols(function_ref<bool(const Symbol &)> ToRemove) override;
  void markSymbols() override;
  void replaceSectionReferences(
      const DenseMap<SectionBase *, SectionBase *> &FromTo) override;
  void onRemove() override;

  ConstRange<SectionBase> members() const {
    return make_pointee_range(GroupMembers);
  }

  static bool classof(const SectionBase *S) {
    return S->OriginalType == ELF::SHT_GROUP;
  }
};

class DynamicSymbolTableSection : public Section {
public:
  explicit DynamicSymbolTableSection(ArrayRef<uint8_t> Data) : Section(Data) {}

  static bool classof(const SectionBase *S) {
    return S->OriginalType == ELF::SHT_DYNSYM;
  }
};

class DynamicSection : public Section {
public:
  explicit DynamicSection(ArrayRef<uint8_t> Data) : Section(Data) {}

  static bool classof(const SectionBase *S) {
    return S->OriginalType == ELF::SHT_DYNAMIC;
  }
};

class DynamicRelocationSection
    : public RelocSectionWithSymtabBase<DynamicSymbolTableSection> {
  MAKE_SEC_WRITER_FRIEND

private:
  ArrayRef<uint8_t> Contents;

public:
  explicit DynamicRelocationSection(ArrayRef<uint8_t> Data) : Contents(Data) {}

  Error accept(SectionVisitor &) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
  Error removeSectionReferences(
      bool AllowBrokenLinks,
      function_ref<bool(const SectionBase *)> ToRemove) override;

  static bool classof(const SectionBase *S) {
    if (!(S->OriginalFlags & ELF::SHF_ALLOC))
      return false;
    return S->OriginalType == ELF::SHT_REL || S->OriginalType == ELF::SHT_RELA;
  }
};

class GnuDebugLinkSection : public SectionBase {
  MAKE_SEC_WRITER_FRIEND

private:
  StringRef FileName;
  uint32_t CRC32;

  void init(StringRef File);

public:
  // If we add this section from an external source we can use this ctor.
  explicit GnuDebugLinkSection(StringRef File, uint32_t PrecomputedCRC);
  Error accept(SectionVisitor &Visitor) const override;
  Error accept(MutableSectionVisitor &Visitor) override;
};

class Reader {
public:
  virtual ~Reader();
  virtual Expected<std::unique_ptr<Object>> create(bool EnsureSymtab) const = 0;
};

using object::Binary;
using object::ELFFile;
using object::ELFObjectFile;
using object::OwningBinary;

class BasicELFBuilder {
protected:
  std::unique_ptr<Object> Obj;

  void initFileHeader();
  void initHeaderSegment();
  StringTableSection *addStrTab();
  SymbolTableSection *addSymTab(StringTableSection *StrTab);
  Error initSections();

public:
  BasicELFBuilder() : Obj(std::make_unique<Object>()) {}
};

class BinaryELFBuilder : public BasicELFBuilder {
  MemoryBuffer *MemBuf;
  uint8_t NewSymbolVisibility;
  void addData(SymbolTableSection *SymTab);

public:
  BinaryELFBuilder(MemoryBuffer *MB, uint8_t NewSymbolVisibility)
      : MemBuf(MB), NewSymbolVisibility(NewSymbolVisibility) {}

  Expected<std::unique_ptr<Object>> build();
};

class IHexELFBuilder : public BasicELFBuilder {
  const std::vector<IHexRecord> &Records;

  void addDataSections();

public:
  IHexELFBuilder(const std::vector<IHexRecord> &Records) : Records(Records) {}

  Expected<std::unique_ptr<Object>> build();
};

template <class ELFT> class ELFBuilder {
private:
  using Elf_Addr = typename ELFT::Addr;
  using Elf_Shdr = typename ELFT::Shdr;
  using Elf_Word = typename ELFT::Word;

  const ELFFile<ELFT> &ElfFile;
  Object &Obj;
  size_t EhdrOffset = 0;
  std::optional<StringRef> ExtractPartition;

  void setParentSegment(Segment &Child);
  Error readProgramHeaders(const ELFFile<ELFT> &HeadersFile);
  Error initGroupSection(GroupSection *GroupSec);
  Error initSymbolTable(SymbolTableSection *SymTab);
  Error readSectionHeaders();
  Error readSections(bool EnsureSymtab);
  Error findEhdrOffset();
  Expected<SectionBase &> makeSection(const Elf_Shdr &Shdr);

public:
  ELFBuilder(const ELFObjectFile<ELFT> &ElfObj, Object &Obj,
             std::optional<StringRef> ExtractPartition);

  Error build(bool EnsureSymtab);
};

class BinaryReader : public Reader {
  MemoryBuffer *MemBuf;
  uint8_t NewSymbolVisibility;

public:
  BinaryReader(MemoryBuffer *MB, const uint8_t NewSymbolVisibility)
      : MemBuf(MB), NewSymbolVisibility(NewSymbolVisibility) {}
  Expected<std::unique_ptr<Object>> create(bool EnsureSymtab) const override;
};

class IHexReader : public Reader {
  MemoryBuffer *MemBuf;

  Expected<std::vector<IHexRecord>> parse() const;
  Error parseError(size_t LineNo, Error E) const {
    return LineNo == -1U
               ? createFileError(MemBuf->getBufferIdentifier(), std::move(E))
               : createFileError(MemBuf->getBufferIdentifier(), LineNo,
                                 std::move(E));
  }
  template <typename... Ts>
  Error parseError(size_t LineNo, char const *Fmt, const Ts &...Vals) const {
    Error E = createStringError(errc::invalid_argument, Fmt, Vals...);
    return parseError(LineNo, std::move(E));
  }

public:
  IHexReader(MemoryBuffer *MB) : MemBuf(MB) {}

  Expected<std::unique_ptr<Object>> create(bool EnsureSymtab) const override;
};

class ELFReader : public Reader {
  Binary *Bin;
  std::optional<StringRef> ExtractPartition;

public:
  Expected<std::unique_ptr<Object>> create(bool EnsureSymtab) const override;
  explicit ELFReader(Binary *B, std::optional<StringRef> ExtractPartition)
      : Bin(B), ExtractPartition(ExtractPartition) {}
};

class Object {
private:
  using SecPtr = std::unique_ptr<SectionBase>;
  using SegPtr = std::unique_ptr<Segment>;

  std::vector<SecPtr> Sections;
  std::vector<SegPtr> Segments;
  std::vector<SecPtr> RemovedSections;
  DenseMap<SectionBase *, std::vector<uint8_t>> UpdatedSections;

  static bool sectionIsAlloc(const SectionBase &Sec) {
    return Sec.Flags & ELF::SHF_ALLOC;
  };

public:
  template <class T>
  using ConstRange = iterator_range<pointee_iterator<
      typename std::vector<std::unique_ptr<T>>::const_iterator>>;

  // It is often the case that the ELF header and the program header table are
  // not present in any segment. This could be a problem during file layout,
  // because other segments may get assigned an offset where either of the
  // two should reside, which will effectively corrupt the resulting binary.
  // Other than that we use these segments to track program header offsets
  // when they may not follow the ELF header.
  Segment ElfHdrSegment;
  Segment ProgramHdrSegment;

  bool Is64Bits;
  uint8_t OSABI;
  uint8_t ABIVersion;
  uint64_t Entry;
  uint64_t SHOff;
  uint32_t Type;
  uint32_t Machine;
  uint32_t Version;
  uint32_t Flags;

  bool HadShdrs = true;
  bool MustBeRelocatable = false;
  StringTableSection *SectionNames = nullptr;
  SymbolTableSection *SymbolTable = nullptr;
  SectionIndexSection *SectionIndexTable = nullptr;

  bool IsMips64EL = false;

  SectionTableRef sections() const { return SectionTableRef(Sections); }
  iterator_range<
      filter_iterator<pointee_iterator<std::vector<SecPtr>::const_iterator>,
                      decltype(&sectionIsAlloc)>>
  allocSections() const {
    return make_filter_range(make_pointee_range(Sections), sectionIsAlloc);
  }

  const auto &getUpdatedSections() const { return UpdatedSections; }
  Error updateSection(StringRef Name, ArrayRef<uint8_t> Data);

  SectionBase *findSection(StringRef Name) {
    auto SecIt =
        find_if(Sections, [&](const SecPtr &Sec) { return Sec->Name == Name; });
    return SecIt == Sections.end() ? nullptr : SecIt->get();
  }
  SectionTableRef removedSections() { return SectionTableRef(RemovedSections); }

  ConstRange<Segment> segments() const { return make_pointee_range(Segments); }

  Error removeSections(bool AllowBrokenLinks,
                       std::function<bool(const SectionBase &)> ToRemove);
  Error compressOrDecompressSections(const CommonConfig &Config);
  Error replaceSections(const DenseMap<SectionBase *, SectionBase *> &FromTo);
  Error removeSymbols(function_ref<bool(const Symbol &)> ToRemove);
  template <class T, class... Ts> T &addSection(Ts &&...Args) {
    auto Sec = std::make_unique<T>(std::forward<Ts>(Args)...);
    auto Ptr = Sec.get();
    MustBeRelocatable |= isa<RelocationSection>(*Ptr);
    Sections.emplace_back(std::move(Sec));
    Ptr->Index = Sections.size();
    return *Ptr;
  }
  Error addNewSymbolTable();
  Segment &addSegment(ArrayRef<uint8_t> Data) {
    Segments.emplace_back(std::make_unique<Segment>(Data));
    return *Segments.back();
  }
  bool isRelocatable() const {
    return (Type != ELF::ET_DYN && Type != ELF::ET_EXEC) || MustBeRelocatable;
  }
};

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm

#endif // LLVM_LIB_OBJCOPY_ELF_ELFOBJECT_H
