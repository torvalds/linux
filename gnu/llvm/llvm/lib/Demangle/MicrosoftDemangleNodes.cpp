//===- MicrosoftDemangle.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a demangler for MSVC-style mangled symbols.
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/MicrosoftDemangleNodes.h"
#include "llvm/Demangle/Utility.h"
#include <cctype>
#include <string>

using namespace llvm;
using namespace ms_demangle;

#define OUTPUT_ENUM_CLASS_VALUE(Enum, Value, Desc)                             \
  case Enum::Value:                                                            \
    OB << Desc;                                                                \
    break;

// Writes a space if the last token does not end with a punctuation.
static void outputSpaceIfNecessary(OutputBuffer &OB) {
  if (OB.empty())
    return;

  char C = OB.back();
  if (std::isalnum(C) || C == '>')
    OB << " ";
}

static void outputSingleQualifier(OutputBuffer &OB, Qualifiers Q) {
  switch (Q) {
  case Q_Const:
    OB << "const";
    break;
  case Q_Volatile:
    OB << "volatile";
    break;
  case Q_Restrict:
    OB << "__restrict";
    break;
  default:
    break;
  }
}

static bool outputQualifierIfPresent(OutputBuffer &OB, Qualifiers Q,
                                     Qualifiers Mask, bool NeedSpace) {
  if (!(Q & Mask))
    return NeedSpace;

  if (NeedSpace)
    OB << " ";

  outputSingleQualifier(OB, Mask);
  return true;
}

static void outputQualifiers(OutputBuffer &OB, Qualifiers Q, bool SpaceBefore,
                             bool SpaceAfter) {
  if (Q == Q_None)
    return;

  size_t Pos1 = OB.getCurrentPosition();
  SpaceBefore = outputQualifierIfPresent(OB, Q, Q_Const, SpaceBefore);
  SpaceBefore = outputQualifierIfPresent(OB, Q, Q_Volatile, SpaceBefore);
  SpaceBefore = outputQualifierIfPresent(OB, Q, Q_Restrict, SpaceBefore);
  size_t Pos2 = OB.getCurrentPosition();
  if (SpaceAfter && Pos2 > Pos1)
    OB << " ";
}

static void outputCallingConvention(OutputBuffer &OB, CallingConv CC) {
  outputSpaceIfNecessary(OB);

  switch (CC) {
  case CallingConv::Cdecl:
    OB << "__cdecl";
    break;
  case CallingConv::Fastcall:
    OB << "__fastcall";
    break;
  case CallingConv::Pascal:
    OB << "__pascal";
    break;
  case CallingConv::Regcall:
    OB << "__regcall";
    break;
  case CallingConv::Stdcall:
    OB << "__stdcall";
    break;
  case CallingConv::Thiscall:
    OB << "__thiscall";
    break;
  case CallingConv::Eabi:
    OB << "__eabi";
    break;
  case CallingConv::Vectorcall:
    OB << "__vectorcall";
    break;
  case CallingConv::Clrcall:
    OB << "__clrcall";
    break;
  case CallingConv::Swift:
    OB << "__attribute__((__swiftcall__)) ";
    break;
  case CallingConv::SwiftAsync:
    OB << "__attribute__((__swiftasynccall__)) ";
    break;
  default:
    break;
  }
}

std::string Node::toString(OutputFlags Flags) const {
  OutputBuffer OB;
  this->output(OB, Flags);
  std::string_view SV = OB;
  std::string Owned(SV.begin(), SV.end());
  std::free(OB.getBuffer());
  return Owned;
}

void PrimitiveTypeNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  switch (PrimKind) {
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Void, "void");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Bool, "bool");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Char, "char");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Schar, "signed char");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Uchar, "unsigned char");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Char8, "char8_t");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Char16, "char16_t");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Char32, "char32_t");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Short, "short");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Ushort, "unsigned short");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Int, "int");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Uint, "unsigned int");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Long, "long");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Ulong, "unsigned long");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Int64, "__int64");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Uint64, "unsigned __int64");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Wchar, "wchar_t");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Float, "float");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Double, "double");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Ldouble, "long double");
    OUTPUT_ENUM_CLASS_VALUE(PrimitiveKind, Nullptr, "std::nullptr_t");
  }
  outputQualifiers(OB, Quals, true, false);
}

void NodeArrayNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  output(OB, Flags, ", ");
}

void NodeArrayNode::output(OutputBuffer &OB, OutputFlags Flags,
                           std::string_view Separator) const {
  if (Count == 0)
    return;
  if (Nodes[0])
    Nodes[0]->output(OB, Flags);
  for (size_t I = 1; I < Count; ++I) {
    OB << Separator;
    Nodes[I]->output(OB, Flags);
  }
}

void EncodedStringLiteralNode::output(OutputBuffer &OB,
                                      OutputFlags Flags) const {
  switch (Char) {
  case CharKind::Wchar:
    OB << "L\"";
    break;
  case CharKind::Char:
    OB << "\"";
    break;
  case CharKind::Char16:
    OB << "u\"";
    break;
  case CharKind::Char32:
    OB << "U\"";
    break;
  }
  OB << DecodedString << "\"";
  if (IsTruncated)
    OB << "...";
}

void IntegerLiteralNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  if (IsNegative)
    OB << '-';
  OB << Value;
}

void TemplateParameterReferenceNode::output(OutputBuffer &OB,
                                            OutputFlags Flags) const {
  if (ThunkOffsetCount > 0)
    OB << "{";
  else if (Affinity == PointerAffinity::Pointer)
    OB << "&";

  if (Symbol) {
    Symbol->output(OB, Flags);
    if (ThunkOffsetCount > 0)
      OB << ", ";
  }

  if (ThunkOffsetCount > 0)
    OB << ThunkOffsets[0];
  for (int I = 1; I < ThunkOffsetCount; ++I) {
    OB << ", " << ThunkOffsets[I];
  }
  if (ThunkOffsetCount > 0)
    OB << "}";
}

void IdentifierNode::outputTemplateParameters(OutputBuffer &OB,
                                              OutputFlags Flags) const {
  if (!TemplateParams)
    return;
  OB << "<";
  TemplateParams->output(OB, Flags);
  OB << ">";
}

void DynamicStructorIdentifierNode::output(OutputBuffer &OB,
                                           OutputFlags Flags) const {
  if (IsDestructor)
    OB << "`dynamic atexit destructor for ";
  else
    OB << "`dynamic initializer for ";

  if (Variable) {
    OB << "`";
    Variable->output(OB, Flags);
    OB << "''";
  } else {
    OB << "'";
    Name->output(OB, Flags);
    OB << "''";
  }
}

void NamedIdentifierNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  OB << Name;
  outputTemplateParameters(OB, Flags);
}

