//===-- LVCodeViewReader.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This implements the LVCodeViewReader class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/LogicalView/Readers/LVCodeViewReader.h"
#include "llvm/DebugInfo/CodeView/CVSymbolVisitor.h"
#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/EnumTables.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolVisitorCallbackPipeline.h"
#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/DebugInfo/PDB/GenericError.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/GlobalsStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::logicalview;
using namespace llvm::msf;
using namespace llvm::object;
using namespace llvm::pdb;

#define DEBUG_TYPE "CodeViewReader"

StringRef LVCodeViewReader::getSymbolKindName(SymbolKind Kind) {
  switch (Kind) {
#define SYMBOL_RECORD(EnumName, EnumVal, Name)                                 \
  case EnumName:                                                               \
    return #EnumName;
#include "llvm/DebugInfo/CodeView/CodeViewSymbols.def"
  default:
    return "UnknownSym";
  }
  llvm_unreachable("Unknown SymbolKind::Kind");
}

std::string LVCodeViewReader::formatRegisterId(RegisterId Register,
                                               CPUType CPU) {
#define RETURN_CASE(Enum, X, Ret)                                              \
  case Enum::X:                                                                \
    return Ret;

  if (CPU == CPUType::ARMNT) {
    switch (Register) {
#define CV_REGISTERS_ARM
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM

    default:
      break;
    }
  } else if (CPU == CPUType::ARM64) {
    switch (Register) {
#define CV_REGISTERS_ARM64
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_ARM64

    default:
      break;
    }
  } else {
    switch (Register) {
#define CV_REGISTERS_X86
#define CV_REGISTER(name, val) RETURN_CASE(RegisterId, name, #name)
#include "llvm/DebugInfo/CodeView/CodeViewRegisters.def"
#undef CV_REGISTER
#undef CV_REGISTERS_X86

    default:
      break;
    }
  }
  return "formatUnknownEnum(Id)";
}

void LVCodeViewReader::printRelocatedField(StringRef Label,
                                           const coff_section *CoffSection,
                                           uint32_t RelocOffset,
                                           uint32_t Offset,
                                           StringRef *RelocSym) {
  StringRef SymStorage;
  StringRef &Symbol = RelocSym ? *RelocSym : SymStorage;
  if (!resolveSymbolName(CoffSection, RelocOffset, Symbol))
    W.printSymbolOffset(Label, Symbol, Offset);
  else
    W.printHex(Label, RelocOffset);
}

void LVCodeViewReader::getLinkageName(const coff_section *CoffSection,
                                      uint32_t RelocOffset, uint32_t Offset,
                                      StringRef *RelocSym) {
  StringRef SymStorage;
  StringRef &Symbol = RelocSym ? *RelocSym : SymStorage;
  if (resolveSymbolName(CoffSection, RelocOffset, Symbol))
    Symbol = "";
}

Expected<StringRef>
LVCodeViewReader::getFileNameForFileOffset(uint32_t FileOffset,
                                           const SymbolGroup *SG) {
  if (SG) {
    Expected<StringRef> Filename = SG->getNameFromChecksums(FileOffset);
    if (!Filename) {
      consumeError(Filename.takeError());
      return StringRef("");
    }
    return *Filename;
  }

  // The file checksum subsection should precede all references to it.
  if (!CVFileChecksumTable.valid() || !CVStringTable.valid())
    return createStringError(object_error::parse_failed, getFileName());

  VarStreamArray<FileChecksumEntry>::Iterator Iter =
      CVFileChecksumTable.getArray().at(FileOffset);

  // Check if the file checksum table offset is valid.
  if (Iter == CVFileChecksumTable.end())
    return createStringError(object_error::parse_failed, getFileName());

  Expected<StringRef> NameOrErr = CVStringTable.getString(Iter->FileNameOffset);
  if (!NameOrErr)
    return createStringError(object_error::parse_failed, getFileName());
  return *NameOrErr;
}

Error LVCodeViewReader::printFileNameForOffset(StringRef Label,
                                               uint32_t FileOffset,
                                               const SymbolGroup *SG) {
  Expected<StringRef> NameOrErr = getFileNameForFileOffset(FileOffset, SG);
  if (!NameOrErr)
    return NameOrErr.takeError();
  W.printHex(Label, *NameOrErr, FileOffset);
  return Error::success();
}

void LVCodeViewReader::cacheRelocations() {
  for (const SectionRef &Section : getObj().sections()) {
    const coff_section *CoffSection = getObj().getCOFFSection(Section);

    for (const RelocationRef &Relocacion : Section.relocations())
      RelocMap[CoffSection].push_back(Relocacion);

    // Sort relocations by address.
    llvm::sort(RelocMap[CoffSection], [](RelocationRef L, RelocationRef R) {
      return L.getOffset() < R.getOffset();
    });
  }
}

// Given a section and an offset into this section the function returns the
// symbol used for the relocation at the offset.
Error LVCodeViewReader::resolveSymbol(const coff_section *CoffSection,
                                      uint64_t Offset, SymbolRef &Sym) {
  const auto &Relocations = RelocMap[CoffSection];
  basic_symbol_iterator SymI = getObj().symbol_end();
  for (const RelocationRef &Relocation : Relocations) {
    uint64_t RelocationOffset = Relocation.getOffset();

    if (RelocationOffset == Offset) {
      SymI = Relocation.getSymbol();
      break;
    }
  }
  if (SymI == getObj().symbol_end())
    return make_error<StringError>("Unknown Symbol", inconvertibleErrorCode());
  Sym = *SymI;
  return ErrorSuccess();
}

