//===------------ MachOBuilder.h -- Build MachO Objects ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Build MachO object files for interaction with the ObjC runtime and debugger.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H
#define LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H

#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

#include <list>
#include <map>
#include <vector>

namespace llvm {
namespace orc {

template <typename MachOStruct>
size_t writeMachOStruct(MutableArrayRef<char> Buf, size_t Offset, MachOStruct S,
                        bool SwapStruct) {
  if (SwapStruct)
    MachO::swapStruct(S);
  assert(Offset + sizeof(MachOStruct) <= Buf.size() && "Buffer overflow");
  memcpy(&Buf[Offset], reinterpret_cast<const char *>(&S), sizeof(MachOStruct));
  return Offset + sizeof(MachOStruct);
}

/// Base type for MachOBuilder load command wrappers.
struct MachOBuilderLoadCommandBase {
  virtual ~MachOBuilderLoadCommandBase() {}
  virtual size_t size() const = 0;
  virtual size_t write(MutableArrayRef<char> Buf, size_t Offset,
                       bool SwapStruct) = 0;
};

/// MachOBuilder load command wrapper type.
template <MachO::LoadCommandType LCType> struct MachOBuilderLoadCommandImplBase;

#define HANDLE_LOAD_COMMAND(Name, Value, LCStruct)                             \
  template <>                                                                  \
  struct MachOBuilderLoadCommandImplBase<MachO::Name>                          \
      : public MachO::LCStruct, public MachOBuilderLoadCommandBase {           \
    using CmdStruct = LCStruct;                                                \
    MachOBuilderLoadCommandImplBase() {                                        \
      memset(&rawStruct(), 0, sizeof(CmdStruct));                              \
      cmd = Value;                                                             \
      cmdsize = sizeof(CmdStruct);                                             \
    }                                                                          \
    template <typename... ArgTs>                                               \
    MachOBuilderLoadCommandImplBase(ArgTs &&...Args)                           \
        : CmdStruct{Value, sizeof(CmdStruct), std::forward<ArgTs>(Args)...} {} \
    CmdStruct &rawStruct() { return static_cast<CmdStruct &>(*this); }         \
    size_t size() const override { return cmdsize; }                           \
    size_t write(MutableArrayRef<char> Buf, size_t Offset,                     \
                 bool SwapStruct) override {                                   \
      return writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);           \
    }                                                                          \
  };

#include "llvm/BinaryFormat/MachO.def"

#undef HANDLE_LOAD_COMMAND

template <MachO::LoadCommandType LCType>
struct MachOBuilderLoadCommand
    : public MachOBuilderLoadCommandImplBase<LCType> {
public:
  MachOBuilderLoadCommand() = default;

  template <typename... ArgTs>
  MachOBuilderLoadCommand(ArgTs &&...Args)
      : MachOBuilderLoadCommandImplBase<LCType>(std::forward<ArgTs>(Args)...) {}
};

template <>
struct MachOBuilderLoadCommand<MachO::LC_ID_DYLIB>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_ID_DYLIB> {

  MachOBuilderLoadCommand(std::string Name, uint32_t Timestamp,
                          uint32_t CurrentVersion,
                          uint32_t CompatibilityVersion)
      : MachOBuilderLoadCommandImplBase(
            MachO::dylib{24, Timestamp, CurrentVersion, CompatibilityVersion}),
        Name(std::move(Name)) {
    cmdsize += (this->Name.size() + 1 + 3) & ~0x3;
  }

  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, Name.data());
    return Offset + ((Name.size() + 1 + 3) & ~0x3);
  }

  std::string Name;
};

template <>
struct MachOBuilderLoadCommand<MachO::LC_LOAD_DYLIB>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_LOAD_DYLIB> {

  MachOBuilderLoadCommand(std::string Name, uint32_t Timestamp,
                          uint32_t CurrentVersion,
                          uint32_t CompatibilityVersion)
      : MachOBuilderLoadCommandImplBase(
            MachO::dylib{24, Timestamp, CurrentVersion, CompatibilityVersion}),
        Name(std::move(Name)) {
    cmdsize += (this->Name.size() + 1 + 3) & ~0x3;
  }

  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, Name.data());
    return Offset + ((Name.size() + 1 + 3) & ~0x3);
  }

  std::string Name;
};

