//===- ASTImporter.h - Importing ASTs from other Contexts -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ASTImporter class which imports AST nodes from one
//  context into another context.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_ASTIMPORTER_H
#define LLVM_CLANG_AST_ASTIMPORTER_H

#include "clang/AST/ASTImportError.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/NestedNameSpecifier.h"
#include "clang/AST/TemplateName.h"
#include "clang/AST/Type.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <utility>

namespace clang {

class ASTContext;
class ASTImporterSharedState;
class Attr;
class CXXBaseSpecifier;
class CXXCtorInitializer;
class Decl;
class DeclContext;
class Expr;
class FileManager;
class NamedDecl;
class Stmt;
class TagDecl;
class TranslationUnitDecl;
class TypeSourceInfo;

  // \brief Returns with a list of declarations started from the canonical decl
  // then followed by subsequent decls in the translation unit.
  // This gives a canonical list for each entry in the redecl chain.
  // `Decl::redecls()` gives a list of decls which always start from the
  // previous decl and the next item is actually the previous item in the order
  // of source locations.  Thus, `Decl::redecls()` gives different lists for
  // the different entries in a given redecl chain.
  llvm::SmallVector<Decl*, 2> getCanonicalForwardRedeclChain(Decl* D);

  /// Imports selected nodes from one AST context into another context,
  /// merging AST nodes where appropriate.
  class ASTImporter {
    friend class ASTNodeImporter;
  public:
    using NonEquivalentDeclSet = llvm::DenseSet<std::pair<Decl *, Decl *>>;
    using ImportedCXXBaseSpecifierMap =
        llvm::DenseMap<const CXXBaseSpecifier *, CXXBaseSpecifier *>;

    enum class ODRHandlingType { Conservative, Liberal };

    // An ImportPath is the list of the AST nodes which we visit during an
    // Import call.
    // If node `A` depends on node `B` then the path contains an `A`->`B` edge.
    // From the call stack of the import functions we can read the very same
    // path.
    //
    // Now imagine the following AST, where the `->` represents dependency in
    // therms of the import.
    // ```
    // A->B->C->D
    //    `->E
    // ```
    // We would like to import A.
    // The import behaves like a DFS, so we will visit the nodes in this order:
    // ABCDE.
    // During the visitation we will have the following ImportPaths:
    // ```
    // A
    // AB
    // ABC
    // ABCD
    // ABC
    // AB
    // ABE
    // AB
    // A
    // ```
    // If during the visit of E there is an error then we set an error for E,
    // then as the call stack shrinks for B, then for A:
    // ```
    // A
    // AB
    // ABC
    // ABCD
    // ABC
    // AB
    // ABE // Error! Set an error to E
    // AB  // Set an error to B
    // A   // Set an error to A
    // ```
    // However, during the import we could import C and D without any error and
    // they are independent from A,B and E.
    // We must not set up an error for C and D.
    // So, at the end of the import we have an entry in `ImportDeclErrors` for
    // A,B,E but not for C,D.
    //
    // Now what happens if there is a cycle in the import path?
    // Let's consider this AST:
    // ```
    // A->B->C->A
    //    `->E
    // ```
    // During the visitation we will have the below ImportPaths and if during
    // the visit of E there is an error then we will set up an error for E,B,A.
    // But what's up with C?
    // ```
    // A
    // AB
    // ABC
    // ABCA
    // ABC
    // AB
    // ABE // Error! Set an error to E
    // AB  // Set an error to B
    // A   // Set an error to A
    // ```
    // This time we know that both B and C are dependent on A.
    // This means we must set up an error for C too.
    // As the call stack reverses back we get to A and we must set up an error
    // to all nodes which depend on A (this includes C).
    // But C is no longer on the import path, it just had been previously.
    // Such situation can happen only if during the visitation we had a cycle.
    // If we didn't have any cycle, then the normal way of passing an Error
    // object through the call stack could handle the situation.
    // This is why we must track cycles during the import process for each
    // visited declaration.
    class ImportPathTy {
    public:
      using VecTy = llvm::SmallVector<Decl *, 32>;

