//===--- PrettyPrinter.h - Classes for aiding with AST printing -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines helper types for AST pretty-printing.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_PRETTYPRINTER_H
#define LLVM_CLANG_AST_PRETTYPRINTER_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/LangOptions.h"

namespace clang {

class DeclContext;
class LangOptions;
class Stmt;

class PrinterHelper {
public:
  virtual ~PrinterHelper();
  virtual bool handledStmt(Stmt* E, raw_ostream& OS) = 0;
};

/// Callbacks to use to customize the behavior of the pretty-printer.
class PrintingCallbacks {
protected:
  ~PrintingCallbacks() = default;

public:
  /// Remap a path to a form suitable for printing.
  virtual std::string remapPath(StringRef Path) const {
    return std::string(Path);
  }

  /// When printing type to be inserted into code in specific context, this
  /// callback can be used to avoid printing the redundant part of the
  /// qualifier. For example, when inserting code inside namespace foo, we
  /// should print bar::SomeType instead of foo::bar::SomeType.
  /// To do this, shouldPrintScope should return true on "foo" NamespaceDecl.
  /// The printing stops at the first isScopeVisible() == true, so there will
  /// be no calls with outer scopes.
  virtual bool isScopeVisible(const DeclContext *DC) const { return false; }
};

/// Describes how types, statements, expressions, and declarations should be
/// printed.
///
/// This type is intended to be small and suitable for passing by value.
/// It is very frequently copied.
struct PrintingPolicy {
  /// Create a default printing policy for the specified language.
  PrintingPolicy(const LangOptions &LO)
      : Indentation(2), SuppressSpecifiers(false),
        SuppressTagKeyword(LO.CPlusPlus), IncludeTagDefinition(false),
        SuppressScope(false), SuppressUnwrittenScope(false),
        SuppressInlineNamespace(true), SuppressElaboration(false),
        SuppressInitializers(false), ConstantArraySizeAsWritten(false),
        AnonymousTagLocations(true), SuppressStrongLifetime(false),
        SuppressLifetimeQualifiers(false),
        SuppressTemplateArgsInCXXConstructors(false),
        SuppressDefaultTemplateArgs(true), Bool(LO.Bool),
        Nullptr(LO.CPlusPlus11 || LO.C23), NullptrTypeInNamespace(LO.CPlusPlus),
        Restrict(LO.C99), Alignof(LO.CPlusPlus11), UnderscoreAlignof(LO.C11),
        UseVoidForZeroParams(!LO.CPlusPlus),
        SplitTemplateClosers(!LO.CPlusPlus11), TerseOutput(false),
        PolishForDeclaration(false), Half(LO.Half),
        MSWChar(LO.MicrosoftExt && !LO.WChar), IncludeNewlines(true),
        MSVCFormatting(false), ConstantsAsWritten(false),
        SuppressImplicitBase(false), FullyQualifiedName(false),
        PrintCanonicalTypes(false), PrintInjectedClassNameWithArguments(true),
        UsePreferredNames(true), AlwaysIncludeTypeForTemplateArgument(false),
        CleanUglifiedParameters(false), EntireContentsOfLargeArray(true),
        UseEnumerators(true), UseHLSLTypes(LO.HLSL) {}

  /// Adjust this printing policy for cases where it's known that we're
  /// printing C++ code (for instance, if AST dumping reaches a C++-only
  /// construct). This should not be used if a real LangOptions object is
  /// available.
  void adjustForCPlusPlus() {
    SuppressTagKeyword = true;
    Bool = true;
    UseVoidForZeroParams = false;
  }

  /// The number of spaces to use to indent each line.
  unsigned Indentation : 8;

  /// Whether we should suppress printing of the actual specifiers for
  /// the given type or declaration.
  ///
  /// This flag is only used when we are printing declarators beyond
  /// the first declarator within a declaration group. For example, given:
  ///
  /// \code
  /// const int *x, *y;
  /// \endcode
  ///
  /// SuppressSpecifiers will be false when printing the
  /// declaration for "x", so that we will print "int *x"; it will be
  /// \c true when we print "y", so that we suppress printing the
  /// "const int" type specifier and instead only print the "*y".
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressSpecifiers : 1;

