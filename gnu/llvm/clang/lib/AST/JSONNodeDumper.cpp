#include "clang/AST/JSONNodeDumper.h"
#include "clang/AST/Type.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/Lexer.h"
#include "llvm/ADT/StringExtras.h"
#include <optional>

using namespace clang;

void JSONNodeDumper::addPreviousDeclaration(const Decl *D) {
  switch (D->getKind()) {
#define DECL(DERIVED, BASE)                                                    \
  case Decl::DERIVED:                                                          \
    return writePreviousDeclImpl(cast<DERIVED##Decl>(D));
#define ABSTRACT_DECL(DECL)
#include "clang/AST/DeclNodes.inc"
#undef ABSTRACT_DECL
#undef DECL
  }
  llvm_unreachable("Decl that isn't part of DeclNodes.inc!");
}

void JSONNodeDumper::Visit(const Attr *A) {
  const char *AttrName = nullptr;
  switch (A->getKind()) {
#define ATTR(X)                                                                \
  case attr::X:                                                                \
    AttrName = #X"Attr";                                                       \
    break;
#include "clang/Basic/AttrList.inc"
#undef ATTR
  }
  JOS.attribute("id", createPointerRepresentation(A));
  JOS.attribute("kind", AttrName);
  JOS.attributeObject("range", [A, this] { writeSourceRange(A->getRange()); });
  attributeOnlyIfTrue("inherited", A->isInherited());
  attributeOnlyIfTrue("implicit", A->isImplicit());

  // FIXME: it would be useful for us to output the spelling kind as well as
  // the actual spelling. This would allow us to distinguish between the
  // various attribute syntaxes, but we don't currently track that information
  // within the AST.
  //JOS.attribute("spelling", A->getSpelling());

  InnerAttrVisitor::Visit(A);
}

void JSONNodeDumper::Visit(const Stmt *S) {
  if (!S)
    return;

  JOS.attribute("id", createPointerRepresentation(S));
  JOS.attribute("kind", S->getStmtClassName());
  JOS.attributeObject("range",
                      [S, this] { writeSourceRange(S->getSourceRange()); });

  if (const auto *E = dyn_cast<Expr>(S)) {
    JOS.attribute("type", createQualType(E->getType()));
    const char *Category = nullptr;
    switch (E->getValueKind()) {
    case VK_LValue: Category = "lvalue"; break;
    case VK_XValue: Category = "xvalue"; break;
    case VK_PRValue:
      Category = "prvalue";
      break;
    }
    JOS.attribute("valueCategory", Category);
  }
  InnerStmtVisitor::Visit(S);
}

void JSONNodeDumper::Visit(const Type *T) {
  JOS.attribute("id", createPointerRepresentation(T));

  if (!T)
    return;

  JOS.attribute("kind", (llvm::Twine(T->getTypeClassName()) + "Type").str());
  JOS.attribute("type", createQualType(QualType(T, 0), /*Desugar=*/false));
  attributeOnlyIfTrue("containsErrors", T->containsErrors());
  attributeOnlyIfTrue("isDependent", T->isDependentType());
  attributeOnlyIfTrue("isInstantiationDependent",
                      T->isInstantiationDependentType());
  attributeOnlyIfTrue("isVariablyModified", T->isVariablyModifiedType());
  attributeOnlyIfTrue("containsUnexpandedPack",
                      T->containsUnexpandedParameterPack());
  attributeOnlyIfTrue("isImported", T->isFromAST());
  InnerTypeVisitor::Visit(T);
}

void JSONNodeDumper::Visit(QualType T) {
  JOS.attribute("id", createPointerRepresentation(T.getAsOpaquePtr()));
  JOS.attribute("kind", "QualType");
  JOS.attribute("type", createQualType(T));
  JOS.attribute("qualifiers", T.split().Quals.getAsString());
}

void JSONNodeDumper::Visit(TypeLoc TL) {
  if (TL.isNull())
    return;
  JOS.attribute("kind",
                (llvm::Twine(TL.getTypeLocClass() == TypeLoc::Qualified
                                 ? "Qualified"
                                 : TL.getTypePtr()->getTypeClassName()) +
                 "TypeLoc")
                    .str());
  JOS.attribute("type",
                createQualType(QualType(TL.getType()), /*Desugar=*/false));
  JOS.attributeObject("range",
                      [TL, this] { writeSourceRange(TL.getSourceRange()); });
}

void JSONNodeDumper::Visit(const Decl *D) {
  JOS.attribute("id", createPointerRepresentation(D));

  if (!D)
    return;

  JOS.attribute("kind", (llvm::Twine(D->getDeclKindName()) + "Decl").str());
  JOS.attributeObject("loc",
                      [D, this] { writeSourceLocation(D->getLocation()); });
  JOS.attributeObject("range",
                      [D, this] { writeSourceRange(D->getSourceRange()); });
  attributeOnlyIfTrue("isImplicit", D->isImplicit());
  attributeOnlyIfTrue("isInvalid", D->isInvalidDecl());

  if (D->isUsed())
    JOS.attribute("isUsed", true);
  else if (D->isThisDeclarationReferenced())
    JOS.attribute("isReferenced", true);

  if (const auto *ND = dyn_cast<NamedDecl>(D))
    attributeOnlyIfTrue("isHidden", !ND->isUnconditionallyVisible());

  if (D->getLexicalDeclContext() != D->getDeclContext()) {
    // Because of multiple inheritance, a DeclContext pointer does not produce
    // the same pointer representation as a Decl pointer that references the
    // same AST Node.
    const auto *ParentDeclContextDecl = dyn_cast<Decl>(D->getDeclContext());
    JOS.attribute("parentDeclContextId",
                  createPointerRepresentation(ParentDeclContextDecl));
  }

  addPreviousDeclaration(D);
  InnerDeclVisitor::Visit(D);
}

void JSONNodeDumper::Visit(const comments::Comment *C,
                           const comments::FullComment *FC) {
  if (!C)
    return;

  JOS.attribute("id", createPointerRepresentation(C));
  JOS.attribute("kind", C->getCommentKindName());
  JOS.attributeObject("loc",
                      [C, this] { writeSourceLocation(C->getLocation()); });
  JOS.attributeObject("range",
                      [C, this] { writeSourceRange(C->getSourceRange()); });

  InnerCommentVisitor::visit(C, FC);
}

void JSONNodeDumper::Visit(const TemplateArgument &TA, SourceRange R,
                           const Decl *From, StringRef Label) {
  JOS.attribute("kind", "TemplateArgument");
  if (R.isValid())
    JOS.attributeObject("range", [R, this] { writeSourceRange(R); });

  if (From)
    JOS.attribute(Label.empty() ? "fromDecl" : Label, createBareDeclRef(From));

  InnerTemplateArgVisitor::Visit(TA);
}

void JSONNodeDumper::Visit(const CXXCtorInitializer *Init) {
  JOS.attribute("kind", "CXXCtorInitializer");
  if (Init->isAnyMemberInitializer())
    JOS.attribute("anyInit", createBareDeclRef(Init->getAnyMember()));
  else if (Init->isBaseInitializer())
    JOS.attribute("baseInit",
                  createQualType(QualType(Init->getBaseClass(), 0)));
  else if (Init->isDelegatingInitializer())
    JOS.attribute("delegatingInit",
                  createQualType(Init->getTypeSourceInfo()->getType()));
  else
    llvm_unreachable("Unknown initializer type");
}

void JSONNodeDumper::Visit(const OpenACCClause *C) {}

void JSONNodeDumper::Visit(const OMPClause *C) {}

void JSONNodeDumper::Visit(const BlockDecl::Capture &C) {
  JOS.attribute("kind", "Capture");
  attributeOnlyIfTrue("byref", C.isByRef());
  attributeOnlyIfTrue("nested", C.isNested());
  if (C.getVariable())
    JOS.attribute("var", createBareDeclRef(C.getVariable()));
}

void JSONNodeDumper::Visit(const GenericSelectionExpr::ConstAssociation &A) {
  JOS.attribute("associationKind", A.getTypeSourceInfo() ? "case" : "default");
  attributeOnlyIfTrue("selected", A.isSelected());
}

void JSONNodeDumper::Visit(const concepts::Requirement *R) {
  if (!R)
    return;

  switch (R->getKind()) {
  case concepts::Requirement::RK_Type:
    JOS.attribute("kind", "TypeRequirement");
    break;
  case concepts::Requirement::RK_Simple:
    JOS.attribute("kind", "SimpleRequirement");
    break;
  case concepts::Requirement::RK_Compound:
    JOS.attribute("kind", "CompoundRequirement");
    break;
  case concepts::Requirement::RK_Nested:
    JOS.attribute("kind", "NestedRequirement");
    break;
  }

  if (auto *ER = dyn_cast<concepts::ExprRequirement>(R))
    attributeOnlyIfTrue("noexcept", ER->hasNoexceptRequirement());

  attributeOnlyIfTrue("isDependent", R->isDependent());
  if (!R->isDependent())
    JOS.attribute("satisfied", R->isSatisfied());
  attributeOnlyIfTrue("containsUnexpandedPack",
                      R->containsUnexpandedParameterPack());
}

void JSONNodeDumper::Visit(const APValue &Value, QualType Ty) {
  std::string Str;
  llvm::raw_string_ostream OS(Str);
  Value.printPretty(OS, Ctx, Ty);
  JOS.attribute("value", Str);
}

void JSONNodeDumper::Visit(const ConceptReference *CR) {
  JOS.attribute("kind", "ConceptReference");
  JOS.attribute("id", createPointerRepresentation(CR->getNamedConcept()));
  if (const auto *Args = CR->getTemplateArgsAsWritten()) {
    JOS.attributeArray("templateArgsAsWritten", [Args, this] {
      for (const TemplateArgumentLoc &TAL : Args->arguments())
        JOS.object(
            [&TAL, this] { Visit(TAL.getArgument(), TAL.getSourceRange()); });
    });
  }
  JOS.attributeObject("loc",
                      [CR, this] { writeSourceLocation(CR->getLocation()); });
  JOS.attributeObject("range",
                      [CR, this] { writeSourceRange(CR->getSourceRange()); });
}

void JSONNodeDumper::writeIncludeStack(PresumedLoc Loc, bool JustFirst) {
  if (Loc.isInvalid())
    return;

  JOS.attributeBegin("includedFrom");
  JOS.objectBegin();

  if (!JustFirst) {
    // Walk the stack recursively, then print out the presumed location.
    writeIncludeStack(SM.getPresumedLoc(Loc.getIncludeLoc()));
  }

  JOS.attribute("file", Loc.getFilename());
  JOS.objectEnd();
  JOS.attributeEnd();
}

void JSONNodeDumper::writeBareSourceLocation(SourceLocation Loc,
                                             bool IsSpelling) {
  PresumedLoc Presumed = SM.getPresumedLoc(Loc);
  unsigned ActualLine = IsSpelling ? SM.getSpellingLineNumber(Loc)
                                   : SM.getExpansionLineNumber(Loc);
  StringRef ActualFile = SM.getBufferName(Loc);

  if (Presumed.isValid()) {
    JOS.attribute("offset", SM.getDecomposedLoc(Loc).second);
    if (LastLocFilename != ActualFile) {
      JOS.attribute("file", ActualFile);
      JOS.attribute("line", ActualLine);
    } else if (LastLocLine != ActualLine)
      JOS.attribute("line", ActualLine);

    StringRef PresumedFile = Presumed.getFilename();
    if (PresumedFile != ActualFile && LastLocPresumedFilename != PresumedFile)
      JOS.attribute("presumedFile", PresumedFile);

    unsigned PresumedLine = Presumed.getLine();
    if (ActualLine != PresumedLine && LastLocPresumedLine != PresumedLine)
      JOS.attribute("presumedLine", PresumedLine);

    JOS.attribute("col", Presumed.getColumn());
    JOS.attribute("tokLen",
                  Lexer::MeasureTokenLength(Loc, SM, Ctx.getLangOpts()));
    LastLocFilename = ActualFile;
    LastLocPresumedFilename = PresumedFile;
    LastLocPresumedLine = PresumedLine;
    LastLocLine = ActualLine;

    // Orthogonal to the file, line, and column de-duplication is whether the
    // given location was a result of an include. If so, print where the
    // include location came from.
    writeIncludeStack(SM.getPresumedLoc(Presumed.getIncludeLoc()),
                      /*JustFirst*/ true);
  }
}

void JSONNodeDumper::writeSourceLocation(SourceLocation Loc) {
  SourceLocation Spelling = SM.getSpellingLoc(Loc);
  SourceLocation Expansion = SM.getExpansionLoc(Loc);

  if (Expansion != Spelling) {
    // If the expansion and the spelling are different, output subobjects
    // describing both locations.
    JOS.attributeObject("spellingLoc", [Spelling, this] {
      writeBareSourceLocation(Spelling, /*IsSpelling*/ true);
    });
    JOS.attributeObject("expansionLoc", [Expansion, Loc, this] {
      writeBareSourceLocation(Expansion, /*IsSpelling*/ false);
      // If there is a macro expansion, add extra information if the interesting
      // bit is the macro arg expansion.
      if (SM.isMacroArgExpansion(Loc))
        JOS.attribute("isMacroArgExpansion", true);
    });
  } else
    writeBareSourceLocation(Spelling, /*IsSpelling*/ true);
}

void JSONNodeDumper::writeSourceRange(SourceRange R) {
  JOS.attributeObject("begin",
                      [R, this] { writeSourceLocation(R.getBegin()); });
  JOS.attributeObject("end", [R, this] { writeSourceLocation(R.getEnd()); });
}

std::string JSONNodeDumper::createPointerRepresentation(const void *Ptr) {
  // Because JSON stores integer values as signed 64-bit integers, trying to
  // represent them as such makes for very ugly pointer values in the resulting
  // output. Instead, we convert the value to hex and treat it as a string.
  return "0x" + llvm::utohexstr(reinterpret_cast<uint64_t>(Ptr), true);
}

llvm::json::Object JSONNodeDumper::createQualType(QualType QT, bool Desugar) {
  SplitQualType SQT = QT.split();
  std::string SQTS = QualType::getAsString(SQT, PrintPolicy);
  llvm::json::Object Ret{{"qualType", SQTS}};

  if (Desugar && !QT.isNull()) {
    SplitQualType DSQT = QT.getSplitDesugaredType();
    if (DSQT != SQT) {
      std::string DSQTS = QualType::getAsString(DSQT, PrintPolicy);
      if (DSQTS != SQTS)
        Ret["desugaredQualType"] = DSQTS;
    }
    if (const auto *TT = QT->getAs<TypedefType>())
      Ret["typeAliasDeclId"] = createPointerRepresentation(TT->getDecl());
  }
  return Ret;
}

void JSONNodeDumper::writeBareDeclRef(const Decl *D) {
  JOS.attribute("id", createPointerRepresentation(D));
  if (!D)
    return;

  JOS.attribute("kind", (llvm::Twine(D->getDeclKindName()) + "Decl").str());
  if (const auto *ND = dyn_cast<NamedDecl>(D))
    JOS.attribute("name", ND->getDeclName().getAsString());
  if (const auto *VD = dyn_cast<ValueDecl>(D))
    JOS.attribute("type", createQualType(VD->getType()));
}

llvm::json::Object JSONNodeDumper::createBareDeclRef(const Decl *D) {
  llvm::json::Object Ret{{"id", createPointerRepresentation(D)}};
  if (!D)
    return Ret;

  Ret["kind"] = (llvm::Twine(D->getDeclKindName()) + "Decl").str();
  if (const auto *ND = dyn_cast<NamedDecl>(D))
    Ret["name"] = ND->getDeclName().getAsString();
  if (const auto *VD = dyn_cast<ValueDecl>(D))
    Ret["type"] = createQualType(VD->getType());
  return Ret;
}

llvm::json::Array JSONNodeDumper::createCastPath(const CastExpr *C) {
  llvm::json::Array Ret;
  if (C->path_empty())
    return Ret;

  for (auto I = C->path_begin(), E = C->path_end(); I != E; ++I) {
    const CXXBaseSpecifier *Base = *I;
    const auto *RD =
        cast<CXXRecordDecl>(Base->getType()->castAs<RecordType>()->getDecl());

    llvm::json::Object Val{{"name", RD->getName()}};
    if (Base->isVirtual())
      Val["isVirtual"] = true;
    Ret.push_back(std::move(Val));
  }
  return Ret;
}

#define FIELD2(Name, Flag)  if (RD->Flag()) Ret[Name] = true
#define FIELD1(Flag)        FIELD2(#Flag, Flag)

static llvm::json::Object
createDefaultConstructorDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("exists", hasDefaultConstructor);
  FIELD2("trivial", hasTrivialDefaultConstructor);
  FIELD2("nonTrivial", hasNonTrivialDefaultConstructor);
  FIELD2("userProvided", hasUserProvidedDefaultConstructor);
  FIELD2("isConstexpr", hasConstexprDefaultConstructor);
  FIELD2("needsImplicit", needsImplicitDefaultConstructor);
  FIELD2("defaultedIsConstexpr", defaultedDefaultConstructorIsConstexpr);

  return Ret;
}

