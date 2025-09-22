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
// This file has no dependencies on the rest of LLVM so that it can be
// easily reused in other programs such as libcxxabi.
//
//===----------------------------------------------------------------------===//

#include "llvm/Demangle/MicrosoftDemangle.h"

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/DemangleConfig.h"
#include "llvm/Demangle/MicrosoftDemangleNodes.h"
#include "llvm/Demangle/StringViewExtras.h"
#include "llvm/Demangle/Utility.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string_view>
#include <tuple>

using namespace llvm;
using namespace ms_demangle;

static bool startsWithDigit(std::string_view S) {
  return !S.empty() && std::isdigit(S.front());
}

struct NodeList {
  Node *N = nullptr;
  NodeList *Next = nullptr;
};

static bool consumeFront(std::string_view &S, char C) {
  if (!llvm::itanium_demangle::starts_with(S, C))
    return false;
  S.remove_prefix(1);
  return true;
}

static bool consumeFront(std::string_view &S, std::string_view C) {
  if (!llvm::itanium_demangle::starts_with(S, C))
    return false;
  S.remove_prefix(C.size());
  return true;
}

static bool consumeFront(std::string_view &S, std::string_view PrefixA,
                         std::string_view PrefixB, bool A) {
  const std::string_view &Prefix = A ? PrefixA : PrefixB;
  return consumeFront(S, Prefix);
}

static bool startsWith(std::string_view S, std::string_view PrefixA,
                       std::string_view PrefixB, bool A) {
  const std::string_view &Prefix = A ? PrefixA : PrefixB;
  return llvm::itanium_demangle::starts_with(S, Prefix);
}

static bool isMemberPointer(std::string_view MangledName, bool &Error) {
  Error = false;
  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case '$':
    // This is probably an rvalue reference (e.g. $$Q), and you cannot have an
    // rvalue reference to a member.
    return false;
  case 'A':
    // 'A' indicates a reference, and you cannot have a reference to a member
    // function or member.
    return false;
  case 'P':
  case 'Q':
  case 'R':
  case 'S':
    // These 4 values indicate some kind of pointer, but we still don't know
    // what.
    break;
  default:
    // isMemberPointer() is called only if isPointerType() returns true,
    // and it rejects other prefixes.
    DEMANGLE_UNREACHABLE;
  }

  // If it starts with a number, then 6 indicates a non-member function
  // pointer, and 8 indicates a member function pointer.
  if (startsWithDigit(MangledName)) {
    if (MangledName[0] != '6' && MangledName[0] != '8') {
      Error = true;
      return false;
    }
    return (MangledName[0] == '8');
  }

  // Remove ext qualifiers since those can appear on either type and are
  // therefore not indicative.
  consumeFront(MangledName, 'E'); // 64-bit
  consumeFront(MangledName, 'I'); // restrict
  consumeFront(MangledName, 'F'); // unaligned

  if (MangledName.empty()) {
    Error = true;
    return false;
  }

  // The next value should be either ABCD (non-member) or QRST (member).
  switch (MangledName.front()) {
  case 'A':
  case 'B':
  case 'C':
  case 'D':
    return false;
  case 'Q':
  case 'R':
  case 'S':
  case 'T':
    return true;
  default:
    Error = true;
    return false;
  }
}

static SpecialIntrinsicKind
consumeSpecialIntrinsicKind(std::string_view &MangledName) {
  if (consumeFront(MangledName, "?_7"))
    return SpecialIntrinsicKind::Vftable;
  if (consumeFront(MangledName, "?_8"))
    return SpecialIntrinsicKind::Vbtable;
  if (consumeFront(MangledName, "?_9"))
    return SpecialIntrinsicKind::VcallThunk;
  if (consumeFront(MangledName, "?_A"))
    return SpecialIntrinsicKind::Typeof;
  if (consumeFront(MangledName, "?_B"))
    return SpecialIntrinsicKind::LocalStaticGuard;
  if (consumeFront(MangledName, "?_C"))
    return SpecialIntrinsicKind::StringLiteralSymbol;
  if (consumeFront(MangledName, "?_P"))
    return SpecialIntrinsicKind::UdtReturning;
  if (consumeFront(MangledName, "?_R0"))
    return SpecialIntrinsicKind::RttiTypeDescriptor;
  if (consumeFront(MangledName, "?_R1"))
    return SpecialIntrinsicKind::RttiBaseClassDescriptor;
  if (consumeFront(MangledName, "?_R2"))
    return SpecialIntrinsicKind::RttiBaseClassArray;
  if (consumeFront(MangledName, "?_R3"))
    return SpecialIntrinsicKind::RttiClassHierarchyDescriptor;
  if (consumeFront(MangledName, "?_R4"))
    return SpecialIntrinsicKind::RttiCompleteObjLocator;
  if (consumeFront(MangledName, "?_S"))
    return SpecialIntrinsicKind::LocalVftable;
  if (consumeFront(MangledName, "?__E"))
    return SpecialIntrinsicKind::DynamicInitializer;
  if (consumeFront(MangledName, "?__F"))
    return SpecialIntrinsicKind::DynamicAtexitDestructor;
  if (consumeFront(MangledName, "?__J"))
    return SpecialIntrinsicKind::LocalStaticThreadGuard;
  return SpecialIntrinsicKind::None;
}

static bool startsWithLocalScopePattern(std::string_view S) {
  if (!consumeFront(S, '?'))
    return false;

  size_t End = S.find('?');
  if (End == std::string_view::npos)
    return false;
  std::string_view Candidate = S.substr(0, End);
  if (Candidate.empty())
    return false;

  // \?[0-9]\?
  // ?@? is the discriminator 0.
  if (Candidate.size() == 1)
    return Candidate[0] == '@' || (Candidate[0] >= '0' && Candidate[0] <= '9');

  // If it's not 0-9, then it's an encoded number terminated with an @
  if (Candidate.back() != '@')
    return false;
  Candidate.remove_suffix(1);

  // An encoded number starts with B-P and all subsequent digits are in A-P.
  // Note that the reason the first digit cannot be A is two fold.  First, it
  // would create an ambiguity with ?A which delimits the beginning of an
  // anonymous namespace.  Second, A represents 0, and you don't start a multi
  // digit number with a leading 0.  Presumably the anonymous namespace
  // ambiguity is also why single digit encoded numbers use 0-9 rather than A-J.
  if (Candidate[0] < 'B' || Candidate[0] > 'P')
    return false;
  Candidate.remove_prefix(1);
  while (!Candidate.empty()) {
    if (Candidate[0] < 'A' || Candidate[0] > 'P')
      return false;
    Candidate.remove_prefix(1);
  }

  return true;
}

static bool isTagType(std::string_view S) {
  switch (S.front()) {
  case 'T': // union
  case 'U': // struct
  case 'V': // class
  case 'W': // enum
    return true;
  }
  return false;
}

static bool isCustomType(std::string_view S) { return S[0] == '?'; }

static bool isPointerType(std::string_view S) {
  if (llvm::itanium_demangle::starts_with(S, "$$Q")) // foo &&
    return true;

  switch (S.front()) {
  case 'A': // foo &
  case 'P': // foo *
  case 'Q': // foo *const
  case 'R': // foo *volatile
  case 'S': // foo *const volatile
    return true;
  }
  return false;
}

static bool isArrayType(std::string_view S) { return S[0] == 'Y'; }

static bool isFunctionType(std::string_view S) {
  return llvm::itanium_demangle::starts_with(S, "$$A8@@") ||
         llvm::itanium_demangle::starts_with(S, "$$A6");
}

static FunctionRefQualifier
demangleFunctionRefQualifier(std::string_view &MangledName) {
  if (consumeFront(MangledName, 'G'))
    return FunctionRefQualifier::Reference;
  else if (consumeFront(MangledName, 'H'))
    return FunctionRefQualifier::RValueReference;
  return FunctionRefQualifier::None;
}

static std::pair<Qualifiers, PointerAffinity>
demanglePointerCVQualifiers(std::string_view &MangledName) {
  if (consumeFront(MangledName, "$$Q"))
    return std::make_pair(Q_None, PointerAffinity::RValueReference);

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case 'A':
    return std::make_pair(Q_None, PointerAffinity::Reference);
  case 'P':
    return std::make_pair(Q_None, PointerAffinity::Pointer);
  case 'Q':
    return std::make_pair(Q_Const, PointerAffinity::Pointer);
  case 'R':
    return std::make_pair(Q_Volatile, PointerAffinity::Pointer);
  case 'S':
    return std::make_pair(Qualifiers(Q_Const | Q_Volatile),
                          PointerAffinity::Pointer);
  }
  // This function is only called if isPointerType() returns true,
  // and it only returns true for the six cases listed above.
  DEMANGLE_UNREACHABLE;
}

std::string_view Demangler::copyString(std::string_view Borrowed) {
  char *Stable = Arena.allocUnalignedBuffer(Borrowed.size());
  // This is not a micro-optimization, it avoids UB, should Borrowed be an null
  // buffer.
  if (Borrowed.size())
    std::memcpy(Stable, Borrowed.data(), Borrowed.size());

  return {Stable, Borrowed.size()};
}

SpecialTableSymbolNode *
Demangler::demangleSpecialTableSymbolNode(std::string_view &MangledName,
                                          SpecialIntrinsicKind K) {
  NamedIdentifierNode *NI = Arena.alloc<NamedIdentifierNode>();
  switch (K) {
  case SpecialIntrinsicKind::Vftable:
    NI->Name = "`vftable'";
    break;
  case SpecialIntrinsicKind::Vbtable:
    NI->Name = "`vbtable'";
    break;
  case SpecialIntrinsicKind::LocalVftable:
    NI->Name = "`local vftable'";
    break;
  case SpecialIntrinsicKind::RttiCompleteObjLocator:
    NI->Name = "`RTTI Complete Object Locator'";
    break;
  default:
    DEMANGLE_UNREACHABLE;
  }
  QualifiedNameNode *QN = demangleNameScopeChain(MangledName, NI);
  SpecialTableSymbolNode *STSN = Arena.alloc<SpecialTableSymbolNode>();
  STSN->Name = QN;
  bool IsMember = false;
  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }
  char Front = MangledName.front();
  MangledName.remove_prefix(1);
  if (Front != '6' && Front != '7') {
    Error = true;
    return nullptr;
  }

  std::tie(STSN->Quals, IsMember) = demangleQualifiers(MangledName);
  if (!consumeFront(MangledName, '@'))
    STSN->TargetName = demangleFullyQualifiedTypeName(MangledName);
  return STSN;
}

LocalStaticGuardVariableNode *
Demangler::demangleLocalStaticGuard(std::string_view &MangledName,
                                    bool IsThread) {
  LocalStaticGuardIdentifierNode *LSGI =
      Arena.alloc<LocalStaticGuardIdentifierNode>();
  LSGI->IsThread = IsThread;
  QualifiedNameNode *QN = demangleNameScopeChain(MangledName, LSGI);
  LocalStaticGuardVariableNode *LSGVN =
      Arena.alloc<LocalStaticGuardVariableNode>();
  LSGVN->Name = QN;

  if (consumeFront(MangledName, "4IA"))
    LSGVN->IsVisible = false;
  else if (consumeFront(MangledName, "5"))
    LSGVN->IsVisible = true;
  else {
    Error = true;
    return nullptr;
  }

  if (!MangledName.empty())
    LSGI->ScopeIndex = demangleUnsigned(MangledName);
  return LSGVN;
}

