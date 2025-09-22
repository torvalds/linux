//===--- USRLocFinder.cpp - Clang refactoring library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Methods for finding all instances of a USR. Our strategy is very
/// simple; we just compare the USR at every relevant AST node with the one
/// provided.
///
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Refactoring/Rename/USRLocFinder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Tooling/Refactoring/Lookup.h"
#include "clang/Tooling/Refactoring/RecursiveSymbolVisitor.h"
#include "clang/Tooling/Refactoring/Rename/SymbolName.h"
#include "clang/Tooling/Refactoring/Rename/USRFinder.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Casting.h"
#include <cstddef>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace clang {
namespace tooling {

namespace {

// Returns true if the given Loc is valid for edit. We don't edit the
// SourceLocations that are valid or in temporary buffer.
bool IsValidEditLoc(const clang::SourceManager& SM, clang::SourceLocation Loc) {
  if (Loc.isInvalid())
    return false;
  const clang::FullSourceLoc FullLoc(Loc, SM);
  std::pair<clang::FileID, unsigned> FileIdAndOffset =
      FullLoc.getSpellingLoc().getDecomposedLoc();
  return SM.getFileEntryForID(FileIdAndOffset.first) != nullptr;
}

// This visitor recursively searches for all instances of a USR in a
// translation unit and stores them for later usage.
class USRLocFindingASTVisitor
    : public RecursiveSymbolVisitor<USRLocFindingASTVisitor> {
public:
  explicit USRLocFindingASTVisitor(const std::vector<std::string> &USRs,
                                   StringRef PrevName,
                                   const ASTContext &Context)
      : RecursiveSymbolVisitor(Context.getSourceManager(),
                               Context.getLangOpts()),
        USRSet(USRs.begin(), USRs.end()), PrevName(PrevName), Context(Context) {
  }

  bool visitSymbolOccurrence(const NamedDecl *ND,
                             ArrayRef<SourceRange> NameRanges) {
    if (USRSet.find(getUSRForDecl(ND)) != USRSet.end()) {
      assert(NameRanges.size() == 1 &&
             "Multiple name pieces are not supported yet!");
      SourceLocation Loc = NameRanges[0].getBegin();
      const SourceManager &SM = Context.getSourceManager();
      // TODO: Deal with macro occurrences correctly.
      if (Loc.isMacroID())
        Loc = SM.getSpellingLoc(Loc);
      checkAndAddLocation(Loc);
    }
    return true;
  }

  // Non-visitors:

  /// Returns a set of unique symbol occurrences. Duplicate or
  /// overlapping occurrences are erroneous and should be reported!
  SymbolOccurrences takeOccurrences() { return std::move(Occurrences); }

private:
  void checkAndAddLocation(SourceLocation Loc) {
    const SourceLocation BeginLoc = Loc;
    const SourceLocation EndLoc = Lexer::getLocForEndOfToken(
        BeginLoc, 0, Context.getSourceManager(), Context.getLangOpts());
    StringRef TokenName =
        Lexer::getSourceText(CharSourceRange::getTokenRange(BeginLoc, EndLoc),
                             Context.getSourceManager(), Context.getLangOpts());
    size_t Offset = TokenName.find(PrevName.getNamePieces()[0]);

    // The token of the source location we find actually has the old
    // name.
    if (Offset != StringRef::npos)
      Occurrences.emplace_back(PrevName, SymbolOccurrence::MatchingSymbol,
                               BeginLoc.getLocWithOffset(Offset));
  }

  const std::set<std::string> USRSet;
  const SymbolName PrevName;
  SymbolOccurrences Occurrences;
  const ASTContext &Context;
};

SourceLocation StartLocationForType(TypeLoc TL) {
  // For elaborated types (e.g. `struct a::A`) we want the portion after the
  // `struct` but including the namespace qualifier, `a::`.
  if (auto ElaboratedTypeLoc = TL.getAs<clang::ElaboratedTypeLoc>()) {
    NestedNameSpecifierLoc NestedNameSpecifier =
        ElaboratedTypeLoc.getQualifierLoc();
    if (NestedNameSpecifier.getNestedNameSpecifier())
      return NestedNameSpecifier.getBeginLoc();
    TL = TL.getNextTypeLoc();
  }
  return TL.getBeginLoc();
}

SourceLocation EndLocationForType(TypeLoc TL) {
  // Dig past any namespace or keyword qualifications.
  while (TL.getTypeLocClass() == TypeLoc::Elaborated ||
         TL.getTypeLocClass() == TypeLoc::Qualified)
    TL = TL.getNextTypeLoc();

  // The location for template specializations (e.g. Foo<int>) includes the
  // templated types in its location range.  We want to restrict this to just
  // before the `<` character.
  if (TL.getTypeLocClass() == TypeLoc::TemplateSpecialization) {
    return TL.castAs<TemplateSpecializationTypeLoc>()
        .getLAngleLoc()
        .getLocWithOffset(-1);
  }
  return TL.getEndLoc();
}

NestedNameSpecifier *GetNestedNameForType(TypeLoc TL) {
  // Dig past any keyword qualifications.
  while (TL.getTypeLocClass() == TypeLoc::Qualified)
    TL = TL.getNextTypeLoc();

  // For elaborated types (e.g. `struct a::A`) we want the portion after the
  // `struct` but including the namespace qualifier, `a::`.
  if (auto ElaboratedTypeLoc = TL.getAs<clang::ElaboratedTypeLoc>())
    return ElaboratedTypeLoc.getQualifierLoc().getNestedNameSpecifier();
  return nullptr;
}

// Find all locations identified by the given USRs for rename.
//
// This class will traverse the AST and find every AST node whose USR is in the
// given USRs' set.
class RenameLocFinder : public RecursiveASTVisitor<RenameLocFinder> {
public:
  RenameLocFinder(llvm::ArrayRef<std::string> USRs, ASTContext &Context)
      : USRSet(USRs.begin(), USRs.end()), Context(Context) {}

