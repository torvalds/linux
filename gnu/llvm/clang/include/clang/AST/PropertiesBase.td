//==--- PropertiesBase.td - Baseline definitions for AST properties -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class HasProperties;

/// The type of the property.
class PropertyType<string typeName = ""> {
  /// The C++ type name for the type.
  string CXXName = !if(!ne(typeName, ""), typeName, NAME);

  /// Whether the C++ type should generally be passed around by reference.
  bit PassByReference = 0;

  /// Whether `const` should be prepended to the type when writing.
  bit ConstWhenWriting = 0;

  /// Given a value of type Optional<CXXName> bound as 'value', yield a
  /// CXXName that can be serialized into a DataStreamTypeWriter.
  string PackOptional = "";

  /// Given a value of type CXXName bound as 'value' that was deserialized
  /// by a DataStreamTypeReader, yield an Optional<CXXName>.
  string UnpackOptional = "";

  /// A list of types for which buffeers must be passed to the read
  /// operations.
  list<PropertyType> BufferElementTypes = [];
}

/// Property types that correspond to specific C++ enums.
class EnumPropertyType<string typeName = ""> : PropertyType<typeName> {}

/// Property types that correspond to a specific C++ class.
/// Supports optional values by using the null representation.
class RefPropertyType<string className> : PropertyType<className # "*"> {
  let PackOptional =
    "value ? *value : nullptr";
  let UnpackOptional =
    "value ? std::optional<" # CXXName # ">(value) : std::nullopt";
}

/// Property types that correspond to a specific subclass of another type.
class SubclassPropertyType<string className, PropertyType base>
    : RefPropertyType<className> {
  PropertyType Base = base;
  string SubclassName = className;
  let ConstWhenWriting = base.ConstWhenWriting;
}

/// Property types that support optional values by using their
/// default value.
class DefaultValuePropertyType<string typeName = ""> : PropertyType<typeName> {
  let PackOptional =
    "value ? *value : " # CXXName # "()";
  let UnpackOptional =
    "value.isNull() ? std::nullopt : std::optional<" # CXXName # ">(value)";
}

/// Property types that correspond to integer types and support optional
/// values by shifting the value over by 1.
class CountPropertyType<string typeName = ""> : PropertyType<typeName> {
  let PackOptional =
    "value ? *value + 1 : 0";
  let UnpackOptional =
    "value ? std::optional<" # CXXName # ">(value - 1) : std::nullopt";
}

def APInt : PropertyType<"llvm::APInt"> { let PassByReference = 1; }
def APSInt : PropertyType<"llvm::APSInt"> { let PassByReference = 1; }
def APValue : PropertyType { let PassByReference = 1; }
def APValueKind : EnumPropertyType<"APValue::ValueKind">;
def ArraySizeModifier : EnumPropertyType<"ArraySizeModifier">;
def AttrKind : EnumPropertyType<"attr::Kind">;
def AutoTypeKeyword : EnumPropertyType;
def Bool : PropertyType<"bool">;
def BuiltinTypeKind : EnumPropertyType<"BuiltinType::Kind">;
def BTFTypeTagAttr : PropertyType<"const BTFTypeTagAttr *">;
def CallingConv : EnumPropertyType;
def DeclarationName : PropertyType;
def DeclarationNameKind : EnumPropertyType<"DeclarationName::NameKind">;
def DeclRef : RefPropertyType<"Decl"> { let ConstWhenWriting = 1; }
  def CXXRecordDeclRef :
    SubclassPropertyType<"CXXRecordDecl", DeclRef>;
  def FunctionDeclRef :
    SubclassPropertyType<"FunctionDecl", DeclRef>;
  def NamedDeclRef :
    SubclassPropertyType<"NamedDecl", DeclRef>;
  def NamespaceDeclRef :
    SubclassPropertyType<"NamespaceDecl", DeclRef>;
  def NamespaceAliasDeclRef :
    SubclassPropertyType<"NamespaceAliasDecl", DeclRef>;
  def ObjCProtocolDeclRef :
    SubclassPropertyType<"ObjCProtocolDecl", DeclRef>;
  def ObjCTypeParamDeclRef :
    SubclassPropertyType<"ObjCTypeParamDecl", DeclRef>;
  def TagDeclRef :
    SubclassPropertyType<"TagDecl", DeclRef>;
  def TemplateDeclRef :
    SubclassPropertyType<"TemplateDecl", DeclRef>;
  def ConceptDeclRef :
    SubclassPropertyType<"ConceptDecl", DeclRef>;
  def TemplateTypeParmDeclRef :
    SubclassPropertyType<"TemplateTypeParmDecl", DeclRef>;
  def TemplateTemplateParmDeclRef :
    SubclassPropertyType<"TemplateTemplateParmDecl", DeclRef>;
  def UsingShadowDeclRef :
    SubclassPropertyType<"UsingShadowDecl", DeclRef>;
  def ValueDeclRef :
    SubclassPropertyType<"ValueDecl", DeclRef>;