static NamedIdentifierNode *synthesizeNamedIdentifier(ArenaAllocator &Arena,
                                                      std::string_view Name) {
  NamedIdentifierNode *Id = Arena.alloc<NamedIdentifierNode>();
  Id->Name = Name;
  return Id;
}

static QualifiedNameNode *synthesizeQualifiedName(ArenaAllocator &Arena,
                                                  IdentifierNode *Identifier) {
  QualifiedNameNode *QN = Arena.alloc<QualifiedNameNode>();
  QN->Components = Arena.alloc<NodeArrayNode>();
  QN->Components->Count = 1;
  QN->Components->Nodes = Arena.allocArray<Node *>(1);
  QN->Components->Nodes[0] = Identifier;
  return QN;
}

static QualifiedNameNode *synthesizeQualifiedName(ArenaAllocator &Arena,
                                                  std::string_view Name) {
  NamedIdentifierNode *Id = synthesizeNamedIdentifier(Arena, Name);
  return synthesizeQualifiedName(Arena, Id);
}

static VariableSymbolNode *synthesizeVariable(ArenaAllocator &Arena,
                                              TypeNode *Type,
                                              std::string_view VariableName) {
  VariableSymbolNode *VSN = Arena.alloc<VariableSymbolNode>();
  VSN->Type = Type;
  VSN->Name = synthesizeQualifiedName(Arena, VariableName);
  return VSN;
}

VariableSymbolNode *
Demangler::demangleUntypedVariable(ArenaAllocator &Arena,
                                   std::string_view &MangledName,
                                   std::string_view VariableName) {
  NamedIdentifierNode *NI = synthesizeNamedIdentifier(Arena, VariableName);
  QualifiedNameNode *QN = demangleNameScopeChain(MangledName, NI);
  VariableSymbolNode *VSN = Arena.alloc<VariableSymbolNode>();
  VSN->Name = QN;
  if (consumeFront(MangledName, "8"))
    return VSN;

  Error = true;
  return nullptr;
}

VariableSymbolNode *
Demangler::demangleRttiBaseClassDescriptorNode(ArenaAllocator &Arena,
                                               std::string_view &MangledName) {
  RttiBaseClassDescriptorNode *RBCDN =
      Arena.alloc<RttiBaseClassDescriptorNode>();
  RBCDN->NVOffset = demangleUnsigned(MangledName);
  RBCDN->VBPtrOffset = demangleSigned(MangledName);
  RBCDN->VBTableOffset = demangleUnsigned(MangledName);
  RBCDN->Flags = demangleUnsigned(MangledName);
  if (Error)
    return nullptr;

  VariableSymbolNode *VSN = Arena.alloc<VariableSymbolNode>();
  VSN->Name = demangleNameScopeChain(MangledName, RBCDN);
  consumeFront(MangledName, '8');
  return VSN;
}

FunctionSymbolNode *
Demangler::demangleInitFiniStub(std::string_view &MangledName,
                                bool IsDestructor) {
  DynamicStructorIdentifierNode *DSIN =
      Arena.alloc<DynamicStructorIdentifierNode>();
  DSIN->IsDestructor = IsDestructor;

  bool IsKnownStaticDataMember = false;
  if (consumeFront(MangledName, '?'))
    IsKnownStaticDataMember = true;

  SymbolNode *Symbol = demangleDeclarator(MangledName);
  if (Error)
    return nullptr;

  FunctionSymbolNode *FSN = nullptr;

  if (Symbol->kind() == NodeKind::VariableSymbol) {
    DSIN->Variable = static_cast<VariableSymbolNode *>(Symbol);

    // Older versions of clang mangled this type of symbol incorrectly.  They
    // would omit the leading ? and they would only emit a single @ at the end.
    // The correct mangling is a leading ? and 2 trailing @ signs.  Handle
    // both cases.
    int AtCount = IsKnownStaticDataMember ? 2 : 1;
    for (int I = 0; I < AtCount; ++I) {
      if (consumeFront(MangledName, '@'))
        continue;
      Error = true;
      return nullptr;
    }

    FSN = demangleFunctionEncoding(MangledName);
    if (FSN)
      FSN->Name = synthesizeQualifiedName(Arena, DSIN);
  } else {
    if (IsKnownStaticDataMember) {
      // This was supposed to be a static data member, but we got a function.
      Error = true;
      return nullptr;
    }

    FSN = static_cast<FunctionSymbolNode *>(Symbol);
    DSIN->Name = Symbol->Name;
    FSN->Name = synthesizeQualifiedName(Arena, DSIN);
  }

  return FSN;
}

SymbolNode *Demangler::demangleSpecialIntrinsic(std::string_view &MangledName) {
  SpecialIntrinsicKind SIK = consumeSpecialIntrinsicKind(MangledName);

  switch (SIK) {
  case SpecialIntrinsicKind::None:
    return nullptr;
  case SpecialIntrinsicKind::StringLiteralSymbol:
    return demangleStringLiteral(MangledName);
  case SpecialIntrinsicKind::Vftable:
  case SpecialIntrinsicKind::Vbtable:
  case SpecialIntrinsicKind::LocalVftable:
  case SpecialIntrinsicKind::RttiCompleteObjLocator:
    return demangleSpecialTableSymbolNode(MangledName, SIK);
  case SpecialIntrinsicKind::VcallThunk:
    return demangleVcallThunkNode(MangledName);
  case SpecialIntrinsicKind::LocalStaticGuard:
    return demangleLocalStaticGuard(MangledName, /*IsThread=*/false);
  case SpecialIntrinsicKind::LocalStaticThreadGuard:
    return demangleLocalStaticGuard(MangledName, /*IsThread=*/true);
  case SpecialIntrinsicKind::RttiTypeDescriptor: {
    TypeNode *T = demangleType(MangledName, QualifierMangleMode::Result);
    if (Error)
      break;
    if (!consumeFront(MangledName, "@8"))
      break;
    if (!MangledName.empty())
      break;
    return synthesizeVariable(Arena, T, "`RTTI Type Descriptor'");
  }
  case SpecialIntrinsicKind::RttiBaseClassArray:
    return demangleUntypedVariable(Arena, MangledName,
                                   "`RTTI Base Class Array'");
  case SpecialIntrinsicKind::RttiClassHierarchyDescriptor:
    return demangleUntypedVariable(Arena, MangledName,
                                   "`RTTI Class Hierarchy Descriptor'");
  case SpecialIntrinsicKind::RttiBaseClassDescriptor:
    return demangleRttiBaseClassDescriptorNode(Arena, MangledName);
  case SpecialIntrinsicKind::DynamicInitializer:
    return demangleInitFiniStub(MangledName, /*IsDestructor=*/false);
  case SpecialIntrinsicKind::DynamicAtexitDestructor:
    return demangleInitFiniStub(MangledName, /*IsDestructor=*/true);
  case SpecialIntrinsicKind::Typeof:
  case SpecialIntrinsicKind::UdtReturning:
    // It's unclear which tools produces these manglings, so demangling
    // support is not (yet?) implemented.
    break;
  case SpecialIntrinsicKind::Unknown:
    DEMANGLE_UNREACHABLE; // Never returned by consumeSpecialIntrinsicKind.
  }
  Error = true;
  return nullptr;
}

IdentifierNode *
Demangler::demangleFunctionIdentifierCode(std::string_view &MangledName) {
  assert(llvm::itanium_demangle::starts_with(MangledName, '?'));
  MangledName.remove_prefix(1);
  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }

  if (consumeFront(MangledName, "__"))
    return demangleFunctionIdentifierCode(
        MangledName, FunctionIdentifierCodeGroup::DoubleUnder);
  if (consumeFront(MangledName, "_"))
    return demangleFunctionIdentifierCode(MangledName,
                                          FunctionIdentifierCodeGroup::Under);
  return demangleFunctionIdentifierCode(MangledName,
                                        FunctionIdentifierCodeGroup::Basic);
}

StructorIdentifierNode *
Demangler::demangleStructorIdentifier(std::string_view &MangledName,
                                      bool IsDestructor) {
  StructorIdentifierNode *N = Arena.alloc<StructorIdentifierNode>();
  N->IsDestructor = IsDestructor;
  return N;
}

ConversionOperatorIdentifierNode *
Demangler::demangleConversionOperatorIdentifier(std::string_view &MangledName) {
  ConversionOperatorIdentifierNode *N =
      Arena.alloc<ConversionOperatorIdentifierNode>();
  return N;
}

LiteralOperatorIdentifierNode *
Demangler::demangleLiteralOperatorIdentifier(std::string_view &MangledName) {
  LiteralOperatorIdentifierNode *N =
      Arena.alloc<LiteralOperatorIdentifierNode>();
  N->Name = demangleSimpleString(MangledName, /*Memorize=*/false);
  return N;
}

