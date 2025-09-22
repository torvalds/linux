//===------------------------- ItaniumDemangle.cpp ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// FIXME: (possibly) incomplete list of features that clang mangles that this
// file does not yet support:
//   - C++ modules TS

#include "llvm/Demangle/Demangle.h"
#include "llvm/Demangle/ItaniumDemangle.h"

#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <utility>

using namespace llvm;
using namespace llvm::itanium_demangle;

constexpr const char *itanium_demangle::FloatData<float>::spec;
constexpr const char *itanium_demangle::FloatData<double>::spec;
constexpr const char *itanium_demangle::FloatData<long double>::spec;

// <discriminator> := _ <non-negative number>      # when number < 10
//                 := __ <non-negative number> _   # when number >= 10
//  extension      := decimal-digit+               # at the end of string
const char *itanium_demangle::parse_discriminator(const char *first,
                                                  const char *last) {
  // parse but ignore discriminator
  if (first != last) {
    if (*first == '_') {
      const char *t1 = first + 1;
      if (t1 != last) {
        if (std::isdigit(*t1))
          first = t1 + 1;
        else if (*t1 == '_') {
          for (++t1; t1 != last && std::isdigit(*t1); ++t1)
            ;
          if (t1 != last && *t1 == '_')
            first = t1 + 1;
        }
      }
    } else if (std::isdigit(*first)) {
      const char *t1 = first + 1;
      for (; t1 != last && std::isdigit(*t1); ++t1)
        ;
      if (t1 == last)
        first = last;
    }
  }
  return first;
}

#ifndef NDEBUG
namespace {
struct DumpVisitor {
  unsigned Depth = 0;
  bool PendingNewline = false;

  template<typename NodeT> static constexpr bool wantsNewline(const NodeT *) {
    return true;
  }
  static bool wantsNewline(NodeArray A) { return !A.empty(); }
  static constexpr bool wantsNewline(...) { return false; }

  template<typename ...Ts> static bool anyWantNewline(Ts ...Vs) {
    for (bool B : {wantsNewline(Vs)...})
      if (B)
        return true;
    return false;
  }

  void printStr(const char *S) { fprintf(stderr, "%s", S); }
  void print(std::string_view SV) {
    fprintf(stderr, "\"%.*s\"", (int)SV.size(), SV.data());
  }
  void print(const Node *N) {
    if (N)
      N->visit(std::ref(*this));
    else
      printStr("<null>");
  }
  void print(NodeArray A) {
    ++Depth;
    printStr("{");
    bool First = true;
    for (const Node *N : A) {
      if (First)
        print(N);
      else
        printWithComma(N);
      First = false;
    }
    printStr("}");
    --Depth;
  }

  // Overload used when T is exactly 'bool', not merely convertible to 'bool'.
  void print(bool B) { printStr(B ? "true" : "false"); }

  template <class T> std::enable_if_t<std::is_unsigned<T>::value> print(T N) {
    fprintf(stderr, "%llu", (unsigned long long)N);
  }

  template <class T> std::enable_if_t<std::is_signed<T>::value> print(T N) {
    fprintf(stderr, "%lld", (long long)N);
  }