// Given a section and an offset into this section the function returns the
// name of the symbol used for the relocation at the offset.
Error LVCodeViewReader::resolveSymbolName(const coff_section *CoffSection,
                                          uint64_t Offset, StringRef &Name) {
  SymbolRef Symbol;
  if (Error E = resolveSymbol(CoffSection, Offset, Symbol))
    return E;
  Expected<StringRef> NameOrErr = Symbol.getName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  Name = *NameOrErr;
  return ErrorSuccess();
}

// CodeView and DWARF can have references to compiler generated elements,
// used for initialization. The MSVC includes in the PDBs, internal compile
// units, associated with the MS runtime support. We mark them as 'system'
// and they are printed only if the command line option 'internal=system'.
bool LVCodeViewReader::isSystemEntry(LVElement *Element, StringRef Name) const {
  Name = Name.empty() ? Element->getName() : Name;
  auto Find = [=](const char *String) -> bool { return Name.contains(String); };
  auto Starts = [=](const char *Pattern) -> bool {
    return Name.starts_with(Pattern);
  };
  auto CheckExclude = [&]() -> bool {
    if (Starts("__") || Starts("_PMD") || Starts("_PMFN"))
      return true;
    if (Find("_s__"))
      return true;
    if (Find("_CatchableType") || Find("_TypeDescriptor"))
      return true;
    if (Find("Intermediate\\vctools"))
      return true;
    if (Find("$initializer$") || Find("dynamic initializer"))
      return true;
    if (Find("`vftable'") || Find("_GLOBAL__sub"))
      return true;
    return false;
  };
  bool Excluded = CheckExclude();
  if (Excluded)
    Element->setIsSystem();

  return Excluded;
}

Error LVCodeViewReader::collectInlineeInfo(
    DebugInlineeLinesSubsectionRef &Lines, const llvm::pdb::SymbolGroup *SG) {
  for (const InlineeSourceLine &Line : Lines) {
    TypeIndex TIInlinee = Line.Header->Inlinee;
    uint32_t LineNumber = Line.Header->SourceLineNum;
    uint32_t FileOffset = Line.Header->FileID;
    LLVM_DEBUG({
      DictScope S(W, "InlineeSourceLine");
      LogicalVisitor.printTypeIndex("Inlinee", TIInlinee, StreamTPI);
      if (Error Err = printFileNameForOffset("FileID", FileOffset, SG))
        return Err;
      W.printNumber("SourceLineNum", LineNumber);

      if (Lines.hasExtraFiles()) {
        W.printNumber("ExtraFileCount", Line.ExtraFiles.size());
        ListScope ExtraFiles(W, "ExtraFiles");
        for (const ulittle32_t &FID : Line.ExtraFiles)
          if (Error Err = printFileNameForOffset("FileID", FID, SG))
            return Err;
      }
    });
    Expected<StringRef> NameOrErr = getFileNameForFileOffset(FileOffset, SG);
    if (!NameOrErr)
      return NameOrErr.takeError();
    LogicalVisitor.addInlineeInfo(TIInlinee, LineNumber, *NameOrErr);
  }

  return Error::success();
}

Error LVCodeViewReader::traverseInlineeLines(StringRef Subsection) {
  BinaryStreamReader SR(Subsection, llvm::endianness::little);
  DebugInlineeLinesSubsectionRef Lines;
  if (Error E = Lines.initialize(SR))
    return createStringError(errorToErrorCode(std::move(E)), getFileName());

  return collectInlineeInfo(Lines);
}

Error LVCodeViewReader::createLines(
    const FixedStreamArray<LineNumberEntry> &LineNumbers, LVAddress Addendum,
    uint32_t Segment, uint32_t Begin, uint32_t Size, uint32_t NameIndex,
    const SymbolGroup *SG) {
  LLVM_DEBUG({
    uint32_t End = Begin + Size;
    W.getOStream() << formatv("{0:x-4}:{1:x-8}-{2:x-8}\n", Segment, Begin, End);
  });

  for (const LineNumberEntry &Line : LineNumbers) {
    if (Line.Offset >= Size)
      return createStringError(object_error::parse_failed, getFileName());

    LineInfo LI(Line.Flags);

    LLVM_DEBUG({
      W.getOStream() << formatv(
          "{0} {1:x-8}\n", utostr(LI.getStartLine()),
          fmt_align(Begin + Line.Offset, AlignStyle::Right, 8, '0'));
    });

    // The 'processLines()' function will move each created logical line
    // to its enclosing logical scope, using the debug ranges information
    // and they will be released when its scope parent is deleted.
    LVLineDebug *LineDebug = createLineDebug();
    CULines.push_back(LineDebug);
    LVAddress Address = linearAddress(Segment, Begin + Line.Offset);
    LineDebug->setAddress(Address + Addendum);

    if (LI.isAlwaysStepInto())
      LineDebug->setIsAlwaysStepInto();
    else if (LI.isNeverStepInto())
      LineDebug->setIsNeverStepInto();
    else
      LineDebug->setLineNumber(LI.getStartLine());

    if (LI.isStatement())
      LineDebug->setIsNewStatement();

    Expected<StringRef> NameOrErr = getFileNameForFileOffset(NameIndex, SG);
    if (!NameOrErr)
      return NameOrErr.takeError();
    LineDebug->setFilename(*NameOrErr);
  }

  return Error::success();
}