IntrinsicFunctionKind
Demangler::translateIntrinsicFunctionCode(char CH,
                                          FunctionIdentifierCodeGroup Group) {
  using IFK = IntrinsicFunctionKind;
  if (!(CH >= '0' && CH <= '9') && !(CH >= 'A' && CH <= 'Z')) {
    Error = true;
    return IFK::None;
  }

  // Not all ? identifiers are intrinsics *functions*.  This function only maps
  // operator codes for the special functions, all others are handled elsewhere,
  // hence the IFK::None entries in the table.
  static IFK Basic[36] = {
      IFK::None,             // ?0 # Foo::Foo()
      IFK::None,             // ?1 # Foo::~Foo()
      IFK::New,              // ?2 # operator new
      IFK::Delete,           // ?3 # operator delete
      IFK::Assign,           // ?4 # operator=
      IFK::RightShift,       // ?5 # operator>>
      IFK::LeftShift,        // ?6 # operator<<
      IFK::LogicalNot,       // ?7 # operator!
      IFK::Equals,           // ?8 # operator==
      IFK::NotEquals,        // ?9 # operator!=
      IFK::ArraySubscript,   // ?A # operator[]
      IFK::None,             // ?B # Foo::operator <type>()
      IFK::Pointer,          // ?C # operator->
      IFK::Dereference,      // ?D # operator*
      IFK::Increment,        // ?E # operator++
      IFK::Decrement,        // ?F # operator--
      IFK::Minus,            // ?G # operator-
      IFK::Plus,             // ?H # operator+
      IFK::BitwiseAnd,       // ?I # operator&
      IFK::MemberPointer,    // ?J # operator->*
      IFK::Divide,           // ?K # operator/
      IFK::Modulus,          // ?L # operator%
      IFK::LessThan,         // ?M operator<
      IFK::LessThanEqual,    // ?N operator<=
      IFK::GreaterThan,      // ?O operator>
      IFK::GreaterThanEqual, // ?P operator>=
      IFK::Comma,            // ?Q operator,
      IFK::Parens,           // ?R operator()
      IFK::BitwiseNot,       // ?S operator~
      IFK::BitwiseXor,       // ?T operator^
      IFK::BitwiseOr,        // ?U operator|
      IFK::LogicalAnd,       // ?V operator&&
      IFK::LogicalOr,        // ?W operator||
      IFK::TimesEqual,       // ?X operator*=
      IFK::PlusEqual,        // ?Y operator+=
      IFK::MinusEqual,       // ?Z operator-=
  };
  static IFK Under[36] = {
      IFK::DivEqual,           // ?_0 operator/=
      IFK::ModEqual,           // ?_1 operator%=
      IFK::RshEqual,           // ?_2 operator>>=
      IFK::LshEqual,           // ?_3 operator<<=
      IFK::BitwiseAndEqual,    // ?_4 operator&=
      IFK::BitwiseOrEqual,     // ?_5 operator|=
      IFK::BitwiseXorEqual,    // ?_6 operator^=
      IFK::None,               // ?_7 # vftable
      IFK::None,               // ?_8 # vbtable
      IFK::None,               // ?_9 # vcall
      IFK::None,               // ?_A # typeof
      IFK::None,               // ?_B # local static guard
      IFK::None,               // ?_C # string literal
      IFK::VbaseDtor,          // ?_D # vbase destructor
      IFK::VecDelDtor,         // ?_E # vector deleting destructor
      IFK::DefaultCtorClosure, // ?_F # default constructor closure
      IFK::ScalarDelDtor,      // ?_G # scalar deleting destructor
      IFK::VecCtorIter,        // ?_H # vector constructor iterator
      IFK::VecDtorIter,        // ?_I # vector destructor iterator
      IFK::VecVbaseCtorIter,   // ?_J # vector vbase constructor iterator
      IFK::VdispMap,           // ?_K # virtual displacement map
      IFK::EHVecCtorIter,      // ?_L # eh vector constructor iterator
      IFK::EHVecDtorIter,      // ?_M # eh vector destructor iterator
      IFK::EHVecVbaseCtorIter, // ?_N # eh vector vbase constructor iterator
      IFK::CopyCtorClosure,    // ?_O # copy constructor closure
      IFK::None,               // ?_P<name> # udt returning <name>
      IFK::None,               // ?_Q # <unknown>
      IFK::None,               // ?_R0 - ?_R4 # RTTI Codes
      IFK::None,               // ?_S # local vftable
      IFK::LocalVftableCtorClosure, // ?_T # local vftable constructor closure
      IFK::ArrayNew,                // ?_U operator new[]
      IFK::ArrayDelete,             // ?_V operator delete[]
      IFK::None,                    // ?_W <unused>
      IFK::None,                    // ?_X <unused>
      IFK::None,                    // ?_Y <unused>
      IFK::None,                    // ?_Z <unused>
  };
  static IFK DoubleUnder[36] = {
      IFK::None,                       // ?__0 <unused>
      IFK::None,                       // ?__1 <unused>
      IFK::None,                       // ?__2 <unused>
      IFK::None,                       // ?__3 <unused>
      IFK::None,                       // ?__4 <unused>
      IFK::None,                       // ?__5 <unused>
      IFK::None,                       // ?__6 <unused>
      IFK::None,                       // ?__7 <unused>
      IFK::None,                       // ?__8 <unused>
      IFK::None,                       // ?__9 <unused>
      IFK::ManVectorCtorIter,          // ?__A managed vector ctor iterator
      IFK::ManVectorDtorIter,          // ?__B managed vector dtor iterator
      IFK::EHVectorCopyCtorIter,       // ?__C EH vector copy ctor iterator
      IFK::EHVectorVbaseCopyCtorIter,  // ?__D EH vector vbase copy ctor iter
      IFK::None,                       // ?__E dynamic initializer for `T'
      IFK::None,                       // ?__F dynamic atexit destructor for `T'
      IFK::VectorCopyCtorIter,         // ?__G vector copy constructor iter
      IFK::VectorVbaseCopyCtorIter,    // ?__H vector vbase copy ctor iter
      IFK::ManVectorVbaseCopyCtorIter, // ?__I managed vector vbase copy ctor
                                       // iter
      IFK::None,                       // ?__J local static thread guard
      IFK::None,                       // ?__K operator ""_name
      IFK::CoAwait,                    // ?__L operator co_await
      IFK::Spaceship,                  // ?__M operator<=>
      IFK::None,                       // ?__N <unused>
      IFK::None,                       // ?__O <unused>
      IFK::None,                       // ?__P <unused>
      IFK::None,                       // ?__Q <unused>
      IFK::None,                       // ?__R <unused>
      IFK::None,                       // ?__S <unused>
      IFK::None,                       // ?__T <unused>
      IFK::None,                       // ?__U <unused>
      IFK::None,                       // ?__V <unused>
      IFK::None,                       // ?__W <unused>
      IFK::None,                       // ?__X <unused>
      IFK::None,                       // ?__Y <unused>
      IFK::None,                       // ?__Z <unused>
  };

  int Index = (CH >= '0' && CH <= '9') ? (CH - '0') : (CH - 'A' + 10);
  switch (Group) {
  case FunctionIdentifierCodeGroup::Basic:
    return Basic[Index];
  case FunctionIdentifierCodeGroup::Under:
    return Under[Index];
  case FunctionIdentifierCodeGroup::DoubleUnder:
    return DoubleUnder[Index];
  }
  DEMANGLE_UNREACHABLE;
}

IdentifierNode *
Demangler::demangleFunctionIdentifierCode(std::string_view &MangledName,
                                          FunctionIdentifierCodeGroup Group) {
  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }
  const char CH = MangledName.front();
  switch (Group) {
  case FunctionIdentifierCodeGroup::Basic:
    MangledName.remove_prefix(1);
    switch (CH) {
    case '0':
    case '1':
      return demangleStructorIdentifier(MangledName, CH == '1');
    case 'B':
      return demangleConversionOperatorIdentifier(MangledName);
    default:
      return Arena.alloc<IntrinsicFunctionIdentifierNode>(
          translateIntrinsicFunctionCode(CH, Group));
    }
  case FunctionIdentifierCodeGroup::Under:
    MangledName.remove_prefix(1);
    return Arena.alloc<IntrinsicFunctionIdentifierNode>(
        translateIntrinsicFunctionCode(CH, Group));
  case FunctionIdentifierCodeGroup::DoubleUnder:
    MangledName.remove_prefix(1);
    switch (CH) {
    case 'K':
      return demangleLiteralOperatorIdentifier(MangledName);
    default:
      return Arena.alloc<IntrinsicFunctionIdentifierNode>(
          translateIntrinsicFunctionCode(CH, Group));
    }
  }

  DEMANGLE_UNREACHABLE;
}

SymbolNode *Demangler::demangleEncodedSymbol(std::string_view &MangledName,
                                             QualifiedNameNode *Name) {
  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }

  // Read a variable.
  switch (MangledName.front()) {
  case '0':
  case '1':
  case '2':
  case '3':
  case '4': {
    StorageClass SC = demangleVariableStorageClass(MangledName);
    return demangleVariableEncoding(MangledName, SC);
  }
  }
  FunctionSymbolNode *FSN = demangleFunctionEncoding(MangledName);

  IdentifierNode *UQN = Name->getUnqualifiedIdentifier();
  if (UQN->kind() == NodeKind::ConversionOperatorIdentifier) {
    ConversionOperatorIdentifierNode *COIN =
        static_cast<ConversionOperatorIdentifierNode *>(UQN);
    if (FSN)
      COIN->TargetType = FSN->Signature->ReturnType;
  }
  return FSN;
}

SymbolNode *Demangler::demangleDeclarator(std::string_view &MangledName) {
  // What follows is a main symbol name. This may include namespaces or class
  // back references.
  QualifiedNameNode *QN = demangleFullyQualifiedSymbolName(MangledName);
  if (Error)
    return nullptr;

  SymbolNode *Symbol = demangleEncodedSymbol(MangledName, QN);
  if (Error)
    return nullptr;
  Symbol->Name = QN;

  IdentifierNode *UQN = QN->getUnqualifiedIdentifier();
  if (UQN->kind() == NodeKind::ConversionOperatorIdentifier) {
    ConversionOperatorIdentifierNode *COIN =
        static_cast<ConversionOperatorIdentifierNode *>(UQN);
    if (!COIN->TargetType) {
      Error = true;
      return nullptr;
    }
  }
  return Symbol;
}

SymbolNode *Demangler::demangleMD5Name(std::string_view &MangledName) {
  assert(llvm::itanium_demangle::starts_with(MangledName, "??@"));
  // This is an MD5 mangled name.  We can't demangle it, just return the
  // mangled name.
  // An MD5 mangled name is ??@ followed by 32 characters and a terminating @.
  size_t MD5Last = MangledName.find('@', strlen("??@"));
  if (MD5Last == std::string_view::npos) {
    Error = true;
    return nullptr;
  }
  const char *Start = MangledName.data();
  const size_t StartSize = MangledName.size();
  MangledName.remove_prefix(MD5Last + 1);

  // There are two additional special cases for MD5 names:
  // 1. For complete object locators where the object name is long enough
  //    for the object to have an MD5 name, the complete object locator is
  //    called ??@...@??_R4@ (with a trailing "??_R4@" instead of the usual
  //    leading "??_R4". This is handled here.
  // 2. For catchable types, in versions of MSVC before 2015 (<1900) or after
  //    2017.2 (>= 1914), the catchable type mangling is _CT??@...@??@...@8
  //    instead of_CT??@...@8 with just one MD5 name. Since we don't yet
  //    demangle catchable types anywhere, this isn't handled for MD5 names
  //    either.
  consumeFront(MangledName, "??_R4@");

  assert(MangledName.size() < StartSize);
  const size_t Count = StartSize - MangledName.size();
  std::string_view MD5(Start, Count);
  SymbolNode *S = Arena.alloc<SymbolNode>(NodeKind::Md5Symbol);
  S->Name = synthesizeQualifiedName(Arena, MD5);

  return S;
}

SymbolNode *Demangler::demangleTypeinfoName(std::string_view &MangledName) {
  assert(llvm::itanium_demangle::starts_with(MangledName, '.'));
  consumeFront(MangledName, '.');

  TypeNode *T = demangleType(MangledName, QualifierMangleMode::Result);
  if (Error || !MangledName.empty()) {
    Error = true;
    return nullptr;
  }
  return synthesizeVariable(Arena, T, "`RTTI Type Descriptor Name'");
}

