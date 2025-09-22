//===----- SemaObjC.h ------ Semantic Analysis for Objective-C ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for Objective-C.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAOBJC_H
#define LLVM_CLANG_SEMA_SEMAOBJC_H

#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprObjC.h"
#include "clang/AST/NSAPI.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Type.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ObjCMethodList.h"
#include "clang/Sema/Ownership.h"
#include "clang/Sema/Redeclaration.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace clang {

enum class CheckedConversionKind;
class ParsedAttr;
struct SkipBodyInfo;

class SemaObjC : public SemaBase {
public:
  SemaObjC(Sema &S);

  ExprResult CheckObjCForCollectionOperand(SourceLocation forLoc,
                                           Expr *collection);
  StmtResult ActOnObjCForCollectionStmt(SourceLocation ForColLoc, Stmt *First,
                                        Expr *collection,
                                        SourceLocation RParenLoc);
  /// FinishObjCForCollectionStmt - Attach the body to a objective-C foreach
  /// statement.
  StmtResult FinishObjCForCollectionStmt(Stmt *ForCollection, Stmt *Body);

  StmtResult ActOnObjCAtCatchStmt(SourceLocation AtLoc, SourceLocation RParen,
                                  Decl *Parm, Stmt *Body);

  StmtResult ActOnObjCAtFinallyStmt(SourceLocation AtLoc, Stmt *Body);

  StmtResult ActOnObjCAtTryStmt(SourceLocation AtLoc, Stmt *Try,
                                MultiStmtArg Catch, Stmt *Finally);

  StmtResult BuildObjCAtThrowStmt(SourceLocation AtLoc, Expr *Throw);
  StmtResult ActOnObjCAtThrowStmt(SourceLocation AtLoc, Expr *Throw,
                                  Scope *CurScope);
  ExprResult ActOnObjCAtSynchronizedOperand(SourceLocation atLoc,
                                            Expr *operand);
  StmtResult ActOnObjCAtSynchronizedStmt(SourceLocation AtLoc, Expr *SynchExpr,
                                         Stmt *SynchBody);

  StmtResult ActOnObjCAutoreleasePoolStmt(SourceLocation AtLoc, Stmt *Body);

  /// Build a an Objective-C protocol-qualified 'id' type where no
  /// base type was specified.
  TypeResult actOnObjCProtocolQualifierType(
      SourceLocation lAngleLoc, ArrayRef<Decl *> protocols,
      ArrayRef<SourceLocation> protocolLocs, SourceLocation rAngleLoc);

  /// Build a specialized and/or protocol-qualified Objective-C type.
  TypeResult actOnObjCTypeArgsAndProtocolQualifiers(
      Scope *S, SourceLocation Loc, ParsedType BaseType,
      SourceLocation TypeArgsLAngleLoc, ArrayRef<ParsedType> TypeArgs,
      SourceLocation TypeArgsRAngleLoc, SourceLocation ProtocolLAngleLoc,
      ArrayRef<Decl *> Protocols, ArrayRef<SourceLocation> ProtocolLocs,
      SourceLocation ProtocolRAngleLoc);

  /// Build an Objective-C type parameter type.
  QualType BuildObjCTypeParamType(const ObjCTypeParamDecl *Decl,
                                  SourceLocation ProtocolLAngleLoc,
                                  ArrayRef<ObjCProtocolDecl *> Protocols,
                                  ArrayRef<SourceLocation> ProtocolLocs,
                                  SourceLocation ProtocolRAngleLoc,
                                  bool FailOnError = false);

  /// Build an Objective-C object pointer type.
  QualType BuildObjCObjectType(
      QualType BaseType, SourceLocation Loc, SourceLocation TypeArgsLAngleLoc,
      ArrayRef<TypeSourceInfo *> TypeArgs, SourceLocation TypeArgsRAngleLoc,
      SourceLocation ProtocolLAngleLoc, ArrayRef<ObjCProtocolDecl *> Protocols,
      ArrayRef<SourceLocation> ProtocolLocs, SourceLocation ProtocolRAngleLoc,
      bool FailOnError, bool Rebuilding);

  /// The parser has parsed the context-sensitive type 'instancetype'
  /// in an Objective-C message declaration. Return the appropriate type.
  ParsedType ActOnObjCInstanceType(SourceLocation Loc);

  /// checkRetainCycles - Check whether an Objective-C message send
  /// might create an obvious retain cycle.
  void checkRetainCycles(ObjCMessageExpr *msg);
  void checkRetainCycles(Expr *receiver, Expr *argument);
  void checkRetainCycles(VarDecl *Var, Expr *Init);

  bool CheckObjCString(Expr *Arg);
  bool CheckObjCMethodCall(ObjCMethodDecl *Method, SourceLocation loc,
                           ArrayRef<const Expr *> Args);
  /// Check whether receiver is mutable ObjC container which
  /// attempts to add itself into the container
  void CheckObjCCircularContainer(ObjCMessageExpr *Message);

  void ActOnObjCContainerStartDefinition(ObjCContainerDecl *IDecl);
  void ActOnObjCContainerFinishDefinition();

  /// Invoked when we must temporarily exit the objective-c container
  /// scope for parsing/looking-up C constructs.
  ///
  /// Must be followed by a call to \see ActOnObjCReenterContainerContext
  void ActOnObjCTemporaryExitContainerContext(ObjCContainerDecl *ObjCCtx);
  void ActOnObjCReenterContainerContext(ObjCContainerDecl *ObjCCtx);

  const DeclContext *getCurObjCLexicalContext() const;

  ObjCProtocolDecl *LookupProtocol(
      IdentifierInfo *II, SourceLocation IdLoc,
      RedeclarationKind Redecl = RedeclarationKind::NotForRedeclaration);

  bool isObjCWritebackConversion(QualType FromType, QualType ToType,
                                 QualType &ConvertedType);

  enum ObjCSubscriptKind { OS_Array, OS_Dictionary, OS_Error };
  ObjCSubscriptKind CheckSubscriptingKind(Expr *FromE);

  /// AddCFAuditedAttribute - Check whether we're currently within
  /// '\#pragma clang arc_cf_code_audited' and, if so, consider adding
  /// the appropriate attribute.
  void AddCFAuditedAttribute(Decl *D);

