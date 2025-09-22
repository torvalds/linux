//===- ELFObject.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ELFObject.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFExtras.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Object/ELF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/Path.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::ELF;
using namespace llvm::objcopy::elf;
using namespace llvm::object;
using namespace llvm::support;

template <class ELFT> void ELFWriter<ELFT>::writePhdr(const Segment &Seg) {
  uint8_t *B = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
               Obj.ProgramHdrSegment.Offset + Seg.Index * sizeof(Elf_Phdr);
  Elf_Phdr &Phdr = *reinterpret_cast<Elf_Phdr *>(B);
  Phdr.p_type = Seg.Type;
  Phdr.p_flags = Seg.Flags;
  Phdr.p_offset = Seg.Offset;
  Phdr.p_vaddr = Seg.VAddr;
  Phdr.p_paddr = Seg.PAddr;
  Phdr.p_filesz = Seg.FileSize;
  Phdr.p_memsz = Seg.MemSize;
  Phdr.p_align = Seg.Align;
}

Error SectionBase::removeSectionReferences(
    bool, function_ref<bool(const SectionBase *)>) {
  return Error::success();
}

Error SectionBase::removeSymbols(function_ref<bool(const Symbol &)>) {
  return Error::success();
}

Error SectionBase::initialize(SectionTableRef) { return Error::success(); }
void SectionBase::finalize() {}
void SectionBase::markSymbols() {}
void SectionBase::replaceSectionReferences(
    const DenseMap<SectionBase *, SectionBase *> &) {}
void SectionBase::onRemove() {}

template <class ELFT> void ELFWriter<ELFT>::writeShdr(const SectionBase &Sec) {
  uint8_t *B =
      reinterpret_cast<uint8_t *>(Buf->getBufferStart()) + Sec.HeaderOffset;
  Elf_Shdr &Shdr = *reinterpret_cast<Elf_Shdr *>(B);
  Shdr.sh_name = Sec.NameIndex;
  Shdr.sh_type = Sec.Type;
  Shdr.sh_flags = Sec.Flags;
  Shdr.sh_addr = Sec.Addr;
  Shdr.sh_offset = Sec.Offset;
  Shdr.sh_size = Sec.Size;
  Shdr.sh_link = Sec.Link;
  Shdr.sh_info = Sec.Info;
  Shdr.sh_addralign = Sec.Align;
  Shdr.sh_entsize = Sec.EntrySize;
}

template <class ELFT> Error ELFSectionSizer<ELFT>::visit(Section &) {
  return Error::success();
}

template <class ELFT> Error ELFSectionSizer<ELFT>::visit(OwnedDataSection &) {
  return Error::success();
}

template <class ELFT> Error ELFSectionSizer<ELFT>::visit(StringTableSection &) {
  return Error::success();
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(DynamicRelocationSection &) {
  return Error::success();
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(SymbolTableSection &Sec) {
  Sec.EntrySize = sizeof(Elf_Sym);
  Sec.Size = Sec.Symbols.size() * Sec.EntrySize;
  // Align to the largest field in Elf_Sym.
  Sec.Align = ELFT::Is64Bits ? sizeof(Elf_Xword) : sizeof(Elf_Word);
  return Error::success();
}

template <bool Is64>
static SmallVector<char, 0> encodeCrel(ArrayRef<Relocation> Relocations) {
  using uint = std::conditional_t<Is64, uint64_t, uint32_t>;
  SmallVector<char, 0> Content;
  raw_svector_ostream OS(Content);
  ELF::encodeCrel<Is64>(OS, Relocations, [&](const Relocation &R) {
    uint32_t CurSymIdx = R.RelocSymbol ? R.RelocSymbol->Index : 0;
    return ELF::Elf_Crel<Is64>{static_cast<uint>(R.Offset), CurSymIdx, R.Type,
                               std::make_signed_t<uint>(R.Addend)};
  });
  return Content;
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(RelocationSection &Sec) {
  if (Sec.Type == SHT_CREL) {
    Sec.Size = encodeCrel<ELFT::Is64Bits>(Sec.Relocations).size();
  } else {
    Sec.EntrySize = Sec.Type == SHT_REL ? sizeof(Elf_Rel) : sizeof(Elf_Rela);
    Sec.Size = Sec.Relocations.size() * Sec.EntrySize;
    // Align to the largest field in Elf_Rel(a).
    Sec.Align = ELFT::Is64Bits ? sizeof(Elf_Xword) : sizeof(Elf_Word);
  }
  return Error::success();
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(GnuDebugLinkSection &) {
  return Error::success();
}

template <class ELFT> Error ELFSectionSizer<ELFT>::visit(GroupSection &Sec) {
  Sec.Size = sizeof(Elf_Word) + Sec.GroupMembers.size() * sizeof(Elf_Word);
  return Error::success();
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(SectionIndexSection &) {
  return Error::success();
}

template <class ELFT> Error ELFSectionSizer<ELFT>::visit(CompressedSection &) {
  return Error::success();
}

template <class ELFT>
Error ELFSectionSizer<ELFT>::visit(DecompressedSection &) {
  return Error::success();
}

Error BinarySectionWriter::visit(const SectionIndexSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write symbol section index table '" +
                               Sec.Name + "' ");
}

Error BinarySectionWriter::visit(const SymbolTableSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write symbol table '" + Sec.Name +
                               "' out to binary");
}

Error BinarySectionWriter::visit(const RelocationSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write relocation section '" + Sec.Name +
                               "' out to binary");
}

Error BinarySectionWriter::visit(const GnuDebugLinkSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write '" + Sec.Name + "' out to binary");
}

Error BinarySectionWriter::visit(const GroupSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write '" + Sec.Name + "' out to binary");
}

Error SectionWriter::visit(const Section &Sec) {
  if (Sec.Type != SHT_NOBITS)
    llvm::copy(Sec.Contents, Out.getBufferStart() + Sec.Offset);

  return Error::success();
}

static bool addressOverflows32bit(uint64_t Addr) {
  // Sign extended 32 bit addresses (e.g 0xFFFFFFFF80000000) are ok
  return Addr > UINT32_MAX && Addr + 0x80000000 > UINT32_MAX;
}

template <class T> static T checkedGetHex(StringRef S) {
  T Value;
  bool Fail = S.getAsInteger(16, Value);
  assert(!Fail);
  (void)Fail;
  return Value;
}

// Fills exactly Len bytes of buffer with hexadecimal characters
// representing value 'X'
template <class T, class Iterator>
static Iterator toHexStr(T X, Iterator It, size_t Len) {
  // Fill range with '0'
  std::fill(It, It + Len, '0');

  for (long I = Len - 1; I >= 0; --I) {
    unsigned char Mod = static_cast<unsigned char>(X) & 15;
    *(It + I) = hexdigit(Mod, false);
    X >>= 4;
  }
  assert(X == 0);
  return It + Len;
}

uint8_t IHexRecord::getChecksum(StringRef S) {
  assert((S.size() & 1) == 0);
  uint8_t Checksum = 0;
  while (!S.empty()) {
    Checksum += checkedGetHex<uint8_t>(S.take_front(2));
    S = S.drop_front(2);
  }
  return -Checksum;
}

IHexLineData IHexRecord::getLine(uint8_t Type, uint16_t Addr,
                                 ArrayRef<uint8_t> Data) {
  IHexLineData Line(getLineLength(Data.size()));
  assert(Line.size());
  auto Iter = Line.begin();
  *Iter++ = ':';
  Iter = toHexStr(Data.size(), Iter, 2);
  Iter = toHexStr(Addr, Iter, 4);
  Iter = toHexStr(Type, Iter, 2);
  for (uint8_t X : Data)
    Iter = toHexStr(X, Iter, 2);
  StringRef S(Line.data() + 1, std::distance(Line.begin() + 1, Iter));
  Iter = toHexStr(getChecksum(S), Iter, 2);
  *Iter++ = '\r';
  *Iter++ = '\n';
  assert(Iter == Line.end());
  return Line;
}

static Error checkRecord(const IHexRecord &R) {
  switch (R.Type) {
  case IHexRecord::Data:
    if (R.HexData.size() == 0)
      return createStringError(
          errc::invalid_argument,
          "zero data length is not allowed for data records");
    break;
  case IHexRecord::EndOfFile:
    break;
  case IHexRecord::SegmentAddr:
    // 20-bit segment address. Data length must be 2 bytes
    // (4 bytes in hex)
    if (R.HexData.size() != 4)
      return createStringError(
          errc::invalid_argument,
          "segment address data should be 2 bytes in size");
    break;
  case IHexRecord::StartAddr80x86:
  case IHexRecord::StartAddr:
    if (R.HexData.size() != 8)
      return createStringError(errc::invalid_argument,
                               "start address data should be 4 bytes in size");
    // According to Intel HEX specification '03' record
    // only specifies the code address within the 20-bit
    // segmented address space of the 8086/80186. This
    // means 12 high order bits should be zeroes.
    if (R.Type == IHexRecord::StartAddr80x86 &&
        R.HexData.take_front(3) != "000")
      return createStringError(errc::invalid_argument,
                               "start address exceeds 20 bit for 80x86");
    break;
  case IHexRecord::ExtendedAddr:
    // 16-31 bits of linear base address
    if (R.HexData.size() != 4)
      return createStringError(
          errc::invalid_argument,
          "extended address data should be 2 bytes in size");
    break;
  default:
    // Unknown record type
    return createStringError(errc::invalid_argument, "unknown record type: %u",
                             static_cast<unsigned>(R.Type));
  }
  return Error::success();
}

// Checks that IHEX line contains valid characters.
// This allows converting hexadecimal data to integers
// without extra verification.
static Error checkChars(StringRef Line) {
  assert(!Line.empty());
  if (Line[0] != ':')
    return createStringError(errc::invalid_argument,
                             "missing ':' in the beginning of line.");

  for (size_t Pos = 1; Pos < Line.size(); ++Pos)
    if (hexDigitValue(Line[Pos]) == -1U)
      return createStringError(errc::invalid_argument,
                               "invalid character at position %zu.", Pos + 1);
  return Error::success();
}

Expected<IHexRecord> IHexRecord::parse(StringRef Line) {
  assert(!Line.empty());

  // ':' + Length + Address + Type + Checksum with empty data ':LLAAAATTCC'
  if (Line.size() < 11)
    return createStringError(errc::invalid_argument,
                             "line is too short: %zu chars.", Line.size());

  if (Error E = checkChars(Line))
    return std::move(E);

  IHexRecord Rec;
  size_t DataLen = checkedGetHex<uint8_t>(Line.substr(1, 2));
  if (Line.size() != getLength(DataLen))
    return createStringError(errc::invalid_argument,
                             "invalid line length %zu (should be %zu)",
                             Line.size(), getLength(DataLen));

  Rec.Addr = checkedGetHex<uint16_t>(Line.substr(3, 4));
  Rec.Type = checkedGetHex<uint8_t>(Line.substr(7, 2));
  Rec.HexData = Line.substr(9, DataLen * 2);

  if (getChecksum(Line.drop_front(1)) != 0)
    return createStringError(errc::invalid_argument, "incorrect checksum.");
  if (Error E = checkRecord(Rec))
    return std::move(E);
  return Rec;
}

static uint64_t sectionPhysicalAddr(const SectionBase *Sec) {
  Segment *Seg = Sec->ParentSegment;
  if (Seg && Seg->Type != ELF::PT_LOAD)
    Seg = nullptr;
  return Seg ? Seg->PAddr + Sec->OriginalOffset - Seg->OriginalOffset
             : Sec->Addr;
}

void IHexSectionWriterBase::writeSection(const SectionBase *Sec,
                                         ArrayRef<uint8_t> Data) {
  assert(Data.size() == Sec->Size);
  const uint32_t ChunkSize = 16;
  uint32_t Addr = sectionPhysicalAddr(Sec) & 0xFFFFFFFFU;
  while (!Data.empty()) {
    uint64_t DataSize = std::min<uint64_t>(Data.size(), ChunkSize);
    if (Addr > SegmentAddr + BaseAddr + 0xFFFFU) {
      if (Addr > 0xFFFFFU) {
        // Write extended address record, zeroing segment address
        // if needed.
        if (SegmentAddr != 0)
          SegmentAddr = writeSegmentAddr(0U);
        BaseAddr = writeBaseAddr(Addr);
      } else {
        // We can still remain 16-bit
        SegmentAddr = writeSegmentAddr(Addr);
      }
    }
    uint64_t SegOffset = Addr - BaseAddr - SegmentAddr;
    assert(SegOffset <= 0xFFFFU);
    DataSize = std::min(DataSize, 0x10000U - SegOffset);
    writeData(0, SegOffset, Data.take_front(DataSize));
    Addr += DataSize;
    Data = Data.drop_front(DataSize);
  }
}

uint64_t IHexSectionWriterBase::writeSegmentAddr(uint64_t Addr) {
  assert(Addr <= 0xFFFFFU);
  uint8_t Data[] = {static_cast<uint8_t>((Addr & 0xF0000U) >> 12), 0};
  writeData(2, 0, Data);
  return Addr & 0xF0000U;
}

uint64_t IHexSectionWriterBase::writeBaseAddr(uint64_t Addr) {
  assert(Addr <= 0xFFFFFFFFU);
  uint64_t Base = Addr & 0xFFFF0000U;
  uint8_t Data[] = {static_cast<uint8_t>(Base >> 24),
                    static_cast<uint8_t>((Base >> 16) & 0xFF)};
  writeData(4, 0, Data);
  return Base;
}

void IHexSectionWriterBase::writeData(uint8_t, uint16_t,
                                      ArrayRef<uint8_t> Data) {
  Offset += IHexRecord::getLineLength(Data.size());
}

Error IHexSectionWriterBase::visit(const Section &Sec) {
  writeSection(&Sec, Sec.Contents);
  return Error::success();
}

Error IHexSectionWriterBase::visit(const OwnedDataSection &Sec) {
  writeSection(&Sec, Sec.Data);
  return Error::success();
}

Error IHexSectionWriterBase::visit(const StringTableSection &Sec) {
  // Check that sizer has already done its work
  assert(Sec.Size == Sec.StrTabBuilder.getSize());
  // We are free to pass an invalid pointer to writeSection as long
  // as we don't actually write any data. The real writer class has
  // to override this method .
  writeSection(&Sec, {nullptr, static_cast<size_t>(Sec.Size)});
  return Error::success();
}

Error IHexSectionWriterBase::visit(const DynamicRelocationSection &Sec) {
  writeSection(&Sec, Sec.Contents);
  return Error::success();
}

void IHexSectionWriter::writeData(uint8_t Type, uint16_t Addr,
                                  ArrayRef<uint8_t> Data) {
  IHexLineData HexData = IHexRecord::getLine(Type, Addr, Data);
  memcpy(Out.getBufferStart() + Offset, HexData.data(), HexData.size());
  Offset += HexData.size();
}

Error IHexSectionWriter::visit(const StringTableSection &Sec) {
  assert(Sec.Size == Sec.StrTabBuilder.getSize());
  std::vector<uint8_t> Data(Sec.Size);
  Sec.StrTabBuilder.write(Data.data());
  writeSection(&Sec, Data);
  return Error::success();
}

Error Section::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error Section::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

void Section::restoreSymTabLink(SymbolTableSection &SymTab) {
  if (HasSymTabLink) {
    assert(LinkSection == nullptr);
    LinkSection = &SymTab;
  }
}

Error SectionWriter::visit(const OwnedDataSection &Sec) {
  llvm::copy(Sec.Data, Out.getBufferStart() + Sec.Offset);
  return Error::success();
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const DecompressedSection &Sec) {
  ArrayRef<uint8_t> Compressed =
      Sec.OriginalData.slice(sizeof(Elf_Chdr_Impl<ELFT>));
  SmallVector<uint8_t, 128> Decompressed;
  DebugCompressionType Type;
  switch (Sec.ChType) {
  case ELFCOMPRESS_ZLIB:
    Type = DebugCompressionType::Zlib;
    break;
  case ELFCOMPRESS_ZSTD:
    Type = DebugCompressionType::Zstd;
    break;
  default:
    return createStringError(errc::invalid_argument,
                             "--decompress-debug-sections: ch_type (" +
                                 Twine(Sec.ChType) + ") of section '" +
                                 Sec.Name + "' is unsupported");
  }
  if (auto *Reason =
          compression::getReasonIfUnsupported(compression::formatFor(Type)))
    return createStringError(errc::invalid_argument,
                             "failed to decompress section '" + Sec.Name +
                                 "': " + Reason);
  if (Error E = compression::decompress(Type, Compressed, Decompressed,
                                        static_cast<size_t>(Sec.Size)))
    return createStringError(errc::invalid_argument,
                             "failed to decompress section '" + Sec.Name +
                                 "': " + toString(std::move(E)));

  uint8_t *Buf = reinterpret_cast<uint8_t *>(Out.getBufferStart()) + Sec.Offset;
  std::copy(Decompressed.begin(), Decompressed.end(), Buf);

  return Error::success();
}

Error BinarySectionWriter::visit(const DecompressedSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write compressed section '" + Sec.Name +
                               "' ");
}

Error DecompressedSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error DecompressedSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

Error OwnedDataSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error OwnedDataSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

void OwnedDataSection::appendHexData(StringRef HexData) {
  assert((HexData.size() & 1) == 0);
  while (!HexData.empty()) {
    Data.push_back(checkedGetHex<uint8_t>(HexData.take_front(2)));
    HexData = HexData.drop_front(2);
  }
  Size = Data.size();
}

Error BinarySectionWriter::visit(const CompressedSection &Sec) {
  return createStringError(errc::operation_not_permitted,
                           "cannot write compressed section '" + Sec.Name +
                               "' ");
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const CompressedSection &Sec) {
  uint8_t *Buf = reinterpret_cast<uint8_t *>(Out.getBufferStart()) + Sec.Offset;
  Elf_Chdr_Impl<ELFT> Chdr = {};
  switch (Sec.CompressionType) {
  case DebugCompressionType::None:
    std::copy(Sec.OriginalData.begin(), Sec.OriginalData.end(), Buf);
    return Error::success();
  case DebugCompressionType::Zlib:
    Chdr.ch_type = ELF::ELFCOMPRESS_ZLIB;
    break;
  case DebugCompressionType::Zstd:
    Chdr.ch_type = ELF::ELFCOMPRESS_ZSTD;
    break;
  }
  Chdr.ch_size = Sec.DecompressedSize;
  Chdr.ch_addralign = Sec.DecompressedAlign;
  memcpy(Buf, &Chdr, sizeof(Chdr));
  Buf += sizeof(Chdr);

  std::copy(Sec.CompressedData.begin(), Sec.CompressedData.end(), Buf);
  return Error::success();
}

CompressedSection::CompressedSection(const SectionBase &Sec,
                                     DebugCompressionType CompressionType,
                                     bool Is64Bits)
    : SectionBase(Sec), CompressionType(CompressionType),
      DecompressedSize(Sec.OriginalData.size()), DecompressedAlign(Sec.Align) {
  compression::compress(compression::Params(CompressionType), OriginalData,
                        CompressedData);

  Flags |= ELF::SHF_COMPRESSED;
  OriginalFlags |= ELF::SHF_COMPRESSED;
  size_t ChdrSize = Is64Bits ? sizeof(object::Elf_Chdr_Impl<object::ELF64LE>)
                             : sizeof(object::Elf_Chdr_Impl<object::ELF32LE>);
  Size = ChdrSize + CompressedData.size();
  Align = 8;
}

CompressedSection::CompressedSection(ArrayRef<uint8_t> CompressedData,
                                     uint32_t ChType, uint64_t DecompressedSize,
                                     uint64_t DecompressedAlign)
    : ChType(ChType), CompressionType(DebugCompressionType::None),
      DecompressedSize(DecompressedSize), DecompressedAlign(DecompressedAlign) {
  OriginalData = CompressedData;
}

Error CompressedSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error CompressedSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

void StringTableSection::addString(StringRef Name) { StrTabBuilder.add(Name); }

uint32_t StringTableSection::findIndex(StringRef Name) const {
  return StrTabBuilder.getOffset(Name);
}

void StringTableSection::prepareForLayout() {
  StrTabBuilder.finalize();
  Size = StrTabBuilder.getSize();
}

Error SectionWriter::visit(const StringTableSection &Sec) {
  Sec.StrTabBuilder.write(reinterpret_cast<uint8_t *>(Out.getBufferStart()) +
                          Sec.Offset);
  return Error::success();
}

Error StringTableSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error StringTableSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const SectionIndexSection &Sec) {
  uint8_t *Buf = reinterpret_cast<uint8_t *>(Out.getBufferStart()) + Sec.Offset;
  llvm::copy(Sec.Indexes, reinterpret_cast<Elf_Word *>(Buf));
  return Error::success();
}

