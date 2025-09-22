//==--- DiagnosticGroups.td - Diagnostic Group Definitions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

def ImplicitFunctionDeclare : DiagGroup<"implicit-function-declaration">;
def ImplicitInt : DiagGroup<"implicit-int">;

// Aggregation warning settings.
def Implicit : DiagGroup<"implicit", [
    ImplicitFunctionDeclare,
    ImplicitInt
]>;

// Empty DiagGroups are recognized by clang but ignored.
def ODR : DiagGroup<"odr">;
def : DiagGroup<"abi">;
def : DiagGroup<"gnu-empty-initializer">; // Now a C extension, not GNU.
def AbsoluteValue : DiagGroup<"absolute-value">;
def MisspelledAssumption : DiagGroup<"misspelled-assumption">;
def UnknownAssumption : DiagGroup<"unknown-assumption">;
def AddressOfTemporary : DiagGroup<"address-of-temporary">;
def : DiagGroup<"aggregate-return">;
def GNUAlignofExpression : DiagGroup<"gnu-alignof-expression">;
def AmbigMemberTemplate : DiagGroup<"ambiguous-member-template">;
def GNUAnonymousStruct : DiagGroup<"gnu-anonymous-struct">;
def GNUAutoType : DiagGroup<"gnu-auto-type">;
def ArrayBounds : DiagGroup<"array-bounds">;
def ArrayBoundsPointerArithmetic : DiagGroup<"array-bounds-pointer-arithmetic">;
def ArrayParameter : DiagGroup<"array-parameter">;
def AutoDisableVptrSanitizer : DiagGroup<"auto-disable-vptr-sanitizer">;
def Availability : DiagGroup<"availability">;
def Section : DiagGroup<"section">;
def : DiagGroup<"auto-import">;
def FrameworkHdrQuotedInclude : DiagGroup<"quoted-include-in-framework-header">;
def FrameworkIncludePrivateFromPublic :
  DiagGroup<"framework-include-private-from-public">;
def DeprecatedModuleDotMap : DiagGroup<"deprecated-module-dot-map">;
def FrameworkHdrAtImport : DiagGroup<"atimport-in-framework-header">;
def CXX14BinaryLiteral : DiagGroup<"c++14-binary-literal">;
def CXXPre14CompatBinaryLiteral : DiagGroup<"c++98-c++11-compat-binary-literal">;
def BinaryLiteral : DiagGroup<"binary-literal", [CXX14BinaryLiteral,
                                                 CXXPre14CompatBinaryLiteral]>;
def GNUCompoundLiteralInitializer : DiagGroup<"gnu-compound-literal-initializer">;
def SingleBitBitFieldConstantConversion :
  DiagGroup<"single-bit-bitfield-constant-conversion">;
def BitFieldConstantConversion : DiagGroup<"bitfield-constant-conversion",
                                           [SingleBitBitFieldConstantConversion]>;
def BitFieldEnumConversion : DiagGroup<"bitfield-enum-conversion">;
def BitFieldWidth : DiagGroup<"bitfield-width">;
def CompoundTokenSplitByMacro : DiagGroup<"compound-token-split-by-macro">;
def CompoundTokenSplitBySpace : DiagGroup<"compound-token-split-by-space">;
def CompoundTokenSplit : DiagGroup<"compound-token-split",
                                   [CompoundTokenSplitByMacro,
                                    CompoundTokenSplitBySpace]>;
def CoroutineMissingUnhandledException :
  DiagGroup<"coroutine-missing-unhandled-exception">;
def DeprecatedCoroutine :
  DiagGroup<"deprecated-coroutine">;
def AlwaysInlineCoroutine :
  DiagGroup<"always-inline-coroutine">;
def CoroNonAlignedAllocationFunction :
  DiagGroup<"coro-non-aligned-allocation-function">;
def Coroutine : DiagGroup<"coroutine", [CoroutineMissingUnhandledException, DeprecatedCoroutine,
                                        AlwaysInlineCoroutine, CoroNonAlignedAllocationFunction]>;
def ObjCBoolConstantConversion : DiagGroup<"objc-bool-constant-conversion">;
def ConstantConversion : DiagGroup<"constant-conversion",
                                   [BitFieldConstantConversion,
                                    ObjCBoolConstantConversion]>;
def LiteralConversion : DiagGroup<"literal-conversion">;
def StringConversion : DiagGroup<"string-conversion">;
def SignConversion : DiagGroup<"sign-conversion">;
def PointerBoolConversion : DiagGroup<"pointer-bool-conversion">;
def UndefinedBoolConversion : DiagGroup<"undefined-bool-conversion">;
def BitwiseInsteadOfLogical : DiagGroup<"bitwise-instead-of-logical">;
def BoolOperation : DiagGroup<"bool-operation", [BitwiseInsteadOfLogical]>;
def BoolConversion : DiagGroup<"bool-conversion", [PointerBoolConversion,
                                                   UndefinedBoolConversion]>;
def IntConversion : DiagGroup<"int-conversion">;
def ClassConversion: DiagGroup<"class-conversion">;
def DeprecatedEnumCompareConditional :
  DiagGroup<"deprecated-enum-compare-conditional">;
def EnumCompareConditional : DiagGroup<"enum-compare-conditional",
                                       [DeprecatedEnumCompareConditional]>;
def EnumCompareSwitch : DiagGroup<"enum-compare-switch">;
def DeprecatedEnumCompare : DiagGroup<"deprecated-enum-compare">;
def EnumCompare : DiagGroup<"enum-compare", [EnumCompareSwitch,
                                             DeprecatedEnumCompare]>;
def DeprecatedAnonEnumEnumConversion : DiagGroup<"deprecated-anon-enum-enum-conversion">;
def DeprecatedEnumEnumConversion : DiagGroup<"deprecated-enum-enum-conversion">;
def DeprecatedEnumFloatConversion : DiagGroup<"deprecated-enum-float-conversion">;
def AnonEnumEnumConversion : DiagGroup<"anon-enum-enum-conversion",
                                   [DeprecatedAnonEnumEnumConversion]>;
def EnumEnumConversion : DiagGroup<"enum-enum-conversion",
                                   [DeprecatedEnumEnumConversion]>;
def EnumFloatConversion : DiagGroup<"enum-float-conversion",
                                    [DeprecatedEnumFloatConversion]>;
def EnumConversion : DiagGroup<"enum-conversion",
                               [EnumEnumConversion,
                                EnumFloatConversion,
                                EnumCompareConditional]>;
def DeprecatedNoRelaxedTemplateTemplateArgs : DiagGroup<"deprecated-no-relaxed-template-template-args">;
def DeprecatedOFast : DiagGroup<"deprecated-ofast">;
def ObjCSignedCharBoolImplicitIntConversion :
  DiagGroup<"objc-signed-char-bool-implicit-int-conversion">;
def Shorten64To32 : DiagGroup<"shorten-64-to-32">;
def ImplicitIntConversion : DiagGroup<"implicit-int-conversion",
                                     [Shorten64To32,
                                      ObjCSignedCharBoolImplicitIntConversion]>;
def ImplicitConstIntFloatConversion : DiagGroup<"implicit-const-int-float-conversion">;
def ImplicitIntFloatConversion : DiagGroup<"implicit-int-float-conversion",
 [ImplicitConstIntFloatConversion]>;
def ObjCSignedCharBoolImplicitFloatConversion :
  DiagGroup<"objc-signed-char-bool-implicit-float-conversion">;
def ImplicitFloatConversion : DiagGroup<"implicit-float-conversion",
  [ImplicitIntFloatConversion,
   ObjCSignedCharBoolImplicitFloatConversion]>;
def ImplicitFixedPointConversion : DiagGroup<"implicit-fixed-point-conversion">;

def FloatOverflowConversion : DiagGroup<"float-overflow-conversion">;
def FloatZeroConversion : DiagGroup<"float-zero-conversion">;
def FloatConversion :
  DiagGroup<"float-conversion", [FloatOverflowConversion,
                                 FloatZeroConversion]>;

def FrameAddress : DiagGroup<"frame-address">;
def FreeNonHeapObject : DiagGroup<"free-nonheap-object">;
def DoublePromotion : DiagGroup<"double-promotion">;
def EnumTooLarge : DiagGroup<"enum-too-large">;
def UnsupportedNan : DiagGroup<"unsupported-nan">;
def UnsupportedAbs : DiagGroup<"unsupported-abs">;
def UnsupportedFPOpt : DiagGroup<"unsupported-floating-point-opt">;
def UnsupportedCB : DiagGroup<"unsupported-cb">;
def UnsupportedGPOpt : DiagGroup<"unsupported-gpopt">;
def UnsupportedTargetOpt : DiagGroup<"unsupported-target-opt">;
def NonLiteralNullConversion : DiagGroup<"non-literal-null-conversion">;
def NullConversion : DiagGroup<"null-conversion">;
def ImplicitConversionFloatingPointToBool :
  DiagGroup<"implicit-conversion-floating-point-to-bool">;
def ObjCLiteralConversion : DiagGroup<"objc-literal-conversion">;
def MacroRedefined : DiagGroup<"macro-redefined">;
def BuiltinMacroRedefined : DiagGroup<"builtin-macro-redefined">;
def BuiltinRequiresHeader : DiagGroup<"builtin-requires-header">;
def C99Compat : DiagGroup<"c99-compat">;
def C23Compat : DiagGroup<"c23-compat">;
def : DiagGroup<"c2x-compat", [C23Compat]>;

def CXXCompat: DiagGroup<"c++-compat">;
def ExternCCompat : DiagGroup<"extern-c-compat">;
def KeywordCompat : DiagGroup<"keyword-compat">;
def GNUCaseRange : DiagGroup<"gnu-case-range">;
def CastAlign : DiagGroup<"cast-align">;
def CastQual : DiagGroup<"cast-qual">;
def : DiagGroup<"char-align">;
def Comment : DiagGroup<"comment">;
def GNUComplexInteger : DiagGroup<"gnu-complex-integer">;
def GNUConditionalOmittedOperand : DiagGroup<"gnu-conditional-omitted-operand">;
def ConfigMacros : DiagGroup<"config-macros">;
def : DiagGroup<"ctor-dtor-privacy">;
def GNUStringLiteralOperatorTemplate :
  DiagGroup<"gnu-string-literal-operator-template">;
def UndefinedVarTemplate : DiagGroup<"undefined-var-template">;
def UndefinedFuncTemplate : DiagGroup<"undefined-func-template">;
def MissingNoEscape : DiagGroup<"missing-noescape">;

def DefaultedFunctionDeleted : DiagGroup<"defaulted-function-deleted">;
def DeleteIncomplete : DiagGroup<"delete-incomplete">;
def DeleteNonAbstractNonVirtualDtor : DiagGroup<"delete-non-abstract-non-virtual-dtor">;
def DeleteAbstractNonVirtualDtor : DiagGroup<"delete-abstract-non-virtual-dtor">;
def DeleteNonVirtualDtor : DiagGroup<"delete-non-virtual-dtor",
                                     [DeleteNonAbstractNonVirtualDtor,
                                      DeleteAbstractNonVirtualDtor]>;