Error LVCodeViewReader::initializeFileAndStringTables(
    BinaryStreamReader &Reader) {
  while (Reader.bytesRemaining() > 0 &&
         (!CVFileChecksumTable.valid() || !CVStringTable.valid())) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;

    if (Error E = Reader.readInteger(SubType))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());
    if (Error E = Reader.readInteger(SubSectionSize))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());

    StringRef Contents;
    if (Error E = Reader.readFixedString(Contents, SubSectionSize))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());

    BinaryStreamRef ST(Contents, llvm::endianness::little);
    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::FileChecksums:
      if (Error E = CVFileChecksumTable.initialize(ST))
        return createStringError(errorToErrorCode(std::move(E)), getFileName());
      break;
    case DebugSubsectionKind::StringTable:
      if (Error E = CVStringTable.initialize(ST))
        return createStringError(errorToErrorCode(std::move(E)), getFileName());
      break;
    default:
      break;
    }

    uint32_t PaddedSize = alignTo(SubSectionSize, 4);
    if (Error E = Reader.skip(PaddedSize - SubSectionSize))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());
  }

  return Error::success();
}

Error LVCodeViewReader::loadTypeServer(TypeServer2Record &TS) {
  LLVM_DEBUG({
    W.printString("Guid", formatv("{0}", TS.getGuid()).str());
    W.printNumber("Age", TS.getAge());
    W.printString("Name", TS.getName());
  });

  SmallString<128> ServerName(TS.getName());
  BuffOrErr = MemoryBuffer::getFile(ServerName);
  if (BuffOrErr.getError()) {
    // The server name does not exist. Try in the same directory as the
    // input file.
    ServerName = createAlternativePath(ServerName);
    BuffOrErr = MemoryBuffer::getFile(ServerName);
    if (BuffOrErr.getError()) {
      // For the error message, use the original type server name.
      return createStringError(errc::bad_file_descriptor,
                               "File '%s' does not exist.",
                               TS.getName().str().c_str());
    }
  }
  MemBuffer = std::move(BuffOrErr.get());

  // Check if the buffer corresponds to a PDB file.
  assert(identify_magic((*MemBuffer).getBuffer()) == file_magic::pdb &&
         "Invalid PDB file.");

  if (Error Err = loadDataForPDB(PDB_ReaderType::Native, ServerName, Session))
    return createStringError(errorToErrorCode(std::move(Err)), "%s",
                             ServerName.c_str());

  PdbSession.reset(static_cast<NativeSession *>(Session.release()));
  PDBFile &Pdb = PdbSession->getPDBFile();

  // Just because a file with a matching name was found and it was an actual
  // PDB file doesn't mean it matches. For it to match the InfoStream's GUID
  // must match the GUID specified in the TypeServer2 record.
  Expected<InfoStream &> expectedInfo = Pdb.getPDBInfoStream();
  if (!expectedInfo || expectedInfo->getGuid() != TS.getGuid())
    return createStringError(errc::invalid_argument, "signature_out_of_date");

  // The reader needs to switch to a type server, to process the types from
  // the server. We need to keep the original input source, as reading other
  // sections will require the input associated with the loaded object file.
  TypeServer = std::make_shared<InputFile>(&Pdb);
  LogicalVisitor.setInput(TypeServer);

  LazyRandomTypeCollection &Types = types();
  LazyRandomTypeCollection &Ids = ids();
  if (Error Err = traverseTypes(Pdb, Types, Ids))
    return Err;

  return Error::success();
}

Error LVCodeViewReader::loadPrecompiledObject(PrecompRecord &Precomp,
                                              CVTypeArray &CVTypesObj) {
  LLVM_DEBUG({
    W.printHex("Count", Precomp.getTypesCount());
    W.printHex("Signature", Precomp.getSignature());
    W.printString("PrecompFile", Precomp.getPrecompFilePath());
  });

  SmallString<128> ServerName(Precomp.getPrecompFilePath());
  BuffOrErr = MemoryBuffer::getFile(ServerName);
  if (BuffOrErr.getError()) {
    // The server name does not exist. Try in the directory as the input file.
    ServerName = createAlternativePath(ServerName);
    if (BuffOrErr.getError()) {
      // For the error message, use the original type server name.
      return createStringError(errc::bad_file_descriptor,
                               "File '%s' does not exist.",
                               Precomp.getPrecompFilePath().str().c_str());
    }
  }
  MemBuffer = std::move(BuffOrErr.get());

  Expected<std::unique_ptr<Binary>> BinOrErr = createBinary(*MemBuffer);
  if (errorToErrorCode(BinOrErr.takeError()))
    return createStringError(errc::not_supported,
                             "Binary object format in '%s' is not supported.",
                             ServerName.c_str());

  Binary &BinaryObj = *BinOrErr.get();
  if (!BinaryObj.isCOFF())
    return createStringError(errc::not_supported, "'%s' is not a COFF object.",
                             ServerName.c_str());

  Builder = std::make_unique<AppendingTypeTableBuilder>(BuilderAllocator);

  // The MSVC precompiled header object file, should contain just a single
  // ".debug$P" section.
  COFFObjectFile &Obj = *cast<COFFObjectFile>(&BinaryObj);
  for (const SectionRef &Section : Obj.sections()) {
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (!SectionNameOrErr)
      return SectionNameOrErr.takeError();
    if (*SectionNameOrErr == ".debug$P") {
      Expected<StringRef> DataOrErr = Section.getContents();
      if (!DataOrErr)
        return DataOrErr.takeError();
      uint32_t Magic;
      if (Error Err = consume(*DataOrErr, Magic))
        return Err;
      if (Magic != COFF::DEBUG_SECTION_MAGIC)
        return errorCodeToError(object_error::parse_failed);

      ReaderPrecomp = std::make_unique<BinaryStreamReader>(
          *DataOrErr, llvm::endianness::little);
      cantFail(
          ReaderPrecomp->readArray(CVTypesPrecomp, ReaderPrecomp->getLength()));

      // Append all the type records up to the LF_ENDPRECOMP marker and
      // check if the signatures match.
      for (const CVType &Type : CVTypesPrecomp) {
        ArrayRef<uint8_t> TypeData = Type.data();
        if (Type.kind() == LF_ENDPRECOMP) {
          EndPrecompRecord EndPrecomp = cantFail(
              TypeDeserializer::deserializeAs<EndPrecompRecord>(TypeData));
          if (Precomp.getSignature() != EndPrecomp.getSignature())
            return createStringError(errc::invalid_argument, "no matching pch");
          break;
        }
        Builder->insertRecordBytes(TypeData);
      }
      // Done processing .debug$P, break out of section loop.
      break;
    }
  }

  // Append all the type records, skipping the first record which is the
  // reference to the precompiled header object information.
  for (const CVType &Type : CVTypesObj) {
    ArrayRef<uint8_t> TypeData = Type.data();
    if (Type.kind() != LF_PRECOMP)
      Builder->insertRecordBytes(TypeData);
  }

  // Set up a type stream that refers to the added type records.
  Builder->ForEachRecord(
      [&](TypeIndex TI, const CVType &Type) { TypeArray.push_back(Type); });

  ItemStream =
      std::make_unique<BinaryItemStream<CVType>>(llvm::endianness::little);
  ItemStream->setItems(TypeArray);
  TypeStream.setUnderlyingStream(*ItemStream);

  PrecompHeader =
      std::make_shared<LazyRandomTypeCollection>(TypeStream, TypeArray.size());

  // Change the original input source to use the collected type records.
  LogicalVisitor.setInput(PrecompHeader);

  LazyRandomTypeCollection &Types = types();
  LazyRandomTypeCollection &Ids = ids();
  LVTypeVisitor TDV(W, &LogicalVisitor, Types, Ids, StreamTPI,
                    LogicalVisitor.getShared());
  return visitTypeStream(Types, TDV);
}