Error SectionIndexSection::initialize(SectionTableRef SecTable) {
  Size = 0;
  Expected<SymbolTableSection *> Sec =
      SecTable.getSectionOfType<SymbolTableSection>(
          Link,
          "Link field value " + Twine(Link) + " in section " + Name +
              " is invalid",
          "Link field value " + Twine(Link) + " in section " + Name +
              " is not a symbol table");
  if (!Sec)
    return Sec.takeError();

  setSymTab(*Sec);
  Symbols->setShndxTable(this);
  return Error::success();
}

void SectionIndexSection::finalize() { Link = Symbols->Index; }

Error SectionIndexSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error SectionIndexSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

static bool isValidReservedSectionIndex(uint16_t Index, uint16_t Machine) {
  switch (Index) {
  case SHN_ABS:
  case SHN_COMMON:
    return true;
  }

  if (Machine == EM_AMDGPU) {
    return Index == SHN_AMDGPU_LDS;
  }

  if (Machine == EM_MIPS) {
    switch (Index) {
    case SHN_MIPS_ACOMMON:
    case SHN_MIPS_SCOMMON:
    case SHN_MIPS_SUNDEFINED:
      return true;
    }
  }

  if (Machine == EM_HEXAGON) {
    switch (Index) {
    case SHN_HEXAGON_SCOMMON:
    case SHN_HEXAGON_SCOMMON_1:
    case SHN_HEXAGON_SCOMMON_2:
    case SHN_HEXAGON_SCOMMON_4:
    case SHN_HEXAGON_SCOMMON_8:
      return true;
    }
  }
  return false;
}

// Large indexes force us to clarify exactly what this function should do. This
// function should return the value that will appear in st_shndx when written
// out.
uint16_t Symbol::getShndx() const {
  if (DefinedIn != nullptr) {
    if (DefinedIn->Index >= SHN_LORESERVE)
      return SHN_XINDEX;
    return DefinedIn->Index;
  }

  if (ShndxType == SYMBOL_SIMPLE_INDEX) {
    // This means that we don't have a defined section but we do need to
    // output a legitimate section index.
    return SHN_UNDEF;
  }

  assert(ShndxType == SYMBOL_ABS || ShndxType == SYMBOL_COMMON ||
         (ShndxType >= SYMBOL_LOPROC && ShndxType <= SYMBOL_HIPROC) ||
         (ShndxType >= SYMBOL_LOOS && ShndxType <= SYMBOL_HIOS));
  return static_cast<uint16_t>(ShndxType);
}

bool Symbol::isCommon() const { return getShndx() == SHN_COMMON; }

void SymbolTableSection::assignIndices() {
  uint32_t Index = 0;
  for (auto &Sym : Symbols) {
    if (Sym->Index != Index)
      IndicesChanged = true;
    Sym->Index = Index++;
  }
}

void SymbolTableSection::addSymbol(Twine Name, uint8_t Bind, uint8_t Type,
                                   SectionBase *DefinedIn, uint64_t Value,
                                   uint8_t Visibility, uint16_t Shndx,
                                   uint64_t SymbolSize) {
  Symbol Sym;
  Sym.Name = Name.str();
  Sym.Binding = Bind;
  Sym.Type = Type;
  Sym.DefinedIn = DefinedIn;
  if (DefinedIn != nullptr)
    DefinedIn->HasSymbol = true;
  if (DefinedIn == nullptr) {
    if (Shndx >= SHN_LORESERVE)
      Sym.ShndxType = static_cast<SymbolShndxType>(Shndx);
    else
      Sym.ShndxType = SYMBOL_SIMPLE_INDEX;
  }
  Sym.Value = Value;
  Sym.Visibility = Visibility;
  Sym.Size = SymbolSize;
  Sym.Index = Symbols.size();
  Symbols.emplace_back(std::make_unique<Symbol>(Sym));
  Size += this->EntrySize;
}

Error SymbolTableSection::removeSectionReferences(
    bool AllowBrokenLinks, function_ref<bool(const SectionBase *)> ToRemove) {
  if (ToRemove(SectionIndexTable))
    SectionIndexTable = nullptr;
  if (ToRemove(SymbolNames)) {
    if (!AllowBrokenLinks)
      return createStringError(
          llvm::errc::invalid_argument,
          "string table '%s' cannot be removed because it is "
          "referenced by the symbol table '%s'",
          SymbolNames->Name.data(), this->Name.data());
    SymbolNames = nullptr;
  }
  return removeSymbols(
      [ToRemove](const Symbol &Sym) { return ToRemove(Sym.DefinedIn); });
}

void SymbolTableSection::updateSymbols(function_ref<void(Symbol &)> Callable) {
  for (SymPtr &Sym : llvm::drop_begin(Symbols))
    Callable(*Sym);
  std::stable_partition(
      std::begin(Symbols), std::end(Symbols),
      [](const SymPtr &Sym) { return Sym->Binding == STB_LOCAL; });
  assignIndices();
}

Error SymbolTableSection::removeSymbols(
    function_ref<bool(const Symbol &)> ToRemove) {
  Symbols.erase(
      std::remove_if(std::begin(Symbols) + 1, std::end(Symbols),
                     [ToRemove](const SymPtr &Sym) { return ToRemove(*Sym); }),
      std::end(Symbols));
  auto PrevSize = Size;
  Size = Symbols.size() * EntrySize;
  if (Size < PrevSize)
    IndicesChanged = true;
  assignIndices();
  return Error::success();
}

void SymbolTableSection::replaceSectionReferences(
    const DenseMap<SectionBase *, SectionBase *> &FromTo) {
  for (std::unique_ptr<Symbol> &Sym : Symbols)
    if (SectionBase *To = FromTo.lookup(Sym->DefinedIn))
      Sym->DefinedIn = To;
}

Error SymbolTableSection::initialize(SectionTableRef SecTable) {
  Size = 0;
  Expected<StringTableSection *> Sec =
      SecTable.getSectionOfType<StringTableSection>(
          Link,
          "Symbol table has link index of " + Twine(Link) +
              " which is not a valid index",
          "Symbol table has link index of " + Twine(Link) +
              " which is not a string table");
  if (!Sec)
    return Sec.takeError();

  setStrTab(*Sec);
  return Error::success();
}

void SymbolTableSection::finalize() {
  uint32_t MaxLocalIndex = 0;
  for (std::unique_ptr<Symbol> &Sym : Symbols) {
    Sym->NameIndex =
        SymbolNames == nullptr ? 0 : SymbolNames->findIndex(Sym->Name);
    if (Sym->Binding == STB_LOCAL)
      MaxLocalIndex = std::max(MaxLocalIndex, Sym->Index);
  }
  // Now we need to set the Link and Info fields.
  Link = SymbolNames == nullptr ? 0 : SymbolNames->Index;
  Info = MaxLocalIndex + 1;
}

void SymbolTableSection::prepareForLayout() {
  // Reserve proper amount of space in section index table, so we can
  // layout sections correctly. We will fill the table with correct
  // indexes later in fillShdnxTable.
  if (SectionIndexTable)
    SectionIndexTable->reserve(Symbols.size());

  // Add all of our strings to SymbolNames so that SymbolNames has the right
  // size before layout is decided.
  // If the symbol names section has been removed, don't try to add strings to
  // the table.
  if (SymbolNames != nullptr)
    for (std::unique_ptr<Symbol> &Sym : Symbols)
      SymbolNames->addString(Sym->Name);
}

void SymbolTableSection::fillShndxTable() {
  if (SectionIndexTable == nullptr)
    return;
  // Fill section index table with real section indexes. This function must
  // be called after assignOffsets.
  for (const std::unique_ptr<Symbol> &Sym : Symbols) {
    if (Sym->DefinedIn != nullptr && Sym->DefinedIn->Index >= SHN_LORESERVE)
      SectionIndexTable->addIndex(Sym->DefinedIn->Index);
    else
      SectionIndexTable->addIndex(SHN_UNDEF);
  }
}

Expected<const Symbol *>
SymbolTableSection::getSymbolByIndex(uint32_t Index) const {
  if (Symbols.size() <= Index)
    return createStringError(errc::invalid_argument,
                             "invalid symbol index: " + Twine(Index));
  return Symbols[Index].get();
}

