//===- CXSourceLocation.cpp - CXSourceLocations APIs ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines routines for manipulating CXSourceLocations.
//
//===----------------------------------------------------------------------===//

#include "CXSourceLocation.h"
#include "CIndexer.h"
#include "CLog.h"
#include "CXFile.h"
#include "CXLoadedDiagnostic.h"
#include "CXString.h"
#include "CXTranslationUnit.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/ASTUnit.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Format.h"

using namespace clang;
using namespace clang::cxindex;

//===----------------------------------------------------------------------===//
// Internal predicates on CXSourceLocations.
//===----------------------------------------------------------------------===//

static bool isASTUnitSourceLocation(const CXSourceLocation &L) {
  // If the lowest bit is clear then the first ptr_data entry is a SourceManager
  // pointer, or the CXSourceLocation is a null location.
  return ((uintptr_t)L.ptr_data[0] & 0x1) == 0;
}

//===----------------------------------------------------------------------===//
// Basic construction and comparison of CXSourceLocations and CXSourceRanges.
//===----------------------------------------------------------------------===//

CXSourceLocation clang_getNullLocation() {
  CXSourceLocation Result = { { nullptr, nullptr }, 0 };
  return Result;
}

unsigned clang_equalLocations(CXSourceLocation loc1, CXSourceLocation loc2) {
  return (loc1.ptr_data[0] == loc2.ptr_data[0] &&
          loc1.ptr_data[1] == loc2.ptr_data[1] &&
          loc1.int_data == loc2.int_data);
}

CXSourceRange clang_getNullRange() {
  CXSourceRange Result = { { nullptr, nullptr }, 0, 0 };
  return Result;
}

CXSourceRange clang_getRange(CXSourceLocation begin, CXSourceLocation end) {
  if (!isASTUnitSourceLocation(begin)) {
    if (isASTUnitSourceLocation(end))
      return clang_getNullRange();
    CXSourceRange Result = { { begin.ptr_data[0], end.ptr_data[0] }, 0, 0 };
    return Result;
  }
  
  if (begin.ptr_data[0] != end.ptr_data[0] ||
      begin.ptr_data[1] != end.ptr_data[1])
    return clang_getNullRange();
  
  CXSourceRange Result = { { begin.ptr_data[0], begin.ptr_data[1] },
                           begin.int_data, end.int_data };

  return Result;
}

unsigned clang_equalRanges(CXSourceRange range1, CXSourceRange range2) {
  return range1.ptr_data[0] == range2.ptr_data[0]
    && range1.ptr_data[1] == range2.ptr_data[1]
    && range1.begin_int_data == range2.begin_int_data
    && range1.end_int_data == range2.end_int_data;
}

int clang_Range_isNull(CXSourceRange range) {
  return clang_equalRanges(range, clang_getNullRange());
}
  
  
CXSourceLocation clang_getRangeStart(CXSourceRange range) {
  // Special decoding for CXSourceLocations for CXLoadedDiagnostics.
  if ((uintptr_t)range.ptr_data[0] & 0x1) {
    CXSourceLocation Result = { { range.ptr_data[0], nullptr }, 0 };
    return Result;    
  }
  
  CXSourceLocation Result = { { range.ptr_data[0], range.ptr_data[1] },
    range.begin_int_data };
  return Result;
}

CXSourceLocation clang_getRangeEnd(CXSourceRange range) {
  // Special decoding for CXSourceLocations for CXLoadedDiagnostics.
  if ((uintptr_t)range.ptr_data[0] & 0x1) {
    CXSourceLocation Result = { { range.ptr_data[1], nullptr }, 0 };
    return Result;    
  }

  CXSourceLocation Result = { { range.ptr_data[0], range.ptr_data[1] },
    range.end_int_data };
  return Result;
}

//===----------------------------------------------------------------------===//
//  Getting CXSourceLocations and CXSourceRanges from a translation unit.
//===----------------------------------------------------------------------===//

