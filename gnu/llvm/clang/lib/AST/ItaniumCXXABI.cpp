//===------- ItaniumCXXABI.cpp - AST support for the Itanium C++ ABI ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides C++ AST support targeting the Itanium C++ ABI, which is
// documented at:
//  http://www.codesourcery.com/public/cxx-abi/abi.html
//  http://www.codesourcery.com/public/cxx-abi/abi-eh.html
//
// It also supports the closely-related ARM C++ ABI, documented at:
// http://infocenter.arm.com/help/topic/com.arm.doc.ihi0041c/IHI0041C_cppabi.pdf
//
//===----------------------------------------------------------------------===//

#include "CXXABI.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Type.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/iterator.h"
#include <optional>

using namespace clang;

namespace {

/// According to Itanium C++ ABI 5.1.2:
/// the name of an anonymous union is considered to be
/// the name of the first named data member found by a pre-order,
/// depth-first, declaration-order walk of the data members of
/// the anonymous union.
/// If there is no such data member (i.e., if all of the data members
/// in the union are unnamed), then there is no way for a program to
/// refer to the anonymous union, and there is therefore no need to mangle its name.
///
/// Returns the name of anonymous union VarDecl or nullptr if it is not found.
static const IdentifierInfo *findAnonymousUnionVarDeclName(const VarDecl& VD) {
  const RecordType *RT = VD.getType()->getAs<RecordType>();
  assert(RT && "type of VarDecl is expected to be RecordType.");
  assert(RT->getDecl()->isUnion() && "RecordType is expected to be a union.");
  if (const FieldDecl *FD = RT->getDecl()->findFirstNamedDataMember()) {
    return FD->getIdentifier();
  }

  return nullptr;
}

/// The name of a decomposition declaration.
struct DecompositionDeclName {
  using BindingArray = ArrayRef<const BindingDecl*>;

  /// Representative example of a set of bindings with these names.
  BindingArray Bindings;

  /// Iterators over the sequence of identifiers in the name.
  struct Iterator
      : llvm::iterator_adaptor_base<Iterator, BindingArray::const_iterator,
                                    std::random_access_iterator_tag,
                                    const IdentifierInfo *> {
    Iterator(BindingArray::const_iterator It) : iterator_adaptor_base(It) {}
    const IdentifierInfo *operator*() const {
      return (*this->I)->getIdentifier();
    }
  };
  Iterator begin() const { return Iterator(Bindings.begin()); }
  Iterator end() const { return Iterator(Bindings.end()); }
};
}

namespace llvm {
template<typename T> bool isDenseMapKeyEmpty(T V) {
  return llvm::DenseMapInfo<T>::isEqual(
      V, llvm::DenseMapInfo<T>::getEmptyKey());
}
template<typename T> bool isDenseMapKeyTombstone(T V) {
  return llvm::DenseMapInfo<T>::isEqual(
      V, llvm::DenseMapInfo<T>::getTombstoneKey());
}

template <typename T>
std::optional<bool> areDenseMapKeysEqualSpecialValues(T LHS, T RHS) {
  bool LHSEmpty = isDenseMapKeyEmpty(LHS);
  bool RHSEmpty = isDenseMapKeyEmpty(RHS);
  if (LHSEmpty || RHSEmpty)
    return LHSEmpty && RHSEmpty;

  bool LHSTombstone = isDenseMapKeyTombstone(LHS);
  bool RHSTombstone = isDenseMapKeyTombstone(RHS);
  if (LHSTombstone || RHSTombstone)
    return LHSTombstone && RHSTombstone;

  return std::nullopt;
}

template<>
struct DenseMapInfo<DecompositionDeclName> {
  using ArrayInfo = llvm::DenseMapInfo<ArrayRef<const BindingDecl*>>;
  static DecompositionDeclName getEmptyKey() {
    return {ArrayInfo::getEmptyKey()};
  }
  static DecompositionDeclName getTombstoneKey() {
    return {ArrayInfo::getTombstoneKey()};
  }
  static unsigned getHashValue(DecompositionDeclName Key) {
    assert(!isEqual(Key, getEmptyKey()) && !isEqual(Key, getTombstoneKey()));
    return llvm::hash_combine_range(Key.begin(), Key.end());
  }
  static bool isEqual(DecompositionDeclName LHS, DecompositionDeclName RHS) {
    if (std::optional<bool> Result =
            areDenseMapKeysEqualSpecialValues(LHS.Bindings, RHS.Bindings))
      return *Result;

    return LHS.Bindings.size() == RHS.Bindings.size() &&
           std::equal(LHS.begin(), LHS.end(), RHS.begin());
  }
};
}