// Parser entry point.
SymbolNode *Demangler::parse(std::string_view &MangledName) {
  // Typeinfo names are strings stored in RTTI data. They're not symbol names.
  // It's still useful to demangle them. They're the only demangled entity
  // that doesn't start with a "?" but a ".".
  if (llvm::itanium_demangle::starts_with(MangledName, '.'))
    return demangleTypeinfoName(MangledName);

  if (llvm::itanium_demangle::starts_with(MangledName, "??@"))
    return demangleMD5Name(MangledName);

  // MSVC-style mangled symbols must start with '?'.
  if (!llvm::itanium_demangle::starts_with(MangledName, '?')) {
    Error = true;
    return nullptr;
  }

  consumeFront(MangledName, '?');

  // ?$ is a template instantiation, but all other names that start with ? are
  // operators / special names.
  if (SymbolNode *SI = demangleSpecialIntrinsic(MangledName))
    return SI;

  return demangleDeclarator(MangledName);
}

TagTypeNode *Demangler::parseTagUniqueName(std::string_view &MangledName) {
  if (!consumeFront(MangledName, ".?A")) {
    Error = true;
    return nullptr;
  }
  consumeFront(MangledName, ".?A");
  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }

  return demangleClassType(MangledName);
}

// <type-encoding> ::= <storage-class> <variable-type>
// <storage-class> ::= 0  # private static member
//                 ::= 1  # protected static member
//                 ::= 2  # public static member
//                 ::= 3  # global
//                 ::= 4  # static local

VariableSymbolNode *
Demangler::demangleVariableEncoding(std::string_view &MangledName,
                                    StorageClass SC) {
  VariableSymbolNode *VSN = Arena.alloc<VariableSymbolNode>();

  VSN->Type = demangleType(MangledName, QualifierMangleMode::Drop);
  VSN->SC = SC;

  if (Error)
    return nullptr;

  // <variable-type> ::= <type> <cvr-qualifiers>
  //                 ::= <type> <pointee-cvr-qualifiers> # pointers, references
  switch (VSN->Type->kind()) {
  case NodeKind::PointerType: {
    PointerTypeNode *PTN = static_cast<PointerTypeNode *>(VSN->Type);

    Qualifiers ExtraChildQuals = Q_None;
    PTN->Quals = Qualifiers(VSN->Type->Quals |
                            demanglePointerExtQualifiers(MangledName));

    bool IsMember = false;
    std::tie(ExtraChildQuals, IsMember) = demangleQualifiers(MangledName);

    if (PTN->ClassParent) {
      QualifiedNameNode *BackRefName =
          demangleFullyQualifiedTypeName(MangledName);
      (void)BackRefName;
    }
    PTN->Pointee->Quals = Qualifiers(PTN->Pointee->Quals | ExtraChildQuals);

    break;
  }
  default:
    VSN->Type->Quals = demangleQualifiers(MangledName).first;
    break;
  }

  return VSN;
}

// Sometimes numbers are encoded in mangled symbols. For example,
// "int (*x)[20]" is a valid C type (x is a pointer to an array of
// length 20), so we need some way to embed numbers as part of symbols.
// This function parses it.
//
// <number>               ::= [?] <non-negative integer>
//
// <non-negative integer> ::= <decimal digit> # when 1 <= Number <= 10
//                        ::= <hex digit>+ @  # when Number == 0 or >= 10
//
// <hex-digit>            ::= [A-P]           # A = 0, B = 1, ...
std::pair<uint64_t, bool>
Demangler::demangleNumber(std::string_view &MangledName) {
  bool IsNegative = consumeFront(MangledName, '?');

  if (startsWithDigit(MangledName)) {
    uint64_t Ret = MangledName[0] - '0' + 1;
    MangledName.remove_prefix(1);
    return {Ret, IsNegative};
  }

  uint64_t Ret = 0;
  for (size_t i = 0; i < MangledName.size(); ++i) {
    char C = MangledName[i];
    if (C == '@') {
      MangledName.remove_prefix(i + 1);
      return {Ret, IsNegative};
    }
    if ('A' <= C && C <= 'P') {
      Ret = (Ret << 4) + (C - 'A');
      continue;
    }
    break;
  }

  Error = true;
  return {0ULL, false};
}

uint64_t Demangler::demangleUnsigned(std::string_view &MangledName) {
  bool IsNegative = false;
  uint64_t Number = 0;
  std::tie(Number, IsNegative) = demangleNumber(MangledName);
  if (IsNegative)
    Error = true;
  return Number;
}

int64_t Demangler::demangleSigned(std::string_view &MangledName) {
  bool IsNegative = false;
  uint64_t Number = 0;
  std::tie(Number, IsNegative) = demangleNumber(MangledName);
  if (Number > INT64_MAX)
    Error = true;
  int64_t I = static_cast<int64_t>(Number);
  return IsNegative ? -I : I;
}

// First 10 strings can be referenced by special BackReferences ?0, ?1, ..., ?9.
// Memorize it.
void Demangler::memorizeString(std::string_view S) {
  if (Backrefs.NamesCount >= BackrefContext::Max)
    return;
  for (size_t i = 0; i < Backrefs.NamesCount; ++i)
    if (S == Backrefs.Names[i]->Name)
      return;
  NamedIdentifierNode *N = Arena.alloc<NamedIdentifierNode>();
  N->Name = S;
  Backrefs.Names[Backrefs.NamesCount++] = N;
}

NamedIdentifierNode *
Demangler::demangleBackRefName(std::string_view &MangledName) {
  assert(startsWithDigit(MangledName));

  size_t I = MangledName[0] - '0';
  if (I >= Backrefs.NamesCount) {
    Error = true;
    return nullptr;
  }

  MangledName.remove_prefix(1);
  return Backrefs.Names[I];
}

void Demangler::memorizeIdentifier(IdentifierNode *Identifier) {
  // Render this class template name into a string buffer so that we can
  // memorize it for the purpose of back-referencing.
  OutputBuffer OB;
  Identifier->output(OB, OF_Default);
  std::string_view Owned = copyString(OB);
  memorizeString(Owned);
  std::free(OB.getBuffer());
}

IdentifierNode *
Demangler::demangleTemplateInstantiationName(std::string_view &MangledName,
                                             NameBackrefBehavior NBB) {
  assert(llvm::itanium_demangle::starts_with(MangledName, "?$"));
  consumeFront(MangledName, "?$");

  BackrefContext OuterContext;
  std::swap(OuterContext, Backrefs);

  IdentifierNode *Identifier =
      demangleUnqualifiedSymbolName(MangledName, NBB_Simple);
  if (!Error)
    Identifier->TemplateParams = demangleTemplateParameterList(MangledName);

  std::swap(OuterContext, Backrefs);
  if (Error)
    return nullptr;

  if (NBB & NBB_Template) {
    // NBB_Template is only set for types and non-leaf names ("a::" in "a::b").
    // Structors and conversion operators only makes sense in a leaf name, so
    // reject them in NBB_Template contexts.
    if (Identifier->kind() == NodeKind::ConversionOperatorIdentifier ||
        Identifier->kind() == NodeKind::StructorIdentifier) {
      Error = true;
      return nullptr;
    }

    memorizeIdentifier(Identifier);
  }

  return Identifier;
}

NamedIdentifierNode *
Demangler::demangleSimpleName(std::string_view &MangledName, bool Memorize) {
  std::string_view S = demangleSimpleString(MangledName, Memorize);
  if (Error)
    return nullptr;

  NamedIdentifierNode *Name = Arena.alloc<NamedIdentifierNode>();
  Name->Name = S;
  return Name;
}

static bool isRebasedHexDigit(char C) { return (C >= 'A' && C <= 'P'); }

static uint8_t rebasedHexDigitToNumber(char C) {
  assert(isRebasedHexDigit(C));
  return (C <= 'J') ? (C - 'A') : (10 + C - 'K');
}

uint8_t Demangler::demangleCharLiteral(std::string_view &MangledName) {
  assert(!MangledName.empty());
  if (!llvm::itanium_demangle::starts_with(MangledName, '?')) {
    const uint8_t F = MangledName.front();
    MangledName.remove_prefix(1);
    return F;
  }

  MangledName.remove_prefix(1);
  if (MangledName.empty())
    goto CharLiteralError;

  if (consumeFront(MangledName, '$')) {
    // Two hex digits
    if (MangledName.size() < 2)
      goto CharLiteralError;
    std::string_view Nibbles = MangledName.substr(0, 2);
    if (!isRebasedHexDigit(Nibbles[0]) || !isRebasedHexDigit(Nibbles[1]))
      goto CharLiteralError;
    // Don't append the null terminator.
    uint8_t C1 = rebasedHexDigitToNumber(Nibbles[0]);
    uint8_t C2 = rebasedHexDigitToNumber(Nibbles[1]);
    MangledName.remove_prefix(2);
    return (C1 << 4) | C2;
  }

  if (startsWithDigit(MangledName)) {
    const char *Lookup = ",/\\:. \n\t'-";
    char C = Lookup[MangledName[0] - '0'];
    MangledName.remove_prefix(1);
    return C;
  }

  if (MangledName[0] >= 'a' && MangledName[0] <= 'z') {
    char Lookup[26] = {'\xE1', '\xE2', '\xE3', '\xE4', '\xE5', '\xE6', '\xE7',
                       '\xE8', '\xE9', '\xEA', '\xEB', '\xEC', '\xED', '\xEE',
                       '\xEF', '\xF0', '\xF1', '\xF2', '\xF3', '\xF4', '\xF5',
                       '\xF6', '\xF7', '\xF8', '\xF9', '\xFA'};
    char C = Lookup[MangledName[0] - 'a'];
    MangledName.remove_prefix(1);
    return C;
  }

  if (MangledName[0] >= 'A' && MangledName[0] <= 'Z') {
    char Lookup[26] = {'\xC1', '\xC2', '\xC3', '\xC4', '\xC5', '\xC6', '\xC7',
                       '\xC8', '\xC9', '\xCA', '\xCB', '\xCC', '\xCD', '\xCE',
                       '\xCF', '\xD0', '\xD1', '\xD2', '\xD3', '\xD4', '\xD5',
                       '\xD6', '\xD7', '\xD8', '\xD9', '\xDA'};
    char C = Lookup[MangledName[0] - 'A'];
    MangledName.remove_prefix(1);
    return C;
  }

CharLiteralError:
  Error = true;
  return '\0';
}

wchar_t Demangler::demangleWcharLiteral(std::string_view &MangledName) {
  uint8_t C1, C2;

  C1 = demangleCharLiteral(MangledName);
  if (Error || MangledName.empty())
    goto WCharLiteralError;
  C2 = demangleCharLiteral(MangledName);
  if (Error)
    goto WCharLiteralError;

  return ((wchar_t)C1 << 8) | (wchar_t)C2;

WCharLiteralError:
  Error = true;
  return L'\0';
}

static void writeHexDigit(char *Buffer, uint8_t Digit) {
  assert(Digit <= 15);
  *Buffer = (Digit < 10) ? ('0' + Digit) : ('A' + Digit - 10);
}