  // A structure records all information of a symbol reference being renamed.
  // We try to add as few prefix qualifiers as possible.
  struct RenameInfo {
    // The begin location of a symbol being renamed.
    SourceLocation Begin;
    // The end location of a symbol being renamed.
    SourceLocation End;
    // The declaration of a symbol being renamed (can be nullptr).
    const NamedDecl *FromDecl;
    // The declaration in which the nested name is contained (can be nullptr).
    const Decl *Context;
    // The nested name being replaced (can be nullptr).
    const NestedNameSpecifier *Specifier;
    // Determine whether the prefix qualifiers of the NewName should be ignored.
    // Normally, we set it to true for the symbol declaration and definition to
    // avoid adding prefix qualifiers.
    // For example, if it is true and NewName is "a::b::foo", then the symbol
    // occurrence which the RenameInfo points to will be renamed to "foo".
    bool IgnorePrefixQualifers;
  };

  bool VisitNamedDecl(const NamedDecl *Decl) {
    // UsingDecl has been handled in other place.
    if (llvm::isa<UsingDecl>(Decl))
      return true;

    // DestructorDecl has been handled in Typeloc.
    if (llvm::isa<CXXDestructorDecl>(Decl))
      return true;

    if (Decl->isImplicit())
      return true;

    if (isInUSRSet(Decl)) {
      // For the case of renaming an alias template, we actually rename the
      // underlying alias declaration of the template.
      if (const auto* TAT = dyn_cast<TypeAliasTemplateDecl>(Decl))
        Decl = TAT->getTemplatedDecl();

      auto StartLoc = Decl->getLocation();
      auto EndLoc = StartLoc;
      if (IsValidEditLoc(Context.getSourceManager(), StartLoc)) {
        RenameInfo Info = {StartLoc,
                           EndLoc,
                           /*FromDecl=*/nullptr,
                           /*Context=*/nullptr,
                           /*Specifier=*/nullptr,
                           /*IgnorePrefixQualifers=*/true};
        RenameInfos.push_back(Info);
      }
    }
    return true;
  }

