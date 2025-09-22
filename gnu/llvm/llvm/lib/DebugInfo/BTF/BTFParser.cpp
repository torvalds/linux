//===- BTFParser.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// BTFParser reads/interprets .BTF and .BTF.ext ELF sections.
// Refer to BTFParser.h for API description.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/BTF/BTFParser.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Errc.h"

#define DEBUG_TYPE "debug-info-btf-parser"

using namespace llvm;
using object::ObjectFile;
using object::SectionedAddress;
using object::SectionRef;

const char BTFSectionName[] = ".BTF";
const char BTFExtSectionName[] = ".BTF.ext";

// Utility class with API similar to raw_ostream but can be cast
// to Error, e.g.:
//
// Error foo(...) {
//   ...
//   if (Error E = bar(...))
//     return Err("error while foo(): ") << E;
//   ...
// }
//
namespace {
class Err {
  std::string Buffer;
  raw_string_ostream Stream;

public:
  Err(const char *InitialMsg) : Buffer(InitialMsg), Stream(Buffer) {}
  Err(const char *SectionName, DataExtractor::Cursor &C)
      : Buffer(), Stream(Buffer) {
    *this << "error while reading " << SectionName
          << " section: " << C.takeError();
  };

  template <typename T> Err &operator<<(T Val) {
    Stream << Val;
    return *this;
  }

  Err &write_hex(unsigned long long Val) {
    Stream.write_hex(Val);
    return *this;
  }

  Err &operator<<(Error Val) {
    handleAllErrors(std::move(Val),
                    [=](ErrorInfoBase &Info) { Stream << Info.message(); });
    return *this;
  }

  operator Error() const {
    return make_error<StringError>(Buffer, errc::invalid_argument);
  }
};
} // anonymous namespace

// ParseContext wraps information that is only necessary while parsing
// ObjectFile and can be discarded once parsing is done.
// Used by BTFParser::parse* auxiliary functions.
struct BTFParser::ParseContext {
  const ObjectFile &Obj;
  const ParseOptions &Opts;
  // Map from ELF section name to SectionRef
  DenseMap<StringRef, SectionRef> Sections;

public:
  ParseContext(const ObjectFile &Obj, const ParseOptions &Opts)
      : Obj(Obj), Opts(Opts) {}

  Expected<DataExtractor> makeExtractor(SectionRef Sec) {
    Expected<StringRef> Contents = Sec.getContents();
    if (!Contents)
      return Contents.takeError();
    return DataExtractor(Contents.get(), Obj.isLittleEndian(),
                         Obj.getBytesInAddress());
  }

  std::optional<SectionRef> findSection(StringRef Name) const {
    auto It = Sections.find(Name);
    if (It != Sections.end())
      return It->second;
    return std::nullopt;
  }
};

Error BTFParser::parseBTF(ParseContext &Ctx, SectionRef BTF) {
  Expected<DataExtractor> MaybeExtractor = Ctx.makeExtractor(BTF);
  if (!MaybeExtractor)
    return MaybeExtractor.takeError();

  DataExtractor &Extractor = MaybeExtractor.get();
  DataExtractor::Cursor C = DataExtractor::Cursor(0);
  uint16_t Magic = Extractor.getU16(C);
  if (!C)
    return Err(".BTF", C);
  if (Magic != BTF::MAGIC)
    return Err("invalid .BTF magic: ").write_hex(Magic);
  uint8_t Version = Extractor.getU8(C);
  if (!C)
    return Err(".BTF", C);
  if (Version != 1)
    return Err("unsupported .BTF version: ") << (unsigned)Version;
  (void)Extractor.getU8(C); // flags
  uint32_t HdrLen = Extractor.getU32(C);
  if (!C)
    return Err(".BTF", C);
  if (HdrLen < 8)
    return Err("unexpected .BTF header length: ") << HdrLen;
  uint32_t TypeOff = Extractor.getU32(C);
  uint32_t TypeLen = Extractor.getU32(C);
  uint32_t StrOff = Extractor.getU32(C);
  uint32_t StrLen = Extractor.getU32(C);
  uint32_t StrStart = HdrLen + StrOff;
  uint32_t StrEnd = StrStart + StrLen;
  uint32_t TypesInfoStart = HdrLen + TypeOff;
  uint32_t TypesInfoEnd = TypesInfoStart + TypeLen;
  uint32_t BytesExpected = std::max(StrEnd, TypesInfoEnd);
  if (!C)
    return Err(".BTF", C);
  if (Extractor.getData().size() < BytesExpected)
    return Err("invalid .BTF section size, expecting at-least ")
           << BytesExpected << " bytes";

  StringsTable = Extractor.getData().slice(StrStart, StrEnd);

  if (TypeLen > 0 && Ctx.Opts.LoadTypes) {
    StringRef RawData = Extractor.getData().slice(TypesInfoStart, TypesInfoEnd);
    if (Error E = parseTypesInfo(Ctx, TypesInfoStart, RawData))
      return E;
  }

  return Error::success();
}