def ElaboratedTypeKeyword : EnumPropertyType;
def ExtParameterInfo : PropertyType<"FunctionProtoType::ExtParameterInfo">;
def FixedPointSemantics : PropertyType<"llvm::FixedPointSemantics"> {
  let PassByReference = 1;
}
def FunctionEffect : PropertyType<"FunctionEffect">;
def EffectConditionExpr : PropertyType<"EffectConditionExpr">;
def Identifier : RefPropertyType<"IdentifierInfo"> { let ConstWhenWriting = 1; }
def LValuePathEntry : PropertyType<"APValue::LValuePathEntry">;
def LValuePathSerializationHelper :
    PropertyType<"APValue::LValuePathSerializationHelper"> {
  let BufferElementTypes = [ LValuePathEntry ];
}
def NestedNameSpecifier : PropertyType<"NestedNameSpecifier *">;
def NestedNameSpecifierKind :
  EnumPropertyType<"NestedNameSpecifier::SpecifierKind">;
def OverloadedOperatorKind : EnumPropertyType;
def Qualifiers : PropertyType;
def QualType : DefaultValuePropertyType;
def RefQualifierKind : EnumPropertyType;
def Selector : PropertyType;
def SourceLocation : PropertyType;
def StmtRef : RefPropertyType<"Stmt"> { let ConstWhenWriting = 1; }
  def ExprRef : SubclassPropertyType<"Expr", StmtRef>;
def TemplateArgument : PropertyType;
def TemplateArgumentKind : EnumPropertyType<"TemplateArgument::ArgKind">;
def TemplateName : DefaultValuePropertyType;
def TemplateNameKind : EnumPropertyType<"TemplateName::NameKind">;
def TypeOfKind : EnumPropertyType<"TypeOfKind">;
def UInt32 : CountPropertyType<"uint32_t">;
def UInt64 : CountPropertyType<"uint64_t">;
def UnaryTypeTransformKind : EnumPropertyType<"UnaryTransformType::UTTKind">;
def VectorKind : EnumPropertyType<"VectorKind">;
def TypeCoupledDeclRefInfo : PropertyType;

def ExceptionSpecInfo : PropertyType<"FunctionProtoType::ExceptionSpecInfo"> {
  let BufferElementTypes = [ QualType ];
}

/// Arrays.  The corresponding C++ type is ArrayRef of the corresponding
/// C++ type of the element.
class Array<PropertyType element> : PropertyType {
  PropertyType Element = element;
  let BufferElementTypes = [ element ];
}

/// std::optional<T>.  The corresponding C++ type is generally just the
/// corresponding C++ type of the element.
///
/// Optional<Unsigned> may restrict the range of the operand for some
/// serialization clients.
class Optional<PropertyType element> : PropertyType {
  PropertyType Element = element;
  let PassByReference = element.PassByReference;
}

/// A property of an AST node.
class Property<string name, PropertyType type> {
  HasProperties Class;
  string Name = name;
  PropertyType Type = type;

  /// A function for reading the property, expressed in terms of a variable
  /// "node".
  code Read;

  /// Code specifying when this property is available.  Can be defined
  /// in terms of other properties, in which case this property must be
  /// read/written after those properties.  Using this will make the
  /// value Optional when deserializing.
  ///
  /// FIXME: the emitter doesn't yet force dependent properties to be
  /// read/written later; this only works if the properties used in the
  /// condition happen to be written first.
  code Conditional = "";
}