static llvm::json::Object
createCopyConstructorDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("simple", hasSimpleCopyConstructor);
  FIELD2("trivial", hasTrivialCopyConstructor);
  FIELD2("nonTrivial", hasNonTrivialCopyConstructor);
  FIELD2("userDeclared", hasUserDeclaredCopyConstructor);
  FIELD2("hasConstParam", hasCopyConstructorWithConstParam);
  FIELD2("implicitHasConstParam", implicitCopyConstructorHasConstParam);
  FIELD2("needsImplicit", needsImplicitCopyConstructor);
  FIELD2("needsOverloadResolution", needsOverloadResolutionForCopyConstructor);
  if (!RD->needsOverloadResolutionForCopyConstructor())
    FIELD2("defaultedIsDeleted", defaultedCopyConstructorIsDeleted);

  return Ret;
}

static llvm::json::Object
createMoveConstructorDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("exists", hasMoveConstructor);
  FIELD2("simple", hasSimpleMoveConstructor);
  FIELD2("trivial", hasTrivialMoveConstructor);
  FIELD2("nonTrivial", hasNonTrivialMoveConstructor);
  FIELD2("userDeclared", hasUserDeclaredMoveConstructor);
  FIELD2("needsImplicit", needsImplicitMoveConstructor);
  FIELD2("needsOverloadResolution", needsOverloadResolutionForMoveConstructor);
  if (!RD->needsOverloadResolutionForMoveConstructor())
    FIELD2("defaultedIsDeleted", defaultedMoveConstructorIsDeleted);

  return Ret;
}

static llvm::json::Object
createCopyAssignmentDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("simple", hasSimpleCopyAssignment);
  FIELD2("trivial", hasTrivialCopyAssignment);
  FIELD2("nonTrivial", hasNonTrivialCopyAssignment);
  FIELD2("hasConstParam", hasCopyAssignmentWithConstParam);
  FIELD2("implicitHasConstParam", implicitCopyAssignmentHasConstParam);
  FIELD2("userDeclared", hasUserDeclaredCopyAssignment);
  FIELD2("needsImplicit", needsImplicitCopyAssignment);
  FIELD2("needsOverloadResolution", needsOverloadResolutionForCopyAssignment);

  return Ret;
}

static llvm::json::Object
createMoveAssignmentDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("exists", hasMoveAssignment);
  FIELD2("simple", hasSimpleMoveAssignment);
  FIELD2("trivial", hasTrivialMoveAssignment);
  FIELD2("nonTrivial", hasNonTrivialMoveAssignment);
  FIELD2("userDeclared", hasUserDeclaredMoveAssignment);
  FIELD2("needsImplicit", needsImplicitMoveAssignment);
  FIELD2("needsOverloadResolution", needsOverloadResolutionForMoveAssignment);

  return Ret;
}