Expected<Symbol *> SymbolTableSection::getSymbolByIndex(uint32_t Index) {
  Expected<const Symbol *> Sym =
      static_cast<const SymbolTableSection *>(this)->getSymbolByIndex(Index);
  if (!Sym)
    return Sym.takeError();

  return const_cast<Symbol *>(*Sym);
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const SymbolTableSection &Sec) {
  Elf_Sym *Sym = reinterpret_cast<Elf_Sym *>(Out.getBufferStart() + Sec.Offset);
  // Loop though symbols setting each entry of the symbol table.
  for (const std::unique_ptr<Symbol> &Symbol : Sec.Symbols) {
    Sym->st_name = Symbol->NameIndex;
    Sym->st_value = Symbol->Value;
    Sym->st_size = Symbol->Size;
    Sym->st_other = Symbol->Visibility;
    Sym->setBinding(Symbol->Binding);
    Sym->setType(Symbol->Type);
    Sym->st_shndx = Symbol->getShndx();
    ++Sym;
  }
  return Error::success();
}

Error SymbolTableSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error SymbolTableSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

StringRef RelocationSectionBase::getNamePrefix() const {
  switch (Type) {
  case SHT_REL:
    return ".rel";
  case SHT_RELA:
    return ".rela";
  case SHT_CREL:
    return ".crel";
  default:
    llvm_unreachable("not a relocation section");
  }
}

Error RelocationSection::removeSectionReferences(
    bool AllowBrokenLinks, function_ref<bool(const SectionBase *)> ToRemove) {
  if (ToRemove(Symbols)) {
    if (!AllowBrokenLinks)
      return createStringError(
          llvm::errc::invalid_argument,
          "symbol table '%s' cannot be removed because it is "
          "referenced by the relocation section '%s'",
          Symbols->Name.data(), this->Name.data());
    Symbols = nullptr;
  }

  for (const Relocation &R : Relocations) {
    if (!R.RelocSymbol || !R.RelocSymbol->DefinedIn ||
        !ToRemove(R.RelocSymbol->DefinedIn))
      continue;
    return createStringError(llvm::errc::invalid_argument,
                             "section '%s' cannot be removed: (%s+0x%" PRIx64
                             ") has relocation against symbol '%s'",
                             R.RelocSymbol->DefinedIn->Name.data(),
                             SecToApplyRel->Name.data(), R.Offset,
                             R.RelocSymbol->Name.c_str());
  }

  return Error::success();
}

template <class SymTabType>
Error RelocSectionWithSymtabBase<SymTabType>::initialize(
    SectionTableRef SecTable) {
  if (Link != SHN_UNDEF) {
    Expected<SymTabType *> Sec = SecTable.getSectionOfType<SymTabType>(
        Link,
        "Link field value " + Twine(Link) + " in section " + Name +
            " is invalid",
        "Link field value " + Twine(Link) + " in section " + Name +
            " is not a symbol table");
    if (!Sec)
      return Sec.takeError();

    setSymTab(*Sec);
  }

  if (Info != SHN_UNDEF) {
    Expected<SectionBase *> Sec =
        SecTable.getSection(Info, "Info field value " + Twine(Info) +
                                      " in section " + Name + " is invalid");
    if (!Sec)
      return Sec.takeError();

    setSection(*Sec);
  } else
    setSection(nullptr);

  return Error::success();
}

template <class SymTabType>
void RelocSectionWithSymtabBase<SymTabType>::finalize() {
  this->Link = Symbols ? Symbols->Index : 0;

  if (SecToApplyRel != nullptr)
    this->Info = SecToApplyRel->Index;
}

template <class ELFT>
static void setAddend(Elf_Rel_Impl<ELFT, false> &, uint64_t) {}

template <class ELFT>
static void setAddend(Elf_Rel_Impl<ELFT, true> &Rela, uint64_t Addend) {
  Rela.r_addend = Addend;
}

template <class RelRange, class T>
static void writeRel(const RelRange &Relocations, T *Buf, bool IsMips64EL) {
  for (const auto &Reloc : Relocations) {
    Buf->r_offset = Reloc.Offset;
    setAddend(*Buf, Reloc.Addend);
    Buf->setSymbolAndType(Reloc.RelocSymbol ? Reloc.RelocSymbol->Index : 0,
                          Reloc.Type, IsMips64EL);
    ++Buf;
  }
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const RelocationSection &Sec) {
  uint8_t *Buf = reinterpret_cast<uint8_t *>(Out.getBufferStart()) + Sec.Offset;
  if (Sec.Type == SHT_CREL) {
    auto Content = encodeCrel<ELFT::Is64Bits>(Sec.Relocations);
    memcpy(Buf, Content.data(), Content.size());
  } else if (Sec.Type == SHT_REL) {
    writeRel(Sec.Relocations, reinterpret_cast<Elf_Rel *>(Buf),
             Sec.getObject().IsMips64EL);
  } else {
    writeRel(Sec.Relocations, reinterpret_cast<Elf_Rela *>(Buf),
             Sec.getObject().IsMips64EL);
  }
  return Error::success();
}

Error RelocationSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error RelocationSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

Error RelocationSection::removeSymbols(
    function_ref<bool(const Symbol &)> ToRemove) {
  for (const Relocation &Reloc : Relocations)
    if (Reloc.RelocSymbol && ToRemove(*Reloc.RelocSymbol))
      return createStringError(
          llvm::errc::invalid_argument,
          "not stripping symbol '%s' because it is named in a relocation",
          Reloc.RelocSymbol->Name.data());
  return Error::success();
}

void RelocationSection::markSymbols() {
  for (const Relocation &Reloc : Relocations)
    if (Reloc.RelocSymbol)
      Reloc.RelocSymbol->Referenced = true;
}

void RelocationSection::replaceSectionReferences(
    const DenseMap<SectionBase *, SectionBase *> &FromTo) {
  // Update the target section if it was replaced.
  if (SectionBase *To = FromTo.lookup(SecToApplyRel))
    SecToApplyRel = To;
}

Error SectionWriter::visit(const DynamicRelocationSection &Sec) {
  llvm::copy(Sec.Contents, Out.getBufferStart() + Sec.Offset);
  return Error::success();
}

Error DynamicRelocationSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error DynamicRelocationSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

Error DynamicRelocationSection::removeSectionReferences(
    bool AllowBrokenLinks, function_ref<bool(const SectionBase *)> ToRemove) {
  if (ToRemove(Symbols)) {
    if (!AllowBrokenLinks)
      return createStringError(
          llvm::errc::invalid_argument,
          "symbol table '%s' cannot be removed because it is "
          "referenced by the relocation section '%s'",
          Symbols->Name.data(), this->Name.data());
    Symbols = nullptr;
  }

  // SecToApplyRel contains a section referenced by sh_info field. It keeps
  // a section to which the relocation section applies. When we remove any
  // sections we also remove their relocation sections. Since we do that much
  // earlier, this assert should never be triggered.
  assert(!SecToApplyRel || !ToRemove(SecToApplyRel));
  return Error::success();
}

Error Section::removeSectionReferences(
    bool AllowBrokenDependency,
    function_ref<bool(const SectionBase *)> ToRemove) {
  if (ToRemove(LinkSection)) {
    if (!AllowBrokenDependency)
      return createStringError(llvm::errc::invalid_argument,
                               "section '%s' cannot be removed because it is "
                               "referenced by the section '%s'",
                               LinkSection->Name.data(), this->Name.data());
    LinkSection = nullptr;
  }
  return Error::success();
}

void GroupSection::finalize() {
  this->Info = Sym ? Sym->Index : 0;
  this->Link = SymTab ? SymTab->Index : 0;
  // Linker deduplication for GRP_COMDAT is based on Sym->Name. The local/global
  // status is not part of the equation. If Sym is localized, the intention is
  // likely to make the group fully localized. Drop GRP_COMDAT to suppress
  // deduplication. See https://groups.google.com/g/generic-abi/c/2X6mR-s2zoc
  if ((FlagWord & GRP_COMDAT) && Sym && Sym->Binding == STB_LOCAL)
    this->FlagWord &= ~GRP_COMDAT;
}

Error GroupSection::removeSectionReferences(
    bool AllowBrokenLinks, function_ref<bool(const SectionBase *)> ToRemove) {
  if (ToRemove(SymTab)) {
    if (!AllowBrokenLinks)
      return createStringError(
          llvm::errc::invalid_argument,
          "section '.symtab' cannot be removed because it is "
          "referenced by the group section '%s'",
          this->Name.data());
    SymTab = nullptr;
    Sym = nullptr;
  }
  llvm::erase_if(GroupMembers, ToRemove);
  return Error::success();
}

Error GroupSection::removeSymbols(function_ref<bool(const Symbol &)> ToRemove) {
  if (ToRemove(*Sym))
    return createStringError(llvm::errc::invalid_argument,
                             "symbol '%s' cannot be removed because it is "
                             "referenced by the section '%s[%d]'",
                             Sym->Name.data(), this->Name.data(), this->Index);
  return Error::success();
}

void GroupSection::markSymbols() {
  if (Sym)
    Sym->Referenced = true;
}

void GroupSection::replaceSectionReferences(
    const DenseMap<SectionBase *, SectionBase *> &FromTo) {
  for (SectionBase *&Sec : GroupMembers)
    if (SectionBase *To = FromTo.lookup(Sec))
      Sec = To;
}

void GroupSection::onRemove() {
  // As the header section of the group is removed, drop the Group flag in its
  // former members.
  for (SectionBase *Sec : GroupMembers)
    Sec->Flags &= ~SHF_GROUP;
}

Error Section::initialize(SectionTableRef SecTable) {
  if (Link == ELF::SHN_UNDEF)
    return Error::success();

  Expected<SectionBase *> Sec =
      SecTable.getSection(Link, "Link field value " + Twine(Link) +
                                    " in section " + Name + " is invalid");
  if (!Sec)
    return Sec.takeError();

  LinkSection = *Sec;

  if (LinkSection->Type == ELF::SHT_SYMTAB) {
    HasSymTabLink = true;
    LinkSection = nullptr;
  }

  return Error::success();
}

void Section::finalize() { this->Link = LinkSection ? LinkSection->Index : 0; }

void GnuDebugLinkSection::init(StringRef File) {
  FileName = sys::path::filename(File);
  // The format for the .gnu_debuglink starts with the file name and is
  // followed by a null terminator and then the CRC32 of the file. The CRC32
  // should be 4 byte aligned. So we add the FileName size, a 1 for the null
  // byte, and then finally push the size to alignment and add 4.
  Size = alignTo(FileName.size() + 1, 4) + 4;
  // The CRC32 will only be aligned if we align the whole section.
  Align = 4;
  Type = OriginalType = ELF::SHT_PROGBITS;
  Name = ".gnu_debuglink";
  // For sections not found in segments, OriginalOffset is only used to
  // establish the order that sections should go in. By using the maximum
  // possible offset we cause this section to wind up at the end.
  OriginalOffset = std::numeric_limits<uint64_t>::max();
}

GnuDebugLinkSection::GnuDebugLinkSection(StringRef File,
                                         uint32_t PrecomputedCRC)
    : FileName(File), CRC32(PrecomputedCRC) {
  init(File);
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const GnuDebugLinkSection &Sec) {
  unsigned char *Buf =
      reinterpret_cast<uint8_t *>(Out.getBufferStart()) + Sec.Offset;
  Elf_Word *CRC =
      reinterpret_cast<Elf_Word *>(Buf + Sec.Size - sizeof(Elf_Word));
  *CRC = Sec.CRC32;
  llvm::copy(Sec.FileName, Buf);
  return Error::success();
}

Error GnuDebugLinkSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error GnuDebugLinkSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

template <class ELFT>
Error ELFSectionWriter<ELFT>::visit(const GroupSection &Sec) {
  ELF::Elf32_Word *Buf =
      reinterpret_cast<ELF::Elf32_Word *>(Out.getBufferStart() + Sec.Offset);
  endian::write32<ELFT::Endianness>(Buf++, Sec.FlagWord);
  for (SectionBase *S : Sec.GroupMembers)
    endian::write32<ELFT::Endianness>(Buf++, S->Index);
  return Error::success();
}

Error GroupSection::accept(SectionVisitor &Visitor) const {
  return Visitor.visit(*this);
}

Error GroupSection::accept(MutableSectionVisitor &Visitor) {
  return Visitor.visit(*this);
}

// Returns true IFF a section is wholly inside the range of a segment
static bool sectionWithinSegment(const SectionBase &Sec, const Segment &Seg) {
  // If a section is empty it should be treated like it has a size of 1. This is
  // to clarify the case when an empty section lies on a boundary between two
  // segments and ensures that the section "belongs" to the second segment and
  // not the first.
  uint64_t SecSize = Sec.Size ? Sec.Size : 1;

  // Ignore just added sections.
  if (Sec.OriginalOffset == std::numeric_limits<uint64_t>::max())
    return false;

  if (Sec.Type == SHT_NOBITS) {
    if (!(Sec.Flags & SHF_ALLOC))
      return false;

    bool SectionIsTLS = Sec.Flags & SHF_TLS;
    bool SegmentIsTLS = Seg.Type == PT_TLS;
    if (SectionIsTLS != SegmentIsTLS)
      return false;

    return Seg.VAddr <= Sec.Addr &&
           Seg.VAddr + Seg.MemSize >= Sec.Addr + SecSize;
  }

  return Seg.Offset <= Sec.OriginalOffset &&
         Seg.Offset + Seg.FileSize >= Sec.OriginalOffset + SecSize;
}

// Returns true IFF a segment's original offset is inside of another segment's
// range.
static bool segmentOverlapsSegment(const Segment &Child,
                                   const Segment &Parent) {

  return Parent.OriginalOffset <= Child.OriginalOffset &&
         Parent.OriginalOffset + Parent.FileSize > Child.OriginalOffset;
}

static bool compareSegmentsByOffset(const Segment *A, const Segment *B) {
  // Any segment without a parent segment should come before a segment
  // that has a parent segment.
  if (A->OriginalOffset < B->OriginalOffset)
    return true;
  if (A->OriginalOffset > B->OriginalOffset)
    return false;
  // If alignments are different, the one with a smaller alignment cannot be the
  // parent; otherwise, layoutSegments will not respect the larger alignment
  // requirement. This rule ensures that PT_LOAD/PT_INTERP/PT_GNU_RELRO/PT_TLS
  // segments at the same offset will be aligned correctly.
  if (A->Align != B->Align)
    return A->Align > B->Align;
  return A->Index < B->Index;
}