CXSourceLocation clang_getLocation(CXTranslationUnit TU,
                                   CXFile file,
                                   unsigned line,
                                   unsigned column) {
  if (cxtu::isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullLocation();
  }
  if (!file)
    return clang_getNullLocation();
  if (line == 0 || column == 0)
    return clang_getNullLocation();
  
  LogRef Log = Logger::make(__func__);
  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);
  ASTUnit::ConcurrencyCheck Check(*CXXUnit);
  FileEntryRef File = *cxfile::getFileEntryRef(file);
  SourceLocation SLoc = CXXUnit->getLocation(File, line, column);
  if (SLoc.isInvalid()) {
    if (Log)
      *Log << llvm::format("(\"%s\", %d, %d) = invalid",
                           File.getName().str().c_str(), line, column);
    return clang_getNullLocation();
  }
  
  CXSourceLocation CXLoc =
      cxloc::translateSourceLocation(CXXUnit->getASTContext(), SLoc);
  if (Log)
    *Log << llvm::format("(\"%s\", %d, %d) = ", File.getName().str().c_str(),
                         line, column)
         << CXLoc;

  return CXLoc;
}
  
CXSourceLocation clang_getLocationForOffset(CXTranslationUnit TU,
                                            CXFile file,
                                            unsigned offset) {
  if (cxtu::isNotUsableTU(TU)) {
    LOG_BAD_TU(TU);
    return clang_getNullLocation();
  }
  if (!file)
    return clang_getNullLocation();

  ASTUnit *CXXUnit = cxtu::getASTUnit(TU);

  SourceLocation SLoc 
    = CXXUnit->getLocation(*cxfile::getFileEntryRef(file), offset);

  if (SLoc.isInvalid())
    return clang_getNullLocation();
  
  return cxloc::translateSourceLocation(CXXUnit->getASTContext(), SLoc);
}

//===----------------------------------------------------------------------===//
// Routines for expanding and manipulating CXSourceLocations, regardless
// of their origin.
//===----------------------------------------------------------------------===//

static void createNullLocation(CXFile *file, unsigned *line,
                               unsigned *column, unsigned *offset) {
  if (file)
    *file = nullptr;
  if (line)
    *line = 0;
  if (column)
    *column = 0;
  if (offset)
    *offset = 0;
}

static void createNullLocation(CXString *filename, unsigned *line,
                               unsigned *column, unsigned *offset = nullptr) {
  if (filename)
    *filename = cxstring::createEmpty();
  if (line)
    *line = 0;
  if (column)
    *column = 0;
  if (offset)
    *offset = 0;
}

int clang_Location_isInSystemHeader(CXSourceLocation location) {
  const SourceLocation Loc =
    SourceLocation::getFromRawEncoding(location.int_data);
  if (Loc.isInvalid())
    return 0;

  const SourceManager &SM =
    *static_cast<const SourceManager*>(location.ptr_data[0]);
  return SM.isInSystemHeader(Loc);
}

int clang_Location_isFromMainFile(CXSourceLocation location) {
  const SourceLocation Loc =
    SourceLocation::getFromRawEncoding(location.int_data);
  if (Loc.isInvalid())
    return 0;

  const SourceManager &SM =
    *static_cast<const SourceManager*>(location.ptr_data[0]);
  return SM.isWrittenInMainFile(Loc);
}

void clang_getExpansionLocation(CXSourceLocation location,
                                CXFile *file,
                                unsigned *line,
                                unsigned *column,
                                unsigned *offset) {
  if (!isASTUnitSourceLocation(location)) {
    CXLoadedDiagnostic::decodeLocation(location, file, line, column, offset);
    return;
  }

  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);

  if (!location.ptr_data[0] || Loc.isInvalid()) {
    createNullLocation(file, line, column, offset);
    return;
  }

  const SourceManager &SM =
  *static_cast<const SourceManager*>(location.ptr_data[0]);
  SourceLocation ExpansionLoc = SM.getExpansionLoc(Loc);
  
  // Check that the FileID is invalid on the expansion location.
  // This can manifest in invalid code.
  FileID fileID = SM.getFileID(ExpansionLoc);
  bool Invalid = false;
  const SrcMgr::SLocEntry &sloc = SM.getSLocEntry(fileID, &Invalid);
  if (Invalid || !sloc.isFile()) {
    createNullLocation(file, line, column, offset);
    return;
  }
  
  if (file)
    *file = cxfile::makeCXFile(SM.getFileEntryRefForID(fileID));
  if (line)
    *line = SM.getExpansionLineNumber(ExpansionLoc);
  if (column)
    *column = SM.getExpansionColumnNumber(ExpansionLoc);
  if (offset)
    *offset = SM.getDecomposedLoc(ExpansionLoc).second;
}