def AbstractFinalClass : DiagGroup<"abstract-final-class">;
def FinalDtorNonFinalClass : DiagGroup<"final-dtor-non-final-class">;
def GNUOffsetofExtensions : DiagGroup<"gnu-offsetof-extensions">;

def CXX11CompatDeprecatedWritableStr :
  DiagGroup<"c++11-compat-deprecated-writable-strings">;

def DeprecatedArrayCompare : DiagGroup<"deprecated-array-compare">;
def DeprecatedAttributes : DiagGroup<"deprecated-attributes">;
def DeprecatedCommaSubscript : DiagGroup<"deprecated-comma-subscript">;
def DeprecatedCopyWithUserProvidedCopy : DiagGroup<"deprecated-copy-with-user-provided-copy">;
def DeprecatedCopyWithUserProvidedDtor : DiagGroup<"deprecated-copy-with-user-provided-dtor">;
def DeprecatedCopy : DiagGroup<"deprecated-copy", [DeprecatedCopyWithUserProvidedCopy]>;
def DeprecatedCopyWithDtor : DiagGroup<"deprecated-copy-with-dtor", [DeprecatedCopyWithUserProvidedDtor]>;
def DeprecatedLiteralOperator : DiagGroup<"deprecated-literal-operator">;
// For compatibility with GCC.
def : DiagGroup<"deprecated-copy-dtor", [DeprecatedCopyWithDtor]>;
def DeprecatedDeclarations : DiagGroup<"deprecated-declarations">;
def DeprecatedRedundantConstexprStaticDef : DiagGroup<"deprecated-redundant-constexpr-static-def">;
def UnavailableDeclarations : DiagGroup<"unavailable-declarations">;
def UnguardedAvailabilityNew : DiagGroup<"unguarded-availability-new">;
def UnguardedAvailability : DiagGroup<"unguarded-availability",
                                      [UnguardedAvailabilityNew]>;
// partial-availability is an alias of unguarded-availability.
def : DiagGroup<"partial-availability", [UnguardedAvailability]>;
def DeprecatedDynamicExceptionSpec
    : DiagGroup<"deprecated-dynamic-exception-spec">;
def DeprecatedBuiltins : DiagGroup<"deprecated-builtins">;
def DeprecatedImplementations :DiagGroup<"deprecated-implementations">;
def DeprecatedIncrementBool : DiagGroup<"deprecated-increment-bool">;
def DeprecatedRegister : DiagGroup<"deprecated-register">;
def DeprecatedThisCapture : DiagGroup<"deprecated-this-capture">;
def DeprecatedVolatile : DiagGroup<"deprecated-volatile">;
def DeprecatedWritableStr : DiagGroup<"deprecated-writable-strings",
                                      [CXX11CompatDeprecatedWritableStr]>;
def DeprecatedPragma : DiagGroup<"deprecated-pragma">;
def DeprecatedType : DiagGroup<"deprecated-type">;
// FIXME: Why is DeprecatedImplementations not in this group?
def Deprecated : DiagGroup<"deprecated", [DeprecatedAnonEnumEnumConversion,
                                          DeprecatedArrayCompare,
                                          DeprecatedAttributes,
                                          DeprecatedCommaSubscript,
                                          DeprecatedCopy,
                                          DeprecatedCopyWithDtor,
                                          DeprecatedDeclarations,
                                          DeprecatedDynamicExceptionSpec,
                                          DeprecatedEnumCompare,
                                          DeprecatedEnumCompareConditional,
                                          DeprecatedEnumEnumConversion,
                                          DeprecatedEnumFloatConversion,
                                          DeprecatedBuiltins,
                                          DeprecatedIncrementBool,
                                          DeprecatedLiteralOperator,
                                          DeprecatedPragma,
                                          DeprecatedRegister,
                                          DeprecatedNoRelaxedTemplateTemplateArgs,
                                          DeprecatedOFast,
                                          DeprecatedThisCapture,
                                          DeprecatedType,
                                          DeprecatedVolatile,
                                          DeprecatedWritableStr,
                                          DeprecatedRedundantConstexprStaticDef
                                          ]>,
                 DiagCategory<"Deprecations">;

def CXX20Designator : DiagGroup<"c++20-designator">;
// Allow -Wno-c99-designator to be used to turn off all warnings on valid C99
// designators (including the warning controlled by -Wc++20-designator).
def C99Designator : DiagGroup<"c99-designator", [CXX20Designator]>;
def GNUDesignator : DiagGroup<"gnu-designator">;
def DtorName : DiagGroup<"dtor-name">;

def DynamicExceptionSpec
    : DiagGroup<"dynamic-exception-spec", [DeprecatedDynamicExceptionSpec]>;

def LibLTO : DiagGroup<"liblto">;
def : DiagGroup<"disabled-optimization">;
def : DiagGroup<"discard-qual">;
def DivZero : DiagGroup<"division-by-zero">;
def : DiagGroup<"div-by-zero", [DivZero]>;

def DocumentationHTML : DiagGroup<"documentation-html">;
def DocumentationUnknownCommand : DiagGroup<"documentation-unknown-command">;
def DocumentationPedantic : DiagGroup<"documentation-pedantic",
                                      [DocumentationUnknownCommand]>;
def DocumentationDeprecatedSync : DiagGroup<"documentation-deprecated-sync">;
def Documentation : DiagGroup<"documentation",
                              [DocumentationHTML,
                               DocumentationDeprecatedSync]>;

def EmptyBody : DiagGroup<"empty-body">;
def Exceptions : DiagGroup<"exceptions">;
def DeclarationAfterStatement : DiagGroup<"declaration-after-statement">;

def GNUEmptyStruct : DiagGroup<"gnu-empty-struct">;
def ExtraTokens : DiagGroup<"extra-tokens">;
def CXX98CompatExtraSemi : DiagGroup<"c++98-compat-extra-semi">;
def CXX11ExtraSemi : DiagGroup<"c++11-extra-semi">;
def EmptyInitStatement : DiagGroup<"empty-init-stmt">;
def ExportUnnamed : DiagGroup<"export-unnamed">;
def ExtraSemiStmt : DiagGroup<"extra-semi-stmt", [EmptyInitStatement]>;
def ExtraSemi : DiagGroup<"extra-semi", [CXX98CompatExtraSemi,
                                         CXX11ExtraSemi]>;

def GNUFlexibleArrayInitializer : DiagGroup<"gnu-flexible-array-initializer">;
def GNUFlexibleArrayUnionMember : DiagGroup<"gnu-flexible-array-union-member">;
def GNUFoldingConstant : DiagGroup<"gnu-folding-constant">;
def FormatInsufficientArgs : DiagGroup<"format-insufficient-args">;
def FormatExtraArgs : DiagGroup<"format-extra-args">;
def FormatZeroLength : DiagGroup<"format-zero-length">;

def InvalidIOSDeploymentTarget : DiagGroup<"invalid-ios-deployment-target">;

def CXX17CompatMangling : DiagGroup<"c++17-compat-mangling">;
def : DiagGroup<"c++1z-compat-mangling", [CXX17CompatMangling]>;
// Name of this warning in GCC.
def NoexceptType : DiagGroup<"noexcept-type", [CXX17CompatMangling]>;

// Warnings for C code which is not compatible with previous C standards.
def CPre11Compat : DiagGroup<"pre-c11-compat">;
def CPre11CompatPedantic : DiagGroup<"pre-c11-compat-pedantic",
                                     [CPre11Compat]>;
def CPre23Compat : DiagGroup<"pre-c23-compat">;
def CPre23CompatPedantic : DiagGroup<"pre-c23-compat-pedantic",
                                     [CPre23Compat]>;
def : DiagGroup<"pre-c2x-compat", [CPre23Compat]>;
def : DiagGroup<"pre-c2x-compat-pedantic", [CPre23CompatPedantic]>;

def CPre2yCompat : DiagGroup<"pre-c2y-compat">;
def CPre2yCompatPedantic : DiagGroup<"pre-c2y-compat-pedantic",
                                     [CPre2yCompat]>;

// Warnings for C++ code which is not compatible with previous C++ standards.
def CXXPre14Compat : DiagGroup<"pre-c++14-compat">;
def : DiagGroup<"c++98-c++11-compat", [CXXPre14Compat]>;
def CXXPre14CompatPedantic : DiagGroup<"pre-c++14-compat-pedantic",
                                       [CXXPre14Compat,
                                        CXXPre14CompatBinaryLiteral]>;
def : DiagGroup<"c++98-c++11-compat-pedantic", [CXXPre14CompatPedantic]>;
def CXXPre17Compat : DiagGroup<"pre-c++17-compat">;
def : DiagGroup<"c++98-c++11-c++14-compat", [CXXPre17Compat]>;
def CXXPre17CompatPedantic : DiagGroup<"pre-c++17-compat-pedantic",
                                       [CXXPre17Compat]>;
def : DiagGroup<"c++98-c++11-c++14-compat-pedantic",
                [CXXPre17CompatPedantic]>;
def CXXPre20Compat : DiagGroup<"pre-c++20-compat">;
def : DiagGroup<"c++98-c++11-c++14-c++17-compat", [CXXPre20Compat]>;
def CXXPre20CompatPedantic : DiagGroup<"pre-c++20-compat-pedantic",
                                       [CXXPre20Compat]>;
def : DiagGroup<"c++98-c++11-c++14-c++17-compat-pedantic",
                [CXXPre20CompatPedantic]>;
def CXXPre23Compat : DiagGroup<"pre-c++23-compat">;
def CXXPre23CompatPedantic :
  DiagGroup<"pre-c++23-compat-pedantic", [CXXPre23Compat]>;
def CXXPre26Compat : DiagGroup<"pre-c++26-compat">;
def CXXPre26CompatPedantic :
  DiagGroup<"pre-c++26-compat-pedantic", [CXXPre26Compat]>;
def : DiagGroup<"pre-c++2c-compat", [CXXPre26Compat]>;
def : DiagGroup<"pre-c++2c-compat-pedantic", [CXXPre26CompatPedantic]>;

def CXX98CompatBindToTemporaryCopy :
  DiagGroup<"c++98-compat-bind-to-temporary-copy">;
def CXX98CompatLocalTypeTemplateArgs :
  DiagGroup<"c++98-compat-local-type-template-args">;
def CXX98CompatUnnamedTypeTemplateArgs :
  DiagGroup<"c++98-compat-unnamed-type-template-args">;

def CXX98Compat : DiagGroup<"c++98-compat",
                            [CXX98CompatLocalTypeTemplateArgs,
                             CXX98CompatUnnamedTypeTemplateArgs,
                             CXXPre14Compat,
                             CXXPre17Compat,
                             CXXPre20Compat,
                             CXXPre23Compat,
                             CXXPre26Compat]>;
// Warnings for C++11 features which are Extensions in C++98 mode.
def CXX98CompatPedantic : DiagGroup<"c++98-compat-pedantic",
                                    [CXX98Compat,
                                     CXX98CompatBindToTemporaryCopy,
                                     CXX98CompatExtraSemi,
                                     CXXPre14CompatPedantic,
                                     CXXPre17CompatPedantic,
                                     CXXPre20CompatPedantic,
                                     CXXPre23CompatPedantic,
                                     CXXPre26CompatPedantic]>;