// Compute record size for each BTF::CommonType sub-type
// (including entries in the tail position).
static size_t byteSize(BTF::CommonType *Type) {
  size_t Size = sizeof(BTF::CommonType);
  switch (Type->getKind()) {
  case BTF::BTF_KIND_INT:
    Size += sizeof(uint32_t);
    break;
  case BTF::BTF_KIND_ARRAY:
    Size += sizeof(BTF::BTFArray);
    break;
  case BTF::BTF_KIND_VAR:
    Size += sizeof(uint32_t);
    break;
  case BTF::BTF_KIND_DECL_TAG:
    Size += sizeof(uint32_t);
    break;
  case BTF::BTF_KIND_STRUCT:
  case BTF::BTF_KIND_UNION:
    Size += sizeof(BTF::BTFMember) * Type->getVlen();
    break;
  case BTF::BTF_KIND_ENUM:
    Size += sizeof(BTF::BTFEnum) * Type->getVlen();
    break;
  case BTF::BTF_KIND_ENUM64:
    Size += sizeof(BTF::BTFEnum64) * Type->getVlen();
    break;
  case BTF::BTF_KIND_FUNC_PROTO:
    Size += sizeof(BTF::BTFParam) * Type->getVlen();
    break;
  case BTF::BTF_KIND_DATASEC:
    Size += sizeof(BTF::BTFDataSec) * Type->getVlen();
    break;
  }
  return Size;
}

// Guard value for voids, simplifies code a bit, but NameOff is not
// actually valid.
const BTF::CommonType VoidTypeInst = {0, BTF::BTF_KIND_UNKN << 24, {0}};

// Type information "parsing" is very primitive:
// - The `RawData` is copied to a buffer owned by `BTFParser` instance.
// - The buffer is treated as an array of `uint32_t` values, each value
//   is swapped to use native endianness. This is possible, because
//   according to BTF spec all buffer elements are structures comprised
//   of `uint32_t` fields.
// - `BTFParser::Types` vector is filled with pointers to buffer
//   elements, using `byteSize()` function to slice the buffer at type
//   record boundaries.
// - If at some point a type definition with incorrect size (logical size
//   exceeding buffer boundaries) is reached it is not added to the
//   `BTFParser::Types` vector and the process stops.
Error BTFParser::parseTypesInfo(ParseContext &Ctx, uint64_t TypesInfoStart,
                                StringRef RawData) {
  using support::endian::byte_swap;

  TypesBuffer = OwningArrayRef<uint8_t>(arrayRefFromStringRef(RawData));
  // Switch endianness if necessary.
  endianness Endianness = Ctx.Obj.isLittleEndian() ? llvm::endianness::little
                                                   : llvm::endianness::big;
  uint32_t *TypesBuffer32 = (uint32_t *)TypesBuffer.data();
  for (uint64_t I = 0; I < TypesBuffer.size() / 4; ++I)
    TypesBuffer32[I] = byte_swap(TypesBuffer32[I], Endianness);

  // The type id 0 is reserved for void type.
  Types.push_back(&VoidTypeInst);

  uint64_t Pos = 0;
  while (Pos < RawData.size()) {
    uint64_t BytesLeft = RawData.size() - Pos;
    uint64_t Offset = TypesInfoStart + Pos;
    BTF::CommonType *Type = (BTF::CommonType *)&TypesBuffer[Pos];
    if (BytesLeft < sizeof(*Type))
      return Err("incomplete type definition in .BTF section:")
             << " offset " << Offset << ", index " << Types.size();

    uint64_t Size = byteSize(Type);
    if (BytesLeft < Size)
      return Err("incomplete type definition in .BTF section:")
             << " offset=" << Offset << ", index=" << Types.size()
             << ", vlen=" << Type->getVlen();

    LLVM_DEBUG({
      llvm::dbgs() << "Adding BTF type:\n"
                   << "  Id = " << Types.size() << "\n"
                   << "  Kind = " << Type->getKind() << "\n"
                   << "  Name = " << findString(Type->NameOff) << "\n"
                   << "  Record Size = " << Size << "\n";
    });
    Types.push_back(Type);
    Pos += Size;
  }

  return Error::success();
}