static llvm::json::Object
createDestructorDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  FIELD2("simple", hasSimpleDestructor);
  FIELD2("irrelevant", hasIrrelevantDestructor);
  FIELD2("trivial", hasTrivialDestructor);
  FIELD2("nonTrivial", hasNonTrivialDestructor);
  FIELD2("userDeclared", hasUserDeclaredDestructor);
  FIELD2("needsImplicit", needsImplicitDestructor);
  FIELD2("needsOverloadResolution", needsOverloadResolutionForDestructor);
  if (!RD->needsOverloadResolutionForDestructor())
    FIELD2("defaultedIsDeleted", defaultedDestructorIsDeleted);

  return Ret;
}

llvm::json::Object
JSONNodeDumper::createCXXRecordDefinitionData(const CXXRecordDecl *RD) {
  llvm::json::Object Ret;

  // This data is common to all C++ classes.
  FIELD1(isGenericLambda);
  FIELD1(isLambda);
  FIELD1(isEmpty);
  FIELD1(isAggregate);
  FIELD1(isStandardLayout);
  FIELD1(isTriviallyCopyable);
  FIELD1(isPOD);
  FIELD1(isTrivial);
  FIELD1(isPolymorphic);
  FIELD1(isAbstract);
  FIELD1(isLiteral);
  FIELD1(canPassInRegisters);
  FIELD1(hasUserDeclaredConstructor);
  FIELD1(hasConstexprNonCopyMoveConstructor);
  FIELD1(hasMutableFields);
  FIELD1(hasVariantMembers);
  FIELD2("canConstDefaultInit", allowConstDefaultInit);

  Ret["defaultCtor"] = createDefaultConstructorDefinitionData(RD);
  Ret["copyCtor"] = createCopyConstructorDefinitionData(RD);
  Ret["moveCtor"] = createMoveConstructorDefinitionData(RD);
  Ret["copyAssign"] = createCopyAssignmentDefinitionData(RD);
  Ret["moveAssign"] = createMoveAssignmentDefinitionData(RD);
  Ret["dtor"] = createDestructorDefinitionData(RD);

  return Ret;
}

#undef FIELD1
#undef FIELD2

std::string JSONNodeDumper::createAccessSpecifier(AccessSpecifier AS) {
  const auto AccessSpelling = getAccessSpelling(AS);
  if (AccessSpelling.empty())
    return "none";
  return AccessSpelling.str();
}

llvm::json::Object
JSONNodeDumper::createCXXBaseSpecifier(const CXXBaseSpecifier &BS) {
  llvm::json::Object Ret;

  Ret["type"] = createQualType(BS.getType());
  Ret["access"] = createAccessSpecifier(BS.getAccessSpecifier());
  Ret["writtenAccess"] =
      createAccessSpecifier(BS.getAccessSpecifierAsWritten());
  if (BS.isVirtual())
    Ret["isVirtual"] = true;
  if (BS.isPackExpansion())
    Ret["isPackExpansion"] = true;

  return Ret;
}

void JSONNodeDumper::VisitAliasAttr(const AliasAttr *AA) {
  JOS.attribute("aliasee", AA->getAliasee());
}

void JSONNodeDumper::VisitCleanupAttr(const CleanupAttr *CA) {
  JOS.attribute("cleanup_function", createBareDeclRef(CA->getFunctionDecl()));
}

void JSONNodeDumper::VisitDeprecatedAttr(const DeprecatedAttr *DA) {
  if (!DA->getMessage().empty())
    JOS.attribute("message", DA->getMessage());
  if (!DA->getReplacement().empty())
    JOS.attribute("replacement", DA->getReplacement());
}

void JSONNodeDumper::VisitUnavailableAttr(const UnavailableAttr *UA) {
  if (!UA->getMessage().empty())
    JOS.attribute("message", UA->getMessage());
}

void JSONNodeDumper::VisitSectionAttr(const SectionAttr *SA) {
  JOS.attribute("section_name", SA->getName());
}

void JSONNodeDumper::VisitVisibilityAttr(const VisibilityAttr *VA) {
  JOS.attribute("visibility", VisibilityAttr::ConvertVisibilityTypeToStr(
                                  VA->getVisibility()));
}

void JSONNodeDumper::VisitTLSModelAttr(const TLSModelAttr *TA) {
  JOS.attribute("tls_model", TA->getModel());
}

void JSONNodeDumper::VisitTypedefType(const TypedefType *TT) {
  JOS.attribute("decl", createBareDeclRef(TT->getDecl()));
  if (!TT->typeMatchesDecl())
    JOS.attribute("type", createQualType(TT->desugar()));
}

void JSONNodeDumper::VisitUsingType(const UsingType *TT) {
  JOS.attribute("decl", createBareDeclRef(TT->getFoundDecl()));
  if (!TT->typeMatchesDecl())
    JOS.attribute("type", createQualType(TT->desugar()));
}

void JSONNodeDumper::VisitFunctionType(const FunctionType *T) {
  FunctionType::ExtInfo E = T->getExtInfo();
  attributeOnlyIfTrue("noreturn", E.getNoReturn());
  attributeOnlyIfTrue("producesResult", E.getProducesResult());
  if (E.getHasRegParm())
    JOS.attribute("regParm", E.getRegParm());
  JOS.attribute("cc", FunctionType::getNameForCallConv(E.getCC()));
}

void JSONNodeDumper::VisitFunctionProtoType(const FunctionProtoType *T) {
  FunctionProtoType::ExtProtoInfo E = T->getExtProtoInfo();
  attributeOnlyIfTrue("trailingReturn", E.HasTrailingReturn);
  attributeOnlyIfTrue("const", T->isConst());
  attributeOnlyIfTrue("volatile", T->isVolatile());
  attributeOnlyIfTrue("restrict", T->isRestrict());
  attributeOnlyIfTrue("variadic", E.Variadic);
  switch (E.RefQualifier) {
  case RQ_LValue: JOS.attribute("refQualifier", "&"); break;
  case RQ_RValue: JOS.attribute("refQualifier", "&&"); break;
  case RQ_None: break;
  }
  switch (E.ExceptionSpec.Type) {
  case EST_DynamicNone:
  case EST_Dynamic: {
    JOS.attribute("exceptionSpec", "throw");
    llvm::json::Array Types;
    for (QualType QT : E.ExceptionSpec.Exceptions)
      Types.push_back(createQualType(QT));
    JOS.attribute("exceptionTypes", std::move(Types));
  } break;
  case EST_MSAny:
    JOS.attribute("exceptionSpec", "throw");
    JOS.attribute("throwsAny", true);
    break;
  case EST_BasicNoexcept:
    JOS.attribute("exceptionSpec", "noexcept");
    break;
  case EST_NoexceptTrue:
  case EST_NoexceptFalse:
    JOS.attribute("exceptionSpec", "noexcept");
    JOS.attribute("conditionEvaluatesTo",
                E.ExceptionSpec.Type == EST_NoexceptTrue);
    //JOS.attributeWithCall("exceptionSpecExpr",
    //                    [this, E]() { Visit(E.ExceptionSpec.NoexceptExpr); });
    break;
  case EST_NoThrow:
    JOS.attribute("exceptionSpec", "nothrow");
    break;
  // FIXME: I cannot find a way to trigger these cases while dumping the AST. I
  // suspect you can only run into them when executing an AST dump from within
  // the debugger, which is not a use case we worry about for the JSON dumping
  // feature.
  case EST_DependentNoexcept:
  case EST_Unevaluated:
  case EST_Uninstantiated:
  case EST_Unparsed:
  case EST_None: break;
  }
  VisitFunctionType(T);
}

void JSONNodeDumper::VisitRValueReferenceType(const ReferenceType *RT) {
  attributeOnlyIfTrue("spelledAsLValue", RT->isSpelledAsLValue());
}

void JSONNodeDumper::VisitArrayType(const ArrayType *AT) {
  switch (AT->getSizeModifier()) {
  case ArraySizeModifier::Star:
    JOS.attribute("sizeModifier", "*");
    break;
  case ArraySizeModifier::Static:
    JOS.attribute("sizeModifier", "static");
    break;
  case ArraySizeModifier::Normal:
    break;
  }

  std::string Str = AT->getIndexTypeQualifiers().getAsString();
  if (!Str.empty())
    JOS.attribute("indexTypeQualifiers", Str);
}

void JSONNodeDumper::VisitConstantArrayType(const ConstantArrayType *CAT) {
  // FIXME: this should use ZExt instead of SExt, but JSON doesn't allow a
  // narrowing conversion to int64_t so it cannot be expressed.
  JOS.attribute("size", CAT->getSExtSize());
  VisitArrayType(CAT);
}

void JSONNodeDumper::VisitDependentSizedExtVectorType(
    const DependentSizedExtVectorType *VT) {
  JOS.attributeObject(
      "attrLoc", [VT, this] { writeSourceLocation(VT->getAttributeLoc()); });
}

void JSONNodeDumper::VisitVectorType(const VectorType *VT) {
  JOS.attribute("numElements", VT->getNumElements());
  switch (VT->getVectorKind()) {
  case VectorKind::Generic:
    break;
  case VectorKind::AltiVecVector:
    JOS.attribute("vectorKind", "altivec");
    break;
  case VectorKind::AltiVecPixel:
    JOS.attribute("vectorKind", "altivec pixel");
    break;
  case VectorKind::AltiVecBool:
    JOS.attribute("vectorKind", "altivec bool");
    break;
  case VectorKind::Neon:
    JOS.attribute("vectorKind", "neon");
    break;
  case VectorKind::NeonPoly:
    JOS.attribute("vectorKind", "neon poly");
    break;
  case VectorKind::SveFixedLengthData:
    JOS.attribute("vectorKind", "fixed-length sve data vector");
    break;
  case VectorKind::SveFixedLengthPredicate:
    JOS.attribute("vectorKind", "fixed-length sve predicate vector");
    break;
  case VectorKind::RVVFixedLengthData:
    JOS.attribute("vectorKind", "fixed-length rvv data vector");
    break;
  case VectorKind::RVVFixedLengthMask:
    JOS.attribute("vectorKind", "fixed-length rvv mask vector");
    break;
  }
}

