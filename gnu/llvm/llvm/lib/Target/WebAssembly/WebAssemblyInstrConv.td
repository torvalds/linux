//===-- WebAssemblyInstrConv.td-WebAssembly Conversion support -*- tablegen -*-=
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// WebAssembly datatype conversions, truncations, reinterpretations,
/// promotions, and demotions operand code-gen constructs.
///
//===----------------------------------------------------------------------===//

defm I32_WRAP_I64 : I<(outs I32:$dst), (ins I64:$src), (outs), (ins),
                      [(set I32:$dst, (trunc I64:$src))],
                      "i32.wrap_i64\t$dst, $src", "i32.wrap_i64", 0xa7>;

defm I64_EXTEND_S_I32 : I<(outs I64:$dst), (ins I32:$src), (outs), (ins),
                          [(set I64:$dst, (sext I32:$src))],
                          "i64.extend_i32_s\t$dst, $src", "i64.extend_i32_s",
                          0xac>;
defm I64_EXTEND_U_I32 : I<(outs I64:$dst), (ins I32:$src), (outs), (ins),
                          [(set I64:$dst, (zext I32:$src))],
                          "i64.extend_i32_u\t$dst, $src", "i64.extend_i32_u",
                          0xad>;

let Predicates = [HasSignExt] in {
defm I32_EXTEND8_S_I32 : I<(outs I32:$dst), (ins I32:$src), (outs), (ins),
                           [(set I32:$dst, (sext_inreg I32:$src, i8))],
                           "i32.extend8_s\t$dst, $src", "i32.extend8_s",
                           0xc0>;
defm I32_EXTEND16_S_I32 : I<(outs I32:$dst), (ins I32:$src), (outs), (ins),
                            [(set I32:$dst, (sext_inreg I32:$src, i16))],
                            "i32.extend16_s\t$dst, $src", "i32.extend16_s",
                            0xc1>;
defm I64_EXTEND8_S_I64 : I<(outs I64:$dst), (ins I64:$src), (outs), (ins),
                            [(set I64:$dst, (sext_inreg I64:$src, i8))],
                            "i64.extend8_s\t$dst, $src", "i64.extend8_s",
                            0xc2>;
defm I64_EXTEND16_S_I64 : I<(outs I64:$dst), (ins I64:$src), (outs), (ins),
                            [(set I64:$dst, (sext_inreg I64:$src, i16))],
                            "i64.extend16_s\t$dst, $src", "i64.extend16_s",
                            0xc3>;
defm I64_EXTEND32_S_I64 : I<(outs I64:$dst), (ins I64:$src), (outs), (ins),
                            [(set I64:$dst, (sext_inreg I64:$src, i32))],
                            "i64.extend32_s\t$dst, $src", "i64.extend32_s",
                            0xc4>;
} // Predicates = [HasSignExt]

// Expand a "don't care" extend into zero-extend (chosen over sign-extend
// somewhat arbitrarily, although it favors popular hardware architectures
// and is conceptually a simpler operation).
def : Pat<(i64 (anyext I32:$src)), (I64_EXTEND_U_I32 I32:$src)>;

// Conversion from floating point to integer instructions which don't trap on
// overflow or invalid.
defm I32_TRUNC_S_SAT_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                             [(set I32:$dst, (fp_to_sint F32:$src))],
                             "i32.trunc_sat_f32_s\t$dst, $src",
                             "i32.trunc_sat_f32_s", 0xfc00>,
                             Requires<[HasNontrappingFPToInt]>;
defm I32_TRUNC_U_SAT_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                             [(set I32:$dst, (fp_to_uint F32:$src))],
                             "i32.trunc_sat_f32_u\t$dst, $src",
                             "i32.trunc_sat_f32_u", 0xfc01>,
                             Requires<[HasNontrappingFPToInt]>;
defm I64_TRUNC_S_SAT_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                             [(set I64:$dst, (fp_to_sint F32:$src))],
                             "i64.trunc_sat_f32_s\t$dst, $src",
                             "i64.trunc_sat_f32_s", 0xfc04>,
                             Requires<[HasNontrappingFPToInt]>;
defm I64_TRUNC_U_SAT_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                             [(set I64:$dst, (fp_to_uint F32:$src))],
                             "i64.trunc_sat_f32_u\t$dst, $src",
                             "i64.trunc_sat_f32_u", 0xfc05>,
                             Requires<[HasNontrappingFPToInt]>;
defm I32_TRUNC_S_SAT_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                             [(set I32:$dst, (fp_to_sint F64:$src))],
                             "i32.trunc_sat_f64_s\t$dst, $src",
                             "i32.trunc_sat_f64_s", 0xfc02>,
                             Requires<[HasNontrappingFPToInt]>;