def CXX11NarrowingConstReference : DiagGroup<"c++11-narrowing-const-reference">;
def CXX11Narrowing : DiagGroup<"c++11-narrowing", [CXX11NarrowingConstReference]>;

def CXX11WarnInconsistentOverrideDestructor :
  DiagGroup<"inconsistent-missing-destructor-override">;
def CXX11WarnInconsistentOverrideMethod :
  DiagGroup<"inconsistent-missing-override">;
def CXX11WarnSuggestOverrideDestructor : DiagGroup<"suggest-destructor-override">;
def CXX11WarnSuggestOverride : DiagGroup<"suggest-override">;

// Original name of this warning in Clang
def : DiagGroup<"c++0x-narrowing", [CXX11Narrowing]>;

// Name of this warning in GCC
def : DiagGroup<"narrowing", [CXX11Narrowing]>;

def CXX11CompatReservedUserDefinedLiteral :
  DiagGroup<"c++11-compat-reserved-user-defined-literal">;
def ReservedUserDefinedLiteral :
  DiagGroup<"reserved-user-defined-literal",
            [CXX11CompatReservedUserDefinedLiteral]>;

def CXX11Compat : DiagGroup<"c++11-compat",
                            [CXX11Narrowing,
                             CXX11CompatReservedUserDefinedLiteral,
                             CXX11CompatDeprecatedWritableStr,
                             CXXPre14Compat,
                             CXXPre17Compat,
                             CXXPre20Compat,
                             CXXPre23Compat,
                             CXXPre26Compat]>;
def : DiagGroup<"c++0x-compat", [CXX11Compat]>;
def CXX11CompatPedantic : DiagGroup<"c++11-compat-pedantic",
                                    [CXX11Compat,
                                     CXXPre14CompatPedantic,
                                     CXXPre17CompatPedantic,
                                     CXXPre20CompatPedantic,
                                     CXXPre23CompatPedantic,
                                     CXXPre26CompatPedantic]>;

def CXX14Compat : DiagGroup<"c++14-compat", [CXXPre17Compat,
                                             CXXPre20Compat,
                                             CXXPre23Compat,
                                             CXXPre26Compat]>;
def CXX14CompatPedantic : DiagGroup<"c++14-compat-pedantic",
                                    [CXX14Compat,
                                     CXXPre17CompatPedantic,
                                     CXXPre20CompatPedantic,
                                     CXXPre23CompatPedantic,
                                     CXXPre26CompatPedantic]>;

def CXX17Compat : DiagGroup<"c++17-compat", [DeprecatedRegister,
                                             DeprecatedIncrementBool,
                                             CXX17CompatMangling,
                                             CXXPre20Compat,
                                             CXXPre23Compat,
                                             CXXPre26Compat]>;
def CXX17CompatPedantic : DiagGroup<"c++17-compat-pedantic",
                                    [CXX17Compat,
                                     CXXPre20CompatPedantic,
                                     CXXPre23CompatPedantic,
                                     CXXPre26CompatPedantic]>;
def : DiagGroup<"c++1z-compat", [CXX17Compat]>;

def CXX20Compat : DiagGroup<"c++20-compat", [CXXPre23Compat,
                                             CXXPre26Compat]>;
def CXX20CompatPedantic : DiagGroup<"c++20-compat-pedantic",
                                    [CXX20Compat,
                                     CXXPre23CompatPedantic,
                                     CXXPre26CompatPedantic]>;
def : DiagGroup<"c++2a-compat", [CXX20Compat]>;
def : DiagGroup<"c++2a-compat-pedantic", [CXX20CompatPedantic]>;

def CXX23Compat : DiagGroup<"c++23-compat", [CXXPre26Compat]>;

def CXX26Compat : DiagGroup<"c++2c-compat", [DeleteIncomplete]>;

def ExitTimeDestructors : DiagGroup<"exit-time-destructors">;
def FlexibleArrayExtensions : DiagGroup<"flexible-array-extensions">;
def FourByteMultiChar : DiagGroup<"four-char-constants">;
def GlobalConstructors : DiagGroup<"global-constructors"> {
 code Documentation = [{
Emit a warning for each variable declaration that generates code run at startup.
 }];
}
def BitwiseConditionalParentheses: DiagGroup<"bitwise-conditional-parentheses">;
def BitwiseOpParentheses: DiagGroup<"bitwise-op-parentheses">;
def LogicalOpParentheses: DiagGroup<"logical-op-parentheses">;
def LogicalNotParentheses: DiagGroup<"logical-not-parentheses">;
def ShiftOpParentheses: DiagGroup<"shift-op-parentheses">;
def OverloadedShiftOpParentheses: DiagGroup<"overloaded-shift-op-parentheses">;
def DanglingAssignment: DiagGroup<"dangling-assignment">;
def DanglingAssignmentGsl : DiagGroup<"dangling-assignment-gsl">;
def DanglingElse: DiagGroup<"dangling-else">;
def DanglingField : DiagGroup<"dangling-field">;
def DanglingInitializerList : DiagGroup<"dangling-initializer-list">;
def DanglingGsl : DiagGroup<"dangling-gsl">;
def ReturnStackAddress : DiagGroup<"return-stack-address">;
// Name of this warning in GCC
def : DiagGroup<"return-local-addr", [ReturnStackAddress]>;
def Dangling : DiagGroup<"dangling", [DanglingAssignment,
                                      DanglingAssignmentGsl,
                                      DanglingField,
                                      DanglingInitializerList,
                                      DanglingGsl,
                                      ReturnStackAddress]>;
def DistributedObjectModifiers : DiagGroup<"distributed-object-modifiers">;
def DllexportExplicitInstantiationDecl : DiagGroup<"dllexport-explicit-instantiation-decl">;
def ExcessInitializers : DiagGroup<"excess-initializers">;
def ExpansionToDefined : DiagGroup<"expansion-to-defined">;
def FlagEnum : DiagGroup<"flag-enum">;
def IncrementBool : DiagGroup<"increment-bool", [DeprecatedIncrementBool]>;
def InfiniteRecursion : DiagGroup<"infinite-recursion">;
def PureVirtualCallFromCtorDtor: DiagGroup<"call-to-pure-virtual-from-ctor-dtor">;
def GNUImaginaryConstant : DiagGroup<"gnu-imaginary-constant">;
def IgnoredGCH : DiagGroup<"ignored-gch">;
def IgnoredReferenceQualifiers : DiagGroup<"ignored-reference-qualifiers">;
def IgnoredQualifiers : DiagGroup<"ignored-qualifiers", [IgnoredReferenceQualifiers]>;
def : DiagGroup<"import">;
def GNUIncludeNext : DiagGroup<"gnu-include-next">;
def IncompatibleMSStruct : DiagGroup<"incompatible-ms-struct">;
def IncompatibleMSPragmaSection : DiagGroup<"incompatible-ms-pragma-section">;
def IncompatiblePointerTypesDiscardsQualifiers
  : DiagGroup<"incompatible-pointer-types-discards-qualifiers">;
def IncompatibleFunctionPointerTypes
  : DiagGroup<"incompatible-function-pointer-types">;
def IncompatiblePointerTypes
  : DiagGroup<"incompatible-pointer-types",
    [IncompatiblePointerTypesDiscardsQualifiers,
     IncompatibleFunctionPointerTypes]>;
def IncompleteUmbrella : DiagGroup<"incomplete-umbrella">;
def IncompleteFrameworkModuleDeclaration
  : DiagGroup<"incomplete-framework-module-declaration">;
def NonModularIncludeInFrameworkModule
  : DiagGroup<"non-modular-include-in-framework-module">;
def NonModularIncludeInModule : DiagGroup<"non-modular-include-in-module",
                                          [NonModularIncludeInFrameworkModule]>;
def IncompleteModule : DiagGroup<"incomplete-module",
    [IncompleteUmbrella, NonModularIncludeInModule]>;
def PrivateModule : DiagGroup<"private-module">;

def CXX11InlineNamespace : DiagGroup<"c++11-inline-namespace">;
def InlineNamespaceReopenedNoninline
    : DiagGroup<"inline-namespace-reopened-noninline">;
def InvalidNoreturn : DiagGroup<"invalid-noreturn">;
def InvalidSourceEncoding : DiagGroup<"invalid-source-encoding">;
def KNRPromotedParameter : DiagGroup<"knr-promoted-parameter">;
def DeprecatedNonPrototype : DiagGroup<"deprecated-non-prototype">;
def StrictPrototypes : DiagGroup<"strict-prototypes", [DeprecatedNonPrototype]>;
def : DiagGroup<"init-self">;
def : DiagGroup<"inline">;
def : DiagGroup<"invalid-pch">;
def GNULabelsAsValue : DiagGroup<"gnu-label-as-value">;
def GNULineMarker : DiagGroup<"gnu-line-marker">;
def LiteralRange : DiagGroup<"literal-range">;
def LocalTypeTemplateArgs : DiagGroup<"local-type-template-args",
                                      [CXX98CompatLocalTypeTemplateArgs]>;
def RangeLoopConstruct : DiagGroup<"range-loop-construct">;
def RangeLoopBindReference : DiagGroup<"range-loop-bind-reference">;
def RangeLoopAnalysis : DiagGroup<"range-loop-analysis",
                                  [RangeLoopConstruct, RangeLoopBindReference]>;
def ForLoopAnalysis : DiagGroup<"for-loop-analysis">;
def LoopAnalysis : DiagGroup<"loop-analysis", [ForLoopAnalysis,
                                               RangeLoopAnalysis]>;
def MalformedWarningCheck : DiagGroup<"malformed-warning-check">;
def Main : DiagGroup<"main">;
def MainReturnType : DiagGroup<"main-return-type">;
def MaxUnsignedZero : DiagGroup<"max-unsigned-zero">;
def MissingBraces : DiagGroup<"missing-braces">;
def MissingDeclarations: DiagGroup<"missing-declarations">;
def : DiagGroup<"missing-format-attribute">;
def MissingIncludeDirs : DiagGroup<"missing-include-dirs">;
def MissingNoreturn : DiagGroup<"missing-noreturn">;
def MultiChar : DiagGroup<"multichar">;
def : DiagGroup<"nested-externs">;
def CXX11LongLong : DiagGroup<"c++11-long-long">;
def LongLong : DiagGroup<"long-long", [CXX11LongLong]>;
def ImplicitlyUnsignedLiteral : DiagGroup<"implicitly-unsigned-literal">;
def MethodSignatures : DiagGroup<"method-signatures">;
def MismatchedParameterTypes : DiagGroup<"mismatched-parameter-types">;
def MismatchedReturnTypes : DiagGroup<"mismatched-return-types">;
def MismatchedTags : DiagGroup<"mismatched-tags">;
def MissingDesignatedFieldInitializers : DiagGroup<"missing-designated-field-initializers">{
  code Documentation = [{
Warn about designated initializers with some fields missing (only in C++).
  }];
}
// Default -Wmissing-field-initializers matches gcc behavior,
// but missing-designated-field-initializers can be turned off to match old clang behavior.
def MissingFieldInitializers : DiagGroup<"missing-field-initializers",
                                         [MissingDesignatedFieldInitializers]>;