Error BTFParser::parseBTFExt(ParseContext &Ctx, SectionRef BTFExt) {
  Expected<DataExtractor> MaybeExtractor = Ctx.makeExtractor(BTFExt);
  if (!MaybeExtractor)
    return MaybeExtractor.takeError();

  DataExtractor &Extractor = MaybeExtractor.get();
  DataExtractor::Cursor C = DataExtractor::Cursor(0);
  uint16_t Magic = Extractor.getU16(C);
  if (!C)
    return Err(".BTF.ext", C);
  if (Magic != BTF::MAGIC)
    return Err("invalid .BTF.ext magic: ").write_hex(Magic);
  uint8_t Version = Extractor.getU8(C);
  if (!C)
    return Err(".BTF", C);
  if (Version != 1)
    return Err("unsupported .BTF.ext version: ") << (unsigned)Version;
  (void)Extractor.getU8(C); // flags
  uint32_t HdrLen = Extractor.getU32(C);
  if (!C)
    return Err(".BTF.ext", C);
  if (HdrLen < 8)
    return Err("unexpected .BTF.ext header length: ") << HdrLen;
  (void)Extractor.getU32(C); // func_info_off
  (void)Extractor.getU32(C); // func_info_len
  uint32_t LineInfoOff = Extractor.getU32(C);
  uint32_t LineInfoLen = Extractor.getU32(C);
  uint32_t RelocInfoOff = Extractor.getU32(C);
  uint32_t RelocInfoLen = Extractor.getU32(C);
  if (!C)
    return Err(".BTF.ext", C);

  if (LineInfoLen > 0 && Ctx.Opts.LoadLines) {
    uint32_t LineInfoStart = HdrLen + LineInfoOff;
    uint32_t LineInfoEnd = LineInfoStart + LineInfoLen;
    if (Error E = parseLineInfo(Ctx, Extractor, LineInfoStart, LineInfoEnd))
      return E;
  }

  if (RelocInfoLen > 0 && Ctx.Opts.LoadRelocs) {
    uint32_t RelocInfoStart = HdrLen + RelocInfoOff;
    uint32_t RelocInfoEnd = RelocInfoStart + RelocInfoLen;
    if (Error E = parseRelocInfo(Ctx, Extractor, RelocInfoStart, RelocInfoEnd))
      return E;
  }

  return Error::success();
}

