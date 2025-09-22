//===- ExtractAPI/DeclarationFragments.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the Declaration Fragments related classes.
///
/// Declaration Fragments represent parts of a symbol declaration tagged with
/// syntactic/semantic information.
/// See https://github.com/apple/swift-docc-symbolkit
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_EXTRACTAPI_DECLARATION_FRAGMENTS_H
#define LLVM_CLANG_EXTRACTAPI_DECLARATION_FRAGMENTS_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/TypeLoc.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Lex/MacroInfo.h"
#include <iterator>
#include <utility>
#include <vector>

namespace clang {
namespace extractapi {

/// DeclarationFragments is a vector of tagged important parts of a symbol's
/// declaration.
///
/// The fragments sequence can be joined to form spans of declaration text, with
/// attached information useful for purposes like syntax-highlighting etc.
/// For example:
/// \code
///   const -> keyword    "const"
///   int   -> type       "int"
///   pi;   -> identifier "pi"
/// \endcode
class DeclarationFragments {
public:
  DeclarationFragments() = default;

  /// The kind of a fragment.
  enum class FragmentKind {
    /// Unknown fragment kind.
    None,

    Keyword,
    Attribute,
    NumberLiteral,
    StringLiteral,
    Identifier,

    /// Identifier that refers to a type in the context.
    TypeIdentifier,

    /// Parameter that's used as generics in the context. For example template
    /// parameters.
    GenericParameter,

    /// External parameters in Objective-C methods.
    /// For example, \c forKey in
    /// \code{.m}
    ///   - (void) setValue:(Value)value forKey(Key)key
    /// \endcode
    ExternalParam,

    /// Internal/local parameters in Objective-C methods.
    /// For example, \c key in
    /// \code{.m}
    ///   - (void) setValue:(Value)value forKey(Key)key
    /// \endcode
    InternalParam,

    Text,
  };

  /// Fragment holds information of a single fragment.
  struct Fragment {
    std::string Spelling;
    FragmentKind Kind;

    /// The USR of the fragment symbol, if applicable.
    std::string PreciseIdentifier;

    /// The associated declaration, if applicable. This is not intended to be
    /// used outside of libclang.
    const Decl *Declaration;

    Fragment(StringRef Spelling, FragmentKind Kind, StringRef PreciseIdentifier,
             const Decl *Declaration)
        : Spelling(Spelling), Kind(Kind), PreciseIdentifier(PreciseIdentifier),
          Declaration(Declaration) {}
  };

  using FragmentIterator = std::vector<Fragment>::iterator;
  using ConstFragmentIterator = std::vector<Fragment>::const_iterator;

  const std::vector<Fragment> &getFragments() const { return Fragments; }

  FragmentIterator begin() { return Fragments.begin(); }

  FragmentIterator end() { return Fragments.end(); }

  ConstFragmentIterator cbegin() const { return Fragments.cbegin(); }

  ConstFragmentIterator cend() const { return Fragments.cend(); }

  /// Prepend another DeclarationFragments to the beginning.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &prepend(DeclarationFragments Other) {
    return insert(begin(), std::move(Other));
  }

  /// Append another DeclarationFragments to the end.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &append(DeclarationFragments Other) {
    return insert(end(), std::move(Other));
  }