      void push(Decl *D) {
        Nodes.push_back(D);
        ++Aux[D];
      }

      void pop() {
        if (Nodes.empty())
          return;
        --Aux[Nodes.back()];
        Nodes.pop_back();
      }

      /// Returns true if the last element can be found earlier in the path.
      bool hasCycleAtBack() const {
        auto Pos = Aux.find(Nodes.back());
        return Pos != Aux.end() && Pos->second > 1;
      }

      using Cycle = llvm::iterator_range<VecTy::const_reverse_iterator>;
      Cycle getCycleAtBack() const {
        assert(Nodes.size() >= 2);
        return Cycle(Nodes.rbegin(),
                     std::find(Nodes.rbegin() + 1, Nodes.rend(), Nodes.back()) +
                         1);
      }

      /// Returns the copy of the cycle.
      VecTy copyCycleAtBack() const {
        auto R = getCycleAtBack();
        return VecTy(R.begin(), R.end());
      }

    private:
      // All nodes of the path.
      VecTy Nodes;
      // Auxiliary container to be able to answer "Do we have a cycle ending
      // at last element?" as fast as possible.
      // We count each Decl's occurrence over the path.
      llvm::SmallDenseMap<Decl *, int, 32> Aux;
    };

  private:
    std::shared_ptr<ASTImporterSharedState> SharedState = nullptr;

    /// The path which we go through during the import of a given AST node.
    ImportPathTy ImportPath;
    /// Sometimes we have to save some part of an import path, so later we can
    /// set up properties to the saved nodes.
    /// We may have several of these import paths associated to one Decl.
    using SavedImportPathsForOneDecl =
        llvm::SmallVector<ImportPathTy::VecTy, 32>;
    using SavedImportPathsTy =
        llvm::SmallDenseMap<Decl *, SavedImportPathsForOneDecl, 32>;
    SavedImportPathsTy SavedImportPaths;

    /// The contexts we're importing to and from.
    ASTContext &ToContext, &FromContext;

    /// The file managers we're importing to and from.
    FileManager &ToFileManager, &FromFileManager;

    /// Whether to perform a minimal import.
    bool Minimal;

    ODRHandlingType ODRHandling;

    /// Whether the last diagnostic came from the "from" context.
    bool LastDiagFromFrom = false;

    /// Mapping from the already-imported types in the "from" context
    /// to the corresponding types in the "to" context.
    llvm::DenseMap<const Type *, const Type *> ImportedTypes;

    /// Mapping from the already-imported declarations in the "from"
    /// context to the corresponding declarations in the "to" context.
    llvm::DenseMap<Decl *, Decl *> ImportedDecls;

    /// Mapping from the already-imported declarations in the "from"
    /// context to the error status of the import of that declaration.
    /// This map contains only the declarations that were not correctly
    /// imported. The same declaration may or may not be included in
    /// ImportedDecls. This map is updated continuously during imports and never
    /// cleared (like ImportedDecls).
    llvm::DenseMap<Decl *, ASTImportError> ImportDeclErrors;

    /// Mapping from the already-imported declarations in the "to"
    /// context to the corresponding declarations in the "from" context.
    llvm::DenseMap<Decl *, Decl *> ImportedFromDecls;

    /// Mapping from the already-imported statements in the "from"
    /// context to the corresponding statements in the "to" context.
    llvm::DenseMap<Stmt *, Stmt *> ImportedStmts;

    /// Mapping from the already-imported FileIDs in the "from" source
    /// manager to the corresponding FileIDs in the "to" source manager.
    llvm::DenseMap<FileID, FileID> ImportedFileIDs;

    /// Mapping from the already-imported CXXBasesSpecifier in
    ///  the "from" source manager to the corresponding CXXBasesSpecifier
    ///  in the "to" source manager.
    ImportedCXXBaseSpecifierMap ImportedCXXBaseSpecifiers;

    /// Declaration (from, to) pairs that are known not to be equivalent
    /// (which we have already complained about).
    NonEquivalentDeclSet NonEquivalentDecls;