void BasicELFBuilder::initFileHeader() {
  Obj->Flags = 0x0;
  Obj->Type = ET_REL;
  Obj->OSABI = ELFOSABI_NONE;
  Obj->ABIVersion = 0;
  Obj->Entry = 0x0;
  Obj->Machine = EM_NONE;
  Obj->Version = 1;
}

void BasicELFBuilder::initHeaderSegment() { Obj->ElfHdrSegment.Index = 0; }

StringTableSection *BasicELFBuilder::addStrTab() {
  auto &StrTab = Obj->addSection<StringTableSection>();
  StrTab.Name = ".strtab";

  Obj->SectionNames = &StrTab;
  return &StrTab;
}

SymbolTableSection *BasicELFBuilder::addSymTab(StringTableSection *StrTab) {
  auto &SymTab = Obj->addSection<SymbolTableSection>();

  SymTab.Name = ".symtab";
  SymTab.Link = StrTab->Index;

  // The symbol table always needs a null symbol
  SymTab.addSymbol("", 0, 0, nullptr, 0, 0, 0, 0);

  Obj->SymbolTable = &SymTab;
  return &SymTab;
}

Error BasicELFBuilder::initSections() {
  for (SectionBase &Sec : Obj->sections())
    if (Error Err = Sec.initialize(Obj->sections()))
      return Err;

  return Error::success();
}

void BinaryELFBuilder::addData(SymbolTableSection *SymTab) {
  auto Data = ArrayRef<uint8_t>(
      reinterpret_cast<const uint8_t *>(MemBuf->getBufferStart()),
      MemBuf->getBufferSize());
  auto &DataSection = Obj->addSection<Section>(Data);
  DataSection.Name = ".data";
  DataSection.Type = ELF::SHT_PROGBITS;
  DataSection.Size = Data.size();
  DataSection.Flags = ELF::SHF_ALLOC | ELF::SHF_WRITE;

  std::string SanitizedFilename = MemBuf->getBufferIdentifier().str();
  std::replace_if(
      std::begin(SanitizedFilename), std::end(SanitizedFilename),
      [](char C) { return !isAlnum(C); }, '_');
  Twine Prefix = Twine("_binary_") + SanitizedFilename;

  SymTab->addSymbol(Prefix + "_start", STB_GLOBAL, STT_NOTYPE, &DataSection,
                    /*Value=*/0, NewSymbolVisibility, 0, 0);
  SymTab->addSymbol(Prefix + "_end", STB_GLOBAL, STT_NOTYPE, &DataSection,
                    /*Value=*/DataSection.Size, NewSymbolVisibility, 0, 0);
  SymTab->addSymbol(Prefix + "_size", STB_GLOBAL, STT_NOTYPE, nullptr,
                    /*Value=*/DataSection.Size, NewSymbolVisibility, SHN_ABS,
                    0);
}

Expected<std::unique_ptr<Object>> BinaryELFBuilder::build() {
  initFileHeader();
  initHeaderSegment();

  SymbolTableSection *SymTab = addSymTab(addStrTab());
  if (Error Err = initSections())
    return std::move(Err);
  addData(SymTab);

  return std::move(Obj);
}

// Adds sections from IHEX data file. Data should have been
// fully validated by this time.
void IHexELFBuilder::addDataSections() {
  OwnedDataSection *Section = nullptr;
  uint64_t SegmentAddr = 0, BaseAddr = 0;
  uint32_t SecNo = 1;

  for (const IHexRecord &R : Records) {
    uint64_t RecAddr;
    switch (R.Type) {
    case IHexRecord::Data:
      // Ignore empty data records
      if (R.HexData.empty())
        continue;
      RecAddr = R.Addr + SegmentAddr + BaseAddr;
      if (!Section || Section->Addr + Section->Size != RecAddr) {
        // OriginalOffset field is only used to sort sections before layout, so
        // instead of keeping track of real offsets in IHEX file, and as
        // layoutSections() and layoutSectionsForOnlyKeepDebug() use
        // llvm::stable_sort(), we can just set it to a constant (zero).
        Section = &Obj->addSection<OwnedDataSection>(
            ".sec" + std::to_string(SecNo), RecAddr,
            ELF::SHF_ALLOC | ELF::SHF_WRITE, 0);
        SecNo++;
      }
      Section->appendHexData(R.HexData);
      break;
    case IHexRecord::EndOfFile:
      break;
    case IHexRecord::SegmentAddr:
      // 20-bit segment address.
      SegmentAddr = checkedGetHex<uint16_t>(R.HexData) << 4;
      break;
    case IHexRecord::StartAddr80x86:
    case IHexRecord::StartAddr:
      Obj->Entry = checkedGetHex<uint32_t>(R.HexData);
      assert(Obj->Entry <= 0xFFFFFU);
      break;
    case IHexRecord::ExtendedAddr:
      // 16-31 bits of linear base address
      BaseAddr = checkedGetHex<uint16_t>(R.HexData) << 16;
      break;
    default:
      llvm_unreachable("unknown record type");
    }
  }
}

Expected<std::unique_ptr<Object>> IHexELFBuilder::build() {
  initFileHeader();
  initHeaderSegment();
  StringTableSection *StrTab = addStrTab();
  addSymTab(StrTab);
  if (Error Err = initSections())
    return std::move(Err);
  addDataSections();

  return std::move(Obj);
}

template <class ELFT>
ELFBuilder<ELFT>::ELFBuilder(const ELFObjectFile<ELFT> &ElfObj, Object &Obj,
                             std::optional<StringRef> ExtractPartition)
    : ElfFile(ElfObj.getELFFile()), Obj(Obj),
      ExtractPartition(ExtractPartition) {
  Obj.IsMips64EL = ElfFile.isMips64EL();
}

template <class ELFT> void ELFBuilder<ELFT>::setParentSegment(Segment &Child) {
  for (Segment &Parent : Obj.segments()) {
    // Every segment will overlap with itself but we don't want a segment to
    // be its own parent so we avoid that situation.
    if (&Child != &Parent && segmentOverlapsSegment(Child, Parent)) {
      // We want a canonical "most parental" segment but this requires
      // inspecting the ParentSegment.
      if (compareSegmentsByOffset(&Parent, &Child))
        if (Child.ParentSegment == nullptr ||
            compareSegmentsByOffset(&Parent, Child.ParentSegment)) {
          Child.ParentSegment = &Parent;
        }
    }
  }
}

template <class ELFT> Error ELFBuilder<ELFT>::findEhdrOffset() {
  if (!ExtractPartition)
    return Error::success();

  for (const SectionBase &Sec : Obj.sections()) {
    if (Sec.Type == SHT_LLVM_PART_EHDR && Sec.Name == *ExtractPartition) {
      EhdrOffset = Sec.Offset;
      return Error::success();
    }
  }
  return createStringError(errc::invalid_argument,
                           "could not find partition named '" +
                               *ExtractPartition + "'");
}

template <class ELFT>
Error ELFBuilder<ELFT>::readProgramHeaders(const ELFFile<ELFT> &HeadersFile) {
  uint32_t Index = 0;

  Expected<typename ELFFile<ELFT>::Elf_Phdr_Range> Headers =
      HeadersFile.program_headers();
  if (!Headers)
    return Headers.takeError();

  for (const typename ELFFile<ELFT>::Elf_Phdr &Phdr : *Headers) {
    if (Phdr.p_offset + Phdr.p_filesz > HeadersFile.getBufSize())
      return createStringError(
          errc::invalid_argument,
          "program header with offset 0x" + Twine::utohexstr(Phdr.p_offset) +
              " and file size 0x" + Twine::utohexstr(Phdr.p_filesz) +
              " goes past the end of the file");

    ArrayRef<uint8_t> Data{HeadersFile.base() + Phdr.p_offset,
                           (size_t)Phdr.p_filesz};
    Segment &Seg = Obj.addSegment(Data);
    Seg.Type = Phdr.p_type;
    Seg.Flags = Phdr.p_flags;
    Seg.OriginalOffset = Phdr.p_offset + EhdrOffset;
    Seg.Offset = Phdr.p_offset + EhdrOffset;
    Seg.VAddr = Phdr.p_vaddr;
    Seg.PAddr = Phdr.p_paddr;
    Seg.FileSize = Phdr.p_filesz;
    Seg.MemSize = Phdr.p_memsz;
    Seg.Align = Phdr.p_align;
    Seg.Index = Index++;
    for (SectionBase &Sec : Obj.sections())
      if (sectionWithinSegment(Sec, Seg)) {
        Seg.addSection(&Sec);
        if (!Sec.ParentSegment || Sec.ParentSegment->Offset > Seg.Offset)
          Sec.ParentSegment = &Seg;
      }
  }

  auto &ElfHdr = Obj.ElfHdrSegment;
  ElfHdr.Index = Index++;
  ElfHdr.OriginalOffset = ElfHdr.Offset = EhdrOffset;

  const typename ELFT::Ehdr &Ehdr = HeadersFile.getHeader();
  auto &PrHdr = Obj.ProgramHdrSegment;
  PrHdr.Type = PT_PHDR;
  PrHdr.Flags = 0;
  // The spec requires us to have p_vaddr % p_align == p_offset % p_align.
  // Whereas this works automatically for ElfHdr, here OriginalOffset is
  // always non-zero and to ensure the equation we assign the same value to
  // VAddr as well.
  PrHdr.OriginalOffset = PrHdr.Offset = PrHdr.VAddr = EhdrOffset + Ehdr.e_phoff;
  PrHdr.PAddr = 0;
  PrHdr.FileSize = PrHdr.MemSize = Ehdr.e_phentsize * Ehdr.e_phnum;
  // The spec requires us to naturally align all the fields.
  PrHdr.Align = sizeof(Elf_Addr);
  PrHdr.Index = Index++;

  // Now we do an O(n^2) loop through the segments in order to match up
  // segments.
  for (Segment &Child : Obj.segments())
    setParentSegment(Child);
  setParentSegment(ElfHdr);
  setParentSegment(PrHdr);

  return Error::success();
}

template <class ELFT>
Error ELFBuilder<ELFT>::initGroupSection(GroupSection *GroupSec) {
  if (GroupSec->Align % sizeof(ELF::Elf32_Word) != 0)
    return createStringError(errc::invalid_argument,
                             "invalid alignment " + Twine(GroupSec->Align) +
                                 " of group section '" + GroupSec->Name + "'");
  SectionTableRef SecTable = Obj.sections();
  if (GroupSec->Link != SHN_UNDEF) {
    auto SymTab = SecTable.template getSectionOfType<SymbolTableSection>(
        GroupSec->Link,
        "link field value '" + Twine(GroupSec->Link) + "' in section '" +
            GroupSec->Name + "' is invalid",
        "link field value '" + Twine(GroupSec->Link) + "' in section '" +
            GroupSec->Name + "' is not a symbol table");
    if (!SymTab)
      return SymTab.takeError();

    Expected<Symbol *> Sym = (*SymTab)->getSymbolByIndex(GroupSec->Info);
    if (!Sym)
      return createStringError(errc::invalid_argument,
                               "info field value '" + Twine(GroupSec->Info) +
                                   "' in section '" + GroupSec->Name +
                                   "' is not a valid symbol index");
    GroupSec->setSymTab(*SymTab);
    GroupSec->setSymbol(*Sym);
  }
  if (GroupSec->Contents.size() % sizeof(ELF::Elf32_Word) ||
      GroupSec->Contents.empty())
    return createStringError(errc::invalid_argument,
                             "the content of the section " + GroupSec->Name +
                                 " is malformed");
  const ELF::Elf32_Word *Word =
      reinterpret_cast<const ELF::Elf32_Word *>(GroupSec->Contents.data());
  const ELF::Elf32_Word *End =
      Word + GroupSec->Contents.size() / sizeof(ELF::Elf32_Word);
  GroupSec->setFlagWord(endian::read32<ELFT::Endianness>(Word++));
  for (; Word != End; ++Word) {
    uint32_t Index = support::endian::read32<ELFT::Endianness>(Word);
    Expected<SectionBase *> Sec = SecTable.getSection(
        Index, "group member index " + Twine(Index) + " in section '" +
                   GroupSec->Name + "' is invalid");
    if (!Sec)
      return Sec.takeError();

    GroupSec->addMember(*Sec);
  }

  return Error::success();
}

template <class ELFT>
Error ELFBuilder<ELFT>::initSymbolTable(SymbolTableSection *SymTab) {
  Expected<const Elf_Shdr *> Shdr = ElfFile.getSection(SymTab->Index);
  if (!Shdr)
    return Shdr.takeError();

  Expected<StringRef> StrTabData = ElfFile.getStringTableForSymtab(**Shdr);
  if (!StrTabData)
    return StrTabData.takeError();

  ArrayRef<Elf_Word> ShndxData;

  Expected<typename ELFFile<ELFT>::Elf_Sym_Range> Symbols =
      ElfFile.symbols(*Shdr);
  if (!Symbols)
    return Symbols.takeError();

  for (const typename ELFFile<ELFT>::Elf_Sym &Sym : *Symbols) {
    SectionBase *DefSection = nullptr;

    Expected<StringRef> Name = Sym.getName(*StrTabData);
    if (!Name)
      return Name.takeError();

    if (Sym.st_shndx == SHN_XINDEX) {
      if (SymTab->getShndxTable() == nullptr)
        return createStringError(errc::invalid_argument,
                                 "symbol '" + *Name +
                                     "' has index SHN_XINDEX but no "
                                     "SHT_SYMTAB_SHNDX section exists");
      if (ShndxData.data() == nullptr) {
        Expected<const Elf_Shdr *> ShndxSec =
            ElfFile.getSection(SymTab->getShndxTable()->Index);
        if (!ShndxSec)
          return ShndxSec.takeError();

        Expected<ArrayRef<Elf_Word>> Data =
            ElfFile.template getSectionContentsAsArray<Elf_Word>(**ShndxSec);
        if (!Data)
          return Data.takeError();

        ShndxData = *Data;
        if (ShndxData.size() != Symbols->size())
          return createStringError(
              errc::invalid_argument,
              "symbol section index table does not have the same number of "
              "entries as the symbol table");
      }
      Elf_Word Index = ShndxData[&Sym - Symbols->begin()];
      Expected<SectionBase *> Sec = Obj.sections().getSection(
          Index,
          "symbol '" + *Name + "' has invalid section index " + Twine(Index));
      if (!Sec)
        return Sec.takeError();

      DefSection = *Sec;
    } else if (Sym.st_shndx >= SHN_LORESERVE) {
      if (!isValidReservedSectionIndex(Sym.st_shndx, Obj.Machine)) {
        return createStringError(
            errc::invalid_argument,
            "symbol '" + *Name +
                "' has unsupported value greater than or equal "
                "to SHN_LORESERVE: " +
                Twine(Sym.st_shndx));
      }
    } else if (Sym.st_shndx != SHN_UNDEF) {
      Expected<SectionBase *> Sec = Obj.sections().getSection(
          Sym.st_shndx, "symbol '" + *Name +
                            "' is defined has invalid section index " +
                            Twine(Sym.st_shndx));
      if (!Sec)
        return Sec.takeError();

      DefSection = *Sec;
    }

    SymTab->addSymbol(*Name, Sym.getBinding(), Sym.getType(), DefSection,
                      Sym.getValue(), Sym.st_other, Sym.st_shndx, Sym.st_size);
  }

  return Error::success();
}

