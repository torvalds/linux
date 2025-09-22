//===- AnnotateFunctions.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Attribute plugin to mark a virtual method as ``call_super``, subclasses must
// call it in the overridden method.
//
// This example shows that attribute plugins combined with ``PluginASTAction``
// in Clang can do some of the same things which Java Annotations do.
//
// Unlike the other attribute plugin examples, this one does not attach an
// attribute AST node to the declaration AST node. Instead, it keeps a separate
// list of attributed declarations, which may be faster than using
// ``Decl::getAttr<T>()`` in some cases. The disadvantage of this approach is
// that the attribute is not part of the AST, which means that dumping the AST
// will lose the attribute information, pretty printing the AST won't write the
// attribute back out to source, and AST matchers will not be able to match
// against the attribute on the declaration.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Sema/ParsedAttr.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "llvm/ADT/SmallPtrSet.h"
using namespace clang;

namespace {
// Cached methods which are marked as 'call_super'.
llvm::SmallPtrSet<const CXXMethodDecl *, 16> MarkedMethods;
bool isMarkedAsCallSuper(const CXXMethodDecl *D) {
  // Uses this way to avoid add an annotation attr to the AST.
  return MarkedMethods.contains(D);
}

class MethodUsageVisitor : public RecursiveASTVisitor<MethodUsageVisitor> {
public:
  bool IsOverriddenUsed = false;
  explicit MethodUsageVisitor(
      llvm::SmallPtrSet<const CXXMethodDecl *, 16> &MustCalledMethods)
      : MustCalledMethods(MustCalledMethods) {}
  bool VisitCallExpr(CallExpr *CallExpr) {
    const CXXMethodDecl *Callee = nullptr;
    for (const auto &MustCalled : MustCalledMethods) {
      if (CallExpr->getCalleeDecl() == MustCalled) {
        // Super is called.
        // Notice that we cannot do delete or insert in the iteration
        // when using SmallPtrSet.
        Callee = MustCalled;
      }
    }
    if (Callee)
      MustCalledMethods.erase(Callee);

    return true;
  }

private:
  llvm::SmallPtrSet<const CXXMethodDecl *, 16> &MustCalledMethods;
};

class CallSuperVisitor : public RecursiveASTVisitor<CallSuperVisitor> {
public:
  CallSuperVisitor(DiagnosticsEngine &Diags) : Diags(Diags) {
    WarningSuperNotCalled = Diags.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "virtual function %q0 is marked as 'call_super' but this overriding "
        "method does not call the base version");
    NotePreviousCallSuperDeclaration = Diags.getCustomDiagID(
        DiagnosticsEngine::Note, "function marked 'call_super' here");
  }
  bool VisitCXXMethodDecl(CXXMethodDecl *MethodDecl) {
    if (MethodDecl->isThisDeclarationADefinition() && MethodDecl->hasBody()) {
      // First find out which overridden methods are marked as 'call_super'
      llvm::SmallPtrSet<const CXXMethodDecl *, 16> OverriddenMarkedMethods;
      for (const auto *Overridden : MethodDecl->overridden_methods()) {
        if (isMarkedAsCallSuper(Overridden)) {
          OverriddenMarkedMethods.insert(Overridden);
        }
      }

      // Now find if the superclass method is called in `MethodDecl`.
      MethodUsageVisitor Visitor(OverriddenMarkedMethods);
      Visitor.TraverseDecl(MethodDecl);
      // After traversing, all methods left in `OverriddenMarkedMethods`
      // are not called, warn about these.
      for (const auto &LeftOverriddens : OverriddenMarkedMethods) {
        Diags.Report(MethodDecl->getLocation(), WarningSuperNotCalled)
            << LeftOverriddens << MethodDecl;
        Diags.Report(LeftOverriddens->getLocation(),
                     NotePreviousCallSuperDeclaration);
      }
    }
    return true;
  }

private:
  DiagnosticsEngine &Diags;
  unsigned WarningSuperNotCalled;
  unsigned NotePreviousCallSuperDeclaration;
};

class CallSuperConsumer : public ASTConsumer {
public:
  void HandleTranslationUnit(ASTContext &Context) override {
    auto &Diags = Context.getDiagnostics();
    for (const auto *Method : MarkedMethods) {
      lateDiagAppertainsToDecl(Diags, Method);
    }

    CallSuperVisitor Visitor(Context.getDiagnostics());
    Visitor.TraverseDecl(Context.getTranslationUnitDecl());
  }

private:
  // This function does checks which cannot be done in `diagAppertainsToDecl()`,
  // typical example is checking Attributes (such as `FinalAttr`), on the time
  // when `diagAppertainsToDecl()` is called, `FinalAttr` is not added into
  // the AST yet.
  void lateDiagAppertainsToDecl(DiagnosticsEngine &Diags,
                                const CXXMethodDecl *MethodDecl) {
    if (MethodDecl->hasAttr<FinalAttr>()) {
      unsigned ID = Diags.getCustomDiagID(
          DiagnosticsEngine::Warning,
          "'call_super' attribute marked on a final method");
      Diags.Report(MethodDecl->getLocation(), ID);
    }
  }
};

class CallSuperAction : public PluginASTAction {
public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<CallSuperConsumer>();
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    if (!args.empty() && args[0] == "help")
      llvm::errs() << "Help for the CallSuperAttr plugin goes here\n";
    return true;
  }

  PluginASTAction::ActionType getActionType() override {
    return AddBeforeMainAction;
  }
};

struct CallSuperAttrInfo : public ParsedAttrInfo {
  CallSuperAttrInfo() {
    OptArgs = 0;
    static constexpr Spelling S[] = {
        {ParsedAttr::AS_GNU, "call_super"},
        {ParsedAttr::AS_CXX11, "clang::call_super"}};
    Spellings = S;
  }

  bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                            const Decl *D) const override {
    const auto *TheMethod = dyn_cast_or_null<CXXMethodDecl>(D);
    if (!TheMethod || !TheMethod->isVirtual()) {
      S.Diag(Attr.getLoc(), diag::warn_attribute_wrong_decl_type_str)
          << Attr << Attr.isRegularKeywordAttribute() << "virtual functions";
      return false;
    }
    MarkedMethods.insert(TheMethod);
    return true;
  }
  AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                   const ParsedAttr &Attr) const override {
    // No need to add an attr object (usually an `AnnotateAttr` is added).
    // Save the address of the Decl in a set, it maybe faster than compare to
    // strings.
    return AttributeNotApplied;
  }
};

} // namespace
static FrontendPluginRegistry::Add<CallSuperAction>
    X("call_super_plugin", "clang plugin, checks every overridden virtual "
                           "function whether called this function or not.");
static ParsedAttrInfoRegistry::Add<CallSuperAttrInfo>
    Y("call_super_attr", "Attr plugin to define 'call_super' attribute");