Error BTFParser::parseLineInfo(ParseContext &Ctx, DataExtractor &Extractor,
                               uint64_t LineInfoStart, uint64_t LineInfoEnd) {
  DataExtractor::Cursor C = DataExtractor::Cursor(LineInfoStart);
  uint32_t RecSize = Extractor.getU32(C);
  if (!C)
    return Err(".BTF.ext", C);
  if (RecSize < 16)
    return Err("unexpected .BTF.ext line info record length: ") << RecSize;

  while (C && C.tell() < LineInfoEnd) {
    uint32_t SecNameOff = Extractor.getU32(C);
    uint32_t NumInfo = Extractor.getU32(C);
    StringRef SecName = findString(SecNameOff);
    std::optional<SectionRef> Sec = Ctx.findSection(SecName);
    if (!C)
      return Err(".BTF.ext", C);
    if (!Sec)
      return Err("") << "can't find section '" << SecName
                     << "' while parsing .BTF.ext line info";
    BTFLinesVector &Lines = SectionLines[Sec->getIndex()];
    for (uint32_t I = 0; C && I < NumInfo; ++I) {
      uint64_t RecStart = C.tell();
      uint32_t InsnOff = Extractor.getU32(C);
      uint32_t FileNameOff = Extractor.getU32(C);
      uint32_t LineOff = Extractor.getU32(C);
      uint32_t LineCol = Extractor.getU32(C);
      if (!C)
        return Err(".BTF.ext", C);
      Lines.push_back({InsnOff, FileNameOff, LineOff, LineCol});
      C.seek(RecStart + RecSize);
    }
    llvm::stable_sort(Lines,
                      [](const BTF::BPFLineInfo &L, const BTF::BPFLineInfo &R) {
                        return L.InsnOffset < R.InsnOffset;
                      });
  }
  if (!C)
    return Err(".BTF.ext", C);

  return Error::success();
}

Error BTFParser::parseRelocInfo(ParseContext &Ctx, DataExtractor &Extractor,
                                uint64_t RelocInfoStart,
                                uint64_t RelocInfoEnd) {
  DataExtractor::Cursor C = DataExtractor::Cursor(RelocInfoStart);
  uint32_t RecSize = Extractor.getU32(C);
  if (!C)
    return Err(".BTF.ext", C);
  if (RecSize < 16)
    return Err("unexpected .BTF.ext field reloc info record length: ")
           << RecSize;
  while (C && C.tell() < RelocInfoEnd) {
    uint32_t SecNameOff = Extractor.getU32(C);
    uint32_t NumInfo = Extractor.getU32(C);
    StringRef SecName = findString(SecNameOff);
    std::optional<SectionRef> Sec = Ctx.findSection(SecName);
    BTFRelocVector &Relocs = SectionRelocs[Sec->getIndex()];
    for (uint32_t I = 0; C && I < NumInfo; ++I) {
      uint64_t RecStart = C.tell();
      uint32_t InsnOff = Extractor.getU32(C);
      uint32_t TypeID = Extractor.getU32(C);
      uint32_t OffsetNameOff = Extractor.getU32(C);
      uint32_t RelocKind = Extractor.getU32(C);
      if (!C)
        return Err(".BTF.ext", C);
      Relocs.push_back({InsnOff, TypeID, OffsetNameOff, RelocKind});
      C.seek(RecStart + RecSize);
    }
    llvm::stable_sort(
        Relocs, [](const BTF::BPFFieldReloc &L, const BTF::BPFFieldReloc &R) {
          return L.InsnOffset < R.InsnOffset;
        });
  }
  if (!C)
    return Err(".BTF.ext", C);

  return Error::success();
}

Error BTFParser::parse(const ObjectFile &Obj, const ParseOptions &Opts) {
  StringsTable = StringRef();
  SectionLines.clear();
  SectionRelocs.clear();
  Types.clear();
  TypesBuffer = OwningArrayRef<uint8_t>();

  ParseContext Ctx(Obj, Opts);
  std::optional<SectionRef> BTF;
  std::optional<SectionRef> BTFExt;
  for (SectionRef Sec : Obj.sections()) {
    Expected<StringRef> MaybeName = Sec.getName();
    if (!MaybeName)
      return Err("error while reading section name: ") << MaybeName.takeError();
    Ctx.Sections[*MaybeName] = Sec;
    if (*MaybeName == BTFSectionName)
      BTF = Sec;
    if (*MaybeName == BTFExtSectionName)
      BTFExt = Sec;
  }
  if (!BTF)
    return Err("can't find .BTF section");
  if (!BTFExt)
    return Err("can't find .BTF.ext section");
  if (Error E = parseBTF(Ctx, *BTF))
    return E;
  if (Error E = parseBTFExt(Ctx, *BTFExt))
    return E;

  return Error::success();
}