  /// Append a new Fragment to the end of the Fragments.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &append(StringRef Spelling, FragmentKind Kind,
                               StringRef PreciseIdentifier = "",
                               const Decl *Declaration = nullptr) {
    if (Kind == FragmentKind::Text && !Fragments.empty() &&
        Fragments.back().Kind == FragmentKind::Text) {
      // If appending a text fragment, and the last fragment is also text,
      // merge into the last fragment.
      Fragments.back().Spelling.append(Spelling.data(), Spelling.size());
    } else {
      Fragments.emplace_back(Spelling, Kind, PreciseIdentifier, Declaration);
    }
    return *this;
  }

  /// Inserts another DeclarationFragments at \p It.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &insert(FragmentIterator It,
                               DeclarationFragments Other) {
    if (Other.Fragments.empty())
      return *this;

    if (Fragments.empty()) {
      Fragments = std::move(Other.Fragments);
      return *this;
    }

    const auto &OtherFrags = Other.Fragments;
    auto ToInsertBegin = std::make_move_iterator(Other.begin());
    auto ToInsertEnd = std::make_move_iterator(Other.end());

    // If we aren't inserting at the end let's make sure that we merge their
    // last fragment with It if both are text fragments.
    if (It != end() && It->Kind == FragmentKind::Text &&
        OtherFrags.back().Kind == FragmentKind::Text) {
      auto &TheirBackSpelling = OtherFrags.back().Spelling;
      It->Spelling.reserve(It->Spelling.size() + TheirBackSpelling.size());
      It->Spelling.insert(It->Spelling.begin(), TheirBackSpelling.begin(),
                          TheirBackSpelling.end());
      --ToInsertEnd;
    }

    // If we aren't inserting at the beginning we want to merge their first
    // fragment with the fragment before It if both are text fragments.
    if (It != begin() && std::prev(It)->Kind == FragmentKind::Text &&
        OtherFrags.front().Kind == FragmentKind::Text) {
      auto PrevIt = std::prev(It);
      auto &TheirFrontSpelling = OtherFrags.front().Spelling;
      PrevIt->Spelling.reserve(PrevIt->Spelling.size() +
                               TheirFrontSpelling.size());
      PrevIt->Spelling.append(TheirFrontSpelling);
      ++ToInsertBegin;
    }

    Fragments.insert(It, ToInsertBegin, ToInsertEnd);
    return *this;
  }

  DeclarationFragments &pop_back() {
    Fragments.pop_back();
    return *this;
  }

  DeclarationFragments &replace(std::string NewSpelling, unsigned Position) {
    Fragments.at(Position).Spelling = NewSpelling;
    return *this;
  }

  /// Append a text Fragment of a space character.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &appendSpace();

  /// Append a text Fragment of a semicolon character.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// appending to chain up consecutive operations.
  DeclarationFragments &appendSemicolon();

  /// Removes a trailing semicolon character if present.
  ///
  /// \returns a reference to the DeclarationFragments object itself after
  /// removing to chain up consecutive operations.
  DeclarationFragments &removeTrailingSemicolon();

  /// Get the string description of a FragmentKind \p Kind.
  static StringRef getFragmentKindString(FragmentKind Kind);

  /// Get the corresponding FragmentKind from string \p S.
  static FragmentKind parseFragmentKindFromString(StringRef S);

  static DeclarationFragments
  getExceptionSpecificationString(ExceptionSpecificationType ExceptionSpec);

  static DeclarationFragments getStructureTypeFragment(const RecordDecl *Decl);

private:
  DeclarationFragments &appendUnduplicatedTextCharacter(char Character);
  std::vector<Fragment> Fragments;
};

class AccessControl {
public:
  AccessControl(std::string Access) : Access(Access) {}
  AccessControl() : Access("public") {}

  const std::string &getAccess() const { return Access; }

  bool empty() const { return Access.empty(); }

private:
  std::string Access;
};

/// Store function signature information with DeclarationFragments of the
/// return type and parameters.
class FunctionSignature {
public:
  FunctionSignature() = default;

  /// Parameter holds the name and DeclarationFragments of a single parameter.
  struct Parameter {
    std::string Name;
    DeclarationFragments Fragments;

    Parameter(StringRef Name, DeclarationFragments Fragments)
        : Name(Name), Fragments(Fragments) {}
  };

  const std::vector<Parameter> &getParameters() const { return Parameters; }
  const DeclarationFragments &getReturnType() const { return ReturnType; }

  FunctionSignature &addParameter(StringRef Name,
                                  DeclarationFragments Fragments) {
    Parameters.emplace_back(Name, Fragments);
    return *this;
  }

  void setReturnType(DeclarationFragments RT) { ReturnType = RT; }

