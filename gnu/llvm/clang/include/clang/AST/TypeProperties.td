//==--- TypeProperties.td - Type property definitions ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "clang/AST/PropertiesBase.td"
include "clang/Basic/TypeNodes.td"

let Class = ComplexType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }

  def : Creator<[{ return ctx.getComplexType(elementType); }]>;
}

let Class = PointerType in {
  def : Property<"pointeeType", QualType> {
    let Read = [{ node->getPointeeType() }];
  }

  def : Creator<[{ return ctx.getPointerType(pointeeType); }]>;
}

let Class = CountAttributedType in {
  def : Property<"WrappedTy", QualType> {
    let Read = [{ node->desugar() }];
  }
  def : Property<"CountExpr", ExprRef> {
    let Read = [{ node->getCountExpr() }];
  }
  def : Property<"CountInBytes", Bool> {
    let Read = [{ node->isCountInBytes() }];
  }
  def : Property<"OrNull", Bool> {
    let Read = [{ node->isOrNull() }];
  }
  def : Property<"CoupledDecls", Array<TypeCoupledDeclRefInfo>> {
    let Read = [{ node->getCoupledDecls() }];
  }
  def : Creator<[{ return ctx.getCountAttributedType(WrappedTy, CountExpr, CountInBytes, OrNull, CoupledDecls); }]>;
}

let Class = AdjustedType in {
  def : Property<"originalType", QualType> {
    let Read = [{ node->getOriginalType() }];
  }
  def : Property<"adjustedType", QualType> {
    let Read = [{ node->getAdjustedType() }];
  }

  def : Creator<[{ return ctx.getAdjustedType(originalType, adjustedType); }]>;
}

let Class = DecayedType in {
  def : Override {
    // We don't need to serialize the adjusted type because we can always
    // derive it by decaying the original type.
    let IgnoredProperties = [ "adjustedType" ];
  }

  def : Creator<[{ return ctx.getAdjustedParameterType(originalType); }]>;
}

let Class = BlockPointerType in {
  def : Property<"pointeeType", QualType> {
    let Read = [{ node->getPointeeType() }];
  }

  def : Creator<[{ return ctx.getBlockPointerType(pointeeType); }]>;
}

let Class = ReferenceType in {
  def : Property<"pointeeTypeAsWritten", QualType> {
    let Read = [{ node->getPointeeTypeAsWritten() }];
  }
}

let Class = LValueReferenceType in {
  def : Property<"isSpelledAsLValue", Bool> {
    let Read = [{ node->isSpelledAsLValue() }];
  }

  def : Creator<[{
    return ctx.getLValueReferenceType(pointeeTypeAsWritten,
                                      isSpelledAsLValue);
  }]>;
}

let Class = RValueReferenceType in {
  def : Creator<[{
    return ctx.getRValueReferenceType(pointeeTypeAsWritten);
  }]>;
}

let Class = MemberPointerType in {
  def : Property<"pointeeType", QualType> {
    let Read = [{ node->getPointeeType() }];
  }
  def : Property<"baseType", QualType> {
    let Read = [{ QualType(node->getClass(), 0) }];
  }

  def : Creator<[{
    return ctx.getMemberPointerType(pointeeType, baseType.getTypePtr());
  }]>;
}

let Class = ArrayType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
  def : Property<"sizeModifier", ArraySizeModifier> {
    let Read = [{ node->getSizeModifier() }];
  }
  def : Property<"indexQualifiers", Qualifiers> {
    let Read = [{ Qualifiers::fromCVRMask(node->getIndexTypeCVRQualifiers()) }];
  }
}

let Class = ConstantArrayType in {
  def : Property<"sizeValue", APInt> {
    let Read = [{ node->getSize() }];
  }
  def : Property<"size", ExprRef> {
    let Read = [{ node->getSizeExpr() }];
  }

  def : Creator<[{
    return ctx.getConstantArrayType(elementType, sizeValue, size,
                                    sizeModifier,
                                    indexQualifiers.getCVRQualifiers());
  }]>;
}