void clang_getPresumedLocation(CXSourceLocation location,
                               CXString *filename,
                               unsigned *line,
                               unsigned *column) {
  if (!isASTUnitSourceLocation(location)) {
    // Other SourceLocation implementations do not support presumed locations
    // at this time.
    createNullLocation(filename, line, column);
    return;
  }

  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);

  if (!location.ptr_data[0] || Loc.isInvalid()) {
    createNullLocation(filename, line, column);
    return;
  }

  const SourceManager &SM =
      *static_cast<const SourceManager *>(location.ptr_data[0]);
  PresumedLoc PreLoc = SM.getPresumedLoc(Loc);
  if (PreLoc.isInvalid()) {
    createNullLocation(filename, line, column);
    return;
  }

  if (filename) *filename = cxstring::createRef(PreLoc.getFilename());
  if (line) *line = PreLoc.getLine();
  if (column) *column = PreLoc.getColumn();
}

void clang_getInstantiationLocation(CXSourceLocation location,
                                    CXFile *file,
                                    unsigned *line,
                                    unsigned *column,
                                    unsigned *offset) {
  // Redirect to new API.
  clang_getExpansionLocation(location, file, line, column, offset);
}

void clang_getSpellingLocation(CXSourceLocation location,
                               CXFile *file,
                               unsigned *line,
                               unsigned *column,
                               unsigned *offset) {
  if (!isASTUnitSourceLocation(location)) {
    CXLoadedDiagnostic::decodeLocation(location, file, line,
                                           column, offset);
    return;
  }
  
  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);
  
  if (!location.ptr_data[0] || Loc.isInvalid())
    return createNullLocation(file, line, column, offset);
  
  const SourceManager &SM =
  *static_cast<const SourceManager*>(location.ptr_data[0]);
  SourceLocation SpellLoc = SM.getSpellingLoc(Loc);
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(SpellLoc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;
  
  if (FID.isInvalid())
    return createNullLocation(file, line, column, offset);
  
  if (file)
    *file = cxfile::makeCXFile(SM.getFileEntryRefForID(FID));
  if (line)
    *line = SM.getLineNumber(FID, FileOffset);
  if (column)
    *column = SM.getColumnNumber(FID, FileOffset);
  if (offset)
    *offset = FileOffset;
}

void clang_getFileLocation(CXSourceLocation location,
                           CXFile *file,
                           unsigned *line,
                           unsigned *column,
                           unsigned *offset) {
  if (!isASTUnitSourceLocation(location)) {
    CXLoadedDiagnostic::decodeLocation(location, file, line,
                                           column, offset);
    return;
  }

  SourceLocation Loc = SourceLocation::getFromRawEncoding(location.int_data);

  if (!location.ptr_data[0] || Loc.isInvalid())
    return createNullLocation(file, line, column, offset);

  const SourceManager &SM =
  *static_cast<const SourceManager*>(location.ptr_data[0]);
  SourceLocation FileLoc = SM.getFileLoc(Loc);
  std::pair<FileID, unsigned> LocInfo = SM.getDecomposedLoc(FileLoc);
  FileID FID = LocInfo.first;
  unsigned FileOffset = LocInfo.second;

  if (FID.isInvalid())
    return createNullLocation(file, line, column, offset);

  if (file)
    *file = cxfile::makeCXFile(SM.getFileEntryRefForID(FID));
  if (line)
    *line = SM.getLineNumber(FID, FileOffset);
  if (column)
    *column = SM.getColumnNumber(FID, FileOffset);
  if (offset)
    *offset = FileOffset;
}
