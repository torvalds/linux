//===--- Builtins.td - Builtins function info database-----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "clang/Basic/BuiltinsBase.td"

class FPMathTemplate : Template<["float", "double", "long double"],
                                ["f",     "",       "l"]>;

class FPMathWithF16Template :
    Template<["float", "double", "long double", "__fp16"],
             ["f",     "",       "l",           "f16"]>;

class FPMathWithF16F128Template :
    Template<["float", "double", "long double", "__fp16", "__float128"],
             ["f",     "",       "l",           "f16",    "f128"]>;

class FPMathWithF128Template :
    Template<["float", "double", "long double", "__float128"],
             ["f",     "",       "l",           "f128"]>;

class F16F128MathTemplate : Template<["__fp16", "__float128"],
                                     ["f16",    "f128"]>;

class IntMathTemplate : Template<["int", "long int", "long long int"],
                                 ["",     "l",       "ll"], /*AsPrefix=*/1>;

class MSInt8_16_32Template : Template<["char", "short", "msint32_t"],
                                      ["8",    "16",    ""]>;

class Int8_16_32_64Template
    : Template<["char", "short", "int", "long long int"],
               ["8",    "16",    "32",  "64"]>;

class MSInt8_16_32_64Template
    : Template<["char", "short", "msint32_t", "long long int"],
               ["8",    "16",    "",          "64"]>;

class MSInt16_32Template : Template<["short", "msint32_t"],
                                    ["16",    ""]>;

class MSUInt16_32_64Template :
    Template<["unsigned short", "unsigned int", "uint64_t"],
             ["16",             "",             "64"]>;

class MSInt32_64Template : Template<["msint32_t", "int64_t"],
                                    ["",          "64"]>;

class FloatDoubleTemplate : Template<["float", "double"],
                                     ["f",     ""]>;

// FIXME: These assume that char -> i8, short -> i16, int -> i32,
// long long -> i64.
class SyncBuiltinsTemplate :
    Template<["char", "short", "int", "long long int", "__int128_t"],
             ["1",    "2",     "4",   "8",             "16"]>;

class BitInt8_16_32_64BuiltinsTemplate :
    Template<["unsigned char", "unsigned short", "uint32_t", "uint64_t"],
             ["8",             "16",             "32",       "64"]>;

class BitShort_Int_Long_LongLongTemplate :
    Template<["short", "int", "long int", "long long int"],
             ["s",     "",    "l",        "ll"]>;

class BitInt_Long_LongLongTemplate :
    Template<["int", "long int", "long long int"],
             ["",    "l",        "ll"]>;

// Most of the types used in the prototypes are types from C, C++ or ObjC. There
// are a few builtin-specific types and qualifiers.
//
// builtin-specific types:
// - __builtin_va_list: This is the internal representation for va_lists
// - __builtin_va_list_ref: A reference-like type to __builtin_va_list
// - msint32_t: 'int' size if target is LP64, 'L' otherwise.
//
// builtin-specific qualifiers:
// - _Constant: Argument has to constant-fold to an integer constant expression