def ModuleLock : DiagGroup<"module-lock">;
def ModuleBuild : DiagGroup<"module-build">;
def ModuleImport : DiagGroup<"module-import">;
def ModuleConflict : DiagGroup<"module-conflict">;
def ModuleFileExtension : DiagGroup<"module-file-extension">;
def ModuleIncludeDirectiveTranslation : DiagGroup<"module-include-translation">;
def RoundTripCC1Args : DiagGroup<"round-trip-cc1-args">;
def NewlineEOF : DiagGroup<"newline-eof">;
def Nullability : DiagGroup<"nullability">;
def NullabilityDeclSpec : DiagGroup<"nullability-declspec">;
def NullabilityInferredOnNestedType : DiagGroup<"nullability-inferred-on-nested-type">;
def NullableToNonNullConversion : DiagGroup<"nullable-to-nonnull-conversion">;
def NullabilityCompletenessOnArrays : DiagGroup<"nullability-completeness-on-arrays">;
def NullabilityCompleteness : DiagGroup<"nullability-completeness",
                                        [NullabilityCompletenessOnArrays]>;
def NullArithmetic : DiagGroup<"null-arithmetic">;
def NullCharacter : DiagGroup<"null-character">;
def NullDereference : DiagGroup<"null-dereference">;
def InitializerOverrides : DiagGroup<"initializer-overrides">;
// For compatibility with GCC; -Woverride-init = -Winitializer-overrides
def : DiagGroup<"override-init", [InitializerOverrides]>;
def NonNull : DiagGroup<"nonnull">;
def NonPODVarargs : DiagGroup<"non-pod-varargs">;
def ClassVarargs : DiagGroup<"class-varargs", [NonPODVarargs]>;
def : DiagGroup<"nonportable-cfstrings">;
def NonVirtualDtor : DiagGroup<"non-virtual-dtor">;
def GNUNullPointerArithmetic : DiagGroup<"gnu-null-pointer-arithmetic">;
def NullPointerArithmetic
    : DiagGroup<"null-pointer-arithmetic", [GNUNullPointerArithmetic]>;
def NullPointerSubtraction : DiagGroup<"null-pointer-subtraction">;
def : DiagGroup<"effc++", [NonVirtualDtor]>;
def OveralignedType : DiagGroup<"over-aligned">;
def OldStyleCast : DiagGroup<"old-style-cast">;
def : DiagGroup<"old-style-definition">;
def OutOfLineDeclaration : DiagGroup<"out-of-line-declaration">;
def : DiagGroup<"overflow">;
def ForwardClassReceiver : DiagGroup<"receiver-forward-class">;
def MethodAccess : DiagGroup<"objc-method-access">;
def ObjCReceiver : DiagGroup<"receiver-expr">;
def OperatorNewReturnsNull : DiagGroup<"new-returns-null">;
def OverlengthStrings : DiagGroup<"overlength-strings">;
def OverloadedVirtual : DiagGroup<"overloaded-virtual">;
def PrivateExtern : DiagGroup<"private-extern">;
def SelTypeCast : DiagGroup<"cast-of-sel-type">;
def FunctionDefInObjCContainer : DiagGroup<"function-def-in-objc-container">;
def BadFunctionCast : DiagGroup<"bad-function-cast">;
def CastFunctionTypeStrict : DiagGroup<"cast-function-type-strict">;
def CastFunctionTypeMismatch : DiagGroup<"cast-function-type-mismatch">;
def CastFunctionType : DiagGroup<"cast-function-type",
                                 [CastFunctionTypeStrict,
                                  CastFunctionTypeMismatch]>;
def ObjCPropertyImpl : DiagGroup<"objc-property-implementation">;
def ObjCPropertyNoAttribute : DiagGroup<"objc-property-no-attribute">;
def ObjCPropertyAssignOnObjectType : DiagGroup<"objc-property-assign-on-object-type">;
def ObjCProtocolQualifiers : DiagGroup<"objc-protocol-qualifiers">;
def ObjCMissingSuperCalls : DiagGroup<"objc-missing-super-calls">;
def ObjCDesignatedInit : DiagGroup<"objc-designated-initializers">;
def ObjCRetainBlockProperty : DiagGroup<"objc-noncopy-retain-block-property">;
def ObjCReadonlyPropertyHasSetter : DiagGroup<"objc-readonly-with-setter-property">;
def ObjCInvalidIBOutletProperty : DiagGroup<"invalid-iboutlet">;
def ObjCRootClass : DiagGroup<"objc-root-class">;
def ObjCPointerIntrospectPerformSelector : DiagGroup<"deprecated-objc-pointer-introspection-performSelector">;
def ObjCPointerIntrospect : DiagGroup<"deprecated-objc-pointer-introspection", [ObjCPointerIntrospectPerformSelector]>;
def ObjCMultipleMethodNames : DiagGroup<"objc-multiple-method-names">;
def ObjCFlexibleArray : DiagGroup<"objc-flexible-array">;
def ObjCBoxing : DiagGroup<"objc-boxing">;
def CompletionHandler : DiagGroup<"completion-handler">;
def CalledOnceParameter : DiagGroup<"called-once-parameter", [CompletionHandler]>;
def OpenCLUnsupportedRGBA: DiagGroup<"opencl-unsupported-rgba">;
def UnderalignedExceptionObject : DiagGroup<"underaligned-exception-object">;
def DeprecatedObjCIsaUsage : DiagGroup<"deprecated-objc-isa-usage">;
def ExplicitInitializeCall : DiagGroup<"explicit-initialize-call">;
def OrderedCompareFunctionPointers : DiagGroup<"ordered-compare-function-pointers">;
def PackedNonPod : DiagGroup<"packed-non-pod">;
def Packed : DiagGroup<"packed", [PackedNonPod]>;
def PaddedBitField : DiagGroup<"padded-bitfield">;
def Padded : DiagGroup<"padded", [PaddedBitField]>;
def UnalignedAccess : DiagGroup<"unaligned-access">;

def PessimizingMove : DiagGroup<"pessimizing-move">;
def ReturnStdMove : DiagGroup<"return-std-move">;

def GNUPointerArith : DiagGroup<"gnu-pointer-arith">;
def PointerArith : DiagGroup<"pointer-arith", [GNUPointerArith]>;

def PoundWarning : DiagGroup<"#warnings">;
def PoundPragmaMessage : DiagGroup<"#pragma-messages">,
                         DiagCategory<"#pragma message Directive">;
def : DiagGroup<"redundant-decls">;
def RedeclaredClassMember : DiagGroup<"redeclared-class-member">;
def GNURedeclaredEnum : DiagGroup<"gnu-redeclared-enum">;
def RedundantMove : DiagGroup<"redundant-move">;
def Register : DiagGroup<"register", [DeprecatedRegister]>;
def ReturnTypeCLinkage : DiagGroup<"return-type-c-linkage">;
def ReturnMismatch : DiagGroup<"return-mismatch">;
def ReturnType : DiagGroup<"return-type", [ReturnTypeCLinkage, ReturnMismatch]>;

def BindToTemporaryCopy : DiagGroup<"bind-to-temporary-copy",
                                    [CXX98CompatBindToTemporaryCopy]>;
def SelfAssignmentField : DiagGroup<"self-assign-field">;
def SelfAssignmentOverloaded : DiagGroup<"self-assign-overloaded">;
def SelfAssignment : DiagGroup<"self-assign", [SelfAssignmentOverloaded, SelfAssignmentField]>;
def SelfMove : DiagGroup<"self-move">;
def SemiBeforeMethodBody : DiagGroup<"semicolon-before-method-body">;
def Sentinel : DiagGroup<"sentinel">;
def MissingMethodReturnType : DiagGroup<"missing-method-return-type">;

def ShadowField : DiagGroup<"shadow-field">;
def ShadowFieldInConstructorModified : DiagGroup<"shadow-field-in-constructor-modified">;
def ShadowFieldInConstructor : DiagGroup<"shadow-field-in-constructor",
                                         [ShadowFieldInConstructorModified]>;
def ShadowIvar : DiagGroup<"shadow-ivar">;
def ShadowUncapturedLocal : DiagGroup<"shadow-uncaptured-local">;

// -Wshadow-all is a catch-all for all shadowing. -Wshadow is just the
// shadowing that we think is unsafe.
def Shadow : DiagGroup<"shadow", [ShadowFieldInConstructorModified,
                                  ShadowIvar]>;
def ShadowAll : DiagGroup<"shadow-all", [Shadow, ShadowFieldInConstructor,
                                         ShadowUncapturedLocal, ShadowField]>;

def : DiagGroup<"sign-promo">;
def SignCompare : DiagGroup<"sign-compare">;
def SwitchDefault  : DiagGroup<"switch-default">;
def : DiagGroup<"synth">;
def SizeofArrayArgument : DiagGroup<"sizeof-array-argument">;
def SizeofArrayDecay : DiagGroup<"sizeof-array-decay">;
def SizeofPointerMemaccess : DiagGroup<"sizeof-pointer-memaccess">;
def MemsetTransposedArgs : DiagGroup<"memset-transposed-args">;
def DynamicClassMemaccess : DiagGroup<"dynamic-class-memaccess">;
def NonTrivialMemaccess : DiagGroup<"nontrivial-memaccess">;
def SuspiciousBzero : DiagGroup<"suspicious-bzero">;
def SuspiciousMemaccess : DiagGroup<"suspicious-memaccess",
  [SizeofPointerMemaccess, DynamicClassMemaccess,
   NonTrivialMemaccess, MemsetTransposedArgs, SuspiciousBzero]>;
def StaticInInline : DiagGroup<"static-in-inline">;
def StaticLocalInInline : DiagGroup<"static-local-in-inline">;
def GNUStaticFloatInit : DiagGroup<"gnu-static-float-init">;
def StaticFloatInit : DiagGroup<"static-float-init", [GNUStaticFloatInit]>;
// Allow differentiation between GNU statement expressions in a macro versus
// written directly in source.
def GNUStatementExpressionFromMacroExpansion :
  DiagGroup<"gnu-statement-expression-from-macro-expansion">;
def GNUStatementExpression : DiagGroup<"gnu-statement-expression",
                                       [GNUStatementExpressionFromMacroExpansion]>;
def StringConcatation : DiagGroup<"string-concatenation">;
def StringCompare : DiagGroup<"string-compare">;
def StringPlusInt : DiagGroup<"string-plus-int">;
def StringPlusChar : DiagGroup<"string-plus-char">;
def StrncatSize : DiagGroup<"strncat-size">;
def SwiftNameAttribute : DiagGroup<"swift-name-attribute">;
def IntInBoolContext : DiagGroup<"int-in-bool-context">;
def TautologicalTypeLimitCompare : DiagGroup<"tautological-type-limit-compare">;
def TautologicalUnsignedZeroCompare : DiagGroup<"tautological-unsigned-zero-compare">;
def TautologicalUnsignedCharZeroCompare : DiagGroup<"tautological-unsigned-char-zero-compare">;
def TautologicalUnsignedEnumZeroCompare : DiagGroup<"tautological-unsigned-enum-zero-compare">;
// For compatibility with GCC. Tautological comparison warnings for constants
// that are an extremal value of the type.
def TypeLimits : DiagGroup<"type-limits", [TautologicalTypeLimitCompare,
                                           TautologicalUnsignedZeroCompare,
                                           TautologicalUnsignedCharZeroCompare,
                                           TautologicalUnsignedEnumZeroCompare]>;