/// A rule for declaring helper variables when read properties from a
/// value of this type.  Note that this means that this code is actually
/// run when *writing* values of this type; however, naming this
/// `ReadHelper` makes the connection to the `Read` operations on the
/// properties much clearer.
class ReadHelper<code _code> {
  HasProperties Class;

  /// Code which will be run when writing objects of this type before
  /// writing any of the properties, specified in terms of a variable
  /// `node`.
  code Code = _code;
}

/// A rule for creating objects of this type.
class Creator<code create> {
  HasProperties Class;

  /// A function for creating values of this kind, expressed in terms of a
  /// variable `ctx` of type `ASTContext &`.  Must also refer to all of the
  /// properties by name.
  code Create = create;
}

/// A rule which overrides some of the normal rules.
class Override {
  HasProperties Class;

  /// Properties from base classes that should be ignored.
  list<string> IgnoredProperties = [];
}

/// A description of how to break a type into cases.  Providing this and
/// an exhaustive list of the cases will cause AbstractBasic{Reader,Writer}
/// to be generated with a default implementation of how to read the
/// type.
///
/// Creator rules for the cases can additionally access a variable
/// `kind` of the KindType.
class PropertyTypeKind<PropertyType type,
                       PropertyType kindType,
                       string readCode> {
  /// The type for which this describes cases.
  PropertyType Type = type;

  /// The type of this type's kind enum.
  PropertyType KindType = kindType;

  /// The property name to use for the kind.
  string KindPropertyName = "kind";

  /// An expression which reads the kind from a value, expressed in terms
  /// of a variable `node`.
  string Read = readCode;
}

/// One of the options for representing a particular type.
class PropertyTypeCase<PropertyType type, string name> : HasProperties {
  /// The type of which this is a case.
  PropertyType Type = type;

  /// The name of the case (a value of the type's kind enum).
  string Name = name;
}

// Type cases for APValue.
def : PropertyTypeKind<APValue, APValueKind,
                       "node.getKind()">;
