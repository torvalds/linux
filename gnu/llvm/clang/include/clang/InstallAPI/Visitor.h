//===- InstallAPI/Visitor.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// ASTVisitor Interface for InstallAPI frontend operations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_VISITOR_H
#define LLVM_CLANG_INSTALLAPI_VISITOR_H

#include "clang/AST/Mangle.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/InstallAPI/Context.h"
#include "llvm/ADT/Twine.h"

namespace clang {
struct AvailabilityInfo;
namespace installapi {

/// ASTVisitor for collecting declarations that represent global symbols.
class InstallAPIVisitor final : public ASTConsumer,
                                public RecursiveASTVisitor<InstallAPIVisitor> {
public:
  InstallAPIVisitor(ASTContext &ASTCtx, InstallAPIContext &Ctx,
                    SourceManager &SrcMgr, Preprocessor &PP)
      : Ctx(Ctx), SrcMgr(SrcMgr), PP(PP),
        MC(ItaniumMangleContext::create(ASTCtx, ASTCtx.getDiagnostics())),
        Layout(ASTCtx.getTargetInfo().getDataLayoutString()) {}
  void HandleTranslationUnit(ASTContext &ASTCtx) override;
  bool shouldVisitTemplateInstantiations() const { return true; }

  /// Collect global variables.
  bool VisitVarDecl(const VarDecl *D);

  /// Collect global functions.
  bool VisitFunctionDecl(const FunctionDecl *D);

  /// Collect Objective-C Interface declarations.
  /// Every Objective-C class has an interface declaration that lists all the
  /// ivars, properties, and methods of the class.
  bool VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D);

  /// Collect Objective-C Category/Extension declarations.
  ///
  /// The class that is being extended might come from a different library and
  /// is therefore itself not collected.
  bool VisitObjCCategoryDecl(const ObjCCategoryDecl *D);

  /// Collect global c++ declarations.
  bool VisitCXXRecordDecl(const CXXRecordDecl *D);

private:
  std::string getMangledName(const NamedDecl *D) const;
  std::string getBackendMangledName(llvm::Twine Name) const;
  std::string getMangledCXXVTableName(const CXXRecordDecl *D) const;
  std::string getMangledCXXThunk(const GlobalDecl &D, const ThunkInfo &Thunk,
                                 bool ElideOverrideInfo) const;
  std::string getMangledCXXRTTI(const CXXRecordDecl *D) const;
  std::string getMangledCXXRTTIName(const CXXRecordDecl *D) const;
  std::string getMangledCtorDtor(const CXXMethodDecl *D, int Type) const;

  std::optional<HeaderType> getAccessForDecl(const NamedDecl *D) const;
  void recordObjCInstanceVariables(
      const ASTContext &ASTCtx, llvm::MachO::ObjCContainerRecord *Record,
      StringRef SuperClass,
      const llvm::iterator_range<
          DeclContext::specific_decl_iterator<ObjCIvarDecl>>
          Ivars);
  void emitVTableSymbols(const CXXRecordDecl *D, const AvailabilityInfo &Avail,
                         const HeaderType Access, bool EmittedVTable = false);

  InstallAPIContext &Ctx;
  SourceManager &SrcMgr;
  Preprocessor &PP;
  std::unique_ptr<clang::ItaniumMangleContext> MC;
  StringRef Layout;
};

} // namespace installapi
} // namespace clang

#endif // LLVM_CLANG_INSTALLAPI_VISITOR_H