  bool VisitMemberExpr(const MemberExpr *Expr) {
    const NamedDecl *Decl = Expr->getFoundDecl();
    auto StartLoc = Expr->getMemberLoc();
    auto EndLoc = Expr->getMemberLoc();
    if (isInUSRSet(Decl)) {
      RenameInfos.push_back({StartLoc, EndLoc,
                            /*FromDecl=*/nullptr,
                            /*Context=*/nullptr,
                            /*Specifier=*/nullptr,
                            /*IgnorePrefixQualifiers=*/true});
    }
    return true;
  }

  bool VisitDesignatedInitExpr(const DesignatedInitExpr *E) {
    for (const DesignatedInitExpr::Designator &D : E->designators()) {
      if (D.isFieldDesignator()) {
        if (const FieldDecl *Decl = D.getFieldDecl()) {
          if (isInUSRSet(Decl)) {
            auto StartLoc = D.getFieldLoc();
            auto EndLoc = D.getFieldLoc();
            RenameInfos.push_back({StartLoc, EndLoc,
                                   /*FromDecl=*/nullptr,
                                   /*Context=*/nullptr,
                                   /*Specifier=*/nullptr,
                                   /*IgnorePrefixQualifiers=*/true});
          }
        }
      }
    }
    return true;
  }

  bool VisitCXXConstructorDecl(const CXXConstructorDecl *CD) {
    // Fix the constructor initializer when renaming class members.
    for (const auto *Initializer : CD->inits()) {
      // Ignore implicit initializers.
      if (!Initializer->isWritten())
        continue;

      if (const FieldDecl *FD = Initializer->getMember()) {
        if (isInUSRSet(FD)) {
          auto Loc = Initializer->getSourceLocation();
          RenameInfos.push_back({Loc, Loc,
                                 /*FromDecl=*/nullptr,
                                 /*Context=*/nullptr,
                                 /*Specifier=*/nullptr,
                                 /*IgnorePrefixQualifiers=*/true});
        }
      }
    }
    return true;
  }

  bool VisitDeclRefExpr(const DeclRefExpr *Expr) {
    const NamedDecl *Decl = Expr->getFoundDecl();
    // Get the underlying declaration of the shadow declaration introduced by a
    // using declaration.
    if (auto *UsingShadow = llvm::dyn_cast<UsingShadowDecl>(Decl)) {
      Decl = UsingShadow->getTargetDecl();
    }

    auto StartLoc = Expr->getBeginLoc();
    // For template function call expressions like `foo<int>()`, we want to
    // restrict the end of location to just before the `<` character.
    SourceLocation EndLoc = Expr->hasExplicitTemplateArgs()
                                ? Expr->getLAngleLoc().getLocWithOffset(-1)
                                : Expr->getEndLoc();

    if (const auto *MD = llvm::dyn_cast<CXXMethodDecl>(Decl)) {
      if (isInUSRSet(MD)) {
        // Handle renaming static template class methods, we only rename the
        // name without prefix qualifiers and restrict the source range to the
        // name.
        RenameInfos.push_back({EndLoc, EndLoc,
                               /*FromDecl=*/nullptr,
                               /*Context=*/nullptr,
                               /*Specifier=*/nullptr,
                               /*IgnorePrefixQualifiers=*/true});
        return true;
      }
    }

    // In case of renaming an enum declaration, we have to explicitly handle
    // unscoped enum constants referenced in expressions (e.g.
    // "auto r = ns1::ns2::Green" where Green is an enum constant of an unscoped
    // enum decl "ns1::ns2::Color") as these enum constants cannot be caught by
    // TypeLoc.
    if (const auto *T = llvm::dyn_cast<EnumConstantDecl>(Decl)) {
      // FIXME: Handle the enum constant without prefix qualifiers (`a = Green`)
      // when renaming an unscoped enum declaration with a new namespace.
      if (!Expr->hasQualifier())
        return true;

      if (const auto *ED =
              llvm::dyn_cast_or_null<EnumDecl>(getClosestAncestorDecl(*T))) {
        if (ED->isScoped())
          return true;
        Decl = ED;
      }
      // The current fix would qualify "ns1::ns2::Green" as
      // "ns1::ns2::Color::Green".
      //
      // Get the EndLoc of the replacement by moving 1 character backward (
      // to exclude the last '::').
      //
      //    ns1::ns2::Green;
      //    ^      ^^
      // BeginLoc  |EndLoc of the qualifier
      //           new EndLoc
      EndLoc = Expr->getQualifierLoc().getEndLoc().getLocWithOffset(-1);
      assert(EndLoc.isValid() &&
             "The enum constant should have prefix qualifers.");
    }
    if (isInUSRSet(Decl) &&
        IsValidEditLoc(Context.getSourceManager(), StartLoc)) {
      RenameInfo Info = {StartLoc,
                         EndLoc,
                         Decl,
                         getClosestAncestorDecl(*Expr),
                         Expr->getQualifier(),
                         /*IgnorePrefixQualifers=*/false};
      RenameInfos.push_back(Info);
    }

    return true;
  }