template <>
struct MachOBuilderLoadCommand<MachO::LC_RPATH>
    : public MachOBuilderLoadCommandImplBase<MachO::LC_RPATH> {
  MachOBuilderLoadCommand(std::string Path)
      : MachOBuilderLoadCommandImplBase(12u), Path(std::move(Path)) {
    cmdsize += (this->Path.size() + 1 + 3) & ~0x3;
  }

  size_t write(MutableArrayRef<char> Buf, size_t Offset,
               bool SwapStruct) override {
    Offset = writeMachOStruct(Buf, Offset, rawStruct(), SwapStruct);
    strcpy(Buf.data() + Offset, Path.data());
    return Offset + ((Path.size() + 1 + 3) & ~0x3);
  }

  std::string Path;
};

// Builds MachO objects.
template <typename MachOTraits> class MachOBuilder {
private:
  struct SymbolContainer {
    size_t SymbolIndexBase = 0;
    std::vector<typename MachOTraits::NList> Symbols;
  };

  struct StringTableEntry {
    StringRef S;
    size_t Offset;
  };

  using StringTable = std::vector<StringTableEntry>;

  static bool swapStruct() {
    return MachOTraits::Endianness != llvm::endianness::native;
  }

public:
  using StringId = size_t;

  struct Section;

  // Points to either an nlist entry (as a (symbol-container, index) pair), or
  // a section.
  class RelocTarget {
  public:
    RelocTarget(const Section &S) : S(&S), Idx(~0U) {}
    RelocTarget(SymbolContainer &SC, size_t Idx) : SC(&SC), Idx(Idx) {}

    bool isSymbol() { return Idx != ~0U; }

    uint32_t getSymbolNum() {
      assert(isSymbol() && "Target is not a symbol");
      return SC->SymbolIndexBase + Idx;
    }

    uint32_t getSectionId() {
      assert(!isSymbol() && "Target is not a section");
      return S->SectionNumber;
    }

    typename MachOTraits::NList &nlist() {
      assert(isSymbol() && "Target is not a symbol");
      return SC->Symbols[Idx];
    }

  private:
    union {
      const Section *S;
      SymbolContainer *SC;
    };
    size_t Idx;
  };

  struct Reloc : public MachO::relocation_info {
    RelocTarget Target;

    Reloc(int32_t Offset, RelocTarget Target, bool PCRel, unsigned Length,
          unsigned Type)
        : Target(Target) {
      assert(Type < 16 && "Relocation type out of range");
      r_address = Offset; // Will slide to account for sec addr during layout
      r_symbolnum = 0;
      r_pcrel = PCRel;
      r_length = Length;
      r_extern = Target.isSymbol();
      r_type = Type;
    }

    MachO::relocation_info &rawStruct() {
      return static_cast<MachO::relocation_info &>(*this);
    }
  };

  struct SectionContent {
    const char *Data = nullptr;
    size_t Size = 0;
  };

  struct Section : public MachOTraits::Section, public RelocTarget {
    MachOBuilder &Builder;
    SectionContent Content;
    size_t SectionNumber = 0;
    SymbolContainer SC;
    std::vector<Reloc> Relocs;

    Section(MachOBuilder &Builder, StringRef SecName, StringRef SegName)
        : RelocTarget(*this), Builder(Builder) {
      memset(&rawStruct(), 0, sizeof(typename MachOTraits::Section));
      assert(SecName.size() <= 16 && "SecName too long");
      assert(SegName.size() <= 16 && "SegName too long");
      memcpy(this->sectname, SecName.data(), SecName.size());
      memcpy(this->segname, SegName.data(), SegName.size());
    }

    RelocTarget addSymbol(int32_t Offset, StringRef Name, uint8_t Type,
                          uint16_t Desc) {
      StringId SI = Builder.addString(Name);
      typename MachOTraits::NList Sym;
      Sym.n_strx = SI;
      Sym.n_type = Type | MachO::N_SECT;
      Sym.n_sect = MachO::NO_SECT; // Will be filled in later.
      Sym.n_desc = Desc;
      Sym.n_value = Offset;
      SC.Symbols.push_back(Sym);
      return {SC, SC.Symbols.size() - 1};
    }

    void addReloc(int32_t Offset, RelocTarget Target, bool PCRel,
                  unsigned Length, unsigned Type) {
      Relocs.push_back({Offset, Target, PCRel, Length, Type});
    }

    auto &rawStruct() {
      return static_cast<typename MachOTraits::Section &>(*this);
    }
  };

  struct Segment : public MachOBuilderLoadCommand<MachOTraits::SegmentCmd> {
    MachOBuilder &Builder;
    std::vector<std::unique_ptr<Section>> Sections;

    Segment(MachOBuilder &Builder, StringRef SegName)
        : MachOBuilderLoadCommand<MachOTraits::SegmentCmd>(), Builder(Builder) {
      assert(SegName.size() <= 16 && "SegName too long");
      memcpy(this->segname, SegName.data(), SegName.size());
      this->maxprot =
          MachO::VM_PROT_READ | MachO::VM_PROT_WRITE | MachO::VM_PROT_EXECUTE;
      this->initprot = this->maxprot;
    }

    Section &addSection(StringRef SecName, StringRef SegName) {
      Sections.push_back(std::make_unique<Section>(Builder, SecName, SegName));
      return *Sections.back();
    }

    size_t write(MutableArrayRef<char> Buf, size_t Offset,
                 bool SwapStruct) override {
      Offset = MachOBuilderLoadCommand<MachOTraits::SegmentCmd>::write(
          Buf, Offset, SwapStruct);
      for (auto &Sec : Sections)
        Offset = writeMachOStruct(Buf, Offset, Sec->rawStruct(), SwapStruct);
      return Offset;
    }
  };

  MachOBuilder(size_t PageSize) : PageSize(PageSize) {
    memset((char *)&Header, 0, sizeof(Header));
    Header.magic = MachOTraits::Magic;
  }

  template <MachO::LoadCommandType LCType, typename... ArgTs>
  MachOBuilderLoadCommand<LCType> &addLoadCommand(ArgTs &&...Args) {
    static_assert(LCType != MachOTraits::SegmentCmd,
                  "Use addSegment to add segment load command");
    auto LC = std::make_unique<MachOBuilderLoadCommand<LCType>>(
        std::forward<ArgTs>(Args)...);
    auto &Tmp = *LC;
    LoadCommands.push_back(std::move(LC));
    return Tmp;
  }

  StringId addString(StringRef Str) {
    if (Strings.empty() && !Str.empty())
      addString("");
    return Strings.insert(std::make_pair(Str, Strings.size())).first->second;
  }

  Segment &addSegment(StringRef SegName) {
    Segments.push_back(Segment(*this, SegName));
    return Segments.back();
  }

  RelocTarget addSymbol(StringRef Name, uint8_t Type, uint8_t Sect,
                        uint16_t Desc, typename MachOTraits::UIntPtr Value) {
    StringId SI = addString(Name);
    typename MachOTraits::NList Sym;
    Sym.n_strx = SI;
    Sym.n_type = Type;
    Sym.n_sect = Sect;
    Sym.n_desc = Desc;
    Sym.n_value = Value;
    SC.Symbols.push_back(Sym);
    return {SC, SC.Symbols.size() - 1};
  }

  // Call to perform layout on the MachO. Returns the total size of the
  // resulting file.
  // This method will automatically insert some load commands (e.g.
  // LC_SYMTAB) and fill in load command fields.
  size_t layout() {

    // Build symbol table and add LC_SYMTAB command.
    makeStringTable();
    MachOBuilderLoadCommand<MachOTraits::SymTabCmd> *SymTabLC = nullptr;
    if (!StrTab.empty())
      SymTabLC = &addLoadCommand<MachOTraits::SymTabCmd>();

    // Lay out header, segment load command, and other load commands.
    size_t Offset = sizeof(Header);
    for (auto &Seg : Segments) {
      Seg.cmdsize +=
          Seg.Sections.size() * sizeof(typename MachOTraits::Section);
      Seg.nsects = Seg.Sections.size();
      Offset += Seg.cmdsize;
    }
    for (auto &LC : LoadCommands)
      Offset += LC->size();

    Header.sizeofcmds = Offset - sizeof(Header);

    // Lay out content, set segment / section addrs and offsets.
    size_t SegVMAddr = 0;
    for (auto &Seg : Segments) {
      Seg.vmaddr = SegVMAddr;
      Seg.fileoff = Offset;
      for (auto &Sec : Seg.Sections) {
        Offset = alignTo(Offset, 1ULL << Sec->align);
        if (Sec->Content.Size)
          Sec->offset = Offset;
        Sec->size = Sec->Content.Size;
        Sec->addr = SegVMAddr + Sec->offset - Seg.fileoff;
        Offset += Sec->Content.Size;
      }
      size_t SegContentSize = Offset - Seg.fileoff;
      Seg.filesize = SegContentSize;
      Seg.vmsize = Header.filetype == MachO::MH_OBJECT
                       ? SegContentSize
                       : alignTo(SegContentSize, PageSize);
      SegVMAddr += Seg.vmsize;
    }

    // Set string table offsets for non-section symbols.
    for (auto &Sym : SC.Symbols)
      Sym.n_strx = StrTab[Sym.n_strx].Offset;

    // Number sections, set symbol section numbers and string table offsets,
    // count relocations.
    size_t NumSymbols = SC.Symbols.size();
    size_t SectionNumber = 0;
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        ++SectionNumber;
        Sec->SectionNumber = SectionNumber;
        Sec->SC.SymbolIndexBase = NumSymbols;
        NumSymbols += Sec->SC.Symbols.size();
        for (auto &Sym : Sec->SC.Symbols) {
          Sym.n_sect = SectionNumber;
          Sym.n_strx = StrTab[Sym.n_strx].Offset;
          Sym.n_value += Sec->addr;
        }
      }
    }

    // Handle relocations
    bool OffsetAlignedForRelocs = false;
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Relocs.empty()) {
          if (!OffsetAlignedForRelocs) {
            Offset = alignTo(Offset, sizeof(MachO::relocation_info));
            OffsetAlignedForRelocs = true;
          }
          Sec->reloff = Offset;
          Sec->nreloc = Sec->Relocs.size();
          Offset += Sec->Relocs.size() * sizeof(MachO::relocation_info);
          for (auto &R : Sec->Relocs)
            R.r_symbolnum = R.Target.isSymbol() ? R.Target.getSymbolNum()
                                                : R.Target.getSectionId();
        }
      }
    }

    // Calculate offset to start of nlist and update symtab command.
    if (NumSymbols > 0) {
      Offset = alignTo(Offset, sizeof(typename MachOTraits::NList));
      SymTabLC->symoff = Offset;
      SymTabLC->nsyms = NumSymbols;

      // Calculate string table bounds and update symtab command.
      if (!StrTab.empty()) {
        Offset += NumSymbols * sizeof(typename MachOTraits::NList);
        size_t StringTableSize =
            StrTab.back().Offset + StrTab.back().S.size() + 1;

        SymTabLC->stroff = Offset;
        SymTabLC->strsize = StringTableSize;
        Offset += StringTableSize;
      }
    }

    return Offset;
  }

  void write(MutableArrayRef<char> Buffer) {
    size_t Offset = 0;
    Offset = writeHeader(Buffer, Offset);
    Offset = writeSegments(Buffer, Offset);
    Offset = writeLoadCommands(Buffer, Offset);
    Offset = writeSectionContent(Buffer, Offset);
    Offset = writeRelocations(Buffer, Offset);
    Offset = writeSymbols(Buffer, Offset);
    Offset = writeStrings(Buffer, Offset);
  }

  typename MachOTraits::Header Header;

