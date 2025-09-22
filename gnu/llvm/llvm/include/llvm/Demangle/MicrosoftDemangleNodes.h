//===- MicrosoftDemangleNodes.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the AST nodes used in the MSVC demangler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEMANGLE_MICROSOFTDEMANGLENODES_H
#define LLVM_DEMANGLE_MICROSOFTDEMANGLENODES_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace llvm {
namespace itanium_demangle {
class OutputBuffer;
}
}

using llvm::itanium_demangle::OutputBuffer;

namespace llvm {
namespace ms_demangle {

// Storage classes
enum Qualifiers : uint8_t {
  Q_None = 0,
  Q_Const = 1 << 0,
  Q_Volatile = 1 << 1,
  Q_Far = 1 << 2,
  Q_Huge = 1 << 3,
  Q_Unaligned = 1 << 4,
  Q_Restrict = 1 << 5,
  Q_Pointer64 = 1 << 6
};

enum class StorageClass : uint8_t {
  None,
  PrivateStatic,
  ProtectedStatic,
  PublicStatic,
  Global,
  FunctionLocalStatic,
};

enum class PointerAffinity { None, Pointer, Reference, RValueReference };
enum class FunctionRefQualifier { None, Reference, RValueReference };

// Calling conventions
enum class CallingConv : uint8_t {
  None,
  Cdecl,
  Pascal,
  Thiscall,
  Stdcall,
  Fastcall,
  Clrcall,
  Eabi,
  Vectorcall,
  Regcall,
  Swift,      // Clang-only
  SwiftAsync, // Clang-only
};

enum class ReferenceKind : uint8_t { None, LValueRef, RValueRef };

enum OutputFlags {
  OF_Default = 0,
  OF_NoCallingConvention = 1,
  OF_NoTagSpecifier = 2,
  OF_NoAccessSpecifier = 4,
  OF_NoMemberType = 8,
  OF_NoReturnType = 16,
  OF_NoVariableType = 32,
};

// Types
enum class PrimitiveKind {
  Void,
  Bool,
  Char,
  Schar,
  Uchar,
  Char8,
  Char16,
  Char32,
  Short,
  Ushort,
  Int,
  Uint,
  Long,
  Ulong,
  Int64,
  Uint64,
  Wchar,
  Float,
  Double,
  Ldouble,
  Nullptr,
};

enum class CharKind {
  Char,
  Char16,
  Char32,
  Wchar,
};

enum class IntrinsicFunctionKind : uint8_t {
  None,
  New,                        // ?2 # operator new
  Delete,                     // ?3 # operator delete
  Assign,                     // ?4 # operator=
  RightShift,                 // ?5 # operator>>
  LeftShift,                  // ?6 # operator<<
  LogicalNot,                 // ?7 # operator!
  Equals,                     // ?8 # operator==
  NotEquals,                  // ?9 # operator!=
  ArraySubscript,             // ?A # operator[]
  Pointer,                    // ?C # operator->
  Dereference,                // ?D # operator*
  Increment,                  // ?E # operator++
  Decrement,                  // ?F # operator--
  Minus,                      // ?G # operator-
  Plus,                       // ?H # operator+
  BitwiseAnd,                 // ?I # operator&
  MemberPointer,              // ?J # operator->*
  Divide,                     // ?K # operator/
  Modulus,                    // ?L # operator%
  LessThan,                   // ?M operator<
  LessThanEqual,              // ?N operator<=
  GreaterThan,                // ?O operator>
  GreaterThanEqual,           // ?P operator>=
  Comma,                      // ?Q operator,
  Parens,                     // ?R operator()
  BitwiseNot,                 // ?S operator~
  BitwiseXor,                 // ?T operator^
  BitwiseOr,                  // ?U operator|
  LogicalAnd,                 // ?V operator&&
  LogicalOr,                  // ?W operator||
  TimesEqual,                 // ?X operator*=
  PlusEqual,                  // ?Y operator+=
  MinusEqual,                 // ?Z operator-=
  DivEqual,                   // ?_0 operator/=
  ModEqual,                   // ?_1 operator%=
  RshEqual,                   // ?_2 operator>>=
  LshEqual,                   // ?_3 operator<<=
  BitwiseAndEqual,            // ?_4 operator&=
  BitwiseOrEqual,             // ?_5 operator|=
  BitwiseXorEqual,            // ?_6 operator^=
  VbaseDtor,                  // ?_D # vbase destructor
  VecDelDtor,                 // ?_E # vector deleting destructor
  DefaultCtorClosure,         // ?_F # default constructor closure
  ScalarDelDtor,              // ?_G # scalar deleting destructor
  VecCtorIter,                // ?_H # vector constructor iterator
  VecDtorIter,                // ?_I # vector destructor iterator
  VecVbaseCtorIter,           // ?_J # vector vbase constructor iterator
  VdispMap,                   // ?_K # virtual displacement map
  EHVecCtorIter,              // ?_L # eh vector constructor iterator
  EHVecDtorIter,              // ?_M # eh vector destructor iterator
  EHVecVbaseCtorIter,         // ?_N # eh vector vbase constructor iterator
  CopyCtorClosure,            // ?_O # copy constructor closure
  LocalVftableCtorClosure,    // ?_T # local vftable constructor closure
  ArrayNew,                   // ?_U operator new[]
  ArrayDelete,                // ?_V operator delete[]
  ManVectorCtorIter,          // ?__A managed vector ctor iterator
  ManVectorDtorIter,          // ?__B managed vector dtor iterator
  EHVectorCopyCtorIter,       // ?__C EH vector copy ctor iterator
  EHVectorVbaseCopyCtorIter,  // ?__D EH vector vbase copy ctor iterator
  VectorCopyCtorIter,         // ?__G vector copy constructor iterator
  VectorVbaseCopyCtorIter,    // ?__H vector vbase copy constructor iterator
  ManVectorVbaseCopyCtorIter, // ?__I managed vector vbase copy constructor
  CoAwait,                    // ?__L operator co_await
  Spaceship,                  // ?__M operator<=>
  MaxIntrinsic
};

enum class SpecialIntrinsicKind {
  None,
  Vftable,
  Vbtable,
  Typeof,
  VcallThunk,
  LocalStaticGuard,
  StringLiteralSymbol,
  UdtReturning,
  Unknown,
  DynamicInitializer,
  DynamicAtexitDestructor,
  RttiTypeDescriptor,
  RttiBaseClassDescriptor,
  RttiBaseClassArray,
  RttiClassHierarchyDescriptor,
  RttiCompleteObjLocator,
  LocalVftable,
  LocalStaticThreadGuard,
};

// Function classes
enum FuncClass : uint16_t {
  FC_None = 0,
  FC_Public = 1 << 0,
  FC_Protected = 1 << 1,
  FC_Private = 1 << 2,
  FC_Global = 1 << 3,
  FC_Static = 1 << 4,
  FC_Virtual = 1 << 5,
  FC_Far = 1 << 6,
  FC_ExternC = 1 << 7,
  FC_NoParameterList = 1 << 8,
  FC_VirtualThisAdjust = 1 << 9,
  FC_VirtualThisAdjustEx = 1 << 10,
  FC_StaticThisAdjust = 1 << 11,
};

enum class TagKind { Class, Struct, Union, Enum };

enum class NodeKind {
  Unknown,
  Md5Symbol,
  PrimitiveType,
  FunctionSignature,
  Identifier,
  NamedIdentifier,
  VcallThunkIdentifier,
  LocalStaticGuardIdentifier,
  IntrinsicFunctionIdentifier,
  ConversionOperatorIdentifier,
  DynamicStructorIdentifier,
  StructorIdentifier,
  LiteralOperatorIdentifier,
  ThunkSignature,
  PointerType,
  TagType,
  ArrayType,
  Custom,
  IntrinsicType,
  NodeArray,
  QualifiedName,
  TemplateParameterReference,
  EncodedStringLiteral,
  IntegerLiteral,
  RttiBaseClassDescriptor,
  LocalStaticGuardVariable,
  FunctionSymbol,
  VariableSymbol,
  SpecialTableSymbol
};

struct Node {
  explicit Node(NodeKind K) : Kind(K) {}
  virtual ~Node() = default;