template <class ELFT>
static void getAddend(uint64_t &, const Elf_Rel_Impl<ELFT, false> &) {}

template <class ELFT>
static void getAddend(uint64_t &ToSet, const Elf_Rel_Impl<ELFT, true> &Rela) {
  ToSet = Rela.r_addend;
}

template <class T>
static Error initRelocations(RelocationSection *Relocs, T RelRange) {
  for (const auto &Rel : RelRange) {
    Relocation ToAdd;
    ToAdd.Offset = Rel.r_offset;
    getAddend(ToAdd.Addend, Rel);
    ToAdd.Type = Rel.getType(Relocs->getObject().IsMips64EL);

    if (uint32_t Sym = Rel.getSymbol(Relocs->getObject().IsMips64EL)) {
      if (!Relocs->getObject().SymbolTable)
        return createStringError(
            errc::invalid_argument,
            "'" + Relocs->Name + "': relocation references symbol with index " +
                Twine(Sym) + ", but there is no symbol table");
      Expected<Symbol *> SymByIndex =
          Relocs->getObject().SymbolTable->getSymbolByIndex(Sym);
      if (!SymByIndex)
        return SymByIndex.takeError();

      ToAdd.RelocSymbol = *SymByIndex;
    }

    Relocs->addRelocation(ToAdd);
  }

  return Error::success();
}

Expected<SectionBase *> SectionTableRef::getSection(uint32_t Index,
                                                    Twine ErrMsg) {
  if (Index == SHN_UNDEF || Index > Sections.size())
    return createStringError(errc::invalid_argument, ErrMsg);
  return Sections[Index - 1].get();
}

template <class T>
Expected<T *> SectionTableRef::getSectionOfType(uint32_t Index,
                                                Twine IndexErrMsg,
                                                Twine TypeErrMsg) {
  Expected<SectionBase *> BaseSec = getSection(Index, IndexErrMsg);
  if (!BaseSec)
    return BaseSec.takeError();

  if (T *Sec = dyn_cast<T>(*BaseSec))
    return Sec;

  return createStringError(errc::invalid_argument, TypeErrMsg);
}

template <class ELFT>
Expected<SectionBase &> ELFBuilder<ELFT>::makeSection(const Elf_Shdr &Shdr) {
  switch (Shdr.sh_type) {
  case SHT_REL:
  case SHT_RELA:
  case SHT_CREL:
    if (Shdr.sh_flags & SHF_ALLOC) {
      if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
        return Obj.addSection<DynamicRelocationSection>(*Data);
      else
        return Data.takeError();
    }
    return Obj.addSection<RelocationSection>(Obj);
  case SHT_STRTAB:
    // If a string table is allocated we don't want to mess with it. That would
    // mean altering the memory image. There are no special link types or
    // anything so we can just use a Section.
    if (Shdr.sh_flags & SHF_ALLOC) {
      if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
        return Obj.addSection<Section>(*Data);
      else
        return Data.takeError();
    }
    return Obj.addSection<StringTableSection>();
  case SHT_HASH:
  case SHT_GNU_HASH:
    // Hash tables should refer to SHT_DYNSYM which we're not going to change.
    // Because of this we don't need to mess with the hash tables either.
    if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
      return Obj.addSection<Section>(*Data);
    else
      return Data.takeError();
  case SHT_GROUP:
    if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
      return Obj.addSection<GroupSection>(*Data);
    else
      return Data.takeError();
  case SHT_DYNSYM:
    if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
      return Obj.addSection<DynamicSymbolTableSection>(*Data);
    else
      return Data.takeError();
  case SHT_DYNAMIC:
    if (Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr))
      return Obj.addSection<DynamicSection>(*Data);
    else
      return Data.takeError();
  case SHT_SYMTAB: {
    // Multiple SHT_SYMTAB sections are forbidden by the ELF gABI.
    if (Obj.SymbolTable != nullptr)
      return createStringError(llvm::errc::invalid_argument,
                               "found multiple SHT_SYMTAB sections");
    auto &SymTab = Obj.addSection<SymbolTableSection>();
    Obj.SymbolTable = &SymTab;
    return SymTab;
  }
  case SHT_SYMTAB_SHNDX: {
    auto &ShndxSection = Obj.addSection<SectionIndexSection>();
    Obj.SectionIndexTable = &ShndxSection;
    return ShndxSection;
  }
  case SHT_NOBITS:
    return Obj.addSection<Section>(ArrayRef<uint8_t>());
  default: {
    Expected<ArrayRef<uint8_t>> Data = ElfFile.getSectionContents(Shdr);
    if (!Data)
      return Data.takeError();

    Expected<StringRef> Name = ElfFile.getSectionName(Shdr);
    if (!Name)
      return Name.takeError();

    if (!(Shdr.sh_flags & ELF::SHF_COMPRESSED))
      return Obj.addSection<Section>(*Data);
    auto *Chdr = reinterpret_cast<const Elf_Chdr_Impl<ELFT> *>(Data->data());
    return Obj.addSection<CompressedSection>(CompressedSection(
        *Data, Chdr->ch_type, Chdr->ch_size, Chdr->ch_addralign));
  }
  }
}

template <class ELFT> Error ELFBuilder<ELFT>::readSectionHeaders() {
  uint32_t Index = 0;
  Expected<typename ELFFile<ELFT>::Elf_Shdr_Range> Sections =
      ElfFile.sections();
  if (!Sections)
    return Sections.takeError();

  for (const typename ELFFile<ELFT>::Elf_Shdr &Shdr : *Sections) {
    if (Index == 0) {
      ++Index;
      continue;
    }
    Expected<SectionBase &> Sec = makeSection(Shdr);
    if (!Sec)
      return Sec.takeError();

    Expected<StringRef> SecName = ElfFile.getSectionName(Shdr);
    if (!SecName)
      return SecName.takeError();
    Sec->Name = SecName->str();
    Sec->Type = Sec->OriginalType = Shdr.sh_type;
    Sec->Flags = Sec->OriginalFlags = Shdr.sh_flags;
    Sec->Addr = Shdr.sh_addr;
    Sec->Offset = Shdr.sh_offset;
    Sec->OriginalOffset = Shdr.sh_offset;
    Sec->Size = Shdr.sh_size;
    Sec->Link = Shdr.sh_link;
    Sec->Info = Shdr.sh_info;
    Sec->Align = Shdr.sh_addralign;
    Sec->EntrySize = Shdr.sh_entsize;
    Sec->Index = Index++;
    Sec->OriginalIndex = Sec->Index;
    Sec->OriginalData = ArrayRef<uint8_t>(
        ElfFile.base() + Shdr.sh_offset,
        (Shdr.sh_type == SHT_NOBITS) ? (size_t)0 : Shdr.sh_size);
  }

  return Error::success();
}

template <class ELFT> Error ELFBuilder<ELFT>::readSections(bool EnsureSymtab) {
  uint32_t ShstrIndex = ElfFile.getHeader().e_shstrndx;
  if (ShstrIndex == SHN_XINDEX) {
    Expected<const Elf_Shdr *> Sec = ElfFile.getSection(0);
    if (!Sec)
      return Sec.takeError();

    ShstrIndex = (*Sec)->sh_link;
  }

  if (ShstrIndex == SHN_UNDEF)
    Obj.HadShdrs = false;
  else {
    Expected<StringTableSection *> Sec =
        Obj.sections().template getSectionOfType<StringTableSection>(
            ShstrIndex,
            "e_shstrndx field value " + Twine(ShstrIndex) + " in elf header " +
                " is invalid",
            "e_shstrndx field value " + Twine(ShstrIndex) + " in elf header " +
                " does not reference a string table");
    if (!Sec)
      return Sec.takeError();

    Obj.SectionNames = *Sec;
  }

  // If a section index table exists we'll need to initialize it before we
  // initialize the symbol table because the symbol table might need to
  // reference it.
  if (Obj.SectionIndexTable)
    if (Error Err = Obj.SectionIndexTable->initialize(Obj.sections()))
      return Err;

  // Now that all of the sections have been added we can fill out some extra
  // details about symbol tables. We need the symbol table filled out before
  // any relocations.
  if (Obj.SymbolTable) {
    if (Error Err = Obj.SymbolTable->initialize(Obj.sections()))
      return Err;
    if (Error Err = initSymbolTable(Obj.SymbolTable))
      return Err;
  } else if (EnsureSymtab) {
    if (Error Err = Obj.addNewSymbolTable())
      return Err;
  }

  // Now that all sections and symbols have been added we can add
  // relocations that reference symbols and set the link and info fields for
  // relocation sections.
  for (SectionBase &Sec : Obj.sections()) {
    if (&Sec == Obj.SymbolTable)
      continue;
    if (Error Err = Sec.initialize(Obj.sections()))
      return Err;
    if (auto RelSec = dyn_cast<RelocationSection>(&Sec)) {
      Expected<typename ELFFile<ELFT>::Elf_Shdr_Range> Sections =
          ElfFile.sections();
      if (!Sections)
        return Sections.takeError();

      const typename ELFFile<ELFT>::Elf_Shdr *Shdr =
          Sections->begin() + RelSec->Index;
      if (RelSec->Type == SHT_CREL) {
        auto RelsOrRelas = ElfFile.crels(*Shdr);
        if (!RelsOrRelas)
          return RelsOrRelas.takeError();
        if (Error Err = initRelocations(RelSec, RelsOrRelas->first))
          return Err;
        if (Error Err = initRelocations(RelSec, RelsOrRelas->second))
          return Err;
      } else if (RelSec->Type == SHT_REL) {
        Expected<typename ELFFile<ELFT>::Elf_Rel_Range> Rels =
            ElfFile.rels(*Shdr);
        if (!Rels)
          return Rels.takeError();

        if (Error Err = initRelocations(RelSec, *Rels))
          return Err;
      } else {
        Expected<typename ELFFile<ELFT>::Elf_Rela_Range> Relas =
            ElfFile.relas(*Shdr);
        if (!Relas)
          return Relas.takeError();

        if (Error Err = initRelocations(RelSec, *Relas))
          return Err;
      }
    } else if (auto GroupSec = dyn_cast<GroupSection>(&Sec)) {
      if (Error Err = initGroupSection(GroupSec))
        return Err;
    }
  }

  return Error::success();
}

template <class ELFT> Error ELFBuilder<ELFT>::build(bool EnsureSymtab) {
  if (Error E = readSectionHeaders())
    return E;
  if (Error E = findEhdrOffset())
    return E;

  // The ELFFile whose ELF headers and program headers are copied into the
  // output file. Normally the same as ElfFile, but if we're extracting a
  // loadable partition it will point to the partition's headers.
  Expected<ELFFile<ELFT>> HeadersFile = ELFFile<ELFT>::create(toStringRef(
      {ElfFile.base() + EhdrOffset, ElfFile.getBufSize() - EhdrOffset}));
  if (!HeadersFile)
    return HeadersFile.takeError();

  const typename ELFFile<ELFT>::Elf_Ehdr &Ehdr = HeadersFile->getHeader();
  Obj.Is64Bits = Ehdr.e_ident[EI_CLASS] == ELFCLASS64;
  Obj.OSABI = Ehdr.e_ident[EI_OSABI];
  Obj.ABIVersion = Ehdr.e_ident[EI_ABIVERSION];
  Obj.Type = Ehdr.e_type;
  Obj.Machine = Ehdr.e_machine;
  Obj.Version = Ehdr.e_version;
  Obj.Entry = Ehdr.e_entry;
  Obj.Flags = Ehdr.e_flags;

  if (Error E = readSections(EnsureSymtab))
    return E;
  return readProgramHeaders(*HeadersFile);
}

Writer::~Writer() = default;

Reader::~Reader() = default;

Expected<std::unique_ptr<Object>>
BinaryReader::create(bool /*EnsureSymtab*/) const {
  return BinaryELFBuilder(MemBuf, NewSymbolVisibility).build();
}

Expected<std::vector<IHexRecord>> IHexReader::parse() const {
  SmallVector<StringRef, 16> Lines;
  std::vector<IHexRecord> Records;
  bool HasSections = false;

  MemBuf->getBuffer().split(Lines, '\n');
  Records.reserve(Lines.size());
  for (size_t LineNo = 1; LineNo <= Lines.size(); ++LineNo) {
    StringRef Line = Lines[LineNo - 1].trim();
    if (Line.empty())
      continue;

    Expected<IHexRecord> R = IHexRecord::parse(Line);
    if (!R)
      return parseError(LineNo, R.takeError());
    if (R->Type == IHexRecord::EndOfFile)
      break;
    HasSections |= (R->Type == IHexRecord::Data);
    Records.push_back(*R);
  }
  if (!HasSections)
    return parseError(-1U, "no sections");

  return std::move(Records);
}

Expected<std::unique_ptr<Object>>
IHexReader::create(bool /*EnsureSymtab*/) const {
  Expected<std::vector<IHexRecord>> Records = parse();
  if (!Records)
    return Records.takeError();

  return IHexELFBuilder(*Records).build();
}

