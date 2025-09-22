//===- MCCodeView.h - Machine Code CodeView support -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Holds state from .cv_file and .cv_loc directives for later emission.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEVIEW_H
#define LLVM_MC_MCCODEVIEW_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include <map>
#include <vector>

namespace llvm {
class MCAssembler;
class MCCVDefRangeFragment;
class MCCVInlineLineTableFragment;
class MCDataFragment;
class MCFragment;
class MCSection;
class MCSymbol;
class MCContext;
class MCObjectStreamer;
class MCStreamer;

/// Instances of this class represent the information from a
/// .cv_loc directive.
class MCCVLoc {
  const MCSymbol *Label = nullptr;
  uint32_t FunctionId;
  uint32_t FileNum;
  uint32_t Line;
  uint16_t Column;
  uint16_t PrologueEnd : 1;
  uint16_t IsStmt : 1;

private: // CodeViewContext manages these
  friend class CodeViewContext;
  MCCVLoc(const MCSymbol *Label, unsigned functionid, unsigned fileNum,
          unsigned line, unsigned column, bool prologueend, bool isstmt)
      : Label(Label), FunctionId(functionid), FileNum(fileNum), Line(line),
        Column(column), PrologueEnd(prologueend), IsStmt(isstmt) {}

  // Allow the default copy constructor and assignment operator to be used
  // for an MCCVLoc object.

public:
  const MCSymbol *getLabel() const { return Label; }

  unsigned getFunctionId() const { return FunctionId; }

  /// Get the FileNum of this MCCVLoc.
  unsigned getFileNum() const { return FileNum; }

  /// Get the Line of this MCCVLoc.
  unsigned getLine() const { return Line; }

  /// Get the Column of this MCCVLoc.
  unsigned getColumn() const { return Column; }

  bool isPrologueEnd() const { return PrologueEnd; }
  bool isStmt() const { return IsStmt; }

  void setLabel(const MCSymbol *L) { Label = L; }

  void setFunctionId(unsigned FID) { FunctionId = FID; }

  /// Set the FileNum of this MCCVLoc.
  void setFileNum(unsigned fileNum) { FileNum = fileNum; }

  /// Set the Line of this MCCVLoc.
  void setLine(unsigned line) { Line = line; }

  /// Set the Column of this MCCVLoc.
  void setColumn(unsigned column) {
    assert(column <= UINT16_MAX);
    Column = column;
  }

  void setPrologueEnd(bool PE) { PrologueEnd = PE; }
  void setIsStmt(bool IS) { IsStmt = IS; }
};

/// Information describing a function or inlined call site introduced by
/// .cv_func_id or .cv_inline_site_id. Accumulates information from .cv_loc
/// directives used with this function's id or the id of an inlined call site
/// within this function or inlined call site.
struct MCCVFunctionInfo {
  /// If this represents an inlined call site, then ParentFuncIdPlusOne will be
  /// the parent function id plus one. If this represents a normal function,
  /// then there is no parent, and ParentFuncIdPlusOne will be FunctionSentinel.
  /// If this struct is an unallocated slot in the function info vector, then
  /// ParentFuncIdPlusOne will be zero.
  unsigned ParentFuncIdPlusOne = 0;

  enum : unsigned { FunctionSentinel = ~0U };

  struct LineInfo {
    unsigned File;
    unsigned Line;
    unsigned Col;
  };

  LineInfo InlinedAt;

  /// The section of the first .cv_loc directive used for this function, or null
  /// if none has been seen yet.
  MCSection *Section = nullptr;

  /// Map from inlined call site id to the inlined at location to use for that
  /// call site. Call chains are collapsed, so for the call chain 'f -> g -> h',
  /// the InlinedAtMap of 'f' will contain entries for 'g' and 'h' that both
  /// list the line info for the 'g' call site.
  DenseMap<unsigned, LineInfo> InlinedAtMap;

  /// Returns true if this is function info has not yet been used in a
  /// .cv_func_id or .cv_inline_site_id directive.
  bool isUnallocatedFunctionInfo() const { return ParentFuncIdPlusOne == 0; }

  /// Returns true if this represents an inlined call site, meaning
  /// ParentFuncIdPlusOne is neither zero nor ~0U.
  bool isInlinedCallSite() const {
    return !isUnallocatedFunctionInfo() &&
           ParentFuncIdPlusOne != FunctionSentinel;
  }

  unsigned getParentFuncId() const {
    assert(isInlinedCallSite());
    return ParentFuncIdPlusOne - 1;
  }
};

/// Holds state from .cv_file and .cv_loc directives for later emission.
class CodeViewContext {
public:
  CodeViewContext(MCContext *MCCtx) : MCCtx(MCCtx) {}
  ~CodeViewContext();