Error LVCodeViewReader::traverseTypeSection(StringRef SectionName,
                                            const SectionRef &Section) {
  LLVM_DEBUG({
    ListScope D(W, "CodeViewTypes");
    W.printNumber("Section", SectionName, getObj().getSectionID(Section));
  });

  Expected<StringRef> DataOrErr = Section.getContents();
  if (!DataOrErr)
    return DataOrErr.takeError();
  uint32_t Magic;
  if (Error Err = consume(*DataOrErr, Magic))
    return Err;
  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    return errorCodeToError(object_error::parse_failed);

  // Get the first type record. It will indicate if this object uses a type
  // server (/Zi) or a PCH file (/Yu).
  CVTypeArray CVTypes;
  BinaryStreamReader Reader(*DataOrErr, llvm::endianness::little);
  cantFail(Reader.readArray(CVTypes, Reader.getLength()));
  CVTypeArray::Iterator FirstType = CVTypes.begin();

  // The object was compiled with /Zi. It uses types from a type server PDB.
  if (FirstType->kind() == LF_TYPESERVER2) {
    TypeServer2Record TS = cantFail(
        TypeDeserializer::deserializeAs<TypeServer2Record>(FirstType->data()));
    return loadTypeServer(TS);
  }

  // The object was compiled with /Yc or /Yu. It uses types from another
  // object file with a matching signature.
  if (FirstType->kind() == LF_PRECOMP) {
    PrecompRecord Precomp = cantFail(
        TypeDeserializer::deserializeAs<PrecompRecord>(FirstType->data()));
    return loadPrecompiledObject(Precomp, CVTypes);
  }

  LazyRandomTypeCollection &Types = types();
  LazyRandomTypeCollection &Ids = ids();
  Types.reset(*DataOrErr, 100);
  LVTypeVisitor TDV(W, &LogicalVisitor, Types, Ids, StreamTPI,
                    LogicalVisitor.getShared());
  return visitTypeStream(Types, TDV);
}

Error LVCodeViewReader::traverseTypes(PDBFile &Pdb,
                                      LazyRandomTypeCollection &Types,
                                      LazyRandomTypeCollection &Ids) {
  // Traverse types (TPI and IPI).
  auto VisitTypes = [&](LazyRandomTypeCollection &Types,
                        LazyRandomTypeCollection &Ids,
                        SpecialStream StreamIdx) -> Error {
    LVTypeVisitor TDV(W, &LogicalVisitor, Types, Ids, StreamIdx,
                      LogicalVisitor.getShared());
    return visitTypeStream(Types, TDV);
  };

  Expected<TpiStream &> StreamTpiOrErr = Pdb.getPDBTpiStream();
  if (!StreamTpiOrErr)
    return StreamTpiOrErr.takeError();
  TpiStream &StreamTpi = *StreamTpiOrErr;
  StreamTpi.buildHashMap();
  LLVM_DEBUG({
    W.getOStream() << formatv("Showing {0:N} TPI records\n",
                              StreamTpi.getNumTypeRecords());
  });
  if (Error Err = VisitTypes(Types, Ids, StreamTPI))
    return Err;

  Expected<TpiStream &> StreamIpiOrErr = Pdb.getPDBIpiStream();
  if (!StreamIpiOrErr)
    return StreamIpiOrErr.takeError();
  TpiStream &StreamIpi = *StreamIpiOrErr;
  StreamIpi.buildHashMap();
  LLVM_DEBUG({
    W.getOStream() << formatv("Showing {0:N} IPI records\n",
                              StreamIpi.getNumTypeRecords());
  });
  return VisitTypes(Ids, Ids, StreamIPI);
}