let Class = ArrayParameterType in {
  def : Creator<[{ return ctx.getAdjustedParameterType(
                              ctx.getConstantArrayType(elementType,sizeValue,
                                    size,sizeModifier,
                                    indexQualifiers.getCVRQualifiers())); }]>;
}

let Class = IncompleteArrayType in {
  def : Creator<[{
    return ctx.getIncompleteArrayType(elementType, sizeModifier,
                                      indexQualifiers.getCVRQualifiers());
  }]>;
}

let Class = VariableArrayType in {
  def : Property<"leftBracketLoc", SourceLocation> {
    let Read = [{ node->getLBracketLoc() }];
  }
  def : Property<"rightBracketLoc", SourceLocation> {
    let Read = [{ node->getRBracketLoc() }];
  }
  def : Property<"size", ExprRef> {
    let Read = [{ node->getSizeExpr() }];
  }

  def : Creator<[{
    return ctx.getVariableArrayType(elementType, size, sizeModifier,
                                    indexQualifiers.getCVRQualifiers(),
                                    SourceRange(leftBracketLoc,
                                                rightBracketLoc));
  }]>;
}

let Class = DependentSizedArrayType in {
  def : Property<"size", ExprRef> {
    let Read = [{ node->getSizeExpr() }];
  }
  def : Property<"leftBracketLoc", SourceLocation> {
    let Read = [{ node->getLBracketLoc() }];
  }
  def : Property<"rightBracketLoc", SourceLocation> {
    let Read = [{ node->getRBracketLoc() }];
  }

  def : Creator<[{
    return ctx.getDependentSizedArrayType(elementType, size, sizeModifier,
                                          indexQualifiers.getCVRQualifiers(),
                                          SourceRange(leftBracketLoc,
                                                      rightBracketLoc));
  }]>;
}

let Class = VectorType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
  def : Property<"numElements", UInt32> {
    let Read = [{ node->getNumElements() }];
  }
  def : Property<"vectorKind", VectorKind> {
    let Read = [{ node->getVectorKind() }];
  }

  def : Creator<[{
    return ctx.getVectorType(elementType, numElements, vectorKind);
  }]>;
}

let Class = DependentVectorType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
  def : Property<"size", ExprRef> {
    let Read = [{ node->getSizeExpr() }];
  }
  def : Property<"attributeLoc", SourceLocation> {
    let Read = [{ node->getAttributeLoc() }];
  }
  def : Property<"vectorKind", VectorKind> {
    let Read = [{ node->getVectorKind() }];
  }

  def : Creator<[{
    return ctx.getDependentVectorType(elementType, size, attributeLoc,
                                      vectorKind);
  }]>;
}

let Class = ExtVectorType in {
  def : Override {
    let IgnoredProperties = [ "vectorKind" ];
  }

  def : Creator<[{
    return ctx.getExtVectorType(elementType, numElements);
  }]>;
}

let Class = DependentSizedExtVectorType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
  def : Property<"size", ExprRef> {
    let Read = [{ node->getSizeExpr() }];
  }
  def : Property<"attributeLoc", SourceLocation> {
    let Read = [{ node->getAttributeLoc() }];
  }

  def : Creator<[{
    return ctx.getDependentSizedExtVectorType(elementType, size, attributeLoc);
  }]>;
}

let Class = MatrixType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
}

let Class = ConstantMatrixType in {
  def : Property<"numRows", UInt32> {
    let Read = [{ node->getNumRows() }];
  }
  def : Property<"numColumns", UInt32> {
    let Read = [{ node->getNumColumns() }];
  }

  def : Creator<[{
    return ctx.getConstantMatrixType(elementType, numRows, numColumns);
  }]>;
}

let Class = DependentSizedMatrixType in {
  def : Property<"rows", ExprRef> {
    let Read = [{ node->getRowExpr() }];
  }
  def : Property<"columns", ExprRef> {
    let Read = [{ node->getColumnExpr() }];
  }
  def : Property<"attributeLoc", SourceLocation> {
    let Read = [{ node->getAttributeLoc() }];
  }

  def : Creator<[{
    return ctx.getDependentSizedMatrixType(elementType, rows, columns, attributeLoc);
  }]>;
}