Expected<std::unique_ptr<Object>> ELFReader::create(bool EnsureSymtab) const {
  auto Obj = std::make_unique<Object>();
  if (auto *O = dyn_cast<ELFObjectFile<ELF32LE>>(Bin)) {
    ELFBuilder<ELF32LE> Builder(*O, *Obj, ExtractPartition);
    if (Error Err = Builder.build(EnsureSymtab))
      return std::move(Err);
    return std::move(Obj);
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF64LE>>(Bin)) {
    ELFBuilder<ELF64LE> Builder(*O, *Obj, ExtractPartition);
    if (Error Err = Builder.build(EnsureSymtab))
      return std::move(Err);
    return std::move(Obj);
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF32BE>>(Bin)) {
    ELFBuilder<ELF32BE> Builder(*O, *Obj, ExtractPartition);
    if (Error Err = Builder.build(EnsureSymtab))
      return std::move(Err);
    return std::move(Obj);
  } else if (auto *O = dyn_cast<ELFObjectFile<ELF64BE>>(Bin)) {
    ELFBuilder<ELF64BE> Builder(*O, *Obj, ExtractPartition);
    if (Error Err = Builder.build(EnsureSymtab))
      return std::move(Err);
    return std::move(Obj);
  }
  return createStringError(errc::invalid_argument, "invalid file type");
}

template <class ELFT> void ELFWriter<ELFT>::writeEhdr() {
  Elf_Ehdr &Ehdr = *reinterpret_cast<Elf_Ehdr *>(Buf->getBufferStart());
  std::fill(Ehdr.e_ident, Ehdr.e_ident + 16, 0);
  Ehdr.e_ident[EI_MAG0] = 0x7f;
  Ehdr.e_ident[EI_MAG1] = 'E';
  Ehdr.e_ident[EI_MAG2] = 'L';
  Ehdr.e_ident[EI_MAG3] = 'F';
  Ehdr.e_ident[EI_CLASS] = ELFT::Is64Bits ? ELFCLASS64 : ELFCLASS32;
  Ehdr.e_ident[EI_DATA] =
      ELFT::Endianness == llvm::endianness::big ? ELFDATA2MSB : ELFDATA2LSB;
  Ehdr.e_ident[EI_VERSION] = EV_CURRENT;
  Ehdr.e_ident[EI_OSABI] = Obj.OSABI;
  Ehdr.e_ident[EI_ABIVERSION] = Obj.ABIVersion;

  Ehdr.e_type = Obj.Type;
  Ehdr.e_machine = Obj.Machine;
  Ehdr.e_version = Obj.Version;
  Ehdr.e_entry = Obj.Entry;
  // We have to use the fully-qualified name llvm::size
  // since some compilers complain on ambiguous resolution.
  Ehdr.e_phnum = llvm::size(Obj.segments());
  Ehdr.e_phoff = (Ehdr.e_phnum != 0) ? Obj.ProgramHdrSegment.Offset : 0;
  Ehdr.e_phentsize = (Ehdr.e_phnum != 0) ? sizeof(Elf_Phdr) : 0;
  Ehdr.e_flags = Obj.Flags;
  Ehdr.e_ehsize = sizeof(Elf_Ehdr);
  if (WriteSectionHeaders && Obj.sections().size() != 0) {
    Ehdr.e_shentsize = sizeof(Elf_Shdr);
    Ehdr.e_shoff = Obj.SHOff;
    // """
    // If the number of sections is greater than or equal to
    // SHN_LORESERVE (0xff00), this member has the value zero and the actual
    // number of section header table entries is contained in the sh_size field
    // of the section header at index 0.
    // """
    auto Shnum = Obj.sections().size() + 1;
    if (Shnum >= SHN_LORESERVE)
      Ehdr.e_shnum = 0;
    else
      Ehdr.e_shnum = Shnum;
    // """
    // If the section name string table section index is greater than or equal
    // to SHN_LORESERVE (0xff00), this member has the value SHN_XINDEX (0xffff)
    // and the actual index of the section name string table section is
    // contained in the sh_link field of the section header at index 0.
    // """
    if (Obj.SectionNames->Index >= SHN_LORESERVE)
      Ehdr.e_shstrndx = SHN_XINDEX;
    else
      Ehdr.e_shstrndx = Obj.SectionNames->Index;
  } else {
    Ehdr.e_shentsize = 0;
    Ehdr.e_shoff = 0;
    Ehdr.e_shnum = 0;
    Ehdr.e_shstrndx = 0;
  }
}

template <class ELFT> void ELFWriter<ELFT>::writePhdrs() {
  for (auto &Seg : Obj.segments())
    writePhdr(Seg);
}

template <class ELFT> void ELFWriter<ELFT>::writeShdrs() {
  // This reference serves to write the dummy section header at the begining
  // of the file. It is not used for anything else
  Elf_Shdr &Shdr =
      *reinterpret_cast<Elf_Shdr *>(Buf->getBufferStart() + Obj.SHOff);
  Shdr.sh_name = 0;
  Shdr.sh_type = SHT_NULL;
  Shdr.sh_flags = 0;
  Shdr.sh_addr = 0;
  Shdr.sh_offset = 0;
  // See writeEhdr for why we do this.
  uint64_t Shnum = Obj.sections().size() + 1;
  if (Shnum >= SHN_LORESERVE)
    Shdr.sh_size = Shnum;
  else
    Shdr.sh_size = 0;
  // See writeEhdr for why we do this.
  if (Obj.SectionNames != nullptr && Obj.SectionNames->Index >= SHN_LORESERVE)
    Shdr.sh_link = Obj.SectionNames->Index;
  else
    Shdr.sh_link = 0;
  Shdr.sh_info = 0;
  Shdr.sh_addralign = 0;
  Shdr.sh_entsize = 0;

  for (SectionBase &Sec : Obj.sections())
    writeShdr(Sec);
}

template <class ELFT> Error ELFWriter<ELFT>::writeSectionData() {
  for (SectionBase &Sec : Obj.sections())
    // Segments are responsible for writing their contents, so only write the
    // section data if the section is not in a segment. Note that this renders
    // sections in segments effectively immutable.
    if (Sec.ParentSegment == nullptr)
      if (Error Err = Sec.accept(*SecWriter))
        return Err;

  return Error::success();
}

template <class ELFT> void ELFWriter<ELFT>::writeSegmentData() {
  for (Segment &Seg : Obj.segments()) {
    size_t Size = std::min<size_t>(Seg.FileSize, Seg.getContents().size());
    std::memcpy(Buf->getBufferStart() + Seg.Offset, Seg.getContents().data(),
                Size);
  }

  for (const auto &it : Obj.getUpdatedSections()) {
    SectionBase *Sec = it.first;
    ArrayRef<uint8_t> Data = it.second;

    auto *Parent = Sec->ParentSegment;
    assert(Parent && "This section should've been part of a segment.");
    uint64_t Offset =
        Sec->OriginalOffset - Parent->OriginalOffset + Parent->Offset;
    llvm::copy(Data, Buf->getBufferStart() + Offset);
  }

  // Iterate over removed sections and overwrite their old data with zeroes.
  for (auto &Sec : Obj.removedSections()) {
    Segment *Parent = Sec.ParentSegment;
    if (Parent == nullptr || Sec.Type == SHT_NOBITS || Sec.Size == 0)
      continue;
    uint64_t Offset =
        Sec.OriginalOffset - Parent->OriginalOffset + Parent->Offset;
    std::memset(Buf->getBufferStart() + Offset, 0, Sec.Size);
  }
}

template <class ELFT>
ELFWriter<ELFT>::ELFWriter(Object &Obj, raw_ostream &Buf, bool WSH,
                           bool OnlyKeepDebug)
    : Writer(Obj, Buf), WriteSectionHeaders(WSH && Obj.HadShdrs),
      OnlyKeepDebug(OnlyKeepDebug) {}

Error Object::updateSection(StringRef Name, ArrayRef<uint8_t> Data) {
  auto It = llvm::find_if(Sections,
                          [&](const SecPtr &Sec) { return Sec->Name == Name; });
  if (It == Sections.end())
    return createStringError(errc::invalid_argument, "section '%s' not found",
                             Name.str().c_str());

  auto *OldSec = It->get();
  if (!OldSec->hasContents())
    return createStringError(
        errc::invalid_argument,
        "section '%s' cannot be updated because it does not have contents",
        Name.str().c_str());

  if (Data.size() > OldSec->Size && OldSec->ParentSegment)
    return createStringError(errc::invalid_argument,
                             "cannot fit data of size %zu into section '%s' "
                             "with size %" PRIu64 " that is part of a segment",
                             Data.size(), Name.str().c_str(), OldSec->Size);

  if (!OldSec->ParentSegment) {
    *It = std::make_unique<OwnedDataSection>(*OldSec, Data);
  } else {
    // The segment writer will be in charge of updating these contents.
    OldSec->Size = Data.size();
    UpdatedSections[OldSec] = Data;
  }

  return Error::success();
}

Error Object::removeSections(
    bool AllowBrokenLinks, std::function<bool(const SectionBase &)> ToRemove) {

  auto Iter = std::stable_partition(
      std::begin(Sections), std::end(Sections), [=](const SecPtr &Sec) {
        if (ToRemove(*Sec))
          return false;
        // TODO: A compressed relocation section may be recognized as
        // RelocationSectionBase. We don't want such a section to be removed.
        if (isa<CompressedSection>(Sec))
          return true;
        if (auto RelSec = dyn_cast<RelocationSectionBase>(Sec.get())) {
          if (auto ToRelSec = RelSec->getSection())
            return !ToRemove(*ToRelSec);
        }
        // Remove empty group sections.
        if (Sec->Type == ELF::SHT_GROUP) {
          auto GroupSec = cast<GroupSection>(Sec.get());
          return !llvm::all_of(GroupSec->members(), ToRemove);
        }
        return true;
      });
  if (SymbolTable != nullptr && ToRemove(*SymbolTable))
    SymbolTable = nullptr;
  if (SectionNames != nullptr && ToRemove(*SectionNames))
    SectionNames = nullptr;
  if (SectionIndexTable != nullptr && ToRemove(*SectionIndexTable))
    SectionIndexTable = nullptr;
  // Now make sure there are no remaining references to the sections that will
  // be removed. Sometimes it is impossible to remove a reference so we emit
  // an error here instead.
  std::unordered_set<const SectionBase *> RemoveSections;
  RemoveSections.reserve(std::distance(Iter, std::end(Sections)));
  for (auto &RemoveSec : make_range(Iter, std::end(Sections))) {
    for (auto &Segment : Segments)
      Segment->removeSection(RemoveSec.get());
    RemoveSec->onRemove();
    RemoveSections.insert(RemoveSec.get());
  }

  // For each section that remains alive, we want to remove the dead references.
  // This either might update the content of the section (e.g. remove symbols
  // from symbol table that belongs to removed section) or trigger an error if
  // a live section critically depends on a section being removed somehow
  // (e.g. the removed section is referenced by a relocation).
  for (auto &KeepSec : make_range(std::begin(Sections), Iter)) {
    if (Error E = KeepSec->removeSectionReferences(
            AllowBrokenLinks, [&RemoveSections](const SectionBase *Sec) {
              return RemoveSections.find(Sec) != RemoveSections.end();
            }))
      return E;
  }

  // Transfer removed sections into the Object RemovedSections container for use
  // later.
  std::move(Iter, Sections.end(), std::back_inserter(RemovedSections));
  // Now finally get rid of them all together.
  Sections.erase(Iter, std::end(Sections));
  return Error::success();
}

Error Object::replaceSections(
    const DenseMap<SectionBase *, SectionBase *> &FromTo) {
  auto SectionIndexLess = [](const SecPtr &Lhs, const SecPtr &Rhs) {
    return Lhs->Index < Rhs->Index;
  };
  assert(llvm::is_sorted(Sections, SectionIndexLess) &&
         "Sections are expected to be sorted by Index");
  // Set indices of new sections so that they can be later sorted into positions
  // of removed ones.
  for (auto &I : FromTo)
    I.second->Index = I.first->Index;

  // Notify all sections about the replacement.
  for (auto &Sec : Sections)
    Sec->replaceSectionReferences(FromTo);

  if (Error E = removeSections(
          /*AllowBrokenLinks=*/false,
          [=](const SectionBase &Sec) { return FromTo.count(&Sec) > 0; }))
    return E;
  llvm::sort(Sections, SectionIndexLess);
  return Error::success();
}

Error Object::removeSymbols(function_ref<bool(const Symbol &)> ToRemove) {
  if (SymbolTable)
    for (const SecPtr &Sec : Sections)
      if (Error E = Sec->removeSymbols(ToRemove))
        return E;
  return Error::success();
}

Error Object::addNewSymbolTable() {
  assert(!SymbolTable && "Object must not has a SymbolTable.");

  // Reuse an existing SHT_STRTAB section if it exists.
  StringTableSection *StrTab = nullptr;
  for (SectionBase &Sec : sections()) {
    if (Sec.Type == ELF::SHT_STRTAB && !(Sec.Flags & SHF_ALLOC)) {
      StrTab = static_cast<StringTableSection *>(&Sec);

      // Prefer a string table that is not the section header string table, if
      // such a table exists.
      if (SectionNames != &Sec)
        break;
    }
  }
  if (!StrTab)
    StrTab = &addSection<StringTableSection>();

  SymbolTableSection &SymTab = addSection<SymbolTableSection>();
  SymTab.Name = ".symtab";
  SymTab.Link = StrTab->Index;
  if (Error Err = SymTab.initialize(sections()))
    return Err;
  SymTab.addSymbol("", 0, 0, nullptr, 0, 0, 0, 0);

  SymbolTable = &SymTab;

  return Error::success();
}

// Orders segments such that if x = y->ParentSegment then y comes before x.
static void orderSegments(std::vector<Segment *> &Segments) {
  llvm::stable_sort(Segments, compareSegmentsByOffset);
}

// This function finds a consistent layout for a list of segments starting from
// an Offset. It assumes that Segments have been sorted by orderSegments and
// returns an Offset one past the end of the last segment.
static uint64_t layoutSegments(std::vector<Segment *> &Segments,
                               uint64_t Offset) {
  assert(llvm::is_sorted(Segments, compareSegmentsByOffset));
  // The only way a segment should move is if a section was between two
  // segments and that section was removed. If that section isn't in a segment
  // then it's acceptable, but not ideal, to simply move it to after the
  // segments. So we can simply layout segments one after the other accounting
  // for alignment.
  for (Segment *Seg : Segments) {
    // We assume that segments have been ordered by OriginalOffset and Index
    // such that a parent segment will always come before a child segment in
    // OrderedSegments. This means that the Offset of the ParentSegment should
    // already be set and we can set our offset relative to it.
    if (Seg->ParentSegment != nullptr) {
      Segment *Parent = Seg->ParentSegment;
      Seg->Offset =
          Parent->Offset + Seg->OriginalOffset - Parent->OriginalOffset;
    } else {
      Seg->Offset =
          alignTo(Offset, std::max<uint64_t>(Seg->Align, 1), Seg->VAddr);
    }
    Offset = std::max(Offset, Seg->Offset + Seg->FileSize);
  }
  return Offset;
}