Error LVCodeViewReader::traverseSymbolsSubsection(StringRef Subsection,
                                                  const SectionRef &Section,
                                                  StringRef SectionContents) {
  ArrayRef<uint8_t> BinaryData(Subsection.bytes_begin(),
                               Subsection.bytes_end());
  LVSymbolVisitorDelegate VisitorDelegate(this, Section, &getObj(),
                                          SectionContents);
  CVSymbolArray Symbols;
  BinaryStreamReader Reader(BinaryData, llvm::endianness::little);
  if (Error E = Reader.readArray(Symbols, Reader.getLength()))
    return createStringError(errorToErrorCode(std::move(E)), getFileName());

  LazyRandomTypeCollection &Types = types();
  LazyRandomTypeCollection &Ids = ids();
  SymbolVisitorCallbackPipeline Pipeline;
  SymbolDeserializer Deserializer(&VisitorDelegate,
                                  CodeViewContainer::ObjectFile);
  // As we are processing a COFF format, use TPI as IPI, so the generic code
  // to process the CodeView format does not contain any additional checks.
  LVSymbolVisitor Traverser(this, W, &LogicalVisitor, Types, Ids,
                            &VisitorDelegate, LogicalVisitor.getShared());

  Pipeline.addCallbackToPipeline(Deserializer);
  Pipeline.addCallbackToPipeline(Traverser);
  CVSymbolVisitor Visitor(Pipeline);
  return Visitor.visitSymbolStream(Symbols);
}

Error LVCodeViewReader::traverseSymbolSection(StringRef SectionName,
                                              const SectionRef &Section) {
  LLVM_DEBUG({
    ListScope D(W, "CodeViewDebugInfo");
    W.printNumber("Section", SectionName, getObj().getSectionID(Section));
  });

  Expected<StringRef> SectionOrErr = Section.getContents();
  if (!SectionOrErr)
    return SectionOrErr.takeError();
  StringRef SectionContents = *SectionOrErr;
  StringRef Data = SectionContents;

  SmallVector<StringRef, 10> SymbolNames;
  StringMap<StringRef> FunctionLineTables;

  uint32_t Magic;
  if (Error E = consume(Data, Magic))
    return createStringError(errorToErrorCode(std::move(E)), getFileName());

  if (Magic != COFF::DEBUG_SECTION_MAGIC)
    return createStringError(object_error::parse_failed, getFileName());

  BinaryStreamReader FSReader(Data, llvm::endianness::little);
  if (Error Err = initializeFileAndStringTables(FSReader))
    return Err;

  while (!Data.empty()) {
    // The section consists of a number of subsection in the following format:
    // |SubSectionType|SubSectionSize|Contents...|
    uint32_t SubType, SubSectionSize;
    if (Error E = consume(Data, SubType))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());
    if (Error E = consume(Data, SubSectionSize))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());

    // Process the subsection as normal even if the ignore bit is set.
    SubType &= ~SubsectionIgnoreFlag;

    // Get the contents of the subsection.
    if (SubSectionSize > Data.size())
      return createStringError(object_error::parse_failed, getFileName());
    StringRef Contents = Data.substr(0, SubSectionSize);

    // Add SubSectionSize to the current offset and align that offset
    // to find the next subsection.
    size_t SectionOffset = Data.data() - SectionContents.data();
    size_t NextOffset = SectionOffset + SubSectionSize;
    NextOffset = alignTo(NextOffset, 4);
    if (NextOffset > SectionContents.size())
      return createStringError(object_error::parse_failed, getFileName());
    Data = SectionContents.drop_front(NextOffset);

    switch (DebugSubsectionKind(SubType)) {
    case DebugSubsectionKind::Symbols:
      if (Error Err =
              traverseSymbolsSubsection(Contents, Section, SectionContents))
        return Err;
      break;

    case DebugSubsectionKind::InlineeLines:
      if (Error Err = traverseInlineeLines(Contents))
        return Err;
      break;

    case DebugSubsectionKind::Lines:
      // Holds a PC to file:line table. Some data to parse this subsection
      // is stored in the other subsections, so just check sanity and store
      // the pointers for deferred processing.

      // Collect function and ranges only if we need to print logical lines.
      if (options().getGeneralCollectRanges()) {

        if (SubSectionSize < 12) {
          // There should be at least three words to store two function
          // relocations and size of the code.
          return createStringError(object_error::parse_failed, getFileName());
        }

        StringRef SymbolName;
        if (Error Err = resolveSymbolName(getObj().getCOFFSection(Section),
                                          SectionOffset, SymbolName))
          return createStringError(errorToErrorCode(std::move(Err)),
                                   getFileName());

        LLVM_DEBUG({ W.printString("Symbol Name", SymbolName); });
        if (FunctionLineTables.count(SymbolName) != 0) {
          // Saw debug info for this function already?
          return createStringError(object_error::parse_failed, getFileName());
        }

        FunctionLineTables[SymbolName] = Contents;
        SymbolNames.push_back(SymbolName);
      }
      break;

    // Do nothing for unrecognized subsections.
    default:
      break;
    }
    W.flush();
  }

  // Traverse the line tables now that we've read all the subsections and
  // know all the required information.
  for (StringRef SymbolName : SymbolNames) {
    LLVM_DEBUG({
      ListScope S(W, "FunctionLineTable");
      W.printString("Symbol Name", SymbolName);
    });

    BinaryStreamReader Reader(FunctionLineTables[SymbolName],
                              llvm::endianness::little);

    DebugLinesSubsectionRef Lines;
    if (Error E = Lines.initialize(Reader))
      return createStringError(errorToErrorCode(std::move(E)), getFileName());

    // Find the associated symbol table information.
    LVSymbolTableEntry SymbolTableEntry = getSymbolTableEntry(SymbolName);
    LVScope *Function = SymbolTableEntry.Scope;
    if (!Function)
      continue;

    LVAddress Addendum = SymbolTableEntry.Address;
    LVSectionIndex SectionIndex = SymbolTableEntry.SectionIndex;

    // The given scope represents the function that contains the line numbers.
    // Collect all generated debug lines associated with the function.
    CULines.clear();

    // For the given scope, collect all scopes ranges.
    LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
    ScopesWithRanges->clear();
    Function->getRanges(*ScopesWithRanges);
    ScopesWithRanges->sort();

    uint16_t Segment = Lines.header()->RelocSegment;
    uint32_t Begin = Lines.header()->RelocOffset;
    uint32_t Size = Lines.header()->CodeSize;
    for (const LineColumnEntry &Block : Lines)
      if (Error Err = createLines(Block.LineNumbers, Addendum, Segment, Begin,
                                  Size, Block.NameIndex))
        return Err;

    // Include lines from any inlined functions within the current function.
    includeInlineeLines(SectionIndex, Function);

    if (Error Err = createInstructions(Function, SectionIndex))
      return Err;

    processLines(&CULines, SectionIndex, Function);
  }

  return Error::success();
}

