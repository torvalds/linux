include "clang/Basic/ASTNode.td"

class TypeNode<TypeNode base, bit abstract = 0> : ASTNode {
	TypeNode Base = base;
  bit Abstract = abstract;
}

/// A type node that is only used to represent dependent types in C++.  For
/// example, DependentTemplateSpecializationType is used to represent types
/// where the base template-id is dependent (such as `T::foo<U>`).  Code
/// that only works with non-dependent types can ignore these type nodes.
class AlwaysDependent {}

/// A type node that is never used to represent a canonical type, which is to
/// say that it always represents some sort of type "sugar" which can
/// (supposedly) be erased without affecting the formal behavior of the
/// language.  For example, in standard C/C++, typedefs do not introduce new
/// types and do not affect the semantics of the program.  Code that only
/// works with canonical types can ignore these type nodes.
///
/// Note that this simple story about non-canonical types is not the whole
/// truth.  Languages and extensions often have formation rules which differ
/// based on how a type is spelled and which therefore are not consistent
/// with immediately stipping away type sugar.  More critically, attributes on
/// typedefs can have semantic impacts in ways that are only reflected in our
/// AST by preserving the typedef sugar; for example, we do not otherwise
/// represent the alignment attribute on typedefs, and so it is necessary to
/// preserve typedef structure into most parts of IR generation.
class NeverCanonical {}

/// A type node that only represents a canonical type in some dependent cases.
/// For example, `std::vector<int>` (a TemplateSpecializationType) is
/// considered to be a non-canonical representation for the RecordType
/// referencing the concrete ClassTemplateSpecializationDecl; but
/// `std::vector<T>` cannot be resolved to a concrete specialization
/// and so remains canonical.  Code which only works with non-dependent
/// canonical types can ignore these nodes.
class NeverCanonicalUnlessDependent {}

/// A type node which never has component type structure.  Some code may be
/// able to operate on leaf types faster than they can on non-leaf types.
///
/// For example, the function type `void (int)` is not a leaf type because it
/// is structurally composed of component types (`void` and `int`).
///
/// A struct type is a leaf type because its field types are not part of its
/// type-expression.
///
/// Nodes like `TypedefType` which are syntactically leaves but can desugar
/// to types that may not be leaves should not declare this.
class LeafType {}

def Type : TypeNode<?, 1>;
def BuiltinType : TypeNode<Type>, LeafType;
def ComplexType : TypeNode<Type>;
def PointerType : TypeNode<Type>;
def BlockPointerType : TypeNode<Type>;
def ReferenceType : TypeNode<Type, 1>;
def LValueReferenceType : TypeNode<ReferenceType>;
def RValueReferenceType : TypeNode<ReferenceType>;
def MemberPointerType : TypeNode<Type>;
def ArrayType : TypeNode<Type, 1>;
def ConstantArrayType : TypeNode<ArrayType>;
def IncompleteArrayType : TypeNode<ArrayType>;
def VariableArrayType : TypeNode<ArrayType>;
def DependentSizedArrayType : TypeNode<ArrayType>, AlwaysDependent;
def ArrayParameterType : TypeNode<ConstantArrayType>;
def DependentSizedExtVectorType : TypeNode<Type>, AlwaysDependent;
def DependentAddressSpaceType : TypeNode<Type>, AlwaysDependent;
def VectorType : TypeNode<Type>;
def DependentVectorType : TypeNode<Type>, AlwaysDependent;
def ExtVectorType : TypeNode<VectorType>;
def MatrixType : TypeNode<Type, 1>;
def ConstantMatrixType : TypeNode<MatrixType>;
def DependentSizedMatrixType : TypeNode<MatrixType>, AlwaysDependent;
def FunctionType : TypeNode<Type, 1>;
def FunctionProtoType : TypeNode<FunctionType>;
def FunctionNoProtoType : TypeNode<FunctionType>;
def UsingType : TypeNode<Type>, NeverCanonical;
def UnresolvedUsingType : TypeNode<Type>, AlwaysDependent;
def ParenType : TypeNode<Type>, NeverCanonical;
def TypedefType : TypeNode<Type>, NeverCanonical;
def MacroQualifiedType : TypeNode<Type>, NeverCanonical;
def AdjustedType : TypeNode<Type>, NeverCanonical;
def DecayedType : TypeNode<AdjustedType>, NeverCanonical;
def TypeOfExprType : TypeNode<Type>, NeverCanonicalUnlessDependent;
def TypeOfType : TypeNode<Type>, NeverCanonicalUnlessDependent;
def DecltypeType : TypeNode<Type>, NeverCanonicalUnlessDependent;
def UnaryTransformType : TypeNode<Type>, NeverCanonicalUnlessDependent;
def TagType : TypeNode<Type, 1>;
def RecordType : TypeNode<TagType>, LeafType;
def EnumType : TypeNode<TagType>, LeafType;
def ElaboratedType : TypeNode<Type>, NeverCanonical;
def AttributedType : TypeNode<Type>, NeverCanonical;
def BTFTagAttributedType : TypeNode<Type>, NeverCanonical;
def TemplateTypeParmType : TypeNode<Type>, AlwaysDependent, LeafType;
def SubstTemplateTypeParmType : TypeNode<Type>, NeverCanonical;
def SubstTemplateTypeParmPackType : TypeNode<Type>, AlwaysDependent;
def TemplateSpecializationType : TypeNode<Type>, NeverCanonicalUnlessDependent;
def DeducedType : TypeNode<Type, 1>;
def AutoType : TypeNode<DeducedType>;
def DeducedTemplateSpecializationType : TypeNode<DeducedType>;
def InjectedClassNameType : TypeNode<Type>, AlwaysDependent, LeafType;
def DependentNameType : TypeNode<Type>, AlwaysDependent;
def DependentTemplateSpecializationType : TypeNode<Type>, AlwaysDependent;
def PackExpansionType : TypeNode<Type>, AlwaysDependent;
def PackIndexingType  : TypeNode<Type>, NeverCanonicalUnlessDependent;
def ObjCTypeParamType : TypeNode<Type>, NeverCanonical;
def ObjCObjectType : TypeNode<Type>;
def ObjCInterfaceType : TypeNode<ObjCObjectType>, LeafType;
def ObjCObjectPointerType : TypeNode<Type>;
def BoundsAttributedType : TypeNode<Type, 1>;
def CountAttributedType : TypeNode<BoundsAttributedType>, NeverCanonical;
def PipeType : TypeNode<Type>;
def AtomicType : TypeNode<Type>;
def BitIntType : TypeNode<Type>;
def DependentBitIntType : TypeNode<Type>, AlwaysDependent;