  /// Whether type printing should skip printing the tag keyword.
  ///
  /// This is used when printing the inner type of elaborated types,
  /// (as the tag keyword is part of the elaborated type):
  ///
  /// \code
  /// struct Geometry::Point;
  /// \endcode
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressTagKeyword : 1;

  /// When true, include the body of a tag definition.
  ///
  /// This is used to place the definition of a struct
  /// in the middle of another declaration as with:
  ///
  /// \code
  /// typedef struct { int x, y; } Point;
  /// \endcode
  LLVM_PREFERRED_TYPE(bool)
  unsigned IncludeTagDefinition : 1;

  /// Suppresses printing of scope specifiers.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressScope : 1;

  /// Suppress printing parts of scope specifiers that are never
  /// written, e.g., for anonymous namespaces.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressUnwrittenScope : 1;

  /// Suppress printing parts of scope specifiers that correspond
  /// to inline namespaces, where the name is unambiguous with the specifier
  /// removed.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressInlineNamespace : 1;

  /// Ignore qualifiers and tag keywords as specified by elaborated type sugar,
  /// instead letting the underlying type print as normal.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressElaboration : 1;

  /// Suppress printing of variable initializers.
  ///
  /// This flag is used when printing the loop variable in a for-range
  /// statement. For example, given:
  ///
  /// \code
  /// for (auto x : coll)
  /// \endcode
  ///
  /// SuppressInitializers will be true when printing "auto x", so that the
  /// internal initializer constructed for x will not be printed.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressInitializers : 1;

  /// Whether we should print the sizes of constant array expressions as written
  /// in the sources.
  ///
  /// This flag determines whether array types declared as
  ///
  /// \code
  /// int a[4+10*10];
  /// char a[] = "A string";
  /// \endcode
  ///
  /// will be printed as written or as follows:
  ///
  /// \code
  /// int a[104];
  /// char a[9] = "A string";
  /// \endcode
  LLVM_PREFERRED_TYPE(bool)
  unsigned ConstantArraySizeAsWritten : 1;

  /// When printing an anonymous tag name, also print the location of that
  /// entity (e.g., "enum <anonymous at t.h:10:5>"). Otherwise, just prints
  /// "(anonymous)" for the name.
  LLVM_PREFERRED_TYPE(bool)
  unsigned AnonymousTagLocations : 1;

  /// When true, suppress printing of the __strong lifetime qualifier in ARC.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressStrongLifetime : 1;

  /// When true, suppress printing of lifetime qualifier in ARC.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressLifetimeQualifiers : 1;

  /// When true, suppresses printing template arguments in names of C++
  /// constructors.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressTemplateArgsInCXXConstructors : 1;

  /// When true, attempt to suppress template arguments that match the default
  /// argument for the parameter.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressDefaultTemplateArgs : 1;

  /// Whether we can use 'bool' rather than '_Bool' (even if the language
  /// doesn't actually have 'bool', because, e.g., it is defined as a macro).
  LLVM_PREFERRED_TYPE(bool)
  unsigned Bool : 1;

  /// Whether we should use 'nullptr' rather than '0' as a null pointer
  /// constant.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Nullptr : 1;

  /// Whether 'nullptr_t' is in namespace 'std' or not.
  LLVM_PREFERRED_TYPE(bool)
  unsigned NullptrTypeInNamespace : 1;

  /// Whether we can use 'restrict' rather than '__restrict'.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Restrict : 1;

  /// Whether we can use 'alignof' rather than '__alignof'.
  LLVM_PREFERRED_TYPE(bool)
  unsigned Alignof : 1;

  /// Whether we can use '_Alignof' rather than '__alignof'.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UnderscoreAlignof : 1;

  /// Whether we should use '(void)' rather than '()' for a function prototype
  /// with zero parameters.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseVoidForZeroParams : 1;

