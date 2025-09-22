//===---- CoverageMappingGen.h - Coverage mapping generation ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based code coverage mapping generator
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_COVERAGEMAPPINGGEN_H
#define LLVM_CLANG_LIB_CODEGEN_COVERAGEMAPPINGGEN_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm::coverage {
extern cl::opt<bool> SystemHeadersCoverage;
}

namespace clang {

class LangOptions;
class SourceManager;
class FileEntry;
class Preprocessor;
class Decl;
class Stmt;

struct SkippedRange {
  enum Kind {
    PPIfElse, // Preprocessor #if/#else ...
    EmptyLine,
    Comment,
  };

  SourceRange Range;
  // The location of token before the skipped source range.
  SourceLocation PrevTokLoc;
  // The location of token after the skipped source range.
  SourceLocation NextTokLoc;
  // The nature of this skipped range
  Kind RangeKind;

  bool isComment() { return RangeKind == Comment; }
  bool isEmptyLine() { return RangeKind == EmptyLine; }
  bool isPPIfElse() { return RangeKind == PPIfElse; }

  SkippedRange(SourceRange Range, Kind K,
               SourceLocation PrevTokLoc = SourceLocation(),
               SourceLocation NextTokLoc = SourceLocation())
      : Range(Range), PrevTokLoc(PrevTokLoc), NextTokLoc(NextTokLoc),
        RangeKind(K) {}
};

/// Stores additional source code information like skipped ranges which
/// is required by the coverage mapping generator and is obtained from
/// the preprocessor.
class CoverageSourceInfo : public PPCallbacks,
                           public CommentHandler,
                           public EmptylineHandler {
  // A vector of skipped source ranges and PrevTokLoc with NextTokLoc.
  std::vector<SkippedRange> SkippedRanges;

  SourceManager &SourceMgr;

public:
  // Location of the token parsed before HandleComment is called. This is
  // updated every time Preprocessor::Lex lexes a new token.
  SourceLocation PrevTokLoc;

  CoverageSourceInfo(SourceManager &SourceMgr) : SourceMgr(SourceMgr) {}

  std::vector<SkippedRange> &getSkippedRanges() { return SkippedRanges; }

  void AddSkippedRange(SourceRange Range, SkippedRange::Kind RangeKind);

  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override;

  void HandleEmptyline(SourceRange Range) override;

  bool HandleComment(Preprocessor &PP, SourceRange Range) override;

  void updateNextTokLoc(SourceLocation Loc);
};

namespace CodeGen {

class CodeGenModule;

namespace MCDC {
struct State;
}

/// Organizes the cross-function state that is used while generating
/// code coverage mapping data.
class CoverageMappingModuleGen {
  /// Information needed to emit a coverage record for a function.
  struct FunctionInfo {
    uint64_t NameHash;
    uint64_t FuncHash;
    std::string CoverageMapping;
    bool IsUsed;
  };

  CodeGenModule &CGM;
  CoverageSourceInfo &SourceInfo;
  llvm::SmallDenseMap<FileEntryRef, unsigned, 8> FileEntries;
  std::vector<llvm::Constant *> FunctionNames;
  std::vector<FunctionInfo> FunctionRecords;

  std::string getCurrentDirname();
  std::string normalizeFilename(StringRef Filename);

  /// Emit a function record.
  void emitFunctionMappingRecord(const FunctionInfo &Info,
                                 uint64_t FilenamesRef);

public:
  static CoverageSourceInfo *setUpCoverageCallbacks(Preprocessor &PP);

  CoverageMappingModuleGen(CodeGenModule &CGM, CoverageSourceInfo &SourceInfo);

  CoverageSourceInfo &getSourceInfo() const {
    return SourceInfo;
  }

  /// Add a function's coverage mapping record to the collection of the
  /// function mapping records.
  void addFunctionMappingRecord(llvm::GlobalVariable *FunctionName,
                                StringRef FunctionNameValue,
                                uint64_t FunctionHash,
                                const std::string &CoverageMapping,
                                bool IsUsed = true);

  /// Emit the coverage mapping data for a translation unit.
  void emit();

  /// Return the coverage mapping translation unit file id
  /// for the given file.
  unsigned getFileID(FileEntryRef File);

  /// Return an interface into CodeGenModule.
  CodeGenModule &getCodeGenModule() { return CGM; }
};

/// Organizes the per-function state that is used while generating
/// code coverage mapping data.
class CoverageMappingGen {
  CoverageMappingModuleGen &CVM;
  SourceManager &SM;
  const LangOptions &LangOpts;
  llvm::DenseMap<const Stmt *, unsigned> *CounterMap;
  MCDC::State *MCDCState;

public:
  CoverageMappingGen(CoverageMappingModuleGen &CVM, SourceManager &SM,
                     const LangOptions &LangOpts)
      : CVM(CVM), SM(SM), LangOpts(LangOpts), CounterMap(nullptr),
        MCDCState(nullptr) {}

  CoverageMappingGen(CoverageMappingModuleGen &CVM, SourceManager &SM,
                     const LangOptions &LangOpts,
                     llvm::DenseMap<const Stmt *, unsigned> *CounterMap,
                     MCDC::State *MCDCState)
      : CVM(CVM), SM(SM), LangOpts(LangOpts), CounterMap(CounterMap),
        MCDCState(MCDCState) {}

  /// Emit the coverage mapping data which maps the regions of
  /// code to counters that will be used to find the execution
  /// counts for those regions.
  void emitCounterMapping(const Decl *D, llvm::raw_ostream &OS);

  /// Emit the coverage mapping data for an unused function.
  /// It creates mapping regions with the counter of zero.
  void emitEmptyMapping(const Decl *D, llvm::raw_ostream &OS);
};

} // end namespace CodeGen
} // end namespace clang

#endif