let Class = FunctionType in {
  def : Property<"returnType", QualType> {
    let Read = [{ node->getReturnType() }];
  }
  def : Property<"noReturn", Bool> {
    let Read = [{ node->getExtInfo().getNoReturn() }];
  }
  def : Property<"hasRegParm", Bool> {
    let Read = [{ node->getExtInfo().getHasRegParm() }];
  }
  def : Property<"regParm", UInt32> {
    let Read = [{ node->getExtInfo().getRegParm() }];
  }
  def : Property<"callingConvention", CallingConv> {
    let Read = [{ node->getExtInfo().getCC() }];
  }
  def : Property<"producesResult", Bool> {
    let Read = [{ node->getExtInfo().getProducesResult() }];
  }
  def : Property<"noCallerSavedRegs", Bool> {
    let Read = [{ node->getExtInfo().getNoCallerSavedRegs() }];
  }
  def : Property<"noCfCheck", Bool> {
    let Read = [{ node->getExtInfo().getNoCfCheck() }];
  }
  def : Property<"cmseNSCall", Bool> {
    let Read = [{ node->getExtInfo().getCmseNSCall() }];
  }
}

let Class = FunctionNoProtoType in {
  def : Creator<[{
    auto extInfo = FunctionType::ExtInfo(noReturn, hasRegParm, regParm,
                                         callingConvention, producesResult,
                                         noCallerSavedRegs, noCfCheck,
                                         cmseNSCall);
    return ctx.getFunctionNoProtoType(returnType, extInfo);
  }]>;
}

let Class = FunctionProtoType in {
  def : Property<"variadic", Bool> {
    let Read = [{ node->isVariadic() }];
  }
  def : Property<"trailingReturn", Bool> {
    let Read = [{ node->hasTrailingReturn() }];
  }
  def : Property<"methodQualifiers", Qualifiers> {
    let Read = [{ node->getMethodQuals() }];
  }
  def : Property<"refQualifier", RefQualifierKind> {
    let Read = [{ node->getRefQualifier() }];
  }
  def : Property<"exceptionSpecifier", ExceptionSpecInfo> {
    let Read = [{ node->getExceptionSpecInfo() }];
  }
  def : Property<"parameters", Array<QualType>> {
    let Read = [{ node->getParamTypes() }];
  }
  def : Property<"extParameterInfo", Array<ExtParameterInfo>> {
    let Read = [{ node->hasExtParameterInfos()
                    ? node->getExtParameterInfos()
                    : llvm::ArrayRef<FunctionProtoType::ExtParameterInfo>() }];
  }
  def : Property<"AArch64SMEAttributes", UInt32> {
    let Read = [{ node->getAArch64SMEAttributes() }];
  }
  def : Property<"functionEffects", Array<FunctionEffect>> {
    let Read = [{ node->getFunctionEffectsWithoutConditions() }];
  }
  def : Property<"functionEffectConds", Array<EffectConditionExpr>> {
    let Read = [{ node->getFunctionEffectConditions() }];
  }

  def : Creator<[{
    auto extInfo = FunctionType::ExtInfo(noReturn, hasRegParm, regParm,
                                         callingConvention, producesResult,
                                         noCallerSavedRegs, noCfCheck,
                                         cmseNSCall);
    FunctionProtoType::ExtProtoInfo epi;
    epi.ExtInfo = extInfo;
    epi.Variadic = variadic;
    epi.HasTrailingReturn = trailingReturn;
    epi.TypeQuals = methodQualifiers;
    epi.RefQualifier = refQualifier;
    epi.ExceptionSpec = exceptionSpecifier;
    epi.ExtParameterInfos =
      extParameterInfo.empty() ? nullptr : extParameterInfo.data();
    epi.AArch64SMEAttributes = AArch64SMEAttributes;
    epi.FunctionEffects = FunctionEffectsRef::create(functionEffects, functionEffectConds);
    return ctx.getFunctionType(returnType, parameters, epi);
  }]>;
}