  NodeKind kind() const { return Kind; }

  virtual void output(OutputBuffer &OB, OutputFlags Flags) const = 0;

  std::string toString(OutputFlags Flags = OF_Default) const;

private:
  NodeKind Kind;
};

struct TypeNode;
struct PrimitiveTypeNode;
struct FunctionSignatureNode;
struct IdentifierNode;
struct NamedIdentifierNode;
struct VcallThunkIdentifierNode;
struct IntrinsicFunctionIdentifierNode;
struct LiteralOperatorIdentifierNode;
struct ConversionOperatorIdentifierNode;
struct StructorIdentifierNode;
struct ThunkSignatureNode;
struct PointerTypeNode;
struct ArrayTypeNode;
struct TagTypeNode;
struct NodeArrayNode;
struct QualifiedNameNode;
struct TemplateParameterReferenceNode;
struct EncodedStringLiteralNode;
struct IntegerLiteralNode;
struct RttiBaseClassDescriptorNode;
struct LocalStaticGuardVariableNode;
struct SymbolNode;
struct FunctionSymbolNode;
struct VariableSymbolNode;
struct SpecialTableSymbolNode;

struct TypeNode : public Node {
  explicit TypeNode(NodeKind K) : Node(K) {}

  virtual void outputPre(OutputBuffer &OB, OutputFlags Flags) const = 0;
  virtual void outputPost(OutputBuffer &OB, OutputFlags Flags) const = 0;