void LVCodeViewReader::sortScopes() { Root->sort(); }

void LVCodeViewReader::print(raw_ostream &OS) const {
  LLVM_DEBUG(dbgs() << "CreateReaders\n");
}

void LVCodeViewReader::mapRangeAddress(const ObjectFile &Obj,
                                       const SectionRef &Section,
                                       bool IsComdat) {
  if (!Obj.isCOFF())
    return;

  const COFFObjectFile *Object = cast<COFFObjectFile>(&Obj);

  for (const SymbolRef &Sym : Object->symbols()) {
    if (!Section.containsSymbol(Sym))
      continue;

    COFFSymbolRef Symbol = Object->getCOFFSymbol(Sym);
    if (Symbol.getComplexType() != llvm::COFF::IMAGE_SYM_DTYPE_FUNCTION)
      continue;

    StringRef SymbolName;
    Expected<StringRef> SymNameOrErr = Object->getSymbolName(Symbol);
    if (!SymNameOrErr) {
      W.startLine() << "Invalid symbol name: " << Symbol.getSectionNumber()
                    << "\n";
      consumeError(SymNameOrErr.takeError());
      continue;
    }
    SymbolName = *SymNameOrErr;

    LLVM_DEBUG({
      Expected<const coff_section *> SectionOrErr =
          Object->getSection(Symbol.getSectionNumber());
      if (!SectionOrErr) {
        W.startLine() << "Invalid section number: " << Symbol.getSectionNumber()
                      << "\n";
        consumeError(SectionOrErr.takeError());
        return;
      }
      W.printNumber("Section #", Symbol.getSectionNumber());
      W.printString("Name", SymbolName);
      W.printHex("Value", Symbol.getValue());
    });

    // Record the symbol name (linkage) and its loading address.
    addToSymbolTable(SymbolName, Symbol.getValue(), Symbol.getSectionNumber(),
                     IsComdat);
  }
}

Error LVCodeViewReader::createScopes(COFFObjectFile &Obj) {
  if (Error Err = loadTargetInfo(Obj))
    return Err;

  // Initialization required when processing a COFF file:
  // Cache the symbols relocations.
  // Create a mapping for virtual addresses.
  // Get the functions entry points.
  cacheRelocations();
  mapVirtualAddress(Obj);

  for (const SectionRef &Section : Obj.sections()) {
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (!SectionNameOrErr)
      return SectionNameOrErr.takeError();
    // .debug$T is a standard CodeView type section, while .debug$P is the
    // same format but used for MSVC precompiled header object files.
    if (*SectionNameOrErr == ".debug$T" || *SectionNameOrErr == ".debug$P")
      if (Error Err = traverseTypeSection(*SectionNameOrErr, Section))
        return Err;
  }

  // Process collected namespaces.
  LogicalVisitor.processNamespaces();

  for (const SectionRef &Section : Obj.sections()) {
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (!SectionNameOrErr)
      return SectionNameOrErr.takeError();
    if (*SectionNameOrErr == ".debug$S")
      if (Error Err = traverseSymbolSection(*SectionNameOrErr, Section))
        return Err;
  }

  // Check if we have to close the Compile Unit scope.
  LogicalVisitor.closeScope();

  // Traverse the strings recorded and transform them into filenames.
  LogicalVisitor.processFiles();

  // Process collected element lines.
  LogicalVisitor.processLines();

  // Translate composite names into a single component.
  Root->transformScopedName();
  return Error::success();
}