    using FoundDeclsTy = SmallVector<NamedDecl *, 2>;
    FoundDeclsTy findDeclsInToCtx(DeclContext *DC, DeclarationName Name);

    void AddToLookupTable(Decl *ToD);
    llvm::Error ImportAttrs(Decl *ToD, Decl *FromD);

  protected:
    /// Can be overwritten by subclasses to implement their own import logic.
    /// The overwritten method should call this method if it didn't import the
    /// decl on its own.
    virtual Expected<Decl *> ImportImpl(Decl *From);

    /// Used only in unittests to verify the behaviour of the error handling.
    virtual bool returnWithErrorInTest() { return false; };

  public:

    /// \param ToContext The context we'll be importing into.
    ///
    /// \param ToFileManager The file manager we'll be importing into.
    ///
    /// \param FromContext The context we'll be importing from.
    ///
    /// \param FromFileManager The file manager we'll be importing into.
    ///
    /// \param MinimalImport If true, the importer will attempt to import
    /// as little as it can, e.g., by importing declarations as forward
    /// declarations that can be completed at a later point.
    ///
    /// \param SharedState The importer specific lookup table which may be
    /// shared amongst several ASTImporter objects.
    /// If not set then the original C/C++ lookup is used.
    ASTImporter(ASTContext &ToContext, FileManager &ToFileManager,
                ASTContext &FromContext, FileManager &FromFileManager,
                bool MinimalImport,
                std::shared_ptr<ASTImporterSharedState> SharedState = nullptr);

    virtual ~ASTImporter();

    /// Whether the importer will perform a minimal import, creating
    /// to-be-completed forward declarations when possible.
    bool isMinimalImport() const { return Minimal; }

    void setODRHandling(ODRHandlingType T) { ODRHandling = T; }

    /// \brief Import the given object, returns the result.
    ///
    /// \param To Import the object into this variable.
    /// \param From Object to import.
    /// \return Error information (success or error).
    template <typename ImportT>
    [[nodiscard]] llvm::Error importInto(ImportT &To, const ImportT &From) {
      auto ToOrErr = Import(From);
      if (ToOrErr)
        To = *ToOrErr;
      return ToOrErr.takeError();
    }

    /// Import cleanup objects owned by ExprWithCleanup.
    llvm::Expected<ExprWithCleanups::CleanupObject>
    Import(ExprWithCleanups::CleanupObject From);

    /// Import the given type from the "from" context into the "to"
    /// context.
    ///
    /// \returns The equivalent type in the "to" context, or the import error.
    llvm::Expected<const Type *> Import(const Type *FromT);

    /// Import the given qualified type from the "from" context into the "to"
    /// context. A null type is imported as a null type (no error).
    ///
    /// \returns The equivalent type in the "to" context, or the import error.
    llvm::Expected<QualType> Import(QualType FromT);

    /// Import the given type source information from the
    /// "from" context into the "to" context.
    ///
    /// \returns The equivalent type source information in the "to"
    /// context, or the import error.
    llvm::Expected<TypeSourceInfo *> Import(TypeSourceInfo *FromTSI);

    /// Import the given attribute from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent attribute in the "to" context, or the import
    /// error.
    llvm::Expected<Attr *> Import(const Attr *FromAttr);

    /// Import the given declaration from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent declaration in the "to" context, or the import
    /// error.
    llvm::Expected<Decl *> Import(Decl *FromD);
    llvm::Expected<const Decl *> Import(const Decl *FromD) {
      return Import(const_cast<Decl *>(FromD));
    }

    llvm::Expected<InheritedConstructor>
    Import(const InheritedConstructor &From);

    /// Return the copy of the given declaration in the "to" context if
    /// it has already been imported from the "from" context.  Otherwise return
    /// nullptr.
    Decl *GetAlreadyImportedOrNull(const Decl *FromD) const;

    /// Return the translation unit from where the declaration was
    /// imported. If it does not exist nullptr is returned.
    TranslationUnitDecl *GetFromTU(Decl *ToD);

