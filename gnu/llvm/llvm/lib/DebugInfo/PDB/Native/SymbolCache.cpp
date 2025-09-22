#include "llvm/DebugInfo/PDB/Native/SymbolCache.h"

#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugSubsectionRecord.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecordHelpers.h"
#include "llvm/DebugInfo/PDB/IPDBSourceFile.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleList.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/ModuleDebugStream.h"
#include "llvm/DebugInfo/PDB/Native/NativeCompilandSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumGlobals.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumLineNumbers.h"
#include "llvm/DebugInfo/PDB/Native/NativeEnumTypes.h"
#include "llvm/DebugInfo/PDB/Native/NativeFunctionSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeInlineSiteSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeLineNumber.h"
#include "llvm/DebugInfo/PDB/Native/NativePublicSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeRawSymbol.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeArray.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeEnum.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypePointer.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeTypedef.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeUDT.h"
#include "llvm/DebugInfo/PDB/Native/NativeTypeVTShape.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/PDBSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::pdb;

// Maps codeview::SimpleTypeKind of a built-in type to the parameters necessary
// to instantiate a NativeBuiltinSymbol for that type.
static const struct BuiltinTypeEntry {
  codeview::SimpleTypeKind Kind;
  PDB_BuiltinType Type;
  uint32_t Size;
} BuiltinTypes[] = {
    {codeview::SimpleTypeKind::None, PDB_BuiltinType::None, 0},
    {codeview::SimpleTypeKind::Void, PDB_BuiltinType::Void, 0},
    {codeview::SimpleTypeKind::HResult, PDB_BuiltinType::HResult, 4},
    {codeview::SimpleTypeKind::Int16Short, PDB_BuiltinType::Int, 2},
    {codeview::SimpleTypeKind::UInt16Short, PDB_BuiltinType::UInt, 2},
    {codeview::SimpleTypeKind::Int32, PDB_BuiltinType::Int, 4},
    {codeview::SimpleTypeKind::UInt32, PDB_BuiltinType::UInt, 4},
    {codeview::SimpleTypeKind::Int32Long, PDB_BuiltinType::Int, 4},
    {codeview::SimpleTypeKind::UInt32Long, PDB_BuiltinType::UInt, 4},
    {codeview::SimpleTypeKind::Int64Quad, PDB_BuiltinType::Int, 8},
    {codeview::SimpleTypeKind::UInt64Quad, PDB_BuiltinType::UInt, 8},
    {codeview::SimpleTypeKind::NarrowCharacter, PDB_BuiltinType::Char, 1},
    {codeview::SimpleTypeKind::WideCharacter, PDB_BuiltinType::WCharT, 2},
    {codeview::SimpleTypeKind::Character16, PDB_BuiltinType::Char16, 2},
    {codeview::SimpleTypeKind::Character32, PDB_BuiltinType::Char32, 4},
    {codeview::SimpleTypeKind::Character8, PDB_BuiltinType::Char8, 1},
    {codeview::SimpleTypeKind::SignedCharacter, PDB_BuiltinType::Char, 1},
    {codeview::SimpleTypeKind::UnsignedCharacter, PDB_BuiltinType::UInt, 1},
    {codeview::SimpleTypeKind::Float32, PDB_BuiltinType::Float, 4},
    {codeview::SimpleTypeKind::Float64, PDB_BuiltinType::Float, 8},
    {codeview::SimpleTypeKind::Float80, PDB_BuiltinType::Float, 10},
    {codeview::SimpleTypeKind::Boolean8, PDB_BuiltinType::Bool, 1},
    // This table can be grown as necessary, but these are the only types we've
    // needed so far.
};