  /// The struct behind the CFErrorRef pointer.
  RecordDecl *CFError = nullptr;
  bool isCFError(RecordDecl *D);

  IdentifierInfo *getNSErrorIdent();

  bool GetFormatNSStringIdx(const FormatAttr *Format, unsigned &Idx);

  /// Diagnose use of %s directive in an NSString which is being passed
  /// as formatting string to formatting method.
  void DiagnoseCStringFormatDirectiveInCFAPI(const NamedDecl *FDecl,
                                             Expr **Args, unsigned NumArgs);

  bool isSignedCharBool(QualType Ty);

  void adornBoolConversionDiagWithTernaryFixit(
      Expr *SourceExpr, const Sema::SemaDiagnosticBuilder &Builder);

  /// Check an Objective-C dictionary literal being converted to the given
  /// target type.
  void checkDictionaryLiteral(QualType TargetType,
                              ObjCDictionaryLiteral *DictionaryLiteral);

  /// Check an Objective-C array literal being converted to the given
  /// target type.
  void checkArrayLiteral(QualType TargetType, ObjCArrayLiteral *ArrayLiteral);

private:
  IdentifierInfo *Ident_NSError = nullptr;

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name ObjC Declarations
  /// Implementations are in SemaDeclObjC.cpp
  ///@{

public:
  enum ObjCSpecialMethodKind {
    OSMK_None,
    OSMK_Alloc,
    OSMK_New,
    OSMK_Copy,
    OSMK_RetainingInit,
    OSMK_NonRetainingInit
  };

  /// Method selectors used in a \@selector expression. Used for implementation
  /// of -Wselector.
  llvm::MapVector<Selector, SourceLocation> ReferencedSelectors;

  class GlobalMethodPool {
  public:
    using Lists = std::pair<ObjCMethodList, ObjCMethodList>;
    using iterator = llvm::DenseMap<Selector, Lists>::iterator;
    iterator begin() { return Methods.begin(); }
    iterator end() { return Methods.end(); }
    iterator find(Selector Sel) { return Methods.find(Sel); }
    std::pair<iterator, bool> insert(std::pair<Selector, Lists> &&Val) {
      return Methods.insert(Val);
    }
    int count(Selector Sel) const { return Methods.count(Sel); }
    bool empty() const { return Methods.empty(); }

  private:
    llvm::DenseMap<Selector, Lists> Methods;
  };

  /// Method Pool - allows efficient lookup when typechecking messages to "id".
  /// We need to maintain a list, since selectors can have differing signatures
  /// across classes. In Cocoa, this happens to be extremely uncommon (only 1%
  /// of selectors are "overloaded").
  /// At the head of the list it is recorded whether there were 0, 1, or >= 2
  /// methods inside categories with a particular selector.
  GlobalMethodPool MethodPool;

  typedef llvm::SmallPtrSet<Selector, 8> SelectorSet;

  enum MethodMatchStrategy { MMS_loose, MMS_strict };

  enum ObjCContainerKind {
    OCK_None = -1,
    OCK_Interface = 0,
    OCK_Protocol,
    OCK_Category,
    OCK_ClassExtension,
    OCK_Implementation,
    OCK_CategoryImplementation
  };
  ObjCContainerKind getObjCContainerKind() const;

  DeclResult actOnObjCTypeParam(Scope *S, ObjCTypeParamVariance variance,
                                SourceLocation varianceLoc, unsigned index,
                                IdentifierInfo *paramName,
                                SourceLocation paramLoc,
                                SourceLocation colonLoc, ParsedType typeBound);

  ObjCTypeParamList *actOnObjCTypeParamList(Scope *S, SourceLocation lAngleLoc,
                                            ArrayRef<Decl *> typeParams,
                                            SourceLocation rAngleLoc);
  void popObjCTypeParamList(Scope *S, ObjCTypeParamList *typeParamList);

  ObjCInterfaceDecl *ActOnStartClassInterface(
      Scope *S, SourceLocation AtInterfaceLoc, IdentifierInfo *ClassName,
      SourceLocation ClassLoc, ObjCTypeParamList *typeParamList,
      IdentifierInfo *SuperName, SourceLocation SuperLoc,
      ArrayRef<ParsedType> SuperTypeArgs, SourceRange SuperTypeArgsRange,
      Decl *const *ProtoRefs, unsigned NumProtoRefs,
      const SourceLocation *ProtoLocs, SourceLocation EndProtoLoc,
      const ParsedAttributesView &AttrList, SkipBodyInfo *SkipBody);

  void ActOnSuperClassOfClassInterface(
      Scope *S, SourceLocation AtInterfaceLoc, ObjCInterfaceDecl *IDecl,
      IdentifierInfo *ClassName, SourceLocation ClassLoc,
      IdentifierInfo *SuperName, SourceLocation SuperLoc,
      ArrayRef<ParsedType> SuperTypeArgs, SourceRange SuperTypeArgsRange);

  void ActOnTypedefedProtocols(SmallVectorImpl<Decl *> &ProtocolRefs,
                               SmallVectorImpl<SourceLocation> &ProtocolLocs,
                               IdentifierInfo *SuperName,
                               SourceLocation SuperLoc);

  Decl *ActOnCompatibilityAlias(SourceLocation AtCompatibilityAliasLoc,
                                IdentifierInfo *AliasName,
                                SourceLocation AliasLocation,
                                IdentifierInfo *ClassName,
                                SourceLocation ClassLocation);

  bool CheckForwardProtocolDeclarationForCircularDependency(
      IdentifierInfo *PName, SourceLocation &PLoc, SourceLocation PrevLoc,
      const ObjCList<ObjCProtocolDecl> &PList);

  ObjCProtocolDecl *ActOnStartProtocolInterface(
      SourceLocation AtProtoInterfaceLoc, IdentifierInfo *ProtocolName,
      SourceLocation ProtocolLoc, Decl *const *ProtoRefNames,
      unsigned NumProtoRefs, const SourceLocation *ProtoLocs,
      SourceLocation EndProtoLoc, const ParsedAttributesView &AttrList,
      SkipBodyInfo *SkipBody);

