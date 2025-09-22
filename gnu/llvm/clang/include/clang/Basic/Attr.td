//==--- Attr.td - attribute definitions -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// The documentation is organized by category. Attributes can have category-
// specific documentation that is collated within the larger document.
class DocumentationCategory<string name> {
  string Name = name;
  code Content = [{}];
}
def DocCatFunction : DocumentationCategory<"Function Attributes">;
def DocCatVariable : DocumentationCategory<"Variable Attributes">;
def DocCatField : DocumentationCategory<"Field Attributes">;
def DocCatType : DocumentationCategory<"Type Attributes">;
def DocCatStmt : DocumentationCategory<"Statement Attributes">;
def DocCatDecl : DocumentationCategory<"Declaration Attributes">;

// This category is for attributes which have not yet been properly documented,
// but should be.
def DocCatUndocumented : DocumentationCategory<"Undocumented"> {
  let Content = [{
This section lists attributes which are recognized by Clang, but which are
currently missing documentation.
}];
}

// Attributes listed under the InternalOnly category do not generate any entry
// in the documentation.  This category should be used only when we _want_
// to not document the attribute, e.g. if the attribute has no spellings.
def DocCatInternalOnly : DocumentationCategory<"InternalOnly">;

class DocDeprecated<string replacement = ""> {
  // If the Replacement field is empty, no replacement will be listed with the
  // documentation. Otherwise, the documentation will specify the attribute has
  // been superseded by this replacement.
  string Replacement = replacement;
}

// Specifies the documentation to be associated with the given category.
class Documentation {
  DocumentationCategory Category;
  code Content;

  // If the heading is empty, one may be picked automatically. If the attribute
  // only has one spelling, no heading is required as the attribute's sole
  // spelling is sufficient. If all spellings are semantically common, the
  // heading will be the semantic spelling. If the spellings are not
  // semantically common and no heading is provided, an error will be emitted.
  string Heading = "";

  // When set, specifies that the attribute is deprecated and can optionally
  // specify a replacement attribute.
  DocDeprecated Deprecated;
}

// Specifies that the attribute is explicitly omitted from the documentation,
// because it is not intended to be user-facing.
def InternalOnly : Documentation {
  let Category = DocCatInternalOnly;
}

// Specifies that the attribute is undocumented, but that it _should_ have
// documentation.
def Undocumented : Documentation {
  let Category = DocCatUndocumented;
  let Content = "No documentation.";
}

include "clang/Basic/AttrDocs.td"

// An attribute's subject is whatever it appertains to. In this file, it is
// more accurately a list of things that an attribute can appertain to. All
// Decls and Stmts are possibly AttrSubjects (even though the syntax may not
// allow attributes on a given Decl or Stmt).
class AttrSubject;

include "clang/Basic/DeclNodes.td"
include "clang/Basic/StmtNodes.td"

// A subset-subject is an AttrSubject constrained to operate only on some subset
// of that subject.
//
// The code fragment is a boolean expression that will confirm that the subject
// meets the requirements; the subject will have the name S, and will have the
// type specified by the base. It should be a simple boolean expression. The
// diagnostic string should be a comma-separated list of subject names.
class SubsetSubject<AttrSubject base, code check, string diag> : AttrSubject {
  AttrSubject Base = base;
  code CheckCode = check;
  string DiagSpelling = diag;
}

def LocalVar : SubsetSubject<Var,
                              [{S->hasLocalStorage() && !isa<ParmVarDecl>(S)}],
                              "local variables">;
def NonParmVar : SubsetSubject<Var,
                               [{S->getKind() != Decl::ParmVar}],
                               "variables">;
def NonLocalVar : SubsetSubject<Var,
                                [{!S->hasLocalStorage()}],
                                "variables with non-local storage">;
def NonBitField : SubsetSubject<Field,
                                [{!S->isBitField()}],
                                "non-bit-field non-static data members">;

def BitField : SubsetSubject<Field,
                             [{S->isBitField()}],
                             "bit-field data members">;

def NonStaticCXXMethod : SubsetSubject<CXXMethod,
                                       [{!S->isStatic()}],
                                       "non-static member functions">;

def NonStaticNonConstCXXMethod
    : SubsetSubject<CXXMethod,
                    [{!S->isStatic() && !S->isConst()}],
                    "non-static non-const member functions">;

def ObjCInstanceMethod : SubsetSubject<ObjCMethod,
                                       [{S->isInstanceMethod()}],
                                       "Objective-C instance methods">;

def Struct : SubsetSubject<Record,
                           [{!S->isUnion()}], "structs">;

def TLSVar : SubsetSubject<Var,
                           [{S->getTLSKind() != 0}], "thread-local variables">;

def SharedVar : SubsetSubject<Var,
                              [{S->hasGlobalStorage() && !S->getTLSKind()}],
                              "global variables">;

def GlobalVar : SubsetSubject<Var,
                             [{S->hasGlobalStorage()}], "global variables">;

def ExternalGlobalVar : SubsetSubject<Var,
                             [{S->hasGlobalStorage() &&
                               S->getStorageClass()!=StorageClass::SC_Static &&
                               !S->isLocalExternDecl()}],
                             "external global variables">;

def NonTLSGlobalVar : SubsetSubject<Var,
                             [{S->hasGlobalStorage() &&
                               S->getTLSKind() == 0}],
                             "non-TLS global variables">;

def InlineFunction : SubsetSubject<Function,
                             [{S->isInlineSpecified()}], "inline functions">;

def FunctionTmpl
    : SubsetSubject<Function, [{S->getTemplatedKind() ==
                                 FunctionDecl::TK_FunctionTemplate}],
                    "function templates">;

def HLSLEntry
    : SubsetSubject<Function,
                    [{S->isExternallyVisible() && !isa<CXXMethodDecl>(S)}],
                    "global functions">;
def HLSLBufferObj : SubsetSubject<HLSLBuffer,
                    [{isa<HLSLBufferDecl>(S)}],
                    "cbuffer/tbuffer">;

def ClassTmpl : SubsetSubject<CXXRecord, [{S->getDescribedClassTemplate()}],
                              "class templates">;

// FIXME: this hack is needed because DeclNodes.td defines the base Decl node
// type to be a class, not a definition. This makes it impossible to create an
// attribute subject which accepts a Decl. Normally, this is not a problem,
// because the attribute can have no Subjects clause to accomplish this. But in
// the case of a SubsetSubject, there's no way to express it without this hack.
def DeclBase : AttrSubject;
def FunctionLike : SubsetSubject<DeclBase,
                                 [{S->getFunctionType(false) != nullptr}],
                                 "functions, function pointers">;

// Function Pointer is a stricter version of FunctionLike that only allows function
// pointers.
def FunctionPointer : SubsetSubject<DeclBase,
                                    [{S->isFunctionPointerType()}],
                                    "functions pointers">;

def OpenCLKernelFunction
    : SubsetSubject<Function, [{S->hasAttr<OpenCLKernelAttr>()}],
                    "kernel functions">;

// HasFunctionProto is a more strict version of FunctionLike, so it should
// never be specified in a Subjects list along with FunctionLike (due to the
// inclusive nature of subject testing).
def HasFunctionProto : SubsetSubject<DeclBase,
                                     [{(S->getFunctionType(true) != nullptr &&
                              isa<FunctionProtoType>(S->getFunctionType())) ||
                                       isa<ObjCMethodDecl>(S) ||
                                       isa<BlockDecl>(S)}],
                                     "non-K&R-style functions">;

// A subject that matches the implicit object parameter of a non-static member
// function. Accepted as a function type attribute on the type of such a
// member function.
// FIXME: This does not actually ever match currently.
def ImplicitObjectParameter
    : SubsetSubject<Function, [{static_cast<void>(S), false}],
                    "implicit object parameters">;

// A single argument to an attribute
class Argument<string name, bit optional, bit fake = 0> {
  string Name = name;
  bit Optional = optional;

  /// A fake argument is used to store and serialize additional information
  /// in an attribute without actually changing its parsing or pretty-printing.
  bit Fake = fake;
}

class BoolArgument<string name, bit opt = 0, bit fake = 0> : Argument<name, opt,
                                                                      fake>;
class IdentifierArgument<string name, bit opt = 0> : Argument<name, opt>;
class IntArgument<string name, bit opt = 0> : Argument<name, opt>;
class StringArgument<string name, bit opt = 0> : Argument<name, opt>;
class ExprArgument<string name, bit opt = 0> : Argument<name, opt>;
class DeclArgument<DeclNode kind, string name, bit opt = 0, bit fake = 0>
    : Argument<name, opt, fake> {
  DeclNode Kind = kind;
}

// An argument of a OMPDeclareVariantAttr that represents the `match`
// clause of the declare variant by keeping the information (incl. nesting) in
// an OMPTraitInfo object.
//
// With some exceptions, the `match(<context-selector>)` clause looks roughly
// as follows:
//   context-selector := list<selector-set>
//       selector-set := <kind>={list<selector>}
//           selector := <kind>([score(<const-expr>):] list<trait>)
//              trait := <kind>
//
// The structure of an OMPTraitInfo object is a tree as defined below:
//
//   OMPTraitInfo     := {list<OMPTraitSet>}
//   OMPTraitSet      := {Kind, list<OMPTraitSelector>}
//   OMPTraitSelector := {Kind, Expr, list<OMPTraitProperty>}
//   OMPTraitProperty := {Kind}
//
class OMPTraitInfoArgument<string name> : Argument<name, 0>;
class VariadicOMPInteropInfoArgument<string name> : Argument<name, 0>;

class TypeArgument<string name, bit opt = 0> : Argument<name, opt>;
class UnsignedArgument<string name, bit opt = 0> : Argument<name, opt>;
class VariadicUnsignedArgument<string name> : Argument<name, 1>;
class VariadicExprArgument<string name> : Argument<name, 1>;
class VariadicStringArgument<string name> : Argument<name, 1>;
class VariadicIdentifierArgument<string name> : Argument<name, 1>;

// Like VariadicUnsignedArgument except values are ParamIdx.
class VariadicParamIdxArgument<string name> : Argument<name, 1>;

// A list of identifiers matching parameters or ParamIdx indices.
class VariadicParamOrParamIdxArgument<string name> : Argument<name, 1>;

// Like VariadicParamIdxArgument but for a single function parameter index.
class ParamIdxArgument<string name, bit opt = 0> : Argument<name, opt>;

// A version of the form major.minor[.subminor].
class VersionArgument<string name, bit opt = 0> : Argument<name, opt>;

// This one's a doozy, so it gets its own special type
// It can be an unsigned integer, or a type. Either can
// be dependent.
class AlignedArgument<string name, bit opt = 0> : Argument<name, opt>;

// A bool argument with a default value
class DefaultBoolArgument<string name, bit default, bit fake = 0>
    : BoolArgument<name, 1, fake> {
  bit Default = default;
}

// An integer argument with a default value
class DefaultIntArgument<string name, int default> : IntArgument<name, 1> {
  int Default = default;
}

// This argument is more complex, it includes the enumerator type
// name, whether the enum type is externally defined, a list of
// possible values, and a list of enumerators to map them to.
class EnumArgument<string name, string type, bit is_string, list<string> values,
                   list<string> enums, bit opt = 0, bit fake = 0,
                   bit isExternalType = 0, bit isCovered = 1>
    : Argument<name, opt, fake> {
  string Type = type;
  // When true, the argument will be parsed as an unevaluated string literal
  // and otherwise as an identifier.
  bit IsString = is_string;
  list<string> Values = values;
  list<string> Enums = enums;
  bit IsExternalType = isExternalType;
  // We need to know whether an external enum is fully covered by the options
  // in order to decide whether to emit unreachable default labels in a switch.
  bit IsCovered = isCovered;
}

// FIXME: There should be a VariadicArgument type that takes any other type
//        of argument and generates the appropriate type.
class VariadicEnumArgument<string name, string type, bit is_string,
                           list<string> values, list<string> enums,
                           bit isExternalType = 0, bit isCovered = 1>
    : Argument<name, 1>  {
  string Type = type;
  // When true, the argument will be parsed as an unevaluated string literal
  // and otherwise as an identifier.
  bit IsString = is_string;
  list<string> Values = values;
  list<string> Enums = enums;
  bit IsExternalType = isExternalType;
  // We need to know whether an external enum is fully covered by the options
  // in order to decide whether to emit unreachable default labels in a switch.
  bit IsCovered = isCovered;
}

// Represents an attribute wrapped by another attribute.
class WrappedAttr<string name, bit opt = 0> : Argument<name, opt>;

// This handles one spelling of an attribute.
class Spelling<string name, string variety, int version = 1> {
  string Name = name;
  string Variety = variety;
  int Version = version;
}

class GNU<string name> : Spelling<name, "GNU">;
class Declspec<string name> : Spelling<name, "Declspec">;
class Microsoft<string name> : Spelling<name, "Microsoft">;
class CXX11<string namespace, string name, int version = 1>
    : Spelling<name, "CXX11", version> {
  string Namespace = namespace;
}
class C23<string namespace, string name, int version = 1>
    : Spelling<name, "C23", version> {
  string Namespace = namespace;
}

class Keyword<string name, bit hasOwnParseRules>
    : Spelling<name, "Keyword"> {
  bit HasOwnParseRules = hasOwnParseRules;
}

// A keyword that can appear wherever a standard attribute can appear,
// and that appertains to whatever a standard attribute would appertain to.
// This is useful for things that affect semantics but that should otherwise
// be treated like standard attributes.
class RegularKeyword<string name> : Keyword<name, 0> {}

// A keyword that has its own individual parsing rules.
class CustomKeyword<string name> : Keyword<name, 1> {}

class Pragma<string namespace, string name> : Spelling<name, "Pragma"> {
  string Namespace = namespace;
}

// The GCC spelling implies GNU<name>, CXX11<"gnu", name>, and optionally,
// C23<"gnu", name>. This spelling should be used for any GCC-compatible
// attributes.
class GCC<string name, bit allowInC = 1> : Spelling<name, "GCC"> {
  bit AllowInC = allowInC;
}

// The Clang spelling implies GNU<name>, CXX11<"clang", name>, and optionally,
// C23<"clang", name>. This spelling should be used for any Clang-specific
// attributes.
class Clang<string name, bit allowInC = 1, int version = 1>
    : Spelling<name, "Clang", version> {
  bit AllowInC = allowInC;
}

// HLSL Annotation spellings
class HLSLAnnotation<string name> : Spelling<name, "HLSLAnnotation">;

class Accessor<string name, list<Spelling> spellings> {
  string Name = name;
  list<Spelling> Spellings = spellings;
}

class SubjectDiag<bit warn> {
  bit Warn = warn;
}
def WarnDiag : SubjectDiag<1>;
def ErrorDiag : SubjectDiag<0>;

class SubjectList<list<AttrSubject> subjects, SubjectDiag diag = WarnDiag,
                  string customDiag = ""> {
  list<AttrSubject> Subjects = subjects;
  SubjectDiag Diag = diag;
  string CustomDiag = customDiag;
}

class LangOpt<string name, code customCode = [{}]> {
  // The language option to test; ignored when custom code is supplied.
  string Name = name;

  // A custom predicate, written as an expression evaluated in a context with
  // "LangOpts" bound.
  code CustomCode = customCode;
}
def MicrosoftExt : LangOpt<"MicrosoftExt">;
def Borland : LangOpt<"Borland">;
def CUDA : LangOpt<"CUDA">;
def HIP : LangOpt<"HIP">;
def SYCL : LangOpt<"SYCLIsDevice">;
def COnly : LangOpt<"", "!LangOpts.CPlusPlus">;
def CPlusPlus : LangOpt<"CPlusPlus">;
def OpenCL : LangOpt<"OpenCL">;
def RenderScript : LangOpt<"RenderScript">;
def ObjC : LangOpt<"ObjC">;
def BlocksSupported : LangOpt<"Blocks">;
def ObjCAutoRefCount : LangOpt<"ObjCAutoRefCount">;
def ObjCNonFragileRuntime
    : LangOpt<"", "LangOpts.ObjCRuntime.allowsClassStubs()">;

def HLSL : LangOpt<"HLSL">;

// Language option for CMSE extensions
def Cmse : LangOpt<"Cmse">;

// Defines targets for target-specific attributes. Empty lists are unchecked.
class TargetSpec {
  // Specifies Architectures for which the target applies, based off the
  // ArchType enumeration in Triple.h.
  list<string> Arches = [];
  // Specifies Operating Systems for which the target applies, based off the
  // OSType enumeration in Triple.h
  list<string> OSes;
  // Specifies Object Formats for which the target applies, based off the
  // ObjectFormatType enumeration in Triple.h
  list<string> ObjectFormats;
  // A custom predicate, written as an expression evaluated in a context
  // with the following declarations in scope:
  //   const clang::TargetInfo &Target;
  //   const llvm::Triple &T = Target.getTriple();
  code CustomCode = [{}];
}

class TargetArch<list<string> arches> : TargetSpec {
  let Arches = arches;
}
def TargetARM : TargetArch<["arm", "thumb", "armeb", "thumbeb"]>;
def TargetAArch64 : TargetArch<["aarch64", "aarch64_be", "aarch64_32"]>;
def TargetAnyArm : TargetArch<!listconcat(TargetARM.Arches, TargetAArch64.Arches)>;
def TargetAVR : TargetArch<["avr"]>;
def TargetBPF : TargetArch<["bpfel", "bpfeb"]>;
def TargetLoongArch : TargetArch<["loongarch32", "loongarch64"]>;
def TargetMips32 : TargetArch<["mips", "mipsel"]>;
def TargetAnyMips : TargetArch<["mips", "mipsel", "mips64", "mips64el"]>;
def TargetMSP430 : TargetArch<["msp430"]>;
def TargetM68k : TargetArch<["m68k"]>;
def TargetRISCV : TargetArch<["riscv32", "riscv64"]>;
def TargetX86 : TargetArch<["x86"]>;
def TargetAnyX86 : TargetArch<["x86", "x86_64"]>;
def TargetWebAssembly : TargetArch<["wasm32", "wasm64"]>;
def TargetNVPTX : TargetArch<["nvptx", "nvptx64"]>;
def TargetWindows : TargetSpec {
  let OSes = ["Win32"];
}
def TargetHasDLLImportExport : TargetSpec {
  let CustomCode = [{ Target.getTriple().hasDLLImportExport() }];
}
def TargetItaniumCXXABI : TargetSpec {
  let CustomCode = [{ Target.getCXXABI().isItaniumFamily() }];
}
def TargetMicrosoftCXXABI : TargetArch<["x86", "x86_64", "arm", "thumb", "aarch64"]> {
  let CustomCode = [{ Target.getCXXABI().isMicrosoft() }];
}
def TargetELF : TargetSpec {
  let ObjectFormats = ["ELF"];
}
def TargetELFOrMachO : TargetSpec {
  let ObjectFormats = ["ELF", "MachO"];
}
def TargetWindowsArm64EC : TargetSpec {
  let CustomCode = [{ Target.getTriple().isWindowsArm64EC() }];
}

def TargetSupportsInitPriority : TargetSpec {
  let CustomCode = [{ !Target.getTriple().isOSzOS() }];
}

class TargetSpecificSpelling<TargetSpec target, list<Spelling> spellings> {
  TargetSpec Target = target;
  list<Spelling> Spellings = spellings;
}

// Attribute subject match rules that are used for #pragma clang attribute.
//
// A instance of AttrSubjectMatcherRule represents an individual match rule.
// An individual match rule can correspond to a number of different attribute
// subjects, e.g. "record" matching rule corresponds to the Record and
// CXXRecord attribute subjects.
//
// Match rules are used in the subject list of the #pragma clang attribute.
// Match rules can have sub-match rules that are instances of
// AttrSubjectMatcherSubRule. A sub-match rule can correspond to a number
// of different attribute subjects, and it can have a negated spelling as well.
// For example, "variable(unless(is_parameter))" matching rule corresponds to
// the NonParmVar attribute subject.
class AttrSubjectMatcherSubRule<string name, list<AttrSubject> subjects,
                                bit negated = 0> {
  string Name = name;
  list<AttrSubject> Subjects = subjects;
  bit Negated = negated;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, the language options are taken
  // from the parent matcher rule.
  list<LangOpt> LangOpts = [];
}
class AttrSubjectMatcherRule<string name, list<AttrSubject> subjects,
                             list<AttrSubjectMatcherSubRule> subrules = []> {
  string Name = name;
  list<AttrSubject> Subjects = subjects;
  list<AttrSubjectMatcherSubRule> Constraints = subrules;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, no language options are required.
  list<LangOpt> LangOpts = [];
}