void IntrinsicFunctionIdentifierNode::output(OutputBuffer &OB,
                                             OutputFlags Flags) const {
  switch (Operator) {
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, New, "operator new");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Delete, "operator delete");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Assign, "operator=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, RightShift, "operator>>");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LeftShift, "operator<<");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LogicalNot, "operator!");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Equals, "operator==");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, NotEquals, "operator!=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ArraySubscript,
                            "operator[]");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Pointer, "operator->");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Increment, "operator++");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Decrement, "operator--");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Minus, "operator-");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Plus, "operator+");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Dereference, "operator*");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseAnd, "operator&");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, MemberPointer,
                            "operator->*");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Divide, "operator/");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Modulus, "operator%");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LessThan, "operator<");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LessThanEqual, "operator<=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, GreaterThan, "operator>");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, GreaterThanEqual,
                            "operator>=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Comma, "operator,");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Parens, "operator()");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseNot, "operator~");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseXor, "operator^");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseOr, "operator|");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LogicalAnd, "operator&&");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LogicalOr, "operator||");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, TimesEqual, "operator*=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, PlusEqual, "operator+=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, MinusEqual, "operator-=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, DivEqual, "operator/=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ModEqual, "operator%=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, RshEqual, "operator>>=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LshEqual, "operator<<=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseAndEqual,
                            "operator&=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseOrEqual,
                            "operator|=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, BitwiseXorEqual,
                            "operator^=");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VbaseDtor, "`vbase dtor'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VecDelDtor,
                            "`vector deleting dtor'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, DefaultCtorClosure,
                            "`default ctor closure'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ScalarDelDtor,
                            "`scalar deleting dtor'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VecCtorIter,
                            "`vector ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VecDtorIter,
                            "`vector dtor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VecVbaseCtorIter,
                            "`vector vbase ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VdispMap,
                            "`virtual displacement map'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, EHVecCtorIter,
                            "`eh vector ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, EHVecDtorIter,
                            "`eh vector dtor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, EHVecVbaseCtorIter,
                            "`eh vector vbase ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, CopyCtorClosure,
                            "`copy ctor closure'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, LocalVftableCtorClosure,
                            "`local vftable ctor closure'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ArrayNew, "operator new[]");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ArrayDelete,
                            "operator delete[]");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ManVectorCtorIter,
                            "`managed vector ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ManVectorDtorIter,
                            "`managed vector dtor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, EHVectorCopyCtorIter,
                            "`EH vector copy ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, EHVectorVbaseCopyCtorIter,
                            "`EH vector vbase copy ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VectorCopyCtorIter,
                            "`vector copy ctor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, VectorVbaseCopyCtorIter,
                            "`vector vbase copy constructor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, ManVectorVbaseCopyCtorIter,
                            "`managed vector vbase copy constructor iterator'");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, CoAwait,
                            "operator co_await");
    OUTPUT_ENUM_CLASS_VALUE(IntrinsicFunctionKind, Spaceship, "operator<=>");
  case IntrinsicFunctionKind::MaxIntrinsic:
  case IntrinsicFunctionKind::None:
    break;
  }
  outputTemplateParameters(OB, Flags);
}

void LocalStaticGuardIdentifierNode::output(OutputBuffer &OB,
                                            OutputFlags Flags) const {
  if (IsThread)
    OB << "`local static thread guard'";
  else
    OB << "`local static guard'";
  if (ScopeIndex > 0)
    OB << "{" << ScopeIndex << "}";
}

void ConversionOperatorIdentifierNode::output(OutputBuffer &OB,
                                              OutputFlags Flags) const {
  OB << "operator";
  outputTemplateParameters(OB, Flags);
  OB << " ";
  TargetType->output(OB, Flags);
}

void StructorIdentifierNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  if (IsDestructor)
    OB << "~";
  Class->output(OB, Flags);
  outputTemplateParameters(OB, Flags);
}

void LiteralOperatorIdentifierNode::output(OutputBuffer &OB,
                                           OutputFlags Flags) const {
  OB << "operator \"\"" << Name;
  outputTemplateParameters(OB, Flags);
}

void FunctionSignatureNode::outputPre(OutputBuffer &OB,
                                      OutputFlags Flags) const {
  if (!(Flags & OF_NoAccessSpecifier)) {
    if (FunctionClass & FC_Public)
      OB << "public: ";
    if (FunctionClass & FC_Protected)
      OB << "protected: ";
    if (FunctionClass & FC_Private)
      OB << "private: ";
  }

  if (!(Flags & OF_NoMemberType)) {
    if (!(FunctionClass & FC_Global)) {
      if (FunctionClass & FC_Static)
        OB << "static ";
    }
    if (FunctionClass & FC_Virtual)
      OB << "virtual ";

    if (FunctionClass & FC_ExternC)
      OB << "extern \"C\" ";
  }

  if (!(Flags & OF_NoReturnType) && ReturnType) {
    ReturnType->outputPre(OB, Flags);
    OB << " ";
  }

  if (!(Flags & OF_NoCallingConvention))
    outputCallingConvention(OB, CallConvention);
}