void JSONNodeDumper::VisitUnresolvedUsingType(const UnresolvedUsingType *UUT) {
  JOS.attribute("decl", createBareDeclRef(UUT->getDecl()));
}

void JSONNodeDumper::VisitUnaryTransformType(const UnaryTransformType *UTT) {
  switch (UTT->getUTTKind()) {
#define TRANSFORM_TYPE_TRAIT_DEF(Enum, Trait)                                  \
  case UnaryTransformType::Enum:                                               \
    JOS.attribute("transformKind", #Trait);                                    \
    break;
#include "clang/Basic/TransformTypeTraits.def"
  }
}

void JSONNodeDumper::VisitTagType(const TagType *TT) {
  JOS.attribute("decl", createBareDeclRef(TT->getDecl()));
}

void JSONNodeDumper::VisitTemplateTypeParmType(
    const TemplateTypeParmType *TTPT) {
  JOS.attribute("depth", TTPT->getDepth());
  JOS.attribute("index", TTPT->getIndex());
  attributeOnlyIfTrue("isPack", TTPT->isParameterPack());
  JOS.attribute("decl", createBareDeclRef(TTPT->getDecl()));
}

void JSONNodeDumper::VisitSubstTemplateTypeParmType(
    const SubstTemplateTypeParmType *STTPT) {
  JOS.attribute("index", STTPT->getIndex());
  if (auto PackIndex = STTPT->getPackIndex())
    JOS.attribute("pack_index", *PackIndex);
}

void JSONNodeDumper::VisitSubstTemplateTypeParmPackType(
    const SubstTemplateTypeParmPackType *T) {
  JOS.attribute("index", T->getIndex());
}

void JSONNodeDumper::VisitAutoType(const AutoType *AT) {
  JOS.attribute("undeduced", !AT->isDeduced());
  switch (AT->getKeyword()) {
  case AutoTypeKeyword::Auto:
    JOS.attribute("typeKeyword", "auto");
    break;
  case AutoTypeKeyword::DecltypeAuto:
    JOS.attribute("typeKeyword", "decltype(auto)");
    break;
  case AutoTypeKeyword::GNUAutoType:
    JOS.attribute("typeKeyword", "__auto_type");
    break;
  }
}

void JSONNodeDumper::VisitTemplateSpecializationType(
    const TemplateSpecializationType *TST) {
  attributeOnlyIfTrue("isAlias", TST->isTypeAlias());

  std::string Str;
  llvm::raw_string_ostream OS(Str);
  TST->getTemplateName().print(OS, PrintPolicy);
  JOS.attribute("templateName", Str);
}

void JSONNodeDumper::VisitInjectedClassNameType(
    const InjectedClassNameType *ICNT) {
  JOS.attribute("decl", createBareDeclRef(ICNT->getDecl()));
}

void JSONNodeDumper::VisitObjCInterfaceType(const ObjCInterfaceType *OIT) {
  JOS.attribute("decl", createBareDeclRef(OIT->getDecl()));
}

void JSONNodeDumper::VisitPackExpansionType(const PackExpansionType *PET) {
  if (std::optional<unsigned> N = PET->getNumExpansions())
    JOS.attribute("numExpansions", *N);
}

void JSONNodeDumper::VisitElaboratedType(const ElaboratedType *ET) {
  if (const NestedNameSpecifier *NNS = ET->getQualifier()) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    NNS->print(OS, PrintPolicy, /*ResolveTemplateArgs*/ true);
    JOS.attribute("qualifier", Str);
  }
  if (const TagDecl *TD = ET->getOwnedTagDecl())
    JOS.attribute("ownedTagDecl", createBareDeclRef(TD));
}

void JSONNodeDumper::VisitMacroQualifiedType(const MacroQualifiedType *MQT) {
  JOS.attribute("macroName", MQT->getMacroIdentifier()->getName());
}

void JSONNodeDumper::VisitMemberPointerType(const MemberPointerType *MPT) {
  attributeOnlyIfTrue("isData", MPT->isMemberDataPointer());
  attributeOnlyIfTrue("isFunction", MPT->isMemberFunctionPointer());
}

void JSONNodeDumper::VisitNamedDecl(const NamedDecl *ND) {
  if (ND && ND->getDeclName()) {
    JOS.attribute("name", ND->getNameAsString());
    // FIXME: There are likely other contexts in which it makes no sense to ask
    // for a mangled name.
    if (isa<RequiresExprBodyDecl>(ND->getDeclContext()))
      return;

    // If the declaration is dependent or is in a dependent context, then the
    // mangling is unlikely to be meaningful (and in some cases may cause
    // "don't know how to mangle this" assertion failures.
    if (ND->isTemplated())
      return;

    // Mangled names are not meaningful for locals, and may not be well-defined
    // in the case of VLAs.
    auto *VD = dyn_cast<VarDecl>(ND);
    if (VD && VD->hasLocalStorage())
      return;

    // Do not mangle template deduction guides.
    if (isa<CXXDeductionGuideDecl>(ND))
      return;

    std::string MangledName = ASTNameGen.getName(ND);
    if (!MangledName.empty())
      JOS.attribute("mangledName", MangledName);
  }
}

void JSONNodeDumper::VisitTypedefDecl(const TypedefDecl *TD) {
  VisitNamedDecl(TD);
  JOS.attribute("type", createQualType(TD->getUnderlyingType()));
}

void JSONNodeDumper::VisitTypeAliasDecl(const TypeAliasDecl *TAD) {
  VisitNamedDecl(TAD);
  JOS.attribute("type", createQualType(TAD->getUnderlyingType()));
}

void JSONNodeDumper::VisitNamespaceDecl(const NamespaceDecl *ND) {
  VisitNamedDecl(ND);
  attributeOnlyIfTrue("isInline", ND->isInline());
  attributeOnlyIfTrue("isNested", ND->isNested());
  if (!ND->isFirstDecl())
    JOS.attribute("originalNamespace", createBareDeclRef(ND->getFirstDecl()));
}

void JSONNodeDumper::VisitUsingDirectiveDecl(const UsingDirectiveDecl *UDD) {
  JOS.attribute("nominatedNamespace",
                createBareDeclRef(UDD->getNominatedNamespace()));
}

void JSONNodeDumper::VisitNamespaceAliasDecl(const NamespaceAliasDecl *NAD) {
  VisitNamedDecl(NAD);
  JOS.attribute("aliasedNamespace",
                createBareDeclRef(NAD->getAliasedNamespace()));
}

void JSONNodeDumper::VisitUsingDecl(const UsingDecl *UD) {
  std::string Name;
  if (const NestedNameSpecifier *NNS = UD->getQualifier()) {
    llvm::raw_string_ostream SOS(Name);
    NNS->print(SOS, UD->getASTContext().getPrintingPolicy());
  }
  Name += UD->getNameAsString();
  JOS.attribute("name", Name);
}

void JSONNodeDumper::VisitUsingEnumDecl(const UsingEnumDecl *UED) {
  JOS.attribute("target", createBareDeclRef(UED->getEnumDecl()));
}

void JSONNodeDumper::VisitUsingShadowDecl(const UsingShadowDecl *USD) {
  JOS.attribute("target", createBareDeclRef(USD->getTargetDecl()));
}

void JSONNodeDumper::VisitVarDecl(const VarDecl *VD) {
  VisitNamedDecl(VD);
  JOS.attribute("type", createQualType(VD->getType()));
  if (const auto *P = dyn_cast<ParmVarDecl>(VD))
    attributeOnlyIfTrue("explicitObjectParameter",
                        P->isExplicitObjectParameter());

  StorageClass SC = VD->getStorageClass();
  if (SC != SC_None)
    JOS.attribute("storageClass", VarDecl::getStorageClassSpecifierString(SC));
  switch (VD->getTLSKind()) {
  case VarDecl::TLS_Dynamic: JOS.attribute("tls", "dynamic"); break;
  case VarDecl::TLS_Static: JOS.attribute("tls", "static"); break;
  case VarDecl::TLS_None: break;
  }
  attributeOnlyIfTrue("nrvo", VD->isNRVOVariable());
  attributeOnlyIfTrue("inline", VD->isInline());
  attributeOnlyIfTrue("constexpr", VD->isConstexpr());
  attributeOnlyIfTrue("modulePrivate", VD->isModulePrivate());
  if (VD->hasInit()) {
    switch (VD->getInitStyle()) {
    case VarDecl::CInit: JOS.attribute("init", "c");  break;
    case VarDecl::CallInit: JOS.attribute("init", "call"); break;
    case VarDecl::ListInit: JOS.attribute("init", "list"); break;
    case VarDecl::ParenListInit:
      JOS.attribute("init", "paren-list");
      break;
    }
  }
  attributeOnlyIfTrue("isParameterPack", VD->isParameterPack());
}

void JSONNodeDumper::VisitFieldDecl(const FieldDecl *FD) {
  VisitNamedDecl(FD);
  JOS.attribute("type", createQualType(FD->getType()));
  attributeOnlyIfTrue("mutable", FD->isMutable());
  attributeOnlyIfTrue("modulePrivate", FD->isModulePrivate());
  attributeOnlyIfTrue("isBitfield", FD->isBitField());
  attributeOnlyIfTrue("hasInClassInitializer", FD->hasInClassInitializer());
}