// function(is_member)
def SubRuleForCXXMethod : AttrSubjectMatcherSubRule<"is_member", [CXXMethod]> {
  let LangOpts = [CPlusPlus];
}
def SubjectMatcherForFunction : AttrSubjectMatcherRule<"function", [Function], [
  SubRuleForCXXMethod
]>;
// hasType is abstract, it should be used with one of the sub-rules.
def SubjectMatcherForType : AttrSubjectMatcherRule<"hasType", [], [
  AttrSubjectMatcherSubRule<"functionType", [FunctionLike]>

  // FIXME: There's a matcher ambiguity with objc methods and blocks since
  // functionType excludes them but functionProtoType includes them.
  // AttrSubjectMatcherSubRule<"functionProtoType", [HasFunctionProto]>
]>;
def SubjectMatcherForTypedef : AttrSubjectMatcherRule<"type_alias",
                                                      [TypedefName]>;
def SubjectMatcherForRecord : AttrSubjectMatcherRule<"record", [Record,
                                                                CXXRecord], [
  // unless(is_union)
  AttrSubjectMatcherSubRule<"is_union", [Struct], 1>
]>;
def SubjectMatcherForEnum : AttrSubjectMatcherRule<"enum", [Enum]>;
def SubjectMatcherForEnumConstant : AttrSubjectMatcherRule<"enum_constant",
                                                           [EnumConstant]>;
def SubjectMatcherForVar : AttrSubjectMatcherRule<"variable", [Var], [
  AttrSubjectMatcherSubRule<"is_thread_local", [TLSVar]>,
  AttrSubjectMatcherSubRule<"is_global", [GlobalVar]>,
  AttrSubjectMatcherSubRule<"is_local", [LocalVar]>,
  AttrSubjectMatcherSubRule<"is_parameter", [ParmVar]>,
  // unless(is_parameter)
  AttrSubjectMatcherSubRule<"is_parameter", [NonParmVar], 1>
]>;
def SubjectMatcherForField : AttrSubjectMatcherRule<"field", [Field]>;
def SubjectMatcherForNamespace : AttrSubjectMatcherRule<"namespace",
                                                        [Namespace]> {
  let LangOpts = [CPlusPlus];
}
def SubjectMatcherForObjCInterface : AttrSubjectMatcherRule<"objc_interface",
                                                            [ObjCInterface]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCProtocol : AttrSubjectMatcherRule<"objc_protocol",
                                                           [ObjCProtocol]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCCategory : AttrSubjectMatcherRule<"objc_category",
                                                           [ObjCCategory]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCImplementation :
    AttrSubjectMatcherRule<"objc_implementation", [ObjCImpl]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCMethod : AttrSubjectMatcherRule<"objc_method",
                                                         [ObjCMethod], [
  AttrSubjectMatcherSubRule<"is_instance", [ObjCInstanceMethod]>
]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForObjCProperty : AttrSubjectMatcherRule<"objc_property",
                                                           [ObjCProperty]> {
  let LangOpts = [ObjC];
}
def SubjectMatcherForBlock : AttrSubjectMatcherRule<"block", [Block]> {
  let LangOpts = [BlocksSupported];
}

// Aggregate attribute subject match rules are abstract match rules that can't
// be used directly in #pragma clang attribute. Instead, users have to use
// subject match rules that correspond to attribute subjects that derive from
// the specified subject.
class AttrSubjectMatcherAggregateRule<AttrSubject subject> {
  AttrSubject Subject = subject;
}

def SubjectMatcherForNamed : AttrSubjectMatcherAggregateRule<Named>;

// Enumeration specifying what kind of behavior should be used for late
// parsing of attributes.
class LateAttrParseKind <int val> {
  int Kind = val;
}

// Never late parsed
def LateAttrParseNever : LateAttrParseKind<0>;

// Standard late attribute parsing
//
// This is language dependent. For example:
//
// * For C++ enables late parsing of a declaration attributes
// * For C does not enable late parsing of attributes
//
def LateAttrParseStandard : LateAttrParseKind<1>;

// Experimental extension to standard late attribute parsing
//
// This extension behaves like `LateAttrParseStandard` but allows
// late parsing attributes in more contexts.
//
// In contexts where `LateAttrParseStandard` attributes are late
// parsed, `LateAttrParseExperimentalExt` attributes will also
// be late parsed.
//
// In contexts that only late parse `LateAttrParseExperimentalExt` attributes
// (see `LateParsedAttrList::lateAttrParseExperimentalExtOnly()`)
//
// * If `-fexperimental-late-parse-attributes`
//   (`LangOpts.ExperimentalLateParseAttributes`) is enabled the attribute
//   will be late parsed.
// * If `-fexperimental-late-parse-attributes`
//   (`LangOpts.ExperimentalLateParseAttributes`) is disabled the attribute
//   will **not** be late parsed (i.e parsed immediately).
//
// The following contexts are supported:
//
// * TODO: Add contexts here when they are implemented.
//
def LateAttrParseExperimentalExt : LateAttrParseKind<2>;

class Attr {
  // The various ways in which an attribute can be spelled in source
  list<Spelling> Spellings;
  // The things to which an attribute can appertain
  SubjectList Subjects;
  // The arguments allowed on an attribute
  list<Argument> Args = [];
  // Accessors which should be generated for the attribute.
  list<Accessor> Accessors = [];
  // Specify targets for spellings.
  list<TargetSpecificSpelling> TargetSpecificSpellings = [];
  // Specifies the late parsing kind.
  LateAttrParseKind LateParsed = LateAttrParseNever;
  // Set to false to prevent an attribute from being propagated from a template
  // to the instantiation.
  bit Clone = 1;
  // Set to true for attributes which must be instantiated within templates
  bit TemplateDependent = 0;
  // Set to true for attributes that have a corresponding AST node.
  bit ASTNode = 1;
  // Set to true for attributes which have handler in Sema.
  bit SemaHandler = 1;
  // Set to true if this attribute doesn't need custom handling in Sema.
  bit SimpleHandler = 0;
  // Set to true for attributes that are completely ignored.
  bit Ignored = 0;
  // Set to true if the attribute's parsing does not match its semantic
  // content. Eg) It parses 3 args, but semantically takes 4 args.  Opts out of
  // common attribute error checking.
  bit HasCustomParsing = 0;
  // Set to true if all of the attribute's arguments should be parsed in an
  // unevaluated context.
  bit ParseArgumentsAsUnevaluated = 0;
  // Set to true if this attribute meaningful when applied to or inherited
  // in a class template definition.
  bit MeaningfulToClassTemplateDefinition = 0;
  // Set to true if this attribute can be used with '#pragma clang attribute'.
  // By default, an attribute is supported by the '#pragma clang attribute'
  // only when:
  // - It has a subject list whose subjects can be represented using subject
  //   match rules.
  // - It has GNU/CXX11 spelling and doesn't require delayed parsing.
  bit PragmaAttributeSupport;
  // Set to true if this attribute accepts parameter pack expansion expressions.
  bit AcceptsExprPack = 0;
  // To support multiple enum parameters to an attribute without breaking
  // our existing general parsing we need to have a separate flag that
  // opts an attribute into strict parsing of attribute parameters
  bit StrictEnumParameters = 0;
  // Lists language options, one of which is required to be true for the
  // attribute to be applicable. If empty, no language options are required.
  list<LangOpt> LangOpts = [];
  // Any additional text that should be included verbatim in the class.
  // Note: Any additional data members will leak and should be constructed
  // externally on the ASTContext.
  code AdditionalMembers = [{}];
  // Any documentation that should be associated with the attribute. Since an
  // attribute may be documented under multiple categories, more than one
  // Documentation entry may be listed.
  list<Documentation> Documentation;
}

/// Used to define a set of mutually exclusive attributes.
class MutualExclusions<list<Attr> Ex> {
  list<Attr> Exclusions = Ex;
}

/// A type attribute is not processed on a declaration or a statement.
class TypeAttr : Attr;

/// A stmt attribute is not processed on a declaration or a type.
class StmtAttr : Attr;

/// An inheritable attribute is inherited by later redeclarations.
class InheritableAttr : Attr {
  // Set to true if this attribute can be duplicated on a subject when inheriting
  // attributes from prior declarations.
  bit InheritEvenIfAlreadyPresent = 0;
}

/// Some attributes, like calling conventions, can appear in either the
/// declaration or the type position. These attributes are morally type
/// attributes, but have historically been written on declarations.
class DeclOrTypeAttr : InheritableAttr;

/// A attribute is either a declaration attribute or a statement attribute.
class DeclOrStmtAttr : InheritableAttr;

/// An attribute class for HLSL Annotations.
class HLSLAnnotationAttr : InheritableAttr;

/// A target-specific attribute.  This class is meant to be used as a mixin
/// with InheritableAttr or Attr depending on the attribute's needs.
class TargetSpecificAttr<TargetSpec target> {
  TargetSpec Target = target;
  // Attributes are generally required to have unique spellings for their names
  // so that the parser can determine what kind of attribute it has parsed.
  // However, target-specific attributes are special in that the attribute only
  // "exists" for a given target. So two target-specific attributes can share
  // the same name when they exist in different targets. To support this, a
  // Kind can be explicitly specified for a target-specific attribute. This
  // corresponds to the ParsedAttr::AT_* enum that is generated and it
  // should contain a shared value between the attributes.
  //
  // Target-specific attributes which use this feature should ensure that the
  // spellings match exactly between the attributes, and if the arguments or
  // subjects differ, should specify HasCustomParsing = 1 and implement their
  // own parsing and semantic handling requirements as-needed.
  string ParseKind;
}

/// An inheritable parameter attribute is inherited by later
/// redeclarations, even when it's written on a parameter.
class InheritableParamAttr : InheritableAttr;

/// An attribute which changes the ABI rules for a specific parameter.
class ParameterABIAttr : InheritableParamAttr {
  let Subjects = SubjectList<[ParmVar]>;
}