  ObjCCategoryDecl *ActOnStartCategoryInterface(
      SourceLocation AtInterfaceLoc, const IdentifierInfo *ClassName,
      SourceLocation ClassLoc, ObjCTypeParamList *typeParamList,
      const IdentifierInfo *CategoryName, SourceLocation CategoryLoc,
      Decl *const *ProtoRefs, unsigned NumProtoRefs,
      const SourceLocation *ProtoLocs, SourceLocation EndProtoLoc,
      const ParsedAttributesView &AttrList);

  ObjCImplementationDecl *ActOnStartClassImplementation(
      SourceLocation AtClassImplLoc, const IdentifierInfo *ClassName,
      SourceLocation ClassLoc, const IdentifierInfo *SuperClassname,
      SourceLocation SuperClassLoc, const ParsedAttributesView &AttrList);

  ObjCCategoryImplDecl *ActOnStartCategoryImplementation(
      SourceLocation AtCatImplLoc, const IdentifierInfo *ClassName,
      SourceLocation ClassLoc, const IdentifierInfo *CatName,
      SourceLocation CatLoc, const ParsedAttributesView &AttrList);

  using DeclGroupPtrTy = OpaquePtr<DeclGroupRef>;

  DeclGroupPtrTy ActOnFinishObjCImplementation(Decl *ObjCImpDecl,
                                               ArrayRef<Decl *> Decls);

  DeclGroupPtrTy
  ActOnForwardProtocolDeclaration(SourceLocation AtProtoclLoc,
                                  ArrayRef<IdentifierLocPair> IdentList,
                                  const ParsedAttributesView &attrList);

  void FindProtocolDeclaration(bool WarnOnDeclarations, bool ForObjCContainer,
                               ArrayRef<IdentifierLocPair> ProtocolId,
                               SmallVectorImpl<Decl *> &Protocols);

  void DiagnoseTypeArgsAndProtocols(IdentifierInfo *ProtocolId,
                                    SourceLocation ProtocolLoc,
                                    IdentifierInfo *TypeArgId,
                                    SourceLocation TypeArgLoc,
                                    bool SelectProtocolFirst = false);

  /// Given a list of identifiers (and their locations), resolve the
  /// names to either Objective-C protocol qualifiers or type
  /// arguments, as appropriate.
  void actOnObjCTypeArgsOrProtocolQualifiers(
      Scope *S, ParsedType baseType, SourceLocation lAngleLoc,
      ArrayRef<IdentifierInfo *> identifiers,
      ArrayRef<SourceLocation> identifierLocs, SourceLocation rAngleLoc,
      SourceLocation &typeArgsLAngleLoc, SmallVectorImpl<ParsedType> &typeArgs,
      SourceLocation &typeArgsRAngleLoc, SourceLocation &protocolLAngleLoc,
      SmallVectorImpl<Decl *> &protocols, SourceLocation &protocolRAngleLoc,
      bool warnOnIncompleteProtocols);

  void DiagnoseClassExtensionDupMethods(ObjCCategoryDecl *CAT,
                                        ObjCInterfaceDecl *ID);

  Decl *ActOnAtEnd(Scope *S, SourceRange AtEnd,
                   ArrayRef<Decl *> allMethods = std::nullopt,
                   ArrayRef<DeclGroupPtrTy> allTUVars = std::nullopt);

  struct ObjCArgInfo {
    IdentifierInfo *Name;
    SourceLocation NameLoc;
    // The Type is null if no type was specified, and the DeclSpec is invalid
    // in this case.
    ParsedType Type;
    ObjCDeclSpec DeclSpec;

    /// ArgAttrs - Attribute list for this argument.
    ParsedAttributesView ArgAttrs;
  };

  Decl *ActOnMethodDeclaration(
      Scope *S,
      SourceLocation BeginLoc, // location of the + or -.
      SourceLocation EndLoc,   // location of the ; or {.
      tok::TokenKind MethodType, ObjCDeclSpec &ReturnQT, ParsedType ReturnType,
      ArrayRef<SourceLocation> SelectorLocs, Selector Sel,
      // optional arguments. The number of types/arguments is obtained
      // from the Sel.getNumArgs().
      ObjCArgInfo *ArgInfo, DeclaratorChunk::ParamInfo *CParamInfo,
      unsigned CNumArgs, // c-style args
      const ParsedAttributesView &AttrList, tok::ObjCKeywordKind MethodImplKind,
      bool isVariadic, bool MethodDefinition);

  bool CheckARCMethodDecl(ObjCMethodDecl *method);

  bool checkInitMethod(ObjCMethodDecl *method, QualType receiverTypeIfCall);

  /// Check whether the given new method is a valid override of the
  /// given overridden method, and set any properties that should be inherited.
  void CheckObjCMethodOverride(ObjCMethodDecl *NewMethod,
                               const ObjCMethodDecl *Overridden);

  /// Describes the compatibility of a result type with its method.
  enum ResultTypeCompatibilityKind {
    RTC_Compatible,
    RTC_Incompatible,
    RTC_Unknown
  };

  void CheckObjCMethodDirectOverrides(ObjCMethodDecl *method,
                                      ObjCMethodDecl *overridden);

  void CheckObjCMethodOverrides(ObjCMethodDecl *ObjCMethod,
                                ObjCInterfaceDecl *CurrentClass,
                                ResultTypeCompatibilityKind RTC);

  /// AddAnyMethodToGlobalPool - Add any method, instance or factory to global
  /// pool.
  void AddAnyMethodToGlobalPool(Decl *D);

  void ActOnStartOfObjCMethodDef(Scope *S, Decl *D);
  bool isObjCMethodDecl(Decl *D) { return isa_and_nonnull<ObjCMethodDecl>(D); }

  /// CheckImplementationIvars - This routine checks if the instance variables
  /// listed in the implelementation match those listed in the interface.
  void CheckImplementationIvars(ObjCImplementationDecl *ImpDecl,
                                ObjCIvarDecl **Fields, unsigned nIvars,
                                SourceLocation Loc);

  void WarnConflictingTypedMethods(ObjCMethodDecl *Method,
                                   ObjCMethodDecl *MethodDecl,
                                   bool IsProtocolMethodDecl);