  void output(OutputBuffer &OB, OutputFlags Flags) const override {
    outputPre(OB, Flags);
    outputPost(OB, Flags);
  }

  Qualifiers Quals = Q_None;
};

struct PrimitiveTypeNode : public TypeNode {
  explicit PrimitiveTypeNode(PrimitiveKind K)
      : TypeNode(NodeKind::PrimitiveType), PrimKind(K) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override {}

  PrimitiveKind PrimKind;
};

struct FunctionSignatureNode : public TypeNode {
  explicit FunctionSignatureNode(NodeKind K) : TypeNode(K) {}
  FunctionSignatureNode() : TypeNode(NodeKind::FunctionSignature) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  // Valid if this FunctionTypeNode is the Pointee of a PointerType or
  // MemberPointerType.
  PointerAffinity Affinity = PointerAffinity::None;

  // The function's calling convention.
  CallingConv CallConvention = CallingConv::None;

  // Function flags (gloabl, public, etc)
  FuncClass FunctionClass = FC_Global;

  FunctionRefQualifier RefQualifier = FunctionRefQualifier::None;

  // The return type of the function.
  TypeNode *ReturnType = nullptr;

  // True if this is a C-style ... varargs function.
  bool IsVariadic = false;

  // Function parameters
  NodeArrayNode *Params = nullptr;

  // True if the function type is noexcept.
  bool IsNoexcept = false;
};

struct IdentifierNode : public Node {
  explicit IdentifierNode(NodeKind K) : Node(K) {}

  NodeArrayNode *TemplateParams = nullptr;

protected:
  void outputTemplateParameters(OutputBuffer &OB, OutputFlags Flags) const;
};

struct VcallThunkIdentifierNode : public IdentifierNode {
  VcallThunkIdentifierNode() : IdentifierNode(NodeKind::VcallThunkIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  uint64_t OffsetInVTable = 0;
};

struct DynamicStructorIdentifierNode : public IdentifierNode {
  DynamicStructorIdentifierNode()
      : IdentifierNode(NodeKind::DynamicStructorIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  VariableSymbolNode *Variable = nullptr;
  QualifiedNameNode *Name = nullptr;
  bool IsDestructor = false;
};

struct NamedIdentifierNode : public IdentifierNode {
  NamedIdentifierNode() : IdentifierNode(NodeKind::NamedIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  std::string_view Name;
};

struct IntrinsicFunctionIdentifierNode : public IdentifierNode {
  explicit IntrinsicFunctionIdentifierNode(IntrinsicFunctionKind Operator)
      : IdentifierNode(NodeKind::IntrinsicFunctionIdentifier),
        Operator(Operator) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  IntrinsicFunctionKind Operator;
};

struct LiteralOperatorIdentifierNode : public IdentifierNode {
  LiteralOperatorIdentifierNode()
      : IdentifierNode(NodeKind::LiteralOperatorIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  std::string_view Name;
};

struct LocalStaticGuardIdentifierNode : public IdentifierNode {
  LocalStaticGuardIdentifierNode()
      : IdentifierNode(NodeKind::LocalStaticGuardIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  bool IsThread = false;
  uint32_t ScopeIndex = 0;
};

struct ConversionOperatorIdentifierNode : public IdentifierNode {
  ConversionOperatorIdentifierNode()
      : IdentifierNode(NodeKind::ConversionOperatorIdentifier) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  // The type that this operator converts too.
  TypeNode *TargetType = nullptr;
};

struct StructorIdentifierNode : public IdentifierNode {
  StructorIdentifierNode() : IdentifierNode(NodeKind::StructorIdentifier) {}
  explicit StructorIdentifierNode(bool IsDestructor)
      : IdentifierNode(NodeKind::StructorIdentifier),
        IsDestructor(IsDestructor) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  // The name of the class that this is a structor of.
  IdentifierNode *Class = nullptr;
  bool IsDestructor = false;
};

struct ThunkSignatureNode : public FunctionSignatureNode {
  ThunkSignatureNode() : FunctionSignatureNode(NodeKind::ThunkSignature) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  struct ThisAdjustor {
    uint32_t StaticOffset = 0;
    int32_t VBPtrOffset = 0;
    int32_t VBOffsetOffset = 0;
    int32_t VtordispOffset = 0;
  };

  ThisAdjustor ThisAdjust;
};

struct PointerTypeNode : public TypeNode {
  PointerTypeNode() : TypeNode(NodeKind::PointerType) {}
  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  // Is this a pointer, reference, or rvalue-reference?
  PointerAffinity Affinity = PointerAffinity::None;

  // If this is a member pointer, this is the class that the member is in.
  QualifiedNameNode *ClassParent = nullptr;

