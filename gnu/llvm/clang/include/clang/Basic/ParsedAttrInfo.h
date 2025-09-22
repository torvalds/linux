//===- ParsedAttrInfo.h - Info needed to parse an attribute -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the ParsedAttrInfo class, which dictates how to
// parse an attribute. This class is the one that plugins derive to
// define a new attribute.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_PARSEDATTRINFO_H
#define LLVM_CLANG_BASIC_PARSEDATTRINFO_H

#include "clang/Basic/AttrSubjectMatchRules.h"
#include "clang/Basic/AttributeCommonInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Registry.h"
#include <climits>
#include <list>

namespace clang {

class Decl;
class LangOptions;
class ParsedAttr;
class Sema;
class Stmt;
class TargetInfo;

struct ParsedAttrInfo {
  /// Corresponds to the Kind enum.
  LLVM_PREFERRED_TYPE(AttributeCommonInfo::Kind)
  unsigned AttrKind : 16;
  /// The number of required arguments of this attribute.
  unsigned NumArgs : 4;
  /// The number of optional arguments of this attributes.
  unsigned OptArgs : 4;
  /// The number of non-fake arguments specified in the attribute definition.
  unsigned NumArgMembers : 4;
  /// True if the parsing does not match the semantic content.
  LLVM_PREFERRED_TYPE(bool)
  unsigned HasCustomParsing : 1;
  // True if this attribute accepts expression parameter pack expansions.
  LLVM_PREFERRED_TYPE(bool)
  unsigned AcceptsExprPack : 1;
  /// True if this attribute is only available for certain targets.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsTargetSpecific : 1;
  /// True if this attribute applies to types.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsType : 1;
  /// True if this attribute applies to statements.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsStmt : 1;
  /// True if this attribute has any spellings that are known to gcc.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsKnownToGCC : 1;
  /// True if this attribute is supported by #pragma clang attribute.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IsSupportedByPragmaAttribute : 1;
  /// The syntaxes supported by this attribute and how they're spelled.
  struct Spelling {
    AttributeCommonInfo::Syntax Syntax;
    const char *NormalizedFullName;
  };
  ArrayRef<Spelling> Spellings;
  // The names of the known arguments of this attribute.
  ArrayRef<const char *> ArgNames;

protected:
  constexpr ParsedAttrInfo(AttributeCommonInfo::Kind AttrKind =
                               AttributeCommonInfo::NoSemaHandlerAttribute)
      : AttrKind(AttrKind), NumArgs(0), OptArgs(0), NumArgMembers(0),
        HasCustomParsing(0), AcceptsExprPack(0), IsTargetSpecific(0), IsType(0),
        IsStmt(0), IsKnownToGCC(0), IsSupportedByPragmaAttribute(0) {}

  constexpr ParsedAttrInfo(AttributeCommonInfo::Kind AttrKind, unsigned NumArgs,
                           unsigned OptArgs, unsigned NumArgMembers,
                           unsigned HasCustomParsing, unsigned AcceptsExprPack,
                           unsigned IsTargetSpecific, unsigned IsType,
                           unsigned IsStmt, unsigned IsKnownToGCC,
                           unsigned IsSupportedByPragmaAttribute,
                           ArrayRef<Spelling> Spellings,
                           ArrayRef<const char *> ArgNames)
      : AttrKind(AttrKind), NumArgs(NumArgs), OptArgs(OptArgs),
        NumArgMembers(NumArgMembers), HasCustomParsing(HasCustomParsing),
        AcceptsExprPack(AcceptsExprPack), IsTargetSpecific(IsTargetSpecific),
        IsType(IsType), IsStmt(IsStmt), IsKnownToGCC(IsKnownToGCC),
        IsSupportedByPragmaAttribute(IsSupportedByPragmaAttribute),
        Spellings(Spellings), ArgNames(ArgNames) {}

public:
  virtual ~ParsedAttrInfo() = default;

  /// Check if this attribute has specified spelling.
  bool hasSpelling(AttributeCommonInfo::Syntax Syntax, StringRef Name) const {
    return llvm::any_of(Spellings, [&](const Spelling &S) {
      return (S.Syntax == Syntax && S.NormalizedFullName == Name);
    });
  }

  /// Check if this attribute appertains to D, and issue a diagnostic if not.
  virtual bool diagAppertainsToDecl(Sema &S, const ParsedAttr &Attr,
                                    const Decl *D) const {
    return true;
  }
  /// Check if this attribute appertains to St, and issue a diagnostic if not.
  virtual bool diagAppertainsToStmt(Sema &S, const ParsedAttr &Attr,
                                    const Stmt *St) const {
    return true;
  }
  /// Check if the given attribute is mutually exclusive with other attributes
  /// already applied to the given declaration.
  virtual bool diagMutualExclusion(Sema &S, const ParsedAttr &A,
                                   const Decl *D) const {
    return true;
  }
  /// Check if this attribute is allowed by the language we are compiling.
  virtual bool acceptsLangOpts(const LangOptions &LO) const { return true; }

  /// Check if this attribute is allowed when compiling for the given target.
  virtual bool existsInTarget(const TargetInfo &Target) const { return true; }

  /// Check if this attribute's spelling is allowed when compiling for the given
  /// target.
  virtual bool spellingExistsInTarget(const TargetInfo &Target,
                                      const unsigned SpellingListIndex) const {
    return true;
  }

  /// Convert the spelling index of Attr to a semantic spelling enum value.
  virtual unsigned
  spellingIndexToSemanticSpelling(const ParsedAttr &Attr) const {
    return UINT_MAX;
  }
  /// Returns true if the specified parameter index for this attribute in
  /// Attr.td is an ExprArgument or VariadicExprArgument, or a subclass thereof;
  /// returns false otherwise.
  virtual bool isParamExpr(size_t N) const { return false; }
  /// Populate Rules with the match rules of this attribute.
  virtual void getPragmaAttributeMatchRules(
      llvm::SmallVectorImpl<std::pair<attr::SubjectMatchRule, bool>> &Rules,
      const LangOptions &LangOpts) const {}

  enum AttrHandling { NotHandled, AttributeApplied, AttributeNotApplied };
  /// If this ParsedAttrInfo knows how to handle this ParsedAttr applied to this
  /// Decl then do so and return either AttributeApplied if it was applied or
  /// AttributeNotApplied if it wasn't. Otherwise return NotHandled.
  virtual AttrHandling handleDeclAttribute(Sema &S, Decl *D,
                                           const ParsedAttr &Attr) const {
    return NotHandled;
  }

  static const ParsedAttrInfo &get(const AttributeCommonInfo &A);
  static ArrayRef<const ParsedAttrInfo *> getAllBuiltin();
};

typedef llvm::Registry<ParsedAttrInfo> ParsedAttrInfoRegistry;

const std::list<std::unique_ptr<ParsedAttrInfo>> &getAttributePluginInstances();

} // namespace clang

#endif // LLVM_CLANG_BASIC_PARSEDATTRINFO_H