defm I32_TRUNC_U_SAT_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                             [(set I32:$dst, (fp_to_uint F64:$src))],
                             "i32.trunc_sat_f64_u\t$dst, $src",
                             "i32.trunc_sat_f64_u", 0xfc03>,
                             Requires<[HasNontrappingFPToInt]>;
defm I64_TRUNC_S_SAT_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                             [(set I64:$dst, (fp_to_sint F64:$src))],
                             "i64.trunc_sat_f64_s\t$dst, $src",
                             "i64.trunc_sat_f64_s", 0xfc06>,
                             Requires<[HasNontrappingFPToInt]>;
defm I64_TRUNC_U_SAT_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                             [(set I64:$dst, (fp_to_uint F64:$src))],
                             "i64.trunc_sat_f64_u\t$dst, $src",
                             "i64.trunc_sat_f64_u", 0xfc07>,
                             Requires<[HasNontrappingFPToInt]>;

// Support the explicitly saturating operations as well.
def : Pat<(fp_to_sint_sat F32:$src, i32), (I32_TRUNC_S_SAT_F32 F32:$src)>;
def : Pat<(fp_to_uint_sat F32:$src, i32), (I32_TRUNC_U_SAT_F32 F32:$src)>;
def : Pat<(fp_to_sint_sat F64:$src, i32), (I32_TRUNC_S_SAT_F64 F64:$src)>;
def : Pat<(fp_to_uint_sat F64:$src, i32), (I32_TRUNC_U_SAT_F64 F64:$src)>;
def : Pat<(fp_to_sint_sat F32:$src, i64), (I64_TRUNC_S_SAT_F32 F32:$src)>;
def : Pat<(fp_to_uint_sat F32:$src, i64), (I64_TRUNC_U_SAT_F32 F32:$src)>;
def : Pat<(fp_to_sint_sat F64:$src, i64), (I64_TRUNC_S_SAT_F64 F64:$src)>;
def : Pat<(fp_to_uint_sat F64:$src, i64), (I64_TRUNC_U_SAT_F64 F64:$src)>;