SymbolCache::SymbolCache(NativeSession &Session, DbiStream *Dbi)
    : Session(Session), Dbi(Dbi) {
  // Id 0 is reserved for the invalid symbol.
  Cache.push_back(nullptr);
  SourceFiles.push_back(nullptr);

  if (Dbi)
    Compilands.resize(Dbi->modules().getModuleCount());
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createTypeEnumerator(TypeLeafKind Kind) {
  return createTypeEnumerator(std::vector<TypeLeafKind>{Kind});
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createTypeEnumerator(std::vector<TypeLeafKind> Kinds) {
  auto Tpi = Session.getPDBFile().getPDBTpiStream();
  if (!Tpi) {
    consumeError(Tpi.takeError());
    return nullptr;
  }
  auto &Types = Tpi->typeCollection();
  return std::unique_ptr<IPDBEnumSymbols>(
      new NativeEnumTypes(Session, Types, std::move(Kinds)));
}

std::unique_ptr<IPDBEnumSymbols>
SymbolCache::createGlobalsEnumerator(codeview::SymbolKind Kind) {
  return std::unique_ptr<IPDBEnumSymbols>(
      new NativeEnumGlobals(Session, {Kind}));
}

SymIndexId SymbolCache::createSimpleType(TypeIndex Index,
                                         ModifierOptions Mods) const {
  if (Index.getSimpleMode() != codeview::SimpleTypeMode::Direct)
    return createSymbol<NativeTypePointer>(Index);

  const auto Kind = Index.getSimpleKind();
  const auto It =
      llvm::find_if(BuiltinTypes, [Kind](const BuiltinTypeEntry &Builtin) {
        return Builtin.Kind == Kind;
      });
  if (It == std::end(BuiltinTypes))
    return 0;
  return createSymbol<NativeTypeBuiltin>(Mods, It->Type, It->Size);
}

SymIndexId
SymbolCache::createSymbolForModifiedType(codeview::TypeIndex ModifierTI,
                                         codeview::CVType CVT) const {
  ModifierRecord Record;
  if (auto EC = TypeDeserializer::deserializeAs<ModifierRecord>(CVT, Record)) {
    consumeError(std::move(EC));
    return 0;
  }

  if (Record.ModifiedType.isSimple())
    return createSimpleType(Record.ModifiedType, Record.Modifiers);

  // Make sure we create and cache a record for the unmodified type.
  SymIndexId UnmodifiedId = findSymbolByTypeIndex(Record.ModifiedType);
  NativeRawSymbol &UnmodifiedNRS = *Cache[UnmodifiedId];

  switch (UnmodifiedNRS.getSymTag()) {
  case PDB_SymType::Enum:
    return createSymbol<NativeTypeEnum>(
        static_cast<NativeTypeEnum &>(UnmodifiedNRS), std::move(Record));
  case PDB_SymType::UDT:
    return createSymbol<NativeTypeUDT>(
        static_cast<NativeTypeUDT &>(UnmodifiedNRS), std::move(Record));
  default:
    // No other types can be modified.  (LF_POINTER, for example, records
    // its modifiers a different way.
    assert(false && "Invalid LF_MODIFIER record");
    break;
  }
  return 0;
}

SymIndexId SymbolCache::findSymbolByTypeIndex(codeview::TypeIndex Index) const {
  // First see if it's already in our cache.
  const auto Entry = TypeIndexToSymbolId.find(Index);
  if (Entry != TypeIndexToSymbolId.end())
    return Entry->second;

  // Symbols for built-in types are created on the fly.
  if (Index.isSimple()) {
    SymIndexId Result = createSimpleType(Index, ModifierOptions::None);
    assert(TypeIndexToSymbolId.count(Index) == 0);
    TypeIndexToSymbolId[Index] = Result;
    return Result;
  }

  // We need to instantiate and cache the desired type symbol.
  auto Tpi = Session.getPDBFile().getPDBTpiStream();
  if (!Tpi) {
    consumeError(Tpi.takeError());
    return 0;
  }
  codeview::LazyRandomTypeCollection &Types = Tpi->typeCollection();
  codeview::CVType CVT = Types.getType(Index);

  if (isUdtForwardRef(CVT)) {
    Expected<TypeIndex> EFD = Tpi->findFullDeclForForwardRef(Index);

    if (!EFD)
      consumeError(EFD.takeError());
    else if (*EFD != Index) {
      assert(!isUdtForwardRef(Types.getType(*EFD)));
      SymIndexId Result = findSymbolByTypeIndex(*EFD);
      // Record a mapping from ForwardRef -> SymIndex of complete type so that
      // we'll take the fast path next time.
      assert(TypeIndexToSymbolId.count(Index) == 0);
      TypeIndexToSymbolId[Index] = Result;
      return Result;
    }
  }

  // At this point if we still have a forward ref udt it means the full decl was
  // not in the PDB.  We just have to deal with it and use the forward ref.
  SymIndexId Id = 0;
  switch (CVT.kind()) {
  case codeview::LF_ENUM:
    Id = createSymbolForType<NativeTypeEnum, EnumRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_ARRAY:
    Id = createSymbolForType<NativeTypeArray, ArrayRecord>(Index,
                                                           std::move(CVT));
    break;
  case codeview::LF_CLASS:
  case codeview::LF_STRUCTURE:
  case codeview::LF_INTERFACE:
    Id = createSymbolForType<NativeTypeUDT, ClassRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_UNION:
    Id = createSymbolForType<NativeTypeUDT, UnionRecord>(Index, std::move(CVT));
    break;
  case codeview::LF_POINTER:
    Id = createSymbolForType<NativeTypePointer, PointerRecord>(Index,
                                                               std::move(CVT));
    break;
  case codeview::LF_MODIFIER:
    Id = createSymbolForModifiedType(Index, std::move(CVT));
    break;
  case codeview::LF_PROCEDURE:
    Id = createSymbolForType<NativeTypeFunctionSig, ProcedureRecord>(
        Index, std::move(CVT));
    break;
  case codeview::LF_MFUNCTION:
    Id = createSymbolForType<NativeTypeFunctionSig, MemberFunctionRecord>(
        Index, std::move(CVT));
    break;
  case codeview::LF_VTSHAPE:
    Id = createSymbolForType<NativeTypeVTShape, VFTableShapeRecord>(
        Index, std::move(CVT));
    break;
  default:
    Id = createSymbolPlaceholder();
    break;
  }
  if (Id != 0) {
    assert(TypeIndexToSymbolId.count(Index) == 0);
    TypeIndexToSymbolId[Index] = Id;
  }
  return Id;
}

std::unique_ptr<PDBSymbol>
SymbolCache::getSymbolById(SymIndexId SymbolId) const {
  assert(SymbolId < Cache.size());

  // Id 0 is reserved.
  if (SymbolId == 0 || SymbolId >= Cache.size())
    return nullptr;

  // Make sure to handle the case where we've inserted a placeholder symbol
  // for types we don't yet support.
  NativeRawSymbol *NRS = Cache[SymbolId].get();
  if (!NRS)
    return nullptr;

  return PDBSymbol::create(Session, *NRS);
}

NativeRawSymbol &SymbolCache::getNativeSymbolById(SymIndexId SymbolId) const {
  return *Cache[SymbolId];
}

uint32_t SymbolCache::getNumCompilands() const {
  if (!Dbi)
    return 0;

  return Dbi->modules().getModuleCount();
}

SymIndexId SymbolCache::getOrCreateGlobalSymbolByOffset(uint32_t Offset) {
  auto Iter = GlobalOffsetToSymbolId.find(Offset);
  if (Iter != GlobalOffsetToSymbolId.end())
    return Iter->second;

  SymbolStream &SS = cantFail(Session.getPDBFile().getPDBSymbolStream());
  CVSymbol CVS = SS.readRecord(Offset);
  SymIndexId Id = 0;
  switch (CVS.kind()) {
  case SymbolKind::S_UDT: {
    UDTSym US = cantFail(SymbolDeserializer::deserializeAs<UDTSym>(CVS));
    Id = createSymbol<NativeTypeTypedef>(std::move(US));
    break;
  }
  default:
    Id = createSymbolPlaceholder();
    break;
  }
  if (Id != 0) {
    assert(GlobalOffsetToSymbolId.count(Offset) == 0);
    GlobalOffsetToSymbolId[Offset] = Id;
  }

  return Id;
}

SymIndexId SymbolCache::getOrCreateInlineSymbol(InlineSiteSym Sym,
                                                uint64_t ParentAddr,
                                                uint16_t Modi,
                                                uint32_t RecordOffset) const {
  auto Iter = SymTabOffsetToSymbolId.find({Modi, RecordOffset});
  if (Iter != SymTabOffsetToSymbolId.end())
    return Iter->second;

  SymIndexId Id = createSymbol<NativeInlineSiteSymbol>(Sym, ParentAddr);
  SymTabOffsetToSymbolId.insert({{Modi, RecordOffset}, Id});
  return Id;
}

std::unique_ptr<PDBSymbol>
SymbolCache::findSymbolBySectOffset(uint32_t Sect, uint32_t Offset,
                                    PDB_SymType Type) {
  switch (Type) {
  case PDB_SymType::Function:
    return findFunctionSymbolBySectOffset(Sect, Offset);
  case PDB_SymType::PublicSymbol:
    return findPublicSymbolBySectOffset(Sect, Offset);
  case PDB_SymType::Compiland: {
    uint16_t Modi;
    if (!Session.moduleIndexForSectOffset(Sect, Offset, Modi))
      return nullptr;
    return getOrCreateCompiland(Modi);
  }
  case PDB_SymType::None: {
    // FIXME: Implement for PDB_SymType::Data. The symbolizer calls this but
    // only uses it to find the symbol length.
    if (auto Sym = findFunctionSymbolBySectOffset(Sect, Offset))
      return Sym;
    return nullptr;
  }
  default:
    return nullptr;
  }
}

std::unique_ptr<PDBSymbol>
SymbolCache::findFunctionSymbolBySectOffset(uint32_t Sect, uint32_t Offset) {
  auto Iter = AddressToSymbolId.find({Sect, Offset});
  if (Iter != AddressToSymbolId.end())
    return getSymbolById(Iter->second);

  if (!Dbi)
    return nullptr;

  uint16_t Modi;
  if (!Session.moduleIndexForSectOffset(Sect, Offset, Modi))
    return nullptr;

  Expected<ModuleDebugStreamRef> ExpectedModS =
      Session.getModuleDebugStream(Modi);
  if (!ExpectedModS) {
    consumeError(ExpectedModS.takeError());
    return nullptr;
  }
  CVSymbolArray Syms = ExpectedModS->getSymbolArray();

  // Search for the symbol in this module.
  for (auto I = Syms.begin(), E = Syms.end(); I != E; ++I) {
    if (I->kind() != S_LPROC32 && I->kind() != S_GPROC32)
      continue;
    auto PS = cantFail(SymbolDeserializer::deserializeAs<ProcSym>(*I));
    if (Sect == PS.Segment && Offset >= PS.CodeOffset &&
        Offset < PS.CodeOffset + PS.CodeSize) {
      // Check if the symbol is already cached.
      auto Found = AddressToSymbolId.find({PS.Segment, PS.CodeOffset});
      if (Found != AddressToSymbolId.end())
        return getSymbolById(Found->second);

      // Otherwise, create a new symbol.
      SymIndexId Id = createSymbol<NativeFunctionSymbol>(PS, I.offset());
      AddressToSymbolId.insert({{PS.Segment, PS.CodeOffset}, Id});
      return getSymbolById(Id);
    }

    // Jump to the end of this ProcSym.
    I = Syms.at(PS.End);
  }
  return nullptr;
}

std::unique_ptr<PDBSymbol>
SymbolCache::findPublicSymbolBySectOffset(uint32_t Sect, uint32_t Offset) {
  auto Iter = AddressToPublicSymId.find({Sect, Offset});
  if (Iter != AddressToPublicSymId.end())
    return getSymbolById(Iter->second);

  auto Publics = Session.getPDBFile().getPDBPublicsStream();
  if (!Publics)
    return nullptr;

  auto ExpectedSyms = Session.getPDBFile().getPDBSymbolStream();
  if (!ExpectedSyms)
    return nullptr;
  BinaryStreamRef SymStream =
      ExpectedSyms->getSymbolArray().getUnderlyingStream();

  // Use binary search to find the first public symbol with an address greater
  // than or equal to Sect, Offset.
  auto AddrMap = Publics->getAddressMap();
  auto First = AddrMap.begin();
  auto It = AddrMap.begin();
  size_t Count = AddrMap.size();
  size_t Half;
  while (Count > 0) {
    It = First;
    Half = Count / 2;
    It += Half;
    Expected<CVSymbol> Sym = readSymbolFromStream(SymStream, *It);
    if (!Sym) {
      consumeError(Sym.takeError());
      return nullptr;
    }

    auto PS =
        cantFail(SymbolDeserializer::deserializeAs<PublicSym32>(Sym.get()));
    if (PS.Segment < Sect || (PS.Segment == Sect && PS.Offset <= Offset)) {
      First = ++It;
      Count -= Half + 1;
    } else
      Count = Half;
  }
  if (It == AddrMap.begin())
    return nullptr;
  --It;

  Expected<CVSymbol> Sym = readSymbolFromStream(SymStream, *It);
  if (!Sym) {
    consumeError(Sym.takeError());
    return nullptr;
  }

  // Check if the symbol is already cached.
  auto PS = cantFail(SymbolDeserializer::deserializeAs<PublicSym32>(Sym.get()));
  auto Found = AddressToPublicSymId.find({PS.Segment, PS.Offset});
  if (Found != AddressToPublicSymId.end())
    return getSymbolById(Found->second);

  // Otherwise, create a new symbol.
  SymIndexId Id = createSymbol<NativePublicSymbol>(PS);
  AddressToPublicSymId.insert({{PS.Segment, PS.Offset}, Id});
  return getSymbolById(Id);
}

std::vector<SymbolCache::LineTableEntry>
SymbolCache::findLineTable(uint16_t Modi) const {
  // Check if this module has already been added.
  auto LineTableIter = LineTable.find(Modi);
  if (LineTableIter != LineTable.end())
    return LineTableIter->second;

  std::vector<LineTableEntry> &ModuleLineTable = LineTable[Modi];

  // If there is an error or there are no lines, just return the
  // empty vector.
  Expected<ModuleDebugStreamRef> ExpectedModS =
      Session.getModuleDebugStream(Modi);
  if (!ExpectedModS) {
    consumeError(ExpectedModS.takeError());
    return ModuleLineTable;
  }

  std::vector<std::vector<LineTableEntry>> EntryList;
  for (const auto &SS : ExpectedModS->getSubsectionsArray()) {
    if (SS.kind() != DebugSubsectionKind::Lines)
      continue;

    DebugLinesSubsectionRef Lines;
    BinaryStreamReader Reader(SS.getRecordData());
    if (auto EC = Lines.initialize(Reader)) {
      consumeError(std::move(EC));
      continue;
    }

    uint32_t RelocSegment = Lines.header()->RelocSegment;
    uint32_t RelocOffset = Lines.header()->RelocOffset;
    for (const LineColumnEntry &Group : Lines) {
      if (Group.LineNumbers.empty())
        continue;

      std::vector<LineTableEntry> Entries;

      // If there are column numbers, then they should be in a parallel stream
      // to the line numbers.
      auto ColIt = Group.Columns.begin();
      auto ColsEnd = Group.Columns.end();

      // Add a line to mark the beginning of this section.
      uint64_t StartAddr =
          Session.getVAFromSectOffset(RelocSegment, RelocOffset);
      LineInfo FirstLine(Group.LineNumbers.front().Flags);
      uint32_t ColNum =
          (Lines.hasColumnInfo()) ? Group.Columns.front().StartColumn : 0;
      Entries.push_back({StartAddr, FirstLine, ColNum, Group.NameIndex, false});

      for (const LineNumberEntry &LN : Group.LineNumbers) {
        uint64_t VA =
            Session.getVAFromSectOffset(RelocSegment, RelocOffset + LN.Offset);
        LineInfo Line(LN.Flags);
        ColNum = 0;

        if (Lines.hasColumnInfo() && ColIt != ColsEnd) {
          ColNum = ColIt->StartColumn;
          ++ColIt;
        }
        Entries.push_back({VA, Line, ColNum, Group.NameIndex, false});
      }

      // Add a terminal entry line to mark the end of this subsection.
      uint64_t EndAddr = StartAddr + Lines.header()->CodeSize;
      LineInfo LastLine(Group.LineNumbers.back().Flags);
      ColNum = (Lines.hasColumnInfo()) ? Group.Columns.back().StartColumn : 0;
      Entries.push_back({EndAddr, LastLine, ColNum, Group.NameIndex, true});

      EntryList.push_back(Entries);
    }
  }

  // Sort EntryList, and add flattened contents to the line table.
  llvm::sort(EntryList, [](const std::vector<LineTableEntry> &LHS,
                           const std::vector<LineTableEntry> &RHS) {
    return LHS[0].Addr < RHS[0].Addr;
  });
  for (std::vector<LineTableEntry> &I : EntryList)
    llvm::append_range(ModuleLineTable, I);

  return ModuleLineTable;
}

std::unique_ptr<IPDBEnumLineNumbers>
SymbolCache::findLineNumbersByVA(uint64_t VA, uint32_t Length) const {
  uint16_t Modi;
  if (!Session.moduleIndexForVA(VA, Modi))
    return nullptr;

  std::vector<LineTableEntry> Lines = findLineTable(Modi);
  if (Lines.empty())
    return nullptr;

  // Find the first line in the line table whose address is not greater than
  // the one we are searching for.
  auto LineIter = llvm::partition_point(Lines, [&](const LineTableEntry &E) {
    return (E.Addr < VA || (E.Addr == VA && E.IsTerminalEntry));
  });

  // Try to back up if we've gone too far.
  if (LineIter == Lines.end() || LineIter->Addr > VA) {
    if (LineIter == Lines.begin() || std::prev(LineIter)->IsTerminalEntry)
      return nullptr;
    --LineIter;
  }

  Expected<ModuleDebugStreamRef> ExpectedModS =
      Session.getModuleDebugStream(Modi);
  if (!ExpectedModS) {
    consumeError(ExpectedModS.takeError());
    return nullptr;
  }
  Expected<DebugChecksumsSubsectionRef> ExpectedChecksums =
      ExpectedModS->findChecksumsSubsection();
  if (!ExpectedChecksums) {
    consumeError(ExpectedChecksums.takeError());
    return nullptr;
  }

  // Populate a vector of NativeLineNumbers that have addresses in the given
  // address range.
  std::vector<NativeLineNumber> LineNumbers;
  while (LineIter != Lines.end()) {
    if (LineIter->IsTerminalEntry) {
      ++LineIter;
      continue;
    }

    // If the line is still within the address range, create a NativeLineNumber
    // and add to the list.
    if (LineIter->Addr > VA + Length)
      break;

    uint32_t LineSect, LineOff;
    Session.addressForVA(LineIter->Addr, LineSect, LineOff);
    uint32_t LineLength = std::next(LineIter)->Addr - LineIter->Addr;
    auto ChecksumIter =
        ExpectedChecksums->getArray().at(LineIter->FileNameIndex);
    uint32_t SrcFileId = getOrCreateSourceFile(*ChecksumIter);
    NativeLineNumber LineNum(Session, LineIter->Line, LineIter->ColumnNumber,
                             LineSect, LineOff, LineLength, SrcFileId, Modi);
    LineNumbers.push_back(LineNum);
    ++LineIter;
  }
  return std::make_unique<NativeEnumLineNumbers>(std::move(LineNumbers));
}

std::unique_ptr<PDBSymbolCompiland>
SymbolCache::getOrCreateCompiland(uint32_t Index) {
  if (!Dbi)
    return nullptr;

  if (Index >= Compilands.size())
    return nullptr;

  if (Compilands[Index] == 0) {
    const DbiModuleList &Modules = Dbi->modules();
    Compilands[Index] =
        createSymbol<NativeCompilandSymbol>(Modules.getModuleDescriptor(Index));
  }

  return Session.getConcreteSymbolById<PDBSymbolCompiland>(Compilands[Index]);
}

std::unique_ptr<IPDBSourceFile>
SymbolCache::getSourceFileById(SymIndexId FileId) const {
  assert(FileId < SourceFiles.size());

  // Id 0 is reserved.
  if (FileId == 0)
    return nullptr;

  return std::make_unique<NativeSourceFile>(*SourceFiles[FileId].get());
}

SymIndexId
SymbolCache::getOrCreateSourceFile(const FileChecksumEntry &Checksums) const {
  auto Iter = FileNameOffsetToId.find(Checksums.FileNameOffset);
  if (Iter != FileNameOffsetToId.end())
    return Iter->second;

  SymIndexId Id = SourceFiles.size();
  auto SrcFile = std::make_unique<NativeSourceFile>(Session, Id, Checksums);
  SourceFiles.push_back(std::move(SrcFile));
  FileNameOffsetToId[Checksums.FileNameOffset] = Id;
  return Id;
}


