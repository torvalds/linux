//===--- BuiltinsBase.td - common structured used by builtins ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// Attributes
// ==========

class Attribute<string mangling> {
  string Mangling = mangling;
}

class IndexedAttribute<string baseMangling, int I> : Attribute<baseMangling> {
  int Index = I;
}

// Standard Attributes
// -------------------
def NoReturn : Attribute<"r">;

// Attributes from the gnu:: namespace
// -----------------------------------
def Const : Attribute<"c">;
def NoThrow : Attribute<"n">;
def Pure : Attribute<"U">;
def ReturnsTwice : Attribute<"j">;
//  FIXME: gcc has nonnull

// builtin-specific attributes
// ---------------------------

// Signature is meaningless, use custom typechecking.
def CustomTypeChecking : Attribute<"t">;

// Type is not important to semantic analysis and codegen; recognize as builtin
// even if type doesn't match signature, and don't warn if we can't be sure the
// type is right.
def IgnoreSignature : Attribute<"T">;

// Arguments are not evaluated for their side-effects.
def UnevaluatedArguments : Attribute<"u">;

// FIXME: This is misused in a lot of the places it is used currently.
// This function is equivalent to a library function without the __builtin_
// prefix. This is relevant for CodeGen; it should not be used if custom CodeGen
// is required for a builtin.
def FunctionWithBuiltinPrefix : Attribute<"F">;

def FunctionWithoutBuiltinPrefix : Attribute<"f">;

// const, but only when -fno-math-errno and FP exceptions are ignored.
def ConstIgnoringErrnoAndExceptions : Attribute<"e">;

// const when FP exceptions are ignored.
def ConstIgnoringExceptions : Attribute<"g">;

// This function requires a specific header or an explicit declaration.
def RequireDeclaration : Attribute<"h">;

class PrintfFormat<int I> : IndexedAttribute<"p", I>;
class VPrintfFormat<int I> : IndexedAttribute<"P", I>;
class ScanfFormat<int I> : IndexedAttribute<"s", I>;
class VScanfFormat<int I> : IndexedAttribute<"S", I>;

// Other Attributes
// ----------------

// Builtin can be constant evaluated
def Constexpr : Attribute<"E">;
// Builtin is immediate and must be constant evaluated. Implies Constexpr, and will only be supported in C++20 mode.
def Consteval : Attribute<"EG">;

// Builtin kinds
// =============

class Builtin {
  list<string> Spellings;
  list<Attribute> Attributes = [];
  string Prototype;
  string Namespace;
  // On some platforms, some functions are actually macros. In that case we need
  // to #undef them.
  bit RequiresUndef = 0;
}

class CustomEntry {
  string Entry;
}

class AtomicBuiltin : Builtin;
class TargetBuiltin : Builtin {
  string Features = "";
}

class LibBuiltin<string header, string languages = "ALL_LANGUAGES"> : Builtin {
  string Header = header;
  string Languages = languages;
  bit AddBuiltinPrefixedAlias = 0;
  bit OnlyBuiltinPrefixedAliasIsConstexpr = 0;
}

class MSLibBuiltin<string header> : LibBuiltin<header, "ALL_MS_LANGUAGES">;
class GNULibBuiltin<string header> : LibBuiltin<header, "ALL_GNU_LANGUAGES">;
class ObjCLibBuiltin<string header> : LibBuiltin<header, "OBJC_LANG">;
class CxxLibBuiltin<string header> : LibBuiltin<header, "CXX_LANG">;

class LangBuiltin<string languages> : Builtin {
  string Languages = languages;
}

class MSLangBuiltin : LangBuiltin<"ALL_MS_LANGUAGES">;
class CoroLangBuiltin : LangBuiltin<"COR_LANG">;
class OCLPipeLangBuiltin : LangBuiltin<"OCL_PIPE">;
class OCL_DSELangBuiltin : LangBuiltin<"OCL_DSE">;
class OCL_GASLangBuiltin : LangBuiltin<"OCL_GAS">;
class OCLLangBuiltin : LangBuiltin<"ALL_OCL_LANGUAGES">;

class Template<list<string> substitutions,
               list<string> affixes,
               bit as_prefix = 0> {
  list<string> Substitutions = substitutions;
  list<string> Affixes = affixes;
  bit AsPrefix = as_prefix;
}