bool BTFParser::hasBTFSections(const ObjectFile &Obj) {
  bool HasBTF = false;
  bool HasBTFExt = false;
  for (SectionRef Sec : Obj.sections()) {
    Expected<StringRef> Name = Sec.getName();
    if (Error E = Name.takeError()) {
      logAllUnhandledErrors(std::move(E), errs());
      continue;
    }
    HasBTF |= *Name == BTFSectionName;
    HasBTFExt |= *Name == BTFExtSectionName;
    if (HasBTF && HasBTFExt)
      return true;
  }
  return false;
}

StringRef BTFParser::findString(uint32_t Offset) const {
  return StringsTable.slice(Offset, StringsTable.find(0, Offset));
}

template <typename T>
static const T *findInfo(const DenseMap<uint64_t, SmallVector<T, 0>> &SecMap,
                         SectionedAddress Address) {
  auto MaybeSecInfo = SecMap.find(Address.SectionIndex);
  if (MaybeSecInfo == SecMap.end())
    return nullptr;

  const SmallVector<T, 0> &SecInfo = MaybeSecInfo->second;
  const uint64_t TargetOffset = Address.Address;
  typename SmallVector<T, 0>::const_iterator MaybeInfo = llvm::partition_point(
      SecInfo, [=](const T &Entry) { return Entry.InsnOffset < TargetOffset; });
  if (MaybeInfo == SecInfo.end() || MaybeInfo->InsnOffset != Address.Address)
    return nullptr;

  return &*MaybeInfo;
}

const BTF::BPFLineInfo *
BTFParser::findLineInfo(SectionedAddress Address) const {
  return findInfo(SectionLines, Address);
}

const BTF::BPFFieldReloc *
BTFParser::findFieldReloc(SectionedAddress Address) const {
  return findInfo(SectionRelocs, Address);
}

const BTF::CommonType *BTFParser::findType(uint32_t Id) const {
  if (Id < Types.size())
    return Types[Id];
  return nullptr;
}

enum RelocKindGroup {
  RKG_FIELD,
  RKG_TYPE,
  RKG_ENUMVAL,
  RKG_UNKNOWN,
};

static RelocKindGroup relocKindGroup(const BTF::BPFFieldReloc *Reloc) {
  switch (Reloc->RelocKind) {
  case BTF::FIELD_BYTE_OFFSET:
  case BTF::FIELD_BYTE_SIZE:
  case BTF::FIELD_EXISTENCE:
  case BTF::FIELD_SIGNEDNESS:
  case BTF::FIELD_LSHIFT_U64:
  case BTF::FIELD_RSHIFT_U64:
    return RKG_FIELD;
  case BTF::BTF_TYPE_ID_LOCAL:
  case BTF::BTF_TYPE_ID_REMOTE:
  case BTF::TYPE_EXISTENCE:
  case BTF::TYPE_MATCH:
  case BTF::TYPE_SIZE:
    return RKG_TYPE;
  case BTF::ENUM_VALUE_EXISTENCE:
  case BTF::ENUM_VALUE:
    return RKG_ENUMVAL;
  default:
    return RKG_UNKNOWN;
  }
}

static bool isMod(const BTF::CommonType *Type) {
  switch (Type->getKind()) {
  case BTF::BTF_KIND_VOLATILE:
  case BTF::BTF_KIND_CONST:
  case BTF::BTF_KIND_RESTRICT:
  case BTF::BTF_KIND_TYPE_TAG:
    return true;
  default:
    return false;
  }
}