let Class = AtomicType in {
  def : Property<"valueType", QualType> {
    let Read = [{ node->getValueType() }];
  }

  def : Creator<[{
    return ctx.getAtomicType(valueType);
  }]>;
}

let Class = UnresolvedUsingType in {
  def : Property<"declaration", DeclRef> {
    let Read = [{ node->getDecl() }];
  }

  def : Creator<[{
    return ctx.getUnresolvedUsingType(cast<UnresolvedUsingTypenameDecl>(declaration));
  }]>;
}

let Class = UsingType in {
  def : Property<"foundDeclaration", UsingShadowDeclRef> {
    let Read = [{ node->getFoundDecl() }];
  }
  def : Property<"underlyingType", QualType> {
    let Read = [{ node->getUnderlyingType() }];
  }

  def : Creator<[{
    return ctx.getUsingType(foundDeclaration, underlyingType);
  }]>;
}

let Class = TypedefType in {
  def : Property<"declaration", DeclRef> {
    let Read = [{ node->getDecl() }];
  }
  def : Property<"underlyingType", QualType> {
    let Read = [{ node->desugar() }];
  }

  def : Creator<[{
    return ctx.getTypedefType(cast<TypedefNameDecl>(declaration), underlyingType);
  }]>;
}

let Class = TypeOfExprType in {
  def : Property<"expression", ExprRef> {
    let Read = [{ node->getUnderlyingExpr() }];
  }

  def : Property<"kind", TypeOfKind> {
    let Read = [{ node->getKind() }];
  }

  def : Creator<[{
    return ctx.getTypeOfExprType(expression, kind);
  }]>;
}

let Class = TypeOfType in {
  def : Property<"unmodifiedType", QualType> {
    let Read = [{ node->getUnmodifiedType() }];
  }

  def : Property<"kind", TypeOfKind> {
    let Read = [{ node->getKind() }];
  }

  def : Creator<[{
    return ctx.getTypeOfType(unmodifiedType, kind);
  }]>;
}

let Class = DecltypeType in {
  def : Property<"underlyingType", QualType> {
    let Read = [{ node->getUnderlyingType() }];
  }
  def : Property<"expression", ExprRef> {
    let Read = [{ node->getUnderlyingExpr() }];
  }

  def : Creator<[{
    return ctx.getDecltypeType(expression, underlyingType);
  }]>;
}

let Class = PackIndexingType in {
  def : Property<"pattern", QualType> {
    let Read = [{ node->getPattern() }];
  }
  def : Property<"indexExpression", ExprRef> {
    let Read = [{ node->getIndexExpr() }];
  }

  def : Creator<[{
    return ctx.getPackIndexingType(pattern, indexExpression);
  }]>;
}


let Class = UnaryTransformType in {
  def : Property<"baseType", QualType> {
    let Read = [{ node->getBaseType() }];
  }
  def : Property<"underlyingType", QualType> {
    let Read = [{ node->getUnderlyingType() }];
  }
  def : Property<"transform", UnaryTypeTransformKind> {
    let Read = [{ node->getUTTKind() }];
  }

  def : Creator<[{
    return ctx.getUnaryTransformType(baseType, underlyingType, transform);
  }]>;
}

let Class = AutoType in {
  def : Property<"deducedType", Optional<QualType>> {
    let Read = [{ makeOptionalFromNullable(node->getDeducedType()) }];
  }
  def : Property<"keyword", AutoTypeKeyword> {
    let Read = [{ node->getKeyword() }];
  }
  def : Property<"typeConstraintConcept", Optional<ConceptDeclRef>> {
    let Read = [{ makeOptionalFromPointer(
        const_cast<const ConceptDecl*>(node->getTypeConstraintConcept())) }];
  }
  def : Property<"typeConstraintArguments", Array<TemplateArgument>> {
    let Read = [{ node->getTypeConstraintArguments() }];
  }
  // FIXME: better enumerated value
  // Only really required when the deduced type is null
  def : Property<"dependence", UInt32> {
    let Read = [{ !node->getDeducedType().isNull() ? 0 :
                  node->containsUnexpandedParameterPack() ? 2 :
                  node->isDependentType() ? 1 : 0 }];
  }

  def : Creator<[{
    return ctx.getAutoType(makeNullableFromOptional(deducedType), keyword,
                           /*isDependentWithoutDeducedType*/ dependence > 0,
                           /*isPackWithoutDeducedType*/ dependence > 1,
                           makePointerFromOptional(typeConstraintConcept),
                           typeConstraintArguments);
  }]>;
}