static void outputHex(OutputBuffer &OB, unsigned C) {
  assert (C != 0);

  // It's easier to do the math if we can work from right to left, but we need
  // to print the numbers from left to right.  So render this into a temporary
  // buffer first, then output the temporary buffer.  Each byte is of the form
  // \xAB, which means that each byte needs 4 characters.  Since there are at
  // most 4 bytes, we need a 4*4+1 = 17 character temporary buffer.
  char TempBuffer[17];

  ::memset(TempBuffer, 0, sizeof(TempBuffer));
  constexpr int MaxPos = sizeof(TempBuffer) - 1;

  int Pos = MaxPos - 1; // TempBuffer[MaxPos] is the terminating \0.
  while (C != 0) {
    for (int I = 0; I < 2; ++I) {
      writeHexDigit(&TempBuffer[Pos--], C % 16);
      C /= 16;
    }
  }
  TempBuffer[Pos--] = 'x';
  assert(Pos >= 0);
  TempBuffer[Pos--] = '\\';
  OB << std::string_view(&TempBuffer[Pos + 1]);
}

static void outputEscapedChar(OutputBuffer &OB, unsigned C) {
  switch (C) {
  case '\0': // nul
    OB << "\\0";
    return;
  case '\'': // single quote
    OB << "\\\'";
    return;
  case '\"': // double quote
    OB << "\\\"";
    return;
  case '\\': // backslash
    OB << "\\\\";
    return;
  case '\a': // bell
    OB << "\\a";
    return;
  case '\b': // backspace
    OB << "\\b";
    return;
  case '\f': // form feed
    OB << "\\f";
    return;
  case '\n': // new line
    OB << "\\n";
    return;
  case '\r': // carriage return
    OB << "\\r";
    return;
  case '\t': // tab
    OB << "\\t";
    return;
  case '\v': // vertical tab
    OB << "\\v";
    return;
  default:
    break;
  }

  if (C > 0x1F && C < 0x7F) {
    // Standard ascii char.
    OB << (char)C;
    return;
  }

  outputHex(OB, C);
}

static unsigned countTrailingNullBytes(const uint8_t *StringBytes, int Length) {
  const uint8_t *End = StringBytes + Length - 1;
  unsigned Count = 0;
  while (Length > 0 && *End == 0) {
    --Length;
    --End;
    ++Count;
  }
  return Count;
}

static unsigned countEmbeddedNulls(const uint8_t *StringBytes,
                                   unsigned Length) {
  unsigned Result = 0;
  for (unsigned I = 0; I < Length; ++I) {
    if (*StringBytes++ == 0)
      ++Result;
  }
  return Result;
}

// A mangled (non-wide) string literal stores the total length of the string it
// refers to (passed in NumBytes), and it contains up to 32 bytes of actual text
// (passed in StringBytes, NumChars).
static unsigned guessCharByteSize(const uint8_t *StringBytes, unsigned NumChars,
                                  uint64_t NumBytes) {
  assert(NumBytes > 0);

  // If the number of bytes is odd, this is guaranteed to be a char string.
  if (NumBytes % 2 == 1)
    return 1;

  // All strings can encode at most 32 bytes of data.  If it's less than that,
  // then we encoded the entire string.  In this case we check for a 1-byte,
  // 2-byte, or 4-byte null terminator.
  if (NumBytes < 32) {
    unsigned TrailingNulls = countTrailingNullBytes(StringBytes, NumChars);
    if (TrailingNulls >= 4 && NumBytes % 4 == 0)
      return 4;
    if (TrailingNulls >= 2)
      return 2;
    return 1;
  }

  // The whole string was not able to be encoded.  Try to look at embedded null
  // terminators to guess.  The heuristic is that we count all embedded null
  // terminators.  If more than 2/3 are null, it's a char32.  If more than 1/3
  // are null, it's a char16.  Otherwise it's a char8.  This obviously isn't
  // perfect and is biased towards languages that have ascii alphabets, but this
  // was always going to be best effort since the encoding is lossy.
  unsigned Nulls = countEmbeddedNulls(StringBytes, NumChars);
  if (Nulls >= 2 * NumChars / 3 && NumBytes % 4 == 0)
    return 4;
  if (Nulls >= NumChars / 3)
    return 2;
  return 1;
}

static unsigned decodeMultiByteChar(const uint8_t *StringBytes,
                                    unsigned CharIndex, unsigned CharBytes) {
  assert(CharBytes == 1 || CharBytes == 2 || CharBytes == 4);
  unsigned Offset = CharIndex * CharBytes;
  unsigned Result = 0;
  StringBytes = StringBytes + Offset;
  for (unsigned I = 0; I < CharBytes; ++I) {
    unsigned C = static_cast<unsigned>(StringBytes[I]);
    Result |= C << (8 * I);
  }
  return Result;
}

FunctionSymbolNode *
Demangler::demangleVcallThunkNode(std::string_view &MangledName) {
  FunctionSymbolNode *FSN = Arena.alloc<FunctionSymbolNode>();
  VcallThunkIdentifierNode *VTIN = Arena.alloc<VcallThunkIdentifierNode>();
  FSN->Signature = Arena.alloc<ThunkSignatureNode>();
  FSN->Signature->FunctionClass = FC_NoParameterList;

  FSN->Name = demangleNameScopeChain(MangledName, VTIN);
  if (!Error)
    Error = !consumeFront(MangledName, "$B");
  if (!Error)
    VTIN->OffsetInVTable = demangleUnsigned(MangledName);
  if (!Error)
    Error = !consumeFront(MangledName, 'A');
  if (!Error)
    FSN->Signature->CallConvention = demangleCallingConvention(MangledName);
  return (Error) ? nullptr : FSN;
}

EncodedStringLiteralNode *
Demangler::demangleStringLiteral(std::string_view &MangledName) {
  // This function uses goto, so declare all variables up front.
  OutputBuffer OB;
  std::string_view CRC;
  uint64_t StringByteSize;
  bool IsWcharT = false;
  bool IsNegative = false;
  size_t CrcEndPos = 0;
  char F;

  EncodedStringLiteralNode *Result = Arena.alloc<EncodedStringLiteralNode>();

  // Prefix indicating the beginning of a string literal
  if (!consumeFront(MangledName, "@_"))
    goto StringLiteralError;
  if (MangledName.empty())
    goto StringLiteralError;

  // Char Type (regular or wchar_t)
  F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case '1':
    IsWcharT = true;
    DEMANGLE_FALLTHROUGH;
  case '0':
    break;
  default:
    goto StringLiteralError;
  }

  // Encoded Length
  std::tie(StringByteSize, IsNegative) = demangleNumber(MangledName);
  if (Error || IsNegative || StringByteSize < (IsWcharT ? 2 : 1))
    goto StringLiteralError;

  // CRC 32 (always 8 characters plus a terminator)
  CrcEndPos = MangledName.find('@');
  if (CrcEndPos == std::string_view::npos)
    goto StringLiteralError;
  CRC = MangledName.substr(0, CrcEndPos);
  MangledName.remove_prefix(CrcEndPos + 1);
  if (MangledName.empty())
    goto StringLiteralError;

  if (IsWcharT) {
    Result->Char = CharKind::Wchar;
    if (StringByteSize > 64)
      Result->IsTruncated = true;

    while (!consumeFront(MangledName, '@')) {
      if (MangledName.size() < 2)
        goto StringLiteralError;
      wchar_t W = demangleWcharLiteral(MangledName);
      if (StringByteSize != 2 || Result->IsTruncated)
        outputEscapedChar(OB, W);
      StringByteSize -= 2;
      if (Error)
        goto StringLiteralError;
    }
  } else {
    // The max byte length is actually 32, but some compilers mangled strings
    // incorrectly, so we have to assume it can go higher.
    constexpr unsigned MaxStringByteLength = 32 * 4;
    uint8_t StringBytes[MaxStringByteLength];

    unsigned BytesDecoded = 0;
    while (!consumeFront(MangledName, '@')) {
      if (MangledName.size() < 1 || BytesDecoded >= MaxStringByteLength)
        goto StringLiteralError;
      StringBytes[BytesDecoded++] = demangleCharLiteral(MangledName);
    }

    if (StringByteSize > BytesDecoded)
      Result->IsTruncated = true;

    unsigned CharBytes =
        guessCharByteSize(StringBytes, BytesDecoded, StringByteSize);
    assert(StringByteSize % CharBytes == 0);
    switch (CharBytes) {
    case 1:
      Result->Char = CharKind::Char;
      break;
    case 2:
      Result->Char = CharKind::Char16;
      break;
    case 4:
      Result->Char = CharKind::Char32;
      break;
    default:
      DEMANGLE_UNREACHABLE;
    }
    const unsigned NumChars = BytesDecoded / CharBytes;
    for (unsigned CharIndex = 0; CharIndex < NumChars; ++CharIndex) {
      unsigned NextChar =
          decodeMultiByteChar(StringBytes, CharIndex, CharBytes);
      if (CharIndex + 1 < NumChars || Result->IsTruncated)
        outputEscapedChar(OB, NextChar);
    }
  }

  Result->DecodedString = copyString(OB);
  std::free(OB.getBuffer());
  return Result;

StringLiteralError:
  Error = true;
  std::free(OB.getBuffer());
  return nullptr;
}

// Returns MangledName's prefix before the first '@', or an error if
// MangledName contains no '@' or the prefix has length 0.
std::string_view Demangler::demangleSimpleString(std::string_view &MangledName,
                                                 bool Memorize) {
  std::string_view S;
  for (size_t i = 0; i < MangledName.size(); ++i) {
    if (MangledName[i] != '@')
      continue;
    if (i == 0)
      break;
    S = MangledName.substr(0, i);
    MangledName.remove_prefix(i + 1);

    if (Memorize)
      memorizeString(S);
    return S;
  }

  Error = true;
  return {};
}

NamedIdentifierNode *
Demangler::demangleAnonymousNamespaceName(std::string_view &MangledName) {
  assert(llvm::itanium_demangle::starts_with(MangledName, "?A"));
  consumeFront(MangledName, "?A");

  NamedIdentifierNode *Node = Arena.alloc<NamedIdentifierNode>();
  Node->Name = "`anonymous namespace'";
  size_t EndPos = MangledName.find('@');
  if (EndPos == std::string_view::npos) {
    Error = true;
    return nullptr;
  }
  std::string_view NamespaceKey = MangledName.substr(0, EndPos);
  memorizeString(NamespaceKey);
  MangledName = MangledName.substr(EndPos + 1);
  return Node;
}

NamedIdentifierNode *
Demangler::demangleLocallyScopedNamePiece(std::string_view &MangledName) {
  assert(startsWithLocalScopePattern(MangledName));

  NamedIdentifierNode *Identifier = Arena.alloc<NamedIdentifierNode>();
  consumeFront(MangledName, '?');
  uint64_t Number = 0;
  bool IsNegative = false;
  std::tie(Number, IsNegative) = demangleNumber(MangledName);
  assert(!IsNegative);

  // One ? to terminate the number
  consumeFront(MangledName, '?');

  assert(!Error);
  Node *Scope = parse(MangledName);
  if (Error)
    return nullptr;

  // Render the parent symbol's name into a buffer.
  OutputBuffer OB;
  OB << '`';
  Scope->output(OB, OF_Default);
  OB << '\'';
  OB << "::`" << Number << "'";

  Identifier->Name = copyString(OB);
  std::free(OB.getBuffer());
  return Identifier;
}

