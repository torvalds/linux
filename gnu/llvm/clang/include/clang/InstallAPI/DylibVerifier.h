//===- InstallAPI/DylibVerifier.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_DYLIBVERIFIER_H
#define LLVM_CLANG_INSTALLAPI_DYLIBVERIFIER_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/InstallAPI/MachO.h"

namespace clang {
namespace installapi {
struct FrontendAttrs;

/// A list of InstallAPI verification modes.
enum class VerificationMode {
  Invalid,
  ErrorsOnly,
  ErrorsAndWarnings,
  Pedantic,
};

using LibAttrs = llvm::StringMap<ArchitectureSet>;
using ReexportedInterfaces = llvm::SmallVector<llvm::MachO::InterfaceFile, 8>;

// Pointers to information about a zippered declaration used for
// querying and reporting violations against different
// declarations that all map to the same symbol.
struct ZipperedDeclSource {
  const FrontendAttrs *FA;
  clang::SourceManager *SrcMgr;
  Target T;
};
using ZipperedDeclSources = std::vector<ZipperedDeclSource>;

/// Service responsible to tracking state of verification across the
/// lifetime of InstallAPI.
/// As declarations are collected during AST traversal, they are
/// compared as symbols against what is available in the binary dylib.
class DylibVerifier : llvm::MachO::RecordVisitor {
private:
  struct SymbolContext;
  struct DWARFContext;

public:
  enum class Result { NoVerify, Ignore, Valid, Invalid };
  struct VerifierContext {
    // Current target being verified against the AST.
    llvm::MachO::Target Target;

    // Target specific API from binary.
    RecordsSlice *DylibSlice = nullptr;

    // Query state of verification after AST has been traversed.
    Result FrontendState = Result::Ignore;

    // First error for AST traversal, which is tied to the target triple.
    bool DiscoveredFirstError = false;

    // Determines what kind of banner to print a violation for.
    bool PrintArch = false;

    // Engine for reporting violations.
    DiagnosticsEngine *Diag = nullptr;

    // Handle diagnostics reporting for target level violations.
    void emitDiag(llvm::function_ref<void()> Report, RecordLoc *Loc = nullptr);

    VerifierContext() = default;
    VerifierContext(DiagnosticsEngine *Diag) : Diag(Diag) {}
  };

  DylibVerifier() = default;

  DylibVerifier(llvm::MachO::Records &&Dylib, ReexportedInterfaces &&Reexports,
                AliasMap Aliases, DiagnosticsEngine *Diag,
                VerificationMode Mode, bool Zippered, bool Demangle,
                StringRef DSYMPath)
      : Dylib(std::move(Dylib)), Reexports(std::move(Reexports)),
        Aliases(std::move(Aliases)), Mode(Mode), Zippered(Zippered),
        Demangle(Demangle), DSYMPath(DSYMPath),
        Exports(std::make_unique<SymbolSet>()), Ctx(VerifierContext{Diag}) {}

  Result verify(GlobalRecord *R, const FrontendAttrs *FA);
  Result verify(ObjCInterfaceRecord *R, const FrontendAttrs *FA);
  Result verify(ObjCIVarRecord *R, const FrontendAttrs *FA,
                const StringRef SuperClass);

  // Scan through dylib slices and report any remaining missing exports.
  Result verifyRemainingSymbols();

  /// Compare and report the attributes represented as
  /// load commands in the dylib to the attributes provided via options.
  bool verifyBinaryAttrs(const ArrayRef<Target> ProvidedTargets,
                         const BinaryAttrs &ProvidedBA,
                         const LibAttrs &ProvidedReexports,
                         const LibAttrs &ProvidedClients,
                         const LibAttrs &ProvidedRPaths, const FileType &FT);

  /// Initialize target for verification.
  void setTarget(const Target &T);

  /// Release ownership over exports.
  std::unique_ptr<SymbolSet> takeExports();

  /// Get result of verification.
  Result getState() const { return Ctx.FrontendState; }

  /// Set different source managers to the same diagnostics engine.
  void setSourceManager(IntrusiveRefCntPtr<SourceManager> SourceMgr);

private:
  /// Determine whether to compare declaration to symbol in binary.
  bool canVerify();

