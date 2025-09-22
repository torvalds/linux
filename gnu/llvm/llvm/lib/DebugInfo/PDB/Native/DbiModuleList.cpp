//===- DbiModuleList.cpp - PDB module information list --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/DbiModuleList.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/Error.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>

using namespace llvm;
using namespace llvm::pdb;

DbiModuleSourceFilesIterator::DbiModuleSourceFilesIterator(
    const DbiModuleList &Modules, uint32_t Modi, uint16_t Filei)
    : Modules(&Modules), Modi(Modi), Filei(Filei) {
  setValue();
}

bool DbiModuleSourceFilesIterator::
operator==(const DbiModuleSourceFilesIterator &R) const {
  // incompatible iterators are never equal
  if (!isCompatible(R))
    return false;

  // If they're compatible, and they're both ends, then they're equal.
  if (isEnd() && R.isEnd())
    return true;

  // If one is an end and the other is not, they're not equal.
  if (isEnd() != R.isEnd())
    return false;

  // Now we know:
  // - They're compatible
  // - They're not *both* end iterators
  // - Their endness is the same.
  // Thus, they're compatible iterators pointing to a valid file on the same
  // module.  All we need to check are the file indices.
  assert(Modules == R.Modules);
  assert(Modi == R.Modi);
  assert(!isEnd());
  assert(!R.isEnd());

  return (Filei == R.Filei);
}

bool DbiModuleSourceFilesIterator::
operator<(const DbiModuleSourceFilesIterator &R) const {
  assert(isCompatible(R));

  // It's not sufficient to compare the file indices, because default
  // constructed iterators could be equal to iterators with valid indices.  To
  // account for this, early-out if they're equal.
  if (*this == R)
    return false;

  return Filei < R.Filei;
}

std::ptrdiff_t DbiModuleSourceFilesIterator::
operator-(const DbiModuleSourceFilesIterator &R) const {
  assert(isCompatible(R));
  assert(!(*this < R));

  // If they're both end iterators, the distance is 0.
  if (isEnd() && R.isEnd())
    return 0;

  assert(!R.isEnd());

  // At this point, R cannot be end, but *this can, which means that *this
  // might be a universal end iterator with none of its fields set.  So in that
  // case have to rely on R as the authority to figure out how many files there
  // are to compute the distance.
  uint32_t Thisi = Filei;
  if (isEnd()) {
    uint32_t RealModi = R.Modi;
    Thisi = R.Modules->getSourceFileCount(RealModi);
  }

  assert(Thisi >= R.Filei);
  return Thisi - R.Filei;
}

DbiModuleSourceFilesIterator &DbiModuleSourceFilesIterator::
operator+=(std::ptrdiff_t N) {
  assert(!isEnd());

  Filei += N;
  assert(Filei <= Modules->getSourceFileCount(Modi));
  setValue();
  return *this;
}

DbiModuleSourceFilesIterator &DbiModuleSourceFilesIterator::
operator-=(std::ptrdiff_t N) {
  // Note that we can subtract from an end iterator, but not a universal end
  // iterator.
  assert(!isUniversalEnd());

  assert(N <= Filei);

  Filei -= N;
  return *this;
}

void DbiModuleSourceFilesIterator::setValue() {
  if (isEnd()) {
    ThisValue = "";
    return;
  }

  uint32_t Off = Modules->ModuleInitialFileIndex[Modi] + Filei;
  auto ExpectedValue = Modules->getFileName(Off);
  if (!ExpectedValue) {
    consumeError(ExpectedValue.takeError());
    Filei = Modules->getSourceFileCount(Modi);
  } else
    ThisValue = *ExpectedValue;
}

bool DbiModuleSourceFilesIterator::isEnd() const {
  if (isUniversalEnd())
    return true;

  assert(Modules);
  assert(Modi <= Modules->getModuleCount());
  assert(Filei <= Modules->getSourceFileCount(Modi));

  if (Modi == Modules->getModuleCount())
    return true;
  if (Filei == Modules->getSourceFileCount(Modi))
    return true;
  return false;
}

bool DbiModuleSourceFilesIterator::isUniversalEnd() const { return !Modules; }

bool DbiModuleSourceFilesIterator::isCompatible(
    const DbiModuleSourceFilesIterator &R) const {
  // Universal iterators are compatible with any other iterator.
  if (isUniversalEnd() || R.isUniversalEnd())
    return true;

  // At this point, neither iterator is a universal end iterator, although one
  // or both might be non-universal end iterators.  Regardless, the module index
  // is valid, so they are compatible if and only if they refer to the same
  // module.
  return Modi == R.Modi;
}

