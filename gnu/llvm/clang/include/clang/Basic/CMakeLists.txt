macro(clang_diag_gen component)
  clang_tablegen(Diagnostic${component}Kinds.inc
    -gen-clang-diags-defs -clang-component=${component}
    SOURCE Diagnostic.td
    TARGET ClangDiagnostic${component})
endmacro(clang_diag_gen)

clang_diag_gen(Analysis)
clang_diag_gen(AST)
clang_diag_gen(Comment)
clang_diag_gen(Common)
clang_diag_gen(CrossTU)
clang_diag_gen(Driver)
clang_diag_gen(Frontend)
clang_diag_gen(InstallAPI)
clang_diag_gen(Lex)
clang_diag_gen(Parse)
clang_diag_gen(Refactoring)
clang_diag_gen(Sema)
clang_diag_gen(Serialization)
clang_tablegen(DiagnosticGroups.inc -gen-clang-diag-groups
  SOURCE Diagnostic.td
  TARGET ClangDiagnosticGroups)

clang_tablegen(DiagnosticIndexName.inc -gen-clang-diags-index-name
  SOURCE Diagnostic.td
  TARGET ClangDiagnosticIndexName)

clang_tablegen(AttrList.inc -gen-clang-attr-list
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE Attr.td
  TARGET ClangAttrList)

clang_tablegen(AttrSubMatchRulesList.inc -gen-clang-attr-subject-match-rule-list
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE Attr.td
  TARGET ClangAttrSubjectMatchRuleList)

clang_tablegen(RegularKeywordAttrInfo.inc -gen-clang-regular-keyword-attr-info
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE Attr.td
  TARGET ClangRegularKeywordAttrInfo
  )

clang_tablegen(AttrHasAttributeImpl.inc -gen-clang-attr-has-attribute-impl
  -I ${CMAKE_CURRENT_SOURCE_DIR}/../../
  SOURCE Attr.td
  TARGET ClangAttrHasAttributeImpl
  )

clang_tablegen(Builtins.inc -gen-clang-builtins
  SOURCE Builtins.td
  TARGET ClangBuiltins)

clang_tablegen(BuiltinsBPF.inc -gen-clang-builtins
  SOURCE BuiltinsBPF.td
  TARGET ClangBuiltinsBPF)

clang_tablegen(BuiltinsRISCV.inc -gen-clang-builtins
  SOURCE BuiltinsRISCV.td
  TARGET ClangBuiltinsRISCV)

# ARM NEON and MVE
clang_tablegen(arm_neon.inc -gen-arm-neon-sema
  SOURCE arm_neon.td
  TARGET ClangARMNeon)
clang_tablegen(arm_fp16.inc -gen-arm-neon-sema
  SOURCE arm_fp16.td
  TARGET ClangARMFP16)
clang_tablegen(arm_mve_builtins.inc -gen-arm-mve-builtin-def
  SOURCE arm_mve.td
  TARGET ClangARMMveBuiltinsDef)
clang_tablegen(arm_mve_builtin_cg.inc -gen-arm-mve-builtin-codegen
  SOURCE arm_mve.td
  TARGET ClangARMMveBuiltinCG)
clang_tablegen(arm_mve_builtin_sema.inc -gen-arm-mve-builtin-sema
  SOURCE arm_mve.td
  TARGET ClangARMMveBuiltinSema)
clang_tablegen(arm_mve_builtin_aliases.inc -gen-arm-mve-builtin-aliases
  SOURCE arm_mve.td
  TARGET ClangARMMveBuiltinAliases)
clang_tablegen(arm_sve_builtins.inc -gen-arm-sve-builtins
  SOURCE arm_sve.td
  TARGET ClangARMSveBuiltins)
clang_tablegen(arm_sve_builtin_cg.inc -gen-arm-sve-builtin-codegen
  SOURCE arm_sve.td
  TARGET ClangARMSveBuiltinCG)
clang_tablegen(arm_sve_typeflags.inc -gen-arm-sve-typeflags
  SOURCE arm_sve.td
  TARGET ClangARMSveTypeFlags)
clang_tablegen(arm_sve_sema_rangechecks.inc -gen-arm-sve-sema-rangechecks
  SOURCE arm_sve.td
  TARGET ClangARMSveSemaRangeChecks)
clang_tablegen(arm_sve_streaming_attrs.inc -gen-arm-sve-streaming-attrs
  SOURCE arm_sve.td
  TARGET ClangARMSveStreamingAttrs)
clang_tablegen(arm_sme_builtins.inc -gen-arm-sme-builtins
  SOURCE arm_sme.td
  TARGET ClangARMSmeBuiltins)
clang_tablegen(arm_sme_builtin_cg.inc -gen-arm-sme-builtin-codegen
  SOURCE arm_sme.td
  TARGET ClangARMSmeBuiltinCG)
clang_tablegen(arm_sme_sema_rangechecks.inc -gen-arm-sme-sema-rangechecks
  SOURCE arm_sme.td
  TARGET ClangARMSmeSemaRangeChecks)
clang_tablegen(arm_sme_streaming_attrs.inc -gen-arm-sme-streaming-attrs
  SOURCE arm_sme.td
  TARGET ClangARMSmeStreamingAttrs)
clang_tablegen(arm_sme_builtins_za_state.inc -gen-arm-sme-builtin-za-state
  SOURCE arm_sme.td
  TARGET ClangARMSmeBuiltinsZAState)
clang_tablegen(arm_cde_builtins.inc -gen-arm-cde-builtin-def
  SOURCE arm_cde.td
  TARGET ClangARMCdeBuiltinsDef)
clang_tablegen(arm_cde_builtin_cg.inc -gen-arm-cde-builtin-codegen
  SOURCE arm_cde.td
  TARGET ClangARMCdeBuiltinCG)
clang_tablegen(arm_cde_builtin_sema.inc -gen-arm-cde-builtin-sema
  SOURCE arm_cde.td
  TARGET ClangARMCdeBuiltinSema)
clang_tablegen(arm_cde_builtin_aliases.inc -gen-arm-cde-builtin-aliases
  SOURCE arm_cde.td
  TARGET ClangARMCdeBuiltinAliases)
clang_tablegen(riscv_vector_builtins.inc -gen-riscv-vector-builtins
  SOURCE riscv_vector.td
  TARGET ClangRISCVVectorBuiltins)
clang_tablegen(riscv_vector_builtin_cg.inc -gen-riscv-vector-builtin-codegen
  SOURCE riscv_vector.td
  TARGET ClangRISCVVectorBuiltinCG)
clang_tablegen(riscv_vector_builtin_sema.inc -gen-riscv-vector-builtin-sema
  SOURCE riscv_vector.td
  TARGET ClangRISCVVectorBuiltinSema)
clang_tablegen(riscv_sifive_vector_builtins.inc -gen-riscv-sifive-vector-builtins
  SOURCE riscv_sifive_vector.td
  TARGET ClangRISCVSiFiveVectorBuiltins)
clang_tablegen(riscv_sifive_vector_builtin_cg.inc -gen-riscv-sifive-vector-builtin-codegen
  SOURCE riscv_sifive_vector.td
  TARGET ClangRISCVSiFiveVectorBuiltinCG)
clang_tablegen(riscv_sifive_vector_builtin_sema.inc -gen-riscv-sifive-vector-builtin-sema
  SOURCE riscv_sifive_vector.td
  TARGET ClangRISCVSiFiveVectorBuiltinSema)