// Conversion from floating point to integer pseudo-instructions which don't
// trap on overflow or invalid.
let usesCustomInserter = 1, isCodeGenOnly = 1 in {
defm FP_TO_SINT_I32_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                            [(set I32:$dst, (fp_to_sint F32:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_UINT_I32_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                            [(set I32:$dst, (fp_to_uint F32:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_SINT_I64_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                            [(set I64:$dst, (fp_to_sint F32:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_UINT_I64_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                            [(set I64:$dst, (fp_to_uint F32:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_SINT_I32_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                            [(set I32:$dst, (fp_to_sint F64:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_UINT_I32_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                            [(set I32:$dst, (fp_to_uint F64:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_SINT_I64_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                            [(set I64:$dst, (fp_to_sint F64:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
defm FP_TO_UINT_I64_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                            [(set I64:$dst, (fp_to_uint F64:$src))], "", "", 0>,
                            Requires<[NotHasNontrappingFPToInt]>;
} // usesCustomInserter, isCodeGenOnly = 1

// Conversion from floating point to integer traps on overflow and invalid.
let hasSideEffects = 1 in {
defm I32_TRUNC_S_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                         [], "i32.trunc_f32_s\t$dst, $src", "i32.trunc_f32_s",
                         0xa8>;
defm I32_TRUNC_U_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                         [], "i32.trunc_f32_u\t$dst, $src", "i32.trunc_f32_u",
                         0xa9>;
defm I64_TRUNC_S_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                         [], "i64.trunc_f32_s\t$dst, $src", "i64.trunc_f32_s",
                         0xae>;
defm I64_TRUNC_U_F32 : I<(outs I64:$dst), (ins F32:$src), (outs), (ins),
                         [], "i64.trunc_f32_u\t$dst, $src", "i64.trunc_f32_u",
                         0xaf>;
defm I32_TRUNC_S_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                         [], "i32.trunc_f64_s\t$dst, $src", "i32.trunc_f64_s",
                         0xaa>;
defm I32_TRUNC_U_F64 : I<(outs I32:$dst), (ins F64:$src), (outs), (ins),
                         [], "i32.trunc_f64_u\t$dst, $src", "i32.trunc_f64_u",
                         0xab>;
defm I64_TRUNC_S_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                         [], "i64.trunc_f64_s\t$dst, $src", "i64.trunc_f64_s",
                         0xb0>;
defm I64_TRUNC_U_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                         [], "i64.trunc_f64_u\t$dst, $src", "i64.trunc_f64_u",
                         0xb1>;
} // hasSideEffects = 1

def : Pat<(int_wasm_trunc_signed F32:$src),
          (I32_TRUNC_S_F32 F32:$src)>;
def : Pat<(int_wasm_trunc_unsigned F32:$src),
          (I32_TRUNC_U_F32 F32:$src)>;
def : Pat<(int_wasm_trunc_signed F64:$src),
          (I32_TRUNC_S_F64 F64:$src)>;
def : Pat<(int_wasm_trunc_unsigned F64:$src),
          (I32_TRUNC_U_F64 F64:$src)>;
def : Pat<(int_wasm_trunc_signed F32:$src),
          (I64_TRUNC_S_F32 F32:$src)>;
def : Pat<(int_wasm_trunc_unsigned F32:$src),
          (I64_TRUNC_U_F32 F32:$src)>;
def : Pat<(int_wasm_trunc_signed F64:$src),
          (I64_TRUNC_S_F64 F64:$src)>;
def : Pat<(int_wasm_trunc_unsigned F64:$src),
          (I64_TRUNC_U_F64 F64:$src)>;

defm F32_CONVERT_S_I32 : I<(outs F32:$dst), (ins I32:$src), (outs), (ins),
                           [(set F32:$dst, (sint_to_fp I32:$src))],
                           "f32.convert_i32_s\t$dst, $src", "f32.convert_i32_s",
                           0xb2>;
defm F32_CONVERT_U_I32 : I<(outs F32:$dst), (ins I32:$src), (outs), (ins),
                           [(set F32:$dst, (uint_to_fp I32:$src))],
                           "f32.convert_i32_u\t$dst, $src", "f32.convert_i32_u",
                           0xb3>;
defm F64_CONVERT_S_I32 : I<(outs F64:$dst), (ins I32:$src), (outs), (ins),
                           [(set F64:$dst, (sint_to_fp I32:$src))],
                           "f64.convert_i32_s\t$dst, $src", "f64.convert_i32_s",
                           0xb7>;
defm F64_CONVERT_U_I32 : I<(outs F64:$dst), (ins I32:$src), (outs), (ins),
                           [(set F64:$dst, (uint_to_fp I32:$src))],
                           "f64.convert_i32_u\t$dst, $src", "f64.convert_i32_u",
                           0xb8>;
defm F32_CONVERT_S_I64 : I<(outs F32:$dst), (ins I64:$src), (outs), (ins),
                           [(set F32:$dst, (sint_to_fp I64:$src))],
                           "f32.convert_i64_s\t$dst, $src", "f32.convert_i64_s",
                           0xb4>;
defm F32_CONVERT_U_I64 : I<(outs F32:$dst), (ins I64:$src), (outs), (ins),
                           [(set F32:$dst, (uint_to_fp I64:$src))],
                           "f32.convert_i64_u\t$dst, $src", "f32.convert_i64_u",
                           0xb5>;
defm F64_CONVERT_S_I64 : I<(outs F64:$dst), (ins I64:$src), (outs), (ins),
                           [(set F64:$dst, (sint_to_fp I64:$src))],
                           "f64.convert_i64_s\t$dst, $src", "f64.convert_i64_s",
                           0xb9>;
defm F64_CONVERT_U_I64 : I<(outs F64:$dst), (ins I64:$src), (outs), (ins),
                           [(set F64:$dst, (uint_to_fp I64:$src))],
                           "f64.convert_i64_u\t$dst, $src", "f64.convert_i64_u",
                           0xba>;

defm F64_PROMOTE_F32 : I<(outs F64:$dst), (ins F32:$src), (outs), (ins),
                         [(set F64:$dst, (fpextend F32:$src))],
                         "f64.promote_f32\t$dst, $src", "f64.promote_f32",
                         0xbb>;
defm F32_DEMOTE_F64 : I<(outs F32:$dst), (ins F64:$src), (outs), (ins),
                        [(set F32:$dst, (fpround F64:$src))],
                        "f32.demote_f64\t$dst, $src", "f32.demote_f64",
                        0xb6>;

defm I32_REINTERPRET_F32 : I<(outs I32:$dst), (ins F32:$src), (outs), (ins),
                             [(set I32:$dst, (bitconvert F32:$src))],
                             "i32.reinterpret_f32\t$dst, $src",
                             "i32.reinterpret_f32", 0xbc>;
defm F32_REINTERPRET_I32 : I<(outs F32:$dst), (ins I32:$src), (outs), (ins),
                             [(set F32:$dst, (bitconvert I32:$src))],
                             "f32.reinterpret_i32\t$dst, $src",
                             "f32.reinterpret_i32", 0xbe>;
defm I64_REINTERPRET_F64 : I<(outs I64:$dst), (ins F64:$src), (outs), (ins),
                             [(set I64:$dst, (bitconvert F64:$src))],
                             "i64.reinterpret_f64\t$dst, $src",
                             "i64.reinterpret_f64", 0xbd>;
defm F64_REINTERPRET_I64 : I<(outs F64:$dst), (ins I64:$src), (outs), (ins),
                             [(set F64:$dst, (bitconvert I64:$src))],
                             "f64.reinterpret_i64\t$dst, $src",
                             "f64.reinterpret_i64", 0xbf>;