let Class = DeducedTemplateSpecializationType in {
  def : Property<"templateName", Optional<TemplateName>> {
    let Read = [{ makeOptionalFromNullable(node->getTemplateName()) }];
  }
  def : Property<"deducedType", QualType> {
    let Read = [{ node->getDeducedType() }];
  }
  // Only really required when the deduced type is null
  def : Property<"dependent", Bool> {
    let Read = [{ !node->getDeducedType().isNull()
                    ? false : node->isDependentType() }];
  }

  def : Creator<[{
    return ctx.getDeducedTemplateSpecializationType(
                                     makeNullableFromOptional(templateName),
                                     deducedType, dependent);
  }]>;
}

let Class = TagType in {
  def : Property<"dependent", Bool> {
    let Read = [{ node->isDependentType() }];
  }
  def : Property<"declaration", DeclRef> {
    // We don't know which declaration was originally referenced here, and we
    // cannot reference a declaration that follows the use (because that can
    // introduce deserialization cycles), so conservatively generate a
    // reference to the first declaration.
    // FIXME: If this is a reference to a class template specialization, that
    // can still introduce a deserialization cycle.
    let Read = [{ node->getDecl()->getCanonicalDecl() }];
  }
}

let Class = EnumType in {
  def : Creator<[{
    QualType result = ctx.getEnumType(cast<EnumDecl>(declaration));
    if (dependent)
      const_cast<Type *>(result.getTypePtr())
          ->addDependence(TypeDependence::DependentInstantiation);
    return result;
  }]>;
}

let Class = RecordType in {
  def : Creator<[{
    auto record = cast<RecordDecl>(declaration);
    QualType result = ctx.getRecordType(record);
    if (dependent)
      const_cast<Type *>(result.getTypePtr())
          ->addDependence(TypeDependence::DependentInstantiation);
    return result;
  }]>;
}

let Class = ElaboratedType in {
  def : Property<"keyword", ElaboratedTypeKeyword> {
    let Read = [{ node->getKeyword() }];
  }
  def : Property<"qualifier", NestedNameSpecifier> {
    let Read = [{ node->getQualifier() }];
  }
  def : Property<"namedType", QualType> {
    let Read = [{ node->getNamedType() }];
  }
  def : Property<"ownedTag", Optional<TagDeclRef>> {
    let Read = [{ makeOptionalFromPointer(
                    const_cast<const TagDecl *>(node->getOwnedTagDecl())) }];
  }

  def : Creator<[{
    return ctx.getElaboratedType(keyword, qualifier, namedType,
                                 makePointerFromOptional(ownedTag));
  }]>;
}

let Class = InjectedClassNameType in {
  def : Property<"declaration", DeclRef> {
    // FIXME: drilling down to the canonical declaration is what the
    // existing serialization code was doing, but it's not clear why.
    let Read = [{ node->getDecl()->getCanonicalDecl() }];
  }
  def : Property<"injectedSpecializationType", QualType> {
    let Read = [{ node->getInjectedSpecializationType() }];
  }

  def : Creator<[{
    // FIXME: ASTContext::getInjectedClassNameType is not currently suitable
    // for AST reading, too much interdependencies.
    const Type *T = nullptr;
    auto typeDecl = cast<CXXRecordDecl>(declaration);
    for (auto *DI = typeDecl; DI; DI = DI->getPreviousDecl()) {
      if (const Type *existing = DI->getTypeForDecl()) {
        T = existing;
        break;
      }
    }
    if (!T) {
      T = new (ctx, TypeAlignment)
            InjectedClassNameType(typeDecl, injectedSpecializationType);
      for (auto *DI = typeDecl; DI; DI = DI->getPreviousDecl())
        DI->setTypeForDecl(T);
    }
    return QualType(T, 0);
  }]>;
}