  /// Determine if the FunctionSignature is empty.
  ///
  /// \returns true if the return type DeclarationFragments is empty and there
  /// is no parameter, otherwise false.
  bool empty() const {
    return Parameters.empty() && ReturnType.getFragments().empty();
  }

private:
  std::vector<Parameter> Parameters;
  DeclarationFragments ReturnType;
};

/// A factory class to build DeclarationFragments for different kinds of Decl.
class DeclarationFragmentsBuilder {
public:
  /// Build FunctionSignature for a function-like declaration \c FunctionT like
  /// FunctionDecl, ObjCMethodDecl, or CXXMethodDecl.
  ///
  /// The logic and implementation of building a signature for a FunctionDecl,
  /// CXXMethodDecl, and ObjCMethodDecl are exactly the same, but they do not
  /// share a common base. This template helps reuse the code.
  template <typename FunctionT>
  static FunctionSignature getFunctionSignature(const FunctionT *Function);

  static AccessControl getAccessControl(const Decl *Decl) {
    switch (Decl->getAccess()) {
    case AS_public:
    case AS_none:
      return AccessControl("public");
    case AS_private:
      return AccessControl("private");
    case AS_protected:
      return AccessControl("protected");
    }
    llvm_unreachable("Unhandled access control");
  }

  static DeclarationFragments
  getFragmentsForNamespace(const NamespaceDecl *Decl);

  /// Build DeclarationFragments for a variable declaration VarDecl.
  static DeclarationFragments getFragmentsForVar(const VarDecl *);

  static DeclarationFragments getFragmentsForVarTemplate(const VarDecl *);

  /// Build DeclarationFragments for a function declaration FunctionDecl.
  static DeclarationFragments getFragmentsForFunction(const FunctionDecl *);

  /// Build DeclarationFragments for an enum constant declaration
  /// EnumConstantDecl.
  static DeclarationFragments
  getFragmentsForEnumConstant(const EnumConstantDecl *);

  /// Build DeclarationFragments for an enum declaration EnumDecl.
  static DeclarationFragments getFragmentsForEnum(const EnumDecl *);

  /// Build DeclarationFragments for a field declaration FieldDecl.
  static DeclarationFragments getFragmentsForField(const FieldDecl *);

  /// Build DeclarationFragments for a struct/union record declaration
  /// RecordDecl.
  static DeclarationFragments getFragmentsForRecordDecl(const RecordDecl *);

  static DeclarationFragments getFragmentsForCXXClass(const CXXRecordDecl *);

  static DeclarationFragments
  getFragmentsForSpecialCXXMethod(const CXXMethodDecl *);

  static DeclarationFragments getFragmentsForCXXMethod(const CXXMethodDecl *);

  static DeclarationFragments
  getFragmentsForConversionFunction(const CXXConversionDecl *);

  static DeclarationFragments
  getFragmentsForOverloadedOperator(const CXXMethodDecl *);

  static DeclarationFragments
      getFragmentsForTemplateParameters(ArrayRef<NamedDecl *>);

  static DeclarationFragments getFragmentsForTemplateArguments(
      const ArrayRef<TemplateArgument>, ASTContext &,
      const std::optional<ArrayRef<TemplateArgumentLoc>>);

  static DeclarationFragments getFragmentsForConcept(const ConceptDecl *);

  static DeclarationFragments
  getFragmentsForRedeclarableTemplate(const RedeclarableTemplateDecl *);

  static DeclarationFragments getFragmentsForClassTemplateSpecialization(
      const ClassTemplateSpecializationDecl *);

  static DeclarationFragments getFragmentsForClassTemplatePartialSpecialization(
      const ClassTemplatePartialSpecializationDecl *);

  static DeclarationFragments getFragmentsForVarTemplateSpecialization(
      const VarTemplateSpecializationDecl *);

  static DeclarationFragments getFragmentsForVarTemplatePartialSpecialization(
      const VarTemplatePartialSpecializationDecl *);

  static DeclarationFragments
  getFragmentsForFunctionTemplate(const FunctionTemplateDecl *Decl);

  static DeclarationFragments
  getFragmentsForFunctionTemplateSpecialization(const FunctionDecl *Decl);