// This function finds a consistent layout for a list of sections. It assumes
// that the ->ParentSegment of each section has already been laid out. The
// supplied starting Offset is used for the starting offset of any section that
// does not have a ParentSegment. It returns either the offset given if all
// sections had a ParentSegment or an offset one past the last section if there
// was a section that didn't have a ParentSegment.
template <class Range>
static uint64_t layoutSections(Range Sections, uint64_t Offset) {
  // Now the offset of every segment has been set we can assign the offsets
  // of each section. For sections that are covered by a segment we should use
  // the segment's original offset and the section's original offset to compute
  // the offset from the start of the segment. Using the offset from the start
  // of the segment we can assign a new offset to the section. For sections not
  // covered by segments we can just bump Offset to the next valid location.
  // While it is not necessary, layout the sections in the order based on their
  // original offsets to resemble the input file as close as possible.
  std::vector<SectionBase *> OutOfSegmentSections;
  uint32_t Index = 1;
  for (auto &Sec : Sections) {
    Sec.Index = Index++;
    if (Sec.ParentSegment != nullptr) {
      const Segment &Segment = *Sec.ParentSegment;
      Sec.Offset =
          Segment.Offset + (Sec.OriginalOffset - Segment.OriginalOffset);
    } else
      OutOfSegmentSections.push_back(&Sec);
  }

  llvm::stable_sort(OutOfSegmentSections,
                    [](const SectionBase *Lhs, const SectionBase *Rhs) {
                      return Lhs->OriginalOffset < Rhs->OriginalOffset;
                    });
  for (auto *Sec : OutOfSegmentSections) {
    Offset = alignTo(Offset, Sec->Align == 0 ? 1 : Sec->Align);
    Sec->Offset = Offset;
    if (Sec->Type != SHT_NOBITS)
      Offset += Sec->Size;
  }
  return Offset;
}

// Rewrite sh_offset after some sections are changed to SHT_NOBITS and thus
// occupy no space in the file.
static uint64_t layoutSectionsForOnlyKeepDebug(Object &Obj, uint64_t Off) {
  // The layout algorithm requires the sections to be handled in the order of
  // their offsets in the input file, at least inside segments.
  std::vector<SectionBase *> Sections;
  Sections.reserve(Obj.sections().size());
  uint32_t Index = 1;
  for (auto &Sec : Obj.sections()) {
    Sec.Index = Index++;
    Sections.push_back(&Sec);
  }
  llvm::stable_sort(Sections,
                    [](const SectionBase *Lhs, const SectionBase *Rhs) {
                      return Lhs->OriginalOffset < Rhs->OriginalOffset;
                    });

  for (auto *Sec : Sections) {
    auto *FirstSec = Sec->ParentSegment && Sec->ParentSegment->Type == PT_LOAD
                         ? Sec->ParentSegment->firstSection()
                         : nullptr;

    // The first section in a PT_LOAD has to have congruent offset and address
    // modulo the alignment, which usually equals the maximum page size.
    if (FirstSec && FirstSec == Sec)
      Off = alignTo(Off, Sec->ParentSegment->Align, Sec->Addr);

    // sh_offset is not significant for SHT_NOBITS sections, but the congruence
    // rule must be followed if it is the first section in a PT_LOAD. Do not
    // advance Off.
    if (Sec->Type == SHT_NOBITS) {
      Sec->Offset = Off;
      continue;
    }

    if (!FirstSec) {
      // FirstSec being nullptr generally means that Sec does not have the
      // SHF_ALLOC flag.
      Off = Sec->Align ? alignTo(Off, Sec->Align) : Off;
    } else if (FirstSec != Sec) {
      // The offset is relative to the first section in the PT_LOAD segment. Use
      // sh_offset for non-SHF_ALLOC sections.
      Off = Sec->OriginalOffset - FirstSec->OriginalOffset + FirstSec->Offset;
    }
    Sec->Offset = Off;
    Off += Sec->Size;
  }
  return Off;
}

// Rewrite p_offset and p_filesz of non-PT_PHDR segments after sh_offset values
// have been updated.
static uint64_t layoutSegmentsForOnlyKeepDebug(std::vector<Segment *> &Segments,
                                               uint64_t HdrEnd) {
  uint64_t MaxOffset = 0;
  for (Segment *Seg : Segments) {
    if (Seg->Type == PT_PHDR)
      continue;

    // The segment offset is generally the offset of the first section.
    //
    // For a segment containing no section (see sectionWithinSegment), if it has
    // a parent segment, copy the parent segment's offset field. This works for
    // empty PT_TLS. If no parent segment, use 0: the segment is not useful for
    // debugging anyway.
    const SectionBase *FirstSec = Seg->firstSection();
    uint64_t Offset =
        FirstSec ? FirstSec->Offset
                 : (Seg->ParentSegment ? Seg->ParentSegment->Offset : 0);
    uint64_t FileSize = 0;
    for (const SectionBase *Sec : Seg->Sections) {
      uint64_t Size = Sec->Type == SHT_NOBITS ? 0 : Sec->Size;
      if (Sec->Offset + Size > Offset)
        FileSize = std::max(FileSize, Sec->Offset + Size - Offset);
    }

    // If the segment includes EHDR and program headers, don't make it smaller
    // than the headers.
    if (Seg->Offset < HdrEnd && HdrEnd <= Seg->Offset + Seg->FileSize) {
      FileSize += Offset - Seg->Offset;
      Offset = Seg->Offset;
      FileSize = std::max(FileSize, HdrEnd - Offset);
    }

    Seg->Offset = Offset;
    Seg->FileSize = FileSize;
    MaxOffset = std::max(MaxOffset, Offset + FileSize);
  }
  return MaxOffset;
}

template <class ELFT> void ELFWriter<ELFT>::initEhdrSegment() {
  Segment &ElfHdr = Obj.ElfHdrSegment;
  ElfHdr.Type = PT_PHDR;
  ElfHdr.Flags = 0;
  ElfHdr.VAddr = 0;
  ElfHdr.PAddr = 0;
  ElfHdr.FileSize = ElfHdr.MemSize = sizeof(Elf_Ehdr);
  ElfHdr.Align = 0;
}

template <class ELFT> void ELFWriter<ELFT>::assignOffsets() {
  // We need a temporary list of segments that has a special order to it
  // so that we know that anytime ->ParentSegment is set that segment has
  // already had its offset properly set.
  std::vector<Segment *> OrderedSegments;
  for (Segment &Segment : Obj.segments())
    OrderedSegments.push_back(&Segment);
  OrderedSegments.push_back(&Obj.ElfHdrSegment);
  OrderedSegments.push_back(&Obj.ProgramHdrSegment);
  orderSegments(OrderedSegments);

  uint64_t Offset;
  if (OnlyKeepDebug) {
    // For --only-keep-debug, the sections that did not preserve contents were
    // changed to SHT_NOBITS. We now rewrite sh_offset fields of sections, and
    // then rewrite p_offset/p_filesz of program headers.
    uint64_t HdrEnd =
        sizeof(Elf_Ehdr) + llvm::size(Obj.segments()) * sizeof(Elf_Phdr);
    Offset = layoutSectionsForOnlyKeepDebug(Obj, HdrEnd);
    Offset = std::max(Offset,
                      layoutSegmentsForOnlyKeepDebug(OrderedSegments, HdrEnd));
  } else {
    // Offset is used as the start offset of the first segment to be laid out.
    // Since the ELF Header (ElfHdrSegment) must be at the start of the file,
    // we start at offset 0.
    Offset = layoutSegments(OrderedSegments, 0);
    Offset = layoutSections(Obj.sections(), Offset);
  }
  // If we need to write the section header table out then we need to align the
  // Offset so that SHOffset is valid.
  if (WriteSectionHeaders)
    Offset = alignTo(Offset, sizeof(Elf_Addr));
  Obj.SHOff = Offset;
}

template <class ELFT> size_t ELFWriter<ELFT>::totalSize() const {
  // We already have the section header offset so we can calculate the total
  // size by just adding up the size of each section header.
  if (!WriteSectionHeaders)
    return Obj.SHOff;
  size_t ShdrCount = Obj.sections().size() + 1; // Includes null shdr.
  return Obj.SHOff + ShdrCount * sizeof(Elf_Shdr);
}

template <class ELFT> Error ELFWriter<ELFT>::write() {
  // Segment data must be written first, so that the ELF header and program
  // header tables can overwrite it, if covered by a segment.
  writeSegmentData();
  writeEhdr();
  writePhdrs();
  if (Error E = writeSectionData())
    return E;
  if (WriteSectionHeaders)
    writeShdrs();

  // TODO: Implement direct writing to the output stream (without intermediate
  // memory buffer Buf).
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

static Error removeUnneededSections(Object &Obj) {
  // We can remove an empty symbol table from non-relocatable objects.
  // Relocatable objects typically have relocation sections whose
  // sh_link field points to .symtab, so we can't remove .symtab
  // even if it is empty.
  if (Obj.isRelocatable() || Obj.SymbolTable == nullptr ||
      !Obj.SymbolTable->empty())
    return Error::success();

  // .strtab can be used for section names. In such a case we shouldn't
  // remove it.
  auto *StrTab = Obj.SymbolTable->getStrTab() == Obj.SectionNames
                     ? nullptr
                     : Obj.SymbolTable->getStrTab();
  return Obj.removeSections(false, [&](const SectionBase &Sec) {
    return &Sec == Obj.SymbolTable || &Sec == StrTab;
  });
}

template <class ELFT> Error ELFWriter<ELFT>::finalize() {
  // It could happen that SectionNames has been removed and yet the user wants
  // a section header table output. We need to throw an error if a user tries
  // to do that.
  if (Obj.SectionNames == nullptr && WriteSectionHeaders)
    return createStringError(llvm::errc::invalid_argument,
                             "cannot write section header table because "
                             "section header string table was removed");

  if (Error E = removeUnneededSections(Obj))
    return E;

  // If the .symtab indices have not been changed, restore the sh_link to
  // .symtab for sections that were linked to .symtab.
  if (Obj.SymbolTable && !Obj.SymbolTable->indicesChanged())
    for (SectionBase &Sec : Obj.sections())
      Sec.restoreSymTabLink(*Obj.SymbolTable);

  // We need to assign indexes before we perform layout because we need to know
  // if we need large indexes or not. We can assign indexes first and check as
  // we go to see if we will actully need large indexes.
  bool NeedsLargeIndexes = false;
  if (Obj.sections().size() >= SHN_LORESERVE) {
    SectionTableRef Sections = Obj.sections();
    // Sections doesn't include the null section header, so account for this
    // when skipping the first N sections.
    NeedsLargeIndexes =
        any_of(drop_begin(Sections, SHN_LORESERVE - 1),
               [](const SectionBase &Sec) { return Sec.HasSymbol; });
    // TODO: handle case where only one section needs the large index table but
    // only needs it because the large index table hasn't been removed yet.
  }

  if (NeedsLargeIndexes) {
    // This means we definitely need to have a section index table but if we
    // already have one then we should use it instead of making a new one.
    if (Obj.SymbolTable != nullptr && Obj.SectionIndexTable == nullptr) {
      // Addition of a section to the end does not invalidate the indexes of
      // other sections and assigns the correct index to the new section.
      auto &Shndx = Obj.addSection<SectionIndexSection>();
      Obj.SymbolTable->setShndxTable(&Shndx);
      Shndx.setSymTab(Obj.SymbolTable);
    }
  } else {
    // Since we don't need SectionIndexTable we should remove it and all
    // references to it.
    if (Obj.SectionIndexTable != nullptr) {
      // We do not support sections referring to the section index table.
      if (Error E = Obj.removeSections(false /*AllowBrokenLinks*/,
                                       [this](const SectionBase &Sec) {
                                         return &Sec == Obj.SectionIndexTable;
                                       }))
        return E;
    }
  }

  // Make sure we add the names of all the sections. Importantly this must be
  // done after we decide to add or remove SectionIndexes.
  if (Obj.SectionNames != nullptr)
    for (const SectionBase &Sec : Obj.sections())
      Obj.SectionNames->addString(Sec.Name);

  initEhdrSegment();

  // Before we can prepare for layout the indexes need to be finalized.
  // Also, the output arch may not be the same as the input arch, so fix up
  // size-related fields before doing layout calculations.
  uint64_t Index = 0;
  auto SecSizer = std::make_unique<ELFSectionSizer<ELFT>>();
  for (SectionBase &Sec : Obj.sections()) {
    Sec.Index = Index++;
    if (Error Err = Sec.accept(*SecSizer))
      return Err;
  }

  // The symbol table does not update all other sections on update. For
  // instance, symbol names are not added as new symbols are added. This means
  // that some sections, like .strtab, don't yet have their final size.
  if (Obj.SymbolTable != nullptr)
    Obj.SymbolTable->prepareForLayout();

  // Now that all strings are added we want to finalize string table builders,
  // because that affects section sizes which in turn affects section offsets.
  for (SectionBase &Sec : Obj.sections())
    if (auto StrTab = dyn_cast<StringTableSection>(&Sec))
      StrTab->prepareForLayout();

  assignOffsets();

  // layoutSections could have modified section indexes, so we need
  // to fill the index table after assignOffsets.
  if (Obj.SymbolTable != nullptr)
    Obj.SymbolTable->fillShndxTable();

  // Finally now that all offsets and indexes have been set we can finalize any
  // remaining issues.
  uint64_t Offset = Obj.SHOff + sizeof(Elf_Shdr);
  for (SectionBase &Sec : Obj.sections()) {
    Sec.HeaderOffset = Offset;
    Offset += sizeof(Elf_Shdr);
    if (WriteSectionHeaders)
      Sec.NameIndex = Obj.SectionNames->findIndex(Sec.Name);
    Sec.finalize();
  }

  size_t TotalSize = totalSize();
  Buf = WritableMemoryBuffer::getNewMemBuffer(TotalSize);
  if (!Buf)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of " +
                                 Twine::utohexstr(TotalSize) + " bytes");

  SecWriter = std::make_unique<ELFSectionWriter<ELFT>>(*Buf);
  return Error::success();
}

