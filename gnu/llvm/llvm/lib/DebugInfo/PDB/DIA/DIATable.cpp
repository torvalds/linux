//===- DIATable.cpp - DIA implementation of IPDBTable -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIATable.h"
#include "llvm/DebugInfo/PDB/DIA/DIAUtils.h"

using namespace llvm;
using namespace llvm::pdb;

DIATable::DIATable(CComPtr<IDiaTable> DiaTable) : Table(DiaTable) {}

uint32_t DIATable::getItemCount() const {
  LONG Count = 0;
  return (S_OK == Table->get_Count(&Count)) ? Count : 0;
}

std::string DIATable::getName() const {
  return invokeBstrMethod(*Table, &IDiaTable::get_name);
}

PDB_TableType DIATable::getTableType() const {
  CComBSTR Name16;
  if (S_OK != Table->get_name(&Name16))
    return PDB_TableType::TableInvalid;

  if (Name16 == DiaTable_Symbols)
    return PDB_TableType::Symbols;
  if (Name16 == DiaTable_SrcFiles)
    return PDB_TableType::SourceFiles;
  if (Name16 == DiaTable_Sections)
    return PDB_TableType::SectionContribs;
  if (Name16 == DiaTable_LineNums)
    return PDB_TableType::LineNumbers;
  if (Name16 == DiaTable_SegMap)
    return PDB_TableType::Segments;
  if (Name16 == DiaTable_InjSrc)
    return PDB_TableType::InjectedSources;
  if (Name16 == DiaTable_FrameData)
    return PDB_TableType::FrameData;
  if (Name16 == DiaTable_InputAssemblyFiles)
    return PDB_TableType::InputAssemblyFiles;
  if (Name16 == DiaTable_Dbg)
    return PDB_TableType::Dbg;
  return PDB_TableType::TableInvalid;
}