Error LVCodeViewReader::createScopes(PDBFile &Pdb) {
  if (Error Err = loadTargetInfo(Pdb))
    return Err;

  if (!Pdb.hasPDBTpiStream() || !Pdb.hasPDBDbiStream())
    return Error::success();

  // Open the executable associated with the PDB file and get the section
  // addresses used to calculate linear addresses for CodeView Symbols.
  if (!ExePath.empty()) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
        MemoryBuffer::getFileOrSTDIN(ExePath);
    if (BuffOrErr.getError()) {
      return createStringError(errc::bad_file_descriptor,
                               "File '%s' does not exist.", ExePath.c_str());
    }
    BinaryBuffer = std::move(BuffOrErr.get());

    // Check if the buffer corresponds to a PECOFF executable.
    assert(identify_magic(BinaryBuffer->getBuffer()) ==
               file_magic::pecoff_executable &&
           "Invalid PECOFF executable file.");

    Expected<std::unique_ptr<Binary>> BinOrErr =
        createBinary(BinaryBuffer->getMemBufferRef());
    if (errorToErrorCode(BinOrErr.takeError())) {
      return createStringError(errc::not_supported,
                               "Binary object format in '%s' is not supported.",
                               ExePath.c_str());
    }
    BinaryExecutable = std::move(*BinOrErr);
    if (COFFObjectFile *COFFObject =
            dyn_cast<COFFObjectFile>(BinaryExecutable.get()))
      mapVirtualAddress(*COFFObject);
  }

  // In order to generate a full logical view, we have to traverse both
  // streams TPI and IPI if they are present. The following table gives
  // the stream where a specified type is located. If the IPI stream is
  // not present, all the types are located in the TPI stream.
  //
  // TPI Stream:
  //   LF_POINTER   LF_MODIFIER     LF_PROCEDURE    LF_MFUNCTION
  //   LF_LABEL     LF_ARGLIST      LF_FIELDLIST    LF_ARRAY
  //   LF_CLASS     LF_STRUCTURE    LF_INTERFACE    LF_UNION
  //   LF_ENUM      LF_TYPESERVER2  LF_VFTABLE      LF_VTSHAPE
  //   LF_BITFIELD  LF_METHODLIST   LF_PRECOMP      LF_ENDPRECOMP
  //
  // IPI stream:
  //   LF_FUNC_ID           LF_MFUNC_ID   LF_BUILDINFO
  //   LF_SUBSTR_LIST       LF_STRING_ID  LF_UDT_SRC_LINE
  //   LF_UDT_MOD_SRC_LINE

  LazyRandomTypeCollection &Types = types();
  LazyRandomTypeCollection &Ids = ids();
  if (Error Err = traverseTypes(Pdb, Types, Ids))
    return Err;

  // Process collected namespaces.
  LogicalVisitor.processNamespaces();

  LLVM_DEBUG({ W.getOStream() << "Traversing inlined lines\n"; });

  auto VisitInlineeLines = [&](int32_t Modi, const SymbolGroup &SG,
                               DebugInlineeLinesSubsectionRef &Lines) -> Error {
    return collectInlineeInfo(Lines, &SG);
  };

  FilterOptions Filters = {};
  LinePrinter Printer(/*Indent=*/2, false, nulls(), Filters);
  const PrintScope HeaderScope(Printer, /*IndentLevel=*/2);
  if (Error Err = iterateModuleSubsections<DebugInlineeLinesSubsectionRef>(
          Input, HeaderScope, VisitInlineeLines))
    return Err;

  // Traverse global symbols.
  LLVM_DEBUG({ W.getOStream() << "Traversing global symbols\n"; });
  if (Pdb.hasPDBGlobalsStream()) {
    Expected<GlobalsStream &> GlobalsOrErr = Pdb.getPDBGlobalsStream();
    if (!GlobalsOrErr)
      return GlobalsOrErr.takeError();
    GlobalsStream &Globals = *GlobalsOrErr;
    const GSIHashTable &Table = Globals.getGlobalsTable();
    Expected<SymbolStream &> ExpectedSyms = Pdb.getPDBSymbolStream();
    if (ExpectedSyms) {

      SymbolVisitorCallbackPipeline Pipeline;
      SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
      LVSymbolVisitor Traverser(this, W, &LogicalVisitor, Types, Ids, nullptr,
                                LogicalVisitor.getShared());

      // As the global symbols do not have an associated Compile Unit, create
      // one, as the container for all global symbols.
      RecordPrefix Prefix(SymbolKind::S_COMPILE3);
      CVSymbol Symbol(&Prefix, sizeof(Prefix));
      uint32_t Offset = 0;
      if (Error Err = Traverser.visitSymbolBegin(Symbol, Offset))
        consumeError(std::move(Err));
      else {
        // The CodeView compile unit containing the global symbols does not
        // have a name; generate one using its parent name (object filename)
        // follow by the '_global' string.
        std::string Name(CompileUnit->getParentScope()->getName());
        CompileUnit->setName(Name.append("_global"));

        Pipeline.addCallbackToPipeline(Deserializer);
        Pipeline.addCallbackToPipeline(Traverser);
        CVSymbolVisitor Visitor(Pipeline);

        BinaryStreamRef SymStream =
            ExpectedSyms->getSymbolArray().getUnderlyingStream();
        for (uint32_t PubSymOff : Table) {
          Expected<CVSymbol> Sym = readSymbolFromStream(SymStream, PubSymOff);
          if (Sym) {
            if (Error Err = Visitor.visitSymbolRecord(*Sym, PubSymOff))
              return createStringError(errorToErrorCode(std::move(Err)),
                                       getFileName());
          } else {
            consumeError(Sym.takeError());
          }
        }
      }

      LogicalVisitor.closeScope();
    } else {
      consumeError(ExpectedSyms.takeError());
    }
  }

  // Traverse symbols (DBI).
  LLVM_DEBUG({ W.getOStream() << "Traversing symbol groups\n"; });

  auto VisitSymbolGroup = [&](uint32_t Modi, const SymbolGroup &SG) -> Error {
    Expected<ModuleDebugStreamRef> ExpectedModS =
        getModuleDebugStream(Pdb, Modi);
    if (ExpectedModS) {
      ModuleDebugStreamRef &ModS = *ExpectedModS;

      LLVM_DEBUG({
        W.getOStream() << formatv("Traversing Group: Mod {0:4}\n", Modi);
      });

      SymbolVisitorCallbackPipeline Pipeline;
      SymbolDeserializer Deserializer(nullptr, CodeViewContainer::Pdb);
      LVSymbolVisitor Traverser(this, W, &LogicalVisitor, Types, Ids, nullptr,
                                LogicalVisitor.getShared());

      Pipeline.addCallbackToPipeline(Deserializer);
      Pipeline.addCallbackToPipeline(Traverser);
      CVSymbolVisitor Visitor(Pipeline);
      BinarySubstreamRef SS = ModS.getSymbolsSubstream();
      if (Error Err =
              Visitor.visitSymbolStream(ModS.getSymbolArray(), SS.Offset))
        return createStringError(errorToErrorCode(std::move(Err)),
                                 getFileName());
    } else {
      // If the module stream does not exist, it is not an error condition.
      consumeError(ExpectedModS.takeError());
    }

    return Error::success();
  };

  if (Error Err = iterateSymbolGroups(Input, HeaderScope, VisitSymbolGroup))
    return Err;

  // At this stage, the logical view contains all scopes, symbols and types.
  // For PDBs we can use the module id, to access its specific compile unit.
  // The line record addresses has been already resolved, so we can apply the
  // flow as when processing DWARF.

  LLVM_DEBUG({ W.getOStream() << "Traversing lines\n"; });

  // Record all line records for a Compile Unit.
  CULines.clear();

  auto VisitDebugLines = [this](int32_t Modi, const SymbolGroup &SG,
                                DebugLinesSubsectionRef &Lines) -> Error {
    if (!options().getPrintLines())
      return Error::success();

    uint16_t Segment = Lines.header()->RelocSegment;
    uint32_t Begin = Lines.header()->RelocOffset;
    uint32_t Size = Lines.header()->CodeSize;

    LLVM_DEBUG({ W.getOStream() << formatv("Modi = {0}\n", Modi); });

    // We have line information for a new module; finish processing the
    // collected information for the current module. Once it is done, start
    // recording the line information for the new module.
    if (CurrentModule != Modi) {
      if (Error Err = processModule())
        return Err;
      CULines.clear();
      CurrentModule = Modi;
    }

    for (const LineColumnEntry &Block : Lines)
      if (Error Err = createLines(Block.LineNumbers, /*Addendum=*/0, Segment,
                                  Begin, Size, Block.NameIndex, &SG))
        return Err;

    return Error::success();
  };

  if (Error Err = iterateModuleSubsections<DebugLinesSubsectionRef>(
          Input, HeaderScope, VisitDebugLines))
    return Err;

  // Check if we have to close the Compile Unit scope.
  LogicalVisitor.closeScope();

  // Process collected element lines.
  LogicalVisitor.processLines();

  // Translate composite names into a single component.
  Root->transformScopedName();
  return Error::success();
}