Error BinaryWriter::write() {
  SmallVector<const SectionBase *, 30> SectionsToWrite;
  for (const SectionBase &Sec : Obj.allocSections()) {
    if (Sec.Type != SHT_NOBITS && Sec.Size > 0)
      SectionsToWrite.push_back(&Sec);
  }

  if (SectionsToWrite.empty())
    return Error::success();

  llvm::stable_sort(SectionsToWrite,
                    [](const SectionBase *LHS, const SectionBase *RHS) {
                      return LHS->Offset < RHS->Offset;
                    });

  assert(SectionsToWrite.front()->Offset == 0);

  for (size_t i = 0; i != SectionsToWrite.size(); ++i) {
    const SectionBase &Sec = *SectionsToWrite[i];
    if (Error Err = Sec.accept(*SecWriter))
      return Err;
    if (GapFill == 0)
      continue;
    uint64_t PadOffset = (i < SectionsToWrite.size() - 1)
                             ? SectionsToWrite[i + 1]->Offset
                             : Buf->getBufferSize();
    assert(PadOffset <= Buf->getBufferSize());
    assert(Sec.Offset + Sec.Size <= PadOffset);
    std::fill(Buf->getBufferStart() + Sec.Offset + Sec.Size,
              Buf->getBufferStart() + PadOffset, GapFill);
  }

  // TODO: Implement direct writing to the output stream (without intermediate
  // memory buffer Buf).
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

Error BinaryWriter::finalize() {
  // Compute the section LMA based on its sh_offset and the containing segment's
  // p_offset and p_paddr. Also compute the minimum LMA of all non-empty
  // sections as MinAddr. In the output, the contents between address 0 and
  // MinAddr will be skipped.
  uint64_t MinAddr = UINT64_MAX;
  for (SectionBase &Sec : Obj.allocSections()) {
    if (Sec.ParentSegment != nullptr)
      Sec.Addr =
          Sec.Offset - Sec.ParentSegment->Offset + Sec.ParentSegment->PAddr;
    if (Sec.Type != SHT_NOBITS && Sec.Size > 0)
      MinAddr = std::min(MinAddr, Sec.Addr);
  }

  // Now that every section has been laid out we just need to compute the total
  // file size. This might not be the same as the offset returned by
  // layoutSections, because we want to truncate the last segment to the end of
  // its last non-empty section, to match GNU objcopy's behaviour.
  TotalSize = PadTo > MinAddr ? PadTo - MinAddr : 0;
  for (SectionBase &Sec : Obj.allocSections())
    if (Sec.Type != SHT_NOBITS && Sec.Size > 0) {
      Sec.Offset = Sec.Addr - MinAddr;
      TotalSize = std::max(TotalSize, Sec.Offset + Sec.Size);
    }

  Buf = WritableMemoryBuffer::getNewMemBuffer(TotalSize);
  if (!Buf)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of " +
                                 Twine::utohexstr(TotalSize) + " bytes");
  SecWriter = std::make_unique<BinarySectionWriter>(*Buf);
  return Error::success();
}

Error ASCIIHexWriter::checkSection(const SectionBase &S) const {
  if (addressOverflows32bit(S.Addr) ||
      addressOverflows32bit(S.Addr + S.Size - 1))
    return createStringError(
        errc::invalid_argument,
        "section '%s' address range [0x%llx, 0x%llx] is not 32 bit",
        S.Name.c_str(), S.Addr, S.Addr + S.Size - 1);
  return Error::success();
}

Error ASCIIHexWriter::finalize() {
  // We can't write 64-bit addresses.
  if (addressOverflows32bit(Obj.Entry))
    return createStringError(errc::invalid_argument,
                             "entry point address 0x%llx overflows 32 bits",
                             Obj.Entry);

  for (const SectionBase &S : Obj.sections()) {
    if ((S.Flags & ELF::SHF_ALLOC) && S.Type != ELF::SHT_NOBITS && S.Size > 0) {
      if (Error E = checkSection(S))
        return E;
      Sections.push_back(&S);
    }
  }

  llvm::sort(Sections, [](const SectionBase *A, const SectionBase *B) {
    return sectionPhysicalAddr(A) < sectionPhysicalAddr(B);
  });

  std::unique_ptr<WritableMemoryBuffer> EmptyBuffer =
      WritableMemoryBuffer::getNewMemBuffer(0);
  if (!EmptyBuffer)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of 0 bytes");

  Expected<size_t> ExpTotalSize = getTotalSize(*EmptyBuffer);
  if (!ExpTotalSize)
    return ExpTotalSize.takeError();
  TotalSize = *ExpTotalSize;

  Buf = WritableMemoryBuffer::getNewMemBuffer(TotalSize);
  if (!Buf)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of 0x" +
                                 Twine::utohexstr(TotalSize) + " bytes");
  return Error::success();
}

uint64_t IHexWriter::writeEntryPointRecord(uint8_t *Buf) {
  IHexLineData HexData;
  uint8_t Data[4] = {};
  // We don't write entry point record if entry is zero.
  if (Obj.Entry == 0)
    return 0;

  if (Obj.Entry <= 0xFFFFFU) {
    Data[0] = ((Obj.Entry & 0xF0000U) >> 12) & 0xFF;
    support::endian::write(&Data[2], static_cast<uint16_t>(Obj.Entry),
                           llvm::endianness::big);
    HexData = IHexRecord::getLine(IHexRecord::StartAddr80x86, 0, Data);
  } else {
    support::endian::write(Data, static_cast<uint32_t>(Obj.Entry),
                           llvm::endianness::big);
    HexData = IHexRecord::getLine(IHexRecord::StartAddr, 0, Data);
  }
  memcpy(Buf, HexData.data(), HexData.size());
  return HexData.size();
}

uint64_t IHexWriter::writeEndOfFileRecord(uint8_t *Buf) {
  IHexLineData HexData = IHexRecord::getLine(IHexRecord::EndOfFile, 0, {});
  memcpy(Buf, HexData.data(), HexData.size());
  return HexData.size();
}

Expected<size_t>
IHexWriter::getTotalSize(WritableMemoryBuffer &EmptyBuffer) const {
  IHexSectionWriterBase LengthCalc(EmptyBuffer);
  for (const SectionBase *Sec : Sections)
    if (Error Err = Sec->accept(LengthCalc))
      return std::move(Err);

  // We need space to write section records + StartAddress record
  // (if start adress is not zero) + EndOfFile record.
  return LengthCalc.getBufferOffset() +
         (Obj.Entry ? IHexRecord::getLineLength(4) : 0) +
         IHexRecord::getLineLength(0);
}

Error IHexWriter::write() {
  IHexSectionWriter Writer(*Buf);
  // Write sections.
  for (const SectionBase *Sec : Sections)
    if (Error Err = Sec->accept(Writer))
      return Err;

  uint64_t Offset = Writer.getBufferOffset();
  // Write entry point address.
  Offset += writeEntryPointRecord(
      reinterpret_cast<uint8_t *>(Buf->getBufferStart()) + Offset);
  // Write EOF.
  Offset += writeEndOfFileRecord(
      reinterpret_cast<uint8_t *>(Buf->getBufferStart()) + Offset);
  assert(Offset == TotalSize);

  // TODO: Implement direct writing to the output stream (without intermediate
  // memory buffer Buf).
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

Error SRECSectionWriterBase::visit(const StringTableSection &Sec) {
  // Check that the sizer has already done its work.
  assert(Sec.Size == Sec.StrTabBuilder.getSize() &&
         "Expected section size to have been finalized");
  // We don't need to write anything here because the real writer has already
  // done it.
  return Error::success();
}

Error SRECSectionWriterBase::visit(const Section &Sec) {
  writeSection(Sec, Sec.Contents);
  return Error::success();
}

Error SRECSectionWriterBase::visit(const OwnedDataSection &Sec) {
  writeSection(Sec, Sec.Data);
  return Error::success();
}

Error SRECSectionWriterBase::visit(const DynamicRelocationSection &Sec) {
  writeSection(Sec, Sec.Contents);
  return Error::success();
}

void SRECSectionWriter::writeRecord(SRecord &Record, uint64_t Off) {
  SRecLineData Data = Record.toString();
  memcpy(Out.getBufferStart() + Off, Data.data(), Data.size());
}

void SRECSectionWriterBase::writeRecords(uint32_t Entry) {
  // The ELF header could contain an entry point outside of the sections we have
  // seen that does not fit the current record Type.
  Type = std::max(Type, SRecord::getType(Entry));
  uint64_t Off = HeaderSize;
  for (SRecord &Record : Records) {
    Record.Type = Type;
    writeRecord(Record, Off);
    Off += Record.getSize();
  }
  Offset = Off;
}

void SRECSectionWriterBase::writeSection(const SectionBase &S,
                                         ArrayRef<uint8_t> Data) {
  const uint32_t ChunkSize = 16;
  uint32_t Address = sectionPhysicalAddr(&S);
  uint32_t EndAddr = Address + S.Size - 1;
  Type = std::max(SRecord::getType(EndAddr), Type);
  while (!Data.empty()) {
    uint64_t DataSize = std::min<uint64_t>(Data.size(), ChunkSize);
    SRecord Record{Type, Address, Data.take_front(DataSize)};
    Records.push_back(Record);
    Data = Data.drop_front(DataSize);
    Address += DataSize;
  }
}

Error SRECSectionWriter::visit(const StringTableSection &Sec) {
  assert(Sec.Size == Sec.StrTabBuilder.getSize() &&
         "Section size does not match the section's string table builder size");
  std::vector<uint8_t> Data(Sec.Size);
  Sec.StrTabBuilder.write(Data.data());
  writeSection(Sec, Data);
  return Error::success();
}

SRecLineData SRecord::toString() const {
  SRecLineData Line(getSize());
  auto *Iter = Line.begin();
  *Iter++ = 'S';
  *Iter++ = '0' + Type;
  // Write 1 byte (2 hex characters) record count.
  Iter = toHexStr(getCount(), Iter, 2);
  // Write the address field with length depending on record type.
  Iter = toHexStr(Address, Iter, getAddressSize());
  // Write data byte by byte.
  for (uint8_t X : Data)
    Iter = toHexStr(X, Iter, 2);
  // Write the 1 byte checksum.
  Iter = toHexStr(getChecksum(), Iter, 2);
  *Iter++ = '\r';
  *Iter++ = '\n';
  assert(Iter == Line.end());
  return Line;
}

uint8_t SRecord::getChecksum() const {
  uint32_t Sum = getCount();
  Sum += (Address >> 24) & 0xFF;
  Sum += (Address >> 16) & 0xFF;
  Sum += (Address >> 8) & 0xFF;
  Sum += Address & 0xFF;
  for (uint8_t Byte : Data)
    Sum += Byte;
  return 0xFF - (Sum & 0xFF);
}

size_t SRecord::getSize() const {
  // Type, Count, Checksum, and CRLF are two characters each.
  return 2 + 2 + getAddressSize() + Data.size() * 2 + 2 + 2;
}

uint8_t SRecord::getAddressSize() const {
  switch (Type) {
  case Type::S2:
    return 6;
  case Type::S3:
    return 8;
  case Type::S7:
    return 8;
  case Type::S8:
    return 6;
  default:
    return 4;
  }
}

uint8_t SRecord::getCount() const {
  uint8_t DataSize = Data.size();
  uint8_t ChecksumSize = 1;
  return getAddressSize() / 2 + DataSize + ChecksumSize;
}

uint8_t SRecord::getType(uint32_t Address) {
  if (isUInt<16>(Address))
    return SRecord::S1;
  if (isUInt<24>(Address))
    return SRecord::S2;
  return SRecord::S3;
}

SRecord SRecord::getHeader(StringRef FileName) {
  // Header is a record with Type S0, Address 0, and Data that is a
  // vendor-specific text comment. For the comment we will use the output file
  // name truncated to 40 characters to match the behavior of GNU objcopy.
  StringRef HeaderContents = FileName.slice(0, 40);
  ArrayRef<uint8_t> Data(
      reinterpret_cast<const uint8_t *>(HeaderContents.data()),
      HeaderContents.size());
  return {SRecord::S0, 0, Data};
}

size_t SRECWriter::writeHeader(uint8_t *Buf) {
  SRecLineData Record = SRecord::getHeader(OutputFileName).toString();
  memcpy(Buf, Record.data(), Record.size());
  return Record.size();
}

size_t SRECWriter::writeTerminator(uint8_t *Buf, uint8_t Type) {
  assert(Type >= SRecord::S7 && Type <= SRecord::S9 &&
         "Invalid record type for terminator");
  uint32_t Entry = Obj.Entry;
  SRecLineData Data = SRecord{Type, Entry, {}}.toString();
  memcpy(Buf, Data.data(), Data.size());
  return Data.size();
}

Expected<size_t>
SRECWriter::getTotalSize(WritableMemoryBuffer &EmptyBuffer) const {
  SRECSizeCalculator SizeCalc(EmptyBuffer, 0);
  for (const SectionBase *Sec : Sections)
    if (Error Err = Sec->accept(SizeCalc))
      return std::move(Err);

  SizeCalc.writeRecords(Obj.Entry);
  // We need to add the size of the Header and Terminator records.
  SRecord Header = SRecord::getHeader(OutputFileName);
  uint8_t TerminatorType = 10 - SizeCalc.getType();
  SRecord Terminator = {TerminatorType, static_cast<uint32_t>(Obj.Entry), {}};
  return Header.getSize() + SizeCalc.getBufferOffset() + Terminator.getSize();
}

Error SRECWriter::write() {
  uint32_t HeaderSize =
      writeHeader(reinterpret_cast<uint8_t *>(Buf->getBufferStart()));
  SRECSectionWriter Writer(*Buf, HeaderSize);
  for (const SectionBase *S : Sections) {
    if (Error E = S->accept(Writer))
      return E;
  }
  Writer.writeRecords(Obj.Entry);
  uint64_t Offset = Writer.getBufferOffset();

  // An S1 record terminates with an S9 record, S2 with S8, and S3 with S7.
  uint8_t TerminatorType = 10 - Writer.getType();
  Offset += writeTerminator(
      reinterpret_cast<uint8_t *>(Buf->getBufferStart() + Offset),
      TerminatorType);
  assert(Offset == TotalSize);
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

namespace llvm {
namespace objcopy {
namespace elf {

template class ELFBuilder<ELF64LE>;
template class ELFBuilder<ELF64BE>;
template class ELFBuilder<ELF32LE>;
template class ELFBuilder<ELF32BE>;

template class ELFWriter<ELF64LE>;
template class ELFWriter<ELF64BE>;
template class ELFWriter<ELF32LE>;
template class ELFWriter<ELF32BE>;

} // end namespace elf
} // end namespace objcopy
} // end namespace llvm