  void CheckConflictingOverridingMethod(ObjCMethodDecl *Method,
                                        ObjCMethodDecl *Overridden,
                                        bool IsProtocolMethodDecl);

  /// WarnExactTypedMethods - This routine issues a warning if method
  /// implementation declaration matches exactly that of its declaration.
  void WarnExactTypedMethods(ObjCMethodDecl *Method, ObjCMethodDecl *MethodDecl,
                             bool IsProtocolMethodDecl);

  /// MatchAllMethodDeclarations - Check methods declaraed in interface or
  /// or protocol against those declared in their implementations.
  void MatchAllMethodDeclarations(
      const SelectorSet &InsMap, const SelectorSet &ClsMap,
      SelectorSet &InsMapSeen, SelectorSet &ClsMapSeen, ObjCImplDecl *IMPDecl,
      ObjCContainerDecl *IDecl, bool &IncompleteImpl, bool ImmediateClass,
      bool WarnCategoryMethodImpl = false);

  /// CheckCategoryVsClassMethodMatches - Checks that methods implemented in
  /// category matches with those implemented in its primary class and
  /// warns each time an exact match is found.
  void CheckCategoryVsClassMethodMatches(ObjCCategoryImplDecl *CatIMP);

  /// ImplMethodsVsClassMethods - This is main routine to warn if any method
  /// remains unimplemented in the class or category \@implementation.
  void ImplMethodsVsClassMethods(Scope *S, ObjCImplDecl *IMPDecl,
                                 ObjCContainerDecl *IDecl,
                                 bool IncompleteImpl = false);

  DeclGroupPtrTy ActOnForwardClassDeclaration(
      SourceLocation Loc, IdentifierInfo **IdentList, SourceLocation *IdentLocs,
      ArrayRef<ObjCTypeParamList *> TypeParamLists, unsigned NumElts);

  /// MatchTwoMethodDeclarations - Checks if two methods' type match and returns
  /// true, or false, accordingly.
  bool MatchTwoMethodDeclarations(const ObjCMethodDecl *Method,
                                  const ObjCMethodDecl *PrevMethod,
                                  MethodMatchStrategy strategy = MMS_strict);

  /// Add the given method to the list of globally-known methods.
  void addMethodToGlobalList(ObjCMethodList *List, ObjCMethodDecl *Method);

  void ReadMethodPool(Selector Sel);
  void updateOutOfDateSelector(Selector Sel);

  /// - Returns instance or factory methods in global method pool for
  /// given selector. It checks the desired kind first, if none is found, and
  /// parameter checkTheOther is set, it then checks the other kind. If no such
  /// method or only one method is found, function returns false; otherwise, it
  /// returns true.
  bool
  CollectMultipleMethodsInGlobalPool(Selector Sel,
                                     SmallVectorImpl<ObjCMethodDecl *> &Methods,
                                     bool InstanceFirst, bool CheckTheOther,
                                     const ObjCObjectType *TypeBound = nullptr);

  bool
  AreMultipleMethodsInGlobalPool(Selector Sel, ObjCMethodDecl *BestMethod,
                                 SourceRange R, bool receiverIdOrClass,
                                 SmallVectorImpl<ObjCMethodDecl *> &Methods);

  void
  DiagnoseMultipleMethodInGlobalPool(SmallVectorImpl<ObjCMethodDecl *> &Methods,
                                     Selector Sel, SourceRange R,
                                     bool receiverIdOrClass);

  const ObjCMethodDecl *
  SelectorsForTypoCorrection(Selector Sel, QualType ObjectType = QualType());
  /// LookupImplementedMethodInGlobalPool - Returns the method which has an
  /// implementation.
  ObjCMethodDecl *LookupImplementedMethodInGlobalPool(Selector Sel);

  void DiagnoseDuplicateIvars(ObjCInterfaceDecl *ID, ObjCInterfaceDecl *SID);

  /// Checks that the Objective-C declaration is declared in the global scope.
  /// Emits an error and marks the declaration as invalid if it's not declared
  /// in the global scope.
  bool CheckObjCDeclScope(Decl *D);

  void ActOnDefs(Scope *S, Decl *TagD, SourceLocation DeclStart,
                 const IdentifierInfo *ClassName,
                 SmallVectorImpl<Decl *> &Decls);

  VarDecl *BuildObjCExceptionDecl(TypeSourceInfo *TInfo, QualType ExceptionType,
                                  SourceLocation StartLoc, SourceLocation IdLoc,
                                  const IdentifierInfo *Id,
                                  bool Invalid = false);

  Decl *ActOnObjCExceptionDecl(Scope *S, Declarator &D);

  /// CollectIvarsToConstructOrDestruct - Collect those ivars which require
  /// initialization.
  void
  CollectIvarsToConstructOrDestruct(ObjCInterfaceDecl *OI,
                                    SmallVectorImpl<ObjCIvarDecl *> &Ivars);

  void DiagnoseUseOfUnimplementedSelectors();

  /// DiagnoseUnusedBackingIvarInAccessor - Issue an 'unused' warning if ivar
  /// which backs the property is not used in the property's accessor.
  void DiagnoseUnusedBackingIvarInAccessor(Scope *S,
                                           const ObjCImplementationDecl *ImplD);

  /// GetIvarBackingPropertyAccessor - If method is a property setter/getter and
  /// it property has a backing ivar, returns this ivar; otherwise, returns
  /// NULL. It also returns ivar's property on success.
  ObjCIvarDecl *
  GetIvarBackingPropertyAccessor(const ObjCMethodDecl *Method,
                                 const ObjCPropertyDecl *&PDecl) const;

  /// AddInstanceMethodToGlobalPool - All instance methods in a translation
  /// unit are added to a global pool. This allows us to efficiently associate
  /// a selector with a method declaraation for purposes of typechecking
  /// messages sent to "id" (where the class of the object is unknown).
  void AddInstanceMethodToGlobalPool(ObjCMethodDecl *Method,
                                     bool impl = false) {
    AddMethodToGlobalPool(Method, impl, /*instance*/ true);
  }

  /// AddFactoryMethodToGlobalPool - Same as above, but for factory methods.
  void AddFactoryMethodToGlobalPool(ObjCMethodDecl *Method, bool impl = false) {
    AddMethodToGlobalPool(Method, impl, /*instance*/ false);
  }