    /// Return the declaration in the "from" context from which the declaration
    /// in the "to" context was imported. If it was not imported or of the wrong
    /// type a null value is returned.
    template <typename DeclT>
    std::optional<DeclT *> getImportedFromDecl(const DeclT *ToD) const {
      auto FromI = ImportedFromDecls.find(ToD);
      if (FromI == ImportedFromDecls.end())
        return {};
      auto *FromD = dyn_cast<DeclT>(FromI->second);
      if (!FromD)
        return {};
      return FromD;
    }

    /// Import the given declaration context from the "from"
    /// AST context into the "to" AST context.
    ///
    /// \returns the equivalent declaration context in the "to"
    /// context, or error value.
    llvm::Expected<DeclContext *> ImportContext(DeclContext *FromDC);

    /// Import the given expression from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent expression in the "to" context, or the import
    /// error.
    llvm::Expected<Expr *> Import(Expr *FromE);

    /// Import the given statement from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent statement in the "to" context, or the import
    /// error.
    llvm::Expected<Stmt *> Import(Stmt *FromS);

    /// Import the given nested-name-specifier from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent nested-name-specifier in the "to"
    /// context, or the import error.
    llvm::Expected<NestedNameSpecifier *> Import(NestedNameSpecifier *FromNNS);

    /// Import the given nested-name-specifier-loc from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent nested-name-specifier-loc in the "to"
    /// context, or the import error.
    llvm::Expected<NestedNameSpecifierLoc>
    Import(NestedNameSpecifierLoc FromNNS);

    /// Import the given template name from the "from" context into the
    /// "to" context, or the import error.
    llvm::Expected<TemplateName> Import(TemplateName From);

    /// Import the given source location from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent source location in the "to" context, or the
    /// import error.
    llvm::Expected<SourceLocation> Import(SourceLocation FromLoc);

    /// Import the given source range from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent source range in the "to" context, or the import
    /// error.
    llvm::Expected<SourceRange> Import(SourceRange FromRange);

    /// Import the given declaration name from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent declaration name in the "to" context, or the
    /// import error.
    llvm::Expected<DeclarationName> Import(DeclarationName FromName);

    /// Import the given identifier from the "from" context
    /// into the "to" context.
    ///
    /// \returns The equivalent identifier in the "to" context. Note: It
    /// returns nullptr only if the FromId was nullptr.
    IdentifierInfo *Import(const IdentifierInfo *FromId);

    /// Import the given Objective-C selector from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent selector in the "to" context, or the import
    /// error.
    llvm::Expected<Selector> Import(Selector FromSel);

    /// Import the given file ID from the "from" context into the
    /// "to" context.
    ///
    /// \returns The equivalent file ID in the source manager of the "to"
    /// context, or the import error.
    llvm::Expected<FileID> Import(FileID, bool IsBuiltin = false);

    /// Import the given C++ constructor initializer from the "from"
    /// context into the "to" context.
    ///
    /// \returns The equivalent initializer in the "to" context, or the import
    /// error.
    llvm::Expected<CXXCtorInitializer *> Import(CXXCtorInitializer *FromInit);

    /// Import the given CXXBaseSpecifier from the "from" context into
    /// the "to" context.
    ///
    /// \returns The equivalent CXXBaseSpecifier in the source manager of the
    /// "to" context, or the import error.
    llvm::Expected<CXXBaseSpecifier *> Import(const CXXBaseSpecifier *FromSpec);

    /// Import the given APValue from the "from" context into
    /// the "to" context.
    ///
    /// \return the equivalent APValue in the "to" context or the import
    /// error.
    llvm::Expected<APValue> Import(const APValue &FromValue);

    /// Import the definition of the given declaration, including all of
    /// the declarations it contains.
    [[nodiscard]] llvm::Error ImportDefinition(Decl *From);