  /// Whether nested templates must be closed like 'a\<b\<c\> \>' rather than
  /// 'a\<b\<c\>\>'.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SplitTemplateClosers : 1;

  /// Provide a 'terse' output.
  ///
  /// For example, in this mode we don't print function bodies, class members,
  /// declarations inside namespaces etc.  Effectively, this should print
  /// only the requested declaration.
  LLVM_PREFERRED_TYPE(bool)
  unsigned TerseOutput : 1;

  /// When true, do certain refinement needed for producing proper declaration
  /// tag; such as, do not print attributes attached to the declaration.
  ///
  LLVM_PREFERRED_TYPE(bool)
  unsigned PolishForDeclaration : 1;

  /// When true, print the half-precision floating-point type as 'half'
  /// instead of '__fp16'
  LLVM_PREFERRED_TYPE(bool)
  unsigned Half : 1;

  /// When true, print the built-in wchar_t type as __wchar_t. For use in
  /// Microsoft mode when wchar_t is not available.
  LLVM_PREFERRED_TYPE(bool)
  unsigned MSWChar : 1;

  /// When true, include newlines after statements like "break", etc.
  LLVM_PREFERRED_TYPE(bool)
  unsigned IncludeNewlines : 1;

  /// Use whitespace and punctuation like MSVC does. In particular, this prints
  /// anonymous namespaces as `anonymous namespace' and does not insert spaces
  /// after template arguments.
  LLVM_PREFERRED_TYPE(bool)
  unsigned MSVCFormatting : 1;

  /// Whether we should print the constant expressions as written in the
  /// sources.
  ///
  /// This flag determines whether constants expressions like
  ///
  /// \code
  /// 0x10
  /// 2.5e3
  /// \endcode
  ///
  /// will be printed as written or as follows:
  ///
  /// \code
  /// 0x10
  /// 2.5e3
  /// \endcode
  LLVM_PREFERRED_TYPE(bool)
  unsigned ConstantsAsWritten : 1;

  /// When true, don't print the implicit 'self' or 'this' expressions.
  LLVM_PREFERRED_TYPE(bool)
  unsigned SuppressImplicitBase : 1;

  /// When true, print the fully qualified name of function declarations.
  /// This is the opposite of SuppressScope and thus overrules it.
  LLVM_PREFERRED_TYPE(bool)
  unsigned FullyQualifiedName : 1;

  /// Whether to print types as written or canonically.
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintCanonicalTypes : 1;

  /// Whether to print an InjectedClassNameType with template arguments or as
  /// written. When a template argument is unnamed, printing it results in
  /// invalid C++ code.
  LLVM_PREFERRED_TYPE(bool)
  unsigned PrintInjectedClassNameWithArguments : 1;

  /// Whether to use C++ template preferred_name attributes when printing
  /// templates.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UsePreferredNames : 1;

  /// Whether to use type suffixes (eg: 1U) on integral non-type template
  /// parameters.
  LLVM_PREFERRED_TYPE(bool)
  unsigned AlwaysIncludeTypeForTemplateArgument : 1;

  /// Whether to strip underscores when printing reserved parameter names.
  /// e.g. std::vector<class _Tp> becomes std::vector<class Tp>.
  /// This only affects parameter names, and so describes a compatible API.
  LLVM_PREFERRED_TYPE(bool)
  unsigned CleanUglifiedParameters : 1;

  /// Whether to print the entire array initializers, especially on non-type
  /// template parameters, no matter how many elements there are.
  LLVM_PREFERRED_TYPE(bool)
  unsigned EntireContentsOfLargeArray : 1;

  /// Whether to print enumerator non-type template parameters with a matching
  /// enumerator name or via cast of an integer.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseEnumerators : 1;

  /// Whether or not we're printing known HLSL code and should print HLSL
  /// sugared types when possible.
  LLVM_PREFERRED_TYPE(bool)
  unsigned UseHLSLTypes : 1;

  /// Callbacks to use to allow the behavior of printing to be customized.
  const PrintingCallbacks *Callbacks = nullptr;
};

} // end namespace clang

#endif