let Class = ParenType in {
  def : Property<"innerType", QualType> {
    let Read = [{ node->getInnerType() }];
  }

  def : Creator<[{
    return ctx.getParenType(innerType);
  }]>;
}

let Class = MacroQualifiedType in {
  def : Property<"underlyingType", QualType> {
    let Read = [{ node->getUnderlyingType() }];
  }
  def : Property<"macroIdentifier", Identifier> {
    let Read = [{ node->getMacroIdentifier() }];
  }

  def : Creator<[{
    return ctx.getMacroQualifiedType(underlyingType, macroIdentifier);
  }]>;
}

let Class = AttributedType in {
  def : Property<"modifiedType", QualType> {
    let Read = [{ node->getModifiedType() }];
  }
  def : Property<"equivalentType", QualType> {
    let Read = [{ node->getEquivalentType() }];
  }
  def : Property<"attribute", AttrKind> {
    let Read = [{ node->getAttrKind() }];
  }

  def : Creator<[{
    return ctx.getAttributedType(attribute, modifiedType, equivalentType);
  }]>;
}

let Class = BTFTagAttributedType in {
  def : Property<"attr", BTFTypeTagAttr> {
    let Read = [{ node->getAttr() }];
  }
  def : Property<"wrappedType", QualType> {
    let Read = [{ node->getWrappedType() }];
  }

  def : Creator<[{
    return ctx.getBTFTagAttributedType(attr, wrappedType);
  }]>;
}

let Class = DependentAddressSpaceType in {
  def : Property<"pointeeType", QualType> {
    let Read = [{ node->getPointeeType() }];
  }
  def : Property<"addressSpace", ExprRef> {
    let Read = [{ node->getAddrSpaceExpr() }];
  }
  def : Property<"attributeLoc", SourceLocation> {
    let Read = [{ node->getAttributeLoc() }];
  }

  def : Creator<[{
    return ctx.getDependentAddressSpaceType(pointeeType, addressSpace,
                                            attributeLoc);
  }]>;
}

let Class = TemplateSpecializationType in {
  def : Property<"dependent", Bool> {
    let Read = [{ node->isDependentType() }];
  }
  def : Property<"templateName", TemplateName> {
    let Read = [{ node->getTemplateName() }];
  }
  def : Property<"templateArguments", Array<TemplateArgument>> {
    let Read = [{ node->template_arguments() }];
  }
  def : Property<"underlyingType", Optional<QualType>> {
    let Read = [{
      node->isTypeAlias()
        ? std::optional<QualType>(node->getAliasedType())
        : node->isCanonicalUnqualified()
            ? std::nullopt
            : std::optional<QualType>(node->getCanonicalTypeInternal())
    }];
  }

  def : Creator<[{
    QualType result;
    if (!underlyingType) {
      result = ctx.getCanonicalTemplateSpecializationType(templateName,
                                                          templateArguments);
    } else {
      result = ctx.getTemplateSpecializationType(templateName,
                                                 templateArguments,
                                                 *underlyingType);
    }
    if (dependent)
      const_cast<Type *>(result.getTypePtr())
          ->addDependence(TypeDependence::DependentInstantiation);
    return result;
  }]>;
}

let Class = DependentTemplateSpecializationType in {
  def : Property<"keyword", ElaboratedTypeKeyword> {
    let Read = [{ node->getKeyword() }];
  }
  def : Property<"qualifier", NestedNameSpecifier> {
    let Read = [{ node->getQualifier() }];
  }
  def : Property<"name", Identifier> {
    let Read = [{ node->getIdentifier() }];
  }
  def : Property<"templateArguments", Array<TemplateArgument>> {
    let Read = [{ node->template_arguments() }];
  }

  def : Creator<[{
    return ctx.getDependentTemplateSpecializationType(keyword, qualifier,
                                                      name, templateArguments);
  }]>;
}