  /// Shared implementation for verifying exported symbols.
  Result verifyImpl(Record *R, SymbolContext &SymCtx);

  /// Check if declaration is marked as obsolete, they are
  // expected to result in a symbol mismatch.
  bool shouldIgnoreObsolete(const Record *R, SymbolContext &SymCtx,
                            const Record *DR);

  /// Check if declaration is exported from a reexported library. These
  /// symbols should be omitted from the text-api file.
  bool shouldIgnoreReexport(const Record *R, SymbolContext &SymCtx) const;

  // Ignore and omit unavailable symbols in zippered libraries.
  bool shouldIgnoreZipperedAvailability(const Record *R, SymbolContext &SymCtx);

  // Check if an internal declaration in zippered library has an
  // external declaration for a different platform. This results
  // in the symbol being in a "separate" platform slice.
  bool shouldIgnoreInternalZipperedSymbol(const Record *R,
                                          const SymbolContext &SymCtx) const;

  /// Compare the visibility declarations to the linkage of symbol found in
  /// dylib.
  Result compareVisibility(const Record *R, SymbolContext &SymCtx,
                           const Record *DR);

  /// An ObjCInterfaceRecord can represent up to three symbols. When verifying,
  // account for this granularity.
  bool compareObjCInterfaceSymbols(const Record *R, SymbolContext &SymCtx,
                                   const ObjCInterfaceRecord *DR);

  /// Validate availability annotations against dylib.
  Result compareAvailability(const Record *R, SymbolContext &SymCtx,
                             const Record *DR);

  /// Compare and validate matching symbol flags.
  bool compareSymbolFlags(const Record *R, SymbolContext &SymCtx,
                          const Record *DR);

  /// Update result state on each call to `verify`.
  void updateState(Result State);

  /// Add verified exported symbol.
  void addSymbol(const Record *R, SymbolContext &SymCtx,
                 TargetList &&Targets = {});

  /// Find matching dylib slice for target triple that is being parsed.
  void assignSlice(const Target &T);

  /// Shared implementation for verifying exported symbols in dylib.
  void visitSymbolInDylib(const Record &R, SymbolContext &SymCtx);

  void visitGlobal(const GlobalRecord &R) override;
  void visitObjCInterface(const ObjCInterfaceRecord &R) override;
  void visitObjCCategory(const ObjCCategoryRecord &R) override;
  void visitObjCIVar(const ObjCIVarRecord &R, const StringRef Super);

  /// Gather annotations for symbol for error reporting.
  std::string getAnnotatedName(const Record *R, SymbolContext &SymCtx,
                               bool ValidSourceLoc = true);

  /// Extract source location for symbol implementations.
  /// As this is a relatively expensive operation, it is only used
  /// when there is a violation to report and there is not a known declaration
  /// in the interface.
  void accumulateSrcLocForDylibSymbols();

  // Symbols in dylib.
  llvm::MachO::Records Dylib;

  // Reexported interfaces apart of the library.
  ReexportedInterfaces Reexports;

  // Symbol aliases.
  AliasMap Aliases;

  // Controls what class of violations to report.
  VerificationMode Mode = VerificationMode::Invalid;

  // Library is zippered.
  bool Zippered = false;

  // Attempt to demangle when reporting violations.
  bool Demangle = false;

  // File path to DSYM file.
  StringRef DSYMPath;

  // Valid symbols in final text file.
  std::unique_ptr<SymbolSet> Exports = std::make_unique<SymbolSet>();

  // Unavailable or obsoleted declarations for a zippered library.
  // These are cross referenced against symbols in the dylib.
  llvm::StringMap<ZipperedDeclSources> DeferredZipperedSymbols;

  // Track current state of verification while traversing AST.
  VerifierContext Ctx;

  // Track DWARF provided source location for dylibs.
  DWARFContext *DWARFCtx = nullptr;

  // Source manager for each unique compiler instance.
  llvm::SmallVector<IntrusiveRefCntPtr<SourceManager>, 12> SourceManagers;
};

} // namespace installapi
} // namespace clang
#endif // LLVM_CLANG_INSTALLAPI_DYLIBVERIFIER_H