let Class = PropertyTypeCase<APValue, "None"> in {
  def : Creator<[{ return APValue(); }]>;
}
let Class = PropertyTypeCase<APValue, "Indeterminate"> in {
  def : Creator<[{ return APValue::IndeterminateValue(); }]>;
}
let Class = PropertyTypeCase<APValue, "Int"> in {
  def : Property<"value", APSInt> {
    let Read = [{ node.getInt() }];
  }
  def : Creator<[{ return APValue(value); }]>;
}
let Class = PropertyTypeCase<APValue, "Float"> in {
  def : Property<"semantics", UInt32> {
    let Read = [{
      static_cast<uint32_t>(
        llvm::APFloatBase::SemanticsToEnum(node.getFloat().getSemantics()))
    }];
  }
  def : Property<"value", APInt> {
    let Read = [{ node.getFloat().bitcastToAPInt() }];
  }
  def : Creator<[{
    const llvm::fltSemantics &floatSema = llvm::APFloatBase::EnumToSemantics(
        static_cast<llvm::APFloatBase::Semantics>(semantics));
    return APValue(llvm::APFloat(floatSema, value));
  }]>;
}
let Class = PropertyTypeCase<APValue, "FixedPoint"> in {
  def : Property<"semantics", FixedPointSemantics> {
    let Read = [{ node.getFixedPoint().getSemantics() }];
  }
  def : Property<"value", APSInt> {
    let Read = [{ node.getFixedPoint().getValue() }];
  }
  def : Creator<[{
    return APValue(llvm::APFixedPoint(std::move(value), semantics));
  }]>;
}
let Class = PropertyTypeCase<APValue, "ComplexInt"> in {
  def : Property<"real", APSInt> {
    let Read = [{ node.getComplexIntReal() }];
  }
  def : Property<"imag", APSInt> {
    let Read = [{ node.getComplexIntImag() }];
  }
  def : Creator<[{ return APValue(real, imag); }]>;
}
let Class = PropertyTypeCase<APValue, "ComplexFloat"> in {
  def : ReadHelper<[{
    auto sema = llvm::APFloatBase::SemanticsToEnum(
        node.getComplexFloatReal().getSemantics());
    assert(sema == llvm::APFloatBase::SemanticsToEnum(
        node.getComplexFloatImag().getSemantics()));
  }]>;
  def : Property<"semantics", UInt32> {
    let Read = [{ static_cast<uint32_t>(sema) }];
  }
  def : Property<"real", APInt> {
    let Read = [{ node.getComplexFloatReal().bitcastToAPInt() }];
  }
  def : Property<"imag", APInt> {
    let Read = [{ node.getComplexFloatImag().bitcastToAPInt() }];
  }
  def : Creator<[{
    const llvm::fltSemantics &sema = llvm::APFloatBase::EnumToSemantics(
        static_cast<llvm::APFloatBase::Semantics>(semantics));
    return APValue(llvm::APFloat(sema, real),
                   llvm::APFloat(sema, imag));
  }]>;
}
let Class = PropertyTypeCase<APValue, "Vector"> in {
  def : ReadHelper<[{
    SmallVector<APValue, 4> buffer;
    unsigned len = node.getVectorLength();
    for (unsigned i = 0; i < len; ++i)
      buffer.push_back(node.getVectorElt(i));
  }]>;
  def : Property<"elements", Array<APValue>> {
    let Read = [{ buffer }];
  }
  def : Creator<[{
    APValue result;
    result.MakeVector();
    unsigned length = elements.size();
    (void)result.setVectorUninit(length);
    for (unsigned i = 0; i < length; i++)
      result.getVectorElt(i) = elements[i];
    return result;
  }]>;
}
let Class = PropertyTypeCase<APValue, "Array"> in {
  def : ReadHelper<[{
    SmallVector<APValue, 4> buffer{};
    unsigned initLength = node.getArrayInitializedElts();
    for (unsigned i = 0; i < initLength; ++i)
      buffer.push_back(node.getArrayInitializedElt(i));
    if (node.hasArrayFiller())
      buffer.push_back(node.getArrayFiller());
  }]>;
  def : Property<"totalLength", UInt32> {
    let Read = [{ node.getArraySize() }];
  }
  def : Property<"hasFiller", Bool> {
    let Read = [{ node.hasArrayFiller() }];
  }
  def : Property<"elements", Array<APValue>> {
    let Read = [{ buffer }];
  }
  def : Creator<[{
    APValue result;
    unsigned initLength = elements.size() - (hasFiller ? 1 : 0);
    result.MakeArray(initLength, totalLength);
    for (unsigned i = 0; i < initLength; ++i)
      result.getArrayInitializedElt(i) = elements[i];
    if (hasFiller)
      result.getArrayFiller() = elements.back();
    return result;
  }]>;
}
let Class = PropertyTypeCase<APValue, "Struct"> in {
  def : ReadHelper<[{
    SmallVector<APValue, 4> structBases;
    unsigned numBases = node.getStructNumBases();
    for (unsigned i = 0; i < numBases; ++i)
      structBases.push_back(node.getStructBase(i));
    SmallVector<APValue, 4> structFields;
    unsigned numFields = node.getStructNumFields();
    for (unsigned i = 0; i < numFields; ++i)
      structFields.push_back(node.getStructField(i));
  }]>;
  def : Property<"bases", Array<APValue>> {
    let Read = [{ structBases }];
  }
  def : Property<"fields", Array<APValue>> {
    let Read = [{ structFields }];
  }
  def : Creator<[{
    APValue result;
    result.MakeStruct(bases.size(), fields.size());
    for (unsigned i = 0; i < bases.size(); ++i)
      result.getStructBase(i) = bases[i];
    for (unsigned i = 0; i < fields.size(); ++i)
      result.getStructField(i) = fields[i];
    return result;
  }]>;
}
let Class = PropertyTypeCase<APValue, "Union"> in {
  def : Property<"fieldDecl", DeclRef> {
    let Read = [{ node.getUnionField() }];
  }
  def : Property<"value", APValue> {
    let Read = [{ node.getUnionValue() }];
  }
  def : Creator<[{
    return APValue(cast<clang::FieldDecl>(fieldDecl), std::move(value));
  }]>;
}
let Class = PropertyTypeCase<APValue, "AddrLabelDiff"> in {
  def : Property<"lhs", StmtRef> {
    let Read = [{ const_cast<AddrLabelExpr *>(node.getAddrLabelDiffLHS()) }];
  }
  def : Property<"rhs", StmtRef> {
    let Read = [{ const_cast<AddrLabelExpr *>(node.getAddrLabelDiffRHS()) }];
  }
  def : Creator<[{
    return APValue(cast<AddrLabelExpr>(lhs), cast<AddrLabelExpr>(rhs));
  }]>;
}
let Class = PropertyTypeCase<APValue, "MemberPointer"> in {
  def : Property<"isDerived", Bool> {
    let Read = [{ node.isMemberPointerToDerivedMember() }];
  }
  def : Property<"member", ValueDeclRef> {
    let Read = [{ node.getMemberPointerDecl() }];
  }
  def : Property<"memberPath", Array<CXXRecordDeclRef>> {
    let Read = [{ node.getMemberPointerPath() }];
  }
  def : Creator<[{
    APValue result;
    unsigned pathSize = memberPath.size();
    const CXXRecordDecl **pathArray =
        result.setMemberPointerUninit(member, isDerived, pathSize).data();
    for (unsigned i = 0; i < pathSize; ++i)
      pathArray[i] = memberPath[i]->getCanonicalDecl();
    return result;
  }]>;
}
let Class = PropertyTypeCase<APValue, "LValue"> in {
  def : ReadHelper<[{
    auto lvalueBase = node.getLValueBase();
    const Expr *expr =
        lvalueBase ? lvalueBase.dyn_cast<const Expr *>() : nullptr;
    bool lvalueBaseIsExpr = (bool) expr;
    bool lvalueBaseIsTypeInfo = lvalueBase.is<TypeInfoLValue>();
    bool lvalueBaseIsDynamicAlloc = lvalueBase.is<DynamicAllocLValue>();
    QualType elemTy;
    if (lvalueBase) {
      if (lvalueBaseIsTypeInfo) {
        elemTy = lvalueBase.getTypeInfoType();
      } else if (lvalueBaseIsDynamicAlloc) {
        elemTy = lvalueBase.getDynamicAllocType();
      } else if (lvalueBaseIsExpr) {
        elemTy = expr->getType();
      } else {
        elemTy = lvalueBase.get<const ValueDecl *>()->getType();
      }
    }
  }]>;
  def : Property<"hasLValuePath", Bool> {
    let Read = [{ node.hasLValuePath() }];
  }
  def : Property<"isLValueOnePastTheEnd", Bool> {
    let Read = [{ node.isLValueOnePastTheEnd() }];
  }
  def : Property<"isExpr", Bool> {
    let Read = [{ lvalueBaseIsExpr }];
  }
  def : Property<"isTypeInfo", Bool> {
    let Read = [{ lvalueBaseIsTypeInfo }];
  }
  def : Property<"isDynamicAlloc", Bool> {
    let Read = [{ lvalueBaseIsDynamicAlloc }];
  }
  def : Property<"hasBase", Bool> {
    let Read = [{ static_cast<bool>(lvalueBase) }];
  }
  def : Property<"isNullPtr", Bool> {
    let Read = [{ node.isNullPointer() }];
  }
  def : Property<"typeInfo", QualType> {
    let Conditional = [{ hasBase && isTypeInfo }];
    let Read = [{
      QualType(node.getLValueBase().get<TypeInfoLValue>().getType(), 0)
    }];
  }
  def : Property<"dynamicAlloc", UInt32> {
    let Conditional = [{ hasBase && isDynamicAlloc }];
    let Read = [{ node.getLValueBase().get<DynamicAllocLValue>().getIndex() }];
  }
  def : Property<"type", QualType> {
    let Conditional = [{ hasBase && (isTypeInfo || isDynamicAlloc) }];
    let Read = [{
      isTypeInfo
        ? node.getLValueBase().getTypeInfoType()
        : node.getLValueBase().getDynamicAllocType()
    }];
  }
  def : Property<"callIndex", UInt32> {
    let Conditional = [{ hasBase && !isTypeInfo }];
    let Read = [{ node.getLValueBase().getCallIndex() }];
  }
  def : Property<"version", UInt32> {
    let Conditional = [{ hasBase && !isTypeInfo }];
    let Read = [{ node.getLValueBase().getVersion() }];
  }
  def : Property<"stmt", StmtRef> {
    let Conditional = [{ hasBase && !isTypeInfo && isExpr }];
    let Read = [{ const_cast<Expr *>(expr) }];
  }
  def : Property<"decl", DeclRef> {
    let Conditional = [{ hasBase && !isTypeInfo && !isDynamicAlloc && !isExpr }];
    let Read = [{ lvalueBase.get<const ValueDecl *>() }];
  }
  def : Property<"offsetQuantity", UInt32> {
    let Read = [{ node.getLValueOffset().getQuantity() }];
  }
  def : Property<"lvaluePath", LValuePathSerializationHelper> {
    let Conditional = [{ hasLValuePath }];
    let Read = [{
      APValue::LValuePathSerializationHelper(node.getLValuePath(), elemTy)
    }];
  }
  def : Creator<[{
    (void)ctx;
    APValue::LValueBase base;
    if (hasBase) {
      if (isTypeInfo) {
        base = APValue::LValueBase::getTypeInfo(
            TypeInfoLValue(typeInfo->getTypePtr()), *type);
      } else if (isDynamicAlloc) {
        base = APValue::LValueBase::getDynamicAlloc(
            DynamicAllocLValue(*dynamicAlloc), *type);
      } else if (isExpr) {
        base = APValue::LValueBase(cast<Expr>(*stmt),
                                   *callIndex, *version);
      } else {
        base = APValue::LValueBase(cast<ValueDecl>(*decl),
                                   *callIndex, *version);
      }
    }
    CharUnits offset = CharUnits::fromQuantity(offsetQuantity);
    APValue result;
    result.MakeLValue();
    if (!hasLValuePath) {
      result.setLValue(base, offset, APValue::NoLValuePath{}, isNullPtr);
      return result;
    }
    auto pathLength = lvaluePath->Path.size();
    APValue::LValuePathEntry *path = result.setLValueUninit(
        base, offset, pathLength, isLValueOnePastTheEnd, isNullPtr).data();
    llvm::copy(lvaluePath->Path, path);
    return result;
  }]>;
}

