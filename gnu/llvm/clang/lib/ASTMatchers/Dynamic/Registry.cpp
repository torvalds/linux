//===- Registry.cpp - Matcher registry ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Registry map populated at static initialization time.
//
//===----------------------------------------------------------------------===//

#include "clang/ASTMatchers/Dynamic/Registry.h"
#include "Marshallers.h"
#include "clang/AST/ASTTypeTraits.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/ASTMatchers/Dynamic/Diagnostics.h"
#include "clang/ASTMatchers/Dynamic/VariantValue.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace clang {
namespace ast_matchers {
namespace dynamic {

namespace {

using internal::MatcherDescriptor;

using ConstructorMap =
    llvm::StringMap<std::unique_ptr<const MatcherDescriptor>>;

class RegistryMaps {
public:
  RegistryMaps();
  ~RegistryMaps();

  const ConstructorMap &constructors() const { return Constructors; }

private:
  void registerMatcher(StringRef MatcherName,
                       std::unique_ptr<MatcherDescriptor> Callback);

  ConstructorMap Constructors;
};

} // namespace

void RegistryMaps::registerMatcher(
    StringRef MatcherName, std::unique_ptr<MatcherDescriptor> Callback) {
  assert(!Constructors.contains(MatcherName));
  Constructors[MatcherName] = std::move(Callback);
}

#define REGISTER_MATCHER(name)                                                 \
  registerMatcher(#name, internal::makeMatcherAutoMarshall(                    \
                             ::clang::ast_matchers::name, #name));

#define REGISTER_MATCHER_OVERLOAD(name)                                        \
  registerMatcher(#name,                                                       \
      std::make_unique<internal::OverloadedMatcherDescriptor>(name##Callbacks))

#define SPECIFIC_MATCHER_OVERLOAD(name, Id)                                    \
  static_cast<::clang::ast_matchers::name##_Type##Id>(                         \
      ::clang::ast_matchers::name)

#define MATCHER_OVERLOAD_ENTRY(name, Id)                                       \
        internal::makeMatcherAutoMarshall(SPECIFIC_MATCHER_OVERLOAD(name, Id), \
                                          #name)

#define REGISTER_OVERLOADED_2(name)                                            \
  do {                                                                         \
    std::unique_ptr<MatcherDescriptor> name##Callbacks[] = {                   \
        MATCHER_OVERLOAD_ENTRY(name, 0),                                       \
        MATCHER_OVERLOAD_ENTRY(name, 1)};                                      \
    REGISTER_MATCHER_OVERLOAD(name);                                           \
  } while (false)

#define REGISTER_REGEX_MATCHER(name)                                           \
  registerMatcher(#name, internal::makeMatcherRegexMarshall(name, name))

/// Generate a registry map with all the known matchers.
/// Please keep sorted alphabetically!
RegistryMaps::RegistryMaps() {
  // TODO: Here is the list of the missing matchers, grouped by reason.
  //
  // Polymorphic + argument overload:
  // findAll
  //
  // Other:
  // equalsNode

  registerMatcher("mapAnyOf",
                  std::make_unique<internal::MapAnyOfBuilderDescriptor>());

  REGISTER_OVERLOADED_2(callee);
  REGISTER_OVERLOADED_2(hasPrefix);
  REGISTER_OVERLOADED_2(hasType);
  REGISTER_OVERLOADED_2(ignoringParens);
  REGISTER_OVERLOADED_2(isDerivedFrom);
  REGISTER_OVERLOADED_2(isDirectlyDerivedFrom);
  REGISTER_OVERLOADED_2(isSameOrDerivedFrom);
  REGISTER_OVERLOADED_2(loc);
  REGISTER_OVERLOADED_2(pointsTo);
  REGISTER_OVERLOADED_2(references);
  REGISTER_OVERLOADED_2(thisPointerType);

  std::unique_ptr<MatcherDescriptor> equalsCallbacks[] = {
      MATCHER_OVERLOAD_ENTRY(equals, 0),
      MATCHER_OVERLOAD_ENTRY(equals, 1),
      MATCHER_OVERLOAD_ENTRY(equals, 2),
  };
  REGISTER_MATCHER_OVERLOAD(equals);

  REGISTER_REGEX_MATCHER(isExpansionInFileMatching);
  REGISTER_REGEX_MATCHER(matchesName);
  REGISTER_REGEX_MATCHER(matchesSelector);

  REGISTER_MATCHER(accessSpecDecl);
  REGISTER_MATCHER(addrLabelExpr);
  REGISTER_MATCHER(alignOfExpr);
  REGISTER_MATCHER(allOf);
  REGISTER_MATCHER(anyOf);
  REGISTER_MATCHER(anything);
  REGISTER_MATCHER(arrayInitIndexExpr);
  REGISTER_MATCHER(arrayInitLoopExpr);
  REGISTER_MATCHER(argumentCountIs);
  REGISTER_MATCHER(argumentCountAtLeast);
  REGISTER_MATCHER(arraySubscriptExpr);
  REGISTER_MATCHER(arrayType);
  REGISTER_MATCHER(asString);
  REGISTER_MATCHER(asmStmt);
  REGISTER_MATCHER(atomicExpr);
  REGISTER_MATCHER(atomicType);
  REGISTER_MATCHER(attr);
  REGISTER_MATCHER(autoType);
  REGISTER_MATCHER(autoreleasePoolStmt)
  REGISTER_MATCHER(binaryConditionalOperator);
  REGISTER_MATCHER(binaryOperator);
  REGISTER_MATCHER(binaryOperation);
  REGISTER_MATCHER(bindingDecl);
  REGISTER_MATCHER(blockDecl);
  REGISTER_MATCHER(blockExpr);
  REGISTER_MATCHER(blockPointerType);
  REGISTER_MATCHER(booleanType);
  REGISTER_MATCHER(breakStmt);
  REGISTER_MATCHER(builtinType);
  REGISTER_MATCHER(cStyleCastExpr);
  REGISTER_MATCHER(callExpr);
  REGISTER_MATCHER(capturesThis);
  REGISTER_MATCHER(capturesVar);
  REGISTER_MATCHER(caseStmt);
  REGISTER_MATCHER(castExpr);
  REGISTER_MATCHER(characterLiteral);
  REGISTER_MATCHER(chooseExpr);
  REGISTER_MATCHER(classTemplateDecl);
  REGISTER_MATCHER(classTemplatePartialSpecializationDecl);
  REGISTER_MATCHER(classTemplateSpecializationDecl);
  REGISTER_MATCHER(complexType);
  REGISTER_MATCHER(compoundLiteralExpr);
  REGISTER_MATCHER(compoundStmt);
  REGISTER_MATCHER(coawaitExpr);
  REGISTER_MATCHER(conceptDecl);
  REGISTER_MATCHER(conditionalOperator);
  REGISTER_MATCHER(constantArrayType);
  REGISTER_MATCHER(constantExpr);
  REGISTER_MATCHER(containsDeclaration);
  REGISTER_MATCHER(continueStmt);
  REGISTER_MATCHER(convertVectorExpr);
  REGISTER_MATCHER(coreturnStmt);
  REGISTER_MATCHER(coroutineBodyStmt);
  REGISTER_MATCHER(coyieldExpr);
  REGISTER_MATCHER(cudaKernelCallExpr);
  REGISTER_MATCHER(cxxBaseSpecifier);
  REGISTER_MATCHER(cxxBindTemporaryExpr);
  REGISTER_MATCHER(cxxBoolLiteral);
  REGISTER_MATCHER(cxxCatchStmt);
  REGISTER_MATCHER(cxxConstCastExpr);
  REGISTER_MATCHER(cxxConstructExpr);
  REGISTER_MATCHER(cxxConstructorDecl);
  REGISTER_MATCHER(cxxConversionDecl);
  REGISTER_MATCHER(cxxCtorInitializer);
  REGISTER_MATCHER(cxxDeductionGuideDecl);
  REGISTER_MATCHER(cxxDefaultArgExpr);
  REGISTER_MATCHER(cxxDeleteExpr);
  REGISTER_MATCHER(cxxDependentScopeMemberExpr);
  REGISTER_MATCHER(cxxDestructorDecl);
  REGISTER_MATCHER(cxxDynamicCastExpr);
  REGISTER_MATCHER(cxxFoldExpr);
  REGISTER_MATCHER(cxxForRangeStmt);
  REGISTER_MATCHER(cxxFunctionalCastExpr);
  REGISTER_MATCHER(cxxMemberCallExpr);
  REGISTER_MATCHER(cxxMethodDecl);
  REGISTER_MATCHER(cxxNewExpr);
  REGISTER_MATCHER(cxxNoexceptExpr);
  REGISTER_MATCHER(cxxNullPtrLiteralExpr);
  REGISTER_MATCHER(cxxOperatorCallExpr);
  REGISTER_MATCHER(cxxRecordDecl);
  REGISTER_MATCHER(cxxReinterpretCastExpr);
  REGISTER_MATCHER(cxxRewrittenBinaryOperator);
  REGISTER_MATCHER(cxxStaticCastExpr);
  REGISTER_MATCHER(cxxStdInitializerListExpr);
  REGISTER_MATCHER(cxxTemporaryObjectExpr);
  REGISTER_MATCHER(cxxThisExpr);
  REGISTER_MATCHER(cxxThrowExpr);
  REGISTER_MATCHER(cxxTryStmt);
  REGISTER_MATCHER(cxxUnresolvedConstructExpr);
  REGISTER_MATCHER(decayedType);
  REGISTER_MATCHER(decl);
  REGISTER_MATCHER(decompositionDecl);
  REGISTER_MATCHER(declCountIs);
  REGISTER_MATCHER(declRefExpr);
  REGISTER_MATCHER(declStmt);
  REGISTER_MATCHER(declaratorDecl);
  REGISTER_MATCHER(decltypeType);
  REGISTER_MATCHER(deducedTemplateSpecializationType);
  REGISTER_MATCHER(defaultStmt);
  REGISTER_MATCHER(dependentCoawaitExpr);
  REGISTER_MATCHER(dependentSizedArrayType);
  REGISTER_MATCHER(dependentSizedExtVectorType);
  REGISTER_MATCHER(designatedInitExpr);
  REGISTER_MATCHER(designatorCountIs);
  REGISTER_MATCHER(doStmt);
  REGISTER_MATCHER(eachOf);
  REGISTER_MATCHER(elaboratedType);
  REGISTER_MATCHER(elaboratedTypeLoc);
  REGISTER_MATCHER(usingType);
  REGISTER_MATCHER(enumConstantDecl);
  REGISTER_MATCHER(enumDecl);
  REGISTER_MATCHER(enumType);
  REGISTER_MATCHER(equalsBoundNode);
  REGISTER_MATCHER(equalsIntegralValue);
  REGISTER_MATCHER(explicitCastExpr);
  REGISTER_MATCHER(expr);
  REGISTER_MATCHER(exprWithCleanups);
  REGISTER_MATCHER(fieldDecl);
  REGISTER_MATCHER(fixedPointLiteral);
  REGISTER_MATCHER(floatLiteral);
  REGISTER_MATCHER(forCallable);
  REGISTER_MATCHER(forDecomposition);
  REGISTER_MATCHER(forEach);
  REGISTER_MATCHER(forEachArgumentWithParam);
  REGISTER_MATCHER(forEachArgumentWithParamType);
  REGISTER_MATCHER(forEachConstructorInitializer);
  REGISTER_MATCHER(forEachDescendant);
  REGISTER_MATCHER(forEachLambdaCapture);
  REGISTER_MATCHER(forEachOverridden);
  REGISTER_MATCHER(forEachSwitchCase);
  REGISTER_MATCHER(forEachTemplateArgument);
  REGISTER_MATCHER(forField);
  REGISTER_MATCHER(forFunction);
  REGISTER_MATCHER(forStmt);
  REGISTER_MATCHER(friendDecl);
  REGISTER_MATCHER(functionDecl);
  REGISTER_MATCHER(functionProtoType);
  REGISTER_MATCHER(functionTemplateDecl);
  REGISTER_MATCHER(functionType);
  REGISTER_MATCHER(genericSelectionExpr);
  REGISTER_MATCHER(gnuNullExpr);
  REGISTER_MATCHER(gotoStmt);
  REGISTER_MATCHER(has);
  REGISTER_MATCHER(hasAncestor);
  REGISTER_MATCHER(hasAnyArgument);
  REGISTER_MATCHER(hasAnyBase);
  REGISTER_MATCHER(hasAnyBinding);
  REGISTER_MATCHER(hasAnyBody);
  REGISTER_MATCHER(hasAnyCapture);
  REGISTER_MATCHER(hasAnyClause);
  REGISTER_MATCHER(hasAnyConstructorInitializer);
  REGISTER_MATCHER(hasAnyDeclaration);
  REGISTER_MATCHER(hasAnyName);
  REGISTER_MATCHER(hasAnyOperatorName);
  REGISTER_MATCHER(hasAnyOverloadedOperatorName);
  REGISTER_MATCHER(hasAnyParameter);
  REGISTER_MATCHER(hasAnyPlacementArg);
  REGISTER_MATCHER(hasAnySelector);
  REGISTER_MATCHER(hasAnySubstatement);
  REGISTER_MATCHER(hasAnyTemplateArgument);
  REGISTER_MATCHER(hasAnyTemplateArgumentLoc);
  REGISTER_MATCHER(hasAnyUsingShadowDecl);
  REGISTER_MATCHER(hasArgument);
  REGISTER_MATCHER(hasArgumentOfType);
  REGISTER_MATCHER(hasArraySize);
  REGISTER_MATCHER(hasAttr);
  REGISTER_MATCHER(hasAutomaticStorageDuration);
  REGISTER_MATCHER(hasBase);
  REGISTER_MATCHER(hasBinding);
  REGISTER_MATCHER(hasBitWidth);
  REGISTER_MATCHER(hasBody);
  REGISTER_MATCHER(hasCanonicalType);
  REGISTER_MATCHER(hasCaseConstant);
  REGISTER_MATCHER(hasCastKind);
  REGISTER_MATCHER(hasCondition);
  REGISTER_MATCHER(hasConditionVariableStatement);
  REGISTER_MATCHER(hasDecayedType);
  REGISTER_MATCHER(hasDeclContext);
  REGISTER_MATCHER(hasDeclaration);
  REGISTER_MATCHER(hasDeducedType);
  REGISTER_MATCHER(hasDefaultArgument);
  REGISTER_MATCHER(hasDefinition);
  REGISTER_MATCHER(hasDescendant);
  REGISTER_MATCHER(hasDestinationType);
  REGISTER_MATCHER(hasDirectBase);
  REGISTER_MATCHER(hasDynamicExceptionSpec);
  REGISTER_MATCHER(hasEitherOperand);
  REGISTER_MATCHER(hasElementType);
  REGISTER_MATCHER(hasElse);
  REGISTER_MATCHER(hasExplicitSpecifier);
  REGISTER_MATCHER(hasExternalFormalLinkage);
  REGISTER_MATCHER(hasFalseExpression);
  REGISTER_MATCHER(hasFoldInit);
  REGISTER_MATCHER(hasGlobalStorage);
  REGISTER_MATCHER(hasImplicitDestinationType);
  REGISTER_MATCHER(hasInClassInitializer);
  REGISTER_MATCHER(hasIncrement);
  REGISTER_MATCHER(hasIndex);
  REGISTER_MATCHER(hasInit);
  REGISTER_MATCHER(hasInitializer);
  REGISTER_MATCHER(hasInitStatement);
  REGISTER_MATCHER(hasKeywordSelector);
  REGISTER_MATCHER(hasLHS);
  REGISTER_MATCHER(hasLocalQualifiers);
  REGISTER_MATCHER(hasLocalStorage);
  REGISTER_MATCHER(hasLoopInit);
  REGISTER_MATCHER(hasLoopVariable);
  REGISTER_MATCHER(hasMemberName);
  REGISTER_MATCHER(hasMethod);
  REGISTER_MATCHER(hasName);
  REGISTER_MATCHER(hasNamedTypeLoc);
  REGISTER_MATCHER(hasNullSelector);
  REGISTER_MATCHER(hasObjectExpression);
  REGISTER_MATCHER(hasOperands);
  REGISTER_MATCHER(hasOperatorName);
  REGISTER_MATCHER(hasOverloadedOperatorName);
  REGISTER_MATCHER(hasParameter);
  REGISTER_MATCHER(hasParent);
  REGISTER_MATCHER(hasPattern);
  REGISTER_MATCHER(hasPointeeLoc);
  REGISTER_MATCHER(hasQualifier);
  REGISTER_MATCHER(hasRHS);
  REGISTER_MATCHER(hasRangeInit);
  REGISTER_MATCHER(hasReceiver);
  REGISTER_MATCHER(hasReceiverType);
  REGISTER_MATCHER(hasReferentLoc);
  REGISTER_MATCHER(hasReplacementType);
  REGISTER_MATCHER(hasReturnTypeLoc);
  REGISTER_MATCHER(hasReturnValue);
  REGISTER_MATCHER(hasPlacementArg);
  REGISTER_MATCHER(hasSelector);
  REGISTER_MATCHER(hasSingleDecl);
  REGISTER_MATCHER(hasSize);
  REGISTER_MATCHER(hasSizeExpr);
  REGISTER_MATCHER(hasSourceExpression);
  REGISTER_MATCHER(hasSpecializedTemplate);
  REGISTER_MATCHER(hasStaticStorageDuration);
  REGISTER_MATCHER(hasStructuredBlock);
  REGISTER_MATCHER(hasSyntacticForm);
  REGISTER_MATCHER(hasTargetDecl);
  REGISTER_MATCHER(hasTemplateArgument);
  REGISTER_MATCHER(hasTemplateArgumentLoc);
  REGISTER_MATCHER(hasThen);
  REGISTER_MATCHER(hasThreadStorageDuration);
  REGISTER_MATCHER(hasTrailingReturn);
  REGISTER_MATCHER(hasTrueExpression);
  REGISTER_MATCHER(hasTypeLoc);
  REGISTER_MATCHER(hasUnaryOperand);
  REGISTER_MATCHER(hasUnarySelector);
  REGISTER_MATCHER(hasUnderlyingDecl);
  REGISTER_MATCHER(hasUnderlyingType);
  REGISTER_MATCHER(hasUnqualifiedDesugaredType);
  REGISTER_MATCHER(hasUnqualifiedLoc);
  REGISTER_MATCHER(hasValueType);
  REGISTER_MATCHER(ifStmt);
  REGISTER_MATCHER(ignoringElidableConstructorCall);
  REGISTER_MATCHER(ignoringImpCasts);
  REGISTER_MATCHER(ignoringImplicit);
  REGISTER_MATCHER(ignoringParenCasts);
  REGISTER_MATCHER(ignoringParenImpCasts);
  REGISTER_MATCHER(imaginaryLiteral);
  REGISTER_MATCHER(implicitCastExpr);
  REGISTER_MATCHER(implicitValueInitExpr);
  REGISTER_MATCHER(incompleteArrayType);
  REGISTER_MATCHER(indirectFieldDecl);
  REGISTER_MATCHER(initListExpr);
  REGISTER_MATCHER(injectedClassNameType);
  REGISTER_MATCHER(innerType);
  REGISTER_MATCHER(integerLiteral);
  REGISTER_MATCHER(invocation);
  REGISTER_MATCHER(isAllowedToContainClauseKind);
  REGISTER_MATCHER(isAnonymous);
  REGISTER_MATCHER(isAnyCharacter);
  REGISTER_MATCHER(isAnyPointer);
  REGISTER_MATCHER(isArray);
  REGISTER_MATCHER(isArrow);
  REGISTER_MATCHER(isAssignmentOperator);
  REGISTER_MATCHER(isAtPosition);
  REGISTER_MATCHER(isBaseInitializer);
  REGISTER_MATCHER(isBinaryFold);
  REGISTER_MATCHER(isBitField);
  REGISTER_MATCHER(isCatchAll);
  REGISTER_MATCHER(isClass);
  REGISTER_MATCHER(isClassMessage);
  REGISTER_MATCHER(isClassMethod);
  REGISTER_MATCHER(isComparisonOperator);
  REGISTER_MATCHER(isConst);
  REGISTER_MATCHER(isConstQualified);
  REGISTER_MATCHER(isConsteval);
  REGISTER_MATCHER(isConstexpr);
  REGISTER_MATCHER(isConstinit);
  REGISTER_MATCHER(isCopyAssignmentOperator);
  REGISTER_MATCHER(isCopyConstructor);
  REGISTER_MATCHER(isDefaultConstructor);
  REGISTER_MATCHER(isDefaulted);
  REGISTER_MATCHER(isDefinition);
  REGISTER_MATCHER(isDelegatingConstructor);
  REGISTER_MATCHER(isDeleted);
  REGISTER_MATCHER(isEnum);
  REGISTER_MATCHER(isExceptionVariable);
  REGISTER_MATCHER(isExpandedFromMacro);
  REGISTER_MATCHER(isExpansionInMainFile);
  REGISTER_MATCHER(isExpansionInSystemHeader);
  REGISTER_MATCHER(isExplicit);
  REGISTER_MATCHER(isExplicitObjectMemberFunction);
  REGISTER_MATCHER(isExplicitTemplateSpecialization);
  REGISTER_MATCHER(isExpr);
  REGISTER_MATCHER(isExternC);
  REGISTER_MATCHER(isFinal);
  REGISTER_MATCHER(isPrivateKind);
  REGISTER_MATCHER(isFirstPrivateKind);
  REGISTER_MATCHER(isImplicit);
  REGISTER_MATCHER(isInAnonymousNamespace);
  REGISTER_MATCHER(isInStdNamespace);
  REGISTER_MATCHER(isInTemplateInstantiation);
  REGISTER_MATCHER(isInitCapture);
  REGISTER_MATCHER(isInline);
  REGISTER_MATCHER(isInstanceMessage);
  REGISTER_MATCHER(isInstanceMethod);
  REGISTER_MATCHER(isInstantiated);
  REGISTER_MATCHER(isInstantiationDependent);
  REGISTER_MATCHER(isInteger);
  REGISTER_MATCHER(isIntegral);
  REGISTER_MATCHER(isLambda);
  REGISTER_MATCHER(isLeftFold);
  REGISTER_MATCHER(isListInitialization);
  REGISTER_MATCHER(isMain);
  REGISTER_MATCHER(isMemberInitializer);
  REGISTER_MATCHER(isMoveAssignmentOperator);
  REGISTER_MATCHER(isMoveConstructor);
  REGISTER_MATCHER(isNoReturn);
  REGISTER_MATCHER(isNoThrow);
  REGISTER_MATCHER(isNoneKind);
  REGISTER_MATCHER(isOverride);
  REGISTER_MATCHER(isPrivate);
  REGISTER_MATCHER(isProtected);
  REGISTER_MATCHER(isPublic);
  REGISTER_MATCHER(isPure);
  REGISTER_MATCHER(isRightFold);
  REGISTER_MATCHER(isScoped);
  REGISTER_MATCHER(isSharedKind);
  REGISTER_MATCHER(isSignedInteger);
  REGISTER_MATCHER(isStandaloneDirective);
  REGISTER_MATCHER(isStaticLocal);
  REGISTER_MATCHER(isStaticStorageClass);
  REGISTER_MATCHER(isStruct);
  REGISTER_MATCHER(isTemplateInstantiation);
  REGISTER_MATCHER(isTypeDependent);
  REGISTER_MATCHER(isUnaryFold);
  REGISTER_MATCHER(isUnion);
  REGISTER_MATCHER(isUnsignedInteger);
  REGISTER_MATCHER(isUserProvided);
  REGISTER_MATCHER(isValueDependent);
  REGISTER_MATCHER(isVariadic);
  REGISTER_MATCHER(isVirtual);
  REGISTER_MATCHER(isVirtualAsWritten);
  REGISTER_MATCHER(isVolatileQualified);
  REGISTER_MATCHER(isWeak);
  REGISTER_MATCHER(isWritten);
  REGISTER_MATCHER(lValueReferenceType);
  REGISTER_MATCHER(labelDecl);
  REGISTER_MATCHER(labelStmt);
  REGISTER_MATCHER(lambdaCapture);
  REGISTER_MATCHER(lambdaExpr);
  REGISTER_MATCHER(linkageSpecDecl);
  REGISTER_MATCHER(macroQualifiedType);
  REGISTER_MATCHER(materializeTemporaryExpr);
  REGISTER_MATCHER(member);
  REGISTER_MATCHER(memberExpr);
  REGISTER_MATCHER(memberHasSameNameAsBoundNode);
  REGISTER_MATCHER(memberPointerType);
  REGISTER_MATCHER(namedDecl);
  REGISTER_MATCHER(namesType);
  REGISTER_MATCHER(namespaceAliasDecl);
  REGISTER_MATCHER(namespaceDecl);
  REGISTER_MATCHER(nestedNameSpecifier);
  REGISTER_MATCHER(nestedNameSpecifierLoc);
  REGISTER_MATCHER(nonTypeTemplateParmDecl);
  REGISTER_MATCHER(nullPointerConstant);
  REGISTER_MATCHER(nullStmt);
  REGISTER_MATCHER(numSelectorArgs);
  REGISTER_MATCHER(objcCatchStmt);
  REGISTER_MATCHER(objcCategoryDecl);
  REGISTER_MATCHER(objcCategoryImplDecl);
  REGISTER_MATCHER(objcFinallyStmt);
  REGISTER_MATCHER(objcImplementationDecl);
  REGISTER_MATCHER(objcInterfaceDecl);
  REGISTER_MATCHER(objcIvarDecl);
  REGISTER_MATCHER(objcIvarRefExpr);
  REGISTER_MATCHER(objcMessageExpr);
  REGISTER_MATCHER(objcMethodDecl);
  REGISTER_MATCHER(objcObjectPointerType);
  REGISTER_MATCHER(objcPropertyDecl);
  REGISTER_MATCHER(objcProtocolDecl);
  REGISTER_MATCHER(objcStringLiteral);
  REGISTER_MATCHER(objcThrowStmt);
  REGISTER_MATCHER(objcTryStmt);
  REGISTER_MATCHER(ofClass);
  REGISTER_MATCHER(ofKind);
  REGISTER_MATCHER(ompDefaultClause);
  REGISTER_MATCHER(ompExecutableDirective);
  REGISTER_MATCHER(on);
  REGISTER_MATCHER(onImplicitObjectArgument);
  REGISTER_MATCHER(opaqueValueExpr);
  REGISTER_MATCHER(optionally);
  REGISTER_MATCHER(parameterCountIs);
  REGISTER_MATCHER(parenExpr);
  REGISTER_MATCHER(parenListExpr);
  REGISTER_MATCHER(parenType);
  REGISTER_MATCHER(parmVarDecl);
  REGISTER_MATCHER(pointee);
  REGISTER_MATCHER(pointerType);
  REGISTER_MATCHER(pointerTypeLoc);
  REGISTER_MATCHER(predefinedExpr);
  REGISTER_MATCHER(qualType);
  REGISTER_MATCHER(qualifiedTypeLoc);
  REGISTER_MATCHER(rValueReferenceType);
  REGISTER_MATCHER(realFloatingPointType);
  REGISTER_MATCHER(recordDecl);
  REGISTER_MATCHER(recordType);
  REGISTER_MATCHER(referenceType);
  REGISTER_MATCHER(referenceTypeLoc);
  REGISTER_MATCHER(refersToDeclaration);
  REGISTER_MATCHER(refersToIntegralType);
  REGISTER_MATCHER(refersToTemplate);
  REGISTER_MATCHER(refersToType);
  REGISTER_MATCHER(requiresZeroInitialization);
  REGISTER_MATCHER(returnStmt);
  REGISTER_MATCHER(returns);
  REGISTER_MATCHER(sizeOfExpr);
  REGISTER_MATCHER(specifiesNamespace);
  REGISTER_MATCHER(specifiesType);
  REGISTER_MATCHER(specifiesTypeLoc);
  REGISTER_MATCHER(statementCountIs);
  REGISTER_MATCHER(staticAssertDecl);
  REGISTER_MATCHER(stmt);
  REGISTER_MATCHER(stmtExpr);
  REGISTER_MATCHER(stringLiteral);
  REGISTER_MATCHER(substNonTypeTemplateParmExpr);
  REGISTER_MATCHER(substTemplateTypeParmType);
  REGISTER_MATCHER(switchCase);
  REGISTER_MATCHER(switchStmt);
  REGISTER_MATCHER(tagDecl);
  REGISTER_MATCHER(tagType);
  REGISTER_MATCHER(templateArgument);
  REGISTER_MATCHER(templateArgumentCountIs);
  REGISTER_MATCHER(templateArgumentLoc);
  REGISTER_MATCHER(templateName);
  REGISTER_MATCHER(templateSpecializationType);
  REGISTER_MATCHER(templateSpecializationTypeLoc);
  REGISTER_MATCHER(templateTemplateParmDecl);
  REGISTER_MATCHER(templateTypeParmDecl);
  REGISTER_MATCHER(templateTypeParmType);
  REGISTER_MATCHER(throughUsingDecl);
  REGISTER_MATCHER(to);
  REGISTER_MATCHER(translationUnitDecl);
  REGISTER_MATCHER(type);
  REGISTER_MATCHER(typeAliasDecl);
  REGISTER_MATCHER(typeAliasTemplateDecl);
  REGISTER_MATCHER(typeLoc);
  REGISTER_MATCHER(typedefDecl);
  REGISTER_MATCHER(typedefNameDecl);
  REGISTER_MATCHER(typedefType);
  REGISTER_MATCHER(unaryExprOrTypeTraitExpr);
  REGISTER_MATCHER(unaryOperator);
  REGISTER_MATCHER(unaryTransformType);
  REGISTER_MATCHER(unless);
  REGISTER_MATCHER(unresolvedLookupExpr);
  REGISTER_MATCHER(unresolvedMemberExpr);
  REGISTER_MATCHER(unresolvedUsingTypenameDecl);
  REGISTER_MATCHER(unresolvedUsingValueDecl);
  REGISTER_MATCHER(userDefinedLiteral);
  REGISTER_MATCHER(usesADL);
  REGISTER_MATCHER(usingDecl);
  REGISTER_MATCHER(usingEnumDecl);
  REGISTER_MATCHER(usingDirectiveDecl);
  REGISTER_MATCHER(valueDecl);
  REGISTER_MATCHER(varDecl);
  REGISTER_MATCHER(variableArrayType);
  REGISTER_MATCHER(voidType);
  REGISTER_MATCHER(whileStmt);
  REGISTER_MATCHER(withInitializer);
}

RegistryMaps::~RegistryMaps() = default;

static llvm::ManagedStatic<RegistryMaps> RegistryData;

ASTNodeKind Registry::nodeMatcherType(MatcherCtor Ctor) {
  return Ctor->nodeMatcherType();
}

internal::MatcherDescriptorPtr::MatcherDescriptorPtr(MatcherDescriptor *Ptr)
    : Ptr(Ptr) {}

internal::MatcherDescriptorPtr::~MatcherDescriptorPtr() { delete Ptr; }

bool Registry::isBuilderMatcher(MatcherCtor Ctor) {
  return Ctor->isBuilderMatcher();
}

internal::MatcherDescriptorPtr
Registry::buildMatcherCtor(MatcherCtor Ctor, SourceRange NameRange,
                           ArrayRef<ParserValue> Args, Diagnostics *Error) {
  return internal::MatcherDescriptorPtr(
      Ctor->buildMatcherCtor(NameRange, Args, Error).release());
}

// static
std::optional<MatcherCtor> Registry::lookupMatcherCtor(StringRef MatcherName) {
  auto it = RegistryData->constructors().find(MatcherName);
  return it == RegistryData->constructors().end() ? std::optional<MatcherCtor>()
                                                  : it->second.get();
}

static llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                                     const std::set<ASTNodeKind> &KS) {
  unsigned Count = 0;
  for (std::set<ASTNodeKind>::const_iterator I = KS.begin(), E = KS.end();
       I != E; ++I) {
    if (I != KS.begin())
      OS << "|";
    if (Count++ == 3) {
      OS << "...";
      break;
    }
    OS << *I;
  }
  return OS;
}

std::vector<ArgKind> Registry::getAcceptedCompletionTypes(
    ArrayRef<std::pair<MatcherCtor, unsigned>> Context) {
  ASTNodeKind InitialTypes[] = {
      ASTNodeKind::getFromNodeKind<Decl>(),
      ASTNodeKind::getFromNodeKind<QualType>(),
      ASTNodeKind::getFromNodeKind<Type>(),
      ASTNodeKind::getFromNodeKind<Stmt>(),
      ASTNodeKind::getFromNodeKind<NestedNameSpecifier>(),
      ASTNodeKind::getFromNodeKind<NestedNameSpecifierLoc>(),
      ASTNodeKind::getFromNodeKind<TypeLoc>()};

  // Starting with the above seed of acceptable top-level matcher types, compute
  // the acceptable type set for the argument indicated by each context element.
  std::set<ArgKind> TypeSet;
  for (auto IT : InitialTypes) {
    TypeSet.insert(ArgKind::MakeMatcherArg(IT));
  }
  for (const auto &CtxEntry : Context) {
    MatcherCtor Ctor = CtxEntry.first;
    unsigned ArgNumber = CtxEntry.second;
    std::vector<ArgKind> NextTypeSet;
    for (const ArgKind &Kind : TypeSet) {
      if (Kind.getArgKind() == Kind.AK_Matcher &&
          Ctor->isConvertibleTo(Kind.getMatcherKind()) &&
          (Ctor->isVariadic() || ArgNumber < Ctor->getNumArgs()))
        Ctor->getArgKinds(Kind.getMatcherKind(), ArgNumber, NextTypeSet);
    }
    TypeSet.clear();
    TypeSet.insert(NextTypeSet.begin(), NextTypeSet.end());
  }
  return std::vector<ArgKind>(TypeSet.begin(), TypeSet.end());
}

std::vector<MatcherCompletion>
Registry::getMatcherCompletions(ArrayRef<ArgKind> AcceptedTypes) {
  std::vector<MatcherCompletion> Completions;

  // Search the registry for acceptable matchers.
  for (const auto &M : RegistryData->constructors()) {
    const MatcherDescriptor& Matcher = *M.getValue();
    StringRef Name = M.getKey();

    std::set<ASTNodeKind> RetKinds;
    unsigned NumArgs = Matcher.isVariadic() ? 1 : Matcher.getNumArgs();
    bool IsPolymorphic = Matcher.isPolymorphic();
    std::vector<std::vector<ArgKind>> ArgsKinds(NumArgs);
    unsigned MaxSpecificity = 0;
    bool NodeArgs = false;
    for (const ArgKind& Kind : AcceptedTypes) {
      if (Kind.getArgKind() != Kind.AK_Matcher &&
          Kind.getArgKind() != Kind.AK_Node) {
        continue;
      }

      if (Kind.getArgKind() == Kind.AK_Node) {
        NodeArgs = true;
        unsigned Specificity;
        ASTNodeKind LeastDerivedKind;
        if (Matcher.isConvertibleTo(Kind.getNodeKind(), &Specificity,
                                    &LeastDerivedKind)) {
          if (MaxSpecificity < Specificity)
            MaxSpecificity = Specificity;
          RetKinds.insert(LeastDerivedKind);
          for (unsigned Arg = 0; Arg != NumArgs; ++Arg)
            Matcher.getArgKinds(Kind.getNodeKind(), Arg, ArgsKinds[Arg]);
          if (IsPolymorphic)
            break;
        }
      } else {
        unsigned Specificity;
        ASTNodeKind LeastDerivedKind;
        if (Matcher.isConvertibleTo(Kind.getMatcherKind(), &Specificity,
                                    &LeastDerivedKind)) {
          if (MaxSpecificity < Specificity)
            MaxSpecificity = Specificity;
          RetKinds.insert(LeastDerivedKind);
          for (unsigned Arg = 0; Arg != NumArgs; ++Arg)
            Matcher.getArgKinds(Kind.getMatcherKind(), Arg, ArgsKinds[Arg]);
          if (IsPolymorphic)
            break;
        }
      }
    }

    if (!RetKinds.empty() && MaxSpecificity > 0) {
      std::string Decl;
      llvm::raw_string_ostream OS(Decl);

      std::string TypedText = std::string(Name);

      if (NodeArgs) {
        OS << Name;
      } else {

        if (IsPolymorphic) {
          OS << "Matcher<T> " << Name << "(Matcher<T>";
        } else {
          OS << "Matcher<" << RetKinds << "> " << Name << "(";
          for (const std::vector<ArgKind> &Arg : ArgsKinds) {
            if (&Arg != &ArgsKinds[0])
              OS << ", ";

            bool FirstArgKind = true;
            std::set<ASTNodeKind> MatcherKinds;
            // Two steps. First all non-matchers, then matchers only.
            for (const ArgKind &AK : Arg) {
              if (AK.getArgKind() == ArgKind::AK_Matcher) {
                MatcherKinds.insert(AK.getMatcherKind());
              } else {
                if (!FirstArgKind)
                  OS << "|";
                FirstArgKind = false;
                OS << AK.asString();
              }
            }
            if (!MatcherKinds.empty()) {
              if (!FirstArgKind) OS << "|";
              OS << "Matcher<" << MatcherKinds << ">";
            }
          }
        }
        if (Matcher.isVariadic())
          OS << "...";
        OS << ")";

        TypedText += "(";
        if (ArgsKinds.empty())
          TypedText += ")";
        else if (ArgsKinds[0][0].getArgKind() == ArgKind::AK_String)
          TypedText += "\"";
      }

      Completions.emplace_back(TypedText, OS.str(), MaxSpecificity);
    }
  }

  return Completions;
}

VariantMatcher Registry::constructMatcher(MatcherCtor Ctor,
                                          SourceRange NameRange,
                                          ArrayRef<ParserValue> Args,
                                          Diagnostics *Error) {
  return Ctor->create(NameRange, Args, Error);
}

VariantMatcher Registry::constructBoundMatcher(MatcherCtor Ctor,
                                               SourceRange NameRange,
                                               StringRef BindID,
                                               ArrayRef<ParserValue> Args,
                                               Diagnostics *Error) {
  VariantMatcher Out = constructMatcher(Ctor, NameRange, Args, Error);
  if (Out.isNull()) return Out;

  std::optional<DynTypedMatcher> Result = Out.getSingleMatcher();
  if (Result) {
    std::optional<DynTypedMatcher> Bound = Result->tryBind(BindID);
    if (Bound) {
      return VariantMatcher::SingleMatcher(*Bound);
    }
  }
  Error->addError(NameRange, Error->ET_RegistryNotBindable);
  return VariantMatcher();
}

} // namespace dynamic
} // namespace ast_matchers
} // namespace clang