// __fp16 and __float128 builtin variants of libc/libm functions.
def AcosF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_acos"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def AcoshF128 : Builtin {
  let Spellings = ["__builtin_acoshf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def AsinF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_asin"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def AsinhF128 : Builtin {
  let Spellings = ["__builtin_asinhf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def AtanF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_atan"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def AtanhF128 : Builtin {
  let Spellings = ["__builtin_atanhf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def CbrtF128 : Builtin {
  let Spellings = ["__builtin_cbrtf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "__float128(__float128)";
}

def CeilF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_ceil"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

def CosF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_cos"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def CoshF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_cosh"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def ErfF128 : Builtin {
  let Spellings = ["__builtin_erff128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def ErfcF128 : Builtin {
  let Spellings = ["__builtin_erfcf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def ExpF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_exp"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Exp2F16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_exp2"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Exp10F16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_exp10"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Expm1F128 : Builtin {
  let Spellings = ["__builtin_expm1f128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def FdimF128 : Builtin {
  let Spellings = ["__builtin_fdimf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def FloorF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_floor"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

def FmaF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_fma"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T, T)";
}

def FmaxF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_fmax"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const, Constexpr];
  let Prototype = "T(T, T)";
}

def FminF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_fmin"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const, Constexpr];
  let Prototype = "T(T, T)";
}

def Atan2F128 : Builtin {
  let Spellings = ["__builtin_atan2f128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def CopysignF16 : Builtin {
  let Spellings = ["__builtin_copysignf16"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "__fp16(__fp16, __fp16)";
}

def CopysignF128 : Builtin {
  let Spellings = ["__builtin_copysignf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const, Constexpr];
  let Prototype = "__float128(__float128, __float128)";
}

def FabsF16 : Builtin {
  let Spellings = ["__builtin_fabsf16"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "__fp16(__fp16)";
}

def FabsF128 : Builtin {
  let Spellings = ["__builtin_fabsf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const, Constexpr];
  let Prototype = "__float128(__float128)";
}

def FmodF16F128 : F16F128MathTemplate, Builtin {
  let Spellings = ["__builtin_fmod"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
}

def FrexpF16F128 : F16F128MathTemplate, Builtin {
  let Spellings = ["__builtin_frexp"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "T(T, int*)";
}

def HugeVal : Builtin, FPMathWithF128Template {
  let Spellings = ["__builtin_huge_val"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T()";
}

def HugeValF16 : Builtin {
       let Spellings = ["__builtin_huge_valf16"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "_Float16()";
}

def Inf : Builtin, FPMathWithF128Template {
  let Spellings = ["__builtin_inf"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T()";
}

def InfF16 : Builtin {
  let Spellings = ["__builtin_inff16"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "_Float16()";
}

def LdexpF16F128 : F16F128MathTemplate, Builtin {
  let Spellings = ["__builtin_ldexp"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, int)";
}

def ModfF128 : Builtin {
  let Spellings = ["__builtin_modff128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "__float128(__float128, __float128*)";
}

// This isn't a FPMathWithF16F128Template because the f16
// version takes a _Float16 for some reason.
def NanF16 : Builtin {
  let Spellings = ["__builtin_nanf16"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Pure, Constexpr];
  let Prototype = "_Float16(char const*)";
}

def NanF128 : Builtin {
  let Spellings = ["__builtin_nanf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Pure, Constexpr];
  let Prototype = "__float128(char const*)";
}

def Nans : Builtin,
    Template<["float", "double", "long double", "_Float16", "__float128"],
             ["f",     "",       "l",           "f16",      "f128"]> {
  let Spellings = ["__builtin_nans"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Pure, Constexpr];
  let Prototype = "T(char const*)";
}

def PowI : Builtin, FPMathTemplate {
  let Spellings = ["__builtin_powi"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T, int)";
}

def PowF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_pow"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
}

def HypotF128 : Builtin {
  let Spellings = ["__builtin_hypotf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def ILogbF128 : Builtin {
  let Spellings = ["__builtin_ilogbf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "int(__float128)";
}

def LgammaF128 : Builtin {
  let Spellings = ["__builtin_lgammaf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "__float128(__float128)";
}

def LLrintF128 : Builtin {
  let Spellings = ["__builtin_llrintf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long long int(__float128)";
}

def LLroundF128 : Builtin {
  let Spellings = ["__builtin_llroundf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long long int(__float128)";
}

def Log10F16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_log10"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Log1pF128 : Builtin {
  let Spellings = ["__builtin_log1pf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def Log2F16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_log2"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def LogbF128 : Builtin {
  let Spellings = ["__builtin_logbf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def LogF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_log"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def LrintF128 : Builtin {
  let Spellings = ["__builtin_lrintf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long int(__float128)";
}

def LroundF128 : Builtin {
  let Spellings = ["__builtin_lroundf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long int(__float128)";
}

def NearbyintF128 : Builtin {
  let Spellings = ["__builtin_nearbyintf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "__float128(__float128)";
}

def NextafterF128 : Builtin {
  let Spellings = ["__builtin_nextafterf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def NexttowardF128 : Builtin {
  let Spellings = ["__builtin_nexttowardf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def RemainderF128 : Builtin {
  let Spellings = ["__builtin_remainderf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, __float128)";
}

def RemquoF128 : Builtin {
  let Spellings = ["__builtin_remquof128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "__float128(__float128, __float128, int*)";
}

def RintF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_rint"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

def RoundF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_round"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

def RoundevenF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_roundeven"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

def ScanlblnF128 : Builtin {
  let Spellings = ["__builtin_scalblnf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, long int)";
}

def ScanlbnF128 : Builtin {
  let Spellings = ["__builtin_scalbnf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128, int)";
}

def SinF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_sin"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def SinhF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_sinh"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def SqrtF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_sqrt"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def TanF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_tan"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def TanhF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_tanh"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def TgammaF128 : Builtin {
  let Spellings = ["__builtin_tgammaf128"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "__float128(__float128)";
}

def TruncF16F128 : Builtin, F16F128MathTemplate {
  let Spellings = ["__builtin_trunc"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "T(T)";
}

// Access to floating point environment.
def BuiltinFltRounds : Builtin {
  let Spellings = ["__builtin_flt_rounds"];
  let Attributes = [NoThrow];
  let Prototype = "int()";
}

def BuiltinSetFltRounds : Builtin {
  let Spellings = ["__builtin_set_flt_rounds"];
  let Attributes = [NoThrow];
  let Prototype = "void(int)";
}

// GCC-compatible C99 CMPLX implementation.
def BuiltinComplex : Builtin {
  let Spellings = ["__builtin_complex"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void(...)";
}

// FP Comparison functions.
def IsGreater : Builtin {
  let Spellings = ["__builtin_isgreater"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def IsGreaterEqual : Builtin {
  let Spellings = ["__builtin_isgreaterequal"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def IsLess : Builtin {
  let Spellings = ["__builtin_isless"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def IsLessEqual : Builtin {
  let Spellings = ["__builtin_islessequal"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def IsLessGreater : Builtin {
  let Spellings = ["__builtin_islessgreater"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def IsUnordered : Builtin {
  let Spellings = ["__builtin_isunordered"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

// Unary FP classification.
def FPClassify : Builtin {
  let Spellings = ["__builtin_fpclassify"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(int, int, int, int, int, ...)";
}

def IsFinite : Builtin {
  let Spellings = ["__builtin_isfinite"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsInf : Builtin {
  let Spellings = ["__builtin_isinf"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsInfSign : Builtin {
  let Spellings = ["__builtin_isinf_sign"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsNan : Builtin {
  let Spellings = ["__builtin_isnan"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsNormal : Builtin {
  let Spellings = ["__builtin_isnormal"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsSubnormal : Builtin {
  let Spellings = ["__builtin_issubnormal"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsZero : Builtin {
  let Spellings = ["__builtin_iszero"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsSignaling : Builtin {
  let Spellings = ["__builtin_issignaling"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

def IsFPClass : Builtin {
  let Spellings = ["__builtin_isfpclass"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "int(...)";
}

// FP signbit builtins.
def Signbit : Builtin {
  let Spellings = ["__builtin_signbit"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const,
                    CustomTypeChecking];
  let Prototype = "int(...)";
}

def SignbitF : Builtin {
  let Spellings = ["__builtin_signbitf"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "int(float)";
}

def SignbitL : Builtin {
  let Spellings = ["__builtin_signbitl"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const];
  let Prototype = "int(long double)";
}

// Special FP builtins.
def Canonicalize : Builtin, FPMathWithF16Template {
  let Spellings = ["__builtin_canonicalize"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
}

// Builtins for arithmetic.
def Clz : Builtin, BitShort_Int_Long_LongLongTemplate {
  let Spellings = ["__builtin_clz"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(unsigned T)";
}

def Clzg : Builtin {
  let Spellings = ["__builtin_clzg"];
  let Attributes = [NoThrow, Const, Constexpr, CustomTypeChecking];
  let Prototype = "int(...)";
}

def Ctz : Builtin, BitShort_Int_Long_LongLongTemplate {
  let Spellings = ["__builtin_ctz"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(unsigned T)";
}

def Ctzg : Builtin {
  let Spellings = ["__builtin_ctzg"];
  let Attributes = [NoThrow, Const, Constexpr, CustomTypeChecking];
  let Prototype = "int(...)";
}

def FFS : Builtin, BitInt_Long_LongLongTemplate {
  let Spellings = ["__builtin_ffs"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Const, Constexpr];
  let Prototype = "int(T)";
}

def Parity : Builtin, BitInt_Long_LongLongTemplate {
  let Spellings = ["__builtin_parity"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(unsigned T)";
}

def Popcount : Builtin, BitInt_Long_LongLongTemplate {
  let Spellings = ["__builtin_popcount"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(unsigned T)";
}

def Popcountg : Builtin {
  let Spellings = ["__builtin_popcountg"];
  let Attributes = [NoThrow, Const, Constexpr, CustomTypeChecking];
  let Prototype = "int(...)";
}

def Clrsb : Builtin, BitInt_Long_LongLongTemplate {
  let Spellings = ["__builtin_clrsb"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(T)";
}

// The following builtins rely on that char == 8 bits, short == 16 bits and that
// there exists native types on the target that are 32- and 64-bits wide, unless
// these conditions are fulfilled these builtins will operate on a not intended
// bitwidth.
def BSwap : Builtin, Template<["unsigned short", "uint32_t", "uint64_t"],
                              ["16",             "32",       "64"]> {
  let Spellings = ["__builtin_bswap"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T)";
}

def Bitreverse : BitInt8_16_32_64BuiltinsTemplate, Builtin {
  let Spellings = ["__builtin_bitreverse"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T)";
}

def RotateLeft : BitInt8_16_32_64BuiltinsTemplate, Builtin {
  let Spellings = ["__builtin_rotateleft"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T, T)";
}

def RotateRight : BitInt8_16_32_64BuiltinsTemplate, Builtin {
  let Spellings = ["__builtin_rotateright"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T, T)";
}

// Random GCC builtins
// FIXME: The builtins marked FunctionWithBuiltinPrefix below should be
//        merged with the library definitions. They are currently not because
//        the attributes are different.

// Builtins for checking CPU features based on the GCC builtins.
def BuiltinCPUIs : Builtin {
  let Spellings = ["__builtin_cpu_is"];
  let Attributes = [NoThrow, Const];
  let Prototype = "bool(char const*)";
}

def BuiltinCPUSupports : Builtin {
  let Spellings = ["__builtin_cpu_supports"];
  let Attributes = [NoThrow, Const];
  let Prototype = "bool(char const*)";
}

def BuiltinCPUInit : Builtin {
  let Spellings = ["__builtin_cpu_init"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

def BuiltinCalloc : Builtin {
  let Spellings = ["__builtin_calloc"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(size_t, size_t)";
}

def BuiltinConstantP : Builtin {
  let Spellings = ["__builtin_constant_p"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, UnevaluatedArguments, Constexpr];
  let Prototype = "int(...)";
}

def BuiltinClassifyType : Builtin {
  let Spellings = ["__builtin_classify_type"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, UnevaluatedArguments, Constexpr];
  let Prototype = "int(...)";
}

def BuiltinCFStringMakeConstantString : Builtin {
  let Spellings = ["__builtin___CFStringMakeConstantString"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "constant_CFString const*(char const*)";
}

def BuiltinNSStringMakeConstantString : Builtin {
  let Spellings = ["__builtin___NSStringMakeConstantString"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "constant_CFString const*(char const*)";
}

def BuiltinVaStart : Builtin {
  let Spellings = ["__builtin_va_start"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(__builtin_va_list_ref, ...)";
}

def BuiltinStdargStart : Builtin {
  let Spellings = ["__builtin_stdarg_start"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(__builtin_va_list_ref, ...)";
}

def BuiltinAssumeAligned : Builtin {
  let Spellings = ["__builtin_assume_aligned"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void const*, size_t, ...)";
}

def BuiltinFree : Builtin {
  let Spellings = ["__builtin_free"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void(void*)";
}

def BuiltinMalloc : Builtin {
  let Spellings = ["__builtin_malloc"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(size_t)";
}

def BuiltinMemcpyInline : Builtin {
  let Spellings = ["__builtin_memcpy_inline"];
  let Attributes = [NoThrow];
  let Prototype = "void(void*, void const*, _Constant size_t)";
}

def BuiltinMempcpy : Builtin {
  let Spellings = ["__builtin_mempcpy"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, void const*, size_t)";
}

def BuiltinMemsetInline : Builtin {
  let Spellings = ["__builtin_memset_inline"];
  let Attributes = [NoThrow];
  let Prototype = "void(void*, int, _Constant size_t)";
}

def BuiltinStrcspn : Builtin {
  let Spellings = ["__builtin_strcspn"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "size_t(char const*, char const*)";
}

def BuiltinRealloc : Builtin {
  let Spellings = ["__builtin_realloc"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, size_t)";
}

def BuiltinReturnAddress : Builtin {
  let Spellings = ["__builtin_return_address"];
  let Attributes = [NoThrow];
  let Prototype = "void*(_Constant unsigned int)";
}

def ExtractReturnAddr : Builtin {
  let Spellings = ["__builtin_extract_return_addr"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*)";
}

def FrameAddress : Builtin {
  let Spellings = ["__builtin_frame_address"];
  let Attributes = [NoThrow];
  let Prototype = "void*(_Constant unsigned int)";
}

def ClearCache : Builtin {
  let Spellings = ["__builtin___clear_cache"];
  let Attributes = [NoThrow];
  let Prototype = "void(char*, char*)";
}

def BuiltinSetjmp : Builtin {
  let Spellings = ["__builtin_setjmp"];
  let Attributes = [ReturnsTwice];
  let Prototype = "int(void**)";
}

def BuiltinLongjmp : Builtin {
  let Spellings = ["__builtin_longjmp"];
  let Attributes = [NoReturn];
  let Prototype = "void(void**, int)";
}

def UnwindInit : Builtin {
  let Spellings = ["__builtin_unwind_init"];
  let Prototype = "void()";
}

def EHReturnDataRegNo : Builtin {
  let Spellings = ["__builtin_eh_return_data_regno"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "int(_Constant int)";
}

def ThreadPointer : Builtin {
  let Spellings = ["__builtin_thread_pointer"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void*()";
}

def Launder : Builtin {
  let Spellings = ["__builtin_launder"];
  let Attributes = [NoThrow, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void*)";
}

def IsConstantEvaluated : LangBuiltin<"CXX_LANG"> {
  let Spellings = ["__builtin_is_constant_evaluated"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool()";
}

// GCC exception builtins
def EHReturn : Builtin {
  let Spellings = ["__builtin_eh_return"];
  let Attributes = [NoReturn];
  // FIXME: Takes intptr_t, not size_t!
  let Prototype = "void(size_t, void*)";
}

def FrobReturnAddr : Builtin {
  let Spellings = ["__builtin_frob_return_addr"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*)";
}

def DWARF_CFA : Builtin {
  let Spellings = ["__builtin_dwarf_cfa"];
  let Attributes = [NoThrow];
  let Prototype = "void*()";
}

def InitDWARFRegSizeTable : Builtin {
  let Spellings = ["__builtin_init_dwarf_reg_size_table"];
  let Attributes = [NoThrow];
  let Prototype = "void(void*)";
}

def DWARFSpColumn : Builtin {
  let Spellings = ["__builtin_dwarf_sp_column"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned int()";
}

def ExtendPointer : Builtin {
  let Spellings = ["__builtin_extend_pointer"];
  let Attributes = [NoThrow];
  // _Unwind_Word == uint64_t
  let Prototype = "unsigned long long int(void*)";
}

// GCC Object size checking builtins.
def ObjectSize : Builtin {
  let Spellings = ["__builtin_object_size"];
  let Attributes = [NoThrow, UnevaluatedArguments, Constexpr];
  let Prototype = "size_t(void const*, int)";
}

def DynamicObjectSize : Builtin { // Clang only
  let Spellings = ["__builtin_dynamic_object_size"];
  let Attributes = [NoThrow, UnevaluatedArguments, Constexpr];
  let Prototype = "size_t(void const*, int)";
}

def MemcpyChk : Builtin {
  let Spellings = ["__builtin___memcpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, void const*, size_t, size_t)";
}

def MemccpyChk : Builtin {
  let Spellings = ["__builtin___memccpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, void const*, int, size_t, size_t)";
}

def MemmoveChk : Builtin {
  let Spellings = ["__builtin___memmove_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, void const*, size_t, size_t)";
}

def MempcpyChk : Builtin {
  let Spellings = ["__builtin___mempcpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, void const*, size_t, size_t)";
}

def MemsetChk : Builtin {
  let Spellings = ["__builtin___memset_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(void*, int, size_t, size_t)";
}

def StpcpyChk : Builtin {
  let Spellings = ["__builtin___stpcpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
}

def StrcatChk : Builtin {
  let Spellings = ["__builtin___strcat_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
}

def StrcpyChk : Builtin {
  let Spellings = ["__builtin___strcpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
}

def StrlcatChk : Builtin {
  let Spellings = ["__builtin___strlcat_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "size_t(char*, char const*, size_t, size_t)";
}

def StrlcpyChk : Builtin {
  let Spellings = ["__builtin___strlcpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "size_t(char*, char const*, size_t, size_t)";
}

def StrncatChk : Builtin {
  let Spellings = ["__builtin___strncat_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t, size_t)";
}

def StrncpyChk : Builtin {
  let Spellings = ["__builtin___strncpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t, size_t)";
}

def StpncpyChk : Builtin {
  let Spellings = ["__builtin___stpncpy_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "char*(char*, char const*, size_t, size_t)";
}

def SNPrintfChk : Builtin {
  let Spellings = ["__builtin___snprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, PrintfFormat<4>];
  let Prototype = "int(char* restrict, size_t, int, size_t, char const* restrict, ...)";
}

def SPrintfChk : Builtin {
  let Spellings = ["__builtin___sprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, PrintfFormat<3>];
  let Prototype = "int(char* restrict, int, size_t, char const* restrict, ...)";
}

def VSNPrintfChk : Builtin {
  let Spellings = ["__builtin___vsnprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, VPrintfFormat<4>];
  let Prototype = "int(char* restrict, size_t, int, size_t, char const* restrict, __builtin_va_list)";
}

def VSPrintfChk : Builtin {
  let Spellings = ["__builtin___vsprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, VPrintfFormat<3>];
  let Prototype = "int(char* restrict, int, size_t, char const* restrict, __builtin_va_list)";
}

def FPrintfChk : Builtin {
  let Spellings = ["__builtin___fprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, PrintfFormat<2>];
  let Prototype = "int(FILE* restrict, int, char const* restrict, ...)";
}

def PrintfChk : Builtin {
  let Spellings = ["__builtin___printf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, PrintfFormat<1>];
  let Prototype = "int(int, char const* restrict, ...)";
}

def VFPrintfChk : Builtin {
  let Spellings = ["__builtin___vfprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, VPrintfFormat<2>];
  let Prototype = "int(FILE* restrict, int, char const* restrict, __builtin_va_list)";
}

def VPrintfChk : Builtin {
  let Spellings = ["__builtin___vprintf_chk"];
  let Attributes = [FunctionWithBuiltinPrefix, VPrintfFormat<1>];
  let Prototype = "int(int, char const* restrict, __builtin_va_list)";
}

def Unpredictable : Builtin {
  let Spellings = ["__builtin_unpredictable"];
  let Attributes = [NoThrow, Const];
  let Prototype = "long int(long int)";
}

def Expect : Builtin {
  let Spellings = ["__builtin_expect"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "long int(long int, long int)";
}

def ExpectWithProbability : Builtin {
  let Spellings = ["__builtin_expect_with_probability"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "long int(long int, long int, double)";
}

def Prefetch : Builtin {
  let Spellings = ["__builtin_prefetch"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(void const*, ...)";
}

def ReadCycleCounter : Builtin {
  let Spellings = ["__builtin_readcyclecounter"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned long long int()";
}

def ReadSteadyCounter : Builtin {
  let Spellings = ["__builtin_readsteadycounter"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned long long int()";
}

def Trap : Builtin {
  let Spellings = ["__builtin_trap"];
  let Attributes = [NoThrow, NoReturn];
  let Prototype = "void()";
}

def VerboseTrap : Builtin {
  let Spellings = ["__builtin_verbose_trap"];
  let Attributes = [NoThrow, NoReturn];
  let Prototype = "void(char const*, char const*)";
}

def Debugtrap : Builtin {
  let Spellings = ["__builtin_debugtrap"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

def Unreachable : Builtin {
  let Spellings = ["__builtin_unreachable"];
  let Attributes = [NoThrow, NoReturn];
  let Prototype = "void()";
}

def AllowRuntimeCheck : Builtin {
  let Spellings = ["__builtin_allow_runtime_check"];
  let Attributes = [NoThrow, Pure, Const];
  let Prototype = "bool(char const*)";
}

def ShuffleVector : Builtin {
  let Spellings = ["__builtin_shufflevector"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ConvertVector : Builtin {
  let Spellings = ["__builtin_convertvector"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def AllocaUninitialized : Builtin {
  let Spellings = ["__builtin_alloca_uninitialized"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(size_t)";
}

def AllocaWithAlign : Builtin {
  let Spellings = ["__builtin_alloca_with_align"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(size_t, _Constant size_t)";
}

def AllocaWithAlignUninitialized : Builtin {
  let Spellings = ["__builtin_alloca_with_align_uninitialized"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow];
  let Prototype = "void*(size_t, _Constant size_t)";
}

def CallWithStaticChain : Builtin {
  let Spellings = ["__builtin_call_with_static_chain"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(...)";
}

def NondetermenisticValue : Builtin {
  let Spellings = ["__builtin_nondeterministic_value"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseAbs : Builtin {
  let Spellings = ["__builtin_elementwise_abs"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseACos : Builtin {
  let Spellings = ["__builtin_elementwise_acos"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseASin : Builtin {
  let Spellings = ["__builtin_elementwise_asin"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseATan : Builtin {
  let Spellings = ["__builtin_elementwise_atan"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseBitreverse : Builtin {
  let Spellings = ["__builtin_elementwise_bitreverse"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseMax : Builtin {
  let Spellings = ["__builtin_elementwise_max"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseMin : Builtin {
  let Spellings = ["__builtin_elementwise_min"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseCeil : Builtin {
  let Spellings = ["__builtin_elementwise_ceil"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseCos : Builtin {
  let Spellings = ["__builtin_elementwise_cos"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseCosh : Builtin {
  let Spellings = ["__builtin_elementwise_cosh"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseExp : Builtin {
  let Spellings = ["__builtin_elementwise_exp"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseExp2 : Builtin {
  let Spellings = ["__builtin_elementwise_exp2"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseFloor : Builtin {
  let Spellings = ["__builtin_elementwise_floor"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseLog : Builtin {
  let Spellings = ["__builtin_elementwise_log"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseLog2 : Builtin {
  let Spellings = ["__builtin_elementwise_log2"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseLog10 : Builtin {
  let Spellings = ["__builtin_elementwise_log10"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwisePow : Builtin {
  let Spellings = ["__builtin_elementwise_pow"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseRoundEven : Builtin {
  let Spellings = ["__builtin_elementwise_roundeven"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseRound : Builtin {
  let Spellings = ["__builtin_elementwise_round"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseRint : Builtin {
  let Spellings = ["__builtin_elementwise_rint"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseNearbyInt : Builtin {
  let Spellings = ["__builtin_elementwise_nearbyint"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseSin : Builtin {
  let Spellings = ["__builtin_elementwise_sin"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseSinh : Builtin {
  let Spellings = ["__builtin_elementwise_sinh"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseSqrt : Builtin {
  let Spellings = ["__builtin_elementwise_sqrt"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseTan : Builtin {
  let Spellings = ["__builtin_elementwise_tan"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseTanh : Builtin {
  let Spellings = ["__builtin_elementwise_tanh"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseTrunc : Builtin {
  let Spellings = ["__builtin_elementwise_trunc"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseCanonicalize : Builtin {
  let Spellings = ["__builtin_elementwise_canonicalize"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseCopysign : Builtin {
  let Spellings = ["__builtin_elementwise_copysign"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseFma : Builtin {
  let Spellings = ["__builtin_elementwise_fma"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseAddSat : Builtin {
  let Spellings = ["__builtin_elementwise_add_sat"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ElementwiseSubSat : Builtin {
  let Spellings = ["__builtin_elementwise_sub_sat"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceMax : Builtin {
  let Spellings = ["__builtin_reduce_max"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceMin : Builtin {
  let Spellings = ["__builtin_reduce_min"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceXor : Builtin {
  let Spellings = ["__builtin_reduce_xor"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceOr : Builtin {
  let Spellings = ["__builtin_reduce_or"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceAnd : Builtin {
  let Spellings = ["__builtin_reduce_and"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceAdd : Builtin {
  let Spellings = ["__builtin_reduce_add"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def ReduceMul : Builtin {
  let Spellings = ["__builtin_reduce_mul"];
  let Attributes = [NoThrow, Const, CustomTypeChecking];
  let Prototype = "void(...)";
}

def MatrixTranspose : Builtin {
  let Spellings = ["__builtin_matrix_transpose"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix, CustomTypeChecking];
  let Prototype = "void(...)";
}

def MatrixColumnMajorLoad : Builtin {
  let Spellings = ["__builtin_matrix_column_major_load"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix, CustomTypeChecking];
  let Prototype = "void(...)";
}

def MatrixColumnMajorStore : Builtin {
  let Spellings = ["__builtin_matrix_column_major_store"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix, CustomTypeChecking];
  let Prototype = "void(...)";
}

// "Overloaded" Atomic operator builtins.  These are overloaded to support data
// types of i8, i16, i32, i64, and i128.  The front-end sees calls to the
// non-suffixed version of these (which has a bogus type) and transforms them to
// the right overloaded version in Sema (plus casts).

def SyncFetchAndAdd : Builtin {
  let Spellings = ["__sync_fetch_and_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndAddN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_add_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncFetchAndSub : Builtin {
  let Spellings = ["__sync_fetch_and_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndSubN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_sub_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncFetchAndOr : Builtin {
  let Spellings = ["__sync_fetch_and_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndOrN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_or_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncFetchAndAnd : Builtin {
  let Spellings = ["__sync_fetch_and_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndAndN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_and_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncFetchAndXor : Builtin {
  let Spellings = ["__sync_fetch_and_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndXorN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_xor_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncFetchAndNand : Builtin {
  let Spellings = ["__sync_fetch_and_nand"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncFetchAndNandN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_fetch_and_nand_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncAddAndFetch : Builtin {
  let Spellings = ["__sync_add_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncAddAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_add_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncSubAndFetch : Builtin {
  let Spellings = ["__sync_sub_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncSubAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_sub_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncOrAndFetch : Builtin {
  let Spellings = ["__sync_or_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncOrAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_or_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncAndAndFetch : Builtin {
  let Spellings = ["__sync_and_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncAndAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_and_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncXorAndFetch : Builtin {
  let Spellings = ["__sync_xor_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncXorAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_xor_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncNandAndFetch : Builtin {
  let Spellings = ["__sync_nand_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncNandAndFetchN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_nand_and_fetch_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncBoolCompareAndSwap : Builtin {
  let Spellings = ["__sync_bool_compare_and_swap"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncBoolCompareAndSwapN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_bool_compare_and_swap_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "bool(T volatile*, T, T, ...)";
}

def SyncValCompareAndSwap : Builtin {
  let Spellings = ["__sync_val_compare_and_swap"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SynLockValCompareAndSwapN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_val_compare_and_swap_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, T, ...)";
}

def SyncLockTestAndSet : Builtin {
  let Spellings = ["__sync_lock_test_and_set"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SynLockLockTestAndSetN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_lock_test_and_set_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

def SyncLockRelease : Builtin {
  let Spellings = ["__sync_lock_release"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncLockReleaseN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_lock_release_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(T volatile*, ...)";
}

def SyncSwap : Builtin {
  let Spellings = ["__sync_swap"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def SyncSwapN : Builtin, SyncBuiltinsTemplate {
  let Spellings = ["__sync_swap_"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "T(T volatile*, T, ...)";
}

// C11 _Atomic operations for <stdatomic.h>.
def C11AtomicInit : AtomicBuiltin {
  let Spellings = ["__c11_atomic_init"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicLoad : AtomicBuiltin {
  let Spellings = ["__c11_atomic_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicStore : AtomicBuiltin {
  let Spellings = ["__c11_atomic_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicExchange : AtomicBuiltin {
  let Spellings = ["__c11_atomic_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicCompareExchangeStrong : AtomicBuiltin {
  let Spellings = ["__c11_atomic_compare_exchange_strong"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicCompareExchangeWeak : AtomicBuiltin {
  let Spellings = ["__c11_atomic_compare_exchange_weak"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchAdd : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchSub : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchAnd : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchOr : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchXor : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchNand : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_nand"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchMax : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_max"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicFetchMin : AtomicBuiltin {
  let Spellings = ["__c11_atomic_fetch_min"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def C11AtomicThreadFence : Builtin {
  let Spellings = ["__c11_atomic_thread_fence"];
  let Attributes = [NoThrow];
  let Prototype = "void(int)";
}

def C11AtomicSignalFence : Builtin {
  let Spellings = ["__c11_atomic_signal_fence"];
  let Attributes = [NoThrow];
  let Prototype = "void(int)";
}

def C11AtomicIsLockFree : Builtin {
  let Spellings = ["__c11_atomic_is_lock_free"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(size_t)";
}

// GNU atomic builtins.
def AtomicLoad : AtomicBuiltin {
  let Spellings = ["__atomic_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicLoadN : AtomicBuiltin {
  let Spellings = ["__atomic_load_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicStore : AtomicBuiltin {
  let Spellings = ["__atomic_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicStoreN : AtomicBuiltin {
  let Spellings = ["__atomic_store_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicExchange : AtomicBuiltin {
  let Spellings = ["__atomic_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicExchangeN : AtomicBuiltin {
  let Spellings = ["__atomic_exchange_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicCompareExchange : AtomicBuiltin {
  let Spellings = ["__atomic_compare_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicCompareExchangeN : AtomicBuiltin {
  let Spellings = ["__atomic_compare_exchange_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchAdd : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchSub : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchAnd : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchOr : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchXor : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchNand : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_nand"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicAddFetch : AtomicBuiltin {
  let Spellings = ["__atomic_add_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicSubFetch : AtomicBuiltin {
  let Spellings = ["__atomic_sub_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicAndFetch : AtomicBuiltin {
  let Spellings = ["__atomic_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicOrFetch : AtomicBuiltin {
  let Spellings = ["__atomic_or_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicXorFetch : AtomicBuiltin {
  let Spellings = ["__atomic_xor_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicMaxFetch : AtomicBuiltin {
  let Spellings = ["__atomic_max_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicMinFetch : AtomicBuiltin {
  let Spellings = ["__atomic_min_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicNandFetch : AtomicBuiltin {
  let Spellings = ["__atomic_nand_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicTestAndSet : Builtin {
  let Spellings = ["__atomic_test_and_set"];
  let Attributes = [NoThrow];
  let Prototype = "bool(void volatile*, int)";
}

def AtomicClear : Builtin {
  let Spellings = ["__atomic_clear"];
  let Attributes = [NoThrow];
  let Prototype = "void(void volatile*, int)";
}

def AtomicThreadFence : Builtin {
  let Spellings = ["__atomic_thread_fence"];
  let Attributes = [NoThrow];
  let Prototype = "void(int)";
}

def AtomicSignalFence : Builtin {
  let Spellings = ["__atomic_signal_fence"];
  let Attributes = [NoThrow];
  let Prototype = "void(int)";
}

def AtomicAlwaysLockFree : Builtin {
  let Spellings = ["__atomic_always_lock_free"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(size_t, void const volatile*)";
}

def AtomicIsLockFree : Builtin {
  let Spellings = ["__atomic_is_lock_free"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(size_t, void const volatile*)";
}

// GNU atomic builtins with atomic scopes.
def ScopedAtomicLoad : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicLoadN : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_load_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicStore : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicStoreN : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_store_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicExchange : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicExchangeN : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_exchange_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicCompareExchange : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_compare_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicCompareExchangeN : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_compare_exchange_n"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchAdd : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchSub : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchAnd : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchOr : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchXor : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchNand : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_nand"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchMin : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_min"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicFetchMax : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_fetch_max"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicAddFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_add_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicSubFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_sub_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicAndFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_and_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicOrFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_or_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicXorFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_xor_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicNandFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_nand_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicMinFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_min_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def ScopedAtomicMaxFetch : AtomicBuiltin {
  let Spellings = ["__scoped_atomic_max_fetch"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

// OpenCL 2.0 atomic builtins.
def OpenCLAtomicInit : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_init"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicLoad : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicStore : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicCompareExchangeWeak : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_compare_exchange_weak"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicCompareExchangeStrong : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_compare_exchange_strong"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicExchange : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchAdd : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchSub : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchAnd : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchOr : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchXor : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchMin : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_min"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def OpenCLAtomicFetchMax : AtomicBuiltin {
  let Spellings = ["__opencl_atomic_fetch_max"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

// GCC does not support these, they are a Clang extension.
def AtomicFetchMax : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_max"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def AtomicFetchMin : AtomicBuiltin {
  let Spellings = ["__atomic_fetch_min"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

// HIP atomic builtins.
def HipAtomicLoad : AtomicBuiltin {
  let Spellings = ["__hip_atomic_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicStore : AtomicBuiltin {
  let Spellings = ["__hip_atomic_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicCompareExchangeWeak : AtomicBuiltin {
  let Spellings = ["__hip_atomic_compare_exchange_weak"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicCompareExchangeStrong : AtomicBuiltin {
  let Spellings = ["__hip_atomic_compare_exchange_strong"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicExchange : AtomicBuiltin {
  let Spellings = ["__hip_atomic_exchange"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchAdd : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_add"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchSub : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_sub"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchAnd : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_and"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchOr : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_or"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchXor : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_xor"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchMin : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_min"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def HipAtomicFetchMax : AtomicBuiltin {
  let Spellings = ["__hip_atomic_fetch_max"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

// Non-overloaded atomic builtins.
def SyncSynchronize : Builtin {
  let Spellings = ["__sync_synchronize"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

// GCC does not support these, they are a Clang extension.
def SyncFetchAndMin : Builtin {
  let Spellings = ["__sync_fetch_and_min"];
  let Attributes = [NoThrow];
  let Prototype = "int(int volatile*, int)";
}

def SyncFetchAndMax : Builtin {
  let Spellings = ["__sync_fetch_and_max"];
  let Attributes = [NoThrow];
  let Prototype = "int(int volatile*, int)";
}

def SyncFetchAndUMin : Builtin {
  let Spellings = ["__sync_fetch_and_umin"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned int(unsigned int volatile*, unsigned int)";
}

def SyncFetchAndUMax : Builtin {
  let Spellings = ["__sync_fetch_and_umax"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned int(unsigned int volatile*, unsigned int)";
}

// ignored glibc builtin, see https://sourceware.org/bugzilla/show_bug.cgi?id=25399
def WarnMemsetZeroLen : Builtin {
  let Spellings = ["__warn_memset_zero_len"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "void()";
}

// Microsoft builtins. These are only active with -fms-extensions.
def Alloca : MSLangBuiltin {
  let Spellings = ["_alloca"];
  let Attributes = [NoThrow];
  let Prototype = "void*(size_t)";
}

def MSAnnotation : MSLangBuiltin {
  let Spellings = ["__annotation"];
  let Attributes = [NoThrow];
  let Prototype = "wchar_t const*(...)";
}

def MSAssume : MSLangBuiltin {
  let Spellings = ["__assume"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void(bool)";
}

def Bittest : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_bittest"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T const*, T)";
}

def BittestAndComplement : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_bittestandcomplement"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T*, T)";
}

def BittestAndReset : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_bittestandreset"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T*, T)";
}

def BittestAndSet : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_bittestandset"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T*, T)";
}

def MSByteswap : MSLibBuiltin<"stdlib.h">,
    Template<["unsigned short", "msuint32_t", "unsigned long long int"],
             ["_ushort",        "_ulong",     "_uint64"]> {
  let Spellings = ["_byteswap"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
}

def Debugbreak : MSLangBuiltin {
  let Spellings = ["__debugbreak"];
  let Attributes = [NoThrow];
  let Prototype = "void()";
}

def ExceptionCode : MSLangBuiltin {
  let Spellings = ["__exception_code", "_exception_code"];
  let Attributes = [NoThrow];
  let Prototype = "msuint32_t()";
}

def ExceptionInfo : MSLangBuiltin {
  let Spellings = ["__exception_info", "_exception_info"];
  let Attributes = [NoThrow];
  let Prototype = "void*()";
}

def AbnormalTermination : MSLangBuiltin {
  let Spellings = ["__abnormal_termination", "_abnormal_termination"];
  let Attributes = [NoThrow];
  let Prototype = "int()";
}

def GetExceptionInfo : MSLangBuiltin {
  let Spellings = ["__GetExceptionInfo"];
  let Attributes = [NoThrow, CustomTypeChecking, UnevaluatedArguments];
  let Prototype = "void*(...)";
  let Namespace = "std";
}

def InterlockedAnd : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedAnd"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedCompareExchange : MSLangBuiltin, MSInt8_16_32_64Template {
  let Spellings = ["_InterlockedCompareExchange"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T, T)";
}

def InterlockedCompareExchangePointer : MSLangBuiltin {
  let Spellings = ["_InterlockedCompareExchangePointer"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void* volatile*, void*, void*)";
}

def InterlockedCompareExchangePointer_nf : MSLangBuiltin {
  let Spellings = ["_InterlockedCompareExchangePointer_nf"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void* volatile*, void*, void*)";
}

def InterlockedDecrement : MSLangBuiltin, MSInt16_32Template {
  let Spellings = ["_InterlockedDecrement"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*)";
}

def InterlockedExchange : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedExchange"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedExchangeAdd : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedExchangeAdd"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedExchangePointer : MSLangBuiltin {
  let Spellings = ["_InterlockedExchangePointer"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void* volatile*, void*)";
}

def InterlockedExchangeSub : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedExchangeSub"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedIncrement : MSLangBuiltin, MSInt16_32Template {
  let Spellings = ["_InterlockedIncrement"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*)";
}

def InterlockedOr : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedOr"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedXor : MSLangBuiltin, MSInt8_16_32Template {
  let Spellings = ["_InterlockedXor"];
  let Attributes = [NoThrow];
  let Prototype = "T(T volatile*, T)";
}

def InterlockedBittestAndReset : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_interlockedbittestandreset"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T volatile*, T)";
}

def InterlockedBittestAndReset_acq : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandreset_acq"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def InterlockedBittestAndReset_nf : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandreset_nf"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def InterlockedBittestAndReset_rel : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandreset_rel"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def InterlockedBittestAndSet : MSLangBuiltin, MSInt32_64Template {
  let Spellings = ["_interlockedbittestandset"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(T volatile*, T)";
}

def InterlockedBittestAndSet_acq : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandset_acq"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def InterlockedBittestAndSet_nf : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandset_nf"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def InterlockedBittestAndSet_rel : MSLangBuiltin {
  let Spellings = ["_interlockedbittestandset_rel"];
  let Attributes = [NoThrow];
  let Prototype = "unsigned char(msint32_t volatile*, msint32_t)";
}

def IsoVolatileLoad : MSLangBuiltin, Int8_16_32_64Template {
  let Spellings = ["__iso_volatile_load"];
  let Attributes = [NoThrow];
  let Prototype = "T(T const volatile*)";
}

def IsoVolatileStore : MSLangBuiltin, Int8_16_32_64Template {
  let Spellings = ["__iso_volatile_store"];
  let Attributes = [NoThrow];
  let Prototype = "void(T volatile*, T)";
}

def Noop : MSLangBuiltin {
  let Spellings = ["__noop"];
  let Attributes = [NoThrow];
  let Prototype = "int(...)";
}

def MSCountLeadingZeroes : MSLangBuiltin, MSUInt16_32_64Template {
  let Spellings = ["__lzcnt"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T)";
}

def MSPopCount : MSLangBuiltin, MSUInt16_32_64Template {
  let Spellings = ["__popcnt"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "T(T)";
}

def MSReturnAddress : MSLangBuiltin {
  let Spellings = ["_ReturnAddress"];
  let Attributes = [NoThrow];
  let Prototype = "void*()";
}

def Rotl8 : MSLangBuiltin {
  let Spellings = ["_rotl8"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned char(unsigned char, unsigned char)";
}

def Rotl16 : MSLangBuiltin {
  let Spellings = ["_rotl16"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned short(unsigned short, unsigned char)";
}

def Rotl : MSLangBuiltin {
  let Spellings = ["_rotl"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned int(unsigned int, int)";
}

def Lrotl : MSLangBuiltin {
  let Spellings = ["_lrotl"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned long int(unsigned long int, int)";
}

def Rotl64 : MSLangBuiltin {
  let Spellings = ["_rotl64"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "uint64_t(uint64_t, int)";
}

def Rotr8 : MSLangBuiltin {
  let Spellings = ["_rotr8"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned char(unsigned char, unsigned char)";
}

def Rotr16 : MSLangBuiltin {
  let Spellings = ["_rotr16"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned short(unsigned short, unsigned char)";
}

def Rotr : MSLangBuiltin {
  let Spellings = ["_rotr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned int(unsigned int, int)";
}

def Lrotr : MSLangBuiltin {
  let Spellings = ["_lrotr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "unsigned long int(unsigned long int, int)";
}

def Rotr64 : MSLangBuiltin {
  let Spellings = ["_rotr64"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "uint64_t(uint64_t, int)";
}

def MSva_start : MSLangBuiltin {
  let Spellings = ["__va_start"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(char**, ...)";
}

def FastFail : MSLangBuiltin {
  let Spellings = ["__fastfail"];
  let Attributes = [NoThrow, NoReturn];
  let Prototype = "void(unsigned int)";
}

// Microsoft library builtins.
def SetJmpEx : MSLibBuiltin<"setjmpex.h"> {
  let Spellings = ["_setjmpex"];
  let Attributes = [IgnoreSignature, ReturnsTwice];
  let Prototype = "int(jmp_buf)";
}

// C99 library functions
// C99 stdarg.h
def VaStart : LibBuiltin<"stdarg.h"> {
  let Spellings = ["va_start"];
  let Attributes = [NoThrow];
  let Prototype = "void(__builtin_va_list_ref, ...)";
}

def VaEnd : LibBuiltin<"stdarg.h"> {
  let Spellings = ["va_end"];
  let Attributes = [NoThrow];
  let Prototype = "void(__builtin_va_list_ref)";
  let AddBuiltinPrefixedAlias = 1;
}

def VaCopy : LibBuiltin<"stdarg.h"> {
  let Spellings = ["va_copy"];
  let Attributes = [NoThrow];
  let Prototype = "void(__builtin_va_list_ref, __builtin_va_list_ref)";
  let AddBuiltinPrefixedAlias = 1;
}

// C99 stdlib.h
def Abort : LibBuiltin<"stdlib.h"> {
  let Spellings = ["abort"];
  let Attributes = [NoThrow, NoReturn];
  let Prototype = "void()";
  let AddBuiltinPrefixedAlias = 1;
}

def Abs : IntMathTemplate, LibBuiltin<"stdlib.h"> {
  let Spellings = ["abs"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Calloc : LibBuiltin<"stdlib.h"> {
  let Spellings = ["calloc"];
  let Prototype = "void*(size_t, size_t)";
}

def Exit : LibBuiltin<"stdlib.h"> {
  let Spellings = ["exit", "_Exit"];
  let Attributes = [NoReturn];
  let Prototype = "void(int)";
}

def Malloc : LibBuiltin<"stdlib.h"> {
  let Spellings = ["malloc"];
  let Prototype = "void*(size_t)";
}

def Realloc : LibBuiltin<"stdlib.h"> {
  let Spellings = ["realloc"];
  let Prototype = "void*(void*, size_t)";
}

def Free : LibBuiltin<"stdlib.h"> {
  let Spellings = ["free"];
  let Prototype = "void(void*)";
}

def StrToD : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtod"];
  let Prototype = "double(char const*, char**)";
}

def StrToF : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtof"];
  let Prototype = "float(char const*, char**)";
}

def StrToLd : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtold"];
  let Prototype = "long double(char const*, char**)";
}

def StrToL : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtol"];
  let Prototype = "long int(char const*, char**, int)";
}

def StrToLL : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtoll"];
  let Prototype = "long long int(char const*, char**, int)";
}

def StrToUL : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtoul"];
  let Prototype = "unsigned long int(char const*, char**, int)";
}

def StrToULL : LibBuiltin<"stdlib.h"> {
  let Spellings = ["strtoull"];
  let Prototype = "unsigned long long int(char const*, char**, int)";
}

// C11 stdlib.h
def AlignedAlloc : LibBuiltin<"stdlib.h"> {
  let Spellings = ["aligned_alloc"];
  let Prototype = "void*(size_t, size_t)";
}

// C99 string.h
def MemCpy : LibBuiltin<"string.h"> {
  let Spellings = ["memcpy"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void*(void*, void const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def BuiltinMemCmp : Builtin {
  let Spellings = ["__builtin_memcmp"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Constexpr];
  let Prototype = "int(void const*, void const*, size_t)";
}

def MemCmp : LibBuiltin<"string.h"> {
  let Spellings = ["memcmp"];
  let Attributes = [Constexpr];
  let Prototype = "int(void const*, void const*, size_t)";
}

def MemMove : LibBuiltin<"string.h"> {
  let Spellings = ["memmove"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void*(void*, void const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrCpy : LibBuiltin<"string.h"> {
  let Spellings = ["strcpy"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrNCpy : LibBuiltin<"string.h"> {
  let Spellings = ["strncpy"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrCmp : LibBuiltin<"string.h"> {
  let Spellings = ["strcmp"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "int(char const*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrNCmp : LibBuiltin<"string.h"> {
  let Spellings = ["strncmp"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "int(char const*, char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrCat : LibBuiltin<"string.h"> {
  let Spellings = ["strcat"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrNCat : LibBuiltin<"string.h"> {
  let Spellings = ["strncat"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrxFrm : LibBuiltin<"string.h"> {
  let Spellings = ["strxfrm"];
  let Prototype = "size_t(char*, char const*, size_t)";
}

def MemChr : LibBuiltin<"string.h"> {
  let Spellings = ["memchr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void*(void const*, int, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrChr : LibBuiltin<"string.h"> {
  let Spellings = ["strchr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "char*(char const*, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrcSpn : LibBuiltin<"string.h"> {
  let Spellings = ["strcspn"];
  let Prototype = "size_t(char const*, char const*)";
}

def StrpBrk : LibBuiltin<"string.h"> {
  let Spellings = ["strpbrk"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrrChr : LibBuiltin<"string.h"> {
  let Spellings = ["strrchr"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrSpn : LibBuiltin<"string.h"> {
  let Spellings = ["strspn"];
  let Attributes = [NoThrow];
  let Prototype = "size_t(char const*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrStr : LibBuiltin<"string.h"> {
  let Spellings = ["strstr"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrTok : LibBuiltin<"string.h"> {
  let Spellings = ["strtok"];
  let Prototype = "char*(char*, char const*)";
}

def MemSet : LibBuiltin<"string.h"> {
  let Spellings = ["memset"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*, int, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrError : LibBuiltin<"string.h"> {
  let Spellings = ["strerror"];
  let Prototype = "char*(int)";
}

def StrLen : LibBuiltin<"string.h"> {
  let Spellings = ["strlen"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "size_t(char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

// C99 stdio.h
// FIXME: This list is incomplete.
def Printf : LibBuiltin<"stdio.h"> {
  let Spellings = ["printf"];
  let Attributes = [PrintfFormat<0>];
  let Prototype = "int(char const*, ...)";
}

// FIXME: The builtin and library function should have the same signature.
def BuiltinPrintf : Builtin {
  let Spellings = ["__builtin_printf"];
  let Attributes = [NoThrow, PrintfFormat<0>, FunctionWithBuiltinPrefix];
  let Prototype = "int(char const* restrict, ...)";
}

def FPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["fprintf"];
  let Attributes = [NoThrow, PrintfFormat<1>];
  let Prototype = "int(FILE* restrict, char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def SnPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["snprintf"];
  let Attributes = [NoThrow, PrintfFormat<2>];
  let Prototype = "int(char* restrict, size_t, char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def SPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["sprintf"];
  let Attributes = [NoThrow, PrintfFormat<1>];
  let Prototype = "int(char* restrict, char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def VPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vprintf"];
  let Attributes = [NoThrow, VPrintfFormat<0>];
  let Prototype = "int(char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def VfPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vfprintf"];
  let Attributes = [NoThrow, VPrintfFormat<1>];
  let Prototype = "int(FILE* restrict, char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def VsnPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vsnprintf"];
  let Attributes = [NoThrow, VPrintfFormat<2>];
  let Prototype = "int(char* restrict, size_t, char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def VsPrintf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vsprintf"];
  let Attributes = [NoThrow, VPrintfFormat<1>];
  let Prototype = "int(char* restrict, char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def Scanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["scanf"];
  let Attributes = [ScanfFormat<0>];
  let Prototype = "int(char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def FScanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["fscanf"];
  let Attributes = [ScanfFormat<1>];
  let Prototype = "int(FILE* restrict, char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def SScanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["sscanf"];
  let Attributes = [ScanfFormat<1>];
  let Prototype = "int(char const* restrict, char const* restrict, ...)";
  let AddBuiltinPrefixedAlias = 1;
}

def VScanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vscanf"];
  let Attributes = [VScanfFormat<0>];
  let Prototype = "int(char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def VFScanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vfscanf"];
  let Attributes = [VScanfFormat<1>];
  let Prototype = "int(FILE* restrict, char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def VSScanf : LibBuiltin<"stdio.h"> {
  let Spellings = ["vsscanf"];
  let Attributes = [VScanfFormat<1>];
  let Prototype = "int(char const* restrict, char const* restrict, __builtin_va_list)";
  let AddBuiltinPrefixedAlias = 1;
}

def Fopen : LibBuiltin<"stdio.h"> {
  let Spellings = ["fopen"];
  let Prototype = "FILE*(char const*, char const*)";
}

def Fread : LibBuiltin<"stdio.h"> {
  let Spellings = ["fread"];
  let Prototype = "size_t(void*, size_t, size_t, FILE*)";
}

def Fwrite : LibBuiltin<"stdio.h"> {
  let Spellings = ["fwrite"];
  let Prototype = "size_t(void const*, size_t, size_t, FILE*)";
}

// C99 ctype.h

def IsAlNum : LibBuiltin<"ctype.h"> {
  let Spellings = ["isalnum"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsAlpha : LibBuiltin<"ctype.h"> {
  let Spellings = ["isalpha"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsBlank : LibBuiltin<"ctype.h"> {
  let Spellings = ["isblank"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsCntrl : LibBuiltin<"ctype.h"> {
  let Spellings = ["iscntrl"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsDigit : LibBuiltin<"ctype.h"> {
  let Spellings = ["isdigit"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsGraph : LibBuiltin<"ctype.h"> {
  let Spellings = ["isgraph"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsLower : LibBuiltin<"ctype.h"> {
  let Spellings = ["islower"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsPrint : LibBuiltin<"ctype.h"> {
  let Spellings = ["isprint"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsPunct : LibBuiltin<"ctype.h"> {
  let Spellings = ["ispunct"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsSpace : LibBuiltin<"ctype.h"> {
  let Spellings = ["isspace"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsUpper : LibBuiltin<"ctype.h"> {
  let Spellings = ["isupper"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def IsXDigit : LibBuiltin<"ctype.h"> {
  let Spellings = ["isxdigit"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def ToLower : LibBuiltin<"ctype.h"> {
  let Spellings = ["tolower"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

def ToUpper : LibBuiltin<"ctype.h"> {
  let Spellings = ["toupper"];
  let Attributes = [NoThrow, Pure];
  let Prototype = "int(int)";
}

// C99 wchar.h
// FIXME: This list is incomplete. We should cover at least the functions that
// take format strings.

def WcsChr : LibBuiltin<"wchar.h"> {
  let Spellings = ["wcschr"];
  let Attributes = [NoThrow, Pure, Constexpr];
  let Prototype = "wchar_t*(wchar_t const*, wchar_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def WcsCmp : LibBuiltin<"wchar.h"> {
  let Spellings = ["wcscmp"];
  let Attributes = [NoThrow, Pure, Constexpr];
  let Prototype = "int(wchar_t const*, wchar_t const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def WcsLen : LibBuiltin<"wchar.h"> {
  let Spellings = ["wcslen"];
  let Attributes = [NoThrow, Pure, Constexpr];
  let Prototype = "size_t(wchar_t const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def WcsnCmp : LibBuiltin<"wchar.h"> {
  let Spellings = ["wcsncmp"];
  let Attributes = [NoThrow, Pure, Constexpr];
  let Prototype = "int(wchar_t const*, wchar_t const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def WMemChr : LibBuiltin<"wchar.h"> {
  let Spellings = ["wmemchr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "wchar_t*(wchar_t const*, wchar_t, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def WMemCmp : LibBuiltin<"wchar.h"> {
  let Spellings = ["wmemcmp"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "int(wchar_t const*, wchar_t const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def WMemCpy : LibBuiltin<"wchar.h"> {
  let Spellings = ["wmemcpy"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "wchar_t*(wchar_t*, wchar_t const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def WMemMove : LibBuiltin<"wchar.h"> {
  let Spellings = ["wmemmove"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "wchar_t*(wchar_t*, wchar_t const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

// C99

// FIXME: MinGW _setjmp has an additional void* parameter.
def SetJmp : LibBuiltin<"setjmp.h"> {
  let Spellings = ["setjmp", "_setjmp"];
  let Attributes = [ReturnsTwice, IgnoreSignature];
  let Prototype = "int(jmp_buf)";
  let RequiresUndef = 1;
}

def LongJmp : LibBuiltin<"setjmp.h"> {
  let Spellings = ["longjmp"];
  let Attributes = [NoReturn, IgnoreSignature];
  let Prototype = "void(jmp_buf, int)";
}

// Non-C library functions, active in GNU mode only.
// Functions with [[gnu::returns_twice]] attribute are still active in
// all languages, because losing this attribute would result in miscompilation
// when these functions are used in non-GNU mode. PR16138.

def AllocA : GNULibBuiltin<"stdlib.h"> {
  let Spellings = ["alloca"];
  let Attributes = [NoThrow];
  let Prototype = "void*(size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

// POSIX malloc.h

def MemAlign : GNULibBuiltin<"malloc.h"> {
  let Spellings = ["memalign"];
  let Prototype = "void*(size_t, size_t)";
}

// POSIX string.h

def MemcCpy : GNULibBuiltin<"string.h"> {
  let Spellings = ["memccpy"];
  let Prototype = "void*(void*, void const*, int, size_t)";
}

def MempCpy : GNULibBuiltin<"string.h"> {
  let Spellings = ["mempcpy"];
  let Prototype = "void*(void*, void const*, size_t)";
}

def StpCpy : GNULibBuiltin<"string.h"> {
  let Spellings = ["stpcpy"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StpnCpy : GNULibBuiltin<"string.h"> {
  let Spellings = ["stpncpy"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char*, char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrDup : GNULibBuiltin<"string.h"> {
  let Spellings = ["strdup"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*)";
  let AddBuiltinPrefixedAlias = 1;
}

def StrnDup : GNULibBuiltin<"string.h"> {
  let Spellings = ["strndup"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

// POSIX strings.h

def Index : GNULibBuiltin<"strings.h"> {
  let Spellings = ["index"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def Rindex : GNULibBuiltin<"strings.h"> {
  let Spellings = ["rindex"];
  let Attributes = [NoThrow];
  let Prototype = "char*(char const*, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def BZero : GNULibBuiltin<"strings.h"> {
  let Spellings = ["bzero"];
  let Attributes = [NoThrow];
  let Prototype = "void(void*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

def Bcopy : GNULibBuiltin<"strings.h"> {
  let Spellings = ["bcopy"];
  let Attributes = [NoThrow];
  let Prototype = "void(void const*, void*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
}

// FIXME: This should be part of BCmp.
def BuiltinBCmp : Builtin {
  let Spellings = ["__builtin_bcmp"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow, Constexpr];
  let Prototype = "int(void const*, void const*, size_t)";
}

def BCmp : GNULibBuiltin<"strings.h"> {
  let Spellings = ["bcmp"];
  let Attributes = [Constexpr];
  let Prototype = "int(void const*, void const*, size_t)";
}

def StrCaseCmp : GNULibBuiltin<"strings.h"> {
  let Spellings = ["strcasecmp"];
  let Prototype = "int(char const*, char const*)";
  let AddBuiltinPrefixedAlias = 1;
  let RequiresUndef = 1;
}

def StrnCaseCmp : GNULibBuiltin<"strings.h"> {
  let Spellings = ["strncasecmp"];
  let Prototype = "int(char const*, char const*, size_t)";
  let AddBuiltinPrefixedAlias = 1;
  let RequiresUndef = 1;
}

def GNU_Exit : GNULibBuiltin<"unistd.h"> {
  let Spellings = ["_exit"];
  let Attributes = [NoReturn];
  let Prototype = "void(int)";
}

def VFork : LibBuiltin<"unistd.h"> {
  let Spellings = ["vfork"];
  let Attributes = [ReturnsTwice, IgnoreSignature];
  let Prototype = "pid_t()";
}

// POSIX pthread.h
// FIXME: This should be a GNULibBuiltin, but it's currently missing the prototype.

def PthreadCreate : CustomEntry {
  let Entry = "LIBBUILTIN(pthread_create, \"\",  \"fC<2,3>\", PTHREAD_H, ALL_GNU_LANGUAGES)";
}

def SigSetJmp : LibBuiltin<"setjmp.h"> {
  let Spellings = ["sigsetjmp", "__sigsetjmp"];
  let Attributes = [ReturnsTwice, IgnoreSignature];
  let Prototype = "int(sigjmp_buf, int)";
}

def SaveCtx : LibBuiltin<"setjmp.h"> {
  let Spellings = ["savectx"];
  let Attributes = [ReturnsTwice, IgnoreSignature];
  let Prototype = "int(sigjmp_buf)";
}

def GetContext : LibBuiltin<"setjmp.h"> {
  let Spellings = ["getcontext"];
  let Attributes = [ReturnsTwice, IgnoreSignature];
  let Prototype = "int(ucontext_t*)";
}

def GNU_LongJmp : GNULibBuiltin<"setjmp.h"> {
  let Spellings = ["_longjmp"];
  let Attributes = [NoReturn, IgnoreSignature];
  let Prototype = "void(jmp_buf, int)";
}

def SigLongJmp : GNULibBuiltin<"setjmp.h"> {
  let Spellings = ["siglongjmp"];
  let Attributes = [NoReturn, IgnoreSignature];
  let Prototype = "void(sigjmp_buf, int)";
}

// non-standard but very common

def StrlCpy : GNULibBuiltin<"string.h"> {
  let Spellings = ["strlcpy"];
  let Prototype = "size_t(char*, char const*, size_t)";
}

def StrlCat : GNULibBuiltin<"string.h"> {
  let Spellings = ["strlcat"];
  let Prototype = "size_t(char*, char const*, size_t)";
}

def ObjcMsgSend : ObjCLibBuiltin<"objc_message.h"> {
  let Spellings = ["objc_msgSend"];
  let Prototype = "id(id, SEL, ...)";
}

def ObjcMsgSendFpret : ObjCLibBuiltin<"objc_message.h"> {
  let Spellings = ["objc_msgSend_fpret"];
  let Prototype = "long double(id, SEL, ...)";
}

def ObjcMsgSendFp2ret : ObjCLibBuiltin<"objc/message.h"> {
  let Spellings = ["objc_msgSend_fp2ret"];
  let Prototype = "_Complex long double(id, SEL, ...)";
}

def ObjcMsgSendStret : ObjCLibBuiltin<"objc/message.h"> {
  let Spellings = ["objc_msgSend_stret"];
  let Prototype = "void(id, SEL, ...)";
}

def ObjcMsgSendSuper : ObjCLibBuiltin<"objc/message.h"> {
  let Spellings = ["objc_msgSendSuper"];
  let Prototype = "id(objc_super*, SEL, ...)";
}

def ObjcMsgSendSuperStret : ObjCLibBuiltin<"objc/message.h"> {
  let Spellings = ["objc_msgSendSuper_stret"];
  let Prototype = "void(objc_super*, SEL, ...)";
}

def ObjcGetClass : ObjCLibBuiltin<"objc/runtime.h"> {
  let Spellings = ["objc_getClass"];
  let Prototype = "id(char const*)";
}

def ObjcGetMetaClass : ObjCLibBuiltin<"objc/runtime.h"> {
  let Spellings = ["objc_getMetaClass"];
  let Prototype = "id(char const*)";
}

def ObjcEnumerationMutation : ObjCLibBuiltin<"objc/runtime.h"> {
  let Spellings = ["objc_enumerationMutation"];
  let Prototype = "void(id)";
}

def ObjcReadWeak : ObjCLibBuiltin<"objc/objc-auto.h"> {
  let Spellings = ["objc_read_weak"];
  let Prototype = "id(id*)";
}

def ObjcAssignWeak : ObjCLibBuiltin<"objc/objc-auto.h"> {
  let Spellings = ["objc_assign_weak"];
  let Prototype = "id(id, id*)";
}

def ObjcAssignIvar : ObjCLibBuiltin<"objc/objc-auto.h"> {
  let Spellings = ["objc_assign_ivar"];
  let Prototype = "id(id, id, ptrdiff_t)";
}

def ObjcAssignGlobal : ObjCLibBuiltin<"objc/objc-auto.h"> {
  let Spellings = ["objc_assign_global"];
  let Prototype = "id(id, id*)";
}

def ObjcAssignStrongCast : ObjCLibBuiltin<"objc/objc-auto.h"> {
  let Spellings = ["objc_assign_strongCast"];
  let Prototype = "id(id, id*)";
}

def ObjcExceptionExtract : ObjCLibBuiltin<"objc/objc_exception.h"> {
  let Spellings = ["objc_exception_extract"];
  let Prototype = "id(void*)";
}

def ObjcExceptionTryEnter : ObjCLibBuiltin<"objc/objc_exception.h"> {
  let Spellings = ["objc_exception_try_enter"];
  let Prototype = "void(void*)";
}

def ObjcExceptionTryExit : ObjCLibBuiltin<"objc/objc_exception.h"> {
  let Spellings = ["objc_exception_try_exit"];
  let Prototype = "void(void*)";
}

def ObjcExceptionMatch : ObjCLibBuiltin<"objc/objc_exception.h"> {
  let Spellings = ["objc_exception_match"];
  let Prototype = "int(id, id)";
}

def ObjcExceptionThrow : ObjCLibBuiltin<"objc/objc_exception.h"> {
  let Spellings = ["objc_exception_throw"];
  let Prototype = "void(id)";
}

def ObjcSyncEnter : ObjCLibBuiltin<"objc_objc_sync.h"> {
  let Spellings = ["objc_sync_enter"];
  let Prototype = "int(id)";
}

def ObjcSyncExit : ObjCLibBuiltin<"objc_objc_sync.h"> {
  let Spellings = ["objc_sync_exit"];
  let Prototype = "int(id)";
}

def ObjcMemmoveCollectable : Builtin {
  let Spellings = ["__builtin_objc_memmove_collectable"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix];
  let Prototype = "void*(void*, void const*, size_t)";
}

def NSLog : ObjCLibBuiltin<"foundation_nsobjcruntime.h"> {
  let Spellings = ["NSLog"];
  let Attributes = [PrintfFormat<0>];
  let Prototype = "void(id, ...)";
}

def NSLogv : ObjCLibBuiltin<"foundation_nsobjcruntime.h"> {
  let Spellings = ["NSLogv"];
  let Attributes = [VPrintfFormat<0>];
  let Prototype = "void(id, __builtin_va_list)";
}

def Atan2 : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["atan2"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Copysign : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["copysign"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
  let OnlyBuiltinPrefixedAliasIsConstexpr = 1;
}

def Fabs : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fabs"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
  let OnlyBuiltinPrefixedAliasIsConstexpr = 1;
}

def Finite : FPMathTemplate, GNULibBuiltin<"math.h"> {
  let Spellings = ["finite"];
  let Attributes = [NoThrow, Const];
  let Prototype = "int(T)";
  let RequiresUndef = 1;
}

def OSXFinite : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["__finite"];
  let Attributes = [NoThrow, Const];
  let Prototype = "int(T)";
}

def Fmod : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fmod"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Frexp : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["frexp"];
  let Attributes = [NoThrow];
  let Prototype = "T(T, int*)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ldexp : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["ldexp"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def Modf : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["modf"];
  let Attributes = [NoThrow];
  let Prototype = "T(T, T*)";
  let AddBuiltinPrefixedAlias = 1;
}

def Nan : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["nan"];
  let Attributes = [Pure, NoThrow];
  let Prototype = "T(char const*)";
  let AddBuiltinPrefixedAlias = 1;
  let OnlyBuiltinPrefixedAliasIsConstexpr = 1;
}

def Pow : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["pow"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Acos : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["acos"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Acosh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["acosh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Asin : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["asin"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Asinh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["asinh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Atan : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["atan"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Atanh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["atanh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cbrt : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["cbrt"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ceil : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["ceil"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cos : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["cos"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cosh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["cosh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Erf : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["erf"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Erfc : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["erfc"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Exp : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["exp"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Exp2 : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["exp2"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

// FIXME: Why is there no builtin without the prefix?
def Exp10 : FPMathTemplate, Builtin {
  let Spellings = ["__builtin_exp10"];
  let Attributes = [FunctionWithBuiltinPrefix, NoThrow,
                    ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Expm1 : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["expm1"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Fdim : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fdim"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Floor : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["floor"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Fma : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fma"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Fmax : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fmax"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
  let OnlyBuiltinPrefixedAliasIsConstexpr = 1;
}

def Fmin : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["fmin"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
  let OnlyBuiltinPrefixedAliasIsConstexpr = 1;
}

def Hypot : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["hypot"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ilogb : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["ilogb"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "int(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Lgamma : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["lgamma"];
  let Attributes = [NoThrow];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Llrint : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["llrint"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long long int(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Llround : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["llround"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long long int(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Log : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["log"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Log10 : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["log10"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Log1p : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["log1p"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Log2 : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["log2"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Logb : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["logb"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Lrint : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["lrint"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long int(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Lround : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["lround"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "long int(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Nearbyint : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["nearbyint"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Nextafter : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["nextafter"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Nexttoward : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["nexttoward"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, long double)";
  let AddBuiltinPrefixedAlias = 1;
}

def Remainder : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["remainder"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Remquo : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["remquo"];
  let Attributes = [NoThrow];
  let Prototype = "T(T, T, int*)";
  let AddBuiltinPrefixedAlias = 1;
}

def Rint : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["rint"];
  let Attributes = [NoThrow, ConstIgnoringExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Round : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["round"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def RoundEven : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["roundeven"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Scalbln : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["scalbln"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, long int)";
  let AddBuiltinPrefixedAlias = 1;
}

def Scalbn : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["scalbn"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T, int)";
  let AddBuiltinPrefixedAlias = 1;
}

def Sin : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["sin"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Sinh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["sinh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Sqrt : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["sqrt"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Tan : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["tan"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Tanh : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["tanh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Tgamma : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["tgamma"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Trunc : FPMathTemplate, LibBuiltin<"math.h"> {
  let Spellings = ["trunc"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cabs : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cabs"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cacos : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cacos"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cacosh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cacosh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Carg : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["carg"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Casin : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["casin"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Casinh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["casinh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Catan : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["catan"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Catanh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["catanh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ccos : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["ccos"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ccosh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["ccosh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cexp : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cexp"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cimag : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cimag"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Conj : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["conj"];
  let Attributes = [NoThrow, Const];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Clog : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["clog"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cproj : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cproj"];
  let Attributes = [NoThrow, Const];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Cpow : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["cpow"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T, _Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Creal : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["creal"];
  let Attributes = [NoThrow, Const];
  let Prototype = "T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Csin : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["csin"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Csinh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["csinh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Csqrt : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["csqrt"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ctan : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["ctan"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

def Ctanh : FPMathTemplate, LibBuiltin<"complex.h"> {
  let Spellings = ["ctanh"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "_Complex T(_Complex T)";
  let AddBuiltinPrefixedAlias = 1;
}

// __sinpi and friends are OS X specific library functions, but otherwise much
// like the standard (non-complex) sin (etc).
def Sinpi : LibBuiltin<"math.h">, FloatDoubleTemplate {
  let Spellings = ["__sinpi"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Cospi : LibBuiltin<"math.h">, FloatDoubleTemplate {
  let Spellings = ["__cospi"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

def Tanpi : LibBuiltin<"math.h">, FloatDoubleTemplate {
  let Spellings = ["__tanpi"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

// Similarly, __exp10 is OS X only
def OSXExp10 : LibBuiltin<"math.h">, FloatDoubleTemplate {
  let Spellings = ["__exp10"];
  let Attributes = [NoThrow, ConstIgnoringErrnoAndExceptions];
  let Prototype = "T(T)";
}

// Blocks runtime Builtin math library functions.
def BlockObjectAssign : LibBuiltin<"blocks.h"> {
  let Spellings = ["_Block_object_assign"];
  let Prototype = "void(void*, void const*, int const)";
}

def BlockObjectDispose : LibBuiltin<"blocks.h"> {
  let Spellings = ["_Block_object_dispose"];
  let Prototype = "void(void const*, int const)";
}
// FIXME: Also declare NSConcreteGlobalBlock and NSConcreteStackBlock.

def __Addressof : LangBuiltin<"CXX_LANG"> {
  let Spellings = ["__addressof"];
  let Attributes = [FunctionWithoutBuiltinPrefix, NoThrow, Const,
                    IgnoreSignature, Constexpr];
  let Prototype = "void*(void&)";
  let Namespace = "std";
}

def Addressof : CxxLibBuiltin<"memory"> {
  let Spellings = ["addressof"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void*(void&)";
  let Namespace = "std";
}

def AsConst : CxxLibBuiltin<"utility"> {
  let Spellings = ["as_const"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void&(void&)";
  let Namespace = "std";
}

def Forward : CxxLibBuiltin<"utility"> {
  let Spellings = ["forward"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void&(void&)";
  let Namespace = "std";
}

def ForwardLike : CxxLibBuiltin<"utility"> {
  let Spellings = ["forward_like"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void&(void&)";
  let Namespace = "std";
}

def Move : CxxLibBuiltin<"utility"> {
  let Spellings = ["move"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void&(void&)";
  let Namespace = "std";
}

def MoveIfNsoexcept : CxxLibBuiltin<"utility"> {
  let Spellings = ["move_if_noexcept"];
  let Attributes = [NoThrow, Const, IgnoreSignature, RequireDeclaration,
                    Constexpr];
  let Prototype = "void&(void&)";
  let Namespace = "std";
}

def Annotation : Builtin {
  let Spellings = ["__builtin_annotation"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(...)";
}

// Invariants
def Assume : Builtin {
  let Spellings = ["__builtin_assume"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void(bool)";
}

def AssumeSeparateStorage : Builtin {
  let Spellings = ["__builtin_assume_separate_storage"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "void(void const volatile*, void const volatile*)";
}

// Multiprecision Arithmetic Builtins.

class MPATemplate : Template<
    ["unsigned char",     "unsigned short",        "unsigned int",
     "unsigned long int", "unsigned long long int"],
    ["b",                 "s",                     "",
     "l",                 "ll"]>;

def Addc : Builtin, MPATemplate {
  let Spellings = ["__builtin_addc"];
  let Attributes = [NoThrow, Constexpr];
  // FIXME: Why are these argumentes marked const?
  let Prototype = "T(T const, T const, T const, T*)";
}

def Subc : Builtin, MPATemplate {
  let Spellings = ["__builtin_subc"];
  let Attributes = [NoThrow, Constexpr];
  // FIXME: Why are these argumentes marked const?
  let Prototype = "T(T const, T const, T const, T*)";
}

// Checked Arithmetic Builtins for Security.
def AddOverflow : Builtin {
  let Spellings = ["__builtin_add_overflow"];
  let Attributes = [NoThrow, CustomTypeChecking, Constexpr];
  let Prototype = "bool(...)";
}

def SubOverflow : Builtin {
  let Spellings = ["__builtin_sub_overflow"];
  let Attributes = [NoThrow, CustomTypeChecking, Constexpr];
  let Prototype = "bool(...)";
}

def MulOverflow : Builtin {
  let Spellings = ["__builtin_mul_overflow"];
  let Attributes = [NoThrow, CustomTypeChecking, Constexpr];
  let Prototype = "bool(...)";
}

class UOverflowTemplate :
    Template<["unsigned int", "unsigned long int", "unsigned long long int"],
             ["_overflow",    "l_overflow",        "ll_overflow"]>;

def UaddOverflow : Builtin, UOverflowTemplate {
  let Spellings = ["__builtin_uadd"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

def UsubOverflow : Builtin, UOverflowTemplate {
  let Spellings = ["__builtin_usub"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

def UmulOverflow : Builtin, UOverflowTemplate {
  let Spellings = ["__builtin_umul"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

class SOverflowTemplate :
    Template<["int",          "long int",          "long long int"],
             ["_overflow",    "l_overflow",        "ll_overflow"]>;

def SaddOverflow : Builtin, SOverflowTemplate {
  let Spellings = ["__builtin_sadd"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

def SsubOverflow : Builtin, SOverflowTemplate {
  let Spellings = ["__builtin_ssub"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

def SmulOverflow : Builtin, SOverflowTemplate {
  let Spellings = ["__builtin_smul"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "bool(T const, T const, T*)";
}

// Clang builtins (not available in GCC).
def BuiltinAddressof : Builtin {
  let Spellings = ["__builtin_addressof"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void&)";
}

def BuiltinFunctionStart : Builtin {
  let Spellings = ["__builtin_function_start"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void&)";
}

def BuiltinOperatorNew : Builtin {
  let Spellings = ["__builtin_operator_new"];
  let Attributes = [Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(size_t)";
}

def BuiltinOperatorDelete : Builtin {
  let Spellings = ["__builtin_operator_delete"];
  let Attributes = [NoThrow, CustomTypeChecking, Constexpr];
  let Prototype = "void(void*)";
}

def BuiltinCharMemchr : Builtin {
  let Spellings = ["__builtin_char_memchr"];
  let Attributes = [NoThrow, Constexpr];
  let Prototype = "char*(char const*, int, size_t)";
}

def BuiltinDumpStruct : Builtin {
  let Spellings = ["__builtin_dump_struct"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def BuiltinPreserveAccessIndex : Builtin {
  let Spellings = ["__builtin_preserve_access_index"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def IsAligned : Builtin {
  let Spellings = ["__builtin_is_aligned"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "bool(void const*, size_t)";
}

def AlignUp : Builtin {
  let Spellings = ["__builtin_align_up"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void const*, size_t)";
}

def AlignDown : Builtin {
  let Spellings = ["__builtin_align_down"];
  let Attributes = [NoThrow, Const, CustomTypeChecking, Constexpr];
  let Prototype = "void*(void const*, size_t)";
}

// Safestack builtins.
def GetUnsafeStackStart : Builtin {
  let Spellings = ["__builtin___get_unsafe_stack_start"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix];
  let Prototype = "void*()";
}

def GetUnsafeStackBottom : Builtin {
  let Spellings = ["__builtin___get_unsafe_stack_bottom"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix];
  let Prototype = "void*()";
}

def GetUnsafeStackTop : Builtin {
  let Spellings = ["__builtin___get_unsafe_stack_top"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix];
  let Prototype = "void*()";
}

def GetUnsafeStackPtr : Builtin {
  let Spellings = ["__builtin___get_unsafe_stack_ptr"];
  let Attributes = [NoThrow, FunctionWithBuiltinPrefix];
  let Prototype = "void*()";
}

// Nontemporal loads/stores builtins.
def NontemporalStore : Builtin {
  let Spellings = ["__builtin_nontemporal_store"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

def NontemporalLoad : Builtin {
  let Spellings = ["__builtin_nontemporal_load"];
  let Attributes = [CustomTypeChecking];
  let Prototype = "void(...)";
}

// Coroutine intrinsics
def CoroResume : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_resume"];
  let Prototype = "void(void*)";
}

def CoroDestroy : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_destroy"];
  let Prototype = "void(void*)";
}

def CoroDone : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_done"];
  let Attributes = [NoThrow];
  let Prototype = "bool(void*)";
}

def CoroPromise : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_promise"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*, _Constant int, _Constant bool)";
}

def CoroSize : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_size"];
  let Attributes = [NoThrow];
  let Prototype = "size_t()";
}

def CoroAlign : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_align"];
  let Attributes = [NoThrow];
  let Prototype = "size_t()";
}

def CoroFrame : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_frame"];
  let Attributes = [NoThrow];
  let Prototype = "void*()";
}

def CoroNoop : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_noop"];
  let Attributes = [NoThrow];
  let Prototype = "void*()";
}

def CoroFree : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_free"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*)";
}

def CoroId : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_id"];
  let Attributes = [NoThrow];
  let Prototype = "void*(_Constant int, void*, void*, void*)";
}

def CoroAlloc : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_alloc"];
  let Attributes = [NoThrow];
  let Prototype = "bool()";
}

def CoroBegin : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_begin"];
  let Attributes = [NoThrow];
  let Prototype = "void*(void*)";
}

def CoroEnd : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_end"];
  let Attributes = [NoThrow];
  let Prototype = "bool(void*, _Constant bool)";
}

def CoroSuspend : CoroLangBuiltin {
  let Spellings = ["__builtin_coro_suspend"];
  let Attributes = [NoThrow];
  let Prototype = "char(_Constant bool)";
}

// Pointer authentication builtins.
def PtrauthStrip : Builtin {
  let Spellings = ["__builtin_ptrauth_strip"];
  let Attributes = [CustomTypeChecking, NoThrow, Const];
  let Prototype = "void*(void*,int)";
}

def PtrauthBlendDiscriminator : Builtin {
  let Spellings = ["__builtin_ptrauth_blend_discriminator"];
  let Attributes = [CustomTypeChecking, NoThrow, Const];
  let Prototype = "size_t(void*,int)";
}

def PtrauthSignUnauthenticated : Builtin {
  let Spellings = ["__builtin_ptrauth_sign_unauthenticated"];
  let Attributes = [CustomTypeChecking, NoThrow, Const];
  let Prototype = "void*(void*,int,void*)";
}

def PtrauthSignConstant : Builtin {
  let Spellings = ["__builtin_ptrauth_sign_constant"];
  let Attributes = [CustomTypeChecking, NoThrow, Const, Constexpr];
  let Prototype = "void*(void*,int,void*)";
}

def PtrauthSignGenericData : Builtin {
  let Spellings = ["__builtin_ptrauth_sign_generic_data"];
  let Attributes = [CustomTypeChecking, NoThrow, Const];
  let Prototype = "size_t(void*,void*)";
}

def PtrauthAuthAndResign : Builtin {
  let Spellings = ["__builtin_ptrauth_auth_and_resign"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void*(void*,int,void*,int,void*)";
}

def PtrauthAuth : Builtin {
  let Spellings = ["__builtin_ptrauth_auth"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void*(void*,int,void*)";
}

def PtrauthStringDiscriminator : Builtin {
  let Spellings = ["__builtin_ptrauth_string_discriminator"];
  let Attributes = [NoThrow, Const, Constexpr];
  let Prototype = "size_t(char const*)";
}

// OpenCL v2.0 s6.13.16, s9.17.3.5 - Pipe functions.
// We need the generic prototype, since the packet type could be anything.
def ReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def WritePipe : OCLPipeLangBuiltin {
  let Spellings = ["write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def ReserveReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["reserve_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def ReserveWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["reserve_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def CommitWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["commit_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def CommitReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["commit_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def SubGroupReserveReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["sub_group_reserve_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def SubGroupReserveWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["sub_group_reserve_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def SubGroupCommitWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["sub_group_commit_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def SubGroupCommitReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["sub_group_commit_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def WorkGroupReserveReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["work_group_reserve_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def WorkGroupReserveWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["work_group_reserve_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def WorkGroupCommitWritePipe : OCLPipeLangBuiltin {
  let Spellings = ["work_group_commit_write_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def WorkGroupCommitReadPipe : OCLPipeLangBuiltin {
  let Spellings = ["work_group_commit_read_pipe"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void(...)";
}

def GetPipeNumPackets : OCLPipeLangBuiltin {
  let Spellings = ["get_pipe_num_packets"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

def GetPipeMaxPackets : OCLPipeLangBuiltin {
  let Spellings = ["get_pipe_max_packets"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

// OpenCL v2.0 s6.13.17 - Enqueue kernel functions.
// Custom builtin check allows to perform special check of passed block arguments.
def EnqueueKernel : OCL_DSELangBuiltin {
  let Spellings = ["enqueue_kernel"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "int(...)";
}

def GetKernelWorkGroupSize : OCL_DSELangBuiltin {
  let Spellings = ["get_kernel_work_group_size"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

def GetKernelPreferredWorkGroupSizeMultiple : OCL_DSELangBuiltin {
  let Spellings = ["get_kernel_preferred_work_group_size_multiple"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

def GetKernelMaxSubGroupSizeForNDRange : OCL_DSELangBuiltin {
  let Spellings = ["get_kernel_max_sub_group_size_for_ndrange"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

def GetKernelSubGroupCountForNDRange : OCL_DSELangBuiltin {
  let Spellings = ["get_kernel_sub_group_count_for_ndrange"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "unsigned int(...)";
}

// OpenCL v2.0 s6.13.9 - Address space qualifier functions.
// FIXME: Pointer parameters of OpenCL builtins should have their address space
// requirement defined.
def ToGlobal : OCL_GASLangBuiltin {
  let Spellings = ["to_global"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void*(void*)";
}

def ToLocal : OCL_GASLangBuiltin {
  let Spellings = ["to_local"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void*(void*)";
}

def ToPrivate : OCL_GASLangBuiltin {
  let Spellings = ["to_private"];
  let Attributes = [CustomTypeChecking, NoThrow];
  let Prototype = "void*(void*)";
}

// OpenCL half load/store builtin.
def StoreHalf : OCLLangBuiltin, FloatDoubleTemplate {
  let Spellings = ["__builtin_store_half"];
  let Attributes = [NoThrow];
  let Prototype = "void(T, __fp16*)";
}

def LoadHalf : OCLLangBuiltin, FloatDoubleTemplate {
  let Spellings = ["__builtin_load_half"];
  // FIXME: Is this actually Const? This looks like it shoud be Pure.
  let Attributes = [NoThrow, Const];
  let Prototype = "T(__fp16 const*)";
}

// Builtins for os_log/os_trace.
def OSLogFormatBufferSize : Builtin {
  let Spellings = ["__builtin_os_log_format_buffer_size"];
  let Attributes = [PrintfFormat<0>, NoThrow, UnevaluatedArguments,
                    CustomTypeChecking, Constexpr];
  let Prototype = "size_t(char const*, ...)";
}

def OSLogFormat : Builtin {
  let Spellings = ["__builtin_os_log_format"];
  // FIXME: The printf attribute looks suspiciously like it should be argument #1.
  let Attributes = [PrintfFormat<0>, NoThrow, CustomTypeChecking];
  let Prototype = "void*(void*, char const*, ...)";
}

// CUDA/HIP
def GetDeviceSideMangledName : LangBuiltin<"CUDA_LANG"> {
  let Spellings = ["__builtin_get_device_side_mangled_name"];
  let Attributes = [NoThrow, Const, IgnoreSignature];
  let Prototype = "char const*(...)";
}

// HLSL
def HLSLAll : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_all"];
  let Attributes = [NoThrow, Const];
  let Prototype = "bool(...)";
}

def HLSLAny : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_any"];
  let Attributes = [NoThrow, Const];
  let Prototype = "bool(...)";
}

def HLSLWaveActiveCountBits : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_wave_active_count_bits"];
  let Attributes = [NoThrow, Const];
  let Prototype = "unsigned int(bool)";
}

def HLSLWaveGetLaneIndex : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_wave_get_lane_index"];
  let Attributes = [NoThrow, Const];
  let Prototype = "unsigned int()";
}

def HLSLClamp : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_clamp"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLCreateHandle : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_create_handle"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void*(unsigned char)";
}

def HLSLDotProduct : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_dot"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLFrac : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_frac"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLIsinf : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_isinf"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLLerp : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_lerp"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLMad : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_mad"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLRcp : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_rcp"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

def HLSLRSqrt : LangBuiltin<"HLSL_LANG"> {
  let Spellings = ["__builtin_hlsl_elementwise_rsqrt"];
  let Attributes = [NoThrow, Const];
  let Prototype = "void(...)";
}

// Builtins for XRay.
def XRayCustomEvent : Builtin {
  let Spellings = ["__xray_customevent"];
  let Prototype = "void(char const*, size_t)";
}

def XRayTypedEvent : Builtin {
  let Spellings = ["__xray_typedevent"];
  let Prototype = "void(size_t, char const*, size_t)";
}

// Win64-compatible va_list functions.
def MSVaStart : Builtin {
  let Spellings = ["__builtin_ms_va_start"];
  let Attributes = [NoThrow, CustomTypeChecking];
  let Prototype = "void(char*&, ...)";
}

def MSVaEnd : Builtin {
  let Spellings = ["__builtin_ms_va_end"];
  let Attributes = [NoThrow];
  let Prototype = "void(char*&)";
}

def MSVaCopy : Builtin {
  let Spellings = ["__builtin_ms_va_copy"];
  let Attributes = [NoThrow];
  let Prototype = "void(char*&, char*&)";
}

// Arithmetic Fence: to prevent FP reordering and reassociation optimizations
// FIXME: Should this just be a Builtin?
def ArithmeticFence : LangBuiltin<"ALL_LANGUAGES"> {
  let Spellings = ["__arithmetic_fence"];
  let Attributes = [CustomTypeChecking, Constexpr];
  let Prototype = "void(...)";
}