Error LVCodeViewReader::processModule() {
  if (LVScope *Scope = getScopeForModule(CurrentModule)) {
    CompileUnit = static_cast<LVScopeCompileUnit *>(Scope);

    LLVM_DEBUG({ dbgs() << "Processing Scope: " << Scope->getName() << "\n"; });

    // For the given compile unit, collect all scopes ranges.
    // For a complete ranges and lines mapping, the logical view support
    // needs for the compile unit to have a low and high pc values. We
    // can traverse the 'Modules' section and get the information for the
    // specific module. Another option, is from all the ranges collected
    // to take the first and last values.
    LVSectionIndex SectionIndex = DotTextSectionIndex;
    LVRange *ScopesWithRanges = getSectionRanges(SectionIndex);
    ScopesWithRanges->clear();
    CompileUnit->getRanges(*ScopesWithRanges);
    if (!ScopesWithRanges->empty())
      CompileUnit->addObject(ScopesWithRanges->getLower(),
                             ScopesWithRanges->getUpper());
    ScopesWithRanges->sort();

    if (Error Err = createInstructions())
      return Err;

    // Include lines from any inlined functions within the current function.
    includeInlineeLines(SectionIndex, Scope);

    processLines(&CULines, SectionIndex, nullptr);
  }

  return Error::success();
}

// In order to create the scopes, the CodeView Reader will:
// = Traverse the TPI/IPI stream (Type visitor):
// Collect forward references, scoped names, type indexes that will represent
// a logical element, strings, line records, linkage names.
// = Traverse the symbols section (Symbol visitor):
// Create the scopes tree and creates the required logical elements, by
// using the collected indexes from the type visitor.
Error LVCodeViewReader::createScopes() {
  LLVM_DEBUG({
    W.startLine() << "\n";
    W.printString("File", getFileName().str());
    W.printString("Exe", ExePath);
    W.printString("Format", FileFormatName);
  });

  if (Error Err = LVReader::createScopes())
    return Err;

  LogicalVisitor.setRoot(Root);

  if (isObj()) {
    if (Error Err = createScopes(getObj()))
      return Err;
  } else {
    if (Error Err = createScopes(getPdb()))
      return Err;
  }

  return Error::success();
}

Error LVCodeViewReader::loadTargetInfo(const ObjectFile &Obj) {
  // Detect the architecture from the object file. We usually don't need OS
  // info to lookup a target and create register info.
  Triple TT;
  TT.setArch(Triple::ArchType(Obj.getArch()));
  TT.setVendor(Triple::UnknownVendor);
  TT.setOS(Triple::UnknownOS);

  // Features to be passed to target/subtarget
  Expected<SubtargetFeatures> Features = Obj.getFeatures();
  SubtargetFeatures FeaturesValue;
  if (!Features) {
    consumeError(Features.takeError());
    FeaturesValue = SubtargetFeatures();
  }
  FeaturesValue = *Features;
  return loadGenericTargetInfo(TT.str(), FeaturesValue.getString());
}

Error LVCodeViewReader::loadTargetInfo(const PDBFile &Pdb) {
  Triple TT;
  TT.setArch(Triple::ArchType::x86_64);
  TT.setVendor(Triple::UnknownVendor);
  TT.setOS(Triple::Win32);

  StringRef TheFeature = "";

  return loadGenericTargetInfo(TT.str(), TheFeature);
}

std::string LVCodeViewReader::getRegisterName(LVSmall Opcode,
                                              ArrayRef<uint64_t> Operands) {
  // Get Compilation Unit CPU Type.
  CPUType CPU = getCompileUnitCPUType();
  // For CodeView the register always is in Operands[0];
  RegisterId Register = (RegisterId(Operands[0]));
  return formatRegisterId(Register, CPU);
}