  QualType AdjustParameterTypeForObjCAutoRefCount(QualType T,
                                                  SourceLocation NameLoc,
                                                  TypeSourceInfo *TSInfo);

  /// Look for an Objective-C class in the translation unit.
  ///
  /// \param Id The name of the Objective-C class we're looking for. If
  /// typo-correction fixes this name, the Id will be updated
  /// to the fixed name.
  ///
  /// \param IdLoc The location of the name in the translation unit.
  ///
  /// \param DoTypoCorrection If true, this routine will attempt typo correction
  /// if there is no class with the given name.
  ///
  /// \returns The declaration of the named Objective-C class, or NULL if the
  /// class could not be found.
  ObjCInterfaceDecl *getObjCInterfaceDecl(const IdentifierInfo *&Id,
                                          SourceLocation IdLoc,
                                          bool TypoCorrection = false);

  bool inferObjCARCLifetime(ValueDecl *decl);

  /// SetIvarInitializers - This routine builds initialization ASTs for the
  /// Objective-C implementation whose ivars need be initialized.
  void SetIvarInitializers(ObjCImplementationDecl *ObjCImplementation);

  Decl *ActOnIvar(Scope *S, SourceLocation DeclStart, Declarator &D,
                  Expr *BitWidth, tok::ObjCKeywordKind visibility);

  ObjCContainerDecl *getObjCDeclContext() const;

private:
  /// AddMethodToGlobalPool - Add an instance or factory method to the global
  /// pool. See descriptoin of AddInstanceMethodToGlobalPool.
  void AddMethodToGlobalPool(ObjCMethodDecl *Method, bool impl, bool instance);

  /// LookupMethodInGlobalPool - Returns the instance or factory method and
  /// optionally warns if there are multiple signatures.
  ObjCMethodDecl *LookupMethodInGlobalPool(Selector Sel, SourceRange R,
                                           bool receiverIdOrClass,
                                           bool instance);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name ObjC Expressions
  /// Implementations are in SemaExprObjC.cpp
  ///@{

public:
  /// Caches identifiers/selectors for NSFoundation APIs.
  std::unique_ptr<NSAPI> NSAPIObj;

  /// The declaration of the Objective-C NSNumber class.
  ObjCInterfaceDecl *NSNumberDecl;

  /// The declaration of the Objective-C NSValue class.
  ObjCInterfaceDecl *NSValueDecl;

  /// Pointer to NSNumber type (NSNumber *).
  QualType NSNumberPointer;

  /// Pointer to NSValue type (NSValue *).
  QualType NSValuePointer;

  /// The Objective-C NSNumber methods used to create NSNumber literals.
  ObjCMethodDecl *NSNumberLiteralMethods[NSAPI::NumNSNumberLiteralMethods];

  /// The declaration of the Objective-C NSString class.
  ObjCInterfaceDecl *NSStringDecl;

  /// Pointer to NSString type (NSString *).
  QualType NSStringPointer;

  /// The declaration of the stringWithUTF8String: method.
  ObjCMethodDecl *StringWithUTF8StringMethod;

  /// The declaration of the valueWithBytes:objCType: method.
  ObjCMethodDecl *ValueWithBytesObjCTypeMethod;

  /// The declaration of the Objective-C NSArray class.
  ObjCInterfaceDecl *NSArrayDecl;

  /// The declaration of the arrayWithObjects:count: method.
  ObjCMethodDecl *ArrayWithObjectsMethod;

  /// The declaration of the Objective-C NSDictionary class.
  ObjCInterfaceDecl *NSDictionaryDecl;

  /// The declaration of the dictionaryWithObjects:forKeys:count: method.
  ObjCMethodDecl *DictionaryWithObjectsMethod;

  /// id<NSCopying> type.
  QualType QIDNSCopying;

  /// will hold 'respondsToSelector:'
  Selector RespondsToSelectorSel;

  ExprResult HandleExprPropertyRefExpr(const ObjCObjectPointerType *OPT,
                                       Expr *BaseExpr, SourceLocation OpLoc,
                                       DeclarationName MemberName,
                                       SourceLocation MemberLoc,
                                       SourceLocation SuperLoc,
                                       QualType SuperType, bool Super);

  ExprResult ActOnClassPropertyRefExpr(const IdentifierInfo &receiverName,
                                       const IdentifierInfo &propertyName,
                                       SourceLocation receiverNameLoc,
                                       SourceLocation propertyNameLoc);

  // ParseObjCStringLiteral - Parse Objective-C string literals.
  ExprResult ParseObjCStringLiteral(SourceLocation *AtLocs,
                                    ArrayRef<Expr *> Strings);

  ExprResult BuildObjCStringLiteral(SourceLocation AtLoc, StringLiteral *S);

  /// BuildObjCNumericLiteral - builds an ObjCBoxedExpr AST node for the
  /// numeric literal expression. Type of the expression will be "NSNumber *"
  /// or "id" if NSNumber is unavailable.
  ExprResult BuildObjCNumericLiteral(SourceLocation AtLoc, Expr *Number);
  ExprResult ActOnObjCBoolLiteral(SourceLocation AtLoc, SourceLocation ValueLoc,
                                  bool Value);
  ExprResult BuildObjCArrayLiteral(SourceRange SR, MultiExprArg Elements);

  /// BuildObjCBoxedExpr - builds an ObjCBoxedExpr AST node for the
  /// '@' prefixed parenthesized expression. The type of the expression will
  /// either be "NSNumber *", "NSString *" or "NSValue *" depending on the type
  /// of ValueType, which is allowed to be a built-in numeric type, "char *",
  /// "const char *" or C structure with attribute 'objc_boxable'.
  ExprResult BuildObjCBoxedExpr(SourceRange SR, Expr *ValueExpr);

  ExprResult BuildObjCSubscriptExpression(SourceLocation RB, Expr *BaseExpr,
                                          Expr *IndexExpr,
                                          ObjCMethodDecl *getterMethod,
                                          ObjCMethodDecl *setterMethod);

  ExprResult
  BuildObjCDictionaryLiteral(SourceRange SR,
                             MutableArrayRef<ObjCDictionaryElement> Elements);