  void print(ReferenceKind RK) {
    switch (RK) {
    case ReferenceKind::LValue:
      return printStr("ReferenceKind::LValue");
    case ReferenceKind::RValue:
      return printStr("ReferenceKind::RValue");
    }
  }
  void print(FunctionRefQual RQ) {
    switch (RQ) {
    case FunctionRefQual::FrefQualNone:
      return printStr("FunctionRefQual::FrefQualNone");
    case FunctionRefQual::FrefQualLValue:
      return printStr("FunctionRefQual::FrefQualLValue");
    case FunctionRefQual::FrefQualRValue:
      return printStr("FunctionRefQual::FrefQualRValue");
    }
  }
  void print(Qualifiers Qs) {
    if (!Qs) return printStr("QualNone");
    struct QualName { Qualifiers Q; const char *Name; } Names[] = {
      {QualConst, "QualConst"},
      {QualVolatile, "QualVolatile"},
      {QualRestrict, "QualRestrict"},
    };
    for (QualName Name : Names) {
      if (Qs & Name.Q) {
        printStr(Name.Name);
        Qs = Qualifiers(Qs & ~Name.Q);
        if (Qs) printStr(" | ");
      }
    }
  }
  void print(SpecialSubKind SSK) {
    switch (SSK) {
    case SpecialSubKind::allocator:
      return printStr("SpecialSubKind::allocator");
    case SpecialSubKind::basic_string:
      return printStr("SpecialSubKind::basic_string");
    case SpecialSubKind::string:
      return printStr("SpecialSubKind::string");
    case SpecialSubKind::istream:
      return printStr("SpecialSubKind::istream");
    case SpecialSubKind::ostream:
      return printStr("SpecialSubKind::ostream");
    case SpecialSubKind::iostream:
      return printStr("SpecialSubKind::iostream");
    }
  }
  void print(TemplateParamKind TPK) {
    switch (TPK) {
    case TemplateParamKind::Type:
      return printStr("TemplateParamKind::Type");
    case TemplateParamKind::NonType:
      return printStr("TemplateParamKind::NonType");
    case TemplateParamKind::Template:
      return printStr("TemplateParamKind::Template");
    }
  }
  void print(Node::Prec P) {
    switch (P) {
    case Node::Prec::Primary:
      return printStr("Node::Prec::Primary");
    case Node::Prec::Postfix:
      return printStr("Node::Prec::Postfix");
    case Node::Prec::Unary:
      return printStr("Node::Prec::Unary");
    case Node::Prec::Cast:
      return printStr("Node::Prec::Cast");
    case Node::Prec::PtrMem:
      return printStr("Node::Prec::PtrMem");
    case Node::Prec::Multiplicative:
      return printStr("Node::Prec::Multiplicative");
    case Node::Prec::Additive:
      return printStr("Node::Prec::Additive");
    case Node::Prec::Shift:
      return printStr("Node::Prec::Shift");
    case Node::Prec::Spaceship:
      return printStr("Node::Prec::Spaceship");
    case Node::Prec::Relational:
      return printStr("Node::Prec::Relational");
    case Node::Prec::Equality:
      return printStr("Node::Prec::Equality");
    case Node::Prec::And:
      return printStr("Node::Prec::And");
    case Node::Prec::Xor:
      return printStr("Node::Prec::Xor");
    case Node::Prec::Ior:
      return printStr("Node::Prec::Ior");
    case Node::Prec::AndIf:
      return printStr("Node::Prec::AndIf");
    case Node::Prec::OrIf:
      return printStr("Node::Prec::OrIf");
    case Node::Prec::Conditional:
      return printStr("Node::Prec::Conditional");
    case Node::Prec::Assign:
      return printStr("Node::Prec::Assign");
    case Node::Prec::Comma:
      return printStr("Node::Prec::Comma");
    case Node::Prec::Default:
      return printStr("Node::Prec::Default");
    }
  }

  void newLine() {
    printStr("\n");
    for (unsigned I = 0; I != Depth; ++I)
      printStr(" ");
    PendingNewline = false;
  }

  template<typename T> void printWithPendingNewline(T V) {
    print(V);
    if (wantsNewline(V))
      PendingNewline = true;
  }

  template<typename T> void printWithComma(T V) {
    if (PendingNewline || wantsNewline(V)) {
      printStr(",");
      newLine();
    } else {
      printStr(", ");
    }

    printWithPendingNewline(V);
  }

  struct CtorArgPrinter {
    DumpVisitor &Visitor;

    template<typename T, typename ...Rest> void operator()(T V, Rest ...Vs) {
      if (Visitor.anyWantNewline(V, Vs...))
        Visitor.newLine();
      Visitor.printWithPendingNewline(V);
      int PrintInOrder[] = { (Visitor.printWithComma(Vs), 0)..., 0 };
      (void)PrintInOrder;
    }
  };

  template<typename NodeT> void operator()(const NodeT *Node) {
    Depth += 2;
    fprintf(stderr, "%s(", itanium_demangle::NodeKind<NodeT>::name());
    Node->match(CtorArgPrinter{*this});
    fprintf(stderr, ")");
    Depth -= 2;
  }

  void operator()(const ForwardTemplateReference *Node) {
    Depth += 2;
    fprintf(stderr, "ForwardTemplateReference(");
    if (Node->Ref && !Node->Printing) {
      Node->Printing = true;
      CtorArgPrinter{*this}(Node->Ref);
      Node->Printing = false;
    } else {
      CtorArgPrinter{*this}(Node->Index);
    }
    fprintf(stderr, ")");
    Depth -= 2;
  }
};
}

void itanium_demangle::Node::dump() const {
  DumpVisitor V;
  visit(std::ref(V));
  V.newLine();
}
#endif

namespace {
class BumpPointerAllocator {
  struct BlockMeta {
    BlockMeta* Next;
    size_t Current;
  };

  static constexpr size_t AllocSize = 4096;
  static constexpr size_t UsableAllocSize = AllocSize - sizeof(BlockMeta);

  alignas(long double) char InitialBuffer[AllocSize];
  BlockMeta* BlockList = nullptr;

  void grow() {
    char* NewMeta = static_cast<char *>(std::malloc(AllocSize));
    if (NewMeta == nullptr)
      std::terminate();
    BlockList = new (NewMeta) BlockMeta{BlockList, 0};
  }