  bool VisitUsingDecl(const UsingDecl *Using) {
    for (const auto *UsingShadow : Using->shadows()) {
      if (isInUSRSet(UsingShadow->getTargetDecl())) {
        UsingDecls.push_back(Using);
        break;
      }
    }
    return true;
  }

  bool VisitNestedNameSpecifierLocations(NestedNameSpecifierLoc NestedLoc) {
    if (!NestedLoc.getNestedNameSpecifier()->getAsType())
      return true;

    if (const auto *TargetDecl =
            getSupportedDeclFromTypeLoc(NestedLoc.getTypeLoc())) {
      if (isInUSRSet(TargetDecl)) {
        RenameInfo Info = {NestedLoc.getBeginLoc(),
                           EndLocationForType(NestedLoc.getTypeLoc()),
                           TargetDecl,
                           getClosestAncestorDecl(NestedLoc),
                           NestedLoc.getNestedNameSpecifier()->getPrefix(),
                           /*IgnorePrefixQualifers=*/false};
        RenameInfos.push_back(Info);
      }
    }
    return true;
  }

  bool VisitTypeLoc(TypeLoc Loc) {
    auto Parents = Context.getParents(Loc);
    TypeLoc ParentTypeLoc;
    if (!Parents.empty()) {
      // Handle cases of nested name specificier locations.
      //
      // The VisitNestedNameSpecifierLoc interface is not impelmented in
      // RecursiveASTVisitor, we have to handle it explicitly.
      if (const auto *NSL = Parents[0].get<NestedNameSpecifierLoc>()) {
        VisitNestedNameSpecifierLocations(*NSL);
        return true;
      }

      if (const auto *TL = Parents[0].get<TypeLoc>())
        ParentTypeLoc = *TL;
    }

    // Handle the outermost TypeLoc which is directly linked to the interesting
    // declaration and don't handle nested name specifier locations.
    if (const auto *TargetDecl = getSupportedDeclFromTypeLoc(Loc)) {
      if (isInUSRSet(TargetDecl)) {
        // Only handle the outermost typeLoc.
        //
        // For a type like "a::Foo", there will be two typeLocs for it.
        // One ElaboratedType, the other is RecordType:
        //
        //   ElaboratedType 0x33b9390 'a::Foo' sugar
        //   `-RecordType 0x338fef0 'class a::Foo'
        //     `-CXXRecord 0x338fe58 'Foo'
        //
        // Skip if this is an inner typeLoc.
        if (!ParentTypeLoc.isNull() &&
            isInUSRSet(getSupportedDeclFromTypeLoc(ParentTypeLoc)))
          return true;

        auto StartLoc = StartLocationForType(Loc);
        auto EndLoc = EndLocationForType(Loc);
        if (IsValidEditLoc(Context.getSourceManager(), StartLoc)) {
          RenameInfo Info = {StartLoc,
                             EndLoc,
                             TargetDecl,
                             getClosestAncestorDecl(Loc),
                             GetNestedNameForType(Loc),
                             /*IgnorePrefixQualifers=*/false};
          RenameInfos.push_back(Info);
        }
        return true;
      }
    }

    // Handle specific template class specialiation cases.
    if (const auto *TemplateSpecType =
            dyn_cast<TemplateSpecializationType>(Loc.getType())) {
      TypeLoc TargetLoc = Loc;
      if (!ParentTypeLoc.isNull()) {
        if (llvm::isa<ElaboratedType>(ParentTypeLoc.getType()))
          TargetLoc = ParentTypeLoc;
      }

      if (isInUSRSet(TemplateSpecType->getTemplateName().getAsTemplateDecl())) {
        TypeLoc TargetLoc = Loc;
        // FIXME: Find a better way to handle this case.
        // For the qualified template class specification type like
        // "ns::Foo<int>" in "ns::Foo<int>& f();", we want the parent typeLoc
        // (ElaboratedType) of the TemplateSpecializationType in order to
        // catch the prefix qualifiers "ns::".
        if (!ParentTypeLoc.isNull() &&
            llvm::isa<ElaboratedType>(ParentTypeLoc.getType()))
          TargetLoc = ParentTypeLoc;

        auto StartLoc = StartLocationForType(TargetLoc);
        auto EndLoc = EndLocationForType(TargetLoc);
        if (IsValidEditLoc(Context.getSourceManager(), StartLoc)) {
          RenameInfo Info = {
              StartLoc,
              EndLoc,
              TemplateSpecType->getTemplateName().getAsTemplateDecl(),
              getClosestAncestorDecl(DynTypedNode::create(TargetLoc)),
              GetNestedNameForType(TargetLoc),
              /*IgnorePrefixQualifers=*/false};
          RenameInfos.push_back(Info);
        }
      }
    }
    return true;
  }