let Class = TemplateTypeParmType in {
  def : Property<"depth", UInt32> {
    let Read = [{ node->getDepth() }];
  }
  def : Property<"index", UInt32> {
    let Read = [{ node->getIndex() }];
  }
  def : Property<"isParameterPack", Bool> {
    let Read = [{ node->isParameterPack() }];
  }
  def : Property<"declaration", Optional<TemplateTypeParmDeclRef>> {
    let Read = [{ makeOptionalFromPointer(
                    const_cast<const TemplateTypeParmDecl*>(node->getDecl())) }];
  }

  def : Creator<[{
    return ctx.getTemplateTypeParmType(depth, index, isParameterPack,
                                       makePointerFromOptional(declaration));
  }]>;
}

let Class = SubstTemplateTypeParmType in {
  def : Property<"replacementType", QualType> {
    let Read = [{ node->getReplacementType() }];
  }
  def : Property<"associatedDecl", DeclRef> {
    let Read = [{ node->getAssociatedDecl() }];
  }
  def : Property<"Index", UInt32> {
    let Read = [{ node->getIndex() }];
  }
  def : Property<"PackIndex", Optional<UInt32>> {
    let Read = [{ node->getPackIndex() }];
  }

  // The call to getCanonicalType here existed in ASTReader.cpp, too.
  def : Creator<[{
    return ctx.getSubstTemplateTypeParmType(
        replacementType, associatedDecl, Index, PackIndex);
  }]>;
}

let Class = PackExpansionType in {
  def : Property<"pattern", QualType> {
    let Read = [{ node->getPattern() }];
  }
  def : Property<"numExpansions", Optional<UInt32>> {
    let Read = [{ node->getNumExpansions() }];
  }

  def : Creator<[{
    return ctx.getPackExpansionType(pattern, numExpansions,
                                    /*ExpectPackInType*/false);
  }]>;
}

let Class = SubstTemplateTypeParmPackType in {
  def : Property<"associatedDecl", DeclRef> {
    let Read = [{ node->getAssociatedDecl() }];
  }
  def : Property<"Index", UInt32> {
    let Read = [{ node->getIndex() }];
  }
  def : Property<"Final", Bool> {
    let Read = [{ node->getFinal() }];
  }
  def : Property<"replacementPack", TemplateArgument> {
    let Read = [{ node->getArgumentPack() }];
  }

  def : Creator<[{
    return ctx.getSubstTemplateTypeParmPackType(
                        associatedDecl, Index, Final, replacementPack);
  }]>;
}

let Class = BuiltinType in {
  def : Property<"kind", BuiltinTypeKind> {
    let Read = [{ node->getKind() }];
  }

  def : Creator<[{
      switch (kind) {
#define IMAGE_TYPE(IMGTYPE, ID, SINGLETON_ID, ACCESS, SUFFIX) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/Basic/OpenCLImageTypes.def"

#define EXT_OPAQUE_TYPE(EXTTYPE, ID, EXT) \
      case BuiltinType::ID: return ctx.ID##Ty;
#include "clang/Basic/OpenCLExtensionTypes.def"

#define SVE_TYPE(NAME, ID, SINGLETON_ID) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/Basic/AArch64SVEACLETypes.def"

#define PPC_VECTOR_TYPE(NAME, ID, SIZE) \
      case BuiltinType::ID: return ctx.ID##Ty;
#include "clang/Basic/PPCTypes.def"

#define RVV_TYPE(NAME, ID, SINGLETON_ID) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/Basic/RISCVVTypes.def"

#define WASM_TYPE(NAME, ID, SINGLETON_ID) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/Basic/WebAssemblyReferenceTypes.def"

#define AMDGPU_TYPE(NAME, ID, SINGLETON_ID) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/Basic/AMDGPUTypes.def"

#define BUILTIN_TYPE(ID, SINGLETON_ID) \
      case BuiltinType::ID: return ctx.SINGLETON_ID;
#include "clang/AST/BuiltinTypes.def"
      }
      llvm_unreachable("unreachable builtin case");
  }]>;
}

