//===- DbiModuleList.h - PDB module information list ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace llvm {
namespace pdb {

class DbiModuleList;
struct FileInfoSubstreamHeader;

class DbiModuleSourceFilesIterator
    : public iterator_facade_base<DbiModuleSourceFilesIterator,
                                  std::random_access_iterator_tag, StringRef> {
  using BaseType = typename DbiModuleSourceFilesIterator::iterator_facade_base;

public:
  DbiModuleSourceFilesIterator(const DbiModuleList &Modules, uint32_t Modi,
                               uint16_t Filei);
  DbiModuleSourceFilesIterator() = default;
  DbiModuleSourceFilesIterator(const DbiModuleSourceFilesIterator &R) = default;
  DbiModuleSourceFilesIterator &
  operator=(const DbiModuleSourceFilesIterator &R) = default;

  bool operator==(const DbiModuleSourceFilesIterator &R) const;

  const StringRef &operator*() const { return ThisValue; }
  StringRef &operator*() { return ThisValue; }

  bool operator<(const DbiModuleSourceFilesIterator &RHS) const;
  std::ptrdiff_t operator-(const DbiModuleSourceFilesIterator &R) const;
  DbiModuleSourceFilesIterator &operator+=(std::ptrdiff_t N);
  DbiModuleSourceFilesIterator &operator-=(std::ptrdiff_t N);

private:
  void setValue();

  bool isEnd() const;
  bool isCompatible(const DbiModuleSourceFilesIterator &R) const;
  bool isUniversalEnd() const;

  StringRef ThisValue;
  const DbiModuleList *Modules{nullptr};
  uint32_t Modi{0};
  uint16_t Filei{0};
};

class DbiModuleList {
  friend DbiModuleSourceFilesIterator;

public:
  Error initialize(BinaryStreamRef ModInfo, BinaryStreamRef FileInfo);

  Expected<StringRef> getFileName(uint32_t Index) const;
  uint32_t getModuleCount() const;
  uint32_t getSourceFileCount() const;
  uint16_t getSourceFileCount(uint32_t Modi) const;

  iterator_range<DbiModuleSourceFilesIterator>
  source_files(uint32_t Modi) const;

  DbiModuleDescriptor getModuleDescriptor(uint32_t Modi) const;

private:
  Error initializeModInfo(BinaryStreamRef ModInfo);
  Error initializeFileInfo(BinaryStreamRef FileInfo);

  VarStreamArray<DbiModuleDescriptor> Descriptors;

  FixedStreamArray<support::little32_t> FileNameOffsets;
  FixedStreamArray<support::ulittle16_t> ModFileCountArray;

  // For each module, there are multiple filenames, which can be obtained by
  // knowing the index of the file.  Given the index of the file, one can use
  // that as an offset into the FileNameOffsets array, which contains the
  // absolute offset of the file name in NamesBuffer.  Thus, for each module
  // we store the first index in the FileNameOffsets array for this module.
  // The number of files for the corresponding module is stored in
  // ModFileCountArray.
  std::vector<uint32_t> ModuleInitialFileIndex;

  // In order to provide random access into the Descriptors array, we iterate it
  // once up front to find the offsets of the individual items and store them in
  // this array.
  std::vector<uint32_t> ModuleDescriptorOffsets;

  const FileInfoSubstreamHeader *FileInfoHeader = nullptr;

  BinaryStreamRef ModInfoSubstream;
  BinaryStreamRef FileInfoSubstream;
  BinaryStreamRef NamesBuffer;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H