// Parses a type name in the form of A@B@C@@ which represents C::B::A.
QualifiedNameNode *
Demangler::demangleFullyQualifiedTypeName(std::string_view &MangledName) {
  IdentifierNode *Identifier =
      demangleUnqualifiedTypeName(MangledName, /*Memorize=*/true);
  if (Error)
    return nullptr;
  assert(Identifier);

  QualifiedNameNode *QN = demangleNameScopeChain(MangledName, Identifier);
  if (Error)
    return nullptr;
  assert(QN);
  return QN;
}

// Parses a symbol name in the form of A@B@C@@ which represents C::B::A.
// Symbol names have slightly different rules regarding what can appear
// so we separate out the implementations for flexibility.
QualifiedNameNode *
Demangler::demangleFullyQualifiedSymbolName(std::string_view &MangledName) {
  // This is the final component of a symbol name (i.e. the leftmost component
  // of a mangled name.  Since the only possible template instantiation that
  // can appear in this context is a function template, and since those are
  // not saved for the purposes of name backreferences, only backref simple
  // names.
  IdentifierNode *Identifier =
      demangleUnqualifiedSymbolName(MangledName, NBB_Simple);
  if (Error)
    return nullptr;

  QualifiedNameNode *QN = demangleNameScopeChain(MangledName, Identifier);
  if (Error)
    return nullptr;

  if (Identifier->kind() == NodeKind::StructorIdentifier) {
    if (QN->Components->Count < 2) {
      Error = true;
      return nullptr;
    }
    StructorIdentifierNode *SIN =
        static_cast<StructorIdentifierNode *>(Identifier);
    Node *ClassNode = QN->Components->Nodes[QN->Components->Count - 2];
    SIN->Class = static_cast<IdentifierNode *>(ClassNode);
  }
  assert(QN);
  return QN;
}

IdentifierNode *
Demangler::demangleUnqualifiedTypeName(std::string_view &MangledName,
                                       bool Memorize) {
  // An inner-most name can be a back-reference, because a fully-qualified name
  // (e.g. Scope + Inner) can contain other fully qualified names inside of
  // them (for example template parameters), and these nested parameters can
  // refer to previously mangled types.
  if (startsWithDigit(MangledName))
    return demangleBackRefName(MangledName);

  if (llvm::itanium_demangle::starts_with(MangledName, "?$"))
    return demangleTemplateInstantiationName(MangledName, NBB_Template);

  return demangleSimpleName(MangledName, Memorize);
}

IdentifierNode *
Demangler::demangleUnqualifiedSymbolName(std::string_view &MangledName,
                                         NameBackrefBehavior NBB) {
  if (startsWithDigit(MangledName))
    return demangleBackRefName(MangledName);
  if (llvm::itanium_demangle::starts_with(MangledName, "?$"))
    return demangleTemplateInstantiationName(MangledName, NBB);
  if (llvm::itanium_demangle::starts_with(MangledName, '?'))
    return demangleFunctionIdentifierCode(MangledName);
  return demangleSimpleName(MangledName, /*Memorize=*/(NBB & NBB_Simple) != 0);
}

IdentifierNode *
Demangler::demangleNameScopePiece(std::string_view &MangledName) {
  if (startsWithDigit(MangledName))
    return demangleBackRefName(MangledName);

  if (llvm::itanium_demangle::starts_with(MangledName, "?$"))
    return demangleTemplateInstantiationName(MangledName, NBB_Template);

  if (llvm::itanium_demangle::starts_with(MangledName, "?A"))
    return demangleAnonymousNamespaceName(MangledName);

  if (startsWithLocalScopePattern(MangledName))
    return demangleLocallyScopedNamePiece(MangledName);

  return demangleSimpleName(MangledName, /*Memorize=*/true);
}

static NodeArrayNode *nodeListToNodeArray(ArenaAllocator &Arena, NodeList *Head,
                                          size_t Count) {
  NodeArrayNode *N = Arena.alloc<NodeArrayNode>();
  N->Count = Count;
  N->Nodes = Arena.allocArray<Node *>(Count);
  for (size_t I = 0; I < Count; ++I) {
    N->Nodes[I] = Head->N;
    Head = Head->Next;
  }
  return N;
}

QualifiedNameNode *
Demangler::demangleNameScopeChain(std::string_view &MangledName,
                                  IdentifierNode *UnqualifiedName) {
  NodeList *Head = Arena.alloc<NodeList>();

  Head->N = UnqualifiedName;

  size_t Count = 1;
  while (!consumeFront(MangledName, "@")) {
    ++Count;
    NodeList *NewHead = Arena.alloc<NodeList>();
    NewHead->Next = Head;
    Head = NewHead;

    if (MangledName.empty()) {
      Error = true;
      return nullptr;
    }

    assert(!Error);
    IdentifierNode *Elem = demangleNameScopePiece(MangledName);
    if (Error)
      return nullptr;

    Head->N = Elem;
  }

  QualifiedNameNode *QN = Arena.alloc<QualifiedNameNode>();
  QN->Components = nodeListToNodeArray(Arena, Head, Count);
  return QN;
}

FuncClass Demangler::demangleFunctionClass(std::string_view &MangledName) {
  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case '9':
    return FuncClass(FC_ExternC | FC_NoParameterList);
  case 'A':
    return FC_Private;
  case 'B':
    return FuncClass(FC_Private | FC_Far);
  case 'C':
    return FuncClass(FC_Private | FC_Static);
  case 'D':
    return FuncClass(FC_Private | FC_Static | FC_Far);
  case 'E':
    return FuncClass(FC_Private | FC_Virtual);
  case 'F':
    return FuncClass(FC_Private | FC_Virtual | FC_Far);
  case 'G':
    return FuncClass(FC_Private | FC_StaticThisAdjust);
  case 'H':
    return FuncClass(FC_Private | FC_StaticThisAdjust | FC_Far);
  case 'I':
    return FuncClass(FC_Protected);
  case 'J':
    return FuncClass(FC_Protected | FC_Far);
  case 'K':
    return FuncClass(FC_Protected | FC_Static);
  case 'L':
    return FuncClass(FC_Protected | FC_Static | FC_Far);
  case 'M':
    return FuncClass(FC_Protected | FC_Virtual);
  case 'N':
    return FuncClass(FC_Protected | FC_Virtual | FC_Far);
  case 'O':
    return FuncClass(FC_Protected | FC_Virtual | FC_StaticThisAdjust);
  case 'P':
    return FuncClass(FC_Protected | FC_Virtual | FC_StaticThisAdjust | FC_Far);
  case 'Q':
    return FuncClass(FC_Public);
  case 'R':
    return FuncClass(FC_Public | FC_Far);
  case 'S':
    return FuncClass(FC_Public | FC_Static);
  case 'T':
    return FuncClass(FC_Public | FC_Static | FC_Far);
  case 'U':
    return FuncClass(FC_Public | FC_Virtual);
  case 'V':
    return FuncClass(FC_Public | FC_Virtual | FC_Far);
  case 'W':
    return FuncClass(FC_Public | FC_Virtual | FC_StaticThisAdjust);
  case 'X':
    return FuncClass(FC_Public | FC_Virtual | FC_StaticThisAdjust | FC_Far);
  case 'Y':
    return FuncClass(FC_Global);
  case 'Z':
    return FuncClass(FC_Global | FC_Far);
  case '$': {
    FuncClass VFlag = FC_VirtualThisAdjust;
    if (consumeFront(MangledName, 'R'))
      VFlag = FuncClass(VFlag | FC_VirtualThisAdjustEx);
    if (MangledName.empty())
      break;
    const char F = MangledName.front();
    MangledName.remove_prefix(1);
    switch (F) {
    case '0':
      return FuncClass(FC_Private | FC_Virtual | VFlag);
    case '1':
      return FuncClass(FC_Private | FC_Virtual | VFlag | FC_Far);
    case '2':
      return FuncClass(FC_Protected | FC_Virtual | VFlag);
    case '3':
      return FuncClass(FC_Protected | FC_Virtual | VFlag | FC_Far);
    case '4':
      return FuncClass(FC_Public | FC_Virtual | VFlag);
    case '5':
      return FuncClass(FC_Public | FC_Virtual | VFlag | FC_Far);
    }
  }
  }

  Error = true;
  return FC_Public;
}

CallingConv
Demangler::demangleCallingConvention(std::string_view &MangledName) {
  if (MangledName.empty()) {
    Error = true;
    return CallingConv::None;
  }

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case 'A':
  case 'B':
    return CallingConv::Cdecl;
  case 'C':
  case 'D':
    return CallingConv::Pascal;
  case 'E':
  case 'F':
    return CallingConv::Thiscall;
  case 'G':
  case 'H':
    return CallingConv::Stdcall;
  case 'I':
  case 'J':
    return CallingConv::Fastcall;
  case 'M':
  case 'N':
    return CallingConv::Clrcall;
  case 'O':
  case 'P':
    return CallingConv::Eabi;
  case 'Q':
    return CallingConv::Vectorcall;
  case 'S':
    return CallingConv::Swift;
  case 'W':
    return CallingConv::SwiftAsync;
  }

  return CallingConv::None;
}

StorageClass
Demangler::demangleVariableStorageClass(std::string_view &MangledName) {
  assert(MangledName.front() >= '0' && MangledName.front() <= '4');

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case '0':
    return StorageClass::PrivateStatic;
  case '1':
    return StorageClass::ProtectedStatic;
  case '2':
    return StorageClass::PublicStatic;
  case '3':
    return StorageClass::Global;
  case '4':
    return StorageClass::FunctionLocalStatic;
  }
  DEMANGLE_UNREACHABLE;
}

std::pair<Qualifiers, bool>
Demangler::demangleQualifiers(std::string_view &MangledName) {
  if (MangledName.empty()) {
    Error = true;
    return std::make_pair(Q_None, false);
  }

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  // Member qualifiers
  case 'Q':
    return std::make_pair(Q_None, true);
  case 'R':
    return std::make_pair(Q_Const, true);
  case 'S':
    return std::make_pair(Q_Volatile, true);
  case 'T':
    return std::make_pair(Qualifiers(Q_Const | Q_Volatile), true);
  // Non-Member qualifiers
  case 'A':
    return std::make_pair(Q_None, false);
  case 'B':
    return std::make_pair(Q_Const, false);
  case 'C':
    return std::make_pair(Q_Volatile, false);
  case 'D':
    return std::make_pair(Qualifiers(Q_Const | Q_Volatile), false);
  }
  Error = true;
  return std::make_pair(Q_None, false);
}