  ExprResult BuildObjCEncodeExpression(SourceLocation AtLoc,
                                       TypeSourceInfo *EncodedTypeInfo,
                                       SourceLocation RParenLoc);

  ExprResult ParseObjCEncodeExpression(SourceLocation AtLoc,
                                       SourceLocation EncodeLoc,
                                       SourceLocation LParenLoc, ParsedType Ty,
                                       SourceLocation RParenLoc);

  /// ParseObjCSelectorExpression - Build selector expression for \@selector
  ExprResult ParseObjCSelectorExpression(Selector Sel, SourceLocation AtLoc,
                                         SourceLocation SelLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation RParenLoc,
                                         bool WarnMultipleSelectors);

  /// ParseObjCProtocolExpression - Build protocol expression for \@protocol
  ExprResult ParseObjCProtocolExpression(IdentifierInfo *ProtocolName,
                                         SourceLocation AtLoc,
                                         SourceLocation ProtoLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation ProtoIdLoc,
                                         SourceLocation RParenLoc);

  ObjCMethodDecl *tryCaptureObjCSelf(SourceLocation Loc);

  /// Describes the kind of message expression indicated by a message
  /// send that starts with an identifier.
  enum ObjCMessageKind {
    /// The message is sent to 'super'.
    ObjCSuperMessage,
    /// The message is an instance message.
    ObjCInstanceMessage,
    /// The message is a class message, and the identifier is a type
    /// name.
    ObjCClassMessage
  };

  ObjCMessageKind getObjCMessageKind(Scope *S, IdentifierInfo *Name,
                                     SourceLocation NameLoc, bool IsSuper,
                                     bool HasTrailingDot,
                                     ParsedType &ReceiverType);

  ExprResult ActOnSuperMessage(Scope *S, SourceLocation SuperLoc, Selector Sel,
                               SourceLocation LBracLoc,
                               ArrayRef<SourceLocation> SelectorLocs,
                               SourceLocation RBracLoc, MultiExprArg Args);

  ExprResult BuildClassMessage(TypeSourceInfo *ReceiverTypeInfo,
                               QualType ReceiverType, SourceLocation SuperLoc,
                               Selector Sel, ObjCMethodDecl *Method,
                               SourceLocation LBracLoc,
                               ArrayRef<SourceLocation> SelectorLocs,
                               SourceLocation RBracLoc, MultiExprArg Args,
                               bool isImplicit = false);

  ExprResult BuildClassMessageImplicit(QualType ReceiverType,
                                       bool isSuperReceiver, SourceLocation Loc,
                                       Selector Sel, ObjCMethodDecl *Method,
                                       MultiExprArg Args);

  ExprResult ActOnClassMessage(Scope *S, ParsedType Receiver, Selector Sel,
                               SourceLocation LBracLoc,
                               ArrayRef<SourceLocation> SelectorLocs,
                               SourceLocation RBracLoc, MultiExprArg Args);

  ExprResult BuildInstanceMessage(Expr *Receiver, QualType ReceiverType,
                                  SourceLocation SuperLoc, Selector Sel,
                                  ObjCMethodDecl *Method,
                                  SourceLocation LBracLoc,
                                  ArrayRef<SourceLocation> SelectorLocs,
                                  SourceLocation RBracLoc, MultiExprArg Args,
                                  bool isImplicit = false);

  ExprResult BuildInstanceMessageImplicit(Expr *Receiver, QualType ReceiverType,
                                          SourceLocation Loc, Selector Sel,
                                          ObjCMethodDecl *Method,
                                          MultiExprArg Args);

  ExprResult ActOnInstanceMessage(Scope *S, Expr *Receiver, Selector Sel,
                                  SourceLocation LBracLoc,
                                  ArrayRef<SourceLocation> SelectorLocs,
                                  SourceLocation RBracLoc, MultiExprArg Args);

  ExprResult BuildObjCBridgedCast(SourceLocation LParenLoc,
                                  ObjCBridgeCastKind Kind,
                                  SourceLocation BridgeKeywordLoc,
                                  TypeSourceInfo *TSInfo, Expr *SubExpr);

  ExprResult ActOnObjCBridgedCast(Scope *S, SourceLocation LParenLoc,
                                  ObjCBridgeCastKind Kind,
                                  SourceLocation BridgeKeywordLoc,
                                  ParsedType Type, SourceLocation RParenLoc,
                                  Expr *SubExpr);

  void CheckTollFreeBridgeCast(QualType castType, Expr *castExpr);

  void CheckObjCBridgeRelatedCast(QualType castType, Expr *castExpr);

  bool CheckTollFreeBridgeStaticCast(QualType castType, Expr *castExpr,
                                     CastKind &Kind);

  bool checkObjCBridgeRelatedComponents(SourceLocation Loc, QualType DestType,
                                        QualType SrcType,
                                        ObjCInterfaceDecl *&RelatedClass,
                                        ObjCMethodDecl *&ClassMethod,
                                        ObjCMethodDecl *&InstanceMethod,
                                        TypedefNameDecl *&TDNDecl, bool CfToNs,
                                        bool Diagnose = true);

  bool CheckObjCBridgeRelatedConversions(SourceLocation Loc, QualType DestType,
                                         QualType SrcType, Expr *&SrcExpr,
                                         bool Diagnose = true);

  /// Private Helper predicate to check for 'self'.
  bool isSelfExpr(Expr *RExpr);
  bool isSelfExpr(Expr *RExpr, const ObjCMethodDecl *Method);

  ObjCMethodDecl *LookupMethodInQualifiedType(Selector Sel,
                                              const ObjCObjectPointerType *OPT,
                                              bool IsInstance);
  ObjCMethodDecl *LookupMethodInObjectType(Selector Sel, QualType Ty,
                                           bool IsInstance);

  bool isKnownName(StringRef name);

  enum ARCConversionResult { ACR_okay, ACR_unbridged, ACR_error };

  /// Checks for invalid conversions and casts between
  /// retainable pointers and other pointer kinds for ARC and Weak.
  ARCConversionResult CheckObjCConversion(SourceRange castRange,
                                          QualType castType, Expr *&op,
                                          CheckedConversionKind CCK,
                                          bool Diagnose = true,
                                          bool DiagnoseCFAudited = false,
                                          BinaryOperatorKind Opc = BO_PtrMemD);