static bool printMod(const BTFParser &BTF, const BTF::CommonType *Type,
                     raw_ostream &Stream) {
  switch (Type->getKind()) {
  case BTF::BTF_KIND_CONST:
    Stream << " const";
    break;
  case BTF::BTF_KIND_VOLATILE:
    Stream << " volatile";
    break;
  case BTF::BTF_KIND_RESTRICT:
    Stream << " restrict";
    break;
  case BTF::BTF_KIND_TYPE_TAG:
    Stream << " type_tag(\"" << BTF.findString(Type->NameOff) << "\")";
    break;
  default:
    return false;
  }
  return true;
}

static const BTF::CommonType *skipModsAndTypedefs(const BTFParser &BTF,
                                                  const BTF::CommonType *Type) {
  while (isMod(Type) || Type->getKind() == BTF::BTF_KIND_TYPEDEF) {
    auto *Base = BTF.findType(Type->Type);
    if (!Base)
      break;
    Type = Base;
  }
  return Type;
}

namespace {
struct StrOrAnon {
  const BTFParser &BTF;
  uint32_t Offset;
  uint32_t Idx;
};

static raw_ostream &operator<<(raw_ostream &Stream, const StrOrAnon &S) {
  StringRef Str = S.BTF.findString(S.Offset);
  if (Str.empty())
    Stream << "<anon " << S.Idx << ">";
  else
    Stream << Str;
  return Stream;
}
} // anonymous namespace

static void relocKindName(uint32_t X, raw_ostream &Out) {
  Out << "<";
  switch (X) {
  default:
    Out << "reloc kind #" << X;
    break;
  case BTF::FIELD_BYTE_OFFSET:
    Out << "byte_off";
    break;
  case BTF::FIELD_BYTE_SIZE:
    Out << "byte_sz";
    break;
  case BTF::FIELD_EXISTENCE:
    Out << "field_exists";
    break;
  case BTF::FIELD_SIGNEDNESS:
    Out << "signed";
    break;
  case BTF::FIELD_LSHIFT_U64:
    Out << "lshift_u64";
    break;
  case BTF::FIELD_RSHIFT_U64:
    Out << "rshift_u64";
    break;
  case BTF::BTF_TYPE_ID_LOCAL:
    Out << "local_type_id";
    break;
  case BTF::BTF_TYPE_ID_REMOTE:
    Out << "target_type_id";
    break;
  case BTF::TYPE_EXISTENCE:
    Out << "type_exists";
    break;
  case BTF::TYPE_MATCH:
    Out << "type_matches";
    break;
  case BTF::TYPE_SIZE:
    Out << "type_size";
    break;
  case BTF::ENUM_VALUE_EXISTENCE:
    Out << "enumval_exists";
    break;
  case BTF::ENUM_VALUE:
    Out << "enumval_value";
    break;
  }
  Out << ">";
}