// Additional tautological comparison warnings based on the expression, not
// only on its type.
def TautologicalValueRangeCompare : DiagGroup<"tautological-value-range-compare">;
def TautologicalInRangeCompare : DiagGroup<"tautological-constant-in-range-compare",
                                           [TypeLimits, TautologicalValueRangeCompare]>;
def TautologicalOutOfRangeCompare : DiagGroup<"tautological-constant-out-of-range-compare">;
def TautologicalConstantCompare : DiagGroup<"tautological-constant-compare",
                                            [TautologicalOutOfRangeCompare]>;
def TautologicalPointerCompare : DiagGroup<"tautological-pointer-compare">;
def TautologicalOverlapCompare : DiagGroup<"tautological-overlap-compare">;
def TautologicalBitwiseCompare : DiagGroup<"tautological-bitwise-compare">;
def TautologicalUndefinedCompare : DiagGroup<"tautological-undefined-compare">;
def TautologicalObjCBoolCompare : DiagGroup<"tautological-objc-bool-compare">;
def TautologicalNegationCompare : DiagGroup<"tautological-negation-compare">;
def TautologicalCompare : DiagGroup<"tautological-compare",
                                    [TautologicalConstantCompare,
                                     TautologicalPointerCompare,
                                     TautologicalOverlapCompare,
                                     TautologicalBitwiseCompare,
                                     TautologicalUndefinedCompare,
                                     TautologicalObjCBoolCompare,
                                     TautologicalNegationCompare]>;
def HeaderHygiene : DiagGroup<"header-hygiene">;
def DuplicateDeclSpecifier : DiagGroup<"duplicate-decl-specifier">;
def CompareDistinctPointerType : DiagGroup<"compare-distinct-pointer-types">;
def GNUUnionCast : DiagGroup<"gnu-union-cast">;
def GNUVariableSizedTypeNotAtEnd : DiagGroup<"gnu-variable-sized-type-not-at-end">;
def Varargs : DiagGroup<"varargs">;
def XorUsedAsPow : DiagGroup<"xor-used-as-pow">;

def Unsequenced : DiagGroup<"unsequenced">;
// GCC name for -Wunsequenced
def : DiagGroup<"sequence-point", [Unsequenced]>;

// Preprocessor warnings.
def AmbiguousMacro : DiagGroup<"ambiguous-macro">;
def KeywordAsMacro : DiagGroup<"keyword-macro">;
def ReservedIdAsMacro : DiagGroup<"reserved-macro-identifier">;
def ReservedIdAsMacroAlias : DiagGroup<"reserved-id-macro", [ReservedIdAsMacro]>;
def RestrictExpansionMacro : DiagGroup<"restrict-expansion">;
def FinalMacro : DiagGroup<"final-macro">;

// Just silence warnings about -Wstrict-aliasing for now.
def : DiagGroup<"strict-aliasing=0">;
def : DiagGroup<"strict-aliasing=1">;
def : DiagGroup<"strict-aliasing=2">;
def : DiagGroup<"strict-aliasing">;

// Just silence warnings about -Wstrict-overflow for now.
def : DiagGroup<"strict-overflow=0">;
def : DiagGroup<"strict-overflow=1">;
def : DiagGroup<"strict-overflow=2">;
def : DiagGroup<"strict-overflow=3">;
def : DiagGroup<"strict-overflow=4">;
def : DiagGroup<"strict-overflow=5">;
def : DiagGroup<"strict-overflow">;

def InvalidOffsetof : DiagGroup<"invalid-offsetof">;
def StrictSelector : DiagGroup<"strict-selector-match">;
def MethodDuplicate : DiagGroup<"duplicate-method-match">;
def ObjCCStringFormat : DiagGroup<"cstring-format-directive">;
def CoveredSwitchDefault : DiagGroup<"covered-switch-default">;
def SwitchBool     : DiagGroup<"switch-bool">;
def SwitchEnum     : DiagGroup<"switch-enum">;
def Switch         : DiagGroup<"switch">;
def ImplicitFallthroughPerFunction :
  DiagGroup<"implicit-fallthrough-per-function">;
def ImplicitFallthrough  : DiagGroup<"implicit-fallthrough",
                                     [ImplicitFallthroughPerFunction]>;
def InvalidPPToken : DiagGroup<"invalid-pp-token">;
def Trigraphs      : DiagGroup<"trigraphs">;

def UndefinedReinterpretCast : DiagGroup<"undefined-reinterpret-cast">;
def ReinterpretBaseClass : DiagGroup<"reinterpret-base-class">;
def Unicode  : DiagGroup<"unicode">;
def UninitializedMaybe : DiagGroup<"conditional-uninitialized">;
def UninitializedSometimes : DiagGroup<"sometimes-uninitialized">;
def UninitializedStaticSelfInit : DiagGroup<"static-self-init">;
def UninitializedConstReference : DiagGroup<"uninitialized-const-reference">;
def Uninitialized  : DiagGroup<"uninitialized", [UninitializedSometimes,
                                                 UninitializedStaticSelfInit,
                                                 UninitializedConstReference]>;
def IgnoredPragmaIntrinsic : DiagGroup<"ignored-pragma-intrinsic">;
// #pragma optimize is often used to avoid to work around MSVC codegen bugs or
// to disable inlining. It's not completely clear what alternative to suggest
// (#pragma clang optimize, noinline) so suggest nothing for now.
def IgnoredPragmaOptimize : DiagGroup<"ignored-pragma-optimize">;
def UnknownPragmas : DiagGroup<"unknown-pragmas">;
def IgnoredPragmas : DiagGroup<"ignored-pragmas",
    [IgnoredPragmaIntrinsic, IgnoredPragmaOptimize]>;
def PragmaClangAttribute : DiagGroup<"pragma-clang-attribute">;
def PragmaPackSuspiciousInclude : DiagGroup<"pragma-pack-suspicious-include">;
def PragmaPack : DiagGroup<"pragma-pack", [PragmaPackSuspiciousInclude]>;
def Pragmas : DiagGroup<"pragmas", [UnknownPragmas, IgnoredPragmas,
                                    PragmaClangAttribute, PragmaPack]>;
def UnknownWarningOption : DiagGroup<"unknown-warning-option">;
def NSobjectAttribute : DiagGroup<"NSObject-attribute">;
def NSConsumedMismatch : DiagGroup<"nsconsumed-mismatch">;
def NSReturnsMismatch : DiagGroup<"nsreturns-mismatch">;

def IndependentClassAttribute : DiagGroup<"IndependentClass-attribute">;
def UnknownAttributes : DiagGroup<"unknown-attributes">;
def IgnoredAttributes : DiagGroup<"ignored-attributes">;
def Attributes : DiagGroup<"attributes", [UnknownAttributes,
                                          IgnoredAttributes]>;
def UnknownSanitizers : DiagGroup<"unknown-sanitizers">;
def UnnamedTypeTemplateArgs : DiagGroup<"unnamed-type-template-args",
                                        [CXX98CompatUnnamedTypeTemplateArgs]>;
def UnsupportedFriend : DiagGroup<"unsupported-friend">;
def UnusedArgument : DiagGroup<"unused-argument">;
def UnusedCommandLineArgument : DiagGroup<"unused-command-line-argument">;
def IgnoredOptimizationArgument : DiagGroup<"ignored-optimization-argument">;
def InvalidCommandLineArgument : DiagGroup<"invalid-command-line-argument",
                                           [IgnoredOptimizationArgument]>;
def UnusedComparison : DiagGroup<"unused-comparison">;
def UnusedExceptionParameter : DiagGroup<"unused-exception-parameter">;
def UnneededInternalDecl : DiagGroup<"unneeded-internal-declaration">;
def UnneededMemberFunction : DiagGroup<"unneeded-member-function">;
def UnusedPrivateField : DiagGroup<"unused-private-field">;
def UnusedFunction : DiagGroup<"unused-function", [UnneededInternalDecl]>;
def UnusedTemplate : DiagGroup<"unused-template", [UnneededInternalDecl]>;
def UnusedMemberFunction : DiagGroup<"unused-member-function",
                                     [UnneededMemberFunction]>;
def UnusedLabel : DiagGroup<"unused-label">;
def UnusedLambdaCapture : DiagGroup<"unused-lambda-capture">;
def UnusedParameter : DiagGroup<"unused-parameter">;
def UnusedButSetParameter : DiagGroup<"unused-but-set-parameter">;
def UnusedResult : DiagGroup<"unused-result">;
def PotentiallyEvaluatedExpression : DiagGroup<"potentially-evaluated-expression">;
def UnevaluatedExpression : DiagGroup<"unevaluated-expression",
                                      [PotentiallyEvaluatedExpression]>;
def UnusedValue : DiagGroup<"unused-value", [UnusedComparison, UnusedResult,
                                             UnevaluatedExpression]>;
def UnusedConstVariable : DiagGroup<"unused-const-variable">;
def UnusedVariable : DiagGroup<"unused-variable",
                               [UnusedConstVariable]>;
def UnusedButSetVariable : DiagGroup<"unused-but-set-variable">;
def UnusedLocalTypedef : DiagGroup<"unused-local-typedef">;
def UnusedPropertyIvar :  DiagGroup<"unused-property-ivar">;
def UnusedGetterReturnValue : DiagGroup<"unused-getter-return-value">;
def UsedButMarkedUnused : DiagGroup<"used-but-marked-unused">;
def UsedSearchPath : DiagGroup<"search-path-usage">;
def UserDefinedLiterals : DiagGroup<"user-defined-literals">;
def UserDefinedWarnings : DiagGroup<"user-defined-warnings">;
def ReorderCtor : DiagGroup<"reorder-ctor">;
def ReorderInitList : DiagGroup<"reorder-init-list">;
def Reorder : DiagGroup<"reorder", [ReorderCtor, ReorderInitList]>;
def UndeclaredSelector : DiagGroup<"undeclared-selector">;
def ImplicitAtomic : DiagGroup<"implicit-atomic-properties">;
def AtomicAlignment : DiagGroup<"atomic-alignment">;
def CustomAtomic : DiagGroup<"custom-atomic-properties">;
def AtomicProperties : DiagGroup<"atomic-properties",
                                 [ImplicitAtomic, CustomAtomic]>;
def SyncAlignment : DiagGroup<"sync-alignment">;
def ARCUnsafeRetainedAssign : DiagGroup<"arc-unsafe-retained-assign">;
def ARCRetainCycles : DiagGroup<"arc-retain-cycles">;
def ARCNonPodMemAccess : DiagGroup<"arc-non-pod-memaccess">;
def AutomaticReferenceCounting : DiagGroup<"arc",
                                           [ARCUnsafeRetainedAssign,
                                            ARCRetainCycles,
                                            ARCNonPodMemAccess]>;