  // Returns a list of RenameInfo.
  const std::vector<RenameInfo> &getRenameInfos() const { return RenameInfos; }

  // Returns a list of using declarations which are needed to update.
  const std::vector<const UsingDecl *> &getUsingDecls() const {
    return UsingDecls;
  }

private:
  // Get the supported declaration from a given typeLoc. If the declaration type
  // is not supported, returns nullptr.
  const NamedDecl *getSupportedDeclFromTypeLoc(TypeLoc Loc) {
    if (const auto* TT = Loc.getType()->getAs<clang::TypedefType>())
      return TT->getDecl();
    if (const auto *RD = Loc.getType()->getAsCXXRecordDecl())
      return RD;
    if (const auto *ED =
            llvm::dyn_cast_or_null<EnumDecl>(Loc.getType()->getAsTagDecl()))
      return ED;
    return nullptr;
  }

  // Get the closest ancester which is a declaration of a given AST node.
  template <typename ASTNodeType>
  const Decl *getClosestAncestorDecl(const ASTNodeType &Node) {
    auto Parents = Context.getParents(Node);
    // FIXME: figure out how to handle it when there are multiple parents.
    if (Parents.size() != 1)
      return nullptr;
    if (ASTNodeKind::getFromNodeKind<Decl>().isBaseOf(Parents[0].getNodeKind()))
      return Parents[0].template get<Decl>();
    return getClosestAncestorDecl(Parents[0]);
  }

  // Get the parent typeLoc of a given typeLoc. If there is no such parent,
  // return nullptr.
  const TypeLoc *getParentTypeLoc(TypeLoc Loc) const {
    auto Parents = Context.getParents(Loc);
    // FIXME: figure out how to handle it when there are multiple parents.
    if (Parents.size() != 1)
      return nullptr;
    return Parents[0].get<TypeLoc>();
  }

  // Check whether the USR of a given Decl is in the USRSet.
  bool isInUSRSet(const Decl *Decl) const {
    auto USR = getUSRForDecl(Decl);
    if (USR.empty())
      return false;
    return llvm::is_contained(USRSet, USR);
  }