// Produces a human readable description of a CO-RE relocation.
// Such relocations are generated by BPF backend, and processed
// by libbpf's BPF program loader [1].
//
// Each relocation record has the following information:
// - Relocation kind;
// - BTF type ID;
// - Access string offset in string table.
//
// There are different kinds of relocations, these kinds could be split
// in three groups:
// - load-time information about types (size, existence),
//   `BTFParser::symbolize()` output for such relocations uses the template:
//
//     <relocation-kind> [<id>] <type-name>
//
//   For example:
//   - "<type_exists> [7] struct foo"
//   - "<type_size> [7] struct foo"
//
// - load-time information about enums (literal existence, literal value),
//   `BTFParser::symbolize()` output for such relocations uses the template:
//
//     <relocation-kind> [<id>] <type-name>::<literal-name> = <original-value>
//
//   For example:
//   - "<enumval_exists> [5] enum foo::U = 1"
//   - "<enumval_value> [5] enum foo::V = 2"
//
// - load-time information about fields (e.g. field offset),
//   `BTFParser::symbolize()` output for such relocations uses the template:
//
//     <relocation-kind> [<id>] \
//       <type-name>::[N].<field-1-name>...<field-M-name> \
//       (<access string>)
//
//   For example:
//   - "<byte_off> [8] struct bar::[7].v (7:1)"
//   - "<field_exists> [8] struct bar::v (0:1)"
//
// If relocation description is not valid output follows the following pattern:
//
//     <relocation-kind> <type-id>::<unprocessedaccess-string> <<error-msg>>
//
// For example:
//
// - "<type_sz> [42] '' <unknown type id: 42>"
// - "<byte_off> [4] '0:' <field spec too short>"
//
// Additional examples could be found in unit tests, see
// llvm/unittests/DebugInfo/BTF/BTFParserTest.cpp.
//
// [1] https://www.kernel.org/doc/html/latest/bpf/libbpf/index.html
void BTFParser::symbolize(const BTF::BPFFieldReloc *Reloc,
                          SmallVectorImpl<char> &Result) const {
  raw_svector_ostream Stream(Result);
  StringRef FullSpecStr = findString(Reloc->OffsetNameOff);
  SmallVector<uint32_t, 8> RawSpec;

  auto Fail = [&](auto Msg) {
    Result.resize(0);
    relocKindName(Reloc->RelocKind, Stream);
    Stream << " [" << Reloc->TypeID << "] '" << FullSpecStr << "'"
           << " <" << Msg << ">";
  };

  // Relocation access string follows pattern [0-9]+(:[0-9]+)*,
  // e.g.: 12:22:3. Code below splits `SpecStr` by ':', parses
  // numbers, and pushes them to `RawSpec`.
  StringRef SpecStr = FullSpecStr;
  while (SpecStr.size()) {
    unsigned long long Val;
    if (consumeUnsignedInteger(SpecStr, 10, Val))
      return Fail("spec string is not a number");
    RawSpec.push_back(Val);
    if (SpecStr.empty())
      break;
    if (SpecStr[0] != ':')
      return Fail(format("unexpected spec string delimiter: '%c'", SpecStr[0]));
    SpecStr = SpecStr.substr(1);
  }

  // Print relocation kind to `Stream`.
  relocKindName(Reloc->RelocKind, Stream);

  uint32_t CurId = Reloc->TypeID;
  const BTF::CommonType *Type = findType(CurId);
  if (!Type)
    return Fail(format("unknown type id: %d", CurId));

  Stream << " [" << CurId << "]";

  // `Type` might have modifiers, e.g. for type 'const int' the `Type`
  // would refer to BTF type of kind BTF_KIND_CONST.
  // Print all these modifiers to `Stream`.
  for (uint32_t ChainLen = 0; printMod(*this, Type, Stream); ++ChainLen) {
    if (ChainLen >= 32)
      return Fail("modifiers chain is too long");

    CurId = Type->Type;
    const BTF::CommonType *NextType = findType(CurId);
    if (!NextType)
      return Fail(format("unknown type id: %d in modifiers chain", CurId));
    Type = NextType;
  }
  // Print the type name to `Stream`.
  if (CurId == 0) {
    Stream << " void";
  } else {
    switch (Type->getKind()) {
    case BTF::BTF_KIND_TYPEDEF:
      Stream << " typedef";
      break;
    case BTF::BTF_KIND_STRUCT:
      Stream << " struct";
      break;
    case BTF::BTF_KIND_UNION:
      Stream << " union";
      break;
    case BTF::BTF_KIND_ENUM:
      Stream << " enum";
      break;
    case BTF::BTF_KIND_ENUM64:
      Stream << " enum";
      break;
    case BTF::BTF_KIND_FWD:
      if (Type->Info & BTF::FWD_UNION_FLAG)
        Stream << " fwd union";
      else
        Stream << " fwd struct";
      break;
    default:
      break;
    }
    Stream << " " << StrOrAnon({*this, Type->NameOff, CurId});
  }

  RelocKindGroup Group = relocKindGroup(Reloc);
  // Type-based relocations don't use access string but clang backend
  // generates '0' and libbpf checks it's value, do the same here.
  if (Group == RKG_TYPE) {
    if (RawSpec.size() != 1 || RawSpec[0] != 0)
      return Fail("unexpected type-based relocation spec: should be '0'");
    return;
  }

  Stream << "::";

  // For enum-based relocations access string is a single number,
  // corresponding to the enum literal sequential number.
  // E.g. for `enum E { U, V }`, relocation requesting value of `V`
  // would look as follows:
  // - kind: BTF::ENUM_VALUE
  // - BTF id: id for `E`
  // - access string: "1"
  if (Group == RKG_ENUMVAL) {
    Type = skipModsAndTypedefs(*this, Type);

    if (RawSpec.size() != 1)
      return Fail("unexpected enumval relocation spec size");

    uint32_t NameOff;
    uint64_t Val;
    uint32_t Idx = RawSpec[0];
    if (auto *T = dyn_cast<BTF::EnumType>(Type)) {
      if (T->values().size() <= Idx)
        return Fail(format("bad value index: %d", Idx));
      const BTF::BTFEnum &E = T->values()[Idx];
      NameOff = E.NameOff;
      Val = E.Val;
    } else if (auto *T = dyn_cast<BTF::Enum64Type>(Type)) {
      if (T->values().size() <= Idx)
        return Fail(format("bad value index: %d", Idx));
      const BTF::BTFEnum64 &E = T->values()[Idx];
      NameOff = E.NameOff;
      Val = (uint64_t)E.Val_Hi32 << 32u | E.Val_Lo32;
    } else {
      return Fail(format("unexpected type kind for enum relocation: %d",
                         Type->getKind()));
    }

    Stream << StrOrAnon({*this, NameOff, Idx});
    if (Type->Info & BTF::ENUM_SIGNED_FLAG)
      Stream << " = " << (int64_t)Val;
    else
      Stream << " = " << (uint64_t)Val;
    return;
  }

  // For type-based relocations access string is an array of numbers,
  // which resemble index parameters for `getelementptr` LLVM IR instruction.
  // E.g. for the following types:
  //
  //   struct foo {
  //     int a;
  //     int b;
  //   };
  //   struct bar {
  //     int u;
  //     struct foo v[7];
  //   };
  //
  // Relocation requesting `offsetof(struct bar, v[2].b)` will have
  // the following access string: 0:1:2:1
  //                              ^ ^ ^ ^
  //                              | | | |
  //                  initial index | | field 'b' is a field #1
  //                                | | (counting from 0)
  //                                | array index #2
  //           field 'v' is a field #1
  //              (counting from 0)
  if (Group == RKG_FIELD) {
    if (RawSpec.size() < 1)
      return Fail("field spec too short");

    if (RawSpec[0] != 0)
      Stream << "[" << RawSpec[0] << "]";
    for (uint32_t I = 1; I < RawSpec.size(); ++I) {
      Type = skipModsAndTypedefs(*this, Type);
      uint32_t Idx = RawSpec[I];

      if (auto *T = dyn_cast<BTF::StructType>(Type)) {
        if (T->getVlen() <= Idx)
          return Fail(
              format("member index %d for spec sub-string %d is out of range",
                     Idx, I));

        const BTF::BTFMember &Member = T->members()[Idx];
        if (I != 1 || RawSpec[0] != 0)
          Stream << ".";
        Stream << StrOrAnon({*this, Member.NameOff, Idx});
        Type = findType(Member.Type);
        if (!Type)
          return Fail(format("unknown member type id %d for spec sub-string %d",
                             Member.Type, I));
      } else if (auto *T = dyn_cast<BTF::ArrayType>(Type)) {
        Stream << "[" << Idx << "]";
        Type = findType(T->getArray().ElemType);
        if (!Type)
          return Fail(
              format("unknown element type id %d for spec sub-string %d",
                     T->getArray().ElemType, I));
      } else {
        return Fail(format("unexpected type kind %d for spec sub-string %d",
                           Type->getKind(), I));
      }
    }

    Stream << " (" << FullSpecStr << ")";
    return;
  }

  return Fail(format("unknown relocation kind: %d", Reloc->RelocKind));
}