private:
  void makeStringTable() {
    if (Strings.empty())
      return;

    StrTab.resize(Strings.size());
    for (auto &KV : Strings)
      StrTab[KV.second] = {KV.first, 0};
    size_t Offset = 0;
    for (auto &Elem : StrTab) {
      Elem.Offset = Offset;
      Offset += Elem.S.size() + 1;
    }
  }

  size_t writeHeader(MutableArrayRef<char> Buf, size_t Offset) {
    Header.ncmds = Segments.size() + LoadCommands.size();
    return writeMachOStruct(Buf, Offset, Header, swapStruct());
  }

  size_t writeSegments(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments)
      Offset = Seg.write(Buf, Offset, swapStruct());
    return Offset;
  }

  size_t writeLoadCommands(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &LC : LoadCommands)
      Offset = LC->write(Buf, Offset, swapStruct());
    return Offset;
  }

  size_t writeSectionContent(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Content.Data) {
          assert(Sec->Relocs.empty() &&
                 "Cant' have relocs for zero-fill segment");
          continue;
        }
        while (Offset != Sec->offset)
          Buf[Offset++] = '\0';

        assert(Offset + Sec->Content.Size <= Buf.size() && "Buffer overflow");
        memcpy(&Buf[Offset], Sec->Content.Data, Sec->Content.Size);
        Offset += Sec->Content.Size;
      }
    }
    return Offset;
  }

  size_t writeRelocations(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        if (!Sec->Relocs.empty()) {
          while (Offset % sizeof(MachO::relocation_info))
            Buf[Offset++] = '\0';
        }
        for (auto &R : Sec->Relocs) {
          assert(Offset + sizeof(MachO::relocation_info) <= Buf.size() &&
                 "Buffer overflow");
          memcpy(&Buf[Offset], reinterpret_cast<const char *>(&R.rawStruct()),
                 sizeof(MachO::relocation_info));
          Offset += sizeof(MachO::relocation_info);
        }
      }
    }
    return Offset;
  }

  size_t writeSymbols(MutableArrayRef<char> Buf, size_t Offset) {

    // Count symbols.
    size_t NumSymbols = SC.Symbols.size();
    for (auto &Seg : Segments)
      for (auto &Sec : Seg.Sections)
        NumSymbols += Sec->SC.Symbols.size();

    // If none then return.
    if (NumSymbols == 0)
      return Offset;

    // Align to nlist entry size.
    while (Offset % sizeof(typename MachOTraits::NList))
      Buf[Offset++] = '\0';

    // Write non-section symbols.
    for (auto &Sym : SC.Symbols)
      Offset = writeMachOStruct(Buf, Offset, Sym, swapStruct());

    // Write section symbols.
    for (auto &Seg : Segments) {
      for (auto &Sec : Seg.Sections) {
        for (auto &Sym : Sec->SC.Symbols) {
          Offset = writeMachOStruct(Buf, Offset, Sym, swapStruct());
        }
      }
    }
    return Offset;
  }

  size_t writeStrings(MutableArrayRef<char> Buf, size_t Offset) {
    for (auto &Elem : StrTab) {
      assert(Offset + Elem.S.size() + 1 <= Buf.size() && "Buffer overflow");
      memcpy(&Buf[Offset], Elem.S.data(), Elem.S.size());
      Offset += Elem.S.size();
      Buf[Offset++] = '\0';
    }
    return Offset;
  }

  size_t PageSize;
  std::list<Segment> Segments;
  std::vector<std::unique_ptr<MachOBuilderLoadCommandBase>> LoadCommands;
  SymbolContainer SC;

  // Maps strings to their "id" (addition order).
  std::map<StringRef, size_t> Strings;
  StringTable StrTab;
};

struct MachO64LE {
  using UIntPtr = uint64_t;
  using Header = MachO::mach_header_64;
  using Section = MachO::section_64;
  using NList = MachO::nlist_64;
  using Relocation = MachO::relocation_info;

  static constexpr llvm::endianness Endianness = llvm::endianness::little;
  static constexpr uint32_t Magic = MachO::MH_MAGIC_64;
  static constexpr MachO::LoadCommandType SegmentCmd = MachO::LC_SEGMENT_64;
  static constexpr MachO::LoadCommandType SymTabCmd = MachO::LC_SYMTAB;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHOBUILDER_H