void FunctionSignatureNode::outputPost(OutputBuffer &OB,
                                       OutputFlags Flags) const {
  if (!(FunctionClass & FC_NoParameterList)) {
    OB << "(";
    if (Params)
      Params->output(OB, Flags);
    else
      OB << "void";

    if (IsVariadic) {
      if (OB.back() != '(')
        OB << ", ";
      OB << "...";
    }
    OB << ")";
  }

  if (Quals & Q_Const)
    OB << " const";
  if (Quals & Q_Volatile)
    OB << " volatile";
  if (Quals & Q_Restrict)
    OB << " __restrict";
  if (Quals & Q_Unaligned)
    OB << " __unaligned";

  if (IsNoexcept)
    OB << " noexcept";

  if (RefQualifier == FunctionRefQualifier::Reference)
    OB << " &";
  else if (RefQualifier == FunctionRefQualifier::RValueReference)
    OB << " &&";

  if (!(Flags & OF_NoReturnType) && ReturnType)
    ReturnType->outputPost(OB, Flags);
}

void ThunkSignatureNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  OB << "[thunk]: ";

  FunctionSignatureNode::outputPre(OB, Flags);
}

void ThunkSignatureNode::outputPost(OutputBuffer &OB, OutputFlags Flags) const {
  if (FunctionClass & FC_StaticThisAdjust) {
    OB << "`adjustor{" << ThisAdjust.StaticOffset << "}'";
  } else if (FunctionClass & FC_VirtualThisAdjust) {
    if (FunctionClass & FC_VirtualThisAdjustEx) {
      OB << "`vtordispex{" << ThisAdjust.VBPtrOffset << ", "
         << ThisAdjust.VBOffsetOffset << ", " << ThisAdjust.VtordispOffset
         << ", " << ThisAdjust.StaticOffset << "}'";
    } else {
      OB << "`vtordisp{" << ThisAdjust.VtordispOffset << ", "
         << ThisAdjust.StaticOffset << "}'";
    }
  }

  FunctionSignatureNode::outputPost(OB, Flags);
}

void PointerTypeNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  if (Pointee->kind() == NodeKind::FunctionSignature) {
    // If this is a pointer to a function, don't output the calling convention.
    // It needs to go inside the parentheses.
    const FunctionSignatureNode *Sig =
        static_cast<const FunctionSignatureNode *>(Pointee);
    Sig->outputPre(OB, OF_NoCallingConvention);
  } else
    Pointee->outputPre(OB, Flags);

  outputSpaceIfNecessary(OB);

  if (Quals & Q_Unaligned)
    OB << "__unaligned ";

  if (Pointee->kind() == NodeKind::ArrayType) {
    OB << "(";
  } else if (Pointee->kind() == NodeKind::FunctionSignature) {
    OB << "(";
    const FunctionSignatureNode *Sig =
        static_cast<const FunctionSignatureNode *>(Pointee);
    outputCallingConvention(OB, Sig->CallConvention);
    OB << " ";
  }

  if (ClassParent) {
    ClassParent->output(OB, Flags);
    OB << "::";
  }

  switch (Affinity) {
  case PointerAffinity::Pointer:
    OB << "*";
    break;
  case PointerAffinity::Reference:
    OB << "&";
    break;
  case PointerAffinity::RValueReference:
    OB << "&&";
    break;
  default:
    assert(false);
  }
  outputQualifiers(OB, Quals, false, false);
}

void PointerTypeNode::outputPost(OutputBuffer &OB, OutputFlags Flags) const {
  if (Pointee->kind() == NodeKind::ArrayType ||
      Pointee->kind() == NodeKind::FunctionSignature)
    OB << ")";

  Pointee->outputPost(OB, Flags);
}

void TagTypeNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  if (!(Flags & OF_NoTagSpecifier)) {
    switch (Tag) {
      OUTPUT_ENUM_CLASS_VALUE(TagKind, Class, "class");
      OUTPUT_ENUM_CLASS_VALUE(TagKind, Struct, "struct");
      OUTPUT_ENUM_CLASS_VALUE(TagKind, Union, "union");
      OUTPUT_ENUM_CLASS_VALUE(TagKind, Enum, "enum");
    }
    OB << " ";
  }
  QualifiedName->output(OB, Flags);
  outputQualifiers(OB, Quals, true, false);
}