  Expr *stripARCUnbridgedCast(Expr *e);
  void diagnoseARCUnbridgedCast(Expr *e);

  bool CheckObjCARCUnavailableWeakConversion(QualType castType,
                                             QualType ExprType);

  /// CheckMessageArgumentTypes - Check types in an Obj-C message send.
  /// \param Method - May be null.
  /// \param [out] ReturnType - The return type of the send.
  /// \return true iff there were any incompatible types.
  bool CheckMessageArgumentTypes(const Expr *Receiver, QualType ReceiverType,
                                 MultiExprArg Args, Selector Sel,
                                 ArrayRef<SourceLocation> SelectorLocs,
                                 ObjCMethodDecl *Method, bool isClassMessage,
                                 bool isSuperMessage, SourceLocation lbrac,
                                 SourceLocation rbrac, SourceRange RecRange,
                                 QualType &ReturnType, ExprValueKind &VK);

  /// Determine the result of a message send expression based on
  /// the type of the receiver, the method expected to receive the message,
  /// and the form of the message send.
  QualType getMessageSendResultType(const Expr *Receiver, QualType ReceiverType,
                                    ObjCMethodDecl *Method, bool isClassMessage,
                                    bool isSuperMessage);

  /// If the given expression involves a message send to a method
  /// with a related result type, emit a note describing what happened.
  void EmitRelatedResultTypeNote(const Expr *E);

  /// Given that we had incompatible pointer types in a return
  /// statement, check whether we're in a method with a related result
  /// type, and if so, emit a note describing what happened.
  void EmitRelatedResultTypeNoteForReturn(QualType destType);

  /// LookupInstanceMethodInGlobalPool - Returns the method and warns if
  /// there are multiple signatures.
  ObjCMethodDecl *
  LookupInstanceMethodInGlobalPool(Selector Sel, SourceRange R,
                                   bool receiverIdOrClass = false) {
    return LookupMethodInGlobalPool(Sel, R, receiverIdOrClass,
                                    /*instance*/ true);
  }

  /// LookupFactoryMethodInGlobalPool - Returns the method and warns if
  /// there are multiple signatures.
  ObjCMethodDecl *
  LookupFactoryMethodInGlobalPool(Selector Sel, SourceRange R,
                                  bool receiverIdOrClass = false) {
    return LookupMethodInGlobalPool(Sel, R, receiverIdOrClass,
                                    /*instance*/ false);
  }

  /// The parser has read a name in, and Sema has detected that we're currently
  /// inside an ObjC method. Perform some additional checks and determine if we
  /// should form a reference to an ivar.
  ///
  /// Ideally, most of this would be done by lookup, but there's
  /// actually quite a lot of extra work involved.
  DeclResult LookupIvarInObjCMethod(LookupResult &Lookup, Scope *S,
                                    IdentifierInfo *II);

  /// The parser has read a name in, and Sema has detected that we're currently
  /// inside an ObjC method. Perform some additional checks and determine if we
  /// should form a reference to an ivar. If so, build an expression referencing
  /// that ivar.
  ExprResult LookupInObjCMethod(LookupResult &LookUp, Scope *S,
                                IdentifierInfo *II,
                                bool AllowBuiltinCreation = false);

  ExprResult BuildIvarRefExpr(Scope *S, SourceLocation Loc, ObjCIvarDecl *IV);

  /// FindCompositeObjCPointerType - Helper method to find composite type of
  /// two objective-c pointer types of the two input expressions.
  QualType FindCompositeObjCPointerType(ExprResult &LHS, ExprResult &RHS,
                                        SourceLocation QuestionLoc);

  bool CheckConversionToObjCLiteral(QualType DstType, Expr *&SrcExpr,
                                    bool Diagnose = true);

  /// ActOnObjCBoolLiteral - Parse {__objc_yes,__objc_no} literals.
  ExprResult ActOnObjCBoolLiteral(SourceLocation OpLoc, tok::TokenKind Kind);

  ExprResult
  ActOnObjCAvailabilityCheckExpr(llvm::ArrayRef<AvailabilitySpec> AvailSpecs,
                                 SourceLocation AtLoc, SourceLocation RParen);

  /// Prepare a conversion of the given expression to an ObjC object
  /// pointer type.
  CastKind PrepareCastToObjCObjectPointer(ExprResult &E);

  // Note that LK_String is intentionally after the other literals, as
  // this is used for diagnostics logic.
  enum ObjCLiteralKind {
    LK_Array,
    LK_Dictionary,
    LK_Numeric,
    LK_Boxed,
    LK_String,
    LK_Block,
    LK_None
  };
  ObjCLiteralKind CheckLiteralKind(Expr *FromE);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name ObjC @property and @synthesize
  /// Implementations are in SemaObjCProperty.cpp
  ///@{

public:
  /// Ensure attributes are consistent with type.
  /// \param [in, out] Attributes The attributes to check; they will
  /// be modified to be consistent with \p PropertyTy.
  void CheckObjCPropertyAttributes(Decl *PropertyPtrTy, SourceLocation Loc,
                                   unsigned &Attributes,
                                   bool propertyInPrimaryClass);

  /// Process the specified property declaration and create decls for the
  /// setters and getters as needed.
  /// \param property The property declaration being processed
  void ProcessPropertyDecl(ObjCPropertyDecl *property);

  Decl *ActOnProperty(Scope *S, SourceLocation AtLoc, SourceLocation LParenLoc,
                      FieldDeclarator &FD, ObjCDeclSpec &ODS,
                      Selector GetterSel, Selector SetterSel,
                      tok::ObjCKeywordKind MethodImplKind,
                      DeclContext *lexicalDC = nullptr);

  Decl *ActOnPropertyImplDecl(Scope *S, SourceLocation AtLoc,
                              SourceLocation PropertyLoc, bool ImplKind,
                              IdentifierInfo *PropertyId,
                              IdentifierInfo *PropertyIvar,
                              SourceLocation PropertyIvarLoc,
                              ObjCPropertyQueryKind QueryKind);