/// An ignored attribute, which we parse but discard with no checking.
class IgnoredAttr : Attr {
  let Ignored = 1;
  let ASTNode = 0;
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

//
// Attributes begin here
//

def AbiTag : Attr {
  let Spellings = [GCC<"abi_tag", /*AllowInC*/0>];
  let Args = [VariadicStringArgument<"Tags">];
  let Subjects = SubjectList<[Struct, Var, Function, Namespace], ErrorDiag>;
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [AbiTagsDocs];
}

def AddressSpace : TypeAttr {
  let Spellings = [Clang<"address_space">];
  let Args = [IntArgument<"AddressSpace">];
  let Documentation = [Undocumented];
}

def Alias : Attr {
  let Spellings = [GCC<"alias">];
  let Args = [StringArgument<"Aliasee">];
  let Subjects = SubjectList<[Function, GlobalVar], ErrorDiag>;
  let Documentation = [Undocumented];
}

def BuiltinAlias : Attr {
  let Spellings = [CXX11<"clang", "builtin_alias">,
                   C23<"clang", "builtin_alias">,
                   GNU<"clang_builtin_alias">];
  let Args = [IdentifierArgument<"BuiltinName">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [BuiltinAliasDocs];
}

def ArmBuiltinAlias : InheritableAttr, TargetSpecificAttr<TargetAnyArm> {
  let Spellings = [Clang<"__clang_arm_builtin_alias">];
  let Args = [IdentifierArgument<"BuiltinName">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [ArmBuiltinAliasDocs];
}

def Aligned : InheritableAttr {
  let Spellings = [GCC<"aligned">, Declspec<"align">, CustomKeyword<"alignas">,
                   CustomKeyword<"_Alignas">];
  let Args = [AlignedArgument<"Alignment", 1>];
  let Accessors = [Accessor<"isGNU", [GCC<"aligned">]>,
                   Accessor<"isC11", [CustomKeyword<"_Alignas">]>,
                   Accessor<"isAlignas", [CustomKeyword<"alignas">,
                                          CustomKeyword<"_Alignas">]>,
                   Accessor<"isDeclspec",[Declspec<"align">]>];
  let Documentation = [Undocumented];
}

def AlignValue : Attr {
  let Spellings = [
    // Unfortunately, this is semantically an assertion, not a directive
    // (something else must ensure the alignment), so aligned_value is a
    // probably a better name. We might want to add an aligned_value spelling in
    // the future (and a corresponding C++ attribute), but this can be done
    // later once we decide if we also want them to have slightly-different
    // semantics than Intel's align_value.
    //
    // Does not get a [[]] spelling because the attribute is not exposed as such
    // by Intel.
    GNU<"align_value">
    // Intel's compiler on Windows also supports:
    // , Declspec<"align_value">
  ];
  let Args = [ExprArgument<"Alignment">];
  let Subjects = SubjectList<[Var, TypedefName]>;
  let Documentation = [AlignValueDocs];
}

def AlignMac68k : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def AlignNatural : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def AlwaysInline : DeclOrStmtAttr {
  let Spellings = [GCC<"always_inline">, CXX11<"clang", "always_inline">,
                   C23<"clang", "always_inline">, CustomKeyword<"__forceinline">];
  let Accessors = [Accessor<"isClangAlwaysInline", [CXX11<"clang", "always_inline">,
                                                    C23<"clang", "always_inline">]>];
  let Subjects = SubjectList<[Function, Stmt], WarnDiag,
                             "functions and statements">;
  let Documentation = [AlwaysInlineDocs];
}

def Artificial : InheritableAttr {
  let Spellings = [GCC<"artificial">];
  let Subjects = SubjectList<[InlineFunction]>;
  let Documentation = [ArtificialDocs];
  let SimpleHandler = 1;
}

def XRayInstrument : InheritableAttr {
  let Spellings = [Clang<"xray_always_instrument">,
                   Clang<"xray_never_instrument">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Accessors = [Accessor<"alwaysXRayInstrument",
                     [Clang<"xray_always_instrument">]>,
                   Accessor<"neverXRayInstrument",
                     [Clang<"xray_never_instrument">]>];
  let Documentation = [XRayDocs];
  let SimpleHandler = 1;
}

def XRayLogArgs : InheritableAttr {
  let Spellings = [Clang<"xray_log_args">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  // This argument is a count not an index, so it has the same encoding (base
  // 1 including C++ implicit this parameter) at the source and LLVM levels of
  // representation, so ParamIdxArgument is inappropriate.  It is never used
  // at the AST level of representation, so it never needs to be adjusted not
  // to include any C++ implicit this parameter.  Thus, we just store it and
  // use it as an unsigned that never needs adjustment.
  let Args = [UnsignedArgument<"ArgumentCount">];
  let Documentation = [XRayDocs];
}

def PatchableFunctionEntry
    : InheritableAttr,
      TargetSpecificAttr<TargetArch<
          ["aarch64", "aarch64_be", "loongarch32", "loongarch64", "riscv32",
           "riscv64", "x86", "x86_64", "ppc", "ppc64"]>> {
  let Spellings = [GCC<"patchable_function_entry">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Args = [UnsignedArgument<"Count">, DefaultIntArgument<"Offset", 0>];
  let Documentation = [PatchableFunctionEntryDocs];
}

def TLSModel : InheritableAttr {
  let Spellings = [GCC<"tls_model">];
  let Subjects = SubjectList<[TLSVar], ErrorDiag>;
  let Args = [StringArgument<"Model">];
  let Documentation = [TLSModelDocs];
}

def AnalyzerNoReturn : InheritableAttr {
  // TODO: should this attribute be exposed with a [[]] spelling under the clang
  // vendor namespace, or should it use a vendor namespace specific to the
  // analyzer?
  let Spellings = [GNU<"analyzer_noreturn">];
  // TODO: Add subject list.
  let Documentation = [Undocumented];
}

def Annotate : InheritableParamAttr {
  let Spellings = [Clang<"annotate">];
  let Args = [StringArgument<"Annotation">, VariadicExprArgument<"Args">];
  // Ensure that the annotate attribute can be used with
  // '#pragma clang attribute' even though it has no subject list.
  let AdditionalMembers = [{
  static AnnotateAttr *Create(ASTContext &Ctx, llvm::StringRef Annotation, \
              const AttributeCommonInfo &CommonInfo) {
    return AnnotateAttr::Create(Ctx, Annotation, nullptr, 0, CommonInfo);
  }
  static AnnotateAttr *CreateImplicit(ASTContext &Ctx, llvm::StringRef Annotation, \
              const AttributeCommonInfo &CommonInfo) {
    return AnnotateAttr::CreateImplicit(Ctx, Annotation, nullptr, 0, CommonInfo);
  }
  }];
  let PragmaAttributeSupport = 1;
  let AcceptsExprPack = 1;
  let Documentation = [Undocumented];
}

def AnnotateType : TypeAttr {
  let Spellings = [CXX11<"clang", "annotate_type">, C23<"clang", "annotate_type">];
  let Args = [StringArgument<"Annotation">, VariadicExprArgument<"Args">];
  let HasCustomParsing = 1;
  let AcceptsExprPack = 1;
  let Documentation = [AnnotateTypeDocs];
}

def ARMInterrupt : InheritableAttr, TargetSpecificAttr<TargetARM> {
  // NOTE: If you add any additional spellings, M68kInterrupt's,
  // MSP430Interrupt's, MipsInterrupt's and AnyX86Interrupt's spellings
  // must match.
  let Spellings = [GCC<"interrupt">];
  let Args = [EnumArgument<"Interrupt", "InterruptType", /*is_string=*/true,
                           ["IRQ", "FIQ", "SWI", "ABORT", "UNDEF", ""],
                           ["IRQ", "FIQ", "SWI", "ABORT", "UNDEF", "Generic"],
                           1>];
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [ARMInterruptDocs];
}

def AVRInterrupt : InheritableAttr, TargetSpecificAttr<TargetAVR> {
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let ParseKind = "Interrupt";
  let Documentation = [AVRInterruptDocs];
}

def AVRSignal : InheritableAttr, TargetSpecificAttr<TargetAVR> {
  let Spellings = [GCC<"signal">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [AVRSignalDocs];
}

def AsmLabel : InheritableAttr {
  let Spellings = [CustomKeyword<"asm">, CustomKeyword<"__asm__">];
  let Args = [
    // Label specifies the mangled name for the decl.
    StringArgument<"Label">,

    // IsLiteralLabel specifies whether the label is literal (i.e. suppresses
    // the global C symbol prefix) or not. If not, the mangle-suppression prefix
    // ('\01') is omitted from the decl name at the LLVM IR level.
    //
    // Non-literal labels are used by some external AST sources like LLDB.
    BoolArgument<"IsLiteralLabel", /*optional=*/0, /*fake=*/1>
  ];
  let SemaHandler = 0;
  let Documentation = [AsmLabelDocs];
  let AdditionalMembers =
[{
bool isEquivalent(AsmLabelAttr *Other) const {
  return getLabel() == Other->getLabel() && getIsLiteralLabel() == Other->getIsLiteralLabel();
}
}];
}

def Availability : InheritableAttr {
  let Spellings = [Clang<"availability">];
  let Args = [IdentifierArgument<"platform">, VersionArgument<"introduced">,
              VersionArgument<"deprecated">, VersionArgument<"obsoleted">,
              BoolArgument<"unavailable">, StringArgument<"message">,
              BoolArgument<"strict">, StringArgument<"replacement">,
              IntArgument<"priority">, IdentifierArgument<"environment">];
  let AdditionalMembers =
[{static llvm::StringRef getPrettyPlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("android", "Android")
             .Case("fuchsia", "Fuchsia")
             .Case("ios", "iOS")
             .Case("macos", "macOS")
             .Case("tvos", "tvOS")
             .Case("watchos", "watchOS")
             .Case("driverkit", "DriverKit")
             .Case("ios_app_extension", "iOS (App Extension)")
             .Case("macos_app_extension", "macOS (App Extension)")
             .Case("tvos_app_extension", "tvOS (App Extension)")
             .Case("watchos_app_extension", "watchOS (App Extension)")
             .Case("maccatalyst", "macCatalyst")
             .Case("maccatalyst_app_extension", "macCatalyst (App Extension)")
             .Case("xros", "visionOS")
             .Case("xros_app_extension", "visionOS (App Extension)")
             .Case("swift", "Swift")
             .Case("shadermodel", "Shader Model")
             .Case("ohos", "OpenHarmony OS")
             .Default(llvm::StringRef());
}
static llvm::StringRef getPlatformNameSourceSpelling(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("ios", "iOS")
             .Case("macos", "macOS")
             .Case("tvos", "tvOS")
             .Case("watchos", "watchOS")
             .Case("ios_app_extension", "iOSApplicationExtension")
             .Case("macos_app_extension", "macOSApplicationExtension")
             .Case("tvos_app_extension", "tvOSApplicationExtension")
             .Case("watchos_app_extension", "watchOSApplicationExtension")
             .Case("maccatalyst", "macCatalyst")
             .Case("maccatalyst_app_extension", "macCatalystApplicationExtension")
             .Case("xros", "visionOS")
             .Case("xros_app_extension", "visionOSApplicationExtension")
             .Case("zos", "z/OS")
             .Case("shadermodel", "ShaderModel")
             .Default(Platform);
}
static llvm::StringRef canonicalizePlatformName(llvm::StringRef Platform) {
    return llvm::StringSwitch<llvm::StringRef>(Platform)
             .Case("iOS", "ios")
             .Case("macOS", "macos")
             .Case("tvOS", "tvos")
             .Case("watchOS", "watchos")
             .Case("iOSApplicationExtension", "ios_app_extension")
             .Case("macOSApplicationExtension", "macos_app_extension")
             .Case("tvOSApplicationExtension", "tvos_app_extension")
             .Case("watchOSApplicationExtension", "watchos_app_extension")
             .Case("macCatalyst", "maccatalyst")
             .Case("macCatalystApplicationExtension", "maccatalyst_app_extension")
             .Case("visionOS", "xros")
             .Case("visionOSApplicationExtension", "xros_app_extension")
             .Case("visionos", "xros")
             .Case("visionos_app_extension", "xros_app_extension")
             .Case("ShaderModel", "shadermodel")
             .Default(Platform);
}
static llvm::Triple::EnvironmentType getEnvironmentType(llvm::StringRef Environment) {
    return llvm::StringSwitch<llvm::Triple::EnvironmentType>(Environment)
             .Case("pixel", llvm::Triple::Pixel)
             .Case("vertex", llvm::Triple::Vertex)
             .Case("geometry", llvm::Triple::Geometry)
             .Case("hull", llvm::Triple::Hull)
             .Case("domain", llvm::Triple::Domain)
             .Case("compute", llvm::Triple::Compute)
             .Case("raygeneration", llvm::Triple::RayGeneration)
             .Case("intersection", llvm::Triple::Intersection)
             .Case("anyhit", llvm::Triple::AnyHit)
             .Case("closesthit", llvm::Triple::ClosestHit)
             .Case("miss", llvm::Triple::Miss)
             .Case("callable", llvm::Triple::Callable)
             .Case("mesh", llvm::Triple::Mesh)
             .Case("amplification", llvm::Triple::Amplification)
             .Case("library", llvm::Triple::Library)
             .Default(llvm::Triple::UnknownEnvironment);
}
}];
  let HasCustomParsing = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Named]>;
  let Documentation = [AvailabilityDocs];
}

def ExternalSourceSymbol : InheritableAttr {
  let Spellings = [Clang<"external_source_symbol", /*allowInC=*/1,
                   /*version=*/20230206>];
  let Args = [StringArgument<"language", 1>,
              StringArgument<"definedIn", 1>,
              BoolArgument<"generatedDeclaration", 1>,
              StringArgument<"USR", 1>];
  let HasCustomParsing = 1;
  let Subjects = SubjectList<[Named]>;
  let Documentation = [ExternalSourceSymbolDocs];
}

def Blocks : InheritableAttr {
  let Spellings = [Clang<"blocks">];
  let Args = [EnumArgument<"Type", "BlockType", /*is_string=*/true,
                           ["byref"], ["ByRef"]>];
  let Documentation = [Undocumented];
}

def Bounded : IgnoredAttr {
  // Does not have a [[]] spelling because the attribute is ignored.
  let Spellings = [GNU<"bounded">];
}

def CarriesDependency : InheritableParamAttr {
  let Spellings = [GNU<"carries_dependency">,
                   CXX11<"","carries_dependency", 200809>];
  let Subjects = SubjectList<[ParmVar, ObjCMethod, Function], ErrorDiag>;
  let Documentation = [CarriesDependencyDocs];
}

def CDecl : DeclOrTypeAttr {
  let Spellings = [GCC<"cdecl">, CustomKeyword<"__cdecl">, CustomKeyword<"_cdecl">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

// cf_audited_transfer indicates that the given function has been
// audited and has been marked with the appropriate cf_consumed and
// cf_returns_retained attributes.  It is generally applied by
// '#pragma clang arc_cf_code_audited' rather than explicitly.
def CFAuditedTransfer : InheritableAttr {
  let Spellings = [Clang<"cf_audited_transfer">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

// cf_unknown_transfer is an explicit opt-out of cf_audited_transfer.
// It indicates that the function has unknown or unautomatable
// transfer semantics.
def CFUnknownTransfer : InheritableAttr {
  let Spellings = [Clang<"cf_unknown_transfer">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}
def : MutualExclusions<[CFAuditedTransfer, CFUnknownTransfer]>;

def CFReturnsRetained : InheritableAttr {
  let Spellings = [Clang<"cf_returns_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def CFReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"cf_returns_not_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def CFConsumed : InheritableParamAttr {
  let Spellings = [Clang<"cf_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}


// coro_only_destroy_when_complete indicates the coroutines whose return type
// is marked by coro_only_destroy_when_complete can only be destroyed when the
// coroutine completes. Then the space for the destroy functions can be saved.
def CoroOnlyDestroyWhenComplete : InheritableAttr {
  let Spellings = [Clang<"coro_only_destroy_when_complete">];
  let Subjects = SubjectList<[CXXRecord]>;
  let LangOpts = [CPlusPlus];
  let Documentation = [CoroOnlyDestroyWhenCompleteDocs];
  let SimpleHandler = 1;
}

def CoroReturnType : InheritableAttr {
  let Spellings = [Clang<"coro_return_type">];
  let Subjects = SubjectList<[CXXRecord]>;
  let LangOpts = [CPlusPlus];
  let Documentation = [CoroReturnTypeAndWrapperDoc];
  let SimpleHandler = 1;
}

def CoroWrapper : InheritableAttr {
  let Spellings = [Clang<"coro_wrapper">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CPlusPlus];
  let Documentation = [CoroReturnTypeAndWrapperDoc];
  let SimpleHandler = 1;
}

def CoroLifetimeBound : InheritableAttr {
  let Spellings = [Clang<"coro_lifetimebound">];
  let Subjects = SubjectList<[CXXRecord]>;
  let LangOpts = [CPlusPlus];
  let Documentation = [CoroLifetimeBoundDoc];
  let SimpleHandler = 1;
}

def CoroDisableLifetimeBound : InheritableAttr {
  let Spellings = [Clang<"coro_disable_lifetimebound">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CPlusPlus];
  let Documentation = [CoroLifetimeBoundDoc];
  let SimpleHandler = 1;
}

// OSObject-based attributes.
def OSConsumed : InheritableParamAttr {
  let Spellings = [Clang<"os_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetained : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty, ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"os_returns_not_retained">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty, ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetainedOnZero : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained_on_zero">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSReturnsRetainedOnNonZero : InheritableAttr {
  let Spellings = [Clang<"os_returns_retained_on_non_zero">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def OSConsumesThis : InheritableAttr {
  let Spellings = [Clang<"os_consumes_this">];
  let Subjects = SubjectList<[NonStaticCXXMethod]>;
  let Documentation = [RetainBehaviorDocs];
  let SimpleHandler = 1;
}

def Cleanup : InheritableAttr {
  let Spellings = [GCC<"cleanup">];
  let Args = [DeclArgument<Function, "FunctionDecl">];
  let Subjects = SubjectList<[LocalVar]>;
  let Documentation = [CleanupDocs];
}

def CmseNSEntry : InheritableAttr, TargetSpecificAttr<TargetARM> {
  let Spellings = [GNU<"cmse_nonsecure_entry">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [Cmse];
  let Documentation = [ArmCmseNSEntryDocs];
}

def CmseNSCall : TypeAttr, TargetSpecificAttr<TargetARM> {
  let Spellings = [GNU<"cmse_nonsecure_call">];
  let LangOpts = [Cmse];
  let Documentation = [ArmCmseNSCallDocs];
}

def Cold : InheritableAttr {
  let Spellings = [GCC<"cold">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [ColdFunctionEntryDocs];
  let SimpleHandler = 1;
}

def Common : InheritableAttr {
  let Spellings = [GCC<"common">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [Undocumented];
}

def Const : InheritableAttr {
  let Spellings = [GCC<"const">, GCC<"__const">];
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def ConstInit : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it requires the
  // CPlusPlus language option.
  let Spellings = [CustomKeyword<"constinit">,
                   Clang<"require_constant_initialization", 0>];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Accessors = [Accessor<"isConstinit", [CustomKeyword<"constinit">]>];
  let Documentation = [ConstInitDocs];
  let LangOpts = [CPlusPlus];
  let SimpleHandler = 1;
}

def Constructor : InheritableAttr {
  let Spellings = [GCC<"constructor">];
  let Args = [DefaultIntArgument<"Priority", 65535>];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CtorDtorDocs];
}

def CPUSpecific : InheritableAttr {
  let Spellings = [Clang<"cpu_specific">, Declspec<"cpu_specific">];
  let Args = [VariadicIdentifierArgument<"Cpus">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CPUSpecificCPUDispatchDocs];
  let AdditionalMembers = [{
    IdentifierInfo *getCPUName(unsigned Index) const {
      return *(cpus_begin() + Index);
    }
  }];
}

def CPUDispatch : InheritableAttr {
  let Spellings = [Clang<"cpu_dispatch">, Declspec<"cpu_dispatch">];
  let Args = [VariadicIdentifierArgument<"Cpus">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CPUSpecificCPUDispatchDocs];
}

// CUDA attributes are spelled __attribute__((attr)) or __declspec(__attr__),
// and they do not receive a [[]] spelling.
def CUDAConstant : InheritableAttr {
  let Spellings = [GNU<"constant">, Declspec<"__constant__">];
  let Subjects = SubjectList<[Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDACudartBuiltin : IgnoredAttr {
  let Spellings = [GNU<"cudart_builtin">, Declspec<"__cudart_builtin__">];
  let LangOpts = [CUDA];
}

def CUDADevice : InheritableAttr {
  let Spellings = [GNU<"device">, Declspec<"__device__">];
  let Subjects = SubjectList<[Function, Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}

def CUDADeviceBuiltin : IgnoredAttr {
  let Spellings = [GNU<"device_builtin">, Declspec<"__device_builtin__">];
  let LangOpts = [CUDA];
}

def CUDADeviceBuiltinSurfaceType : InheritableAttr {
  let Spellings = [GNU<"device_builtin_surface_type">,
                   Declspec<"__device_builtin_surface_type__">];
  let LangOpts = [CUDA];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [CUDADeviceBuiltinSurfaceTypeDocs];
  let MeaningfulToClassTemplateDefinition = 1;
  let SimpleHandler = 1;
}

def CUDADeviceBuiltinTextureType : InheritableAttr {
  let Spellings = [GNU<"device_builtin_texture_type">,
                   Declspec<"__device_builtin_texture_type__">];
  let LangOpts = [CUDA];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [CUDADeviceBuiltinTextureTypeDocs];
  let MeaningfulToClassTemplateDefinition = 1;
  let SimpleHandler = 1;
}
def : MutualExclusions<[CUDADeviceBuiltinSurfaceType,
                        CUDADeviceBuiltinTextureType]>;

def CUDAGlobal : InheritableAttr {
  let Spellings = [GNU<"global">, Declspec<"__global__">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}
def : MutualExclusions<[CUDADevice, CUDAGlobal]>;

def CUDAHost : InheritableAttr {
  let Spellings = [GNU<"host">, Declspec<"__host__">];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}
def : MutualExclusions<[CUDAGlobal, CUDAHost]>;

def NVPTXKernel : InheritableAttr, TargetSpecificAttr<TargetNVPTX> {
  let Spellings = [Clang<"nvptx_kernel">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def HIPManaged : InheritableAttr {
  let Spellings = [GNU<"managed">, Declspec<"__managed__">];
  let Subjects = SubjectList<[Var]>;
  let LangOpts = [HIP];
  let Documentation = [HIPManagedAttrDocs];
}

def CUDAInvalidTarget : InheritableAttr {
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let LangOpts = [CUDA];
  let Documentation = [InternalOnly];
}

def CUDALaunchBounds : InheritableAttr {
  let Spellings = [GNU<"launch_bounds">, Declspec<"__launch_bounds__">];
  let Args = [ExprArgument<"MaxThreads">, ExprArgument<"MinBlocks", 1>,
              ExprArgument<"MaxBlocks", 1>];
  let LangOpts = [CUDA];
  let Subjects = SubjectList<[ObjCMethod, FunctionLike]>;
  // An AST node is created for this attribute, but is not used by other parts
  // of the compiler. However, this node needs to exist in the AST because
  // non-LLVM backends may be relying on the attribute's presence.
  let Documentation = [Undocumented];
}

def CUDAShared : InheritableAttr {
  let Spellings = [GNU<"shared">, Declspec<"__shared__">];
  let Subjects = SubjectList<[Var]>;
  let LangOpts = [CUDA];
  let Documentation = [Undocumented];
}
def : MutualExclusions<[CUDAConstant, CUDAShared, HIPManaged]>;

def SYCLKernel : InheritableAttr {
  let Spellings = [Clang<"sycl_kernel">];
  let Subjects = SubjectList<[FunctionTmpl]>;
  let LangOpts = [SYCL];
  let Documentation = [SYCLKernelDocs];
}

def SYCLSpecialClass: InheritableAttr {
  let Spellings = [Clang<"sycl_special_class">];
  let Subjects = SubjectList<[CXXRecord]>;
  let LangOpts = [SYCL];
  let Documentation = [SYCLSpecialClassDocs];
}

def C11NoReturn : InheritableAttr {
  let Spellings = [CustomKeyword<"_Noreturn">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let SemaHandler = 0;
  let Documentation = [C11NoReturnDocs];
}

def CXX11NoReturn : InheritableAttr {
  let Spellings = [CXX11<"", "noreturn", 200809>,
                   C23<"", "noreturn", 202202>, C23<"", "_Noreturn", 202202>];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [CXX11NoReturnDocs];
}

def NonBlocking : TypeAttr {
  let Spellings = [Clang<"nonblocking">];
  let Args = [ExprArgument<"Cond", /*optional*/1>];
  let Documentation = [NonBlockingDocs];
}

def NonAllocating : TypeAttr {
  let Spellings = [Clang<"nonallocating">];
  let Args = [ExprArgument<"Cond", /*optional*/1>];
  let Documentation = [NonAllocatingDocs];
}

def Blocking : TypeAttr {
  let Spellings = [Clang<"blocking">];
  let Documentation = [BlockingDocs];
}

def Allocating : TypeAttr {
  let Spellings = [Clang<"allocating">];
  let Documentation = [AllocatingDocs];
}

// Similar to CUDA, OpenCL attributes do not receive a [[]] spelling because
// the specification does not expose them with one currently.
def OpenCLKernel : InheritableAttr {
  let Spellings = [CustomKeyword<"__kernel">, CustomKeyword<"kernel">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def OpenCLUnrollHint : StmtAttr {
  let Spellings = [GNU<"opencl_unroll_hint">];
  let Subjects = SubjectList<[ForStmt, CXXForRangeStmt, WhileStmt, DoStmt],
                             ErrorDiag, "'for', 'while', and 'do' statements">;
  let Args = [UnsignedArgument<"UnrollHint", /*opt*/1>];
  let Documentation = [OpenCLUnrollHintDocs];
}

def OpenCLIntelReqdSubGroupSize: InheritableAttr {
  let Spellings = [GNU<"intel_reqd_sub_group_size">];
  let Args = [UnsignedArgument<"SubGroupSize">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [OpenCLIntelReqdSubGroupSizeDocs];
}

// This attribute is both a type attribute, and a declaration attribute (for
// parameter variables).
def OpenCLAccess : Attr {
  let Spellings = [CustomKeyword<"__read_only">, CustomKeyword<"read_only">,
                   CustomKeyword<"__write_only">, CustomKeyword<"write_only">,
                   CustomKeyword<"__read_write">, CustomKeyword<"read_write">];
  let Subjects = SubjectList<[ParmVar, TypedefName], ErrorDiag>;
  let Accessors = [Accessor<"isReadOnly", [CustomKeyword<"__read_only">,
                                           CustomKeyword<"read_only">]>,
                   Accessor<"isReadWrite", [CustomKeyword<"__read_write">,
                                            CustomKeyword<"read_write">]>,
                   Accessor<"isWriteOnly", [CustomKeyword<"__write_only">,
                                            CustomKeyword<"write_only">]>];
  let Documentation = [OpenCLAccessDocs];
}

def OpenCLPrivateAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"__private">, CustomKeyword<"private">,
                   Clang<"opencl_private">];
  let Documentation = [OpenCLAddressSpacePrivateDocs];
}

def OpenCLGlobalAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"__global">, CustomKeyword<"global">,
                   Clang<"opencl_global">];
  let Documentation = [OpenCLAddressSpaceGlobalDocs];
}

def OpenCLGlobalDeviceAddressSpace : TypeAttr {
  let Spellings = [Clang<"opencl_global_device">];
  let Documentation = [OpenCLAddressSpaceGlobalExtDocs];
}

def OpenCLGlobalHostAddressSpace : TypeAttr {
  let Spellings = [Clang<"opencl_global_host">];
  let Documentation = [OpenCLAddressSpaceGlobalExtDocs];
}

def OpenCLLocalAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"__local">, CustomKeyword<"local">,
                   Clang<"opencl_local">];
  let Documentation = [OpenCLAddressSpaceLocalDocs];
}

def OpenCLConstantAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"__constant">, CustomKeyword<"constant">,
                   Clang<"opencl_constant">];
  let Documentation = [OpenCLAddressSpaceConstantDocs];
}

def OpenCLGenericAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"__generic">, CustomKeyword<"generic">,
                   Clang<"opencl_generic">];
  let Documentation = [OpenCLAddressSpaceGenericDocs];
}

def OpenCLNoSVM : Attr {
  let Spellings = [GNU<"nosvm">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [OpenCLNoSVMDocs];
  let LangOpts = [OpenCL];
  let ASTNode = 0;
}

def RenderScriptKernel : Attr {
  let Spellings = [GNU<"kernel">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [RenderScriptKernelAttributeDocs];
  let LangOpts = [RenderScript];
  let SimpleHandler = 1;
}

def Deprecated : InheritableAttr {
  let Spellings = [GCC<"deprecated">, Declspec<"deprecated">,
                   CXX11<"","deprecated", 201309>,
                   C23<"", "deprecated", 201904>];
  let Args = [StringArgument<"Message", 1>,
              // An optional string argument that enables us to provide a
              // Fix-It.
              StringArgument<"Replacement", 1>];
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [DeprecatedDocs];
}

def Destructor : InheritableAttr {
  let Spellings = [GCC<"destructor">];
  let Args = [DefaultIntArgument<"Priority", 65535>];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CtorDtorDocs];
}

def EmptyBases : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"empty_bases">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [EmptyBasesDocs];
  let SimpleHandler = 1;
}

def AllocSize : InheritableAttr {
  let Spellings = [GCC<"alloc_size">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let Args = [ParamIdxArgument<"ElemSizeParam">,
              ParamIdxArgument<"NumElemsParam", /*opt*/ 1>];
  let TemplateDependent = 1;
  let Documentation = [AllocSizeDocs];
}

def EnableIf : InheritableAttr {
  // Does not have a [[]] spelling because this attribute requires the ability
  // to parse function arguments but the attribute is not written in the type
  // position.
  let Spellings = [GNU<"enable_if">];
  let Subjects = SubjectList<[Function]>;
  let Args = [ExprArgument<"Cond">, StringArgument<"Message">];
  let TemplateDependent = 1;
  let Documentation = [EnableIfDocs];
}

def ExtVectorType : Attr {
  // This is an OpenCL-related attribute and does not receive a [[]] spelling.
  let Spellings = [GNU<"ext_vector_type">];
  // FIXME: This subject list is wrong; this is a type attribute.
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Args = [ExprArgument<"NumElements">];
  let ASTNode = 0;
  let Documentation = [Undocumented];
  // This is a type attribute with an incorrect subject list, so should not be
  // permitted by #pragma clang attribute.
  let PragmaAttributeSupport = 0;
}

def FallThrough : StmtAttr {
  let Spellings = [CXX11<"", "fallthrough", 201603>,
                   C23<"", "fallthrough", 201910>,
                   CXX11<"clang", "fallthrough">, GCC<"fallthrough">];
  // The attribute only applies to a NullStmt, but we have special fix-it
  // behavior if applied to a case label.
  let Subjects = SubjectList<[NullStmt, SwitchCase], ErrorDiag,
                             "empty statements">;
  let Documentation = [FallthroughDocs];
}

def Likely : StmtAttr {
  let Spellings = [CXX11<"", "likely", 201803>, C23<"clang", "likely">];
  let Documentation = [LikelihoodDocs];
}

def Unlikely : StmtAttr {
  let Spellings = [CXX11<"", "unlikely", 201803>, C23<"clang", "unlikely">];
  let Documentation = [LikelihoodDocs];
}
def : MutualExclusions<[Likely, Unlikely]>;

def CXXAssume : StmtAttr {
  let Spellings = [CXX11<"", "assume", 202207>, Clang<"assume">];
  let Subjects = SubjectList<[NullStmt], ErrorDiag, "empty statements">;
  let Args = [ExprArgument<"Assumption">];
  let Documentation = [CXXAssumeDocs];
  let HasCustomParsing = 1;
}

def NoMerge : DeclOrStmtAttr {
  let Spellings = [Clang<"nomerge">];
  let Documentation = [NoMergeDocs];
  let Subjects = SubjectList<[Function, Stmt, Var], ErrorDiag,
                             "functions, statements and variables">;
}

def MustTail : StmtAttr {
  let Spellings = [Clang<"musttail">];
  let Documentation = [MustTailDocs];
  let Subjects = SubjectList<[ReturnStmt], ErrorDiag, "return statements">;
}

def FastCall : DeclOrTypeAttr {
  let Spellings = [GCC<"fastcall">, CustomKeyword<"__fastcall">,
                   CustomKeyword<"_fastcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [FastCallDocs];
}

def RegCall : DeclOrTypeAttr {
  let Spellings = [GCC<"regcall">, CustomKeyword<"__regcall">];
  let Documentation = [RegCallDocs];
}

def Final : InheritableAttr {
  let Spellings = [CustomKeyword<"final">, CustomKeyword<"sealed">];
  let Accessors = [Accessor<"isSpelledAsSealed", [CustomKeyword<"sealed">]>];
  let SemaHandler = 0;
  // Omitted from docs, since this is language syntax, not an attribute, as far
  // as users are concerned.
  let Documentation = [InternalOnly];
}

def MinSize : InheritableAttr {
  let Spellings = [Clang<"minsize">];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [MinSizeDocs];
}

def FlagEnum : InheritableAttr {
  let Spellings = [Clang<"flag_enum">];
  let Subjects = SubjectList<[Enum]>;
  let Documentation = [FlagEnumDocs];
  let SimpleHandler = 1;
}

def EnumExtensibility : InheritableAttr {
  let Spellings = [Clang<"enum_extensibility">];
  let Subjects = SubjectList<[Enum]>;
  let Args = [EnumArgument<"Extensibility", "Kind", /*is_string=*/false,
              ["closed", "open"], ["Closed", "Open"]>];
  let Documentation = [EnumExtensibilityDocs];
}

def Flatten : InheritableAttr {
  let Spellings = [GCC<"flatten">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [FlattenDocs];
  let SimpleHandler = 1;
}

def Format : InheritableAttr {
  let Spellings = [GCC<"format">];
  let Args = [IdentifierArgument<"Type">, IntArgument<"FormatIdx">,
              IntArgument<"FirstArg">];
  let Subjects = SubjectList<[ObjCMethod, Block, HasFunctionProto]>;
  let Documentation = [FormatDocs];
}

def FormatArg : InheritableAttr {
  let Spellings = [GCC<"format_arg">];
  let Args = [ParamIdxArgument<"FormatIdx">];
  let Subjects = SubjectList<[ObjCMethod, HasFunctionProto]>;
  let Documentation = [Undocumented];
}

def Callback : InheritableAttr {
  let Spellings = [Clang<"callback">];
  let Args = [VariadicParamOrParamIdxArgument<"Encoding">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [CallbackDocs];
}

def GNUInline : InheritableAttr {
  let Spellings = [GCC<"gnu_inline">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [GnuInlineDocs];
}

def Hot : InheritableAttr {
  let Spellings = [GCC<"hot">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [HotFunctionEntryDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[Hot, Cold]>;

def IBAction : InheritableAttr {
  let Spellings = [Clang<"ibaction">];
  let Subjects = SubjectList<[ObjCInstanceMethod]>;
  // An AST node is created for this attribute, but is not used by other parts
  // of the compiler. However, this node needs to exist in the AST because
  // external tools rely on it.
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def IBOutlet : InheritableAttr {
  let Spellings = [Clang<"iboutlet">];
//  let Subjects = [ObjCIvar, ObjCProperty];
  let Documentation = [Undocumented];
}

def IBOutletCollection : InheritableAttr {
  let Spellings = [Clang<"iboutletcollection">];
  let Args = [TypeArgument<"Interface", 1>];
//  let Subjects = [ObjCIvar, ObjCProperty];
  let Documentation = [Undocumented];
}

def IFunc : Attr, TargetSpecificAttr<TargetELFOrMachO> {
  let Spellings = [GCC<"ifunc">];
  let Args = [StringArgument<"Resolver">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [IFuncDocs];
}

def Restrict : InheritableAttr {
  let Spellings = [Declspec<"restrict">, GCC<"malloc">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [RestrictDocs];
}

def LayoutVersion : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"layout_version">];
  let Args = [UnsignedArgument<"Version">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [LayoutVersionDocs];
}

def Leaf : InheritableAttr {
  let Spellings = [GCC<"leaf">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [LeafDocs];
  let SimpleHandler = 1;
}

def LifetimeBound : DeclOrTypeAttr {
  let Spellings = [Clang<"lifetimebound", 0>];
  let Subjects = SubjectList<[ParmVar, ImplicitObjectParameter], ErrorDiag>;
  let Documentation = [LifetimeBoundDocs];
  let LangOpts = [CPlusPlus];
  let SimpleHandler = 1;
}

def TrivialABI : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it requires the
  // CPlusPlus language option.
  let Spellings = [Clang<"trivial_abi", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [TrivialABIDocs];
  let LangOpts = [CPlusPlus];
  let SimpleHandler = 1;
}

def MaxFieldAlignment : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [UnsignedArgument<"Alignment">];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def MayAlias : InheritableAttr {
  // FIXME: this is a type attribute in GCC, but a declaration attribute here.
  let Spellings = [GCC<"may_alias">];
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def MIGServerRoutine : InheritableAttr {
  let Spellings = [Clang<"mig_server_routine">];
  let Subjects = SubjectList<[Function, ObjCMethod, Block]>;
  let Documentation = [MIGConventionDocs];
}

def MSABI : DeclOrTypeAttr {
  let Spellings = [GCC<"ms_abi">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [MSABIDocs];
}

def MSP430Interrupt : InheritableAttr, TargetSpecificAttr<TargetMSP430> {
  // NOTE: If you add any additional spellings, ARMInterrupt's, M68kInterrupt's,
  // MipsInterrupt's and AnyX86Interrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Args = [UnsignedArgument<"Number">];
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def Mips16 : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"mips16">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def MipsInterrupt : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  // NOTE: If you add any additional spellings, ARMInterrupt's,
  // M68kInterrupt's, MSP430Interrupt's and AnyX86Interrupt's spellings
  // must match.
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let Args = [EnumArgument<"Interrupt", "InterruptType", /*is_string=*/true,
                           ["vector=sw0", "vector=sw1", "vector=hw0",
                            "vector=hw1", "vector=hw2", "vector=hw3",
                            "vector=hw4", "vector=hw5", "eic", ""],
                           ["sw0", "sw1", "hw0", "hw1", "hw2", "hw3",
                            "hw4", "hw5", "eic", "eic"]
                           >];
  let ParseKind = "Interrupt";
  let Documentation = [MipsInterruptDocs];
}
def : MutualExclusions<[Mips16, MipsInterrupt]>;

def MicroMips : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"micromips">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MicroMipsDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[Mips16, MicroMips]>;

def MipsLongCall : InheritableAttr, TargetSpecificAttr<TargetAnyMips> {
  let Spellings = [GCC<"long_call">, GCC<"far">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [MipsLongCallStyleDocs];
  let SimpleHandler = 1;
}

def MipsShortCall : InheritableAttr, TargetSpecificAttr<TargetAnyMips> {
  let Spellings = [GCC<"short_call">, GCC<"near">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [MipsShortCallStyleDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[MipsLongCall, MipsShortCall]>;

def M68kInterrupt : InheritableAttr, TargetSpecificAttr<TargetM68k> {
  // NOTE: If you add any additional spellings, ARMInterrupt's, MipsInterrupt's
  // MSP430Interrupt's and AnyX86Interrupt's spellings must match.
  let Spellings = [GNU<"interrupt">];
  let Args = [UnsignedArgument<"Number">];
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def Mode : Attr {
  let Spellings = [GCC<"mode">];
  let Subjects = SubjectList<[Var, Enum, TypedefName, Field], ErrorDiag>;
  let Args = [IdentifierArgument<"Mode">];
  let Documentation = [Undocumented];
  // This is notionally a type attribute, which #pragma clang attribute
  // generally does not support.
  let PragmaAttributeSupport = 0;
}

def Naked : InheritableAttr {
  let Spellings = [GCC<"naked">, Declspec<"naked">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def NeonPolyVectorType : TypeAttr {
  let Spellings = [Clang<"neon_polyvector_type">];
  let Args = [IntArgument<"NumElements">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def NeonVectorType : TypeAttr {
  let Spellings = [Clang<"neon_vector_type">];
  let Args = [IntArgument<"NumElements">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def ArmSveVectorBits : TypeAttr {
  let Spellings = [GNU<"arm_sve_vector_bits">];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Args = [UnsignedArgument<"NumBits">];
  let Documentation = [ArmSveVectorBitsDocs];
  let PragmaAttributeSupport = 0;
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def ArmMveStrictPolymorphism : TypeAttr, TargetSpecificAttr<TargetARM> {
  let Spellings = [Clang<"__clang_arm_mve_strict_polymorphism">];
  let Documentation = [ArmMveStrictPolymorphismDocs];
}

def NoUniqueAddress : InheritableAttr {
  let Subjects = SubjectList<[NonBitField], ErrorDiag>;
  let Spellings = [CXX11<"", "no_unique_address", 201803>, CXX11<"msvc", "no_unique_address", 201803>];
  let TargetSpecificSpellings = [
    TargetSpecificSpelling<TargetItaniumCXXABI, [CXX11<"", "no_unique_address", 201803>]>,
    TargetSpecificSpelling<TargetMicrosoftCXXABI, [CXX11<"msvc", "no_unique_address", 201803>]>,
  ];
  let Documentation = [NoUniqueAddressDocs];
}

def ReturnsTwice : InheritableAttr {
  let Spellings = [GCC<"returns_twice">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def DisableTailCalls : InheritableAttr {
  let Spellings = [Clang<"disable_tail_calls">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Documentation = [DisableTailCallsDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[Naked, DisableTailCalls]>;

def NoAlias : InheritableAttr {
  let Spellings = [Declspec<"noalias">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoAliasDocs];
  let SimpleHandler = 1;
}

def NoCommon : InheritableAttr {
  let Spellings = [GCC<"nocommon">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def NoDebug : InheritableAttr {
  let Spellings = [GCC<"nodebug">];
  let Subjects = SubjectList<[TypedefName, FunctionLike, ObjCMethod, NonParmVar]>;
  let Documentation = [NoDebugDocs];
}

def StandaloneDebug : InheritableAttr {
  let Spellings = [Clang<"standalone_debug", /*allowInC =*/0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [StandaloneDebugDocs];
  let SimpleHandler = 1;
  let LangOpts = [CPlusPlus];
}

def NoDuplicate : InheritableAttr {
  let Spellings = [Clang<"noduplicate">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoDuplicateDocs];
  let SimpleHandler = 1;
}

def Convergent : InheritableAttr {
  let Spellings = [Clang<"convergent">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [ConvergentDocs];
  let SimpleHandler = 1;
}

def NoInline : DeclOrStmtAttr {
  let Spellings = [CustomKeyword<"__noinline__">, GCC<"noinline">,
                   CXX11<"clang", "noinline">, C23<"clang", "noinline">,
                   CXX11<"msvc", "noinline">, C23<"msvc", "noinline">,
                   Declspec<"noinline">];
  let Accessors = [Accessor<"isStmtNoInline", [CXX11<"clang", "noinline">,
                                               C23<"clang", "noinline">,
                                               CXX11<"msvc", "noinline">,
                                               C23<"msvc", "noinline">]>];
  let Documentation = [NoInlineDocs];
  let Subjects = SubjectList<[Function, Stmt], WarnDiag,
                             "functions and statements">;
  let SimpleHandler = 1;
}

def NoMips16 : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"nomips16">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def NoMicroMips : InheritableAttr, TargetSpecificAttr<TargetMips32> {
  let Spellings = [GCC<"nomicromips">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MicroMipsDocs];
  let SimpleHandler = 1;
}

def RISCVInterrupt : InheritableAttr, TargetSpecificAttr<TargetRISCV> {
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[Function]>;
  let Args = [EnumArgument<"Interrupt", "InterruptType", /*is_string=*/true,
                           ["supervisor", "machine"],
                           ["supervisor", "machine"],
                           1>];
  let ParseKind = "Interrupt";
  let Documentation = [RISCVInterruptDocs];
}

def RISCVRVVVectorBits : TypeAttr {
  let Spellings = [GNU<"riscv_rvv_vector_bits">];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Args = [UnsignedArgument<"NumBits">];
  let Documentation = [RISCVRVVVectorBitsDocs];
  let PragmaAttributeSupport = 0;
  // Represented as VectorType instead.
  let ASTNode = 0;
}

// This is not a TargetSpecificAttr so that is silently accepted and
// ignored on other targets as encouraged by the OpenCL spec.
//
// See OpenCL 1.2 6.11.5: "It is our intention that a particular
// implementation of OpenCL be free to ignore all attributes and the
// resulting executable binary will produce the same result."
//
// However, only AMD GPU targets will emit the corresponding IR
// attribute.
//
// FIXME: This provides a sub-optimal error message if you attempt to
// use this in CUDA, since CUDA does not use the same terminology.
//
// FIXME: SubjectList should be for OpenCLKernelFunction, but is not to
// workaround needing to see kernel attribute before others to know if
// this should be rejected on non-kernels.

def AMDGPUFlatWorkGroupSize : InheritableAttr {
  let Spellings = [Clang<"amdgpu_flat_work_group_size", 0>];
  let Args = [ExprArgument<"Min">, ExprArgument<"Max">];
  let Documentation = [AMDGPUFlatWorkGroupSizeDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUWavesPerEU : InheritableAttr {
  let Spellings = [Clang<"amdgpu_waves_per_eu", 0>];
  let Args = [ExprArgument<"Min">, ExprArgument<"Max", 1>];
  let Documentation = [AMDGPUWavesPerEUDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUNumSGPR : InheritableAttr {
  let Spellings = [Clang<"amdgpu_num_sgpr", 0>];
  let Args = [UnsignedArgument<"NumSGPR">];
  let Documentation = [AMDGPUNumSGPRNumVGPRDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUNumVGPR : InheritableAttr {
  let Spellings = [Clang<"amdgpu_num_vgpr", 0>];
  let Args = [UnsignedArgument<"NumVGPR">];
  let Documentation = [AMDGPUNumSGPRNumVGPRDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUMaxNumWorkGroups : InheritableAttr {
  let Spellings = [Clang<"amdgpu_max_num_work_groups", 0>];
  let Args = [ExprArgument<"MaxNumWorkGroupsX">, ExprArgument<"MaxNumWorkGroupsY", 1>, ExprArgument<"MaxNumWorkGroupsZ", 1>];
  let Documentation = [AMDGPUMaxNumWorkGroupsDocs];
  let Subjects = SubjectList<[Function], ErrorDiag, "kernel functions">;
}

def AMDGPUKernelCall : DeclOrTypeAttr {
  let Spellings = [Clang<"amdgpu_kernel">];
  let Documentation = [Undocumented];
}

def BPFPreserveAccessIndex : InheritableAttr,
                             TargetSpecificAttr<TargetBPF>  {
  let Spellings = [Clang<"preserve_access_index">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Documentation = [BPFPreserveAccessIndexDocs];
  let LangOpts = [COnly];
}

def BPFPreserveStaticOffset : InheritableAttr,
                              TargetSpecificAttr<TargetBPF>  {
  let Spellings = [Clang<"preserve_static_offset">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Documentation = [BPFPreserveStaticOffsetDocs];
  let LangOpts = [COnly];
}

def BTFDeclTag : InheritableAttr {
  let Spellings = [Clang<"btf_decl_tag">];
  let Args = [StringArgument<"BTFDeclTag">];
  let Subjects = SubjectList<[Var, Function, Record, Field, TypedefName],
                             ErrorDiag>;
  let Documentation = [BTFDeclTagDocs];
  let LangOpts = [COnly];
}

def BTFTypeTag : TypeAttr {
  let Spellings = [Clang<"btf_type_tag">];
  let Args = [StringArgument<"BTFTypeTag">];
  let Documentation = [BTFTypeTagDocs];
  let LangOpts = [COnly];
}

def WebAssemblyExportName : InheritableAttr,
                            TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [Clang<"export_name">];
  let Args = [StringArgument<"ExportName">];
  let Documentation = [WebAssemblyExportNameDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
}

def WebAssemblyImportModule : InheritableAttr,
                              TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [Clang<"import_module">];
  let Args = [StringArgument<"ImportModule">];
  let Documentation = [WebAssemblyImportModuleDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
}

def WebAssemblyImportName : InheritableAttr,
                            TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [Clang<"import_name">];
  let Args = [StringArgument<"ImportName">];
  let Documentation = [WebAssemblyImportNameDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
}

def NoSplitStack : InheritableAttr {
  let Spellings = [GCC<"no_split_stack">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [NoSplitStackDocs];
  let SimpleHandler = 1;
}

def NonNull : InheritableParamAttr {
  let Spellings = [GCC<"nonnull">];
  let Subjects = SubjectList<[ObjCMethod, HasFunctionProto, ParmVar], WarnDiag,
                             "functions, methods, and parameters">;
  let Args = [VariadicParamIdxArgument<"Args">];
  let AdditionalMembers = [{
    bool isNonNull(unsigned IdxAST) const {
      if (!args_size())
        return true;
      return llvm::any_of(args(), [=](const ParamIdx &Idx) {
        return Idx.getASTIndex() == IdxAST;
      });
    }
  }];
  // FIXME: We should merge duplicates into a single nonnull attribute.
  let InheritEvenIfAlreadyPresent = 1;
  let Documentation = [NonNullDocs];
}

def ReturnsNonNull : InheritableAttr {
  let Spellings = [GCC<"returns_nonnull">];
  let Subjects = SubjectList<[ObjCMethod, Function]>;
  let Documentation = [ReturnsNonNullDocs];
}

def CalledOnce : Attr {
  let Spellings = [Clang<"called_once">];
  let Subjects = SubjectList<[ParmVar]>;
  let LangOpts = [ObjC];
  let Documentation = [CalledOnceDocs];
}

// pass_object_size(N) indicates that the parameter should have
// __builtin_object_size with Type=N evaluated on the parameter at the callsite.
def PassObjectSize : InheritableParamAttr {
  let Spellings = [Clang<"pass_object_size">,
                   Clang<"pass_dynamic_object_size">];
  let Accessors = [Accessor<"isDynamic", [Clang<"pass_dynamic_object_size">]>];
  let Args = [IntArgument<"Type">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [PassObjectSizeDocs];
}

// Nullability type attributes.
def TypeNonNull : TypeAttr {
  let Spellings = [CustomKeyword<"_Nonnull">];
  let Documentation = [TypeNonNullDocs];
}

def TypeNullable : DeclOrTypeAttr {
  let Spellings = [CustomKeyword<"_Nullable">];
  let Documentation = [TypeNullableDocs];
//  let Subjects = SubjectList<[CXXRecord], ErrorDiag>;
}

def TypeNullableResult : TypeAttr {
  let Spellings = [CustomKeyword<"_Nullable_result">];
  let Documentation = [TypeNullableResultDocs];
}

def TypeNullUnspecified : TypeAttr {
  let Spellings = [CustomKeyword<"_Null_unspecified">];
  let Documentation = [TypeNullUnspecifiedDocs];
}

def CountedBy : DeclOrTypeAttr {
  let Spellings = [Clang<"counted_by">];
  let Subjects = SubjectList<[Field], ErrorDiag>;
  let Args = [ExprArgument<"Count">, IntArgument<"NestedLevel", 1>];
  let LateParsed = LateAttrParseExperimentalExt;
  let ParseArgumentsAsUnevaluated = 1;
  let Documentation = [CountedByDocs];
  let LangOpts = [COnly];
}

def CountedByOrNull : DeclOrTypeAttr {
  let Spellings = [Clang<"counted_by_or_null">];
  let Subjects = SubjectList<[Field], ErrorDiag>;
  let Args = [ExprArgument<"Count">, IntArgument<"NestedLevel", 1>];
  let LateParsed = LateAttrParseExperimentalExt;
  let ParseArgumentsAsUnevaluated = 1;
  let Documentation = [CountedByDocs];
  let LangOpts = [COnly];
}

def SizedBy : DeclOrTypeAttr {
  let Spellings = [Clang<"sized_by">];
  let Subjects = SubjectList<[Field], ErrorDiag>;
  let Args = [ExprArgument<"Size">, IntArgument<"NestedLevel", 1>];
  let LateParsed = LateAttrParseExperimentalExt;
  let ParseArgumentsAsUnevaluated = 1;
  let Documentation = [CountedByDocs];
  let LangOpts = [COnly];
}

def SizedByOrNull : DeclOrTypeAttr {
  let Spellings = [Clang<"sized_by_or_null">];
  let Subjects = SubjectList<[Field], ErrorDiag>;
  let Args = [ExprArgument<"Size">, IntArgument<"NestedLevel", 1>];
  let LateParsed = LateAttrParseExperimentalExt;
  let ParseArgumentsAsUnevaluated = 1;
  let Documentation = [CountedByDocs];
  let LangOpts = [COnly];
}

// This is a marker used to indicate that an __unsafe_unretained qualifier was
// ignored because ARC is not enabled. The usual representation for this
// qualifier is as an ObjCOwnership attribute with Kind == "none".
def ObjCInertUnsafeUnretained : TypeAttr {
  let Spellings = [CustomKeyword<"__unsafe_unretained">];
  let Documentation = [InternalOnly];
}

def ObjCKindOf : TypeAttr {
  let Spellings = [CustomKeyword<"__kindof">];
  let Documentation = [Undocumented];
}

def NoEscape : Attr {
  let Spellings = [Clang<"noescape">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [NoEscapeDocs];
}

def MaybeUndef : InheritableAttr {
  let Spellings = [Clang<"maybe_undef">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [MaybeUndefDocs];
  let SimpleHandler = 1;
}

def AssumeAligned : InheritableAttr {
  let Spellings = [GCC<"assume_aligned">];
  let Subjects = SubjectList<[ObjCMethod, Function]>;
  let Args = [ExprArgument<"Alignment">, ExprArgument<"Offset", 1>];
  let Documentation = [AssumeAlignedDocs];
}

def AllocAlign : InheritableAttr {
  let Spellings = [GCC<"alloc_align">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let Args = [ParamIdxArgument<"ParamIndex">];
  let Documentation = [AllocAlignDocs];
}

def NoReturn : InheritableAttr {
  let Spellings = [GCC<"noreturn">, Declspec<"noreturn">];
  // FIXME: Does GCC allow this on the function instead?
  let Documentation = [Undocumented];
}

def NoInstrumentFunction : InheritableAttr {
  let Spellings = [GCC<"no_instrument_function">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def NoProfileFunction : InheritableAttr {
  let Spellings = [GCC<"no_profile_instrument_function">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoProfileInstrumentFunctionDocs];
  let SimpleHandler = 1;
}

def NotTailCalled : InheritableAttr {
  let Spellings = [Clang<"not_tail_called">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NotTailCalledDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[AlwaysInline, NotTailCalled]>;

def NoStackProtector : InheritableAttr {
  let Spellings = [Clang<"no_stack_protector">, CXX11<"gnu", "no_stack_protector">,
                   C23<"gnu", "no_stack_protector">, Declspec<"safebuffers">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoStackProtectorDocs];
  let SimpleHandler = 1;
}

def StrictGuardStackCheck : InheritableAttr {
  let Spellings = [Declspec<"strict_gs_check">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [StrictGuardStackCheckDocs];
  let SimpleHandler = 1;
}

def NoThrow : InheritableAttr {
  let Spellings = [GCC<"nothrow">, Declspec<"nothrow">];
  let Subjects = SubjectList<[FunctionLike]>;
  let Documentation = [NoThrowDocs];
}

def NoUwtable : InheritableAttr {
  let Spellings = [Clang<"nouwtable">];
  let Subjects = SubjectList<[FunctionLike]>;
  let Documentation = [NoUwtableDocs];
  let SimpleHandler = 1;
}

def NvWeak : IgnoredAttr {
  // No Declspec spelling of this attribute; the CUDA headers use
  // __attribute__((nv_weak)) unconditionally. Does not receive an [[]]
  // spelling because it is a CUDA attribute.
  let Spellings = [GNU<"nv_weak">];
  let LangOpts = [CUDA];
}

def ObjCBridge : InheritableAttr {
  let Spellings = [Clang<"objc_bridge">];
  let Subjects = SubjectList<[Record, TypedefName], ErrorDiag>;
  let Args = [IdentifierArgument<"BridgedType">];
  let Documentation = [Undocumented];
}

def ObjCBridgeMutable : InheritableAttr {
  let Spellings = [Clang<"objc_bridge_mutable">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Args = [IdentifierArgument<"BridgedType">];
  let Documentation = [Undocumented];
}

def ObjCBridgeRelated : InheritableAttr {
  let Spellings = [Clang<"objc_bridge_related">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Args = [IdentifierArgument<"RelatedClass">,
          IdentifierArgument<"ClassMethod">,
          IdentifierArgument<"InstanceMethod">];
  let HasCustomParsing = 1;
  let Documentation = [Undocumented];
}

def NSErrorDomain : InheritableAttr {
  let Spellings = [GNU<"ns_error_domain">];
  let Subjects = SubjectList<[Enum], ErrorDiag>;
  let Args = [IdentifierArgument<"ErrorDomain">];
  let Documentation = [NSErrorDomainDocs];
}

def NSReturnsRetained : DeclOrTypeAttr {
  let Spellings = [Clang<"ns_returns_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSReturnsNotRetained : InheritableAttr {
  let Spellings = [Clang<"ns_returns_not_retained">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSReturnsAutoreleased : InheritableAttr {
  let Spellings = [Clang<"ns_returns_autoreleased">];
//  let Subjects = SubjectList<[ObjCMethod, ObjCProperty, Function]>;
  let Documentation = [RetainBehaviorDocs];
}

def NSConsumesSelf : InheritableAttr {
  let Spellings = [Clang<"ns_consumes_self">];
  let Subjects = SubjectList<[ObjCMethod]>;
  let Documentation = [RetainBehaviorDocs];
  let SimpleHandler = 1;
}

def NSConsumed : InheritableParamAttr {
  let Spellings = [Clang<"ns_consumed">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [RetainBehaviorDocs];
}

def ObjCException : InheritableAttr {
  let Spellings = [Clang<"objc_exception">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def ObjCMethodFamily : InheritableAttr {
  let Spellings = [Clang<"objc_method_family">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Args = [EnumArgument<"Family", "FamilyKind", /*is_string=*/false,
               ["none", "alloc", "copy", "init", "mutableCopy", "new"],
               ["OMF_None", "OMF_alloc", "OMF_copy", "OMF_init",
                "OMF_mutableCopy", "OMF_new"]>];
  let Documentation = [ObjCMethodFamilyDocs];
}

def ObjCNSObject : InheritableAttr {
  let Spellings = [Clang<"NSObject">];
  let Documentation = [Undocumented];
}

def ObjCIndependentClass : InheritableAttr {
  let Spellings = [Clang<"objc_independent_class">];
  let Documentation = [Undocumented];
}

def ObjCPreciseLifetime : InheritableAttr {
  let Spellings = [Clang<"objc_precise_lifetime">];
  let Subjects = SubjectList<[Var], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCReturnsInnerPointer : InheritableAttr {
  let Spellings = [Clang<"objc_returns_inner_pointer">];
  let Subjects = SubjectList<[ObjCMethod, ObjCProperty], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCRequiresSuper : InheritableAttr {
  let Spellings = [Clang<"objc_requires_super">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Documentation = [ObjCRequiresSuperDocs];
}

def ObjCRootClass : InheritableAttr {
  let Spellings = [Clang<"objc_root_class">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def ObjCNonLazyClass : Attr {
  let Spellings = [Clang<"objc_nonlazy_class">];
  let Subjects = SubjectList<[ObjCInterface, ObjCImpl], ErrorDiag>;
  let LangOpts = [ObjC];
  let Documentation = [ObjCNonLazyClassDocs];
  let SimpleHandler = 1;
}

def ObjCSubclassingRestricted : InheritableAttr {
  let Spellings = [Clang<"objc_subclassing_restricted">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [ObjCSubclassingRestrictedDocs];
  let SimpleHandler = 1;
}

def ObjCExplicitProtocolImpl : InheritableAttr {
  let Spellings = [Clang<"objc_protocol_requires_explicit_implementation">];
  let Subjects = SubjectList<[ObjCProtocol], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCDesignatedInitializer : Attr {
  let Spellings = [Clang<"objc_designated_initializer">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let Documentation = [Undocumented];
}

def ObjCDirect : Attr {
  let Spellings = [Clang<"objc_direct">];
  let Subjects = SubjectList<[ObjCMethod], ErrorDiag>;
  let LangOpts = [ObjC];
  let Documentation = [ObjCDirectDocs];
}

def ObjCDirectMembers : Attr {
  let Spellings = [Clang<"objc_direct_members">];
  let Subjects = SubjectList<[ObjCImpl, ObjCInterface, ObjCCategory], ErrorDiag>;
  let LangOpts = [ObjC];
  let Documentation = [ObjCDirectMembersDocs];
}

def ObjCNonRuntimeProtocol : Attr {
  let Spellings = [Clang<"objc_non_runtime_protocol">];
  let Subjects = SubjectList<[ObjCProtocol], ErrorDiag>;
  let LangOpts = [ObjC];
  let Documentation = [ObjCNonRuntimeProtocolDocs];
  let SimpleHandler = 1;
}

def ObjCRuntimeName : Attr {
  let Spellings = [Clang<"objc_runtime_name">];
  let Subjects = SubjectList<[ObjCInterface, ObjCProtocol], ErrorDiag>;
  let Args = [StringArgument<"MetadataName">];
  let Documentation = [ObjCRuntimeNameDocs];
}

def ObjCRuntimeVisible : Attr {
  let Spellings = [Clang<"objc_runtime_visible">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [ObjCRuntimeVisibleDocs];
  let SimpleHandler = 1;
}

def ObjCClassStub : Attr {
  let Spellings = [Clang<"objc_class_stub">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [ObjCClassStubDocs];
  let LangOpts = [ObjCNonFragileRuntime];
  let SimpleHandler = 1;
}

def ObjCBoxable : Attr {
  let Spellings = [Clang<"objc_boxable">];
  let Subjects = SubjectList<[Record], ErrorDiag>;
  let Documentation = [ObjCBoxableDocs];
}

def OptimizeNone : InheritableAttr {
  let Spellings = [Clang<"optnone">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Documentation = [OptnoneDocs];
}

def Overloadable : Attr {
  let Spellings = [Clang<"overloadable">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [OverloadableDocs];
  let SimpleHandler = 1;
}

def Override : InheritableAttr {
  let Spellings = [CustomKeyword<"override">];
  let SemaHandler = 0;
  // Omitted from docs, since this is language syntax, not an attribute, as far
  // as users are concerned.
  let Documentation = [InternalOnly];
}

def Ownership : InheritableAttr {
  let Spellings = [Clang<"ownership_holds">, Clang<"ownership_returns">,
                   Clang<"ownership_takes">];
  let Accessors = [Accessor<"isHolds", [Clang<"ownership_holds">]>,
                   Accessor<"isReturns", [Clang<"ownership_returns">]>,
                   Accessor<"isTakes", [Clang<"ownership_takes">]>];
  let AdditionalMembers = [{
    enum OwnershipKind { Holds, Returns, Takes };
    OwnershipKind getOwnKind() const {
      return isHolds() ? Holds :
             isTakes() ? Takes :
             Returns;
    }
  }];
  let Args = [IdentifierArgument<"Module">,
              VariadicParamIdxArgument<"Args">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let Documentation = [Undocumented];
}

def Packed : InheritableAttr {
  let Spellings = [GCC<"packed">];
//  let Subjects = [Tag, Field];
  let Documentation = [Undocumented];
}

def IntelOclBicc : DeclOrTypeAttr {
  let Spellings = [Clang<"intel_ocl_bicc", 0>];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

def Pcs : DeclOrTypeAttr {
  let Spellings = [GCC<"pcs">];
  let Args = [EnumArgument<"PCS", "PCSType", /*is_string=*/true,
                           ["aapcs", "aapcs-vfp"],
                           ["AAPCS", "AAPCS_VFP"]>];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [PcsDocs];
}

def AArch64VectorPcs: DeclOrTypeAttr {
  let Spellings = [Clang<"aarch64_vector_pcs">];
  let Documentation = [AArch64VectorPcsDocs];
}

def AArch64SVEPcs: DeclOrTypeAttr {
  let Spellings = [Clang<"aarch64_sve_pcs">];
  let Documentation = [AArch64SVEPcsDocs];
}

def ArmStreaming : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_streaming">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmSmeStreamingDocs];
}

def ArmStreamingCompatible : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_streaming_compatible">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmSmeStreamingCompatibleDocs];
}

def ArmNew : InheritableAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_new">];
  let Args = [VariadicStringArgument<"NewArgs">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [ArmNewDocs];

  let AdditionalMembers = [{
    bool isNewZA() const {
      return llvm::is_contained(newArgs(), "za");
    }
    bool isNewZT0() const {
      return llvm::is_contained(newArgs(), "zt0");
    }
  }];
}

def ArmIn : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_in">];
  let Args = [VariadicStringArgument<"InArgs">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmInDocs];
}

def ArmOut : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_out">];
  let Args = [VariadicStringArgument<"OutArgs">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmOutDocs];
}

def ArmInOut : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_inout">];
  let Args = [VariadicStringArgument<"InOutArgs">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmInOutDocs];
}

def ArmPreserves : TypeAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_preserves">];
  let Args = [VariadicStringArgument<"PreserveArgs">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Documentation = [ArmPreservesDocs];
}

def ArmLocallyStreaming : InheritableAttr, TargetSpecificAttr<TargetAArch64> {
  let Spellings = [RegularKeyword<"__arm_locally_streaming">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [ArmSmeLocallyStreamingDocs];
}


def Pure : InheritableAttr {
  let Spellings = [GCC<"pure">];
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def Regparm : TypeAttr {
  let Spellings = [GCC<"regparm">];
  let Args = [UnsignedArgument<"NumParams">];
  let Documentation = [RegparmDocs];
  // Represented as part of the enclosing function type.
  let ASTNode = 0;
}

def SwiftAsyncName : InheritableAttr {
  let Spellings = [GNU<"swift_async_name">];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[ObjCMethod, Function], ErrorDiag>;
  let Documentation = [SwiftAsyncNameDocs];
}

def SwiftAttr : InheritableAttr {
  let Spellings = [GNU<"swift_attr">];
  let Args = [StringArgument<"Attribute">];
  let Documentation = [SwiftAttrDocs];
  let PragmaAttributeSupport = 1;
}

def SwiftBridge : InheritableAttr {
  let Spellings = [GNU<"swift_bridge">];
  let Args = [StringArgument<"SwiftType">];
  let Subjects = SubjectList<[Tag, TypedefName, ObjCInterface, ObjCProtocol],
                             ErrorDiag>;
  let Documentation = [SwiftBridgeDocs];
}

def SwiftBridgedTypedef : InheritableAttr {
  let Spellings = [GNU<"swift_bridged_typedef">];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Documentation = [SwiftBridgedTypedefDocs];
  let SimpleHandler = 1;
}

def SwiftObjCMembers : Attr {
  let Spellings = [GNU<"swift_objc_members">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [SwiftObjCMembersDocs];
  let SimpleHandler = 1;
}

def SwiftError : InheritableAttr {
  let Spellings = [GNU<"swift_error">];
  let Args = [
      EnumArgument<"Convention", "ConventionKind", /*is_string=*/false,
                   ["none", "nonnull_error", "null_result", "zero_result", "nonzero_result"],
                   ["None", "NonNullError", "NullResult", "ZeroResult", "NonZeroResult"]>
  ];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [SwiftErrorDocs];
}

def SwiftImportAsNonGeneric : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly
  // from API notes.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def SwiftImportPropertyAsAccessors : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly
  // from API notes.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def SwiftName : InheritableAttr {
  let Spellings = [GNU<"swift_name">];
  let Args = [StringArgument<"Name">];
  let Documentation = [SwiftNameDocs];
}

def SwiftNewType : InheritableAttr {
  let Spellings = [GNU<"swift_newtype">, GNU<"swift_wrapper">];
  let Args = [EnumArgument<"NewtypeKind", "NewtypeKind", /*is_string=*/false,
                           ["struct", "enum"], ["NK_Struct", "NK_Enum"]>];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Documentation = [SwiftNewTypeDocs];
  let HasCustomParsing = 1;
}

def SwiftPrivate : InheritableAttr {
  let Spellings = [GNU<"swift_private">];
  let Documentation = [SwiftPrivateDocs];
  let SimpleHandler = 1;
}

def SwiftVersionedAddition : Attr {
  // This attribute has no spellings as it is only ever created implicitly
  // from API notes.
  let Spellings = [];
  let Args = [VersionArgument<"Version">, WrappedAttr<"AdditionalAttr">,
              BoolArgument<"IsReplacedByActive">];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def SwiftVersionedRemoval : Attr {
  // This attribute has no spellings as it is only ever created implicitly
  // from API notes.
  let Spellings = [];
  let Args = [VersionArgument<"Version">, UnsignedArgument<"RawKind">,
              BoolArgument<"IsReplacedByActive">];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
  let AdditionalMembers = [{
    attr::Kind getAttrKindToRemove() const {
      return static_cast<attr::Kind>(getRawKind());
    }
  }];
}

def NoDeref : TypeAttr {
  let Spellings = [Clang<"noderef">];
  let Documentation = [NoDerefDocs];
}

def ReqdWorkGroupSize : InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"reqd_work_group_size">];
  let Args = [UnsignedArgument<"XDim">, UnsignedArgument<"YDim">,
              UnsignedArgument<"ZDim">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def WorkGroupSizeHint :  InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"work_group_size_hint">];
  let Args = [UnsignedArgument<"XDim">,
              UnsignedArgument<"YDim">,
              UnsignedArgument<"ZDim">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def InitPriority : InheritableAttr, TargetSpecificAttr<TargetSupportsInitPriority> {
  let Spellings = [GCC<"init_priority", /*AllowInC*/0>];
  let Args = [UnsignedArgument<"Priority">];
  let Subjects = SubjectList<[Var], ErrorDiag>;
  let Documentation = [InitPriorityDocs];
}

def Section : InheritableAttr {
  let Spellings = [GCC<"section">, Declspec<"allocate">];
  let Args = [StringArgument<"Name">];
  let Subjects =
      SubjectList<[ Function, GlobalVar, ObjCMethod, ObjCProperty ], ErrorDiag>;
  let Documentation = [SectionDocs];
}

// This is used for `__declspec(code_seg("segname"))`, but not for
// `#pragma code_seg("segname")`.
def CodeSeg : InheritableAttr {
  let Spellings = [Declspec<"code_seg">];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[Function, CXXRecord], ErrorDiag>;
  let Documentation = [CodeSegDocs];
}

def PragmaClangBSSSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [InternalOnly];
}

def PragmaClangDataSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [InternalOnly];
}

def PragmaClangRodataSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [InternalOnly];
}

def PragmaClangRelroSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[GlobalVar], ErrorDiag>;
  let Documentation = [InternalOnly];
}

def StrictFP : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  // Function uses strict floating point operations.
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [InternalOnly];
}

def PragmaClangTextSection : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [StringArgument<"Name">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [InternalOnly];
}

def CodeModel : InheritableAttr, TargetSpecificAttr<TargetLoongArch> {
  let Spellings = [GCC<"model">];
  let Args = [EnumArgument<"Model", "llvm::CodeModel::Model", /*is_string=*/1,
              ["normal", "medium", "extreme"], ["Small", "Medium", "Large"],
              /*opt=*/0, /*fake=*/0, /*isExternalType=*/1, /*isCovered=*/0>];
  let Subjects = SubjectList<[NonTLSGlobalVar], ErrorDiag>;
  let Documentation = [CodeModelDocs];
}

def Sentinel : InheritableAttr {
  let Spellings = [GCC<"sentinel">];
  let Args = [DefaultIntArgument<"Sentinel", 0>,
              DefaultIntArgument<"NullPos", 0>];
//  let Subjects = SubjectList<[Function, ObjCMethod, Block, Var]>;
  let Documentation = [Undocumented];
}

def StdCall : DeclOrTypeAttr {
  let Spellings = [GCC<"stdcall">, CustomKeyword<"__stdcall">,
                   CustomKeyword<"_stdcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [StdCallDocs];
}

def SwiftCall : DeclOrTypeAttr {
  let Spellings = [Clang<"swiftcall">];
//  let Subjects = SubjectList<[Function]>;
  let Documentation = [SwiftCallDocs];
}

def SwiftAsyncCall : DeclOrTypeAttr {
  let Spellings = [Clang<"swiftasynccall">];
  let Documentation = [SwiftAsyncCallDocs];
}

def SwiftContext : ParameterABIAttr {
  let Spellings = [Clang<"swift_context">];
  let Documentation = [SwiftContextDocs];
}

def SwiftAsyncContext : ParameterABIAttr {
  let Spellings = [Clang<"swift_async_context">];
  let Documentation = [SwiftAsyncContextDocs];
}

def SwiftErrorResult : ParameterABIAttr {
  let Spellings = [Clang<"swift_error_result">];
  let Documentation = [SwiftErrorResultDocs];
}

def SwiftIndirectResult : ParameterABIAttr {
  let Spellings = [Clang<"swift_indirect_result">];
  let Documentation = [SwiftIndirectResultDocs];
}

def SwiftAsync : InheritableAttr {
  let Spellings = [Clang<"swift_async">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Args = [EnumArgument<"Kind", "Kind", /*is_string=*/false,
                ["none", "swift_private", "not_swift_private"],
                ["None", "SwiftPrivate", "NotSwiftPrivate"]>,
              ParamIdxArgument<"CompletionHandlerIndex", /*opt=*/1>];
  let Documentation = [SwiftAsyncDocs];
}

def SwiftAsyncError : InheritableAttr {
  let Spellings = [Clang<"swift_async_error">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Args = [EnumArgument<"Convention", "ConventionKind", /*is_string=*/false,
              ["none", "nonnull_error", "zero_argument", "nonzero_argument"],
              ["None", "NonNullError", "ZeroArgument", "NonZeroArgument"]>,
              UnsignedArgument<"HandlerParamIdx", /*opt=*/1>];
  let Documentation = [SwiftAsyncErrorDocs];
}

def Suppress : DeclOrStmtAttr {
  let Spellings = [CXX11<"gsl", "suppress">, Clang<"suppress">];
  let Args = [VariadicStringArgument<"DiagnosticIdentifiers">];
  let Accessors = [Accessor<"isGSL", [CXX11<"gsl", "suppress">]>];
  // There's no fundamental reason why we can't simply accept all Decls
  // but let's make a short list so that to avoid supporting something weird
  // by accident. We can always expand the list later.
  let Subjects = SubjectList<[
    Stmt, Var, Field, ObjCProperty, Function, ObjCMethod, Record, ObjCInterface,
    ObjCImplementation, Namespace, Empty
  ], ErrorDiag, "variables, functions, structs, interfaces, and namespaces">;
  let Documentation = [SuppressDocs];
}

def SysVABI : DeclOrTypeAttr {
  let Spellings = [GCC<"sysv_abi">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [SysVABIDocs];
}

def ThisCall : DeclOrTypeAttr {
  let Spellings = [GCC<"thiscall">, CustomKeyword<"__thiscall">,
                   CustomKeyword<"_thiscall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [ThisCallDocs];
}

def VectorCall : DeclOrTypeAttr {
  let Spellings = [Clang<"vectorcall">, CustomKeyword<"__vectorcall">,
                   CustomKeyword<"_vectorcall">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [VectorCallDocs];
}

def ZeroCallUsedRegs : InheritableAttr {
  let Spellings = [GCC<"zero_call_used_regs">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Args = [
    EnumArgument<"ZeroCallUsedRegs", "ZeroCallUsedRegsKind", /*is_string=*/true,
                 ["skip", "used-gpr-arg", "used-gpr", "used-arg", "used",
                  "all-gpr-arg", "all-gpr", "all-arg", "all"],
                 ["Skip", "UsedGPRArg", "UsedGPR", "UsedArg", "Used",
                  "AllGPRArg", "AllGPR", "AllArg", "All"]>
  ];
  let Documentation = [ZeroCallUsedRegsDocs];
}

def Pascal : DeclOrTypeAttr {
  let Spellings = [Clang<"pascal">, CustomKeyword<"__pascal">,
                   CustomKeyword<"_pascal">];
//  let Subjects = [Function, ObjCMethod];
  let Documentation = [Undocumented];
}

def PreferredName : InheritableAttr {
  let Spellings = [Clang<"preferred_name", /*AllowInC*/0>];
  let Subjects = SubjectList<[ClassTmpl]>;
  let Args = [TypeArgument<"TypedefType">];
  let Documentation = [PreferredNameDocs];
  let InheritEvenIfAlreadyPresent = 1;
  let MeaningfulToClassTemplateDefinition = 1;
  let TemplateDependent = 1;
}

def PreserveMost : DeclOrTypeAttr {
  let Spellings = [Clang<"preserve_most">];
  let Documentation = [PreserveMostDocs];
}

def PreserveAll : DeclOrTypeAttr {
  let Spellings = [Clang<"preserve_all">];
  let Documentation = [PreserveAllDocs];
}

def M68kRTD: DeclOrTypeAttr {
  let Spellings = [Clang<"m68k_rtd">];
  let Documentation = [M68kRTDDocs];
}

def PreserveNone : DeclOrTypeAttr,
                   TargetSpecificAttr<TargetArch<!listconcat(TargetAArch64.Arches, TargetAnyX86.Arches)>> {
  let Spellings = [Clang<"preserve_none">];
  let Subjects = SubjectList<[FunctionLike]>;
  let Documentation = [PreserveNoneDocs];
}

def RISCVVectorCC: DeclOrTypeAttr, TargetSpecificAttr<TargetRISCV> {
 let Spellings = [CXX11<"riscv", "vector_cc">,
                  C23<"riscv", "vector_cc">,
                  Clang<"riscv_vector_cc">];
 let Documentation = [RISCVVectorCCDocs];
}

def Target : InheritableAttr {
  let Spellings = [GCC<"target">];
  let Args = [StringArgument<"featuresStr">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [TargetDocs];
  let AdditionalMembers = [{
    StringRef getArchitecture() const {
      StringRef Features = getFeaturesStr();
      if (Features == "default") return {};

      SmallVector<StringRef, 1> AttrFeatures;
      Features.split(AttrFeatures, ",");

      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();
        if (Feature.starts_with("arch="))
          return Feature.drop_front(sizeof("arch=") - 1);
      }
      return "";
    }

    // Gets the list of features as simple string-refs with no +/- or 'no-'.
    // Only adds the items to 'Out' that are additions.
    void getAddedFeatures(llvm::SmallVectorImpl<StringRef> &Out) const {
      StringRef Features = getFeaturesStr();
      if (Features == "default") return;

      SmallVector<StringRef, 1> AttrFeatures;
      Features.split(AttrFeatures, ",");

      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();

        if (!Feature.starts_with("no-") && !Feature.starts_with("arch=") &&
            !Feature.starts_with("fpmath=") && !Feature.starts_with("tune="))
          Out.push_back(Feature);
      }
    }

    bool isDefaultVersion() const { return getFeaturesStr() == "default"; }
  }];
}

def TargetVersion : InheritableAttr {
  let Spellings = [GCC<"target_version">];
  let Args = [StringArgument<"NamesStr">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [TargetVersionDocs];
  let AdditionalMembers = [{
    StringRef getName() const { return getNamesStr().trim(); }
    bool isDefaultVersion() const {
      return getName() == "default";
    }
    void getFeatures(llvm::SmallVectorImpl<StringRef> &Out) const {
      if (isDefaultVersion()) return;
      StringRef Features = getName();

      SmallVector<StringRef, 8> AttrFeatures;
      Features.split(AttrFeatures, "+");

      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();
        Out.push_back(Feature);
      }
    }
  }];
}

def TargetClones : InheritableAttr {
  let Spellings = [GCC<"target_clones">];
  let Args = [VariadicStringArgument<"featuresStrs">];
  let Documentation = [TargetClonesDocs];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let AdditionalMembers = [{
    StringRef getFeatureStr(unsigned Index) const {
      return *(featuresStrs_begin() + Index);
    }
    bool isDefaultVersion(unsigned Index) const {
      return getFeatureStr(Index) == "default";
    }
    void getFeatures(llvm::SmallVectorImpl<StringRef> &Out,
                     unsigned Index) const {
      if (isDefaultVersion(Index)) return;
      StringRef Features = getFeatureStr(Index);
      SmallVector<StringRef, 8> AttrFeatures;
      Features.split(AttrFeatures, "+");
      for (auto &Feature : AttrFeatures) {
        Feature = Feature.trim();
        Out.push_back(Feature);
      }
    }
    // Given an index into the 'featuresStrs' sequence, compute a unique
    // ID to be used with function name mangling for the associated variant.
    // This mapping is necessary due to a requirement that the mangling ID
    // used for the "default" variant be the largest mangling ID in the
    // variant set. Duplicate variants present in 'featuresStrs' are also
    // assigned their own unique ID (the mapping is bijective).
    unsigned getMangledIndex(unsigned Index) const {
      if (getFeatureStr(Index) == "default")
        return std::count_if(featuresStrs_begin(), featuresStrs_end(),
                              [](StringRef S) { return S != "default"; });

      return std::count_if(featuresStrs_begin(), featuresStrs_begin() + Index,
                           [](StringRef S) { return S != "default"; });
    }

    // Given an index into the 'featuresStrs' sequence, determine if the
    // index corresponds to the first instance of the named variant. This
    // is used to skip over duplicate variant instances when iterating over
    // 'featuresStrs'.
    bool isFirstOfVersion(unsigned Index) const {
      StringRef FeatureStr(getFeatureStr(Index));
      return 0 == std::count_if(
                      featuresStrs_begin(), featuresStrs_begin() + Index,
                      [FeatureStr](StringRef S) { return S == FeatureStr; });

    }
  }];
}

def : MutualExclusions<[TargetClones, TargetVersion, Target, CPUDispatch, CPUSpecific]>;

def MinVectorWidth : InheritableAttr {
  let Spellings = [Clang<"min_vector_width">];
  let Args = [UnsignedArgument<"VectorWidth">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [MinVectorWidthDocs];
}

def TransparentUnion : InheritableAttr {
  let Spellings = [GCC<"transparent_union">];
//  let Subjects = SubjectList<[Record, TypedefName]>;
  let Documentation = [TransparentUnionDocs];
  let LangOpts = [COnly];
}

def Unavailable : InheritableAttr {
  let Spellings = [Clang<"unavailable">];
  let Args = [StringArgument<"Message", 1>,
              EnumArgument<"ImplicitReason", "ImplicitReason", /*is_string=*/0, // FIXME
                ["", "", "", ""],
                ["IR_None",
                 "IR_ARCForbiddenType",
                 "IR_ForbiddenWeak",
                 "IR_ARCForbiddenConversion",
                 "IR_ARCInitReturnsUnrelated",
                 "IR_ARCFieldWithOwnership"], 1, /*fake*/ 1>];
  let Documentation = [Undocumented];
  let MeaningfulToClassTemplateDefinition = 1;
}

def DiagnoseIf : InheritableAttr {
  // Does not have a [[]] spelling because this attribute requires the ability
  // to parse function arguments but the attribute is not written in the type
  // position.
  let Spellings = [GNU<"diagnose_if">];
  let Subjects = SubjectList<[Function, ObjCMethod, ObjCProperty]>;
  let Args = [ExprArgument<"Cond">, StringArgument<"Message">,
              EnumArgument<"DiagnosticType", "DiagnosticType",
                           /*is_string=*/true,
                           ["error", "warning"],
                           ["DT_Error", "DT_Warning"]>,
              BoolArgument<"ArgDependent", 0, /*fake*/ 1>,
              DeclArgument<Named, "Parent", 0, /*fake*/ 1>];
  let InheritEvenIfAlreadyPresent = 1;
  let LateParsed = LateAttrParseStandard;
  let AdditionalMembers = [{
    bool isError() const { return diagnosticType == DT_Error; }
    bool isWarning() const { return diagnosticType == DT_Warning; }
  }];
  let TemplateDependent = 1;
  let Documentation = [DiagnoseIfDocs];
}

def ArcWeakrefUnavailable : InheritableAttr {
  let Spellings = [Clang<"objc_arc_weak_reference_unavailable">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def ObjCGC : TypeAttr {
  let Spellings = [Clang<"objc_gc">];
  let Args = [IdentifierArgument<"Kind">];
  let Documentation = [Undocumented];
}

def ObjCOwnership : DeclOrTypeAttr {
  let Spellings = [Clang<"objc_ownership">];
  let Args = [IdentifierArgument<"Kind">];
  let Documentation = [Undocumented];
}

def ObjCRequiresPropertyDefs : InheritableAttr {
  let Spellings = [Clang<"objc_requires_property_definitions">];
  let Subjects = SubjectList<[ObjCInterface], ErrorDiag>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def Unused : InheritableAttr {
  let Spellings = [CXX11<"", "maybe_unused", 201603>, GCC<"unused">,
                   C23<"", "maybe_unused", 202106>];
  let Subjects = SubjectList<[Var, Binding, ObjCIvar, Type, Enum, EnumConstant, Label,
                              Field, ObjCMethod, FunctionLike]>;
  let Documentation = [WarnMaybeUnusedDocs];
}

def Used : InheritableAttr {
  let Spellings = [GCC<"used">];
  let Subjects = SubjectList<[NonLocalVar, Function, ObjCMethod]>;
  let Documentation = [UsedDocs];
  let SimpleHandler = 1;
}

def Retain : InheritableAttr {
  let Spellings = [GCC<"retain">];
  let Subjects = SubjectList<[NonLocalVar, Function, ObjCMethod]>;
  let Documentation = [RetainDocs];
  let SimpleHandler = 1;
}

def Uuid : InheritableAttr {
  let Spellings = [Declspec<"uuid">, Microsoft<"uuid">];
  let Args = [StringArgument<"Guid">,
              DeclArgument<MSGuid, "GuidDecl", 0, /*fake=*/1>];
  let Subjects = SubjectList<[Record, Enum]>;
  // FIXME: Allow expressing logical AND for LangOpts. Our condition should be:
  // CPlusPlus && (MicrosoftExt || Borland)
  let LangOpts = [MicrosoftExt, Borland];
  let Documentation = [Undocumented];
}

def VectorSize : TypeAttr {
  let Spellings = [GCC<"vector_size">];
  let Args = [ExprArgument<"NumBytes">];
  let Documentation = [Undocumented];
  // Represented as VectorType instead.
  let ASTNode = 0;
}

def VecTypeHint : InheritableAttr {
  // Does not have a [[]] spelling because it is an OpenCL-related attribute.
  let Spellings = [GNU<"vec_type_hint">];
  let Args = [TypeArgument<"TypeHint">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def MatrixType : TypeAttr {
  let Spellings = [Clang<"matrix_type">];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Args = [ExprArgument<"NumRows">, ExprArgument<"NumColumns">];
  let Documentation = [Undocumented];
  let ASTNode = 0;
  let PragmaAttributeSupport = 0;
}

def Visibility : InheritableAttr {
  let Clone = 0;
  let Spellings = [GCC<"visibility">];
  let Args = [EnumArgument<"Visibility", "VisibilityType", /*is_string=*/true,
                           ["default", "hidden", "internal", "protected"],
                           ["Default", "Hidden", "Hidden", "Protected"]>];
  let MeaningfulToClassTemplateDefinition = 1;
  let Documentation = [Undocumented];
}

def TypeVisibility : InheritableAttr {
  let Clone = 0;
  let Spellings = [Clang<"type_visibility">];
  let Args = [EnumArgument<"Visibility", "VisibilityType", /*is_string=*/true,
                           ["default", "hidden", "internal", "protected"],
                           ["Default", "Hidden", "Hidden", "Protected"]>];
  // let Subjects = SubjectList<[Tag, ObjCInterface, Namespace], ErrorDiag>;
  let Documentation = [TypeVisibilityDocs];
}

def VecReturn : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"vecreturn", 0>];
  let Subjects = SubjectList<[CXXRecord], ErrorDiag>;
  let Documentation = [Undocumented];
}

def WarnUnused : InheritableAttr {
  let Spellings = [GCC<"warn_unused">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def WarnUnusedResult : InheritableAttr {
  let Spellings = [CXX11<"", "nodiscard", 201907>,
                   C23<"", "nodiscard", 202003>,
                   CXX11<"clang", "warn_unused_result">,
                   GCC<"warn_unused_result">];
  let Subjects = SubjectList<[ObjCMethod, Enum, Record, FunctionLike, TypedefName]>;
  let Args = [StringArgument<"Message", 1>];
  let Documentation = [WarnUnusedResultsDocs];
  let AdditionalMembers = [{
    // Check whether this the C++11 nodiscard version, even in non C++11
    // spellings.
    bool IsCXX11NoDiscard() const {
      return this->getSemanticSpelling() == CXX11_nodiscard;
    }
  }];
}

def Weak : InheritableAttr {
  let Spellings = [GCC<"weak">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [WeakDocs];
  let SimpleHandler = 1;
}

def WeakImport : InheritableAttr {
  let Spellings = [Clang<"weak_import">];
  let Documentation = [Undocumented];
}

def WeakRef : InheritableAttr {
  let Spellings = [GCC<"weakref">];
  // A WeakRef that has an argument is treated as being an AliasAttr
  let Args = [StringArgument<"Aliasee", 1>];
  let Subjects = SubjectList<[Var, Function], ErrorDiag>;
  let Documentation = [Undocumented];
}

def LTOVisibilityPublic : InheritableAttr {
  let Spellings = [Clang<"lto_visibility_public">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [LTOVisibilityDocs];
  let SimpleHandler = 1;
}

def AnyX86Interrupt : InheritableAttr, TargetSpecificAttr<TargetAnyX86> {
  // NOTE: If you add any additional spellings, ARMInterrupt's,
  // M68kInterrupt's, MSP430Interrupt's and MipsInterrupt's spellings must match.
  let Spellings = [GCC<"interrupt">];
  let Subjects = SubjectList<[HasFunctionProto]>;
  let ParseKind = "Interrupt";
  let HasCustomParsing = 1;
  let Documentation = [AnyX86InterruptDocs];
}

def AnyX86NoCallerSavedRegisters : InheritableAttr,
                                   TargetSpecificAttr<TargetAnyX86> {
  let Spellings = [GCC<"no_caller_saved_registers">];
  let Documentation = [AnyX86NoCallerSavedRegistersDocs];
  let SimpleHandler = 1;
}

def AnyX86NoCfCheck : DeclOrTypeAttr, TargetSpecificAttr<TargetAnyX86>{
  let Spellings = [GCC<"nocf_check">];
  let Subjects = SubjectList<[FunctionLike]>;
  let Documentation = [AnyX86NoCfCheckDocs];
}

def X86ForceAlignArgPointer : InheritableAttr, TargetSpecificAttr<TargetAnyX86> {
  let Spellings = [GCC<"force_align_arg_pointer">];
  // Technically, this appertains to a FunctionDecl, but the target-specific
  // code silently allows anything function-like (such as typedefs or function
  // pointers), but does not apply the attribute to them.
  let Documentation = [X86ForceAlignArgPointerDocs];
}

def NoSanitize : InheritableAttr {
  let Spellings = [Clang<"no_sanitize">];
  let Args = [VariadicStringArgument<"Sanitizers">];
  let Subjects = SubjectList<[Function, ObjCMethod, GlobalVar], ErrorDiag>;
  let Documentation = [NoSanitizeDocs];
  let AdditionalMembers = [{
    SanitizerMask getMask() const {
      SanitizerMask Mask;
      for (auto SanitizerName : sanitizers()) {
        SanitizerMask ParsedMask =
            parseSanitizerValue(SanitizerName, /*AllowGroups=*/true);
        Mask |= expandSanitizerGroups(ParsedMask);
      }
      return Mask;
    }

    bool hasCoverage() const {
      return llvm::is_contained(sanitizers(), "coverage");
    }
  }];
}

// Attributes to disable a specific sanitizer. No new sanitizers should be added
// to this list; the no_sanitize attribute should be extended instead.
def NoSanitizeSpecific : InheritableAttr {
  let Spellings = [GCC<"no_address_safety_analysis">,
                   GCC<"no_sanitize_address">,
                   GCC<"no_sanitize_thread">,
                   Clang<"no_sanitize_memory">];
  let Subjects = SubjectList<[Function, GlobalVar], ErrorDiag>;
  let Documentation = [NoSanitizeAddressDocs, NoSanitizeThreadDocs,
                       NoSanitizeMemoryDocs];
  let ASTNode = 0;
}

def DisableSanitizerInstrumentation : InheritableAttr {
  let Spellings = [Clang<"disable_sanitizer_instrumentation">];
  let Subjects = SubjectList<[Function, ObjCMethod, GlobalVar]>;
  let Documentation = [DisableSanitizerInstrumentationDocs];
  let SimpleHandler = 1;
}

def CFICanonicalJumpTable : InheritableAttr {
  let Spellings = [Clang<"cfi_canonical_jump_table">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [CFICanonicalJumpTableDocs];
  let SimpleHandler = 1;
}

// C/C++ Thread safety attributes (e.g. for deadlock, data race checking)
// Not all of these attributes will be given a [[]] spelling. The attributes
// which require access to function parameter names cannot use the [[]] spelling
// because they are not written in the type position. Some attributes are given
// an updated captability-based name and the older name will only be supported
// under the GNU-style spelling.
def GuardedVar : InheritableAttr {
  let Spellings = [Clang<"guarded_var", 0>];
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def PtGuardedVar : InheritableAttr {
  let Spellings = [Clang<"pt_guarded_var", 0>];
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def Lockable : InheritableAttr {
  let Spellings = [GNU<"lockable">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
  let ASTNode = 0;  // Replaced by Capability
}

def ScopedLockable : InheritableAttr {
  let Spellings = [Clang<"scoped_lockable", 0>];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def Capability : InheritableAttr {
  let Spellings = [Clang<"capability", 0>, Clang<"shared_capability", 0>];
  let Subjects = SubjectList<[Record, TypedefName], ErrorDiag>;
  let Args = [StringArgument<"Name">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"shared_capability", 0>]>];
  let Documentation = [Undocumented];
}

def AssertCapability : InheritableAttr {
  let Spellings = [Clang<"assert_capability", 0>,
                   Clang<"assert_shared_capability", 0>];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"assert_shared_capability", 0>]>];
  let Documentation = [AssertCapabilityDocs];
}

def AcquireCapability : InheritableAttr {
  let Spellings = [Clang<"acquire_capability", 0>,
                   Clang<"acquire_shared_capability", 0>,
                   GNU<"exclusive_lock_function">,
                   GNU<"shared_lock_function">];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"acquire_shared_capability", 0>,
                     GNU<"shared_lock_function">]>];
  let Documentation = [AcquireCapabilityDocs];
}

def TryAcquireCapability : InheritableAttr {
  let Spellings = [Clang<"try_acquire_capability", 0>,
                   Clang<"try_acquire_shared_capability", 0>];
  let Subjects = SubjectList<[Function],
                             ErrorDiag>;
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"try_acquire_shared_capability", 0>]>];
  let Documentation = [TryAcquireCapabilityDocs];
}

def ReleaseCapability : InheritableAttr {
  let Spellings = [Clang<"release_capability", 0>,
                   Clang<"release_shared_capability", 0>,
                   Clang<"release_generic_capability", 0>,
                   Clang<"unlock_function", 0>];
  let Subjects = SubjectList<[Function]>;
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Args = [VariadicExprArgument<"Args">];
  let Accessors = [Accessor<"isShared",
                    [Clang<"release_shared_capability", 0>]>,
                   Accessor<"isGeneric",
                     [Clang<"release_generic_capability", 0>,
                      Clang<"unlock_function", 0>]>];
  let Documentation = [ReleaseCapabilityDocs];
}

def RequiresCapability : InheritableAttr {
  let Spellings = [Clang<"requires_capability", 0>,
                   Clang<"exclusive_locks_required", 0>,
                   Clang<"requires_shared_capability", 0>,
                   Clang<"shared_locks_required", 0>];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Accessors = [Accessor<"isShared", [Clang<"requires_shared_capability", 0>,
                                         Clang<"shared_locks_required", 0>]>];
  let Documentation = [Undocumented];
}

def NoThreadSafetyAnalysis : InheritableAttr {
  let Spellings = [Clang<"no_thread_safety_analysis">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def GuardedBy : InheritableAttr {
  let Spellings = [GNU<"guarded_by">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = LateAttrParseExperimentalExt;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def PtGuardedBy : InheritableAttr {
  let Spellings = [GNU<"pt_guarded_by">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = LateAttrParseExperimentalExt;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AcquiredAfter : InheritableAttr {
  let Spellings = [GNU<"acquired_after">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseExperimentalExt;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AcquiredBefore : InheritableAttr {
  let Spellings = [GNU<"acquired_before">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseExperimentalExt;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Field, SharedVar]>;
  let Documentation = [Undocumented];
}

def AssertExclusiveLock : InheritableAttr {
  let Spellings = [GNU<"assert_exclusive_lock">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def AssertSharedLock : InheritableAttr {
  let Spellings = [GNU<"assert_shared_lock">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// The first argument is an integer or boolean value specifying the return value
// of a successful lock acquisition.
def ExclusiveTrylockFunction : InheritableAttr {
  let Spellings = [GNU<"exclusive_trylock_function">];
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// The first argument is an integer or boolean value specifying the return value
// of a successful lock acquisition.
def SharedTrylockFunction : InheritableAttr {
  let Spellings = [GNU<"shared_trylock_function">];
  let Args = [ExprArgument<"SuccessValue">, VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def LockReturned : InheritableAttr {
  let Spellings = [GNU<"lock_returned">];
  let Args = [ExprArgument<"Arg">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

def LocksExcluded : InheritableAttr {
  let Spellings = [GNU<"locks_excluded">];
  let Args = [VariadicExprArgument<"Args">];
  let LateParsed = LateAttrParseStandard;
  let TemplateDependent = 1;
  let ParseArgumentsAsUnevaluated = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Subjects = SubjectList<[Function]>;
  let Documentation = [Undocumented];
}

// C/C++ consumed attributes.

def Consumable : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Args = [EnumArgument<"DefaultState", "ConsumedState", /*is_string=*/false,
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ConsumableDocs];
}

def ConsumableAutoCast : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable_auto_cast_state", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def ConsumableSetOnRead : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ struct/class/union.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"consumable_set_state_on_read", 0>];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def CallableWhen : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"callable_when", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [VariadicEnumArgument<"CallableStates", "ConsumedState",
                                   /*is_string=*/true,
                                   ["unknown", "consumed", "unconsumed"],
                                   ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [CallableWhenDocs];
}

def ParamTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to a parameter whose type is a consumable C++ class.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"param_typestate", 0>];
  let Subjects = SubjectList<[ParmVar]>;
  let Args = [EnumArgument<"ParamState", "ConsumedState", /*is_string=*/false,
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ParamTypestateDocs];
}

def ReturnTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to a parameter or function return type that is a consumable C++ class.
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"return_typestate", 0>];
  let Subjects = SubjectList<[Function, ParmVar]>;
  let Args = [EnumArgument<"State", "ConsumedState", /*is_string=*/false,
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [ReturnTypestateDocs];
}

def SetTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"set_typestate", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [EnumArgument<"NewState", "ConsumedState", /*is_string=*/false,
                           ["unknown", "consumed", "unconsumed"],
                           ["Unknown", "Consumed", "Unconsumed"]>];
  let Documentation = [SetTypestateDocs];
}

def TestTypestate : InheritableAttr {
  // This attribute does not have a C [[]] spelling because it only appertains
  // to C++ function (but doesn't require it to be a member function).
  // FIXME: should this attribute have a CPlusPlus language option?
  let Spellings = [Clang<"test_typestate", 0>];
  let Subjects = SubjectList<[CXXMethod]>;
  let Args = [EnumArgument<"TestState", "ConsumedState", /*is_string=*/false,
                           ["consumed", "unconsumed"],
                           ["Consumed", "Unconsumed"]>];
  let Documentation = [TestTypestateDocs];
}

// Type safety attributes for `void *' pointers and type tags.

def ArgumentWithTypeTag : InheritableAttr {
  let Spellings = [Clang<"argument_with_type_tag">,
                   Clang<"pointer_with_type_tag">];
  let Subjects = SubjectList<[HasFunctionProto], ErrorDiag>;
  let Args = [IdentifierArgument<"ArgumentKind">,
              ParamIdxArgument<"ArgumentIdx">,
              ParamIdxArgument<"TypeTagIdx">,
              BoolArgument<"IsPointer", /*opt*/0, /*fake*/1>];
  let Documentation = [ArgumentWithTypeTagDocs, PointerWithTypeTagDocs];
}

def TypeTagForDatatype : InheritableAttr {
  let Spellings = [Clang<"type_tag_for_datatype">];
  let Args = [IdentifierArgument<"ArgumentKind">,
              TypeArgument<"MatchingCType">,
              BoolArgument<"LayoutCompatible">,
              BoolArgument<"MustBeNull">];
//  let Subjects = SubjectList<[Var], ErrorDiag>;
  let HasCustomParsing = 1;
  let Documentation = [TypeTagForDatatypeDocs];
}

def Owner : InheritableAttr {
  let Spellings = [CXX11<"gsl", "Owner">];
  let Subjects = SubjectList<[Struct]>;
  let Args = [TypeArgument<"DerefType", /*opt=*/1>];
  let Documentation = [LifetimeOwnerDocs];
}

def Pointer : InheritableAttr {
  let Spellings = [CXX11<"gsl", "Pointer">];
  let Subjects = SubjectList<[Struct]>;
  let Args = [TypeArgument<"DerefType", /*opt=*/1>];
  let Documentation = [LifetimePointerDocs];
}
def : MutualExclusions<[Owner, Pointer]>;

// Microsoft-related attributes

def MSConstexpr : InheritableAttr {
  let LangOpts = [MicrosoftExt];
  let Spellings = [CXX11<"msvc", "constexpr">];
  let Subjects = SubjectList<[Function, ReturnStmt], ErrorDiag,
                             "functions and return statements">;
  let Documentation = [MSConstexprDocs];
}

def MSNoVTable : InheritableAttr, TargetSpecificAttr<TargetMicrosoftCXXABI> {
  let Spellings = [Declspec<"novtable">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [MSNoVTableDocs];
  let SimpleHandler = 1;
}

def : IgnoredAttr {
  let Spellings = [Declspec<"property">];
}

def MSAllocator : InheritableAttr {
  let Spellings = [Declspec<"allocator">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [MSAllocatorDocs];
}

def CFGuard : InheritableAttr, TargetSpecificAttr<TargetWindows> {
  // Currently only the __declspec(guard(nocf)) modifier is supported. In future
  // we might also want to support __declspec(guard(suppress)).
  let Spellings = [Declspec<"guard">, Clang<"guard">];
  let Subjects = SubjectList<[Function]>;
  let Args = [EnumArgument<"Guard", "GuardArg", /*is_string=*/false,
                           ["nocf"], ["nocf"]>];
  let Documentation = [CFGuardDocs];
}

def MSStruct : InheritableAttr {
  let Spellings = [GCC<"ms_struct">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [Undocumented];
  let SimpleHandler = 1;
}

def DLLExport : InheritableAttr, TargetSpecificAttr<TargetHasDLLImportExport> {
  let Spellings = [Declspec<"dllexport">, GCC<"dllexport">];
  let Subjects = SubjectList<[Function, Var, CXXRecord, ObjCInterface]>;
  let Documentation = [DLLExportDocs];
}

def DLLExportStaticLocal : InheritableAttr, TargetSpecificAttr<TargetHasDLLImportExport> {
  // This attribute is used internally only when -fno-dllexport-inlines is
  // passed. This attribute is added to inline functions of a class having the
  // dllexport attribute. If the function has static local variables, this
  // attribute is used to determine whether the variables are exported or not. If
  // the function has local static variables, the function is dllexported too.
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [InternalOnly];
}

def DLLImport : InheritableAttr, TargetSpecificAttr<TargetHasDLLImportExport> {
  let Spellings = [Declspec<"dllimport">, GCC<"dllimport">];
  let Subjects = SubjectList<[Function, Var, CXXRecord, ObjCInterface]>;
  let Documentation = [DLLImportDocs];


  let AdditionalMembers = [{
private:
  bool PropagatedToBaseTemplate = false;

public:
  void setPropagatedToBaseTemplate() { PropagatedToBaseTemplate = true; }
  bool wasPropagatedToBaseTemplate() { return PropagatedToBaseTemplate; }
  }];
}

def DLLImportStaticLocal : InheritableAttr, TargetSpecificAttr<TargetHasDLLImportExport> {
  // This attribute is used internally only when -fno-dllexport-inlines is
  // passed. This attribute is added to inline functions of a class having the
  // dllimport attribute. If the function has static local variables, this
  // attribute is used to determine whether the variables are imported or not.
  let Spellings = [];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [InternalOnly];
}

def SelectAny : InheritableAttr {
  let Spellings = [Declspec<"selectany">, GCC<"selectany">];
  let Documentation = [SelectAnyDocs];
  let SimpleHandler = 1;
}

def HybridPatchable : InheritableAttr, TargetSpecificAttr<TargetWindowsArm64EC> {
  let Spellings = [Declspec<"hybrid_patchable">, Clang<"hybrid_patchable">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [HybridPatchableDocs];
}

def Thread : Attr {
  let Spellings = [Declspec<"thread">];
  let LangOpts = [MicrosoftExt];
  let Documentation = [ThreadDocs];
  let Subjects = SubjectList<[Var]>;
}

def Win64 : IgnoredAttr {
  let Spellings = [CustomKeyword<"__w64">];
  let LangOpts = [MicrosoftExt];
}

def Ptr32 : TypeAttr {
  let Spellings = [CustomKeyword<"__ptr32">];
  let Documentation = [Ptr32Docs];
}

def Ptr64 : TypeAttr {
  let Spellings = [CustomKeyword<"__ptr64">];
  let Documentation = [Ptr64Docs];
}

def SPtr : TypeAttr {
  let Spellings = [CustomKeyword<"__sptr">];
  let Documentation = [SPtrDocs];
}

def UPtr : TypeAttr {
  let Spellings = [CustomKeyword<"__uptr">];
  let Documentation = [UPtrDocs];
}

def MSInheritance : InheritableAttr {
  let LangOpts = [MicrosoftExt];
  let Args = [DefaultBoolArgument<"BestCase", /*default*/1, /*fake*/1>];
  let Spellings = [CustomKeyword<"__single_inheritance">,
                   CustomKeyword<"__multiple_inheritance">,
                   CustomKeyword<"__virtual_inheritance">,
                   CustomKeyword<"__unspecified_inheritance">];
  let AdditionalMembers = [{
  MSInheritanceModel getInheritanceModel() const {
    // The spelling enum should agree with MSInheritanceModel.
    return MSInheritanceModel(getSemanticSpelling());
  }
  }];
  let Documentation = [MSInheritanceDocs];
}

def MSVtorDisp : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let Args = [UnsignedArgument<"vdm">];
  let SemaHandler = 0;

  let AdditionalMembers = [{
  MSVtorDispMode getVtorDispMode() const { return MSVtorDispMode(vdm); }
  }];
  let Documentation = [InternalOnly];
}

def InitSeg : Attr {
  let Spellings = [Pragma<"", "init_seg">];
  let Args = [StringArgument<"Section">];
  let SemaHandler = 0;
  let Documentation = [InitSegDocs];
  let AdditionalMembers = [{
  void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const {
    OS << " (" << getSection() << ')';
  }
  }];
}

def LoopHint : Attr {
  /// #pragma clang loop <option> directive
  /// vectorize: vectorizes loop operations if State == Enable.
  /// vectorize_width: vectorize loop operations with width 'Value'.
  /// interleave: interleave multiple loop iterations if State == Enable.
  /// interleave_count: interleaves 'Value' loop iterations.
  /// unroll: fully unroll loop if State == Enable.
  /// unroll_count: unrolls loop 'Value' times.
  /// unroll_and_jam: attempt to unroll and jam loop if State == Enable.
  /// unroll_and_jam_count: unroll and jams loop 'Value' times.
  /// distribute: attempt to distribute loop if State == Enable.
  /// pipeline: disable pipelining loop if State == Disable.
  /// pipeline_initiation_interval: create loop schedule with initiation interval equal to 'Value'.

  /// #pragma unroll <argument> directive
  /// <no arg>: fully unrolls loop.
  /// boolean: fully unrolls loop if State == Enable.
  /// expression: unrolls loop 'Value' times.

  let Spellings = [Pragma<"clang", "loop">, Pragma<"", "unroll">,
                   Pragma<"", "nounroll">, Pragma<"", "unroll_and_jam">,
                   Pragma<"", "nounroll_and_jam">];

  /// State of the loop optimization specified by the spelling.
  let Args = [EnumArgument<"Option", "OptionType", /*is_string=*/false,
                          ["vectorize", "vectorize_width", "interleave", "interleave_count",
                           "unroll", "unroll_count", "unroll_and_jam", "unroll_and_jam_count",
                           "pipeline", "pipeline_initiation_interval", "distribute",
                           "vectorize_predicate"],
                          ["Vectorize", "VectorizeWidth", "Interleave", "InterleaveCount",
                           "Unroll", "UnrollCount", "UnrollAndJam", "UnrollAndJamCount",
                           "PipelineDisabled", "PipelineInitiationInterval", "Distribute",
                           "VectorizePredicate"]>,
              EnumArgument<"State", "LoopHintState", /*is_string=*/false,
                           ["enable", "disable", "numeric", "fixed_width",
                            "scalable_width", "assume_safety", "full"],
                           ["Enable", "Disable", "Numeric", "FixedWidth",
                            "ScalableWidth", "AssumeSafety", "Full"]>,
              ExprArgument<"Value">];

  let AdditionalMembers = [{
  static const char *getOptionName(int Option) {
    switch(Option) {
    case Vectorize: return "vectorize";
    case VectorizeWidth: return "vectorize_width";
    case Interleave: return "interleave";
    case InterleaveCount: return "interleave_count";
    case Unroll: return "unroll";
    case UnrollCount: return "unroll_count";
    case UnrollAndJam: return "unroll_and_jam";
    case UnrollAndJamCount: return "unroll_and_jam_count";
    case PipelineDisabled: return "pipeline";
    case PipelineInitiationInterval: return "pipeline_initiation_interval";
    case Distribute: return "distribute";
    case VectorizePredicate: return "vectorize_predicate";
    }
    llvm_unreachable("Unhandled LoopHint option.");
  }

  void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const;

  // Return a string containing the loop hint argument including the
  // enclosing parentheses.
  std::string getValueString(const PrintingPolicy &Policy) const;

  // Return a string suitable for identifying this attribute in diagnostics.
  std::string getDiagnosticName(const PrintingPolicy &Policy) const;
  }];

  let Documentation = [LoopHintDocs, UnrollHintDocs];
  let HasCustomParsing = 1;
}

/// The HLSL loop attributes
def HLSLLoopHint: StmtAttr {
  /// [unroll(directive)]
  /// [loop]
  let Spellings = [Microsoft<"unroll">, Microsoft<"loop">];
  let Args = [UnsignedArgument<"directive", /*opt*/1>];
  let Subjects = SubjectList<[ForStmt, WhileStmt, DoStmt],
                              ErrorDiag, "'for', 'while', and 'do' statements">;
  let LangOpts = [HLSL];
  let Documentation = [HLSLLoopHintDocs, HLSLUnrollHintDocs];
}

def CapturedRecord : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def OMPThreadPrivateDecl : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def OMPCaptureNoInit : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def OMPCaptureKind : Attr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Args = [UnsignedArgument<"CaptureKindVal">];
  let Documentation = [InternalOnly];
  let AdditionalMembers = [{
    llvm::omp::Clause getCaptureKind() const {
      return static_cast<llvm::omp::Clause>(getCaptureKindVal());
    }
  }];
}

def OMPReferencedVar : Attr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Args = [ExprArgument<"Ref">];
  let Documentation = [InternalOnly];
}

def OMPDeclareSimdDecl : Attr {
  let Spellings = [Pragma<"omp", "declare simd">];
  let Subjects = SubjectList<[Function]>;
  let SemaHandler = 0;
  let HasCustomParsing = 1;
  let Documentation = [OMPDeclareSimdDocs];
  let Args = [
    EnumArgument<"BranchState", "BranchStateTy", /*is_string=*/false,
                 [ "", "inbranch", "notinbranch" ],
                 [ "BS_Undefined", "BS_Inbranch", "BS_Notinbranch" ]>,
    ExprArgument<"Simdlen">, VariadicExprArgument<"Uniforms">,
    VariadicExprArgument<"Aligneds">, VariadicExprArgument<"Alignments">,
    VariadicExprArgument<"Linears">, VariadicUnsignedArgument<"Modifiers">,
    VariadicExprArgument<"Steps">
  ];
  let AdditionalMembers = [{
    void printPrettyPragma(raw_ostream & OS, const PrintingPolicy &Policy)
        const;
  }];
}

def OMPDeclareTargetDecl : InheritableAttr {
  let Spellings = [Pragma<"omp", "declare target">];
  let SemaHandler = 0;
  let Subjects = SubjectList<[Function, SharedVar]>;
  let Documentation = [OMPDeclareTargetDocs];
  let Args = [
    EnumArgument<"MapType", "MapTypeTy", /*is_string=*/false,
                 [ "to", "enter", "link" ],
                 [ "MT_To", "MT_Enter", "MT_Link" ]>,
    EnumArgument<"DevType", "DevTypeTy", /*is_string=*/false,
                 [ "host", "nohost", "any" ],
                 [ "DT_Host", "DT_NoHost", "DT_Any" ]>,
    ExprArgument<"IndirectExpr">,
    BoolArgument<"Indirect">,
    UnsignedArgument<"Level">
  ];
  let AdditionalMembers = [{
    void printPrettyPragma(raw_ostream &OS, const PrintingPolicy &Policy) const;
    static std::optional<MapTypeTy>
    isDeclareTargetDeclaration(const ValueDecl *VD);
    static std::optional<OMPDeclareTargetDeclAttr*> getActiveAttr(const ValueDecl *VD);
    static std::optional<DevTypeTy> getDeviceType(const ValueDecl *VD);
    static std::optional<SourceLocation> getLocation(const ValueDecl *VD);
  }];
}

def OMPAllocateDecl : InheritableAttr {
  // This attribute has no spellings as it is only ever created implicitly.
  let Spellings = [];
  let SemaHandler = 0;
  let Args = [
    EnumArgument<"AllocatorType", "AllocatorTypeTy", /*is_string=*/false,
                 [
                   "omp_null_allocator", "omp_default_mem_alloc",
                   "omp_large_cap_mem_alloc", "omp_const_mem_alloc",
                   "omp_high_bw_mem_alloc", "omp_low_lat_mem_alloc",
                   "omp_cgroup_mem_alloc", "omp_pteam_mem_alloc",
                   "omp_thread_mem_alloc", ""
                 ],
                 [
                   "OMPNullMemAlloc", "OMPDefaultMemAlloc",
                   "OMPLargeCapMemAlloc", "OMPConstMemAlloc",
                   "OMPHighBWMemAlloc", "OMPLowLatMemAlloc",
                   "OMPCGroupMemAlloc", "OMPPTeamMemAlloc", "OMPThreadMemAlloc",
                   "OMPUserDefinedMemAlloc"
                 ]>,
    ExprArgument<"Allocator">,
    ExprArgument<"Alignment">
  ];
  let Documentation = [InternalOnly];
}

def OMPDeclareVariant : InheritableAttr {
  let Spellings = [Pragma<"omp", "declare variant">];
  let Subjects = SubjectList<[Function]>;
  let SemaHandler = 0;
  let HasCustomParsing = 1;
  let InheritEvenIfAlreadyPresent = 1;
  let Documentation = [OMPDeclareVariantDocs];
  let Args = [
    ExprArgument<"VariantFuncRef">,
    OMPTraitInfoArgument<"TraitInfos">,
    VariadicExprArgument<"AdjustArgsNothing">,
    VariadicExprArgument<"AdjustArgsNeedDevicePtr">,
    VariadicOMPInteropInfoArgument<"AppendArgs">,
  ];
  let AdditionalMembers = [{
    OMPTraitInfo &getTraitInfo() { return *traitInfos; }
    void printPrettyPragma(raw_ostream & OS, const PrintingPolicy &Policy)
        const;
    static StringRef getInteropTypeString(const OMPInteropInfo *I) {
      if (I->IsTarget && I->IsTargetSync)
        return "target,targetsync";
      if (I->IsTarget)
        return "target";
      return "targetsync";
    }
  }];
}

def OMPAssume : InheritableAttr {
  let Spellings = [CXX11<"omp", "assume">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let InheritEvenIfAlreadyPresent = 1;
  let Documentation = [OMPAssumeDocs];
  let Args = [StringArgument<"Assumption">];
}

def InternalLinkage : InheritableAttr {
  let Spellings = [Clang<"internal_linkage">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [InternalLinkageDocs];
}
def : MutualExclusions<[Common, InternalLinkage]>;

def ExcludeFromExplicitInstantiation : InheritableAttr {
  let Spellings = [Clang<"exclude_from_explicit_instantiation">];
  let Subjects = SubjectList<[Var, Function, CXXRecord]>;
  let Documentation = [ExcludeFromExplicitInstantiationDocs];
  let MeaningfulToClassTemplateDefinition = 1;
  let SimpleHandler = 1;
}

def Reinitializes : InheritableAttr {
  let Spellings = [Clang<"reinitializes", 0>];
  let Subjects = SubjectList<[NonStaticNonConstCXXMethod], ErrorDiag>;
  let Documentation = [ReinitializesDocs];
  let SimpleHandler = 1;
}

def NoDestroy : InheritableAttr {
  let Spellings = [Clang<"no_destroy", 0>];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [NoDestroyDocs];
}

def AlwaysDestroy : InheritableAttr {
  let Spellings = [Clang<"always_destroy", 0>];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [AlwaysDestroyDocs];
}
def : MutualExclusions<[NoDestroy, AlwaysDestroy]>;

def SpeculativeLoadHardening : InheritableAttr {
  let Spellings = [Clang<"speculative_load_hardening">];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [SpeculativeLoadHardeningDocs];
  let SimpleHandler = 1;
}

def NoSpeculativeLoadHardening : InheritableAttr {
  let Spellings = [Clang<"no_speculative_load_hardening">];
  let Subjects = SubjectList<[Function, ObjCMethod], ErrorDiag>;
  let Documentation = [NoSpeculativeLoadHardeningDocs];
  let SimpleHandler = 1;
}
def : MutualExclusions<[SpeculativeLoadHardening, NoSpeculativeLoadHardening]>;

def Uninitialized : InheritableAttr {
  let Spellings = [Clang<"uninitialized", 0>];
  let Subjects = SubjectList<[LocalVar]>;
  let PragmaAttributeSupport = 1;
  let Documentation = [UninitializedDocs];
}

def LoaderUninitialized : Attr {
  let Spellings = [Clang<"loader_uninitialized">];
  let Subjects = SubjectList<[GlobalVar]>;
  let Documentation = [LoaderUninitializedDocs];
  let SimpleHandler = 1;
}

def ObjCExternallyRetained : InheritableAttr {
  let LangOpts = [ObjCAutoRefCount];
  let Spellings = [Clang<"objc_externally_retained">];
  let Subjects = SubjectList<[NonParmVar, Function, Block, ObjCMethod]>;
  let Documentation = [ObjCExternallyRetainedDocs];
}

def NoBuiltin : Attr {
  let Spellings = [Clang<"no_builtin">];
  let Args = [VariadicStringArgument<"BuiltinNames">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [NoBuiltinDocs];
}

def UsingIfExists : InheritableAttr {
  let Spellings = [Clang<"using_if_exists", 0>];
  let Subjects = SubjectList<[Using,
                              UnresolvedUsingTypename,
                              UnresolvedUsingValue], ErrorDiag>;
  let Documentation = [UsingIfExistsDocs];
}

// FIXME: This attribute is not inheritable, it will not be propagated to
// redecls. [[clang::lifetimebound]] has the same problems. This should be
// fixed in TableGen (by probably adding a new inheritable flag).
def AcquireHandle : DeclOrTypeAttr {
  let Spellings = [Clang<"acquire_handle">];
  let Args = [StringArgument<"HandleType">];
  let Subjects = SubjectList<[Function, TypedefName, ParmVar]>;
  let Documentation = [AcquireHandleDocs];
}

def UseHandle : InheritableParamAttr {
  let Spellings = [Clang<"use_handle">];
  let Args = [StringArgument<"HandleType">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [UseHandleDocs];
}

def ReleaseHandle : InheritableParamAttr {
  let Spellings = [Clang<"release_handle">];
  let Args = [StringArgument<"HandleType">];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [ReleaseHandleDocs];
}

def UnsafeBufferUsage : InheritableAttr {
  let Spellings = [Clang<"unsafe_buffer_usage">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [UnsafeBufferUsageDocs];
}

def DiagnoseAsBuiltin : InheritableAttr {
  let Spellings = [Clang<"diagnose_as_builtin">];
  let Args = [DeclArgument<Function, "Function">,
              VariadicUnsignedArgument<"ArgIndices">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [DiagnoseAsBuiltinDocs];
}

def Builtin : InheritableAttr {
  let Spellings = [];
  let Args = [UnsignedArgument<"ID">];
  let Subjects = SubjectList<[Function]>;
  let SemaHandler = 0;
  let Documentation = [InternalOnly];
}

def EnforceTCB : InheritableAttr {
  let Spellings = [Clang<"enforce_tcb">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Args = [StringArgument<"TCBName">];
  let Documentation = [EnforceTCBDocs];
  bit InheritEvenIfAlreadyPresent = 1;
}

def EnforceTCBLeaf : InheritableAttr {
  let Spellings = [Clang<"enforce_tcb_leaf">];
  let Subjects = SubjectList<[Function, ObjCMethod]>;
  let Args = [StringArgument<"TCBName">];
  let Documentation = [EnforceTCBLeafDocs];
  bit InheritEvenIfAlreadyPresent = 1;
}

def Error : InheritableAttr {
  let Spellings = [GCC<"error">, GCC<"warning">];
  let Accessors = [Accessor<"isError", [GCC<"error">]>,
                   Accessor<"isWarning", [GCC<"warning">]>];
  let Args = [StringArgument<"UserDiagnostic">];
  let Subjects = SubjectList<[Function], ErrorDiag>;
  let Documentation = [ErrorAttrDocs];
}

def HLSLNumThreads: InheritableAttr {
  let Spellings = [Microsoft<"numthreads">];
  let Args = [IntArgument<"X">, IntArgument<"Y">, IntArgument<"Z">];
  let Subjects = SubjectList<[HLSLEntry]>;
  let LangOpts = [HLSL];
  let Documentation = [NumThreadsDocs];
}

def HLSLSV_GroupIndex: HLSLAnnotationAttr {
  let Spellings = [HLSLAnnotation<"SV_GroupIndex">];
  let Subjects = SubjectList<[ParmVar, GlobalVar]>;
  let LangOpts = [HLSL];
  let Documentation = [HLSLSV_GroupIndexDocs];
}

def HLSLResourceBinding: InheritableAttr {
  let Spellings = [HLSLAnnotation<"register">];
  let Subjects = SubjectList<[HLSLBufferObj, ExternalGlobalVar]>;
  let LangOpts = [HLSL];
  let Args = [StringArgument<"Slot">, StringArgument<"Space", 1>];
  let Documentation = [HLSLResourceBindingDocs];
}

def HLSLPackOffset: HLSLAnnotationAttr {
  let Spellings = [HLSLAnnotation<"packoffset">];
  let LangOpts = [HLSL];
  let Args = [IntArgument<"Subcomponent">, IntArgument<"Component">];
  let Documentation = [HLSLPackOffsetDocs];
  let AdditionalMembers = [{
      unsigned getOffset() {
        return subcomponent * 4 + component;
      }
  }];
}

def HLSLSV_DispatchThreadID: HLSLAnnotationAttr {
  let Spellings = [HLSLAnnotation<"SV_DispatchThreadID">];
  let Subjects = SubjectList<[ParmVar, Field]>;
  let LangOpts = [HLSL];
  let Documentation = [HLSLSV_DispatchThreadIDDocs];
}

def HLSLShader : InheritableAttr {
  let Spellings = [Microsoft<"shader">];
  let Subjects = SubjectList<[HLSLEntry]>;
  let LangOpts = [HLSL];
  let Args = [
    EnumArgument<"Type", "llvm::Triple::EnvironmentType", /*is_string=*/true,
                 ["pixel", "vertex", "geometry", "hull", "domain", "compute",
                  "raygeneration", "intersection", "anyhit", "closesthit",
                  "miss", "callable", "mesh", "amplification"],
                 ["Pixel", "Vertex", "Geometry", "Hull", "Domain", "Compute",
                  "RayGeneration", "Intersection", "AnyHit", "ClosestHit",
                  "Miss", "Callable", "Mesh", "Amplification"],
                  /*opt=*/0, /*fake=*/0, /*isExternalType=*/1, /*isCovered=*/0>
  ];
  let Documentation = [HLSLSV_ShaderTypeAttrDocs];
  let AdditionalMembers =
[{
  static bool isValidShaderType(llvm::Triple::EnvironmentType ShaderType) {
    return ShaderType >= llvm::Triple::Pixel && ShaderType <= llvm::Triple::Amplification;
  }
}];
}

def HLSLResource : InheritableAttr {
  let Spellings = [];
  let Subjects = SubjectList<[Struct]>;
  let LangOpts = [HLSL];
  let Args = [    
    EnumArgument<
        "ResourceKind", "llvm::hlsl::ResourceKind",
        /*is_string=*/0,
        [
          "Texture1D", "Texture2D", "Texture2DMS", "Texture3D", "TextureCube",
          "Texture1DArray", "Texture2DArray", "Texture2DMSArray",
          "TextureCubeArray", "TypedBuffer", "RawBuffer", "StructuredBuffer",
          "CBuffer", "Sampler", "TBuffer", "RTAccelerationStructure",
          "FeedbackTexture2D", "FeedbackTexture2DArray"
        ],
        [
          "Texture1D", "Texture2D", "Texture2DMS", "Texture3D", "TextureCube",
          "Texture1DArray", "Texture2DArray", "Texture2DMSArray",
          "TextureCubeArray", "TypedBuffer", "RawBuffer", "StructuredBuffer",
          "CBuffer", "Sampler", "TBuffer", "RTAccelerationStructure",
          "FeedbackTexture2D", "FeedbackTexture2DArray"
        ],
        /*opt=*/0, /*fake=*/0, /*isExternalType=*/1, /*isCovered=*/0>,
    DefaultBoolArgument<"isROV", /*default=*/0>
  ];
  let Documentation = [InternalOnly];
}

def HLSLResourceClass : InheritableAttr {
  let Spellings = [CXX11<"hlsl", "resource_class">];
  let Subjects = SubjectList<[Struct]>;
  let LangOpts = [HLSL];
  let Args = [
	EnumArgument<"ResourceClass", "llvm::hlsl::ResourceClass",
				 /*is_string=*/true, ["SRV", "UAV", "CBuffer", "Sampler"],
				 ["SRV", "UAV", "CBuffer", "Sampler"],
				 /*opt=*/0, /*fake=*/0, /*isExternalType=*/1>
  ];
  let Documentation = [InternalOnly];
}

def HLSLGroupSharedAddressSpace : TypeAttr {
  let Spellings = [CustomKeyword<"groupshared">];
  let Subjects = SubjectList<[Var]>;
  let Documentation = [HLSLGroupSharedAddressSpaceDocs];
}

def HLSLParamModifier : TypeAttr {
  let Spellings = [CustomKeyword<"in">, CustomKeyword<"inout">, CustomKeyword<"out">];
  let Accessors = [Accessor<"isIn", [CustomKeyword<"in">]>,
                   Accessor<"isInOut", [CustomKeyword<"inout">]>,
                   Accessor<"isOut", [CustomKeyword<"out">]>,
                   Accessor<"isAnyOut", [CustomKeyword<"out">, CustomKeyword<"inout">]>,
                   Accessor<"isAnyIn", [CustomKeyword<"in">, CustomKeyword<"inout">]>];
  let Subjects = SubjectList<[ParmVar]>;
  let Documentation = [HLSLParamQualifierDocs];
  let Args = [DefaultBoolArgument<"MergedSpelling", /*default*/0, /*fake*/1>];
}

def RandomizeLayout : InheritableAttr {
  let Spellings = [GCC<"randomize_layout">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [ClangRandomizeLayoutDocs];
  let LangOpts = [COnly];
}

def NoRandomizeLayout : InheritableAttr {
  let Spellings = [GCC<"no_randomize_layout">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [ClangRandomizeLayoutDocs];
  let LangOpts = [COnly];
}
def : MutualExclusions<[RandomizeLayout, NoRandomizeLayout]>;

def VTablePointerAuthentication : InheritableAttr {
  let Spellings = [Clang<"ptrauth_vtable_pointer">];
  let Subjects = SubjectList<[CXXRecord]>;
  let Documentation = [Undocumented];
  let StrictEnumParameters = 1;
  let Args = [EnumArgument<"Key", "VPtrAuthKeyType", /*is_string=*/ true,
                ["default_key", "no_authentication", "process_dependent",
                 "process_independent"],
                ["DefaultKey", "NoKey", "ProcessDependent",
                 "ProcessIndependent"]>,
              EnumArgument<"AddressDiscrimination", "AddressDiscriminationMode",
                /*is_string=*/ true,
                ["default_address_discrimination", "no_address_discrimination",
                 "address_discrimination"],
                ["DefaultAddressDiscrimination", "NoAddressDiscrimination",
                 "AddressDiscrimination"]>,
              EnumArgument<"ExtraDiscrimination", "ExtraDiscrimination",
                /*is_string=*/ true,
                ["default_extra_discrimination", "no_extra_discrimination",
                 "type_discrimination", "custom_discrimination"],
                ["DefaultExtraDiscrimination", "NoExtraDiscrimination",
                 "TypeDiscrimination", "CustomDiscrimination"]>,
              IntArgument<"CustomDiscriminationValue", 1>];
}

def FunctionReturnThunks : InheritableAttr,
    TargetSpecificAttr<TargetAnyX86> {
  let Spellings = [GCC<"function_return">];
  let Args = [EnumArgument<"ThunkType", "Kind", /*is_string=*/true,
    ["keep", "thunk-extern"],
    ["Keep", "Extern"]
  >];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [FunctionReturnThunksDocs];
}

def WebAssemblyFuncref : TypeAttr, TargetSpecificAttr<TargetWebAssembly> {
  let Spellings = [CustomKeyword<"__funcref">];
  let Documentation = [WebAssemblyExportNameDocs];
  let Subjects = SubjectList<[FunctionPointer], ErrorDiag>;
}

def ReadOnlyPlacement : InheritableAttr {
  let Spellings = [Clang<"enforce_read_only_placement">];
  let Subjects = SubjectList<[Record]>;
  let Documentation = [ReadOnlyPlacementDocs];
}

def AvailableOnlyInDefaultEvalMethod : InheritableAttr {
  let Spellings = [Clang<"available_only_in_default_eval_method">];
  let Subjects = SubjectList<[TypedefName], ErrorDiag>;
  let Documentation = [Undocumented];
}

def PreferredType: InheritableAttr {
  let Spellings = [Clang<"preferred_type">];
  let Subjects = SubjectList<[BitField], ErrorDiag>;
  let Args = [TypeArgument<"Type", 1>];
  let Documentation = [PreferredTypeDocumentation];
}

def CodeAlign: StmtAttr {
  let Spellings = [Clang<"code_align">];
  let Subjects = SubjectList<[ForStmt, CXXForRangeStmt, WhileStmt, DoStmt],
                              ErrorDiag, "'for', 'while', and 'do' statements">;
  let Args = [ExprArgument<"Alignment">];
  let Documentation = [CodeAlignAttrDocs];
  let AdditionalMembers = [{
    static constexpr int MinimumAlignment = 1;
    static constexpr int MaximumAlignment = 4096;
  }];
}

def ClspvLibclcBuiltin: InheritableAttr {
  let Spellings = [Clang<"clspv_libclc_builtin">];
  let Subjects = SubjectList<[Function]>;
  let Documentation = [ClspvLibclcBuiltinDoc];
  let SimpleHandler = 1;
}
