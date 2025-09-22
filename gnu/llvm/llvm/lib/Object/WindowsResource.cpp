//===-- WindowsResource.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the .res file class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/WindowsResource.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/WindowsMachineFlag.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ScopedPrinter.h"
#include <ctime>
#include <queue>

using namespace llvm;
using namespace object;

namespace llvm {
namespace object {

#define RETURN_IF_ERROR(X)                                                     \
  if (auto EC = X)                                                             \
    return EC;

#define UNWRAP_REF_OR_RETURN(Name, Expr)                                       \
  auto Name##OrErr = Expr;                                                     \
  if (!Name##OrErr)                                                            \
    return Name##OrErr.takeError();                                            \
  const auto &Name = *Name##OrErr;

#define UNWRAP_OR_RETURN(Name, Expr)                                           \
  auto Name##OrErr = Expr;                                                     \
  if (!Name##OrErr)                                                            \
    return Name##OrErr.takeError();                                            \
  auto Name = *Name##OrErr;

const uint32_t MIN_HEADER_SIZE = 7 * sizeof(uint32_t) + 2 * sizeof(uint16_t);

// COFF files seem to be inconsistent with alignment between sections, just use
// 8-byte because it makes everyone happy.
const uint32_t SECTION_ALIGNMENT = sizeof(uint64_t);

WindowsResource::WindowsResource(MemoryBufferRef Source)
    : Binary(Binary::ID_WinRes, Source) {
  size_t LeadingSize = WIN_RES_MAGIC_SIZE + WIN_RES_NULL_ENTRY_SIZE;
  BBS = BinaryByteStream(Data.getBuffer().drop_front(LeadingSize),
                         llvm::endianness::little);
}

// static
Expected<std::unique_ptr<WindowsResource>>
WindowsResource::createWindowsResource(MemoryBufferRef Source) {
  if (Source.getBufferSize() < WIN_RES_MAGIC_SIZE + WIN_RES_NULL_ENTRY_SIZE)
    return make_error<GenericBinaryError>(
        Source.getBufferIdentifier() + ": too small to be a resource file",
        object_error::invalid_file_type);
  std::unique_ptr<WindowsResource> Ret(new WindowsResource(Source));
  return std::move(Ret);
}

Expected<ResourceEntryRef> WindowsResource::getHeadEntry() {
  if (BBS.getLength() < sizeof(WinResHeaderPrefix) + sizeof(WinResHeaderSuffix))
    return make_error<EmptyResError>(getFileName() + " contains no entries",
                                     object_error::unexpected_eof);
  return ResourceEntryRef::create(BinaryStreamRef(BBS), this);
}

ResourceEntryRef::ResourceEntryRef(BinaryStreamRef Ref,
                                   const WindowsResource *Owner)
    : Reader(Ref), Owner(Owner) {}

Expected<ResourceEntryRef>
ResourceEntryRef::create(BinaryStreamRef BSR, const WindowsResource *Owner) {
  auto Ref = ResourceEntryRef(BSR, Owner);
  if (auto E = Ref.loadNext())
    return E;
  return Ref;
}

Error ResourceEntryRef::moveNext(bool &End) {
  // Reached end of all the entries.
  if (Reader.bytesRemaining() == 0) {
    End = true;
    return Error::success();
  }
  RETURN_IF_ERROR(loadNext());

  return Error::success();
}

static Error readStringOrId(BinaryStreamReader &Reader, uint16_t &ID,
                            ArrayRef<UTF16> &Str, bool &IsString) {
  uint16_t IDFlag;
  RETURN_IF_ERROR(Reader.readInteger(IDFlag));
  IsString = IDFlag != 0xffff;

  if (IsString) {
    Reader.setOffset(
        Reader.getOffset() -
        sizeof(uint16_t)); // Re-read the bytes which we used to check the flag.
    RETURN_IF_ERROR(Reader.readWideString(Str));
  } else
    RETURN_IF_ERROR(Reader.readInteger(ID));

  return Error::success();
}

Error ResourceEntryRef::loadNext() {
  const WinResHeaderPrefix *Prefix;
  RETURN_IF_ERROR(Reader.readObject(Prefix));

  if (Prefix->HeaderSize < MIN_HEADER_SIZE)
    return make_error<GenericBinaryError>(Owner->getFileName() +
                                              ": header size too small",
                                          object_error::parse_failed);

  RETURN_IF_ERROR(readStringOrId(Reader, TypeID, Type, IsStringType));

  RETURN_IF_ERROR(readStringOrId(Reader, NameID, Name, IsStringName));

  RETURN_IF_ERROR(Reader.padToAlignment(WIN_RES_HEADER_ALIGNMENT));

  RETURN_IF_ERROR(Reader.readObject(Suffix));

  RETURN_IF_ERROR(Reader.readArray(Data, Prefix->DataSize));

  RETURN_IF_ERROR(Reader.padToAlignment(WIN_RES_DATA_ALIGNMENT));

  return Error::success();
}

WindowsResourceParser::WindowsResourceParser(bool MinGW)
    : Root(false), MinGW(MinGW) {}

void printResourceTypeName(uint16_t TypeID, raw_ostream &OS) {
  switch (TypeID) {
  case  1: OS << "CURSOR (ID 1)"; break;
  case  2: OS << "BITMAP (ID 2)"; break;
  case  3: OS << "ICON (ID 3)"; break;
  case  4: OS << "MENU (ID 4)"; break;
  case  5: OS << "DIALOG (ID 5)"; break;
  case  6: OS << "STRINGTABLE (ID 6)"; break;
  case  7: OS << "FONTDIR (ID 7)"; break;
  case  8: OS << "FONT (ID 8)"; break;
  case  9: OS << "ACCELERATOR (ID 9)"; break;
  case 10: OS << "RCDATA (ID 10)"; break;
  case 11: OS << "MESSAGETABLE (ID 11)"; break;
  case 12: OS << "GROUP_CURSOR (ID 12)"; break;
  case 14: OS << "GROUP_ICON (ID 14)"; break;
  case 16: OS << "VERSIONINFO (ID 16)"; break;
  case 17: OS << "DLGINCLUDE (ID 17)"; break;
  case 19: OS << "PLUGPLAY (ID 19)"; break;
  case 20: OS << "VXD (ID 20)"; break;
  case 21: OS << "ANICURSOR (ID 21)"; break;
  case 22: OS << "ANIICON (ID 22)"; break;
  case 23: OS << "HTML (ID 23)"; break;
  case 24: OS << "MANIFEST (ID 24)"; break;
  default: OS << "ID " << TypeID; break;
  }
}

static bool convertUTF16LEToUTF8String(ArrayRef<UTF16> Src, std::string &Out) {
  if (!sys::IsBigEndianHost)
    return convertUTF16ToUTF8String(Src, Out);

  std::vector<UTF16> EndianCorrectedSrc;
  EndianCorrectedSrc.resize(Src.size() + 1);
  llvm::copy(Src, EndianCorrectedSrc.begin() + 1);
  EndianCorrectedSrc[0] = UNI_UTF16_BYTE_ORDER_MARK_SWAPPED;
  return convertUTF16ToUTF8String(ArrayRef(EndianCorrectedSrc), Out);
}

static std::string makeDuplicateResourceError(
    const ResourceEntryRef &Entry, StringRef File1, StringRef File2) {
  std::string Ret;
  raw_string_ostream OS(Ret);

  OS << "duplicate resource:";

  OS << " type ";
  if (Entry.checkTypeString()) {
    std::string UTF8;
    if (!convertUTF16LEToUTF8String(Entry.getTypeString(), UTF8))
      UTF8 = "(failed conversion from UTF16)";
    OS << '\"' << UTF8 << '\"';
  } else
    printResourceTypeName(Entry.getTypeID(), OS);

  OS << "/name ";
  if (Entry.checkNameString()) {
    std::string UTF8;
    if (!convertUTF16LEToUTF8String(Entry.getNameString(), UTF8))
      UTF8 = "(failed conversion from UTF16)";
    OS << '\"' << UTF8 << '\"';
  } else {
    OS << "ID " << Entry.getNameID();
  }

  OS << "/language " << Entry.getLanguage() << ", in " << File1 << " and in "
     << File2;

  return OS.str();
}

static void printStringOrID(const WindowsResourceParser::StringOrID &S,
                            raw_string_ostream &OS, bool IsType, bool IsID) {
  if (S.IsString) {
    std::string UTF8;
    if (!convertUTF16LEToUTF8String(S.String, UTF8))
      UTF8 = "(failed conversion from UTF16)";
    OS << '\"' << UTF8 << '\"';
  } else if (IsType)
    printResourceTypeName(S.ID, OS);
  else if (IsID)
    OS << "ID " << S.ID;
  else
    OS << S.ID;
}

static std::string makeDuplicateResourceError(
    const std::vector<WindowsResourceParser::StringOrID> &Context,
    StringRef File1, StringRef File2) {
  std::string Ret;
  raw_string_ostream OS(Ret);

  OS << "duplicate resource:";

  if (Context.size() >= 1) {
    OS << " type ";
    printStringOrID(Context[0], OS, /* IsType */ true, /* IsID */ true);
  }

  if (Context.size() >= 2) {
    OS << "/name ";
    printStringOrID(Context[1], OS, /* IsType */ false, /* IsID */ true);
  }

  if (Context.size() >= 3) {
    OS << "/language ";
    printStringOrID(Context[2], OS, /* IsType */ false, /* IsID */ false);
  }
  OS << ", in " << File1 << " and in " << File2;

  return OS.str();
}

// MinGW specific. Remove default manifests (with language zero) if there are
// other manifests present, and report an error if there are more than one
// manifest with a non-zero language code.
// GCC has the concept of a default manifest resource object, which gets
// linked in implicitly if present. This default manifest has got language
// id zero, and should be dropped silently if there's another manifest present.
// If the user resources surprisignly had a manifest with language id zero,
// we should also ignore the duplicate default manifest.
void WindowsResourceParser::cleanUpManifests(
    std::vector<std::string> &Duplicates) {
  auto TypeIt = Root.IDChildren.find(/* RT_MANIFEST */ 24);
  if (TypeIt == Root.IDChildren.end())
    return;

  TreeNode *TypeNode = TypeIt->second.get();
  auto NameIt =
      TypeNode->IDChildren.find(/* CREATEPROCESS_MANIFEST_RESOURCE_ID */ 1);
  if (NameIt == TypeNode->IDChildren.end())
    return;

  TreeNode *NameNode = NameIt->second.get();
  if (NameNode->IDChildren.size() <= 1)
    return; // None or one manifest present, all good.

  // If we have more than one manifest, drop the language zero one if present,
  // and check again.
  auto LangZeroIt = NameNode->IDChildren.find(0);
  if (LangZeroIt != NameNode->IDChildren.end() &&
      LangZeroIt->second->IsDataNode) {
    uint32_t RemovedIndex = LangZeroIt->second->DataIndex;
    NameNode->IDChildren.erase(LangZeroIt);
    Data.erase(Data.begin() + RemovedIndex);
    Root.shiftDataIndexDown(RemovedIndex);

    // If we're now down to one manifest, all is good.
    if (NameNode->IDChildren.size() <= 1)
      return;
  }

  // More than one non-language-zero manifest
  auto FirstIt = NameNode->IDChildren.begin();
  uint32_t FirstLang = FirstIt->first;
  TreeNode *FirstNode = FirstIt->second.get();
  auto LastIt = NameNode->IDChildren.rbegin();
  uint32_t LastLang = LastIt->first;
  TreeNode *LastNode = LastIt->second.get();
  Duplicates.push_back(
      ("duplicate non-default manifests with languages " + Twine(FirstLang) +
       " in " + InputFilenames[FirstNode->Origin] + " and " + Twine(LastLang) +
       " in " + InputFilenames[LastNode->Origin])
          .str());
}

// Ignore duplicates of manifests with language zero (the default manifest),
// in case the user has provided a manifest with that language id. See
// the function comment above for context. Only returns true if MinGW is set
// to true.
bool WindowsResourceParser::shouldIgnoreDuplicate(
    const ResourceEntryRef &Entry) const {
  return MinGW && !Entry.checkTypeString() &&
         Entry.getTypeID() == /* RT_MANIFEST */ 24 &&
         !Entry.checkNameString() &&
         Entry.getNameID() == /* CREATEPROCESS_MANIFEST_RESOURCE_ID */ 1 &&
         Entry.getLanguage() == 0;
}

bool WindowsResourceParser::shouldIgnoreDuplicate(
    const std::vector<StringOrID> &Context) const {
  return MinGW && Context.size() == 3 && !Context[0].IsString &&
         Context[0].ID == /* RT_MANIFEST */ 24 && !Context[1].IsString &&
         Context[1].ID == /* CREATEPROCESS_MANIFEST_RESOURCE_ID */ 1 &&
         !Context[2].IsString && Context[2].ID == 0;
}

Error WindowsResourceParser::parse(WindowsResource *WR,
                                   std::vector<std::string> &Duplicates) {
  auto EntryOrErr = WR->getHeadEntry();
  if (!EntryOrErr) {
    auto E = EntryOrErr.takeError();
    if (E.isA<EmptyResError>()) {
      // Check if the .res file contains no entries.  In this case we don't have
      // to throw an error but can rather just return without parsing anything.
      // This applies for files which have a valid PE header magic and the
      // mandatory empty null resource entry.  Files which do not fit this
      // criteria would have already been filtered out by
      // WindowsResource::createWindowsResource().
      consumeError(std::move(E));
      return Error::success();
    }
    return E;
  }

  ResourceEntryRef Entry = EntryOrErr.get();
  uint32_t Origin = InputFilenames.size();
  InputFilenames.push_back(std::string(WR->getFileName()));
  bool End = false;
  while (!End) {

    TreeNode *Node;
    bool IsNewNode = Root.addEntry(Entry, Origin, Data, StringTable, Node);
    if (!IsNewNode) {
      if (!shouldIgnoreDuplicate(Entry))
        Duplicates.push_back(makeDuplicateResourceError(
            Entry, InputFilenames[Node->Origin], WR->getFileName()));
    }

    RETURN_IF_ERROR(Entry.moveNext(End));
  }

  return Error::success();
}

Error WindowsResourceParser::parse(ResourceSectionRef &RSR, StringRef Filename,
                                   std::vector<std::string> &Duplicates) {
  UNWRAP_REF_OR_RETURN(BaseTable, RSR.getBaseTable());
  uint32_t Origin = InputFilenames.size();
  InputFilenames.push_back(std::string(Filename));
  std::vector<StringOrID> Context;
  return addChildren(Root, RSR, BaseTable, Origin, Context, Duplicates);
}

void WindowsResourceParser::printTree(raw_ostream &OS) const {
  ScopedPrinter Writer(OS);
  Root.print(Writer, "Resource Tree");
}

bool WindowsResourceParser::TreeNode::addEntry(
    const ResourceEntryRef &Entry, uint32_t Origin,
    std::vector<std::vector<uint8_t>> &Data,
    std::vector<std::vector<UTF16>> &StringTable, TreeNode *&Result) {
  TreeNode &TypeNode = addTypeNode(Entry, StringTable);
  TreeNode &NameNode = TypeNode.addNameNode(Entry, StringTable);
  return NameNode.addLanguageNode(Entry, Origin, Data, Result);
}

Error WindowsResourceParser::addChildren(TreeNode &Node,
                                         ResourceSectionRef &RSR,
                                         const coff_resource_dir_table &Table,
                                         uint32_t Origin,
                                         std::vector<StringOrID> &Context,
                                         std::vector<std::string> &Duplicates) {

  for (int i = 0; i < Table.NumberOfNameEntries + Table.NumberOfIDEntries;
       i++) {
    UNWRAP_REF_OR_RETURN(Entry, RSR.getTableEntry(Table, i));
    TreeNode *Child;

    if (Entry.Offset.isSubDir()) {

      // Create a new subdirectory and recurse
      if (i < Table.NumberOfNameEntries) {
        UNWRAP_OR_RETURN(NameString, RSR.getEntryNameString(Entry));
        Child = &Node.addNameChild(NameString, StringTable);
        Context.push_back(StringOrID(NameString));
      } else {
        Child = &Node.addIDChild(Entry.Identifier.ID);
        Context.push_back(StringOrID(Entry.Identifier.ID));
      }

      UNWRAP_REF_OR_RETURN(NextTable, RSR.getEntrySubDir(Entry));
      Error E =
          addChildren(*Child, RSR, NextTable, Origin, Context, Duplicates);
      if (E)
        return E;
      Context.pop_back();

    } else {

      // Data leaves are supposed to have a numeric ID as identifier (language).
      if (Table.NumberOfNameEntries > 0)
        return createStringError(object_error::parse_failed,
                                 "unexpected string key for data object");

      // Try adding a data leaf
      UNWRAP_REF_OR_RETURN(DataEntry, RSR.getEntryData(Entry));
      TreeNode *Child;
      Context.push_back(StringOrID(Entry.Identifier.ID));
      bool Added = Node.addDataChild(Entry.Identifier.ID, Table.MajorVersion,
                                     Table.MinorVersion, Table.Characteristics,
                                     Origin, Data.size(), Child);
      if (Added) {
        UNWRAP_OR_RETURN(Contents, RSR.getContents(DataEntry));
        Data.push_back(ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(Contents.data()),
            Contents.size()));
      } else {
        if (!shouldIgnoreDuplicate(Context))
          Duplicates.push_back(makeDuplicateResourceError(
              Context, InputFilenames[Child->Origin], InputFilenames.back()));
      }
      Context.pop_back();

    }
  }
  return Error::success();
}

WindowsResourceParser::TreeNode::TreeNode(uint32_t StringIndex)
    : StringIndex(StringIndex) {}

WindowsResourceParser::TreeNode::TreeNode(uint16_t MajorVersion,
                                          uint16_t MinorVersion,
                                          uint32_t Characteristics,
                                          uint32_t Origin, uint32_t DataIndex)
    : IsDataNode(true), DataIndex(DataIndex), MajorVersion(MajorVersion),
      MinorVersion(MinorVersion), Characteristics(Characteristics),
      Origin(Origin) {}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createStringNode(uint32_t Index) {
  return std::unique_ptr<TreeNode>(new TreeNode(Index));
}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createIDNode() {
  return std::unique_ptr<TreeNode>(new TreeNode(0));
}

std::unique_ptr<WindowsResourceParser::TreeNode>
WindowsResourceParser::TreeNode::createDataNode(uint16_t MajorVersion,
                                                uint16_t MinorVersion,
                                                uint32_t Characteristics,
                                                uint32_t Origin,
                                                uint32_t DataIndex) {
  return std::unique_ptr<TreeNode>(new TreeNode(
      MajorVersion, MinorVersion, Characteristics, Origin, DataIndex));
}

WindowsResourceParser::TreeNode &WindowsResourceParser::TreeNode::addTypeNode(
    const ResourceEntryRef &Entry,
    std::vector<std::vector<UTF16>> &StringTable) {
  if (Entry.checkTypeString())
    return addNameChild(Entry.getTypeString(), StringTable);
  else
    return addIDChild(Entry.getTypeID());
}

WindowsResourceParser::TreeNode &WindowsResourceParser::TreeNode::addNameNode(
    const ResourceEntryRef &Entry,
    std::vector<std::vector<UTF16>> &StringTable) {
  if (Entry.checkNameString())
    return addNameChild(Entry.getNameString(), StringTable);
  else
    return addIDChild(Entry.getNameID());
}

bool WindowsResourceParser::TreeNode::addLanguageNode(
    const ResourceEntryRef &Entry, uint32_t Origin,
    std::vector<std::vector<uint8_t>> &Data, TreeNode *&Result) {
  bool Added = addDataChild(Entry.getLanguage(), Entry.getMajorVersion(),
                            Entry.getMinorVersion(), Entry.getCharacteristics(),
                            Origin, Data.size(), Result);
  if (Added)
    Data.push_back(Entry.getData());
  return Added;
}

bool WindowsResourceParser::TreeNode::addDataChild(
    uint32_t ID, uint16_t MajorVersion, uint16_t MinorVersion,
    uint32_t Characteristics, uint32_t Origin, uint32_t DataIndex,
    TreeNode *&Result) {
  auto NewChild = createDataNode(MajorVersion, MinorVersion, Characteristics,
                                 Origin, DataIndex);
  auto ElementInserted = IDChildren.emplace(ID, std::move(NewChild));
  Result = ElementInserted.first->second.get();
  return ElementInserted.second;
}

WindowsResourceParser::TreeNode &WindowsResourceParser::TreeNode::addIDChild(
    uint32_t ID) {
  auto Child = IDChildren.find(ID);
  if (Child == IDChildren.end()) {
    auto NewChild = createIDNode();
    WindowsResourceParser::TreeNode &Node = *NewChild;
    IDChildren.emplace(ID, std::move(NewChild));
    return Node;
  } else
    return *(Child->second);
}

WindowsResourceParser::TreeNode &WindowsResourceParser::TreeNode::addNameChild(
    ArrayRef<UTF16> NameRef, std::vector<std::vector<UTF16>> &StringTable) {
  std::string NameString;
  convertUTF16LEToUTF8String(NameRef, NameString);

  auto Child = StringChildren.find(NameString);
  if (Child == StringChildren.end()) {
    auto NewChild = createStringNode(StringTable.size());
    StringTable.push_back(NameRef);
    WindowsResourceParser::TreeNode &Node = *NewChild;
    StringChildren.emplace(NameString, std::move(NewChild));
    return Node;
  } else
    return *(Child->second);
}

void WindowsResourceParser::TreeNode::print(ScopedPrinter &Writer,
                                            StringRef Name) const {
  ListScope NodeScope(Writer, Name);
  for (auto const &Child : StringChildren) {
    Child.second->print(Writer, Child.first);
  }
  for (auto const &Child : IDChildren) {
    Child.second->print(Writer, to_string(Child.first));
  }
}

// This function returns the size of the entire resource tree, including
// directory tables, directory entries, and data entries.  It does not include
// the directory strings or the relocations of the .rsrc section.
uint32_t WindowsResourceParser::TreeNode::getTreeSize() const {
  uint32_t Size = (IDChildren.size() + StringChildren.size()) *
                  sizeof(coff_resource_dir_entry);

  // Reached a node pointing to a data entry.
  if (IsDataNode) {
    Size += sizeof(coff_resource_data_entry);
    return Size;
  }

  // If the node does not point to data, it must have a directory table pointing
  // to other nodes.
  Size += sizeof(coff_resource_dir_table);

  for (auto const &Child : StringChildren) {
    Size += Child.second->getTreeSize();
  }
  for (auto const &Child : IDChildren) {
    Size += Child.second->getTreeSize();
  }
  return Size;
}

// Shift DataIndex of all data children with an Index greater or equal to the
// given one, to fill a gap from removing an entry from the Data vector.
void WindowsResourceParser::TreeNode::shiftDataIndexDown(uint32_t Index) {
  if (IsDataNode && DataIndex >= Index) {
    DataIndex--;
  } else {
    for (auto &Child : IDChildren)
      Child.second->shiftDataIndexDown(Index);
    for (auto &Child : StringChildren)
      Child.second->shiftDataIndexDown(Index);
  }
}

class WindowsResourceCOFFWriter {
public:
  WindowsResourceCOFFWriter(COFF::MachineTypes MachineType,
                            const WindowsResourceParser &Parser, Error &E);
  std::unique_ptr<MemoryBuffer> write(uint32_t TimeDateStamp);

private:
  void performFileLayout();
  void performSectionOneLayout();
  void performSectionTwoLayout();
  void writeCOFFHeader(uint32_t TimeDateStamp);
  void writeFirstSectionHeader();
  void writeSecondSectionHeader();
  void writeFirstSection();
  void writeSecondSection();
  void writeSymbolTable();
  void writeStringTable();
  void writeDirectoryTree();
  void writeDirectoryStringTable();
  void writeFirstSectionRelocations();
  std::unique_ptr<WritableMemoryBuffer> OutputBuffer;
  char *BufferStart;
  uint64_t CurrentOffset = 0;
  COFF::MachineTypes MachineType;
  const WindowsResourceParser::TreeNode &Resources;
  const ArrayRef<std::vector<uint8_t>> Data;
  uint64_t FileSize;
  uint32_t SymbolTableOffset;
  uint32_t SectionOneSize;
  uint32_t SectionOneOffset;
  uint32_t SectionOneRelocations;
  uint32_t SectionTwoSize;
  uint32_t SectionTwoOffset;
  const ArrayRef<std::vector<UTF16>> StringTable;
  std::vector<uint32_t> StringTableOffsets;
  std::vector<uint32_t> DataOffsets;
  std::vector<uint32_t> RelocationAddresses;
};

WindowsResourceCOFFWriter::WindowsResourceCOFFWriter(
    COFF::MachineTypes MachineType, const WindowsResourceParser &Parser,
    Error &E)
    : MachineType(MachineType), Resources(Parser.getTree()),
      Data(Parser.getData()), StringTable(Parser.getStringTable()) {
  performFileLayout();

  OutputBuffer = WritableMemoryBuffer::getNewMemBuffer(
      FileSize, "internal .obj file created from .res files");
}

void WindowsResourceCOFFWriter::performFileLayout() {
  // Add size of COFF header.
  FileSize = COFF::Header16Size;

  // one .rsrc section header for directory tree, another for resource data.
  FileSize += 2 * COFF::SectionSize;

  performSectionOneLayout();
  performSectionTwoLayout();

  // We have reached the address of the symbol table.
  SymbolTableOffset = FileSize;

  FileSize += COFF::Symbol16Size;     // size of the @feat.00 symbol.
  FileSize += 4 * COFF::Symbol16Size; // symbol + aux for each section.
  FileSize += Data.size() * COFF::Symbol16Size; // 1 symbol per resource.
  FileSize += 4; // four null bytes for the string table.
}

void WindowsResourceCOFFWriter::performSectionOneLayout() {
  SectionOneOffset = FileSize;

  SectionOneSize = Resources.getTreeSize();
  uint32_t CurrentStringOffset = SectionOneSize;
  uint32_t TotalStringTableSize = 0;
  for (auto const &String : StringTable) {
    StringTableOffsets.push_back(CurrentStringOffset);
    uint32_t StringSize = String.size() * sizeof(UTF16) + sizeof(uint16_t);
    CurrentStringOffset += StringSize;
    TotalStringTableSize += StringSize;
  }
  SectionOneSize += alignTo(TotalStringTableSize, sizeof(uint32_t));

  // account for the relocations of section one.
  SectionOneRelocations = FileSize + SectionOneSize;
  FileSize += SectionOneSize;
  FileSize +=
      Data.size() * COFF::RelocationSize; // one relocation for each resource.
  FileSize = alignTo(FileSize, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::performSectionTwoLayout() {
  // add size of .rsrc$2 section, which contains all resource data on 8-byte
  // alignment.
  SectionTwoOffset = FileSize;
  SectionTwoSize = 0;
  for (auto const &Entry : Data) {
    DataOffsets.push_back(SectionTwoSize);
    SectionTwoSize += alignTo(Entry.size(), sizeof(uint64_t));
  }
  FileSize += SectionTwoSize;
  FileSize = alignTo(FileSize, SECTION_ALIGNMENT);
}

std::unique_ptr<MemoryBuffer>
WindowsResourceCOFFWriter::write(uint32_t TimeDateStamp) {
  BufferStart = OutputBuffer->getBufferStart();

  writeCOFFHeader(TimeDateStamp);
  writeFirstSectionHeader();
  writeSecondSectionHeader();
  writeFirstSection();
  writeSecondSection();
  writeSymbolTable();
  writeStringTable();

  return std::move(OutputBuffer);
}

// According to COFF specification, if the Src has a size equal to Dest,
// it's okay to *not* copy the trailing zero.
static void coffnamecpy(char (&Dest)[COFF::NameSize], StringRef Src) {
  assert(Src.size() <= COFF::NameSize &&
         "Src is larger than COFF::NameSize");
  assert((Src.size() == COFF::NameSize || Dest[Src.size()] == '\0') &&
         "Dest not zeroed upon initialization");
  memcpy(Dest, Src.data(), Src.size());
}

void WindowsResourceCOFFWriter::writeCOFFHeader(uint32_t TimeDateStamp) {
  // Write the COFF header.
  auto *Header = reinterpret_cast<coff_file_header *>(BufferStart);
  Header->Machine = MachineType;
  Header->NumberOfSections = 2;
  Header->TimeDateStamp = TimeDateStamp;
  Header->PointerToSymbolTable = SymbolTableOffset;
  // One symbol for every resource plus 2 for each section and 1 for @feat.00
  Header->NumberOfSymbols = Data.size() + 5;
  Header->SizeOfOptionalHeader = 0;
  // cvtres.exe sets 32BIT_MACHINE even for 64-bit machine types. Match it.
  Header->Characteristics = COFF::IMAGE_FILE_32BIT_MACHINE;
}

void WindowsResourceCOFFWriter::writeFirstSectionHeader() {
  // Write the first section header.
  CurrentOffset += sizeof(coff_file_header);
  auto *SectionOneHeader =
      reinterpret_cast<coff_section *>(BufferStart + CurrentOffset);
  coffnamecpy(SectionOneHeader->Name, ".rsrc$01");
  SectionOneHeader->VirtualSize = 0;
  SectionOneHeader->VirtualAddress = 0;
  SectionOneHeader->SizeOfRawData = SectionOneSize;
  SectionOneHeader->PointerToRawData = SectionOneOffset;
  SectionOneHeader->PointerToRelocations = SectionOneRelocations;
  SectionOneHeader->PointerToLinenumbers = 0;
  SectionOneHeader->NumberOfRelocations = Data.size();
  SectionOneHeader->NumberOfLinenumbers = 0;
  SectionOneHeader->Characteristics += COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  SectionOneHeader->Characteristics += COFF::IMAGE_SCN_MEM_READ;
}

void WindowsResourceCOFFWriter::writeSecondSectionHeader() {
  // Write the second section header.
  CurrentOffset += sizeof(coff_section);
  auto *SectionTwoHeader =
      reinterpret_cast<coff_section *>(BufferStart + CurrentOffset);
  coffnamecpy(SectionTwoHeader->Name, ".rsrc$02");
  SectionTwoHeader->VirtualSize = 0;
  SectionTwoHeader->VirtualAddress = 0;
  SectionTwoHeader->SizeOfRawData = SectionTwoSize;
  SectionTwoHeader->PointerToRawData = SectionTwoOffset;
  SectionTwoHeader->PointerToRelocations = 0;
  SectionTwoHeader->PointerToLinenumbers = 0;
  SectionTwoHeader->NumberOfRelocations = 0;
  SectionTwoHeader->NumberOfLinenumbers = 0;
  SectionTwoHeader->Characteristics = COFF::IMAGE_SCN_CNT_INITIALIZED_DATA;
  SectionTwoHeader->Characteristics += COFF::IMAGE_SCN_MEM_READ;
}

void WindowsResourceCOFFWriter::writeFirstSection() {
  // Write section one.
  CurrentOffset += sizeof(coff_section);

  writeDirectoryTree();
  writeDirectoryStringTable();
  writeFirstSectionRelocations();

  CurrentOffset = alignTo(CurrentOffset, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::writeSecondSection() {
  // Now write the .rsrc$02 section.
  for (auto const &RawDataEntry : Data) {
    llvm::copy(RawDataEntry, BufferStart + CurrentOffset);
    CurrentOffset += alignTo(RawDataEntry.size(), sizeof(uint64_t));
  }

  CurrentOffset = alignTo(CurrentOffset, SECTION_ALIGNMENT);
}

void WindowsResourceCOFFWriter::writeSymbolTable() {
  // Now write the symbol table.
  // First, the feat symbol.
  auto *Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  coffnamecpy(Symbol->Name.ShortName, "@feat.00");
  Symbol->Value = 0x11;
  Symbol->SectionNumber = 0xffff;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 0;
  CurrentOffset += sizeof(coff_symbol16);

  // Now write the .rsrc1 symbol + aux.
  Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  coffnamecpy(Symbol->Name.ShortName, ".rsrc$01");
  Symbol->Value = 0;
  Symbol->SectionNumber = 1;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 1;
  CurrentOffset += sizeof(coff_symbol16);
  auto *Aux = reinterpret_cast<coff_aux_section_definition *>(BufferStart +
                                                              CurrentOffset);
  Aux->Length = SectionOneSize;
  Aux->NumberOfRelocations = Data.size();
  Aux->NumberOfLinenumbers = 0;
  Aux->CheckSum = 0;
  Aux->NumberLowPart = 0;
  Aux->Selection = 0;
  CurrentOffset += sizeof(coff_aux_section_definition);

  // Now write the .rsrc2 symbol + aux.
  Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
  coffnamecpy(Symbol->Name.ShortName, ".rsrc$02");
  Symbol->Value = 0;
  Symbol->SectionNumber = 2;
  Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
  Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
  Symbol->NumberOfAuxSymbols = 1;
  CurrentOffset += sizeof(coff_symbol16);
  Aux = reinterpret_cast<coff_aux_section_definition *>(BufferStart +
                                                        CurrentOffset);
  Aux->Length = SectionTwoSize;
  Aux->NumberOfRelocations = 0;
  Aux->NumberOfLinenumbers = 0;
  Aux->CheckSum = 0;
  Aux->NumberLowPart = 0;
  Aux->Selection = 0;
  CurrentOffset += sizeof(coff_aux_section_definition);

  // Now write a symbol for each relocation.
  for (unsigned i = 0; i < Data.size(); i++) {
    auto RelocationName = formatv("$R{0:X-6}", i & 0xffffff).sstr<COFF::NameSize>();
    Symbol = reinterpret_cast<coff_symbol16 *>(BufferStart + CurrentOffset);
    coffnamecpy(Symbol->Name.ShortName, RelocationName);
    Symbol->Value = DataOffsets[i];
    Symbol->SectionNumber = 2;
    Symbol->Type = COFF::IMAGE_SYM_DTYPE_NULL;
    Symbol->StorageClass = COFF::IMAGE_SYM_CLASS_STATIC;
    Symbol->NumberOfAuxSymbols = 0;
    CurrentOffset += sizeof(coff_symbol16);
  }
}

void WindowsResourceCOFFWriter::writeStringTable() {
  // Just 4 null bytes for the string table.
  auto COFFStringTable = reinterpret_cast<void *>(BufferStart + CurrentOffset);
  memset(COFFStringTable, 0, 4);
}

void WindowsResourceCOFFWriter::writeDirectoryTree() {
  // Traverse parsed resource tree breadth-first and write the corresponding
  // COFF objects.
  std::queue<const WindowsResourceParser::TreeNode *> Queue;
  Queue.push(&Resources);
  uint32_t NextLevelOffset =
      sizeof(coff_resource_dir_table) + (Resources.getStringChildren().size() +
                                         Resources.getIDChildren().size()) *
                                            sizeof(coff_resource_dir_entry);
  std::vector<const WindowsResourceParser::TreeNode *> DataEntriesTreeOrder;
  uint32_t CurrentRelativeOffset = 0;

  while (!Queue.empty()) {
    auto CurrentNode = Queue.front();
    Queue.pop();
    auto *Table = reinterpret_cast<coff_resource_dir_table *>(BufferStart +
                                                              CurrentOffset);
    Table->Characteristics = CurrentNode->getCharacteristics();
    Table->TimeDateStamp = 0;
    Table->MajorVersion = CurrentNode->getMajorVersion();
    Table->MinorVersion = CurrentNode->getMinorVersion();
    auto &IDChildren = CurrentNode->getIDChildren();
    auto &StringChildren = CurrentNode->getStringChildren();
    Table->NumberOfNameEntries = StringChildren.size();
    Table->NumberOfIDEntries = IDChildren.size();
    CurrentOffset += sizeof(coff_resource_dir_table);
    CurrentRelativeOffset += sizeof(coff_resource_dir_table);

    // Write the directory entries immediately following each directory table.
    for (auto const &Child : StringChildren) {
      auto *Entry = reinterpret_cast<coff_resource_dir_entry *>(BufferStart +
                                                                CurrentOffset);
      Entry->Identifier.setNameOffset(
          StringTableOffsets[Child.second->getStringIndex()]);
      if (Child.second->checkIsDataNode()) {
        Entry->Offset.DataEntryOffset = NextLevelOffset;
        NextLevelOffset += sizeof(coff_resource_data_entry);
        DataEntriesTreeOrder.push_back(Child.second.get());
      } else {
        Entry->Offset.SubdirOffset = NextLevelOffset + (1 << 31);
        NextLevelOffset += sizeof(coff_resource_dir_table) +
                           (Child.second->getStringChildren().size() +
                            Child.second->getIDChildren().size()) *
                               sizeof(coff_resource_dir_entry);
        Queue.push(Child.second.get());
      }
      CurrentOffset += sizeof(coff_resource_dir_entry);
      CurrentRelativeOffset += sizeof(coff_resource_dir_entry);
    }
    for (auto const &Child : IDChildren) {
      auto *Entry = reinterpret_cast<coff_resource_dir_entry *>(BufferStart +
                                                                CurrentOffset);
      Entry->Identifier.ID = Child.first;
      if (Child.second->checkIsDataNode()) {
        Entry->Offset.DataEntryOffset = NextLevelOffset;
        NextLevelOffset += sizeof(coff_resource_data_entry);
        DataEntriesTreeOrder.push_back(Child.second.get());
      } else {
        Entry->Offset.SubdirOffset = NextLevelOffset + (1 << 31);
        NextLevelOffset += sizeof(coff_resource_dir_table) +
                           (Child.second->getStringChildren().size() +
                            Child.second->getIDChildren().size()) *
                               sizeof(coff_resource_dir_entry);
        Queue.push(Child.second.get());
      }
      CurrentOffset += sizeof(coff_resource_dir_entry);
      CurrentRelativeOffset += sizeof(coff_resource_dir_entry);
    }
  }

  RelocationAddresses.resize(Data.size());
  // Now write all the resource data entries.
  for (const auto *DataNodes : DataEntriesTreeOrder) {
    auto *Entry = reinterpret_cast<coff_resource_data_entry *>(BufferStart +
                                                               CurrentOffset);
    RelocationAddresses[DataNodes->getDataIndex()] = CurrentRelativeOffset;
    Entry->DataRVA = 0; // Set to zero because it is a relocation.
    Entry->DataSize = Data[DataNodes->getDataIndex()].size();
    Entry->Codepage = 0;
    Entry->Reserved = 0;
    CurrentOffset += sizeof(coff_resource_data_entry);
    CurrentRelativeOffset += sizeof(coff_resource_data_entry);
  }
}

void WindowsResourceCOFFWriter::writeDirectoryStringTable() {
  // Now write the directory string table for .rsrc$01
  uint32_t TotalStringTableSize = 0;
  for (auto &String : StringTable) {
    uint16_t Length = String.size();
    support::endian::write16le(BufferStart + CurrentOffset, Length);
    CurrentOffset += sizeof(uint16_t);
    auto *Start = reinterpret_cast<UTF16 *>(BufferStart + CurrentOffset);
    llvm::copy(String, Start);
    CurrentOffset += Length * sizeof(UTF16);
    TotalStringTableSize += Length * sizeof(UTF16) + sizeof(uint16_t);
  }
  CurrentOffset +=
      alignTo(TotalStringTableSize, sizeof(uint32_t)) - TotalStringTableSize;
}

void WindowsResourceCOFFWriter::writeFirstSectionRelocations() {

  // Now write the relocations for .rsrc$01
  // Five symbols already in table before we start, @feat.00 and 2 for each
  // .rsrc section.
  uint32_t NextSymbolIndex = 5;
  for (unsigned i = 0; i < Data.size(); i++) {
    auto *Reloc =
        reinterpret_cast<coff_relocation *>(BufferStart + CurrentOffset);
    Reloc->VirtualAddress = RelocationAddresses[i];
    Reloc->SymbolTableIndex = NextSymbolIndex++;
    switch (getMachineArchType(MachineType)) {
    case Triple::thumb:
      Reloc->Type = COFF::IMAGE_REL_ARM_ADDR32NB;
      break;
    case Triple::x86_64:
      Reloc->Type = COFF::IMAGE_REL_AMD64_ADDR32NB;
      break;
    case Triple::x86:
      Reloc->Type = COFF::IMAGE_REL_I386_DIR32NB;
      break;
    case Triple::aarch64:
      Reloc->Type = COFF::IMAGE_REL_ARM64_ADDR32NB;
      break;
    default:
      llvm_unreachable("unknown machine type");
    }
    CurrentOffset += sizeof(coff_relocation);
  }
}

Expected<std::unique_ptr<MemoryBuffer>>
writeWindowsResourceCOFF(COFF::MachineTypes MachineType,
                         const WindowsResourceParser &Parser,
                         uint32_t TimeDateStamp) {
  Error E = Error::success();
  WindowsResourceCOFFWriter Writer(MachineType, Parser, E);
  if (E)
    return E;
  return Writer.write(TimeDateStamp);
}

} // namespace object
} // namespace llvm