// Type cases for DeclarationName.
def : PropertyTypeKind<DeclarationName, DeclarationNameKind,
                       "node.getNameKind()">;
let Class = PropertyTypeCase<DeclarationName, "Identifier"> in {
  def : Property<"identifier", Identifier> {
    let Read = [{ node.getAsIdentifierInfo() }];
  }
  def : Creator<[{
    return DeclarationName(identifier);
  }]>;
}
foreach count = ["Zero", "One", "Multi"] in {
  let Class = PropertyTypeCase<DeclarationName, "ObjC"#count#"ArgSelector"> in {
    def : Property<"selector", Selector> {
      let Read = [{ node.getObjCSelector() }];
    }
    def : Creator<[{
      return DeclarationName(selector);
    }]>;
  }
}
foreach kind = ["Constructor", "Destructor", "ConversionFunction"] in {
  let Class = PropertyTypeCase<DeclarationName, "CXX"#kind#"Name"> in {
    def : Property<"type", QualType> {
      let Read = [{ node.getCXXNameType() }];
    }
    def : Creator<[{
      return ctx.DeclarationNames.getCXX}]#kind#[{Name(
               ctx.getCanonicalType(type));
    }]>;
  }
}
let Class = PropertyTypeCase<DeclarationName, "CXXDeductionGuideName"> in {
  def : Property<"declaration", TemplateDeclRef> {
    let Read = [{ node.getCXXDeductionGuideTemplate() }];
  }
  def : Creator<[{
    return ctx.DeclarationNames.getCXXDeductionGuideName(declaration);
  }]>;
}
let Class = PropertyTypeCase<DeclarationName, "CXXOperatorName"> in {
  def : Property<"operatorKind", OverloadedOperatorKind> {
    let Read = [{ node.getCXXOverloadedOperator() }];
  }
  def : Creator<[{
    return ctx.DeclarationNames.getCXXOperatorName(operatorKind);
  }]>;
}
let Class = PropertyTypeCase<DeclarationName, "CXXLiteralOperatorName"> in {
  def : Property<"identifier", Identifier> {
    let Read = [{ node.getCXXLiteralIdentifier() }];
  }
  def : Creator<[{
    return ctx.DeclarationNames.getCXXLiteralOperatorName(identifier);
  }]>;
}
let Class = PropertyTypeCase<DeclarationName, "CXXUsingDirective"> in {
  def : Creator<[{
    return DeclarationName::getUsingDirectiveName();
  }]>;
}

