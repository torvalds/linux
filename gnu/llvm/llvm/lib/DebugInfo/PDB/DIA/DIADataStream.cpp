//===- DIADataStream.cpp - DIA implementation of IPDBDataStream -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIADataStream.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm;
using namespace llvm::pdb;

DIADataStream::DIADataStream(CComPtr<IDiaEnumDebugStreamData> DiaStreamData)
    : StreamData(DiaStreamData) {}

uint32_t DIADataStream::getRecordCount() const {
  LONG Count = 0;
  return (S_OK == StreamData->get_Count(&Count)) ? Count : 0;
}

std::string DIADataStream::getName() const {
  return invokeBstrMethod(*StreamData, &IDiaEnumDebugStreamData::get_name);
}

std::optional<DIADataStream::RecordType>
DIADataStream::getItemAtIndex(uint32_t Index) const {
  RecordType Record;
  DWORD RecordSize = 0;
  StreamData->Item(Index, 0, &RecordSize, nullptr);
  if (RecordSize == 0)
    return std::nullopt;

  Record.resize(RecordSize);
  if (S_OK != StreamData->Item(Index, RecordSize, &RecordSize, &Record[0]))
    return std::nullopt;
  return Record;
}

bool DIADataStream::getNext(RecordType &Record) {
  Record.clear();
  DWORD RecordSize = 0;
  ULONG CountFetched = 0;
  StreamData->Next(1, 0, &RecordSize, nullptr, &CountFetched);
  if (RecordSize == 0)
    return false;

  Record.resize(RecordSize);
  if (S_OK ==
      StreamData->Next(1, RecordSize, &RecordSize, &Record[0], &CountFetched))
    return false;
  return true;
}

void DIADataStream::reset() { StreamData->Reset(); }