  CodeViewContext &operator=(const CodeViewContext &other) = delete;
  CodeViewContext(const CodeViewContext &other) = delete;

  bool isValidFileNumber(unsigned FileNumber) const;
  bool addFile(MCStreamer &OS, unsigned FileNumber, StringRef Filename,
               ArrayRef<uint8_t> ChecksumBytes, uint8_t ChecksumKind);

  /// Records the function id of a normal function. Returns false if the
  /// function id has already been used, and true otherwise.
  bool recordFunctionId(unsigned FuncId);

  /// Records the function id of an inlined call site. Records the "inlined at"
  /// location info of the call site, including what function or inlined call
  /// site it was inlined into. Returns false if the function id has already
  /// been used, and true otherwise.
  bool recordInlinedCallSiteId(unsigned FuncId, unsigned IAFunc,
                               unsigned IAFile, unsigned IALine,
                               unsigned IACol);

  /// Retreive the function info if this is a valid function id, or nullptr.
  MCCVFunctionInfo *getCVFunctionInfo(unsigned FuncId);

  /// Saves the information from the currently parsed .cv_loc directive
  /// and sets CVLocSeen.  When the next instruction is assembled an entry
  /// in the line number table with this information and the address of the
  /// instruction will be created.
  void recordCVLoc(MCContext &Ctx, const MCSymbol *Label, unsigned FunctionId,
                   unsigned FileNo, unsigned Line, unsigned Column,
                   bool PrologueEnd, bool IsStmt);

  /// Add a line entry.
  void addLineEntry(const MCCVLoc &LineEntry);

  std::vector<MCCVLoc> getFunctionLineEntries(unsigned FuncId);

  std::pair<size_t, size_t> getLineExtent(unsigned FuncId);
  std::pair<size_t, size_t> getLineExtentIncludingInlinees(unsigned FuncId);

  ArrayRef<MCCVLoc> getLinesForExtent(size_t L, size_t R);

  /// Emits a line table substream.
  void emitLineTableForFunction(MCObjectStreamer &OS, unsigned FuncId,
                                const MCSymbol *FuncBegin,
                                const MCSymbol *FuncEnd);

  void emitInlineLineTableForFunction(MCObjectStreamer &OS,
                                      unsigned PrimaryFunctionId,
                                      unsigned SourceFileId,
                                      unsigned SourceLineNum,
                                      const MCSymbol *FnStartSym,
                                      const MCSymbol *FnEndSym);

  /// Encodes the binary annotations once we have a layout.
  void encodeInlineLineTable(const MCAssembler &Asm,
                             MCCVInlineLineTableFragment &F);

  MCFragment *
  emitDefRange(MCObjectStreamer &OS,
               ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
               StringRef FixedSizePortion);

  void encodeDefRange(const MCAssembler &Asm, MCCVDefRangeFragment &F);

  /// Emits the string table substream.
  void emitStringTable(MCObjectStreamer &OS);

  /// Emits the file checksum substream.
  void emitFileChecksums(MCObjectStreamer &OS);

  /// Emits the offset into the checksum table of the given file number.
  void emitFileChecksumOffset(MCObjectStreamer &OS, unsigned FileNo);

  /// Add something to the string table.  Returns the final string as well as
  /// offset into the string table.
  std::pair<StringRef, unsigned> addToStringTable(StringRef S);

private:
  MCContext *MCCtx;

  /// Map from string to string table offset.
  StringMap<unsigned> StringTable;

  /// The fragment that ultimately holds our strings.
  MCDataFragment *StrTabFragment = nullptr;
  bool InsertedStrTabFragment = false;

  MCDataFragment *getStringTableFragment();

  /// Get a string table offset.
  unsigned getStringTableOffset(StringRef S);

  struct FileInfo {
    unsigned StringTableOffset;

    // Indicates if this FileInfo corresponds to an actual file, or hasn't been
    // set yet.
    bool Assigned = false;

    uint8_t ChecksumKind;

    ArrayRef<uint8_t> Checksum;

    // Checksum offset stored as a symbol because it might be requested
    // before it has been calculated, so a fixup may be needed.
    MCSymbol *ChecksumTableOffset;
  };

  /// Array storing added file information.
  SmallVector<FileInfo, 4> Files;

  /// The offset of the first and last .cv_loc directive for a given function
  /// id.
  std::map<unsigned, std::pair<size_t, size_t>> MCCVLineStartStop;

  /// A collection of MCCVLoc for each section.
  std::vector<MCCVLoc> MCCVLines;

  /// All known functions and inlined call sites, indexed by function id.
  std::vector<MCCVFunctionInfo> Functions;

  /// Indicate whether we have already laid out the checksum table addresses or
  /// not.
  bool ChecksumOffsetsAssigned = false;
};

} // end namespace llvm
#endif