// Type cases for TemplateName.
def : PropertyTypeKind<TemplateName, TemplateNameKind, "node.getKind()">;
let Class = PropertyTypeCase<TemplateName, "Template"> in {
  def : Property<"declaration", TemplateDeclRef> {
    let Read = [{ node.getAsTemplateDecl() }];
  }
  def : Creator<[{
    return TemplateName(declaration);
  }]>;
}

let Class = PropertyTypeCase<TemplateName, "UsingTemplate"> in {
  def : Property<"foundDecl", UsingShadowDeclRef> {
    let Read = [{ node.getAsUsingShadowDecl() }];
  }
  def : Creator<[{
    return TemplateName(foundDecl);
  }]>;
}

let Class = PropertyTypeCase<TemplateName, "OverloadedTemplate"> in {
  def : Property<"overloads", Array<NamedDeclRef>> {
    let Read = [{ node.getAsOverloadedTemplate()->decls() }];
  }
  def : Creator<[{
    // Copy into an UnresolvedSet to satisfy the interface.
    UnresolvedSet<8> overloadSet;
    for (auto overload : overloads) {
      overloadSet.addDecl(overload);
    }

    return ctx.getOverloadedTemplateName(overloadSet.begin(),
                                         overloadSet.end());
  }]>;
}
let Class = PropertyTypeCase<TemplateName, "AssumedTemplate"> in {
  def : Property<"name", DeclarationName> {
    let Read = [{ node.getAsAssumedTemplateName()->getDeclName() }];
  }
  def : Creator<[{
    return ctx.getAssumedTemplateName(name);
  }]>;
}
let Class = PropertyTypeCase<TemplateName, "QualifiedTemplate"> in {
  def : ReadHelper<[{
    auto qtn = node.getAsQualifiedTemplateName();
  }]>;
  def : Property<"qualifier", NestedNameSpecifier> {
    let Read = [{ qtn->getQualifier() }];
  }
  def : Property<"hasTemplateKeyword", Bool> {
    let Read = [{ qtn->hasTemplateKeyword() }];
  }
  def : Property<"underlyingTemplateName", TemplateName> {
    let Read = [{ qtn->getUnderlyingTemplate() }];
  }
  def : Creator<[{
    return ctx.getQualifiedTemplateName(qualifier, hasTemplateKeyword,
                                        underlyingTemplateName);
  }]>;
}
let Class = PropertyTypeCase<TemplateName, "DependentTemplate"> in {
  def : ReadHelper<[{
    auto dtn = node.getAsDependentTemplateName();
  }]>;
  def : Property<"qualifier", NestedNameSpecifier> {
    let Read = [{ dtn->getQualifier() }];
  }
  def : Property<"identifier", Optional<Identifier>> {
    let Read = [{ makeOptionalFromPointer(
                    dtn->isIdentifier()
                      ? dtn->getIdentifier()
                      : nullptr) }];
  }
  def : Property<"operatorKind", OverloadedOperatorKind> {
    let Conditional = [{ !identifier }];
    let Read = [{ dtn->getOperator() }];
  }
  def : Creator<[{
    if (identifier) {
      return ctx.getDependentTemplateName(qualifier, *identifier);
    } else {
      return ctx.getDependentTemplateName(qualifier, *operatorKind);
    }
  }]>;
}
let Class = PropertyTypeCase<TemplateName, "SubstTemplateTemplateParm"> in {
  def : ReadHelper<[{
    auto parm = node.getAsSubstTemplateTemplateParm();
  }]>;
  def : Property<"replacement", TemplateName> {
    let Read = [{ parm->getReplacement() }];
  }
  def : Property<"associatedDecl", DeclRef> {
    let Read = [{ parm->getAssociatedDecl() }];
  }
  def : Property<"index", UInt32> {
    let Read = [{ parm->getIndex() }];
  }
  def : Property<"packIndex", Optional<UInt32>> {
    let Read = [{ parm->getPackIndex() }];
  }
  def : Creator<[{
    return ctx.getSubstTemplateTemplateParm(replacement, associatedDecl, index, packIndex);
  }]>;
}
let Class = PropertyTypeCase<TemplateName, "SubstTemplateTemplateParmPack"> in {
  def : ReadHelper<[{
    auto parm = node.getAsSubstTemplateTemplateParmPack();
  }]>;
  def : Property<"argumentPack", TemplateArgument> {
    let Read = [{ parm->getArgumentPack() }];
  }
  def : Property<"associatedDecl", DeclRef> {
    let Read = [{ parm->getAssociatedDecl() }];
  }
  def : Property<"index", UInt32> {
    let Read = [{ parm->getIndex() }];
  }
  def : Property<"final", Bool> {
    let Read = [{ parm->getFinal() }];
  }
  def : Creator<[{
    return ctx.getSubstTemplateTemplateParmPack(argumentPack, associatedDecl, index, final);
  }]>;
}