void TagTypeNode::outputPost(OutputBuffer &OB, OutputFlags Flags) const {}

void ArrayTypeNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  ElementType->outputPre(OB, Flags);
  outputQualifiers(OB, Quals, true, false);
}

void ArrayTypeNode::outputOneDimension(OutputBuffer &OB, OutputFlags Flags,
                                       Node *N) const {
  assert(N->kind() == NodeKind::IntegerLiteral);
  IntegerLiteralNode *ILN = static_cast<IntegerLiteralNode *>(N);
  if (ILN->Value != 0)
    ILN->output(OB, Flags);
}

void ArrayTypeNode::outputDimensionsImpl(OutputBuffer &OB,
                                         OutputFlags Flags) const {
  if (Dimensions->Count == 0)
    return;

  outputOneDimension(OB, Flags, Dimensions->Nodes[0]);
  for (size_t I = 1; I < Dimensions->Count; ++I) {
    OB << "][";
    outputOneDimension(OB, Flags, Dimensions->Nodes[I]);
  }
}

void ArrayTypeNode::outputPost(OutputBuffer &OB, OutputFlags Flags) const {
  OB << "[";
  outputDimensionsImpl(OB, Flags);
  OB << "]";

  ElementType->outputPost(OB, Flags);
}

void SymbolNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  Name->output(OB, Flags);
}

void FunctionSymbolNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  Signature->outputPre(OB, Flags);
  outputSpaceIfNecessary(OB);
  Name->output(OB, Flags);
  Signature->outputPost(OB, Flags);
}

void VariableSymbolNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  const char *AccessSpec = nullptr;
  bool IsStatic = true;
  switch (SC) {
  case StorageClass::PrivateStatic:
    AccessSpec = "private";
    break;
  case StorageClass::PublicStatic:
    AccessSpec = "public";
    break;
  case StorageClass::ProtectedStatic:
    AccessSpec = "protected";
    break;
  default:
    IsStatic = false;
    break;
  }
  if (!(Flags & OF_NoAccessSpecifier) && AccessSpec)
    OB << AccessSpec << ": ";
  if (!(Flags & OF_NoMemberType) && IsStatic)
    OB << "static ";

  if (!(Flags & OF_NoVariableType) && Type) {
    Type->outputPre(OB, Flags);
    outputSpaceIfNecessary(OB);
  }
  Name->output(OB, Flags);
  if (!(Flags & OF_NoVariableType) && Type)
    Type->outputPost(OB, Flags);
}

void CustomTypeNode::outputPre(OutputBuffer &OB, OutputFlags Flags) const {
  Identifier->output(OB, Flags);
}
void CustomTypeNode::outputPost(OutputBuffer &OB, OutputFlags Flags) const {}

void QualifiedNameNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  Components->output(OB, Flags, "::");
}

void RttiBaseClassDescriptorNode::output(OutputBuffer &OB,
                                         OutputFlags Flags) const {
  OB << "`RTTI Base Class Descriptor at (";
  OB << NVOffset << ", " << VBPtrOffset << ", " << VBTableOffset << ", "
     << this->Flags;
  OB << ")'";
}

void LocalStaticGuardVariableNode::output(OutputBuffer &OB,
                                          OutputFlags Flags) const {
  Name->output(OB, Flags);
}

void VcallThunkIdentifierNode::output(OutputBuffer &OB,
                                      OutputFlags Flags) const {
  OB << "`vcall'{" << OffsetInVTable << ", {flat}}";
}

void SpecialTableSymbolNode::output(OutputBuffer &OB, OutputFlags Flags) const {
  outputQualifiers(OB, Quals, false, true);
  Name->output(OB, Flags);
  if (TargetName) {
    OB << "{for `";
    TargetName->output(OB, Flags);
    OB << "'}";
  }
}