def ARCRepeatedUseOfWeakMaybe : DiagGroup<"arc-maybe-repeated-use-of-weak">;
def ARCRepeatedUseOfWeak : DiagGroup<"arc-repeated-use-of-weak",
                                     [ARCRepeatedUseOfWeakMaybe]>;
def BlockCaptureAutoReleasing : DiagGroup<"block-capture-autoreleasing">;
def ObjCBridge : DiagGroup<"bridge-cast">;

def DeallocInCategory:DiagGroup<"dealloc-in-category">;
def SelectorTypeMismatch : DiagGroup<"selector-type-mismatch">;
def Selector : DiagGroup<"selector", [SelectorTypeMismatch]>;
def Protocol : DiagGroup<"protocol">;
// No longer in use, preserve for backwards compatibility.
def : DiagGroup<"at-protocol">;
def PropertyAccessDotSyntax: DiagGroup<"property-access-dot-syntax">;
def PropertyAttr : DiagGroup<"property-attribute-mismatch">;
def SuperSubClassMismatch : DiagGroup<"super-class-method-mismatch">;
def OverridingMethodMismatch : DiagGroup<"overriding-method-mismatch">;
def VariadicMacros : DiagGroup<"variadic-macros">;
def VectorConversion : DiagGroup<"vector-conversion">;      // clang specific
def VexingParse : DiagGroup<"vexing-parse">;
def VLAUseStaticAssert : DiagGroup<"vla-extension-static-assert">;
def VLACxxExtension : DiagGroup<"vla-cxx-extension", [VLAUseStaticAssert]>;
def VLAExtension : DiagGroup<"vla-extension", [VLACxxExtension]>;
def VLA : DiagGroup<"vla", [VLAExtension]>;
def VolatileRegisterVar : DiagGroup<"volatile-register-var">;
def Visibility : DiagGroup<"visibility">;
def ZeroLengthArray : DiagGroup<"zero-length-array">;
def GNUZeroLineDirective : DiagGroup<"gnu-zero-line-directive">;
def GNUZeroVariadicMacroArguments : DiagGroup<"gnu-zero-variadic-macro-arguments">;
def MisleadingIndentation : DiagGroup<"misleading-indentation">;
def PtrAuthNullPointers : DiagGroup<"ptrauth-null-pointers">;

// This covers both the deprecated case (in C++98)
// and the extension case (in C++11 onwards).
def WritableStrings : DiagGroup<"writable-strings", [DeprecatedWritableStr]>;

// GCC calls -Wdeprecated-writable-strings -Wwrite-strings.
//
// Bizarrely, this warning flag enables -fconst-strings in C. This is
// GCC-compatible, but really weird.
//
// FIXME: Should this affect C++11 (where this is an error,
//        not just deprecated) or not?
def GCCWriteStrings : DiagGroup<"write-strings" , [WritableStrings],
                                GCCWriteStringsDocs>;

def CharSubscript : DiagGroup<"char-subscripts">;
def LargeByValueCopy : DiagGroup<"large-by-value-copy">;
def DuplicateArgDecl : DiagGroup<"duplicate-method-arg">;
def SignedEnumBitfield : DiagGroup<"signed-enum-bitfield">;

def ReservedModuleIdentifier : DiagGroup<"reserved-module-identifier">;
def ReservedIdentifier : DiagGroup<"reserved-identifier",
    [ReservedIdAsMacro, ReservedModuleIdentifier, UserDefinedLiterals]>;

// Unreachable code warning groups.
//
//  The goal is make -Wunreachable-code on by default, in -Wall, or at
//  least actively used, with more noisy versions of the warning covered
//  under separate flags.
//
def UnreachableCodeLoopIncrement : DiagGroup<"unreachable-code-loop-increment">;
def UnreachableCodeFallthrough : DiagGroup<"unreachable-code-fallthrough">;
def UnreachableCodeGenericAssoc : DiagGroup<"unreachable-code-generic-assoc">;
def UnreachableCode : DiagGroup<"unreachable-code",
                                [UnreachableCodeLoopIncrement,
                                 UnreachableCodeFallthrough,
                                 UnreachableCodeGenericAssoc]>;
def UnreachableCodeBreak : DiagGroup<"unreachable-code-break">;
def UnreachableCodeReturn : DiagGroup<"unreachable-code-return">;
def UnreachableCodeAggressive : DiagGroup<"unreachable-code-aggressive",
                                    [UnreachableCode,
                                     UnreachableCodeBreak,
                                     UnreachableCodeReturn]>;

// Aggregation warning settings.

// Populate -Waddress with warnings from other groups.
def : DiagGroup<"address", [PointerBoolConversion,
                            StringCompare,
                            TautologicalPointerCompare]>;

// -Widiomatic-parentheses contains warnings about 'idiomatic'
// missing parentheses;  it is off by default.  We do not include it
// in -Wparentheses because most users who use -Wparentheses explicitly
// do not want these warnings.
def ParenthesesOnEquality : DiagGroup<"parentheses-equality">;
def Parentheses : DiagGroup<"parentheses",
                            [LogicalOpParentheses,
                             LogicalNotParentheses,
                             BitwiseConditionalParentheses,
                             BitwiseOpParentheses,
                             ShiftOpParentheses,
                             OverloadedShiftOpParentheses,
                             ParenthesesOnEquality,
                             DanglingElse]>;

// -Wconversion has its own warnings, but we split a few out for
// legacy reasons:
//   - some people want just 64-to-32 warnings
//   - conversion warnings with constant sources are on by default
//   - conversion warnings for literals are on by default
//   - bool-to-pointer conversion warnings are on by default
//   - __null-to-integer conversion warnings are on by default
def Conversion : DiagGroup<"conversion",
                           [BoolConversion,
                            ConstantConversion,
                            EnumConversion,
                            BitFieldEnumConversion,
                            FloatConversion,
                            IntConversion,
                            ImplicitIntConversion,
                            ImplicitFloatConversion,
                            LiteralConversion,
                            NonLiteralNullConversion, // (1-1)->pointer (etc)
                            NullConversion, // NULL->non-pointer
                            ObjCLiteralConversion,
                            SignConversion,
                            StringConversion]>,
                 DiagCategory<"Value Conversion Issue">;

def Unused : DiagGroup<"unused",
                       [UnusedArgument, UnusedFunction, UnusedLabel,
                        // UnusedParameter, (matches GCC's behavior)
                        // UnusedTemplate, (clean-up libc++ before enabling)
                        // UnusedMemberFunction, (clean-up llvm before enabling)
                        UnusedPrivateField, UnusedLambdaCapture,
                        UnusedLocalTypedef, UnusedValue, UnusedVariable,
                        UnusedButSetVariable, UnusedPropertyIvar]>,
                        DiagCategory<"Unused Entity Issue">;

// Format settings.
def FormatInvalidSpecifier : DiagGroup<"format-invalid-specifier">;
def FormatSecurity : DiagGroup<"format-security">;
def FormatNonStandard : DiagGroup<"format-non-iso">;
def FormatY2K : DiagGroup<"format-y2k">;
def FormatPedantic : DiagGroup<"format-pedantic">;
def FormatSignedness : DiagGroup<"format-signedness">;
def FormatTypeConfusion : DiagGroup<"format-type-confusion">;

def FormatOverflowNonKprintf: DiagGroup<"format-overflow-non-kprintf">;
def FormatOverflow: DiagGroup<"format-overflow", [FormatOverflowNonKprintf]>;
def FormatTruncationNonKprintf: DiagGroup<"format-truncation-non-kprintf">;
def FormatTruncation: DiagGroup<"format-truncation", [FormatTruncationNonKprintf]>;

def Format : DiagGroup<"format",
                       [FormatExtraArgs, FormatZeroLength, NonNull,
                        FormatSecurity, FormatY2K, FormatInvalidSpecifier,
                        FormatInsufficientArgs, FormatOverflow, FormatTruncation]>,
             DiagCategory<"Format String Issue">;
def FormatNonLiteral : DiagGroup<"format-nonliteral">;
def Format2 : DiagGroup<"format=2",
                        [FormatNonLiteral, FormatSecurity, FormatY2K]>;

def TypeSafety : DiagGroup<"type-safety">;

def IncompatibleExceptionSpec : DiagGroup<"incompatible-exception-spec">;

def IntToVoidPointerCast : DiagGroup<"int-to-void-pointer-cast">;
def IntToPointerCast : DiagGroup<"int-to-pointer-cast",
                                 [IntToVoidPointerCast]>;
def VoidPointerToEnumCast : DiagGroup<"void-pointer-to-enum-cast">;
def VoidPointerToIntCast : DiagGroup<"void-pointer-to-int-cast",
                                     [VoidPointerToEnumCast]>;
def PointerToEnumCast : DiagGroup<"pointer-to-enum-cast",
                                  [VoidPointerToEnumCast]>;
def PointerToIntCast : DiagGroup<"pointer-to-int-cast",
                                 [PointerToEnumCast, VoidPointerToIntCast]>;
def VoidPointerDeref : DiagGroup<"void-ptr-dereference">;

def FUseLdPath : DiagGroup<"fuse-ld-path">;

def Move : DiagGroup<"move", [
    PessimizingMove,
    RedundantMove,
    ReturnStdMove,
    SelfMove
  ]>;

def Extra : DiagGroup<"extra", [
    DeprecatedCopy,
    MissingFieldInitializers,
    IgnoredQualifiers,
    InitializerOverrides,
    SemiBeforeMethodBody,
    MissingMethodReturnType,
    SignCompare,
    UnusedParameter,
    UnusedButSetParameter,
    NullPointerArithmetic,
    NullPointerSubtraction,
    EmptyInitStatement,
    StringConcatation,
    FUseLdPath,
    CastFunctionTypeMismatch,
  ]>;

def Most : DiagGroup<"most", [
    ArrayParameter,
    BoolOperation,
    CharSubscript,
    Comment,
    DeleteNonVirtualDtor,
    Format,
    ForLoopAnalysis,
    FrameAddress,
    Implicit,
    InfiniteRecursion,
    IntInBoolContext,
    MismatchedTags,
    MissingBraces,
    Move,
    MultiChar,
    RangeLoopConstruct,
    Reorder,
    ReturnType,
    SelfAssignment,
    SelfMove,
    SizeofArrayArgument,
    SizeofArrayDecay,
    StringPlusInt,
    TautologicalCompare,
    Trigraphs,
    Uninitialized,
    UnknownPragmas,
    Unused,
    VolatileRegisterVar,
    ObjCMissingSuperCalls,
    ObjCDesignatedInit,
    ObjCFlexibleArray,
    OverloadedVirtual,
    PrivateExtern,
    SelTypeCast,
    ExternCCompat,
    UserDefinedWarnings
 ]>;

// Thread Safety warnings
def ThreadSafetyAttributes : DiagGroup<"thread-safety-attributes">;
def ThreadSafetyAnalysis   : DiagGroup<"thread-safety-analysis">;
def ThreadSafetyPrecise    : DiagGroup<"thread-safety-precise">;
def ThreadSafetyReferenceReturn  : DiagGroup<"thread-safety-reference-return">;
def ThreadSafetyReference  : DiagGroup<"thread-safety-reference",
                                             [ThreadSafetyReferenceReturn]>;