void JSONNodeDumper::VisitFunctionDecl(const FunctionDecl *FD) {
  VisitNamedDecl(FD);
  JOS.attribute("type", createQualType(FD->getType()));
  StorageClass SC = FD->getStorageClass();
  if (SC != SC_None)
    JOS.attribute("storageClass", VarDecl::getStorageClassSpecifierString(SC));
  attributeOnlyIfTrue("inline", FD->isInlineSpecified());
  attributeOnlyIfTrue("virtual", FD->isVirtualAsWritten());
  attributeOnlyIfTrue("pure", FD->isPureVirtual());
  attributeOnlyIfTrue("explicitlyDeleted", FD->isDeletedAsWritten());
  attributeOnlyIfTrue("constexpr", FD->isConstexpr());
  attributeOnlyIfTrue("variadic", FD->isVariadic());
  attributeOnlyIfTrue("immediate", FD->isImmediateFunction());

  if (FD->isDefaulted())
    JOS.attribute("explicitlyDefaulted",
                  FD->isDeleted() ? "deleted" : "default");

  if (StringLiteral *Msg = FD->getDeletedMessage())
    JOS.attribute("deletedMessage", Msg->getString());
}

void JSONNodeDumper::VisitEnumDecl(const EnumDecl *ED) {
  VisitNamedDecl(ED);
  if (ED->isFixed())
    JOS.attribute("fixedUnderlyingType", createQualType(ED->getIntegerType()));
  if (ED->isScoped())
    JOS.attribute("scopedEnumTag",
                  ED->isScopedUsingClassTag() ? "class" : "struct");
}
void JSONNodeDumper::VisitEnumConstantDecl(const EnumConstantDecl *ECD) {
  VisitNamedDecl(ECD);
  JOS.attribute("type", createQualType(ECD->getType()));
}

void JSONNodeDumper::VisitRecordDecl(const RecordDecl *RD) {
  VisitNamedDecl(RD);
  JOS.attribute("tagUsed", RD->getKindName());
  attributeOnlyIfTrue("completeDefinition", RD->isCompleteDefinition());
}
void JSONNodeDumper::VisitCXXRecordDecl(const CXXRecordDecl *RD) {
  VisitRecordDecl(RD);

  // All other information requires a complete definition.
  if (!RD->isCompleteDefinition())
    return;

  JOS.attribute("definitionData", createCXXRecordDefinitionData(RD));
  if (RD->getNumBases()) {
    JOS.attributeArray("bases", [this, RD] {
      for (const auto &Spec : RD->bases())
        JOS.value(createCXXBaseSpecifier(Spec));
    });
  }
}

void JSONNodeDumper::VisitHLSLBufferDecl(const HLSLBufferDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("bufferKind", D->isCBuffer() ? "cbuffer" : "tbuffer");
}

void JSONNodeDumper::VisitTemplateTypeParmDecl(const TemplateTypeParmDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("tagUsed", D->wasDeclaredWithTypename() ? "typename" : "class");
  JOS.attribute("depth", D->getDepth());
  JOS.attribute("index", D->getIndex());
  attributeOnlyIfTrue("isParameterPack", D->isParameterPack());

  if (D->hasDefaultArgument())
    JOS.attributeObject("defaultArg", [=] {
      Visit(D->getDefaultArgument().getArgument(), SourceRange(),
            D->getDefaultArgStorage().getInheritedFrom(),
            D->defaultArgumentWasInherited() ? "inherited from" : "previous");
    });
}

void JSONNodeDumper::VisitNonTypeTemplateParmDecl(
    const NonTypeTemplateParmDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("type", createQualType(D->getType()));
  JOS.attribute("depth", D->getDepth());
  JOS.attribute("index", D->getIndex());
  attributeOnlyIfTrue("isParameterPack", D->isParameterPack());

  if (D->hasDefaultArgument())
    JOS.attributeObject("defaultArg", [=] {
      Visit(D->getDefaultArgument().getArgument(), SourceRange(),
            D->getDefaultArgStorage().getInheritedFrom(),
            D->defaultArgumentWasInherited() ? "inherited from" : "previous");
    });
}

void JSONNodeDumper::VisitTemplateTemplateParmDecl(
    const TemplateTemplateParmDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("depth", D->getDepth());
  JOS.attribute("index", D->getIndex());
  attributeOnlyIfTrue("isParameterPack", D->isParameterPack());

  if (D->hasDefaultArgument())
    JOS.attributeObject("defaultArg", [=] {
      const auto *InheritedFrom = D->getDefaultArgStorage().getInheritedFrom();
      Visit(D->getDefaultArgument().getArgument(),
            InheritedFrom ? InheritedFrom->getSourceRange() : SourceLocation{},
            InheritedFrom,
            D->defaultArgumentWasInherited() ? "inherited from" : "previous");
    });
}

void JSONNodeDumper::VisitLinkageSpecDecl(const LinkageSpecDecl *LSD) {
  StringRef Lang;
  switch (LSD->getLanguage()) {
  case LinkageSpecLanguageIDs::C:
    Lang = "C";
    break;
  case LinkageSpecLanguageIDs::CXX:
    Lang = "C++";
    break;
  }
  JOS.attribute("language", Lang);
  attributeOnlyIfTrue("hasBraces", LSD->hasBraces());
}

void JSONNodeDumper::VisitAccessSpecDecl(const AccessSpecDecl *ASD) {
  JOS.attribute("access", createAccessSpecifier(ASD->getAccess()));
}

void JSONNodeDumper::VisitFriendDecl(const FriendDecl *FD) {
  if (const TypeSourceInfo *T = FD->getFriendType())
    JOS.attribute("type", createQualType(T->getType()));
}

void JSONNodeDumper::VisitObjCIvarDecl(const ObjCIvarDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("type", createQualType(D->getType()));
  attributeOnlyIfTrue("synthesized", D->getSynthesize());
  switch (D->getAccessControl()) {
  case ObjCIvarDecl::None: JOS.attribute("access", "none"); break;
  case ObjCIvarDecl::Private: JOS.attribute("access", "private"); break;
  case ObjCIvarDecl::Protected: JOS.attribute("access", "protected"); break;
  case ObjCIvarDecl::Public: JOS.attribute("access", "public"); break;
  case ObjCIvarDecl::Package: JOS.attribute("access", "package"); break;
  }
}

void JSONNodeDumper::VisitObjCMethodDecl(const ObjCMethodDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("returnType", createQualType(D->getReturnType()));
  JOS.attribute("instance", D->isInstanceMethod());
  attributeOnlyIfTrue("variadic", D->isVariadic());
}

void JSONNodeDumper::VisitObjCTypeParamDecl(const ObjCTypeParamDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("type", createQualType(D->getUnderlyingType()));
  attributeOnlyIfTrue("bounded", D->hasExplicitBound());
  switch (D->getVariance()) {
  case ObjCTypeParamVariance::Invariant:
    break;
  case ObjCTypeParamVariance::Covariant:
    JOS.attribute("variance", "covariant");
    break;
  case ObjCTypeParamVariance::Contravariant:
    JOS.attribute("variance", "contravariant");
    break;
  }
}

void JSONNodeDumper::VisitObjCCategoryDecl(const ObjCCategoryDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("interface", createBareDeclRef(D->getClassInterface()));
  JOS.attribute("implementation", createBareDeclRef(D->getImplementation()));

  llvm::json::Array Protocols;
  for (const auto* P : D->protocols())
    Protocols.push_back(createBareDeclRef(P));
  if (!Protocols.empty())
    JOS.attribute("protocols", std::move(Protocols));
}

void JSONNodeDumper::VisitObjCCategoryImplDecl(const ObjCCategoryImplDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("interface", createBareDeclRef(D->getClassInterface()));
  JOS.attribute("categoryDecl", createBareDeclRef(D->getCategoryDecl()));
}

void JSONNodeDumper::VisitObjCProtocolDecl(const ObjCProtocolDecl *D) {
  VisitNamedDecl(D);

  llvm::json::Array Protocols;
  for (const auto *P : D->protocols())
    Protocols.push_back(createBareDeclRef(P));
  if (!Protocols.empty())
    JOS.attribute("protocols", std::move(Protocols));
}

void JSONNodeDumper::VisitObjCInterfaceDecl(const ObjCInterfaceDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("super", createBareDeclRef(D->getSuperClass()));
  JOS.attribute("implementation", createBareDeclRef(D->getImplementation()));

  llvm::json::Array Protocols;
  for (const auto* P : D->protocols())
    Protocols.push_back(createBareDeclRef(P));
  if (!Protocols.empty())
    JOS.attribute("protocols", std::move(Protocols));
}

void JSONNodeDumper::VisitObjCImplementationDecl(
    const ObjCImplementationDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("super", createBareDeclRef(D->getSuperClass()));
  JOS.attribute("interface", createBareDeclRef(D->getClassInterface()));
}

void JSONNodeDumper::VisitObjCCompatibleAliasDecl(
    const ObjCCompatibleAliasDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("interface", createBareDeclRef(D->getClassInterface()));
}

void JSONNodeDumper::VisitObjCPropertyDecl(const ObjCPropertyDecl *D) {
  VisitNamedDecl(D);
  JOS.attribute("type", createQualType(D->getType()));

  switch (D->getPropertyImplementation()) {
  case ObjCPropertyDecl::None: break;
  case ObjCPropertyDecl::Required: JOS.attribute("control", "required"); break;
  case ObjCPropertyDecl::Optional: JOS.attribute("control", "optional"); break;
  }

  ObjCPropertyAttribute::Kind Attrs = D->getPropertyAttributes();
  if (Attrs != ObjCPropertyAttribute::kind_noattr) {
    if (Attrs & ObjCPropertyAttribute::kind_getter)
      JOS.attribute("getter", createBareDeclRef(D->getGetterMethodDecl()));
    if (Attrs & ObjCPropertyAttribute::kind_setter)
      JOS.attribute("setter", createBareDeclRef(D->getSetterMethodDecl()));
    attributeOnlyIfTrue("readonly",
                        Attrs & ObjCPropertyAttribute::kind_readonly);
    attributeOnlyIfTrue("assign", Attrs & ObjCPropertyAttribute::kind_assign);
    attributeOnlyIfTrue("readwrite",
                        Attrs & ObjCPropertyAttribute::kind_readwrite);
    attributeOnlyIfTrue("retain", Attrs & ObjCPropertyAttribute::kind_retain);
    attributeOnlyIfTrue("copy", Attrs & ObjCPropertyAttribute::kind_copy);
    attributeOnlyIfTrue("nonatomic",
                        Attrs & ObjCPropertyAttribute::kind_nonatomic);
    attributeOnlyIfTrue("atomic", Attrs & ObjCPropertyAttribute::kind_atomic);
    attributeOnlyIfTrue("weak", Attrs & ObjCPropertyAttribute::kind_weak);
    attributeOnlyIfTrue("strong", Attrs & ObjCPropertyAttribute::kind_strong);
    attributeOnlyIfTrue("unsafe_unretained",
                        Attrs & ObjCPropertyAttribute::kind_unsafe_unretained);
    attributeOnlyIfTrue("class", Attrs & ObjCPropertyAttribute::kind_class);
    attributeOnlyIfTrue("direct", Attrs & ObjCPropertyAttribute::kind_direct);
    attributeOnlyIfTrue("nullability",
                        Attrs & ObjCPropertyAttribute::kind_nullability);
    attributeOnlyIfTrue("null_resettable",
                        Attrs & ObjCPropertyAttribute::kind_null_resettable);
  }
}