  void* allocateMassive(size_t NBytes) {
    NBytes += sizeof(BlockMeta);
    BlockMeta* NewMeta = reinterpret_cast<BlockMeta*>(std::malloc(NBytes));
    if (NewMeta == nullptr)
      std::terminate();
    BlockList->Next = new (NewMeta) BlockMeta{BlockList->Next, 0};
    return static_cast<void*>(NewMeta + 1);
  }

public:
  BumpPointerAllocator()
      : BlockList(new (InitialBuffer) BlockMeta{nullptr, 0}) {}

  void* allocate(size_t N) {
    N = (N + 15u) & ~15u;
    if (N + BlockList->Current >= UsableAllocSize) {
      if (N > UsableAllocSize)
        return allocateMassive(N);
      grow();
    }
    BlockList->Current += N;
    return static_cast<void*>(reinterpret_cast<char*>(BlockList + 1) +
                              BlockList->Current - N);
  }

  void reset() {
    while (BlockList) {
      BlockMeta* Tmp = BlockList;
      BlockList = BlockList->Next;
      if (reinterpret_cast<char*>(Tmp) != InitialBuffer)
        std::free(Tmp);
    }
    BlockList = new (InitialBuffer) BlockMeta{nullptr, 0};
  }

  ~BumpPointerAllocator() { reset(); }
};

class DefaultAllocator {
  BumpPointerAllocator Alloc;

public:
  void reset() { Alloc.reset(); }

  template<typename T, typename ...Args> T *makeNode(Args &&...args) {
    return new (Alloc.allocate(sizeof(T)))
        T(std::forward<Args>(args)...);
  }

  void *allocateNodeArray(size_t sz) {
    return Alloc.allocate(sizeof(Node *) * sz);
  }
};
}  // unnamed namespace

//===----------------------------------------------------------------------===//
// Code beyond this point should not be synchronized with libc++abi.
//===----------------------------------------------------------------------===//

using Demangler = itanium_demangle::ManglingParser<DefaultAllocator>;

char *llvm::itaniumDemangle(std::string_view MangledName, bool ParseParams) {
  if (MangledName.empty())
    return nullptr;

  Demangler Parser(MangledName.data(),
                   MangledName.data() + MangledName.length());
  Node *AST = Parser.parse(ParseParams);
  if (!AST)
    return nullptr;

  OutputBuffer OB;
  assert(Parser.ForwardTemplateRefs.empty());
  AST->print(OB);
  OB += '\0';
  return OB.getBuffer();
}

ItaniumPartialDemangler::ItaniumPartialDemangler()
    : RootNode(nullptr), Context(new Demangler{nullptr, nullptr}) {}

ItaniumPartialDemangler::~ItaniumPartialDemangler() {
  delete static_cast<Demangler *>(Context);
}

ItaniumPartialDemangler::ItaniumPartialDemangler(
    ItaniumPartialDemangler &&Other)
    : RootNode(Other.RootNode), Context(Other.Context) {
  Other.Context = Other.RootNode = nullptr;
}

ItaniumPartialDemangler &ItaniumPartialDemangler::
operator=(ItaniumPartialDemangler &&Other) {
  std::swap(RootNode, Other.RootNode);
  std::swap(Context, Other.Context);
  return *this;
}

// Demangle MangledName into an AST, storing it into this->RootNode.
bool ItaniumPartialDemangler::partialDemangle(const char *MangledName) {
  Demangler *Parser = static_cast<Demangler *>(Context);
  size_t Len = std::strlen(MangledName);
  Parser->reset(MangledName, MangledName + Len);
  RootNode = Parser->parse();
  return RootNode == nullptr;
}

static char *printNode(const Node *RootNode, char *Buf, size_t *N) {
  OutputBuffer OB(Buf, N);
  RootNode->print(OB);
  OB += '\0';
  if (N != nullptr)
    *N = OB.getCurrentPosition();
  return OB.getBuffer();
}

char *ItaniumPartialDemangler::getFunctionBaseName(char *Buf, size_t *N) const {
  if (!isFunction())
    return nullptr;

  const Node *Name = static_cast<const FunctionEncoding *>(RootNode)->getName();

  while (true) {
    switch (Name->getKind()) {
    case Node::KAbiTagAttr:
      Name = static_cast<const AbiTagAttr *>(Name)->Base;
      continue;
    case Node::KModuleEntity:
      Name = static_cast<const ModuleEntity *>(Name)->Name;
      continue;
    case Node::KNestedName:
      Name = static_cast<const NestedName *>(Name)->Name;
      continue;
    case Node::KLocalName:
      Name = static_cast<const LocalName *>(Name)->Entity;
      continue;
    case Node::KNameWithTemplateArgs:
      Name = static_cast<const NameWithTemplateArgs *>(Name)->Name;
      continue;
    default:
      return printNode(Name, Buf, N);
    }
  }
}