Error DbiModuleList::initialize(BinaryStreamRef ModInfo,
                                BinaryStreamRef FileInfo) {
  if (auto EC = initializeModInfo(ModInfo))
    return EC;
  if (auto EC = initializeFileInfo(FileInfo))
    return EC;

  return Error::success();
}

Error DbiModuleList::initializeModInfo(BinaryStreamRef ModInfo) {
  ModInfoSubstream = ModInfo;

  if (ModInfo.getLength() == 0)
    return Error::success();

  BinaryStreamReader Reader(ModInfo);

  if (auto EC = Reader.readArray(Descriptors, ModInfo.getLength()))
    return EC;

  return Error::success();
}

Error DbiModuleList::initializeFileInfo(BinaryStreamRef FileInfo) {
  FileInfoSubstream = FileInfo;

  if (FileInfo.getLength() == 0)
    return Error::success();

  BinaryStreamReader FISR(FileInfo);
  if (auto EC = FISR.readObject(FileInfoHeader))
    return EC;

  // First is an array of `NumModules` module indices.  This does not seem to be
  // used for anything meaningful, so we ignore it.
  FixedStreamArray<support::ulittle16_t> ModuleIndices;
  if (auto EC = FISR.readArray(ModuleIndices, FileInfoHeader->NumModules))
    return EC;
  if (auto EC = FISR.readArray(ModFileCountArray, FileInfoHeader->NumModules))
    return EC;

  // Compute the real number of source files.  We can't trust the value in
  // `FileInfoHeader->NumSourceFiles` because it is a unit16, and the sum of all
  // source file counts might be larger than a unit16.  So we compute the real
  // count by summing up the individual counts.
  uint32_t NumSourceFiles = 0;
  for (auto Count : ModFileCountArray)
    NumSourceFiles += Count;

  // In the reference implementation, this array is where the pointer documented
  // at the definition of ModuleInfoHeader::FileNameOffs points to.  Note that
  // although the field in ModuleInfoHeader is ignored this array is not, as it
  // is the authority on where each filename begins in the names buffer.
  if (auto EC = FISR.readArray(FileNameOffsets, NumSourceFiles))
    return EC;

  if (auto EC = FISR.readStreamRef(NamesBuffer))
    return EC;

  auto DescriptorIter = Descriptors.begin();
  uint32_t NextFileIndex = 0;
  ModuleInitialFileIndex.resize(FileInfoHeader->NumModules);
  ModuleDescriptorOffsets.resize(FileInfoHeader->NumModules);
  for (size_t I = 0; I < FileInfoHeader->NumModules; ++I) {
    assert(DescriptorIter != Descriptors.end());
    ModuleInitialFileIndex[I] = NextFileIndex;
    ModuleDescriptorOffsets[I] = DescriptorIter.offset();

    NextFileIndex += ModFileCountArray[I];
    ++DescriptorIter;
  }

  assert(DescriptorIter == Descriptors.end());
  assert(NextFileIndex == NumSourceFiles);

  return Error::success();
}

uint32_t DbiModuleList::getModuleCount() const {
  // Workaround to avoid the crash until upstream issue is fixed:
  // https://github.com/llvm/llvm-project/issues/55214
  return FileInfoHeader ? FileInfoHeader->NumModules : 0;
}

uint32_t DbiModuleList::getSourceFileCount() const {
  return FileNameOffsets.size();
}

uint16_t DbiModuleList::getSourceFileCount(uint32_t Modi) const {
  return ModFileCountArray[Modi];
}

DbiModuleDescriptor DbiModuleList::getModuleDescriptor(uint32_t Modi) const {
  assert(Modi < getModuleCount());
  uint32_t Offset = ModuleDescriptorOffsets[Modi];
  auto Iter = Descriptors.at(Offset);
  assert(Iter != Descriptors.end());
  return *Iter;
}

iterator_range<DbiModuleSourceFilesIterator>
DbiModuleList::source_files(uint32_t Modi) const {
  return make_range<DbiModuleSourceFilesIterator>(
      DbiModuleSourceFilesIterator(*this, Modi, 0),
      DbiModuleSourceFilesIterator());
}

Expected<StringRef> DbiModuleList::getFileName(uint32_t Index) const {
  BinaryStreamReader Names(NamesBuffer);
  if (Index >= getSourceFileCount())
    return make_error<RawError>(raw_error_code::index_out_of_bounds);

  uint32_t FileOffset = FileNameOffsets[Index];
  Names.setOffset(FileOffset);
  StringRef Name;
  if (auto EC = Names.readCString(Name))
    return std::move(EC);
  return Name;
}