void JSONNodeDumper::VisitObjCPropertyImplDecl(const ObjCPropertyImplDecl *D) {
  VisitNamedDecl(D->getPropertyDecl());
  JOS.attribute("implKind", D->getPropertyImplementation() ==
                                    ObjCPropertyImplDecl::Synthesize
                                ? "synthesize"
                                : "dynamic");
  JOS.attribute("propertyDecl", createBareDeclRef(D->getPropertyDecl()));
  JOS.attribute("ivarDecl", createBareDeclRef(D->getPropertyIvarDecl()));
}

void JSONNodeDumper::VisitBlockDecl(const BlockDecl *D) {
  attributeOnlyIfTrue("variadic", D->isVariadic());
  attributeOnlyIfTrue("capturesThis", D->capturesCXXThis());
}

void JSONNodeDumper::VisitAtomicExpr(const AtomicExpr *AE) {
  JOS.attribute("name", AE->getOpAsString());
}

void JSONNodeDumper::VisitObjCEncodeExpr(const ObjCEncodeExpr *OEE) {
  JOS.attribute("encodedType", createQualType(OEE->getEncodedType()));
}

void JSONNodeDumper::VisitObjCMessageExpr(const ObjCMessageExpr *OME) {
  std::string Str;
  llvm::raw_string_ostream OS(Str);

  OME->getSelector().print(OS);
  JOS.attribute("selector", Str);

  switch (OME->getReceiverKind()) {
  case ObjCMessageExpr::Instance:
    JOS.attribute("receiverKind", "instance");
    break;
  case ObjCMessageExpr::Class:
    JOS.attribute("receiverKind", "class");
    JOS.attribute("classType", createQualType(OME->getClassReceiver()));
    break;
  case ObjCMessageExpr::SuperInstance:
    JOS.attribute("receiverKind", "super (instance)");
    JOS.attribute("superType", createQualType(OME->getSuperType()));
    break;
  case ObjCMessageExpr::SuperClass:
    JOS.attribute("receiverKind", "super (class)");
    JOS.attribute("superType", createQualType(OME->getSuperType()));
    break;
  }

  QualType CallReturnTy = OME->getCallReturnType(Ctx);
  if (OME->getType() != CallReturnTy)
    JOS.attribute("callReturnType", createQualType(CallReturnTy));
}

void JSONNodeDumper::VisitObjCBoxedExpr(const ObjCBoxedExpr *OBE) {
  if (const ObjCMethodDecl *MD = OBE->getBoxingMethod()) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);

    MD->getSelector().print(OS);
    JOS.attribute("selector", Str);
  }
}

void JSONNodeDumper::VisitObjCSelectorExpr(const ObjCSelectorExpr *OSE) {
  std::string Str;
  llvm::raw_string_ostream OS(Str);

  OSE->getSelector().print(OS);
  JOS.attribute("selector", Str);
}

void JSONNodeDumper::VisitObjCProtocolExpr(const ObjCProtocolExpr *OPE) {
  JOS.attribute("protocol", createBareDeclRef(OPE->getProtocol()));
}

void JSONNodeDumper::VisitObjCPropertyRefExpr(const ObjCPropertyRefExpr *OPRE) {
  if (OPRE->isImplicitProperty()) {
    JOS.attribute("propertyKind", "implicit");
    if (const ObjCMethodDecl *MD = OPRE->getImplicitPropertyGetter())
      JOS.attribute("getter", createBareDeclRef(MD));
    if (const ObjCMethodDecl *MD = OPRE->getImplicitPropertySetter())
      JOS.attribute("setter", createBareDeclRef(MD));
  } else {
    JOS.attribute("propertyKind", "explicit");
    JOS.attribute("property", createBareDeclRef(OPRE->getExplicitProperty()));
  }

  attributeOnlyIfTrue("isSuperReceiver", OPRE->isSuperReceiver());
  attributeOnlyIfTrue("isMessagingGetter", OPRE->isMessagingGetter());
  attributeOnlyIfTrue("isMessagingSetter", OPRE->isMessagingSetter());
}

void JSONNodeDumper::VisitObjCSubscriptRefExpr(
    const ObjCSubscriptRefExpr *OSRE) {
  JOS.attribute("subscriptKind",
                OSRE->isArraySubscriptRefExpr() ? "array" : "dictionary");

  if (const ObjCMethodDecl *MD = OSRE->getAtIndexMethodDecl())
    JOS.attribute("getter", createBareDeclRef(MD));
  if (const ObjCMethodDecl *MD = OSRE->setAtIndexMethodDecl())
    JOS.attribute("setter", createBareDeclRef(MD));
}

void JSONNodeDumper::VisitObjCIvarRefExpr(const ObjCIvarRefExpr *OIRE) {
  JOS.attribute("decl", createBareDeclRef(OIRE->getDecl()));
  attributeOnlyIfTrue("isFreeIvar", OIRE->isFreeIvar());
  JOS.attribute("isArrow", OIRE->isArrow());
}

void JSONNodeDumper::VisitObjCBoolLiteralExpr(const ObjCBoolLiteralExpr *OBLE) {
  JOS.attribute("value", OBLE->getValue() ? "__objc_yes" : "__objc_no");
}

void JSONNodeDumper::VisitDeclRefExpr(const DeclRefExpr *DRE) {
  JOS.attribute("referencedDecl", createBareDeclRef(DRE->getDecl()));
  if (DRE->getDecl() != DRE->getFoundDecl())
    JOS.attribute("foundReferencedDecl",
                  createBareDeclRef(DRE->getFoundDecl()));
  switch (DRE->isNonOdrUse()) {
  case NOUR_None: break;
  case NOUR_Unevaluated: JOS.attribute("nonOdrUseReason", "unevaluated"); break;
  case NOUR_Constant: JOS.attribute("nonOdrUseReason", "constant"); break;
  case NOUR_Discarded: JOS.attribute("nonOdrUseReason", "discarded"); break;
  }
  attributeOnlyIfTrue("isImmediateEscalating", DRE->isImmediateEscalating());
}

void JSONNodeDumper::VisitSYCLUniqueStableNameExpr(
    const SYCLUniqueStableNameExpr *E) {
  JOS.attribute("typeSourceInfo",
                createQualType(E->getTypeSourceInfo()->getType()));
}

void JSONNodeDumper::VisitPredefinedExpr(const PredefinedExpr *PE) {
  JOS.attribute("name", PredefinedExpr::getIdentKindName(PE->getIdentKind()));
}

void JSONNodeDumper::VisitUnaryOperator(const UnaryOperator *UO) {
  JOS.attribute("isPostfix", UO->isPostfix());
  JOS.attribute("opcode", UnaryOperator::getOpcodeStr(UO->getOpcode()));
  if (!UO->canOverflow())
    JOS.attribute("canOverflow", false);
}

void JSONNodeDumper::VisitBinaryOperator(const BinaryOperator *BO) {
  JOS.attribute("opcode", BinaryOperator::getOpcodeStr(BO->getOpcode()));
}

void JSONNodeDumper::VisitCompoundAssignOperator(
    const CompoundAssignOperator *CAO) {
  VisitBinaryOperator(CAO);
  JOS.attribute("computeLHSType", createQualType(CAO->getComputationLHSType()));
  JOS.attribute("computeResultType",
                createQualType(CAO->getComputationResultType()));
}

void JSONNodeDumper::VisitMemberExpr(const MemberExpr *ME) {
  // Note, we always write this Boolean field because the information it conveys
  // is critical to understanding the AST node.
  ValueDecl *VD = ME->getMemberDecl();
  JOS.attribute("name", VD && VD->getDeclName() ? VD->getNameAsString() : "");
  JOS.attribute("isArrow", ME->isArrow());
  JOS.attribute("referencedMemberDecl", createPointerRepresentation(VD));
  switch (ME->isNonOdrUse()) {
  case NOUR_None: break;
  case NOUR_Unevaluated: JOS.attribute("nonOdrUseReason", "unevaluated"); break;
  case NOUR_Constant: JOS.attribute("nonOdrUseReason", "constant"); break;
  case NOUR_Discarded: JOS.attribute("nonOdrUseReason", "discarded"); break;
  }
}

void JSONNodeDumper::VisitCXXNewExpr(const CXXNewExpr *NE) {
  attributeOnlyIfTrue("isGlobal", NE->isGlobalNew());
  attributeOnlyIfTrue("isArray", NE->isArray());
  attributeOnlyIfTrue("isPlacement", NE->getNumPlacementArgs() != 0);
  switch (NE->getInitializationStyle()) {
  case CXXNewInitializationStyle::None:
    break;
  case CXXNewInitializationStyle::Parens:
    JOS.attribute("initStyle", "call");
    break;
  case CXXNewInitializationStyle::Braces:
    JOS.attribute("initStyle", "list");
    break;
  }
  if (const FunctionDecl *FD = NE->getOperatorNew())
    JOS.attribute("operatorNewDecl", createBareDeclRef(FD));
  if (const FunctionDecl *FD = NE->getOperatorDelete())
    JOS.attribute("operatorDeleteDecl", createBareDeclRef(FD));
}
void JSONNodeDumper::VisitCXXDeleteExpr(const CXXDeleteExpr *DE) {
  attributeOnlyIfTrue("isGlobal", DE->isGlobalDelete());
  attributeOnlyIfTrue("isArray", DE->isArrayForm());
  attributeOnlyIfTrue("isArrayAsWritten", DE->isArrayFormAsWritten());
  if (const FunctionDecl *FD = DE->getOperatorDelete())
    JOS.attribute("operatorDeleteDecl", createBareDeclRef(FD));
}