  const std::set<std::string> USRSet;
  ASTContext &Context;
  std::vector<RenameInfo> RenameInfos;
  // Record all interested using declarations which contains the using-shadow
  // declarations of the symbol declarations being renamed.
  std::vector<const UsingDecl *> UsingDecls;
};

} // namespace

SymbolOccurrences getOccurrencesOfUSRs(ArrayRef<std::string> USRs,
                                       StringRef PrevName, Decl *Decl) {
  USRLocFindingASTVisitor Visitor(USRs, PrevName, Decl->getASTContext());
  Visitor.TraverseDecl(Decl);
  return Visitor.takeOccurrences();
}

std::vector<tooling::AtomicChange>
createRenameAtomicChanges(llvm::ArrayRef<std::string> USRs,
                          llvm::StringRef NewName, Decl *TranslationUnitDecl) {
  RenameLocFinder Finder(USRs, TranslationUnitDecl->getASTContext());
  Finder.TraverseDecl(TranslationUnitDecl);

  const SourceManager &SM =
      TranslationUnitDecl->getASTContext().getSourceManager();

  std::vector<tooling::AtomicChange> AtomicChanges;
  auto Replace = [&](SourceLocation Start, SourceLocation End,
                     llvm::StringRef Text) {
    tooling::AtomicChange ReplaceChange = tooling::AtomicChange(SM, Start);
    llvm::Error Err = ReplaceChange.replace(
        SM, CharSourceRange::getTokenRange(Start, End), Text);
    if (Err) {
      llvm::errs() << "Failed to add replacement to AtomicChange: "
                   << llvm::toString(std::move(Err)) << "\n";
      return;
    }
    AtomicChanges.push_back(std::move(ReplaceChange));
  };

  for (const auto &RenameInfo : Finder.getRenameInfos()) {
    std::string ReplacedName = NewName.str();
    if (RenameInfo.IgnorePrefixQualifers) {
      // Get the name without prefix qualifiers from NewName.
      size_t LastColonPos = NewName.find_last_of(':');
      if (LastColonPos != std::string::npos)
        ReplacedName = std::string(NewName.substr(LastColonPos + 1));
    } else {
      if (RenameInfo.FromDecl && RenameInfo.Context) {
        if (!llvm::isa<clang::TranslationUnitDecl>(
                RenameInfo.Context->getDeclContext())) {
          ReplacedName = tooling::replaceNestedName(
              RenameInfo.Specifier, RenameInfo.Begin,
              RenameInfo.Context->getDeclContext(), RenameInfo.FromDecl,
              NewName.starts_with("::") ? NewName.str()
                                        : ("::" + NewName).str());
        } else {
          // This fixes the case where type `T` is a parameter inside a function
          // type (e.g. `std::function<void(T)>`) and the DeclContext of `T`
          // becomes the translation unit. As a workaround, we simply use
          // fully-qualified name here for all references whose `DeclContext` is
          // the translation unit and ignore the possible existence of
          // using-decls (in the global scope) that can shorten the replaced
          // name.
          llvm::StringRef ActualName = Lexer::getSourceText(
              CharSourceRange::getTokenRange(
                  SourceRange(RenameInfo.Begin, RenameInfo.End)),
              SM, TranslationUnitDecl->getASTContext().getLangOpts());
          // Add the leading "::" back if the name written in the code contains
          // it.
          if (ActualName.starts_with("::") && !NewName.starts_with("::")) {
            ReplacedName = "::" + NewName.str();
          }
        }
      }
      // If the NewName contains leading "::", add it back.
      if (NewName.starts_with("::") && NewName.substr(2) == ReplacedName)
        ReplacedName = NewName.str();
    }
    Replace(RenameInfo.Begin, RenameInfo.End, ReplacedName);
  }

  // Hanlde using declarations explicitly as "using a::Foo" don't trigger
  // typeLoc for "a::Foo".
  for (const auto *Using : Finder.getUsingDecls())
    Replace(Using->getBeginLoc(), Using->getEndLoc(), "using " + NewName.str());

  return AtomicChanges;
}

} // end namespace tooling
} // end namespace clang