  /// Build DeclarationFragments for an Objective-C category declaration
  /// ObjCCategoryDecl.
  static DeclarationFragments
  getFragmentsForObjCCategory(const ObjCCategoryDecl *);

  /// Build DeclarationFragments for an Objective-C interface declaration
  /// ObjCInterfaceDecl.
  static DeclarationFragments
  getFragmentsForObjCInterface(const ObjCInterfaceDecl *);

  /// Build DeclarationFragments for an Objective-C method declaration
  /// ObjCMethodDecl.
  static DeclarationFragments getFragmentsForObjCMethod(const ObjCMethodDecl *);

  /// Build DeclarationFragments for an Objective-C property declaration
  /// ObjCPropertyDecl.
  static DeclarationFragments
  getFragmentsForObjCProperty(const ObjCPropertyDecl *);

  /// Build DeclarationFragments for an Objective-C protocol declaration
  /// ObjCProtocolDecl.
  static DeclarationFragments
  getFragmentsForObjCProtocol(const ObjCProtocolDecl *);

  /// Build DeclarationFragments for a macro.
  ///
  /// \param Name name of the macro.
  /// \param MD the associated MacroDirective.
  static DeclarationFragments getFragmentsForMacro(StringRef Name,
                                                   const MacroDirective *MD);

  /// Build DeclarationFragments for a typedef \p TypedefNameDecl.
  static DeclarationFragments
  getFragmentsForTypedef(const TypedefNameDecl *Decl);

  /// Build sub-heading fragments for a NamedDecl.
  static DeclarationFragments getSubHeading(const NamedDecl *);

  /// Build sub-heading fragments for an Objective-C method.
  static DeclarationFragments getSubHeading(const ObjCMethodDecl *);

  /// Build a sub-heading for macro \p Name.
  static DeclarationFragments getSubHeadingForMacro(StringRef Name);

private:
  DeclarationFragmentsBuilder() = delete;

  /// Build DeclarationFragments for a QualType.
  static DeclarationFragments getFragmentsForType(const QualType, ASTContext &,
                                                  DeclarationFragments &);

  /// Build DeclarationFragments for a Type.
  static DeclarationFragments getFragmentsForType(const Type *, ASTContext &,
                                                  DeclarationFragments &);

  /// Build DeclarationFragments for a NestedNameSpecifier.
  static DeclarationFragments getFragmentsForNNS(const NestedNameSpecifier *,
                                                 ASTContext &,
                                                 DeclarationFragments &);

  /// Build DeclarationFragments for Qualifiers.
  static DeclarationFragments getFragmentsForQualifiers(const Qualifiers quals);

  /// Build DeclarationFragments for a parameter variable declaration
  /// ParmVarDecl.
  static DeclarationFragments getFragmentsForParam(const ParmVarDecl *);

  static DeclarationFragments
  getFragmentsForBlock(const NamedDecl *BlockDecl, FunctionTypeLoc &Block,
                       FunctionProtoTypeLoc &BlockProto,
                       DeclarationFragments &After);
};

template <typename FunctionT>
FunctionSignature
DeclarationFragmentsBuilder::getFunctionSignature(const FunctionT *Function) {
  FunctionSignature Signature;

  DeclarationFragments ReturnType, After;
  ReturnType = getFragmentsForType(Function->getReturnType(),
                                   Function->getASTContext(), After);
  if (isa<FunctionDecl>(Function) &&
      dyn_cast<FunctionDecl>(Function)->getDescribedFunctionTemplate() &&
      StringRef(ReturnType.begin()->Spelling).starts_with("type-parameter")) {
    std::string ProperArgName = Function->getReturnType().getAsString();
    ReturnType.begin()->Spelling.swap(ProperArgName);
  }
  ReturnType.append(std::move(After));
  Signature.setReturnType(ReturnType);

  for (const auto *Param : Function->parameters())
    Signature.addParameter(Param->getName(), getFragmentsForParam(Param));

  return Signature;
}

} // namespace extractapi
} // namespace clang

#endif // LLVM_CLANG_EXTRACTAPI_DECLARATION_FRAGMENTS_H