void JSONNodeDumper::VisitCXXThisExpr(const CXXThisExpr *TE) {
  attributeOnlyIfTrue("implicit", TE->isImplicit());
}

void JSONNodeDumper::VisitCastExpr(const CastExpr *CE) {
  JOS.attribute("castKind", CE->getCastKindName());
  llvm::json::Array Path = createCastPath(CE);
  if (!Path.empty())
    JOS.attribute("path", std::move(Path));
  // FIXME: This may not be useful information as it can be obtusely gleaned
  // from the inner[] array.
  if (const NamedDecl *ND = CE->getConversionFunction())
    JOS.attribute("conversionFunc", createBareDeclRef(ND));
}

void JSONNodeDumper::VisitImplicitCastExpr(const ImplicitCastExpr *ICE) {
  VisitCastExpr(ICE);
  attributeOnlyIfTrue("isPartOfExplicitCast", ICE->isPartOfExplicitCast());
}

void JSONNodeDumper::VisitCallExpr(const CallExpr *CE) {
  attributeOnlyIfTrue("adl", CE->usesADL());
}

void JSONNodeDumper::VisitUnaryExprOrTypeTraitExpr(
    const UnaryExprOrTypeTraitExpr *TTE) {
  JOS.attribute("name", getTraitSpelling(TTE->getKind()));
  if (TTE->isArgumentType())
    JOS.attribute("argType", createQualType(TTE->getArgumentType()));
}

void JSONNodeDumper::VisitSizeOfPackExpr(const SizeOfPackExpr *SOPE) {
  VisitNamedDecl(SOPE->getPack());
}

void JSONNodeDumper::VisitUnresolvedLookupExpr(
    const UnresolvedLookupExpr *ULE) {
  JOS.attribute("usesADL", ULE->requiresADL());
  JOS.attribute("name", ULE->getName().getAsString());

  JOS.attributeArray("lookups", [this, ULE] {
    for (const NamedDecl *D : ULE->decls())
      JOS.value(createBareDeclRef(D));
  });
}

void JSONNodeDumper::VisitAddrLabelExpr(const AddrLabelExpr *ALE) {
  JOS.attribute("name", ALE->getLabel()->getName());
  JOS.attribute("labelDeclId", createPointerRepresentation(ALE->getLabel()));
}

void JSONNodeDumper::VisitCXXTypeidExpr(const CXXTypeidExpr *CTE) {
  if (CTE->isTypeOperand()) {
    QualType Adjusted = CTE->getTypeOperand(Ctx);
    QualType Unadjusted = CTE->getTypeOperandSourceInfo()->getType();
    JOS.attribute("typeArg", createQualType(Unadjusted));
    if (Adjusted != Unadjusted)
      JOS.attribute("adjustedTypeArg", createQualType(Adjusted));
  }
}

void JSONNodeDumper::VisitConstantExpr(const ConstantExpr *CE) {
  if (CE->getResultAPValueKind() != APValue::None)
    Visit(CE->getAPValueResult(), CE->getType());
}

void JSONNodeDumper::VisitInitListExpr(const InitListExpr *ILE) {
  if (const FieldDecl *FD = ILE->getInitializedFieldInUnion())
    JOS.attribute("field", createBareDeclRef(FD));
}

void JSONNodeDumper::VisitGenericSelectionExpr(
    const GenericSelectionExpr *GSE) {
  attributeOnlyIfTrue("resultDependent", GSE->isResultDependent());
}

void JSONNodeDumper::VisitCXXUnresolvedConstructExpr(
    const CXXUnresolvedConstructExpr *UCE) {
  if (UCE->getType() != UCE->getTypeAsWritten())
    JOS.attribute("typeAsWritten", createQualType(UCE->getTypeAsWritten()));
  attributeOnlyIfTrue("list", UCE->isListInitialization());
}

void JSONNodeDumper::VisitCXXConstructExpr(const CXXConstructExpr *CE) {
  CXXConstructorDecl *Ctor = CE->getConstructor();
  JOS.attribute("ctorType", createQualType(Ctor->getType()));
  attributeOnlyIfTrue("elidable", CE->isElidable());
  attributeOnlyIfTrue("list", CE->isListInitialization());
  attributeOnlyIfTrue("initializer_list", CE->isStdInitListInitialization());
  attributeOnlyIfTrue("zeroing", CE->requiresZeroInitialization());
  attributeOnlyIfTrue("hadMultipleCandidates", CE->hadMultipleCandidates());
  attributeOnlyIfTrue("isImmediateEscalating", CE->isImmediateEscalating());

  switch (CE->getConstructionKind()) {
  case CXXConstructionKind::Complete:
    JOS.attribute("constructionKind", "complete");
    break;
  case CXXConstructionKind::Delegating:
    JOS.attribute("constructionKind", "delegating");
    break;
  case CXXConstructionKind::NonVirtualBase:
    JOS.attribute("constructionKind", "non-virtual base");
    break;
  case CXXConstructionKind::VirtualBase:
    JOS.attribute("constructionKind", "virtual base");
    break;
  }
}

void JSONNodeDumper::VisitExprWithCleanups(const ExprWithCleanups *EWC) {
  attributeOnlyIfTrue("cleanupsHaveSideEffects",
                      EWC->cleanupsHaveSideEffects());
  if (EWC->getNumObjects()) {
    JOS.attributeArray("cleanups", [this, EWC] {
      for (const ExprWithCleanups::CleanupObject &CO : EWC->getObjects())
        if (auto *BD = CO.dyn_cast<BlockDecl *>()) {
          JOS.value(createBareDeclRef(BD));
        } else if (auto *CLE = CO.dyn_cast<CompoundLiteralExpr *>()) {
          llvm::json::Object Obj;
          Obj["id"] = createPointerRepresentation(CLE);
          Obj["kind"] = CLE->getStmtClassName();
          JOS.value(std::move(Obj));
        } else {
          llvm_unreachable("unexpected cleanup object type");
        }
    });
  }
}

void JSONNodeDumper::VisitCXXBindTemporaryExpr(
    const CXXBindTemporaryExpr *BTE) {
  const CXXTemporary *Temp = BTE->getTemporary();
  JOS.attribute("temp", createPointerRepresentation(Temp));
  if (const CXXDestructorDecl *Dtor = Temp->getDestructor())
    JOS.attribute("dtor", createBareDeclRef(Dtor));
}

void JSONNodeDumper::VisitMaterializeTemporaryExpr(
    const MaterializeTemporaryExpr *MTE) {
  if (const ValueDecl *VD = MTE->getExtendingDecl())
    JOS.attribute("extendingDecl", createBareDeclRef(VD));

  switch (MTE->getStorageDuration()) {
  case SD_Automatic:
    JOS.attribute("storageDuration", "automatic");
    break;
  case SD_Dynamic:
    JOS.attribute("storageDuration", "dynamic");
    break;
  case SD_FullExpression:
    JOS.attribute("storageDuration", "full expression");
    break;
  case SD_Static:
    JOS.attribute("storageDuration", "static");
    break;
  case SD_Thread:
    JOS.attribute("storageDuration", "thread");
    break;
  }

  attributeOnlyIfTrue("boundToLValueRef", MTE->isBoundToLvalueReference());
}

void JSONNodeDumper::VisitCXXDefaultArgExpr(const CXXDefaultArgExpr *Node) {
  attributeOnlyIfTrue("hasRewrittenInit", Node->hasRewrittenInit());
}

void JSONNodeDumper::VisitCXXDefaultInitExpr(const CXXDefaultInitExpr *Node) {
  attributeOnlyIfTrue("hasRewrittenInit", Node->hasRewrittenInit());
}

void JSONNodeDumper::VisitCXXDependentScopeMemberExpr(
    const CXXDependentScopeMemberExpr *DSME) {
  JOS.attribute("isArrow", DSME->isArrow());
  JOS.attribute("member", DSME->getMember().getAsString());
  attributeOnlyIfTrue("hasTemplateKeyword", DSME->hasTemplateKeyword());
  attributeOnlyIfTrue("hasExplicitTemplateArgs",
                      DSME->hasExplicitTemplateArgs());

  if (DSME->getNumTemplateArgs()) {
    JOS.attributeArray("explicitTemplateArgs", [DSME, this] {
      for (const TemplateArgumentLoc &TAL : DSME->template_arguments())
        JOS.object(
            [&TAL, this] { Visit(TAL.getArgument(), TAL.getSourceRange()); });
    });
  }
}

void JSONNodeDumper::VisitRequiresExpr(const RequiresExpr *RE) {
  if (!RE->isValueDependent())
    JOS.attribute("satisfied", RE->isSatisfied());
}

