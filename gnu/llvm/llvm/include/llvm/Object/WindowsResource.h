//===-- WindowsResource.h ---------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file declares the .res file class.  .res files are intermediate
// products of the typical resource-compilation process on Windows.  This
// process is as follows:
//
// .rc file(s) ---(rc.exe)---> .res file(s) ---(cvtres.exe)---> COFF file
//
// .rc files are human-readable scripts that list all resources a program uses.
//
// They are compiled into .res files, which are a list of the resources in
// binary form.
//
// Finally the data stored in the .res is compiled into a COFF file, where it
// is organized in a directory tree structure for optimized access by the
// program during runtime.
//
// Ref: msdn.microsoft.com/en-us/library/windows/desktop/ms648007(v=vs.85).aspx
//
//===---------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_WINDOWSRESOURCE_H
#define LLVM_OBJECT_WINDOWSRESOURCE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"

#include <map>

namespace llvm {

class raw_ostream;
class ScopedPrinter;

namespace object {

class WindowsResource;
class ResourceSectionRef;
struct coff_resource_dir_table;

const size_t WIN_RES_MAGIC_SIZE = 16;
const size_t WIN_RES_NULL_ENTRY_SIZE = 16;
const uint32_t WIN_RES_HEADER_ALIGNMENT = 4;
const uint32_t WIN_RES_DATA_ALIGNMENT = 4;
const uint16_t WIN_RES_PURE_MOVEABLE = 0x0030;

struct WinResHeaderPrefix {
  support::ulittle32_t DataSize;
  support::ulittle32_t HeaderSize;
};

// Type and Name may each either be an integer ID or a string.  This struct is
// only used in the case where they are both IDs.
struct WinResIDs {
  uint16_t TypeFlag;
  support::ulittle16_t TypeID;
  uint16_t NameFlag;
  support::ulittle16_t NameID;

  void setType(uint16_t ID) {
    TypeFlag = 0xffff;
    TypeID = ID;
  }

  void setName(uint16_t ID) {
    NameFlag = 0xffff;
    NameID = ID;
  }
};

struct WinResHeaderSuffix {
  support::ulittle32_t DataVersion;
  support::ulittle16_t MemoryFlags;
  support::ulittle16_t Language;
  support::ulittle32_t Version;
  support::ulittle32_t Characteristics;
};

class EmptyResError : public GenericBinaryError {
public:
  EmptyResError(Twine Msg, object_error ECOverride)
      : GenericBinaryError(Msg, ECOverride) {}
};

class ResourceEntryRef {
public:
  Error moveNext(bool &End);
  bool checkTypeString() const { return IsStringType; }
  ArrayRef<UTF16> getTypeString() const { return Type; }
  uint16_t getTypeID() const { return TypeID; }
  bool checkNameString() const { return IsStringName; }
  ArrayRef<UTF16> getNameString() const { return Name; }
  uint16_t getNameID() const { return NameID; }
  uint16_t getDataVersion() const { return Suffix->DataVersion; }
  uint16_t getLanguage() const { return Suffix->Language; }
  uint16_t getMemoryFlags() const { return Suffix->MemoryFlags; }
  uint16_t getMajorVersion() const { return Suffix->Version >> 16; }
  uint16_t getMinorVersion() const { return Suffix->Version; }
  uint32_t getCharacteristics() const { return Suffix->Characteristics; }
  ArrayRef<uint8_t> getData() const { return Data; }

private:
  friend class WindowsResource;

  ResourceEntryRef(BinaryStreamRef Ref, const WindowsResource *Owner);
  Error loadNext();

  static Expected<ResourceEntryRef> create(BinaryStreamRef Ref,
                                           const WindowsResource *Owner);

  BinaryStreamReader Reader;
  const WindowsResource *Owner;
  bool IsStringType;
  ArrayRef<UTF16> Type;
  uint16_t TypeID;
  bool IsStringName;
  ArrayRef<UTF16> Name;
  uint16_t NameID;
  const WinResHeaderSuffix *Suffix = nullptr;
  ArrayRef<uint8_t> Data;
};

class WindowsResource : public Binary {
public:
  Expected<ResourceEntryRef> getHeadEntry();

  static bool classof(const Binary *V) { return V->isWinRes(); }

  static Expected<std::unique_ptr<WindowsResource>>
  createWindowsResource(MemoryBufferRef Source);

private:
  friend class ResourceEntryRef;

  WindowsResource(MemoryBufferRef Source);

