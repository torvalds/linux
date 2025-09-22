//===-- ARCMT.h - ARC Migration Rewriter ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ARCMIGRATE_ARCMT_H
#define LLVM_CLANG_ARCMIGRATE_ARCMT_H

#include "clang/ARCMigrate/FileRemapper.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Frontend/CompilerInvocation.h"

namespace clang {
  class ASTContext;
  class DiagnosticConsumer;
  class PCHContainerOperations;

namespace arcmt {
  class MigrationPass;

/// Creates an AST with the provided CompilerInvocation but with these
/// changes:
///   -if a PCH/PTH is set, the original header is used instead
///   -Automatic Reference Counting mode is enabled
///
/// It then checks the AST and produces errors/warning for ARC migration issues
/// that the user needs to handle manually.
///
/// \param emitPremigrationARCErrors if true all ARC errors will get emitted
/// even if the migrator can fix them, but the function will still return false
/// if all ARC errors can be fixed.
///
/// \param plistOut if non-empty, it is the file path to store the plist with
/// the pre-migration ARC diagnostics.
///
/// \returns false if no error is produced, true otherwise.
bool
checkForManualIssues(CompilerInvocation &CI, const FrontendInputFile &Input,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagClient,
                     bool emitPremigrationARCErrors = false,
                     StringRef plistOut = StringRef());

/// Works similar to checkForManualIssues but instead of checking, it
/// applies automatic modifications to source files to conform to ARC.
///
/// \returns false if no error is produced, true otherwise.
bool
applyTransformations(CompilerInvocation &origCI,
                     const FrontendInputFile &Input,
                     std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                     DiagnosticConsumer *DiagClient);

/// Applies automatic modifications and produces temporary files
/// and metadata into the \p outputDir path.
///
/// \param emitPremigrationARCErrors if true all ARC errors will get emitted
/// even if the migrator can fix them, but the function will still return false
/// if all ARC errors can be fixed.
///
/// \param plistOut if non-empty, it is the file path to store the plist with
/// the pre-migration ARC diagnostics.
///
/// \returns false if no error is produced, true otherwise.
bool migrateWithTemporaryFiles(
    CompilerInvocation &origCI, const FrontendInputFile &Input,
    std::shared_ptr<PCHContainerOperations> PCHContainerOps,
    DiagnosticConsumer *DiagClient, StringRef outputDir,
    bool emitPremigrationARCErrors, StringRef plistOut);

/// Get the set of file remappings from the \p outputDir path that
/// migrateWithTemporaryFiles produced.
///
/// \returns false if no error is produced, true otherwise.
bool getFileRemappings(std::vector<std::pair<std::string,std::string> > &remap,
                       StringRef outputDir,
                       DiagnosticConsumer *DiagClient);

/// Get the set of file remappings from a list of files with remapping
/// info.
///
/// \returns false if no error is produced, true otherwise.
bool getFileRemappingsFromFileList(
                        std::vector<std::pair<std::string,std::string> > &remap,
                        ArrayRef<StringRef> remapFiles,
                        DiagnosticConsumer *DiagClient);

typedef void (*TransformFn)(MigrationPass &pass);

std::vector<TransformFn> getAllTransformations(LangOptions::GCMode OrigGCMode,
                                               bool NoFinalizeRemoval);

class MigrationProcess {
  CompilerInvocation OrigCI;
  std::shared_ptr<PCHContainerOperations> PCHContainerOps;
  DiagnosticConsumer *DiagClient;
  FileRemapper Remapper;

public:
  bool HadARCErrors;

  MigrationProcess(CompilerInvocation &CI,
                   std::shared_ptr<PCHContainerOperations> PCHContainerOps,
                   DiagnosticConsumer *diagClient,
                   StringRef outputDir = StringRef());

  class RewriteListener {
  public:
    virtual ~RewriteListener();

    virtual void start(ASTContext &Ctx) { }
    virtual void finish() { }

    virtual void insert(SourceLocation loc, StringRef text) { }
    virtual void remove(CharSourceRange range) { }
  };

  bool applyTransform(TransformFn trans, RewriteListener *listener = nullptr);

  FileRemapper &getRemapper() { return Remapper; }
};

} // end namespace arcmt

}  // end namespace clang

#endif
