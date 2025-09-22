//===- DIALineNumber.cpp - DIA implementation of IPDBLineNumber -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/DIA/DIALineNumber.h"

using namespace llvm;
using namespace llvm::pdb;

DIALineNumber::DIALineNumber(CComPtr<IDiaLineNumber> DiaLineNumber)
    : LineNumber(DiaLineNumber) {}

uint32_t DIALineNumber::getLineNumber() const {
  DWORD Line = 0;
  return (S_OK == LineNumber->get_lineNumber(&Line)) ? Line : 0;
}

uint32_t DIALineNumber::getLineNumberEnd() const {
  DWORD LineEnd = 0;
  return (S_OK == LineNumber->get_lineNumberEnd(&LineEnd)) ? LineEnd : 0;
}

uint32_t DIALineNumber::getColumnNumber() const {
  DWORD Column = 0;
  return (S_OK == LineNumber->get_columnNumber(&Column)) ? Column : 0;
}

uint32_t DIALineNumber::getColumnNumberEnd() const {
  DWORD ColumnEnd = 0;
  return (S_OK == LineNumber->get_columnNumberEnd(&ColumnEnd)) ? ColumnEnd : 0;
}

uint32_t DIALineNumber::getAddressSection() const {
  DWORD Section = 0;
  return (S_OK == LineNumber->get_addressSection(&Section)) ? Section : 0;
}

uint32_t DIALineNumber::getAddressOffset() const {
  DWORD Offset = 0;
  return (S_OK == LineNumber->get_addressOffset(&Offset)) ? Offset : 0;
}

uint32_t DIALineNumber::getRelativeVirtualAddress() const {
  DWORD RVA = 0;
  return (S_OK == LineNumber->get_relativeVirtualAddress(&RVA)) ? RVA : 0;
}

uint64_t DIALineNumber::getVirtualAddress() const {
  ULONGLONG Addr = 0;
  return (S_OK == LineNumber->get_virtualAddress(&Addr)) ? Addr : 0;
}

uint32_t DIALineNumber::getLength() const {
  DWORD Length = 0;
  return (S_OK == LineNumber->get_length(&Length)) ? Length : 0;
}

uint32_t DIALineNumber::getSourceFileId() const {
  DWORD Id = 0;
  return (S_OK == LineNumber->get_sourceFileId(&Id)) ? Id : 0;
}

uint32_t DIALineNumber::getCompilandId() const {
  DWORD Id = 0;
  return (S_OK == LineNumber->get_compilandId(&Id)) ? Id : 0;
}

bool DIALineNumber::isStatement() const {
  BOOL Statement = 0;
  return (S_OK == LineNumber->get_statement(&Statement)) ? Statement : false;
}