  BinaryByteStream BBS;
};

class WindowsResourceParser {
public:
  class TreeNode;
  WindowsResourceParser(bool MinGW = false);
  Error parse(WindowsResource *WR, std::vector<std::string> &Duplicates);
  Error parse(ResourceSectionRef &RSR, StringRef Filename,
              std::vector<std::string> &Duplicates);
  void cleanUpManifests(std::vector<std::string> &Duplicates);
  void printTree(raw_ostream &OS) const;
  const TreeNode &getTree() const { return Root; }
  ArrayRef<std::vector<uint8_t>> getData() const { return Data; }
  ArrayRef<std::vector<UTF16>> getStringTable() const { return StringTable; }

  class TreeNode {
  public:
    template <typename T>
    using Children = std::map<T, std::unique_ptr<TreeNode>>;

    void print(ScopedPrinter &Writer, StringRef Name) const;
    uint32_t getTreeSize() const;
    uint32_t getStringIndex() const { return StringIndex; }
    uint32_t getDataIndex() const { return DataIndex; }
    uint16_t getMajorVersion() const { return MajorVersion; }
    uint16_t getMinorVersion() const { return MinorVersion; }
    uint32_t getCharacteristics() const { return Characteristics; }
    bool checkIsDataNode() const { return IsDataNode; }
    const Children<uint32_t> &getIDChildren() const { return IDChildren; }
    const Children<std::string> &getStringChildren() const {
      return StringChildren;
    }

  private:
    friend class WindowsResourceParser;

    // Index is the StringTable vector index for this node's name.
    static std::unique_ptr<TreeNode> createStringNode(uint32_t Index);
    static std::unique_ptr<TreeNode> createIDNode();
    // DataIndex is the Data vector index that the data node points at.
    static std::unique_ptr<TreeNode> createDataNode(uint16_t MajorVersion,
                                                    uint16_t MinorVersion,
                                                    uint32_t Characteristics,
                                                    uint32_t Origin,
                                                    uint32_t DataIndex);

    explicit TreeNode(uint32_t StringIndex);
    TreeNode(uint16_t MajorVersion, uint16_t MinorVersion,
             uint32_t Characteristics, uint32_t Origin, uint32_t DataIndex);

    bool addEntry(const ResourceEntryRef &Entry, uint32_t Origin,
                  std::vector<std::vector<uint8_t>> &Data,
                  std::vector<std::vector<UTF16>> &StringTable,
                  TreeNode *&Result);
    TreeNode &addTypeNode(const ResourceEntryRef &Entry,
                          std::vector<std::vector<UTF16>> &StringTable);
    TreeNode &addNameNode(const ResourceEntryRef &Entry,
                          std::vector<std::vector<UTF16>> &StringTable);
    bool addLanguageNode(const ResourceEntryRef &Entry, uint32_t Origin,
                         std::vector<std::vector<uint8_t>> &Data,
                         TreeNode *&Result);
    bool addDataChild(uint32_t ID, uint16_t MajorVersion, uint16_t MinorVersion,
                      uint32_t Characteristics, uint32_t Origin,
                      uint32_t DataIndex, TreeNode *&Result);
    TreeNode &addIDChild(uint32_t ID);
    TreeNode &addNameChild(ArrayRef<UTF16> NameRef,
                           std::vector<std::vector<UTF16>> &StringTable);
    void shiftDataIndexDown(uint32_t Index);

    bool IsDataNode = false;
    uint32_t StringIndex;
    uint32_t DataIndex;
    Children<uint32_t> IDChildren;
    Children<std::string> StringChildren;
    uint16_t MajorVersion = 0;
    uint16_t MinorVersion = 0;
    uint32_t Characteristics = 0;

    // The .res file that defined this TreeNode, for diagnostics.
    // Index into InputFilenames.
    uint32_t Origin;
  };

  struct StringOrID {
    bool IsString;
    ArrayRef<UTF16> String;
    uint32_t ID = ~0u;

    StringOrID(uint32_t ID) : IsString(false), ID(ID) {}
    StringOrID(ArrayRef<UTF16> String) : IsString(true), String(String) {}
  };

private:
  Error addChildren(TreeNode &Node, ResourceSectionRef &RSR,
                    const coff_resource_dir_table &Table, uint32_t Origin,
                    std::vector<StringOrID> &Context,
                    std::vector<std::string> &Duplicates);
  bool shouldIgnoreDuplicate(const ResourceEntryRef &Entry) const;
  bool shouldIgnoreDuplicate(const std::vector<StringOrID> &Context) const;

  TreeNode Root;
  std::vector<std::vector<uint8_t>> Data;
  std::vector<std::vector<UTF16>> StringTable;

  std::vector<std::string> InputFilenames;

  bool MinGW;
};

Expected<std::unique_ptr<MemoryBuffer>>
writeWindowsResourceCOFF(llvm::COFF::MachineTypes MachineType,
                         const WindowsResourceParser &Parser,
                         uint32_t TimeDateStamp);

void printResourceTypeName(uint16_t TypeID, raw_ostream &OS);
} // namespace object
} // namespace llvm

#endif
