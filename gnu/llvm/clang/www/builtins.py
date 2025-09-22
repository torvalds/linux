#!/usr/bin/env python

import sys, fileinput

err = 0

# Giant associative set of builtin->intrinsic mappings where clang doesn't
# implement the builtin since the vector operation works by default.

repl_map = {
    "__builtin_ia32_addps": "_mm_add_ps",
    "__builtin_ia32_addsd": "_mm_add_sd",
    "__builtin_ia32_addpd": "_mm_add_pd",
    "__builtin_ia32_addss": "_mm_add_ss",
    "__builtin_ia32_paddb128": "_mm_add_epi8",
    "__builtin_ia32_paddw128": "_mm_add_epi16",
    "__builtin_ia32_paddd128": "_mm_add_epi32",
    "__builtin_ia32_paddq128": "_mm_add_epi64",
    "__builtin_ia32_subps": "_mm_sub_ps",
    "__builtin_ia32_subsd": "_mm_sub_sd",
    "__builtin_ia32_subpd": "_mm_sub_pd",
    "__builtin_ia32_subss": "_mm_sub_ss",
    "__builtin_ia32_psubb128": "_mm_sub_epi8",
    "__builtin_ia32_psubw128": "_mm_sub_epi16",
    "__builtin_ia32_psubd128": "_mm_sub_epi32",
    "__builtin_ia32_psubq128": "_mm_sub_epi64",
    "__builtin_ia32_mulsd": "_mm_mul_sd",
    "__builtin_ia32_mulpd": "_mm_mul_pd",
    "__builtin_ia32_mulps": "_mm_mul_ps",
    "__builtin_ia32_mulss": "_mm_mul_ss",
    "__builtin_ia32_pmullw128": "_mm_mullo_epi16",
    "__builtin_ia32_divsd": "_mm_div_sd",
    "__builtin_ia32_divpd": "_mm_div_pd",
    "__builtin_ia32_divps": "_mm_div_ps",
    "__builtin_ia32_subss": "_mm_div_ss",
    "__builtin_ia32_andpd": "_mm_and_pd",
    "__builtin_ia32_andps": "_mm_and_ps",
    "__builtin_ia32_pand128": "_mm_and_si128",
    "__builtin_ia32_andnpd": "_mm_andnot_pd",
    "__builtin_ia32_andnps": "_mm_andnot_ps",
    "__builtin_ia32_pandn128": "_mm_andnot_si128",
    "__builtin_ia32_orpd": "_mm_or_pd",
    "__builtin_ia32_orps": "_mm_or_ps",
    "__builtin_ia32_por128": "_mm_or_si128",
    "__builtin_ia32_xorpd": "_mm_xor_pd",
    "__builtin_ia32_xorps": "_mm_xor_ps",
    "__builtin_ia32_pxor128": "_mm_xor_si128",
    "__builtin_ia32_cvtps2dq": "_mm_cvtps_epi32",
    "__builtin_ia32_cvtsd2ss": "_mm_cvtsd_ss",
    "__builtin_ia32_cvtsi2sd": "_mm_cvtsi32_sd",
    "__builtin_ia32_cvtss2sd": "_mm_cvtss_sd",
    "__builtin_ia32_cvttsd2si": "_mm_cvttsd_si32",
    "__builtin_ia32_vec_ext_v2df": "_mm_cvtsd_f64",
    "__builtin_ia32_loadhpd": "_mm_loadh_pd",
    "__builtin_ia32_loadlpd": "_mm_loadl_pd",
    "__builtin_ia32_loadlv4si": "_mm_loadl_epi64",
    "__builtin_ia32_cmpeqps": "_mm_cmpeq_ps",
    "__builtin_ia32_cmpltps": "_mm_cmplt_ps",
    "__builtin_ia32_cmpleps": "_mm_cmple_ps",
    "__builtin_ia32_cmpgtps": "_mm_cmpgt_ps",
    "__builtin_ia32_cmpgeps": "_mm_cmpge_ps",
    "__builtin_ia32_cmpunordps": "_mm_cmpunord_ps",
    "__builtin_ia32_cmpneqps": "_mm_cmpneq_ps",
    "__builtin_ia32_cmpnltps": "_mm_cmpnlt_ps",
    "__builtin_ia32_cmpnleps": "_mm_cmpnle_ps",
    "__builtin_ia32_cmpngtps": "_mm_cmpngt_ps",
    "__builtin_ia32_cmpordps": "_mm_cmpord_ps",
    "__builtin_ia32_cmpeqss": "_mm_cmpeq_ss",
    "__builtin_ia32_cmpltss": "_mm_cmplt_ss",
    "__builtin_ia32_cmpless": "_mm_cmple_ss",
    "__builtin_ia32_cmpunordss": "_mm_cmpunord_ss",
    "__builtin_ia32_cmpneqss": "_mm_cmpneq_ss",
    "__builtin_ia32_cmpnltss": "_mm_cmpnlt_ss",
    "__builtin_ia32_cmpnless": "_mm_cmpnle_ss",
    "__builtin_ia32_cmpngtss": "_mm_cmpngt_ss",
    "__builtin_ia32_cmpngess": "_mm_cmpnge_ss",
    "__builtin_ia32_cmpordss": "_mm_cmpord_ss",
    "__builtin_ia32_movss": "_mm_move_ss",
    "__builtin_ia32_movsd": "_mm_move_sd",
    "__builtin_ia32_movhlps": "_mm_movehl_ps",
    "__builtin_ia32_movlhps": "_mm_movelh_ps",
    "__builtin_ia32_movqv4si": "_mm_move_epi64",
    "__builtin_ia32_unpckhps": "_mm_unpackhi_ps",
    "__builtin_ia32_unpckhpd": "_mm_unpackhi_pd",
    "__builtin_ia32_punpckhbw128": "_mm_unpackhi_epi8",
    "__builtin_ia32_punpckhwd128": "_mm_unpackhi_epi16",
    "__builtin_ia32_punpckhdq128": "_mm_unpackhi_epi32",
    "__builtin_ia32_punpckhqdq128": "_mm_unpackhi_epi64",
    "__builtin_ia32_unpcklps": "_mm_unpacklo_ps",
    "__builtin_ia32_unpcklpd": "_mm_unpacklo_pd",
    "__builtin_ia32_punpcklbw128": "_mm_unpacklo_epi8",
    "__builtin_ia32_punpcklwd128": "_mm_unpacklo_epi16",
    "__builtin_ia32_punpckldq128": "_mm_unpacklo_epi32",
    "__builtin_ia32_punpcklqdq128": "_mm_unpacklo_epi64",
    "__builtin_ia32_cmpeqpd": "_mm_cmpeq_pd",
    "__builtin_ia32_cmpltpd": "_mm_cmplt_pd",
    "__builtin_ia32_cmplepd": "_mm_cmple_pd",
    "__builtin_ia32_cmpgtpd": "_mm_cmpgt_pd",
    "__builtin_ia32_cmpgepd": "_mm_cmpge_pd",
    "__builtin_ia32_cmpunordpd": "_mm_cmpunord_pd",
    "__builtin_ia32_cmpneqpd": "_mm_cmpneq_pd",
    "__builtin_ia32_cmpnltpd": "_mm_cmpnlt_pd",
    "__builtin_ia32_cmpnlepd": "_mm_cmpnle_pd",
    "__builtin_ia32_cmpngtpd": "_mm_cmpngt_pd",
    "__builtin_ia32_cmpngepd": "_mm_cmpnge_pd",
    "__builtin_ia32_cmpordpd": "_mm_cmpord_pd",
    "__builtin_ia32_cmpeqsd": "_mm_cmpeq_sd",
    "__builtin_ia32_cmpltsd": "_mm_cmplt_sd",
    "__builtin_ia32_cmplesd": "_mm_cmple_sd",
    "__builtin_ia32_cmpunordsd": "_mm_cmpunord_sd",
    "__builtin_ia32_cmpneqsd": "_mm_cmpneq_sd",
    "__builtin_ia32_cmpnltsd": "_mm_cmpnlt_sd",
    "__builtin_ia32_cmpnlesd": "_mm_cmpnle_sd",
    "__builtin_ia32_cmpordsd": "_mm_cmpord_sd",
    "__builtin_ia32_cvtsi642ss": "_mm_cvtsi64_ss",
    "__builtin_ia32_cvttss2si64": "_mm_cvtss_si64",
    "__builtin_ia32_shufps": "_mm_shuffle_ps",
    "__builtin_ia32_shufpd": "_mm_shuffle_pd",
    "__builtin_ia32_pshufhw": "_mm_shufflehi_epi16",
    "__builtin_ia32_pshuflw": "_mm_shufflelo_epi16",
    "__builtin_ia32_pshufd": "_mm_shuffle_epi32",
    "__builtin_ia32_movshdup": "_mm_movehdup_ps",
    "__builtin_ia32_movsldup": "_mm_moveldup_ps",
    "__builtin_ia32_maxps": "_mm_max_ps",
    "__builtin_ia32_pslldi128": "_mm_slli_epi32",
    "__builtin_ia32_vec_set_v16qi": "_mm_insert_epi8",
    "__builtin_ia32_vec_set_v8hi": "_mm_insert_epi16",
    "__builtin_ia32_vec_set_v4si": "_mm_insert_epi32",
    "__builtin_ia32_vec_set_v2di": "_mm_insert_epi64",
    "__builtin_ia32_vec_set_v4hi": "_mm_insert_pi16",
    "__builtin_ia32_vec_ext_v16qi": "_mm_extract_epi8",
    "__builtin_ia32_vec_ext_v8hi": "_mm_extract_epi16",
    "__builtin_ia32_vec_ext_v4si": "_mm_extract_epi32",
    "__builtin_ia32_vec_ext_v2di": "_mm_extract_epi64",
    "__builtin_ia32_vec_ext_v4hi": "_mm_extract_pi16",
    "__builtin_ia32_vec_ext_v4sf": "_mm_extract_ps",
}

# Special unhandled cases:
#   __builtin_ia32_vec_ext_*(__P, idx) -> _mm_store_sd/_mm_storeh_pd
#     depending on index. No abstract insert/extract for these oddly.
unhandled = [
    "__builtin_ia32_vec_ext_v2df",
    "__builtin_ia32_vec_ext_v2si",
]


def report_repl(builtin, repl):
    sys.stderr.write(
        "%s:%d: x86 builtin %s used, replaced with %s\n"
        % (fileinput.filename(), fileinput.filelineno(), builtin, repl)
    )


def report_cant(builtin):
    sys.stderr.write(
        "%s:%d: x86 builtin %s used, too many replacements\n"
        % (fileinput.filename(), fileinput.filelineno(), builtin)
    )


for line in fileinput.input(inplace=1):
    for builtin, repl in repl_map.items():
        if builtin in line:
            line = line.replace(builtin, repl)
            report_repl(builtin, repl)
    for unh in unhandled:
        if unh in line:
            report_cant(unh)
    sys.stdout.write(line)

sys.exit(err)