def ThreadSafetyNegative   : DiagGroup<"thread-safety-negative">;
def ThreadSafety : DiagGroup<"thread-safety",
                             [ThreadSafetyAttributes,
                              ThreadSafetyAnalysis,
                              ThreadSafetyPrecise,
                              ThreadSafetyReference]>;
def ThreadSafetyVerbose : DiagGroup<"thread-safety-verbose">;
def ThreadSafetyBeta : DiagGroup<"thread-safety-beta">;

// Uniqueness Analysis warnings
def Consumed       : DiagGroup<"consumed">;

// Note that putting warnings in -Wall will not disable them by default. If a
// warning should be active _only_ when -Wall is passed in, mark it as
// DefaultIgnore in addition to putting it here.
def All : DiagGroup<"all", [Most, Parentheses, Switch, SwitchBool,
                            MisleadingIndentation, PackedNonPod,
                            VLACxxExtension]>;

// Warnings that should be in clang-cl /w4.
def : DiagGroup<"CL4", [All, Extra]>;

// Warnings enabled by -pedantic.  This is magically filled in by TableGen.
def Pedantic : DiagGroup<"pedantic">;

// Aliases.
def : DiagGroup<"", [Extra]>;                   // -W = -Wextra
def : DiagGroup<"endif-labels", [ExtraTokens]>; // -Wendif-labels=-Wextra-tokens
def : DiagGroup<"cpp", [PoundWarning]>;         // -Wcpp = -W#warnings
def : DiagGroup<"comments", [Comment]>;         // -Wcomments = -Wcomment
def : DiagGroup<"conversion-null",
                [NullConversion]>; // -Wconversion-null = -Wnull-conversion
def : DiagGroup<"bool-conversions",
                [BoolConversion]>; // -Wbool-conversions  = -Wbool-conversion
def : DiagGroup<"int-conversions",
                [IntConversion]>; // -Wint-conversions = -Wint-conversion
def : DiagGroup<"vector-conversions",
                [VectorConversion]>; // -Wvector-conversions = -Wvector-conversion
def : DiagGroup<"unused-local-typedefs", [UnusedLocalTypedef]>;
                // -Wunused-local-typedefs = -Wunused-local-typedef

// A warning group for warnings that we want to have on by default in clang,
// but which aren't on by default in GCC.
def NonGCC : DiagGroup<"non-gcc",
    [SignCompare, Conversion, LiteralRange]>;

def CXX14Attrs : DiagGroup<"c++14-attribute-extensions">;
def CXX17Attrs : DiagGroup<"c++17-attribute-extensions">;
def CXX20Attrs : DiagGroup<"c++20-attribute-extensions">;
def CXX23Attrs : DiagGroup<"c++23-attribute-extensions">;
def FutureAttrs : DiagGroup<"future-attribute-extensions", [CXX14Attrs,
                                                            CXX17Attrs,
                                                            CXX20Attrs,
                                                            CXX23Attrs]>;

def CXX23AttrsOnLambda : DiagGroup<"c++23-lambda-attributes">;

// A warning group for warnings about using C++11 features as extensions in
// earlier C++ versions.
def CXX11 : DiagGroup<"c++11-extensions", [CXX11ExtraSemi, CXX11InlineNamespace,
                                           CXX11LongLong]>;

// A warning group for warnings about using C++14 features as extensions in
// earlier C++ versions.
def CXX14 : DiagGroup<"c++14-extensions", [CXX14BinaryLiteral, CXX14Attrs]>;

// A warning group for warnings about using C++17 features as extensions in
// earlier C++ versions.
def CXX17 : DiagGroup<"c++17-extensions", [CXX17Attrs]>;

// A warning group for warnings about using C++20 features as extensions in
// earlier C++ versions.
def CXX20 : DiagGroup<"c++20-extensions", [CXX20Designator, CXX20Attrs]>;

// A warning group for warnings about using C++23 features as extensions in
// earlier C++ versions.
def CXX23 : DiagGroup<"c++23-extensions", [CXX23AttrsOnLambda]>;

// A warning group for warnings about using C++26 features as extensions in
// earlier C++ versions.
def CXX26 : DiagGroup<"c++26-extensions">;

def : DiagGroup<"c++0x-extensions", [CXX11]>;
def : DiagGroup<"c++1y-extensions", [CXX14]>;
def : DiagGroup<"c++1z-extensions", [CXX17]>;
def : DiagGroup<"c++2a-extensions", [CXX20]>;
def : DiagGroup<"c++2b-extensions", [CXX23]>;
def : DiagGroup<"c++2c-extensions", [CXX26]>;

def DelegatingCtorCycles :
  DiagGroup<"delegating-ctor-cycles">;

// A warning group for warnings about using C11 features as extensions.
def C11 : DiagGroup<"c11-extensions">;

// A warning group for warnings about using C99 features as extensions.
def C99 : DiagGroup<"c99-extensions", [C99Designator]>;

// A warning group for warnings about using C23 features as extensions.
def C23 : DiagGroup<"c23-extensions">;

def : DiagGroup<"c2x-extensions", [C23]>;

// A warning group for warnings about using C2y features as extensions.
def C2y : DiagGroup<"c2y-extensions">;

// Previously supported warning group which is no longer pertinent as binary
// literals are a C++14 and C23 extension now instead of a GNU extension.
def GNUBinaryLiteral : DiagGroup<"gnu-binary-literal">;

// A warning group for warnings about GCC extensions.
def GNU : DiagGroup<"gnu", [GNUAlignofExpression, GNUAnonymousStruct,
                            GNUAutoType, GNUBinaryLiteral, GNUCaseRange,
                            GNUComplexInteger, GNUCompoundLiteralInitializer,
                            GNUConditionalOmittedOperand, GNUDesignator,
                            GNUEmptyStruct,
                            VLAExtension, GNUFlexibleArrayInitializer,
                            GNUFlexibleArrayUnionMember, GNUFoldingConstant,
                            GNUImaginaryConstant, GNUIncludeNext,
                            GNULabelsAsValue, GNULineMarker, GNUNullPointerArithmetic,
                            GNUOffsetofExtensions, GNUPointerArith,
                            RedeclaredClassMember, GNURedeclaredEnum,
                            GNUStatementExpression, GNUStaticFloatInit,
                            GNUStringLiteralOperatorTemplate, GNUUnionCast,
                            GNUVariableSizedTypeNotAtEnd, ZeroLengthArray,
                            GNUZeroLineDirective,
                            GNUZeroVariadicMacroArguments]>;
// A warning group for warnings about code that clang accepts but gcc doesn't.
def GccCompat : DiagGroup<"gcc-compat">;

// A warning group for warnings about code that may be incompatible on AIX.
def AIXCompat : DiagGroup<"aix-compat">;

// Warnings for Microsoft extensions.
def MicrosoftCharize : DiagGroup<"microsoft-charize">;
def MicrosoftDrectveSection : DiagGroup<"microsoft-drectve-section">;
def MicrosoftInclude : DiagGroup<"microsoft-include">;
def MicrosoftCppMacro : DiagGroup<"microsoft-cpp-macro">;
def MicrosoftFixedEnum : DiagGroup<"microsoft-fixed-enum">;
def MicrosoftSealed : DiagGroup<"microsoft-sealed">;
def MicrosoftAbstract : DiagGroup<"microsoft-abstract">;
def MicrosoftUnqualifiedFriend : DiagGroup<"microsoft-unqualified-friend">;
def MicrosoftExceptionSpec : DiagGroup<"microsoft-exception-spec">;
def MicrosoftUsingDecl : DiagGroup<"microsoft-using-decl">;
def MicrosoftMutableReference : DiagGroup<"microsoft-mutable-reference">;
def MicrosoftPureDefinition : DiagGroup<"microsoft-pure-definition">;
def MicrosoftUnionMemberReference : DiagGroup<
    "microsoft-union-member-reference">;
def MicrosoftExplicitConstructorCall : DiagGroup<
    "microsoft-explicit-constructor-call">;
def MicrosoftEnumValue : DiagGroup<"microsoft-enum-value">;
def MicrosoftDefaultArgRedefinition :
    DiagGroup<"microsoft-default-arg-redefinition">;
def MicrosoftTemplateShadow : DiagGroup<"microsoft-template-shadow">;
def MicrosoftTemplate : DiagGroup<"microsoft-template", [MicrosoftTemplateShadow]>;
def MicrosoftInconsistentDllImport : DiagGroup<"inconsistent-dllimport">;
def MicrosoftRedeclareStatic : DiagGroup<"microsoft-redeclare-static">;
def MicrosoftEnumForwardReference :
    DiagGroup<"microsoft-enum-forward-reference">;
def MicrosoftGoto : DiagGroup<"microsoft-goto">;
def MicrosoftFlexibleArray : DiagGroup<"microsoft-flexible-array">;
def MicrosoftExtraQualification : DiagGroup<"microsoft-extra-qualification">;
def MicrosoftCast : DiagGroup<"microsoft-cast">;
def MicrosoftConstInit : DiagGroup<"microsoft-const-init">;
def MicrosoftVoidPseudoDtor : DiagGroup<"microsoft-void-pseudo-dtor">;
def MicrosoftAnonTag : DiagGroup<"microsoft-anon-tag">;
def MicrosoftCommentPaste : DiagGroup<"microsoft-comment-paste">;
def MicrosoftEndOfFile : DiagGroup<"microsoft-end-of-file">;
def MicrosoftInaccessibleBase : DiagGroup<"microsoft-inaccessible-base">;
def MicrosoftStaticAssert : DiagGroup<"microsoft-static-assert">;
def MicrosoftInitFromPredefined : DiagGroup<"microsoft-init-from-predefined">;
def MicrosoftStringLiteralFromPredefined : DiagGroup<
    "microsoft-string-literal-from-predefined">;

// Aliases.
def : DiagGroup<"msvc-include", [MicrosoftInclude]>;
                // -Wmsvc-include = -Wmicrosoft-include

// Warnings group for warnings about Microsoft extensions.
def Microsoft : DiagGroup<"microsoft",
    [MicrosoftCharize, MicrosoftDrectveSection, MicrosoftInclude,
     MicrosoftCppMacro, MicrosoftFixedEnum, MicrosoftSealed, MicrosoftAbstract,
     MicrosoftUnqualifiedFriend, MicrosoftExceptionSpec, MicrosoftUsingDecl,
     MicrosoftMutableReference, MicrosoftPureDefinition,
     MicrosoftUnionMemberReference, MicrosoftExplicitConstructorCall,
     MicrosoftEnumValue, MicrosoftDefaultArgRedefinition, MicrosoftTemplate,
     MicrosoftRedeclareStatic, MicrosoftEnumForwardReference, MicrosoftGoto,
     MicrosoftFlexibleArray, MicrosoftExtraQualification, MicrosoftCast,
     MicrosoftConstInit, MicrosoftVoidPseudoDtor, MicrosoftAnonTag,
     MicrosoftCommentPaste, MicrosoftEndOfFile, MicrosoftStaticAssert,
     MicrosoftInitFromPredefined, MicrosoftStringLiteralFromPredefined,
     MicrosoftInconsistentDllImport]>;