// Type cases for TemplateArgument.
def : PropertyTypeKind<TemplateArgument, TemplateArgumentKind,
                       "node.getKind()">;
let Class = PropertyTypeCase<TemplateArgument, "Null"> in {
  def : Creator<[{
    return TemplateArgument();
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Type"> in {
  def : Property<"type", QualType> {
    let Read = [{ node.getAsType() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(type, /* isNullPtr */ false, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Declaration"> in {
  def : Property<"declaration", ValueDeclRef> {
    let Read = [{ node.getAsDecl() }];
  }
  def : Property<"parameterType", QualType> {
    let Read = [{ node.getParamTypeForDecl() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(declaration, parameterType, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "NullPtr"> in {
  def : Property<"type", QualType> {
    let Read = [{ node.getNullPtrType() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(type, /*nullptr*/ true, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Integral"> in {
  def : Property<"value", APSInt> {
    let Read = [{ node.getAsIntegral() }];
  }
  def : Property<"type", QualType> {
    let Read = [{ node.getIntegralType() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(ctx, value, type, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "StructuralValue"> in {
  def : Property<"value", APValue> {
    let Read = [{ node.getAsStructuralValue() }];
  }
  def : Property<"type", QualType> {
    let Read = [{ node.getStructuralValueType() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(ctx, type, value, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Template"> in {
  def : Property<"name", TemplateName> {
    let Read = [{ node.getAsTemplateOrTemplatePattern() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(name, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "TemplateExpansion"> in {
  def : Property<"name", TemplateName> {
    let Read = [{ node.getAsTemplateOrTemplatePattern() }];
  }
  def : Property<"numExpansions", Optional<UInt32>> {
    let Read = [{
      // Translate unsigned -> uint32_t just in case.
      llvm::transformOptional(node.getNumTemplateExpansions(),
                              [](unsigned i) { return uint32_t(i); })
    }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    auto numExpansionsUnsigned = llvm::transformOptional(
        numExpansions, [](uint32_t i) { return unsigned(i); });

    return TemplateArgument(name, numExpansionsUnsigned, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Expression"> in {
  def : Property<"expression", ExprRef> {
    let Read = [{ node.getAsExpr() }];
  }
  def : Property<"isDefaulted", Bool> {
    let Read = [{ node.getIsDefaulted() }];
  }
  def : Creator<[{
    return TemplateArgument(expression, isDefaulted);
  }]>;
}
let Class = PropertyTypeCase<TemplateArgument, "Pack"> in {
  def : Property<"elements", Array<TemplateArgument>> {
    let Read = [{ node.pack_elements() }];
  }
  def : Creator<[{
    // Copy the pack into the ASTContext.
    TemplateArgument *ctxElements = new (ctx) TemplateArgument[elements.size()];
    for (size_t i = 0, e = elements.size(); i != e; ++i)
      ctxElements[i] = elements[i];
    return TemplateArgument(llvm::ArrayRef(ctxElements, elements.size()));
  }]>;
}