char *ItaniumPartialDemangler::getFunctionDeclContextName(char *Buf,
                                                          size_t *N) const {
  if (!isFunction())
    return nullptr;
  const Node *Name = static_cast<const FunctionEncoding *>(RootNode)->getName();

  OutputBuffer OB(Buf, N);

 KeepGoingLocalFunction:
  while (true) {
    if (Name->getKind() == Node::KAbiTagAttr) {
      Name = static_cast<const AbiTagAttr *>(Name)->Base;
      continue;
    }
    if (Name->getKind() == Node::KNameWithTemplateArgs) {
      Name = static_cast<const NameWithTemplateArgs *>(Name)->Name;
      continue;
    }
    break;
  }

  if (Name->getKind() == Node::KModuleEntity)
    Name = static_cast<const ModuleEntity *>(Name)->Name;

  switch (Name->getKind()) {
  case Node::KNestedName:
    static_cast<const NestedName *>(Name)->Qual->print(OB);
    break;
  case Node::KLocalName: {
    auto *LN = static_cast<const LocalName *>(Name);
    LN->Encoding->print(OB);
    OB += "::";
    Name = LN->Entity;
    goto KeepGoingLocalFunction;
  }
  default:
    break;
  }
  OB += '\0';
  if (N != nullptr)
    *N = OB.getCurrentPosition();
  return OB.getBuffer();
}

char *ItaniumPartialDemangler::getFunctionName(char *Buf, size_t *N) const {
  if (!isFunction())
    return nullptr;
  auto *Name = static_cast<FunctionEncoding *>(RootNode)->getName();
  return printNode(Name, Buf, N);
}

char *ItaniumPartialDemangler::getFunctionParameters(char *Buf,
                                                     size_t *N) const {
  if (!isFunction())
    return nullptr;
  NodeArray Params = static_cast<FunctionEncoding *>(RootNode)->getParams();

  OutputBuffer OB(Buf, N);

  OB += '(';
  Params.printWithComma(OB);
  OB += ')';
  OB += '\0';
  if (N != nullptr)
    *N = OB.getCurrentPosition();
  return OB.getBuffer();
}

char *ItaniumPartialDemangler::getFunctionReturnType(
    char *Buf, size_t *N) const {
  if (!isFunction())
    return nullptr;

  OutputBuffer OB(Buf, N);

  if (const Node *Ret =
          static_cast<const FunctionEncoding *>(RootNode)->getReturnType())
    Ret->print(OB);

  OB += '\0';
  if (N != nullptr)
    *N = OB.getCurrentPosition();
  return OB.getBuffer();
}

char *ItaniumPartialDemangler::finishDemangle(char *Buf, size_t *N) const {
  assert(RootNode != nullptr && "must call partialDemangle()");
  return printNode(static_cast<Node *>(RootNode), Buf, N);
}

bool ItaniumPartialDemangler::hasFunctionQualifiers() const {
  assert(RootNode != nullptr && "must call partialDemangle()");
  if (!isFunction())
    return false;
  auto *E = static_cast<const FunctionEncoding *>(RootNode);
  return E->getCVQuals() != QualNone || E->getRefQual() != FrefQualNone;
}

bool ItaniumPartialDemangler::isCtorOrDtor() const {
  const Node *N = static_cast<const Node *>(RootNode);
  while (N) {
    switch (N->getKind()) {
    default:
      return false;
    case Node::KCtorDtorName:
      return true;

    case Node::KAbiTagAttr:
      N = static_cast<const AbiTagAttr *>(N)->Base;
      break;
    case Node::KFunctionEncoding:
      N = static_cast<const FunctionEncoding *>(N)->getName();
      break;
    case Node::KLocalName:
      N = static_cast<const LocalName *>(N)->Entity;
      break;
    case Node::KNameWithTemplateArgs:
      N = static_cast<const NameWithTemplateArgs *>(N)->Name;
      break;
    case Node::KNestedName:
      N = static_cast<const NestedName *>(N)->Name;
      break;
    case Node::KModuleEntity:
      N = static_cast<const ModuleEntity *>(N)->Name;
      break;
    }
  }
  return false;
}

bool ItaniumPartialDemangler::isFunction() const {
  assert(RootNode != nullptr && "must call partialDemangle()");
  return static_cast<const Node *>(RootNode)->getKind() ==
         Node::KFunctionEncoding;
}

bool ItaniumPartialDemangler::isSpecialName() const {
  assert(RootNode != nullptr && "must call partialDemangle()");
  auto K = static_cast<const Node *>(RootNode)->getKind();
  return K == Node::KSpecialName || K == Node::KCtorVtableSpecialName;
}

bool ItaniumPartialDemangler::isData() const {
  return !isFunction() && !isSpecialName();
}