    /// Cope with a name conflict when importing a declaration into the
    /// given context.
    ///
    /// This routine is invoked whenever there is a name conflict while
    /// importing a declaration. The returned name will become the name of the
    /// imported declaration. By default, the returned name is the same as the
    /// original name, leaving the conflict unresolve such that name lookup
    /// for this name is likely to find an ambiguity later.
    ///
    /// Subclasses may override this routine to resolve the conflict, e.g., by
    /// renaming the declaration being imported.
    ///
    /// \param Name the name of the declaration being imported, which conflicts
    /// with other declarations.
    ///
    /// \param DC the declaration context (in the "to" AST context) in which
    /// the name is being imported.
    ///
    /// \param IDNS the identifier namespace in which the name will be found.
    ///
    /// \param Decls the set of declarations with the same name as the
    /// declaration being imported.
    ///
    /// \param NumDecls the number of conflicting declarations in \p Decls.
    ///
    /// \returns the name that the newly-imported declaration should have. Or
    /// an error if we can't handle the name conflict.
    virtual Expected<DeclarationName>
    HandleNameConflict(DeclarationName Name, DeclContext *DC, unsigned IDNS,
                       NamedDecl **Decls, unsigned NumDecls);

    /// Retrieve the context that AST nodes are being imported into.
    ASTContext &getToContext() const { return ToContext; }

    /// Retrieve the context that AST nodes are being imported from.
    ASTContext &getFromContext() const { return FromContext; }

    /// Retrieve the file manager that AST nodes are being imported into.
    FileManager &getToFileManager() const { return ToFileManager; }

    /// Retrieve the file manager that AST nodes are being imported from.
    FileManager &getFromFileManager() const { return FromFileManager; }

    /// Report a diagnostic in the "to" context.
    DiagnosticBuilder ToDiag(SourceLocation Loc, unsigned DiagID);

    /// Report a diagnostic in the "from" context.
    DiagnosticBuilder FromDiag(SourceLocation Loc, unsigned DiagID);

    /// Return the set of declarations that we know are not equivalent.
    NonEquivalentDeclSet &getNonEquivalentDecls() { return NonEquivalentDecls; }

    /// Called for ObjCInterfaceDecl, ObjCProtocolDecl, and TagDecl.
    /// Mark the Decl as complete, filling it in as much as possible.
    ///
    /// \param D A declaration in the "to" context.
    virtual void CompleteDecl(Decl* D);

    /// Subclasses can override this function to observe all of the \c From ->
    /// \c To declaration mappings as they are imported.
    virtual void Imported(Decl *From, Decl *To) {}

    void RegisterImportedDecl(Decl *FromD, Decl *ToD);

    /// Store and assign the imported declaration to its counterpart.
    /// It may happen that several decls from the 'from' context are mapped to
    /// the same decl in the 'to' context.
    Decl *MapImported(Decl *From, Decl *To);

    /// Called by StructuralEquivalenceContext.  If a RecordDecl is
    /// being compared to another RecordDecl as part of import, completing the
    /// other RecordDecl may trigger importation of the first RecordDecl. This
    /// happens especially for anonymous structs.  If the original of the second
    /// RecordDecl can be found, we can complete it without the need for
    /// importation, eliminating this loop.
    virtual Decl *GetOriginalDecl(Decl *To) { return nullptr; }

    /// Return if import of the given declaration has failed and if yes
    /// the kind of the problem. This gives the first error encountered with
    /// the node.
    std::optional<ASTImportError> getImportDeclErrorIfAny(Decl *FromD) const;

    /// Mark (newly) imported declaration with error.
    void setImportDeclError(Decl *From, ASTImportError Error);

    /// Determine whether the given types are structurally
    /// equivalent.
    bool IsStructurallyEquivalent(QualType From, QualType To,
                                  bool Complain = true);

    /// Determine the index of a field in its parent record.
    /// F should be a field (or indirect field) declaration.
    /// \returns The index of the field in its parent context (starting from 0).
    /// On error `std::nullopt` is returned (parent context is non-record).
    static std::optional<unsigned> getFieldIndex(Decl *F);
  };

} // namespace clang

#endif // LLVM_CLANG_AST_ASTIMPORTER_H