def ClangClPch : DiagGroup<"clang-cl-pch">;

def ObjCNonUnifiedException : DiagGroup<"objc-nonunified-exceptions">;

def ObjCProtocolMethodImpl : DiagGroup<"objc-protocol-method-implementation">;

def ObjCNoPropertyAutoSynthesis : DiagGroup<"objc-property-synthesis">;

// ObjC API warning groups.
def ObjCRedundantLiteralUse : DiagGroup<"objc-redundant-literal-use">;
def ObjCRedundantAPIUse : DiagGroup<"objc-redundant-api-use", [
    ObjCRedundantLiteralUse
  ]>;

def ObjCCocoaAPI : DiagGroup<"objc-cocoa-api", [
    ObjCRedundantAPIUse
  ]>;

def ObjCStringComparison : DiagGroup<"objc-string-compare">;
def ObjCStringConcatenation : DiagGroup<"objc-string-concatenation">;
def ObjCLiteralComparison : DiagGroup<"objc-literal-compare", [
    ObjCStringComparison
  ]>;

def ObjCSignedCharBool : DiagGroup<"objc-signed-char-bool",
  [ObjCSignedCharBoolImplicitIntConversion,
   ObjCSignedCharBoolImplicitFloatConversion,
   ObjCBoolConstantConversion,
   TautologicalObjCBoolCompare]>;

def ObjCPotentiallyDirectSelector : DiagGroup<"potentially-direct-selector">;
def ObjCStrictPotentiallyDirectSelector :
  DiagGroup<"strict-potentially-direct-selector",
            [ObjCPotentiallyDirectSelector]>;

// Inline ASM warnings.
def ASMOperandWidths : DiagGroup<"asm-operand-widths">;
def ASM : DiagGroup<"asm", [
    ASMOperandWidths
  ]>;

// Linker warnings.
def LinkerWarnings : DiagGroup<"linker-warnings">;

// OpenMP warnings.
def SourceUsesOpenMP : DiagGroup<"source-uses-openmp">;
def OpenMPClauses : DiagGroup<"openmp-clauses">;
def OpenMPLoopForm : DiagGroup<"openmp-loop-form">;
def OpenMPMapping : DiagGroup<"openmp-mapping">;
def OpenMPTarget : DiagGroup<"openmp-target", [OpenMPMapping]>;
def OpenMPPre51Compat : DiagGroup<"pre-openmp-51-compat">;
def OpenMP51Ext : DiagGroup<"openmp-51-extensions">;
def OpenMPExtensions : DiagGroup<"openmp-extensions">;
def OpenMPTargetException : DiagGroup<"openmp-target-exception">;
def OpenMP : DiagGroup<"openmp", [
    SourceUsesOpenMP, OpenMPClauses, OpenMPLoopForm, OpenMPTarget,
    OpenMPMapping, OpenMP51Ext, OpenMPExtensions, OpenMPTargetException
  ]>;

// OpenACC warnings.
def SourceUsesOpenACC : DiagGroup<"source-uses-openacc">;
def OpenACC : DiagGroup<"openacc", [SourceUsesOpenACC]>;

// Backend warnings.
def BackendInlineAsm : DiagGroup<"inline-asm">;
def BackendSourceMgr : DiagGroup<"source-mgr">;
def BackendFrameLargerThan : DiagGroup<"frame-larger-than">{
 code Documentation = [{
More fine grained information about the stack layout is available by adding the
`-Rpass-analysis=stack-frame-layout` command-line flag to the compiler
invocation.

The diagnostic information can be saved to a file in a machine readable format,
like YAML by adding the `-foptimization-record-file=<file>` command-line flag.

Results can be filtered by function name by passing
`-mllvm -filter-print-funcs=foo`, where `foo` is the target function's name.

   .. code-block:: console

      clang -c a.cpp -Rpass-analysis=stack-frame-layout -mllvm -filter-print-funcs=foo

   .. code-block:: console

      clang -c a.cpp -Rpass-analysis=stack-frame-layout -foptimization-record-file=<file>
}];
}
// Compatibility flag name from old versions of Clang.
def : DiagGroup<"frame-larger-than=", [BackendFrameLargerThan]>;
def BackendPlugin : DiagGroup<"backend-plugin">;
def RemarkBackendPlugin : DiagGroup<"remark-backend-plugin">;
def BackendOptimizationRemark : DiagGroup<"pass">;
def BackendOptimizationRemarkMissed : DiagGroup<"pass-missed">;
def BackendOptimizationRemarkAnalysis : DiagGroup<"pass-analysis">;
def BackendOptimizationFailure : DiagGroup<"pass-failed">;
def BackendWarningAttributes : DiagGroup<"attribute-warning">;

// Instrumentation based profiling warnings.
def ProfileInstrMissing : DiagGroup<"profile-instr-missing">;
def ProfileInstrOutOfDate : DiagGroup<"profile-instr-out-of-date">;
def ProfileInstrUnprofiled : DiagGroup<"profile-instr-unprofiled">;
def MisExpect : DiagGroup<"misexpect">;

// AddressSanitizer frontend instrumentation remarks.
def SanitizeAddressRemarks : DiagGroup<"sanitize-address">;

// Issues with serialized diagnostics.
def SerializedDiagnostics : DiagGroup<"serialized-diagnostics">;

// A warning group for warnings about code that clang accepts when
// compiling CUDA C/C++ but which is not compatible with the CUDA spec.
def CudaCompat : DiagGroup<"cuda-compat">;

// Warning about unknown CUDA SDK version.
def CudaUnknownVersion: DiagGroup<"unknown-cuda-version">;

// A warning group for warnings about features supported by HIP but
// ignored by CUDA.
def HIPOnly : DiagGroup<"hip-only">;

// Warning about mixed HIP and OpenMP compilation / target offloading.
def HIPOpenMPOffloading: DiagGroup<"hip-omp-target-directives">;

// Warning about multiple GPUs are detected.
def MultiGPU: DiagGroup<"multi-gpu">;

// Warnings which cause linking of the runtime libraries like
// libc and the CRT to be skipped.
def AVRRtlibLinkingQuirks : DiagGroup<"avr-rtlib-linking-quirks">;

// A warning group related to AArch64 SME function attribues.
def AArch64SMEAttributes : DiagGroup<"aarch64-sme-attributes">;

// A warning group for things that will change semantics in the future.
def FutureCompat : DiagGroup<"future-compat">;

def InvalidOrNonExistentDirectory : DiagGroup<"invalid-or-nonexistent-directory">;

def OptionIgnored : DiagGroup<"option-ignored">;

def UnknownArgument : DiagGroup<"unknown-argument">;

def UnsupportedABI : DiagGroup<"unsupported-abi">;

// A warning group for warnings about code that clang accepts when
// compiling OpenCL C/C++ but which is not compatible with the SPIR(-V) spec.
def SpirCompat : DiagGroup<"spir-compat">;
def : DiagGroup<"spirv-compat", [SpirCompat]>; // Alias.

// Warning for the GlobalISel options.
def GlobalISel : DiagGroup<"global-isel">;

// A warning group for the GNU extension to allow mixed specifier types for
// target-clones multiversioning.
def TargetClonesMixedSpecifiers : DiagGroup<"target-clones-mixed-specifiers">;

// A warning group specifically for warnings related to function
// multiversioning.
def FunctionMultiVersioning
    : DiagGroup<"function-multiversion", [TargetClonesMixedSpecifiers]>;

def NoDeref : DiagGroup<"noderef">;

// -fbounds-safety and bounds annotation related warnings
def BoundsSafetyCountedByEltTyUnknownSize :
  DiagGroup<"bounds-safety-counted-by-elt-type-unknown-size">;

// A group for cross translation unit static analysis related warnings.
def CrossTU : DiagGroup<"ctu">;

def CTADMaybeUnsupported : DiagGroup<"ctad-maybe-unsupported">;

def FortifySource : DiagGroup<"fortify-source", [FormatOverflow, FormatTruncation]>;

def MaxTokens : DiagGroup<"max-tokens"> {
  code Documentation = [{
The warning is issued if the number of pre-processor tokens exceeds
the token limit, which can be set in three ways:

1. As a limit at a specific point in a file, using the ``clang max_tokens_here``
   pragma:

   .. code-block:: c++

      #pragma clang max_tokens_here 1234

2. As a per-translation unit limit, using the ``-fmax-tokens=`` command-line
   flag:

   .. code-block:: console

      clang -c a.cpp -fmax-tokens=1234

3. As a per-translation unit limit using the ``clang max_tokens_total`` pragma,
   which works like and overrides the ``-fmax-tokens=`` flag:

   .. code-block:: c++

      #pragma clang max_tokens_total 1234

These limits can be helpful in limiting code growth through included files.

Setting a token limit of zero means no limit.

Note that the warning is disabled by default, so -Wmax-tokens must be used
in addition with the pragmas or -fmax-tokens flag to get any warnings.
}];
}

def WebAssemblyExceptionSpec : DiagGroup<"wasm-exception-spec">;

def RTTI : DiagGroup<"rtti">;

def OpenCLCoreFeaturesDiagGroup : DiagGroup<"pedantic-core-features">;

// Warnings and extensions to make preprocessor macro usage pedantic.
def PedanticMacros : DiagGroup<"pedantic-macros",
                    [DeprecatedPragma,
                     MacroRedefined,
                     BuiltinMacroRedefined,
                     RestrictExpansionMacro,
                     FinalMacro]>;

def BranchProtection : DiagGroup<"branch-protection">;

// HLSL diagnostic groups
// Warnings for HLSL Clang extensions
def HLSLExtension : DiagGroup<"hlsl-extensions">;

// Warning for mix packoffset and non-packoffset.
def HLSLMixPackOffset : DiagGroup<"mix-packoffset">;

// Warnings for DXIL validation
def DXILValidation : DiagGroup<"dxil-validation">;

// Warning for HLSL API availability
def HLSLAvailability : DiagGroup<"hlsl-availability">;

// Warnings and notes related to const_var_decl_type attribute checks
def ReadOnlyPlacementChecks : DiagGroup<"read-only-types">;

// Warnings and fixes to support the "safe buffers" programming model.
def UnsafeBufferUsageInContainer : DiagGroup<"unsafe-buffer-usage-in-container">;
def UnsafeBufferUsage : DiagGroup<"unsafe-buffer-usage", [UnsafeBufferUsageInContainer]>;

// Warnings and notes related to the function effects system underlying
// the nonblocking and nonallocating attributes.
def FunctionEffects : DiagGroup<"function-effects">;

// Warnings and notes InstallAPI verification.
def InstallAPIViolation : DiagGroup<"installapi-violation">;

// Warnings related to _BitInt extension
def BitIntExtension : DiagGroup<"bit-int-extension">;

// Warnings about misuse of ExtractAPI options.
def ExtractAPIMisuse : DiagGroup<"extractapi-misuse">;

// Warnings about using the non-standard extension having an explicit specialization
// with a storage class specifier.
def ExplicitSpecializationStorageClass : DiagGroup<"explicit-specialization-storage-class">;