  // Represents a type X in "a pointer to X", "a reference to X", or
  // "rvalue-reference to X"
  TypeNode *Pointee = nullptr;
};

struct TagTypeNode : public TypeNode {
  explicit TagTypeNode(TagKind Tag) : TypeNode(NodeKind::TagType), Tag(Tag) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  QualifiedNameNode *QualifiedName = nullptr;
  TagKind Tag;
};

struct ArrayTypeNode : public TypeNode {
  ArrayTypeNode() : TypeNode(NodeKind::ArrayType) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  void outputDimensionsImpl(OutputBuffer &OB, OutputFlags Flags) const;
  void outputOneDimension(OutputBuffer &OB, OutputFlags Flags, Node *N) const;

  // A list of array dimensions.  e.g. [3,4,5] in `int Foo[3][4][5]`
  NodeArrayNode *Dimensions = nullptr;

  // The type of array element.
  TypeNode *ElementType = nullptr;
};

struct IntrinsicNode : public TypeNode {
  IntrinsicNode() : TypeNode(NodeKind::IntrinsicType) {}
  void output(OutputBuffer &OB, OutputFlags Flags) const override {}
};

struct CustomTypeNode : public TypeNode {
  CustomTypeNode() : TypeNode(NodeKind::Custom) {}

  void outputPre(OutputBuffer &OB, OutputFlags Flags) const override;
  void outputPost(OutputBuffer &OB, OutputFlags Flags) const override;

  IdentifierNode *Identifier = nullptr;
};

struct NodeArrayNode : public Node {
  NodeArrayNode() : Node(NodeKind::NodeArray) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  void output(OutputBuffer &OB, OutputFlags Flags,
              std::string_view Separator) const;

  Node **Nodes = nullptr;
  size_t Count = 0;
};

struct QualifiedNameNode : public Node {
  QualifiedNameNode() : Node(NodeKind::QualifiedName) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  NodeArrayNode *Components = nullptr;

  IdentifierNode *getUnqualifiedIdentifier() {
    Node *LastComponent = Components->Nodes[Components->Count - 1];
    return static_cast<IdentifierNode *>(LastComponent);
  }
};

struct TemplateParameterReferenceNode : public Node {
  TemplateParameterReferenceNode()
      : Node(NodeKind::TemplateParameterReference) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  SymbolNode *Symbol = nullptr;

  int ThunkOffsetCount = 0;
  std::array<int64_t, 3> ThunkOffsets;
  PointerAffinity Affinity = PointerAffinity::None;
  bool IsMemberPointer = false;
};

struct IntegerLiteralNode : public Node {
  IntegerLiteralNode() : Node(NodeKind::IntegerLiteral) {}
  IntegerLiteralNode(uint64_t Value, bool IsNegative)
      : Node(NodeKind::IntegerLiteral), Value(Value), IsNegative(IsNegative) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  uint64_t Value = 0;
  bool IsNegative = false;
};

struct RttiBaseClassDescriptorNode : public IdentifierNode {
  RttiBaseClassDescriptorNode()
      : IdentifierNode(NodeKind::RttiBaseClassDescriptor) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  uint32_t NVOffset = 0;
  int32_t VBPtrOffset = 0;
  uint32_t VBTableOffset = 0;
  uint32_t Flags = 0;
};

struct SymbolNode : public Node {
  explicit SymbolNode(NodeKind K) : Node(K) {}
  void output(OutputBuffer &OB, OutputFlags Flags) const override;
  QualifiedNameNode *Name = nullptr;
};

struct SpecialTableSymbolNode : public SymbolNode {
  explicit SpecialTableSymbolNode()
      : SymbolNode(NodeKind::SpecialTableSymbol) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;
  QualifiedNameNode *TargetName = nullptr;
  Qualifiers Quals = Qualifiers::Q_None;
};

struct LocalStaticGuardVariableNode : public SymbolNode {
  LocalStaticGuardVariableNode()
      : SymbolNode(NodeKind::LocalStaticGuardVariable) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  bool IsVisible = false;
};

struct EncodedStringLiteralNode : public SymbolNode {
  EncodedStringLiteralNode() : SymbolNode(NodeKind::EncodedStringLiteral) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  std::string_view DecodedString;
  bool IsTruncated = false;
  CharKind Char = CharKind::Char;
};

struct VariableSymbolNode : public SymbolNode {
  VariableSymbolNode() : SymbolNode(NodeKind::VariableSymbol) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  StorageClass SC = StorageClass::None;
  TypeNode *Type = nullptr;
};

struct FunctionSymbolNode : public SymbolNode {
  FunctionSymbolNode() : SymbolNode(NodeKind::FunctionSymbol) {}

  void output(OutputBuffer &OB, OutputFlags Flags) const override;

  FunctionSignatureNode *Signature = nullptr;
};

} // namespace ms_demangle
} // namespace llvm

#endif