// <variable-type> ::= <type> <cvr-qualifiers>
//                 ::= <type> <pointee-cvr-qualifiers> # pointers, references
TypeNode *Demangler::demangleType(std::string_view &MangledName,
                                  QualifierMangleMode QMM) {
  Qualifiers Quals = Q_None;
  bool IsMember = false;
  if (QMM == QualifierMangleMode::Mangle) {
    std::tie(Quals, IsMember) = demangleQualifiers(MangledName);
  } else if (QMM == QualifierMangleMode::Result) {
    if (consumeFront(MangledName, '?'))
      std::tie(Quals, IsMember) = demangleQualifiers(MangledName);
  }

  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }

  TypeNode *Ty = nullptr;
  if (isTagType(MangledName))
    Ty = demangleClassType(MangledName);
  else if (isPointerType(MangledName)) {
    if (isMemberPointer(MangledName, Error))
      Ty = demangleMemberPointerType(MangledName);
    else if (!Error)
      Ty = demanglePointerType(MangledName);
    else
      return nullptr;
  } else if (isArrayType(MangledName))
    Ty = demangleArrayType(MangledName);
  else if (isFunctionType(MangledName)) {
    if (consumeFront(MangledName, "$$A8@@"))
      Ty = demangleFunctionType(MangledName, true);
    else {
      assert(llvm::itanium_demangle::starts_with(MangledName, "$$A6"));
      consumeFront(MangledName, "$$A6");
      Ty = demangleFunctionType(MangledName, false);
    }
  } else if (isCustomType(MangledName)) {
    Ty = demangleCustomType(MangledName);
  } else {
    Ty = demanglePrimitiveType(MangledName);
  }

  if (!Ty || Error)
    return Ty;
  Ty->Quals = Qualifiers(Ty->Quals | Quals);
  return Ty;
}

bool Demangler::demangleThrowSpecification(std::string_view &MangledName) {
  if (consumeFront(MangledName, "_E"))
    return true;
  if (consumeFront(MangledName, 'Z'))
    return false;

  Error = true;
  return false;
}

FunctionSignatureNode *
Demangler::demangleFunctionType(std::string_view &MangledName,
                                bool HasThisQuals) {
  FunctionSignatureNode *FTy = Arena.alloc<FunctionSignatureNode>();

  if (HasThisQuals) {
    FTy->Quals = demanglePointerExtQualifiers(MangledName);
    FTy->RefQualifier = demangleFunctionRefQualifier(MangledName);
    FTy->Quals = Qualifiers(FTy->Quals | demangleQualifiers(MangledName).first);
  }

  // Fields that appear on both member and non-member functions.
  FTy->CallConvention = demangleCallingConvention(MangledName);

  // <return-type> ::= <type>
  //               ::= @ # structors (they have no declared return type)
  bool IsStructor = consumeFront(MangledName, '@');
  if (!IsStructor)
    FTy->ReturnType = demangleType(MangledName, QualifierMangleMode::Result);

  FTy->Params = demangleFunctionParameterList(MangledName, FTy->IsVariadic);

  FTy->IsNoexcept = demangleThrowSpecification(MangledName);

  return FTy;
}

FunctionSymbolNode *
Demangler::demangleFunctionEncoding(std::string_view &MangledName) {
  FuncClass ExtraFlags = FC_None;
  if (consumeFront(MangledName, "$$J0"))
    ExtraFlags = FC_ExternC;

  if (MangledName.empty()) {
    Error = true;
    return nullptr;
  }

  FuncClass FC = demangleFunctionClass(MangledName);
  FC = FuncClass(ExtraFlags | FC);

  FunctionSignatureNode *FSN = nullptr;
  ThunkSignatureNode *TTN = nullptr;
  if (FC & FC_StaticThisAdjust) {
    TTN = Arena.alloc<ThunkSignatureNode>();
    TTN->ThisAdjust.StaticOffset = demangleSigned(MangledName);
  } else if (FC & FC_VirtualThisAdjust) {
    TTN = Arena.alloc<ThunkSignatureNode>();
    if (FC & FC_VirtualThisAdjustEx) {
      TTN->ThisAdjust.VBPtrOffset = demangleSigned(MangledName);
      TTN->ThisAdjust.VBOffsetOffset = demangleSigned(MangledName);
    }
    TTN->ThisAdjust.VtordispOffset = demangleSigned(MangledName);
    TTN->ThisAdjust.StaticOffset = demangleSigned(MangledName);
  }

  if (FC & FC_NoParameterList) {
    // This is an extern "C" function whose full signature hasn't been mangled.
    // This happens when we need to mangle a local symbol inside of an extern
    // "C" function.
    FSN = Arena.alloc<FunctionSignatureNode>();
  } else {
    bool HasThisQuals = !(FC & (FC_Global | FC_Static));
    FSN = demangleFunctionType(MangledName, HasThisQuals);
  }

  if (Error)
    return nullptr;

  if (TTN) {
    *static_cast<FunctionSignatureNode *>(TTN) = *FSN;
    FSN = TTN;
  }
  FSN->FunctionClass = FC;

  FunctionSymbolNode *Symbol = Arena.alloc<FunctionSymbolNode>();
  Symbol->Signature = FSN;
  return Symbol;
}

CustomTypeNode *Demangler::demangleCustomType(std::string_view &MangledName) {
  assert(llvm::itanium_demangle::starts_with(MangledName, '?'));
  MangledName.remove_prefix(1);

  CustomTypeNode *CTN = Arena.alloc<CustomTypeNode>();
  CTN->Identifier = demangleUnqualifiedTypeName(MangledName, /*Memorize=*/true);
  if (!consumeFront(MangledName, '@'))
    Error = true;
  if (Error)
    return nullptr;
  return CTN;
}

// Reads a primitive type.
PrimitiveTypeNode *
Demangler::demanglePrimitiveType(std::string_view &MangledName) {
  if (consumeFront(MangledName, "$$T"))
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Nullptr);

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case 'X':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Void);
  case 'D':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Char);
  case 'C':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Schar);
  case 'E':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Uchar);
  case 'F':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Short);
  case 'G':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Ushort);
  case 'H':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Int);
  case 'I':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Uint);
  case 'J':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Long);
  case 'K':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Ulong);
  case 'M':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Float);
  case 'N':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Double);
  case 'O':
    return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Ldouble);
  case '_': {
    if (MangledName.empty()) {
      Error = true;
      return nullptr;
    }
    const char F = MangledName.front();
    MangledName.remove_prefix(1);
    switch (F) {
    case 'N':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Bool);
    case 'J':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Int64);
    case 'K':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Uint64);
    case 'W':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Wchar);
    case 'Q':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Char8);
    case 'S':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Char16);
    case 'U':
      return Arena.alloc<PrimitiveTypeNode>(PrimitiveKind::Char32);
    }
    break;
  }
  }
  Error = true;
  return nullptr;
}

TagTypeNode *Demangler::demangleClassType(std::string_view &MangledName) {
  TagTypeNode *TT = nullptr;

  const char F = MangledName.front();
  MangledName.remove_prefix(1);
  switch (F) {
  case 'T':
    TT = Arena.alloc<TagTypeNode>(TagKind::Union);
    break;
  case 'U':
    TT = Arena.alloc<TagTypeNode>(TagKind::Struct);
    break;
  case 'V':
    TT = Arena.alloc<TagTypeNode>(TagKind::Class);
    break;
  case 'W':
    if (!consumeFront(MangledName, '4')) {
      Error = true;
      return nullptr;
    }
    TT = Arena.alloc<TagTypeNode>(TagKind::Enum);
    break;
  default:
    assert(false);
  }

  TT->QualifiedName = demangleFullyQualifiedTypeName(MangledName);
  return TT;
}

// <pointer-type> ::= E? <pointer-cvr-qualifiers> <ext-qualifiers> <type>
//                       # the E is required for 64-bit non-static pointers
PointerTypeNode *Demangler::demanglePointerType(std::string_view &MangledName) {
  PointerTypeNode *Pointer = Arena.alloc<PointerTypeNode>();

  std::tie(Pointer->Quals, Pointer->Affinity) =
      demanglePointerCVQualifiers(MangledName);

  if (consumeFront(MangledName, "6")) {
    Pointer->Pointee = demangleFunctionType(MangledName, false);
    return Pointer;
  }

  Qualifiers ExtQuals = demanglePointerExtQualifiers(MangledName);
  Pointer->Quals = Qualifiers(Pointer->Quals | ExtQuals);

  Pointer->Pointee = demangleType(MangledName, QualifierMangleMode::Mangle);
  return Pointer;
}

PointerTypeNode *
Demangler::demangleMemberPointerType(std::string_view &MangledName) {
  PointerTypeNode *Pointer = Arena.alloc<PointerTypeNode>();

  std::tie(Pointer->Quals, Pointer->Affinity) =
      demanglePointerCVQualifiers(MangledName);
  assert(Pointer->Affinity == PointerAffinity::Pointer);

  Qualifiers ExtQuals = demanglePointerExtQualifiers(MangledName);
  Pointer->Quals = Qualifiers(Pointer->Quals | ExtQuals);

  // isMemberPointer() only returns true if there is at least one character
  // after the qualifiers.
  if (consumeFront(MangledName, "8")) {
    Pointer->ClassParent = demangleFullyQualifiedTypeName(MangledName);
    Pointer->Pointee = demangleFunctionType(MangledName, true);
  } else {
    Qualifiers PointeeQuals = Q_None;
    bool IsMember = false;
    std::tie(PointeeQuals, IsMember) = demangleQualifiers(MangledName);
    assert(IsMember || Error);
    Pointer->ClassParent = demangleFullyQualifiedTypeName(MangledName);

    Pointer->Pointee = demangleType(MangledName, QualifierMangleMode::Drop);
    if (Pointer->Pointee)
      Pointer->Pointee->Quals = PointeeQuals;
  }

  return Pointer;
}

Qualifiers
Demangler::demanglePointerExtQualifiers(std::string_view &MangledName) {
  Qualifiers Quals = Q_None;
  if (consumeFront(MangledName, 'E'))
    Quals = Qualifiers(Quals | Q_Pointer64);
  if (consumeFront(MangledName, 'I'))
    Quals = Qualifiers(Quals | Q_Restrict);
  if (consumeFront(MangledName, 'F'))
    Quals = Qualifiers(Quals | Q_Unaligned);

  return Quals;
}

ArrayTypeNode *Demangler::demangleArrayType(std::string_view &MangledName) {
  assert(MangledName.front() == 'Y');
  MangledName.remove_prefix(1);

  uint64_t Rank = 0;
  bool IsNegative = false;
  std::tie(Rank, IsNegative) = demangleNumber(MangledName);
  if (IsNegative || Rank == 0) {
    Error = true;
    return nullptr;
  }

  ArrayTypeNode *ATy = Arena.alloc<ArrayTypeNode>();
  NodeList *Head = Arena.alloc<NodeList>();
  NodeList *Tail = Head;

  for (uint64_t I = 0; I < Rank; ++I) {
    uint64_t D = 0;
    std::tie(D, IsNegative) = demangleNumber(MangledName);
    if (Error || IsNegative) {
      Error = true;
      return nullptr;
    }
    Tail->N = Arena.alloc<IntegerLiteralNode>(D, IsNegative);
    if (I + 1 < Rank) {
      Tail->Next = Arena.alloc<NodeList>();
      Tail = Tail->Next;
    }
  }
  ATy->Dimensions = nodeListToNodeArray(Arena, Head, Rank);

  if (consumeFront(MangledName, "$$C")) {
    bool IsMember = false;
    std::tie(ATy->Quals, IsMember) = demangleQualifiers(MangledName);
    if (IsMember) {
      Error = true;
      return nullptr;
    }
  }

  ATy->ElementType = demangleType(MangledName, QualifierMangleMode::Drop);
  return ATy;
}