void JSONNodeDumper::VisitIntegerLiteral(const IntegerLiteral *IL) {
  llvm::SmallString<16> Buffer;
  IL->getValue().toString(Buffer,
                          /*Radix=*/10, IL->getType()->isSignedIntegerType());
  JOS.attribute("value", Buffer);
}
void JSONNodeDumper::VisitCharacterLiteral(const CharacterLiteral *CL) {
  // FIXME: This should probably print the character literal as a string,
  // rather than as a numerical value. It would be nice if the behavior matched
  // what we do to print a string literal; right now, it is impossible to tell
  // the difference between 'a' and L'a' in C from the JSON output.
  JOS.attribute("value", CL->getValue());
}
void JSONNodeDumper::VisitFixedPointLiteral(const FixedPointLiteral *FPL) {
  JOS.attribute("value", FPL->getValueAsString(/*Radix=*/10));
}
void JSONNodeDumper::VisitFloatingLiteral(const FloatingLiteral *FL) {
  llvm::SmallString<16> Buffer;
  FL->getValue().toString(Buffer);
  JOS.attribute("value", Buffer);
}
void JSONNodeDumper::VisitStringLiteral(const StringLiteral *SL) {
  std::string Buffer;
  llvm::raw_string_ostream SS(Buffer);
  SL->outputString(SS);
  JOS.attribute("value", Buffer);
}
void JSONNodeDumper::VisitCXXBoolLiteralExpr(const CXXBoolLiteralExpr *BLE) {
  JOS.attribute("value", BLE->getValue());
}

void JSONNodeDumper::VisitIfStmt(const IfStmt *IS) {
  attributeOnlyIfTrue("hasInit", IS->hasInitStorage());
  attributeOnlyIfTrue("hasVar", IS->hasVarStorage());
  attributeOnlyIfTrue("hasElse", IS->hasElseStorage());
  attributeOnlyIfTrue("isConstexpr", IS->isConstexpr());
  attributeOnlyIfTrue("isConsteval", IS->isConsteval());
  attributeOnlyIfTrue("constevalIsNegated", IS->isNegatedConsteval());
}

void JSONNodeDumper::VisitSwitchStmt(const SwitchStmt *SS) {
  attributeOnlyIfTrue("hasInit", SS->hasInitStorage());
  attributeOnlyIfTrue("hasVar", SS->hasVarStorage());
}
void JSONNodeDumper::VisitCaseStmt(const CaseStmt *CS) {
  attributeOnlyIfTrue("isGNURange", CS->caseStmtIsGNURange());
}

void JSONNodeDumper::VisitLabelStmt(const LabelStmt *LS) {
  JOS.attribute("name", LS->getName());
  JOS.attribute("declId", createPointerRepresentation(LS->getDecl()));
  attributeOnlyIfTrue("sideEntry", LS->isSideEntry());
}
void JSONNodeDumper::VisitGotoStmt(const GotoStmt *GS) {
  JOS.attribute("targetLabelDeclId",
                createPointerRepresentation(GS->getLabel()));
}

void JSONNodeDumper::VisitWhileStmt(const WhileStmt *WS) {
  attributeOnlyIfTrue("hasVar", WS->hasVarStorage());
}

void JSONNodeDumper::VisitObjCAtCatchStmt(const ObjCAtCatchStmt* OACS) {
  // FIXME: it would be nice for the ASTNodeTraverser would handle the catch
  // parameter the same way for C++ and ObjC rather. In this case, C++ gets a
  // null child node and ObjC gets no child node.
  attributeOnlyIfTrue("isCatchAll", OACS->getCatchParamDecl() == nullptr);
}

void JSONNodeDumper::VisitNullTemplateArgument(const TemplateArgument &TA) {
  JOS.attribute("isNull", true);
}
void JSONNodeDumper::VisitTypeTemplateArgument(const TemplateArgument &TA) {
  JOS.attribute("type", createQualType(TA.getAsType()));
}
void JSONNodeDumper::VisitDeclarationTemplateArgument(
    const TemplateArgument &TA) {
  JOS.attribute("decl", createBareDeclRef(TA.getAsDecl()));
}
void JSONNodeDumper::VisitNullPtrTemplateArgument(const TemplateArgument &TA) {
  JOS.attribute("isNullptr", true);
}
void JSONNodeDumper::VisitIntegralTemplateArgument(const TemplateArgument &TA) {
  JOS.attribute("value", TA.getAsIntegral().getSExtValue());
}
void JSONNodeDumper::VisitTemplateTemplateArgument(const TemplateArgument &TA) {
  // FIXME: cannot just call dump() on the argument, as that doesn't specify
  // the output format.
}
void JSONNodeDumper::VisitTemplateExpansionTemplateArgument(
    const TemplateArgument &TA) {
  // FIXME: cannot just call dump() on the argument, as that doesn't specify
  // the output format.
}
void JSONNodeDumper::VisitExpressionTemplateArgument(
    const TemplateArgument &TA) {
  JOS.attribute("isExpr", true);
}
void JSONNodeDumper::VisitPackTemplateArgument(const TemplateArgument &TA) {
  JOS.attribute("isPack", true);
}

StringRef JSONNodeDumper::getCommentCommandName(unsigned CommandID) const {
  if (Traits)
    return Traits->getCommandInfo(CommandID)->Name;
  if (const comments::CommandInfo *Info =
          comments::CommandTraits::getBuiltinCommandInfo(CommandID))
    return Info->Name;
  return "<invalid>";
}

void JSONNodeDumper::visitTextComment(const comments::TextComment *C,
                                      const comments::FullComment *) {
  JOS.attribute("text", C->getText());
}

void JSONNodeDumper::visitInlineCommandComment(
    const comments::InlineCommandComment *C, const comments::FullComment *) {
  JOS.attribute("name", getCommentCommandName(C->getCommandID()));

  switch (C->getRenderKind()) {
  case comments::InlineCommandRenderKind::Normal:
    JOS.attribute("renderKind", "normal");
    break;
  case comments::InlineCommandRenderKind::Bold:
    JOS.attribute("renderKind", "bold");
    break;
  case comments::InlineCommandRenderKind::Emphasized:
    JOS.attribute("renderKind", "emphasized");
    break;
  case comments::InlineCommandRenderKind::Monospaced:
    JOS.attribute("renderKind", "monospaced");
    break;
  case comments::InlineCommandRenderKind::Anchor:
    JOS.attribute("renderKind", "anchor");
    break;
  }

  llvm::json::Array Args;
  for (unsigned I = 0, E = C->getNumArgs(); I < E; ++I)
    Args.push_back(C->getArgText(I));

  if (!Args.empty())
    JOS.attribute("args", std::move(Args));
}

void JSONNodeDumper::visitHTMLStartTagComment(
    const comments::HTMLStartTagComment *C, const comments::FullComment *) {
  JOS.attribute("name", C->getTagName());
  attributeOnlyIfTrue("selfClosing", C->isSelfClosing());
  attributeOnlyIfTrue("malformed", C->isMalformed());

  llvm::json::Array Attrs;
  for (unsigned I = 0, E = C->getNumAttrs(); I < E; ++I)
    Attrs.push_back(
        {{"name", C->getAttr(I).Name}, {"value", C->getAttr(I).Value}});

  if (!Attrs.empty())
    JOS.attribute("attrs", std::move(Attrs));
}

void JSONNodeDumper::visitHTMLEndTagComment(
    const comments::HTMLEndTagComment *C, const comments::FullComment *) {
  JOS.attribute("name", C->getTagName());
}

void JSONNodeDumper::visitBlockCommandComment(
    const comments::BlockCommandComment *C, const comments::FullComment *) {
  JOS.attribute("name", getCommentCommandName(C->getCommandID()));

  llvm::json::Array Args;
  for (unsigned I = 0, E = C->getNumArgs(); I < E; ++I)
    Args.push_back(C->getArgText(I));

  if (!Args.empty())
    JOS.attribute("args", std::move(Args));
}

void JSONNodeDumper::visitParamCommandComment(
    const comments::ParamCommandComment *C, const comments::FullComment *FC) {
  switch (C->getDirection()) {
  case comments::ParamCommandPassDirection::In:
    JOS.attribute("direction", "in");
    break;
  case comments::ParamCommandPassDirection::Out:
    JOS.attribute("direction", "out");
    break;
  case comments::ParamCommandPassDirection::InOut:
    JOS.attribute("direction", "in,out");
    break;
  }
  attributeOnlyIfTrue("explicit", C->isDirectionExplicit());

  if (C->hasParamName())
    JOS.attribute("param", C->isParamIndexValid() ? C->getParamName(FC)
                                                  : C->getParamNameAsWritten());

  if (C->isParamIndexValid() && !C->isVarArgParam())
    JOS.attribute("paramIdx", C->getParamIndex());
}

void JSONNodeDumper::visitTParamCommandComment(
    const comments::TParamCommandComment *C, const comments::FullComment *FC) {
  if (C->hasParamName())
    JOS.attribute("param", C->isPositionValid() ? C->getParamName(FC)
                                                : C->getParamNameAsWritten());
  if (C->isPositionValid()) {
    llvm::json::Array Positions;
    for (unsigned I = 0, E = C->getDepth(); I < E; ++I)
      Positions.push_back(C->getIndex(I));

    if (!Positions.empty())
      JOS.attribute("positions", std::move(Positions));
  }
}

void JSONNodeDumper::visitVerbatimBlockComment(
    const comments::VerbatimBlockComment *C, const comments::FullComment *) {
  JOS.attribute("name", getCommentCommandName(C->getCommandID()));
  JOS.attribute("closeName", C->getCloseName());
}

void JSONNodeDumper::visitVerbatimBlockLineComment(
    const comments::VerbatimBlockLineComment *C,
    const comments::FullComment *) {
  JOS.attribute("text", C->getText());
}

void JSONNodeDumper::visitVerbatimLineComment(
    const comments::VerbatimLineComment *C, const comments::FullComment *) {
  JOS.attribute("text", C->getText());
}

llvm::json::Object JSONNodeDumper::createFPOptions(FPOptionsOverride FPO) {
  llvm::json::Object Ret;
#define OPTION(NAME, TYPE, WIDTH, PREVIOUS)                                    \
  if (FPO.has##NAME##Override())                                               \
    Ret.try_emplace(#NAME, static_cast<unsigned>(FPO.get##NAME##Override()));
#include "clang/Basic/FPOptions.def"
  return Ret;
}

void JSONNodeDumper::VisitCompoundStmt(const CompoundStmt *S) {
  VisitStmt(S);
  if (S->hasStoredFPFeatures())
    JOS.attribute("fpoptions", createFPOptions(S->getStoredFPFeatures()));
}