namespace {

/// Keeps track of the mangled names of lambda expressions and block
/// literals within a particular context.
class ItaniumNumberingContext : public MangleNumberingContext {
  ItaniumMangleContext *Mangler;
  llvm::StringMap<unsigned> LambdaManglingNumbers;
  unsigned BlockManglingNumber = 0;
  llvm::DenseMap<const IdentifierInfo *, unsigned> VarManglingNumbers;
  llvm::DenseMap<const IdentifierInfo *, unsigned> TagManglingNumbers;
  llvm::DenseMap<DecompositionDeclName, unsigned>
      DecompsitionDeclManglingNumbers;

public:
  ItaniumNumberingContext(ItaniumMangleContext *Mangler) : Mangler(Mangler) {}

  unsigned getManglingNumber(const CXXMethodDecl *CallOperator) override {
    const CXXRecordDecl *Lambda = CallOperator->getParent();
    assert(Lambda->isLambda());

    // Computation of the <lambda-sig> is non-trivial and subtle. Rather than
    // duplicating it here, just mangle the <lambda-sig> directly.
    llvm::SmallString<128> LambdaSig;
    llvm::raw_svector_ostream Out(LambdaSig);
    Mangler->mangleLambdaSig(Lambda, Out);

    return ++LambdaManglingNumbers[LambdaSig];
  }

  unsigned getManglingNumber(const BlockDecl *BD) override {
    return ++BlockManglingNumber;
  }

  unsigned getStaticLocalNumber(const VarDecl *VD) override {
    return 0;
  }

  /// Variable decls are numbered by identifier.
  unsigned getManglingNumber(const VarDecl *VD, unsigned) override {
    if (auto *DD = dyn_cast<DecompositionDecl>(VD)) {
      DecompositionDeclName Name{DD->bindings()};
      return ++DecompsitionDeclManglingNumbers[Name];
    }

    const IdentifierInfo *Identifier = VD->getIdentifier();
    if (!Identifier) {
      // VarDecl without an identifier represents an anonymous union
      // declaration.
      Identifier = findAnonymousUnionVarDeclName(*VD);
    }
    return ++VarManglingNumbers[Identifier];
  }

  unsigned getManglingNumber(const TagDecl *TD, unsigned) override {
    return ++TagManglingNumbers[TD->getIdentifier()];
  }
};

// A version of this for SYCL that makes sure that 'device' mangling context
// matches the lambda mangling number, so that __builtin_sycl_unique_stable_name
// can be consistently generated between a MS and Itanium host by just referring
// to the device mangling number.
class ItaniumSYCLNumberingContext : public ItaniumNumberingContext {
  llvm::DenseMap<const CXXMethodDecl *, unsigned> ManglingNumbers;
  using ManglingItr = decltype(ManglingNumbers)::iterator;

public:
  ItaniumSYCLNumberingContext(ItaniumMangleContext *Mangler)
      : ItaniumNumberingContext(Mangler) {}

  unsigned getManglingNumber(const CXXMethodDecl *CallOperator) override {
    unsigned Number = ItaniumNumberingContext::getManglingNumber(CallOperator);
    std::pair<ManglingItr, bool> emplace_result =
        ManglingNumbers.try_emplace(CallOperator, Number);
    (void)emplace_result;
    assert(emplace_result.second && "Lambda number set multiple times?");
    return Number;
  }