  /// Called by ActOnProperty to handle \@property declarations in
  /// class extensions.
  ObjCPropertyDecl *HandlePropertyInClassExtension(
      Scope *S, SourceLocation AtLoc, SourceLocation LParenLoc,
      FieldDeclarator &FD, Selector GetterSel, SourceLocation GetterNameLoc,
      Selector SetterSel, SourceLocation SetterNameLoc, const bool isReadWrite,
      unsigned &Attributes, const unsigned AttributesAsWritten, QualType T,
      TypeSourceInfo *TSI, tok::ObjCKeywordKind MethodImplKind);

  /// Called by ActOnProperty and HandlePropertyInClassExtension to
  /// handle creating the ObjcPropertyDecl for a category or \@interface.
  ObjCPropertyDecl *
  CreatePropertyDecl(Scope *S, ObjCContainerDecl *CDecl, SourceLocation AtLoc,
                     SourceLocation LParenLoc, FieldDeclarator &FD,
                     Selector GetterSel, SourceLocation GetterNameLoc,
                     Selector SetterSel, SourceLocation SetterNameLoc,
                     const bool isReadWrite, const unsigned Attributes,
                     const unsigned AttributesAsWritten, QualType T,
                     TypeSourceInfo *TSI, tok::ObjCKeywordKind MethodImplKind,
                     DeclContext *lexicalDC = nullptr);

  void DiagnosePropertyMismatch(ObjCPropertyDecl *Property,
                                ObjCPropertyDecl *SuperProperty,
                                const IdentifierInfo *Name,
                                bool OverridingProtocolProperty);

  bool DiagnosePropertyAccessorMismatch(ObjCPropertyDecl *PD,
                                        ObjCMethodDecl *Getter,
                                        SourceLocation Loc);

  /// DiagnoseUnimplementedProperties - This routine warns on those properties
  /// which must be implemented by this implementation.
  void DiagnoseUnimplementedProperties(Scope *S, ObjCImplDecl *IMPDecl,
                                       ObjCContainerDecl *CDecl,
                                       bool SynthesizeProperties);

  /// Diagnose any null-resettable synthesized setters.
  void diagnoseNullResettableSynthesizedSetters(const ObjCImplDecl *impDecl);

  /// DefaultSynthesizeProperties - This routine default synthesizes all
  /// properties which must be synthesized in the class's \@implementation.
  void DefaultSynthesizeProperties(Scope *S, ObjCImplDecl *IMPDecl,
                                   ObjCInterfaceDecl *IDecl,
                                   SourceLocation AtEnd);
  void DefaultSynthesizeProperties(Scope *S, Decl *D, SourceLocation AtEnd);

  /// IvarBacksCurrentMethodAccessor - This routine returns 'true' if 'IV' is
  /// an ivar synthesized for 'Method' and 'Method' is a property accessor
  /// declared in class 'IFace'.
  bool IvarBacksCurrentMethodAccessor(ObjCInterfaceDecl *IFace,
                                      ObjCMethodDecl *Method, ObjCIvarDecl *IV);

  void DiagnoseOwningPropertyGetterSynthesis(const ObjCImplementationDecl *D);

  void
  DiagnoseMissingDesignatedInitOverrides(const ObjCImplementationDecl *ImplD,
                                         const ObjCInterfaceDecl *IFD);

  /// AtomicPropertySetterGetterRules - This routine enforces the rule (via
  /// warning) when atomic property has one but not the other user-declared
  /// setter or getter.
  void AtomicPropertySetterGetterRules(ObjCImplDecl *IMPDecl,
                                       ObjCInterfaceDecl *IDecl);

  ///@}

  //
  //
  // -------------------------------------------------------------------------
  //
  //

  /// \name ObjC Attributes
  /// Implementations are in SemaObjC.cpp
  ///@{

  bool isNSStringType(QualType T, bool AllowNSAttributedString = false);
  bool isCFStringType(QualType T);

  void handleIBOutlet(Decl *D, const ParsedAttr &AL);
  void handleIBOutletCollection(Decl *D, const ParsedAttr &AL);

  void handleSuppresProtocolAttr(Decl *D, const ParsedAttr &AL);
  void handleDirectAttr(Decl *D, const ParsedAttr &AL);
  void handleDirectMembersAttr(Decl *D, const ParsedAttr &AL);
  void handleMethodFamilyAttr(Decl *D, const ParsedAttr &AL);
  void handleNSObject(Decl *D, const ParsedAttr &AL);
  void handleIndependentClass(Decl *D, const ParsedAttr &AL);
  void handleBlocksAttr(Decl *D, const ParsedAttr &AL);
  void handleReturnsInnerPointerAttr(Decl *D, const ParsedAttr &Attrs);
  void handleXReturnsXRetainedAttr(Decl *D, const ParsedAttr &AL);
  void handleRequiresSuperAttr(Decl *D, const ParsedAttr &Attrs);
  void handleNSErrorDomain(Decl *D, const ParsedAttr &Attr);
  void handleBridgeAttr(Decl *D, const ParsedAttr &AL);
  void handleBridgeMutableAttr(Decl *D, const ParsedAttr &AL);
  void handleBridgeRelatedAttr(Decl *D, const ParsedAttr &AL);
  void handleDesignatedInitializer(Decl *D, const ParsedAttr &AL);
  void handleRuntimeName(Decl *D, const ParsedAttr &AL);
  void handleBoxable(Decl *D, const ParsedAttr &AL);
  void handleOwnershipAttr(Decl *D, const ParsedAttr &AL);
  void handlePreciseLifetimeAttr(Decl *D, const ParsedAttr &AL);
  void handleExternallyRetainedAttr(Decl *D, const ParsedAttr &AL);

  void AddXConsumedAttr(Decl *D, const AttributeCommonInfo &CI,
                        Sema::RetainOwnershipKind K,
                        bool IsTemplateInstantiation);

  /// \return whether the parameter is a pointer to OSObject pointer.
  bool isValidOSObjectOutParameter(const Decl *D);
  bool checkNSReturnsRetainedReturnType(SourceLocation loc, QualType type);

  Sema::RetainOwnershipKind
  parsedAttrToRetainOwnershipKind(const ParsedAttr &AL);

  ///@}
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAOBJC_H