// Reads a function's parameters.
NodeArrayNode *
Demangler::demangleFunctionParameterList(std::string_view &MangledName,
                                         bool &IsVariadic) {
  // Empty parameter list.
  if (consumeFront(MangledName, 'X'))
    return nullptr;

  NodeList *Head = Arena.alloc<NodeList>();
  NodeList **Current = &Head;
  size_t Count = 0;
  while (!Error && !llvm::itanium_demangle::starts_with(MangledName, '@') &&
         !llvm::itanium_demangle::starts_with(MangledName, 'Z')) {
    ++Count;

    if (startsWithDigit(MangledName)) {
      size_t N = MangledName[0] - '0';
      if (N >= Backrefs.FunctionParamCount) {
        Error = true;
        return nullptr;
      }
      MangledName.remove_prefix(1);

      *Current = Arena.alloc<NodeList>();
      (*Current)->N = Backrefs.FunctionParams[N];
      Current = &(*Current)->Next;
      continue;
    }

    size_t OldSize = MangledName.size();

    *Current = Arena.alloc<NodeList>();
    TypeNode *TN = demangleType(MangledName, QualifierMangleMode::Drop);
    if (!TN || Error)
      return nullptr;

    (*Current)->N = TN;

    size_t CharsConsumed = OldSize - MangledName.size();
    assert(CharsConsumed != 0);

    // Single-letter types are ignored for backreferences because memorizing
    // them doesn't save anything.
    if (Backrefs.FunctionParamCount <= 9 && CharsConsumed > 1)
      Backrefs.FunctionParams[Backrefs.FunctionParamCount++] = TN;

    Current = &(*Current)->Next;
  }

  if (Error)
    return nullptr;

  NodeArrayNode *NA = nodeListToNodeArray(Arena, Head, Count);
  // A non-empty parameter list is terminated by either 'Z' (variadic) parameter
  // list or '@' (non variadic).  Careful not to consume "@Z", as in that case
  // the following Z could be a throw specifier.
  if (consumeFront(MangledName, '@'))
    return NA;

  if (consumeFront(MangledName, 'Z')) {
    IsVariadic = true;
    return NA;
  }

  DEMANGLE_UNREACHABLE;
}

NodeArrayNode *
Demangler::demangleTemplateParameterList(std::string_view &MangledName) {
  NodeList *Head = nullptr;
  NodeList **Current = &Head;
  size_t Count = 0;

  while (!llvm::itanium_demangle::starts_with(MangledName, '@')) {
    if (consumeFront(MangledName, "$S") || consumeFront(MangledName, "$$V") ||
        consumeFront(MangledName, "$$$V") || consumeFront(MangledName, "$$Z")) {
      // parameter pack separator
      continue;
    }

    ++Count;

    // Template parameter lists don't participate in back-referencing.
    *Current = Arena.alloc<NodeList>();

    NodeList &TP = **Current;

    // <auto-nttp> ::= $ M <type> <nttp>
    const bool IsAutoNTTP = consumeFront(MangledName, "$M");
    if (IsAutoNTTP) {
      // The deduced type of the auto NTTP parameter isn't printed so
      // we want to ignore the AST created from demangling the type.
      //
      // TODO: Avoid the extra allocations to the bump allocator in this case.
      (void)demangleType(MangledName, QualifierMangleMode::Drop);
      if (Error)
        return nullptr;
    }

    TemplateParameterReferenceNode *TPRN = nullptr;
    if (consumeFront(MangledName, "$$Y")) {
      // Template alias
      TP.N = demangleFullyQualifiedTypeName(MangledName);
    } else if (consumeFront(MangledName, "$$B")) {
      // Array
      TP.N = demangleType(MangledName, QualifierMangleMode::Drop);
    } else if (consumeFront(MangledName, "$$C")) {
      // Type has qualifiers.
      TP.N = demangleType(MangledName, QualifierMangleMode::Mangle);
    } else if (startsWith(MangledName, "$1", "1", !IsAutoNTTP) ||
               startsWith(MangledName, "$H", "H", !IsAutoNTTP) ||
               startsWith(MangledName, "$I", "I", !IsAutoNTTP) ||
               startsWith(MangledName, "$J", "J", !IsAutoNTTP)) {
      // Pointer to member
      TP.N = TPRN = Arena.alloc<TemplateParameterReferenceNode>();
      TPRN->IsMemberPointer = true;

      if (!IsAutoNTTP)
        MangledName.remove_prefix(1); // Remove leading '$'

      // 1 - single inheritance       <name>
      // H - multiple inheritance     <name> <number>
      // I - virtual inheritance      <name> <number> <number>
      // J - unspecified inheritance  <name> <number> <number> <number>
      char InheritanceSpecifier = MangledName.front();
      MangledName.remove_prefix(1);
      SymbolNode *S = nullptr;
      if (llvm::itanium_demangle::starts_with(MangledName, '?')) {
        S = parse(MangledName);
        if (Error || !S->Name) {
          Error = true;
          return nullptr;
        }
        memorizeIdentifier(S->Name->getUnqualifiedIdentifier());
      }

      switch (InheritanceSpecifier) {
      case 'J':
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        DEMANGLE_FALLTHROUGH;
      case 'I':
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        DEMANGLE_FALLTHROUGH;
      case 'H':
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        DEMANGLE_FALLTHROUGH;
      case '1':
        break;
      default:
        DEMANGLE_UNREACHABLE;
      }
      TPRN->Affinity = PointerAffinity::Pointer;
      TPRN->Symbol = S;
    } else if (llvm::itanium_demangle::starts_with(MangledName, "$E?")) {
      consumeFront(MangledName, "$E");
      // Reference to symbol
      TP.N = TPRN = Arena.alloc<TemplateParameterReferenceNode>();
      TPRN->Symbol = parse(MangledName);
      TPRN->Affinity = PointerAffinity::Reference;
    } else if (startsWith(MangledName, "$F", "F", !IsAutoNTTP) ||
               startsWith(MangledName, "$G", "G", !IsAutoNTTP)) {
      TP.N = TPRN = Arena.alloc<TemplateParameterReferenceNode>();

      // Data member pointer.
      if (!IsAutoNTTP)
        MangledName.remove_prefix(1); // Remove leading '$'
      char InheritanceSpecifier = MangledName.front();
      MangledName.remove_prefix(1);

      switch (InheritanceSpecifier) {
      case 'G':
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        DEMANGLE_FALLTHROUGH;
      case 'F':
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        TPRN->ThunkOffsets[TPRN->ThunkOffsetCount++] =
            demangleSigned(MangledName);
        break;
      default:
        DEMANGLE_UNREACHABLE;
      }
      TPRN->IsMemberPointer = true;

    } else if (consumeFront(MangledName, "$0", "0", !IsAutoNTTP)) {
      // Integral non-type template parameter
      bool IsNegative = false;
      uint64_t Value = 0;
      std::tie(Value, IsNegative) = demangleNumber(MangledName);

      TP.N = Arena.alloc<IntegerLiteralNode>(Value, IsNegative);
    } else {
      TP.N = demangleType(MangledName, QualifierMangleMode::Drop);
    }
    if (Error)
      return nullptr;

    Current = &TP.Next;
  }

  // The loop above returns nullptr on Error.
  assert(!Error);

  // Template parameter lists cannot be variadic, so it can only be terminated
  // by @ (as opposed to 'Z' in the function parameter case).
  assert(llvm::itanium_demangle::starts_with(
      MangledName, '@')); // The above loop exits only on '@'.
  consumeFront(MangledName, '@');
  return nodeListToNodeArray(Arena, Head, Count);
}

void Demangler::dumpBackReferences() {
  std::printf("%d function parameter backreferences\n",
              (int)Backrefs.FunctionParamCount);

  // Create an output stream so we can render each type.
  OutputBuffer OB;
  for (size_t I = 0; I < Backrefs.FunctionParamCount; ++I) {
    OB.setCurrentPosition(0);

    TypeNode *T = Backrefs.FunctionParams[I];
    T->output(OB, OF_Default);

    std::string_view B = OB;
    std::printf("  [%d] - %.*s\n", (int)I, (int)B.size(), B.data());
  }
  std::free(OB.getBuffer());

  if (Backrefs.FunctionParamCount > 0)
    std::printf("\n");
  std::printf("%d name backreferences\n", (int)Backrefs.NamesCount);
  for (size_t I = 0; I < Backrefs.NamesCount; ++I) {
    std::printf("  [%d] - %.*s\n", (int)I, (int)Backrefs.Names[I]->Name.size(),
                Backrefs.Names[I]->Name.data());
  }
  if (Backrefs.NamesCount > 0)
    std::printf("\n");
}

std::optional<size_t>
llvm::getArm64ECInsertionPointInMangledName(std::string_view MangledName) {
  std::string_view ProcessedName{MangledName};

  // We only support this for MSVC-style C++ symbols.
  if (!consumeFront(ProcessedName, '?'))
    return std::nullopt;

  // The insertion point is just after the name of the symbol, so parse that to
  // remove it from the processed name.
  Demangler D;
  D.demangleFullyQualifiedSymbolName(ProcessedName);
  if (D.Error)
    return std::nullopt;

  return MangledName.length() - ProcessedName.length();
}

char *llvm::microsoftDemangle(std::string_view MangledName, size_t *NMangled,
                              int *Status, MSDemangleFlags Flags) {
  Demangler D;

  std::string_view Name{MangledName};
  SymbolNode *AST = D.parse(Name);
  if (!D.Error && NMangled)
    *NMangled = MangledName.size() - Name.size();

  if (Flags & MSDF_DumpBackrefs)
    D.dumpBackReferences();

  OutputFlags OF = OF_Default;
  if (Flags & MSDF_NoCallingConvention)
    OF = OutputFlags(OF | OF_NoCallingConvention);
  if (Flags & MSDF_NoAccessSpecifier)
    OF = OutputFlags(OF | OF_NoAccessSpecifier);
  if (Flags & MSDF_NoReturnType)
    OF = OutputFlags(OF | OF_NoReturnType);
  if (Flags & MSDF_NoMemberType)
    OF = OutputFlags(OF | OF_NoMemberType);
  if (Flags & MSDF_NoVariableType)
    OF = OutputFlags(OF | OF_NoVariableType);

  int InternalStatus = demangle_success;
  char *Buf;
  if (D.Error)
    InternalStatus = demangle_invalid_mangled_name;
  else {
    OutputBuffer OB;
    AST->output(OB, OF);
    OB += '\0';
    Buf = OB.getBuffer();
  }

  if (Status)
    *Status = InternalStatus;
  return InternalStatus == demangle_success ? Buf : nullptr;
}