  using ItaniumNumberingContext::getManglingNumber;

  unsigned getDeviceManglingNumber(const CXXMethodDecl *CallOperator) override {
    ManglingItr Itr = ManglingNumbers.find(CallOperator);
    assert(Itr != ManglingNumbers.end() && "Lambda not yet mangled?");

    return Itr->second;
  }
};

class ItaniumCXXABI : public CXXABI {
private:
  std::unique_ptr<MangleContext> Mangler;
protected:
  ASTContext &Context;
public:
  ItaniumCXXABI(ASTContext &Ctx)
      : Mangler(Ctx.createMangleContext()), Context(Ctx) {}

  MemberPointerInfo
  getMemberPointerInfo(const MemberPointerType *MPT) const override {
    const TargetInfo &Target = Context.getTargetInfo();
    TargetInfo::IntType PtrDiff = Target.getPtrDiffType(LangAS::Default);
    MemberPointerInfo MPI;
    MPI.Width = Target.getTypeWidth(PtrDiff);
    MPI.Align = Target.getTypeAlign(PtrDiff);
    MPI.HasPadding = false;
    if (MPT->isMemberFunctionPointer())
      MPI.Width *= 2;
    return MPI;
  }

  CallingConv getDefaultMethodCallConv(bool isVariadic) const override {
    const llvm::Triple &T = Context.getTargetInfo().getTriple();
    if (!isVariadic && T.isWindowsGNUEnvironment() &&
        T.getArch() == llvm::Triple::x86)
      return CC_X86ThisCall;
    return Context.getTargetInfo().getDefaultCallingConv();
  }

  // We cheat and just check that the class has a vtable pointer, and that it's
  // only big enough to have a vtable pointer and nothing more (or less).
  bool isNearlyEmpty(const CXXRecordDecl *RD) const override {

    // Check that the class has a vtable pointer.
    if (!RD->isDynamicClass())
      return false;

    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);
    CharUnits PointerSize = Context.toCharUnitsFromBits(
        Context.getTargetInfo().getPointerWidth(LangAS::Default));
    return Layout.getNonVirtualSize() == PointerSize;
  }

  const CXXConstructorDecl *
  getCopyConstructorForExceptionObject(CXXRecordDecl *RD) override {
    return nullptr;
  }

  void addCopyConstructorForExceptionObject(CXXRecordDecl *RD,
                                            CXXConstructorDecl *CD) override {}

  void addTypedefNameForUnnamedTagDecl(TagDecl *TD,
                                       TypedefNameDecl *DD) override {}

  TypedefNameDecl *getTypedefNameForUnnamedTagDecl(const TagDecl *TD) override {
    return nullptr;
  }

  void addDeclaratorForUnnamedTagDecl(TagDecl *TD,
                                      DeclaratorDecl *DD) override {}

  DeclaratorDecl *getDeclaratorForUnnamedTagDecl(const TagDecl *TD) override {
    return nullptr;
  }

  std::unique_ptr<MangleNumberingContext>
  createMangleNumberingContext() const override {
    if (Context.getLangOpts().isSYCL())
      return std::make_unique<ItaniumSYCLNumberingContext>(
          cast<ItaniumMangleContext>(Mangler.get()));
    return std::make_unique<ItaniumNumberingContext>(
        cast<ItaniumMangleContext>(Mangler.get()));
  }
};
}

CXXABI *clang::CreateItaniumCXXABI(ASTContext &Ctx) {
  return new ItaniumCXXABI(Ctx);
}

std::unique_ptr<MangleNumberingContext>
clang::createItaniumNumberingContext(MangleContext *Mangler) {
  return std::make_unique<ItaniumNumberingContext>(
      cast<ItaniumMangleContext>(Mangler));
}