let Class = DependentNameType in {
  def : Property<"keyword", ElaboratedTypeKeyword> {
    let Read = [{ node->getKeyword() }];
  }
  def : Property<"qualifier", NestedNameSpecifier> {
    let Read = [{ node->getQualifier() }];
  }
  def : Property<"name", Identifier> {
    let Read = [{ node->getIdentifier() }];
  }
  def : Property<"underlyingType", Optional<QualType>> {
    let Read = [{
      node->isCanonicalUnqualified()
        ? std::nullopt
        : std::optional<QualType>(node->getCanonicalTypeInternal())
    }];
  }

  def : Creator<[{
    QualType canon = (underlyingType
                        ? ctx.getCanonicalType(*underlyingType)
                        : QualType());
    return ctx.getDependentNameType(keyword, qualifier, name, canon);
  }]>;
}

let Class = ObjCObjectType in {
  def : Property<"baseType", QualType> {
    let Read = [{ node->getBaseType() }];
  }
  def : Property<"typeArgsAsWritten", Array<QualType>> {
    let Read = [{ node->getTypeArgsAsWritten() }];
  }
  def : Property<"qualifiers", Array<ObjCProtocolDeclRef>> {
    let Read = [{ node->getProtocols() }];
  }
  def : Property<"isKindOfTypeAsWritten", Bool> {
    let Read = [{ node->isKindOfTypeAsWritten() }];
  }

  def : Creator<[{
    return ctx.getObjCObjectType(baseType, typeArgsAsWritten, qualifiers,
                                 isKindOfTypeAsWritten);
  }]>;
}

let Class = ObjCInterfaceType in {
  // We don't actually want any of the properties of the superclass.
  def : Override {
    let IgnoredProperties = [ "baseType", "typeArgsAsWritten",
                              "qualifiers", "isKindOfTypeAsWritten" ];
  }

  def : Property<"declaration", DeclRef> {
    // FIXME: drilling down to the canonical declaration is what the
    // existing serialization code was doing, but it's not clear why.
    let Read = [{ node->getDecl()->getCanonicalDecl() }];
  }

  def : Creator<[{
    return ctx.getObjCInterfaceType(
             cast<ObjCInterfaceDecl>(declaration->getCanonicalDecl()));
  }]>;
}

let Class = ObjCTypeParamType in {
  def : Property<"declaration", ObjCTypeParamDeclRef> {
    let Read = [{ node->getDecl() }];
  }
  def : Property<"qualifiers", Array<ObjCProtocolDeclRef>> {
    let Read = [{ node->getProtocols() }];
  }

  def : Creator<[{
    return ctx.getObjCTypeParamType(declaration, qualifiers);
  }]>;
}

let Class = ObjCObjectPointerType in {
  def : Property<"pointeeType", QualType> {
    let Read = [{ node->getPointeeType() }];
  }

  def : Creator<[{
    return ctx.getObjCObjectPointerType(pointeeType);
  }]>;
}

let Class = PipeType in {
  def : Property<"elementType", QualType> {
    let Read = [{ node->getElementType() }];
  }
  def : Property<"isReadOnly", Bool> {
    let Read = [{ node->isReadOnly() }];
  }

  def : Creator<[{
    return ctx.getPipeType(elementType, isReadOnly);
  }]>;
}

let Class = BitIntType in {
  def : Property<"isUnsigned", Bool> {
    let Read = [{ node->isUnsigned() }];
  }
  def : Property <"numBits", UInt32> {
    let Read = [{ node->getNumBits() }];
  }

  def : Creator<[{
    return ctx.getBitIntType(isUnsigned, numBits);
  }]>;
}

let Class = DependentBitIntType in {
  def : Property<"isUnsigned", Bool> {
    let Read = [{ node->isUnsigned() }];
  }
  def : Property <"numBitsExpr", ExprRef> {
    let Read = [{ node->getNumBitsExpr() }];
  }
  def : Creator<[{
    return ctx.getDependentBitIntType(isUnsigned, numBitsExpr);
  }]>;
}
