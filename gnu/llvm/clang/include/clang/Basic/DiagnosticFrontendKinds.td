//==--- DiagnosticFrontendKinds.td - frontend diagnostics -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class BackendInfo : CatBackend, ShowInSystemHeader;

let Component = "Frontend" in {

def err_fe_error_opening : Error<"error opening '%0': %1">;
def err_fe_error_reading : Error<"error reading '%0': %1">;
def err_fe_error_reading_stdin : Error<"error reading stdin: %0">;
def err_fe_error_backend : Error<"error in backend: %0">, DefaultFatal;

def err_fe_inline_asm : Error<"%0">, CatInlineAsm;
def warn_fe_inline_asm : Warning<"%0">, CatInlineAsm, InGroup<BackendInlineAsm>;
def note_fe_inline_asm : Note<"%0">, CatInlineAsm;
def note_fe_inline_asm_here : Note<"instantiated into assembly here">;
def err_fe_source_mgr : Error<"%0">, CatSourceMgr;
def warn_fe_source_mgr : Warning<"%0">, CatSourceMgr, InGroup<BackendSourceMgr>;
def note_fe_source_mgr : Note<"%0">, CatSourceMgr;
def err_fe_linking_module : Error<"cannot link module '%0': %1">, DefaultFatal;
def warn_fe_linking_module : Warning<"linking module '%0': %1">, InGroup<LinkerWarnings>;
def note_fe_linking_module : Note<"linking module '%0': %1">;

def warn_fe_frame_larger_than : Warning<"stack frame size (%0) exceeds limit (%1) in '%2'">,
    BackendInfo, InGroup<BackendFrameLargerThan>;
def warn_fe_backend_frame_larger_than: Warning<"%0">,
    BackendInfo, InGroup<BackendFrameLargerThan>;
def err_fe_backend_frame_larger_than: Error<"%0">, BackendInfo;
def note_fe_backend_frame_larger_than: Note<"%0">, BackendInfo;

def warn_fe_backend_plugin: Warning<"%0">, BackendInfo, InGroup<BackendPlugin>;
def err_fe_backend_plugin: Error<"%0">, BackendInfo;

def warn_fe_backend_resource_limit: Warning<"%0 (%1) exceeds limit (%2) in '%3'">, BackendInfo, InGroup<BackendPlugin>;
def err_fe_backend_resource_limit: Error<"%0 (%1) exceeds limit (%2) in '%3'">, BackendInfo;
def note_fe_backend_resource_limit: Note<"%0 (%1) exceeds limit (%2) in '%3'">, BackendInfo;

def remark_fe_backend_plugin: Remark<"%0">, BackendInfo, InGroup<RemarkBackendPlugin>;
def note_fe_backend_plugin: Note<"%0">, BackendInfo;

def warn_fe_override_module : Warning<
    "overriding the module target triple with %0">,
    InGroup<DiagGroup<"override-module">>;
def warn_fe_backend_unsupported_fp_rounding : Warning<
    "overriding currently unsupported rounding mode on this target">,
    InGroup<UnsupportedFPOpt>;
def warn_fe_backend_unsupported_fp_exceptions : Warning<
    "overriding currently unsupported use of floating point exceptions "
    "on this target">, InGroup<UnsupportedFPOpt>;
def warn_fe_backend_invalid_feature_flag : Warning<
    "feature flag '%0' must start with either '+' to enable the feature or '-'"
    " to disable it; flag ignored">, InGroup<InvalidCommandLineArgument>;
def warn_fe_backend_readonly_feature_flag : Warning<
    "feature flag '%0' is ignored since the feature is read only">,
    InGroup<InvalidCommandLineArgument>;

def err_incompatible_fp_eval_method_options : Error<
    "option 'ffp-eval-method' cannot be used with option "
    "%select{'fapprox-func'|'mreassociate'|'freciprocal'}0">;

def remark_fe_backend_optimization_remark : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemark>;
def remark_fe_backend_optimization_remark_missed : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemarkMissed>;
def remark_fe_backend_optimization_remark_analysis : Remark<"%0">, BackendInfo,
    InGroup<BackendOptimizationRemarkAnalysis>;
def remark_fe_backend_optimization_remark_analysis_fpcommute : Remark<"%0; "
    "allow reordering by specifying '#pragma clang loop vectorize(enable)' "
    "before the loop or by providing the compiler option '-ffast-math'">,
    BackendInfo, InGroup<BackendOptimizationRemarkAnalysis>;
def remark_fe_backend_optimization_remark_analysis_aliasing : Remark<"%0; "
    "allow reordering by specifying '#pragma clang loop vectorize(enable)' "
    "before the loop; if the arrays will always be independent, specify "
    "'#pragma clang loop vectorize(assume_safety)' before the loop or provide "
    "the '__restrict__' qualifier with the independent array arguments -- "
    "erroneous results will occur if these options are incorrectly applied">,
    BackendInfo, InGroup<BackendOptimizationRemarkAnalysis>;

def warn_fe_backend_optimization_failure : Warning<"%0">, BackendInfo,
    InGroup<BackendOptimizationFailure>, DefaultWarn;
def note_fe_backend_invalid_loc : Note<"could "
  "not determine the original source location for %0:%1:%2">, BackendInfo;

def err_fe_backend_unsupported : Error<"%0">, BackendInfo;
def warn_fe_backend_unsupported : Warning<"%0">, BackendInfo;

def err_fe_backend_error_attr :
  Error<"call to '%0' declared with 'error' attribute: %1">, BackendInfo;
def warn_fe_backend_warning_attr :
  Warning<"call to '%0' declared with 'warning' attribute: %1">, BackendInfo,
  InGroup<BackendWarningAttributes>;
def warn_toc_unsupported_type : Warning<"-mtocdata option is ignored "
  "for %0 because %1">, InGroup<BackendWarningAttributes>;

def err_fe_invalid_code_complete_file : Error<
    "cannot locate code-completion file %0">, DefaultFatal;
def err_fe_dependency_file_requires_MT : Error<
    "-dependency-file requires at least one -MT or -MQ option">;
def err_fe_invalid_plugin_name : Error<
    "unable to find plugin '%0'">;
def err_fe_expected_compiler_job : Error<
    "unable to handle compilation, expected exactly one compiler job in '%0'">;
def err_fe_expected_clang_command : Error<
    "expected a clang compiler command">;
def err_fe_remap_missing_to_file : Error<
    "could not remap file '%0' to the contents of file '%1'">, DefaultFatal;
def err_fe_remap_missing_from_file : Error<
    "could not remap from missing file '%0'">, DefaultFatal;
def err_fe_unable_to_load_pch : Error<
    "unable to load PCH file">;
def err_fe_unable_to_load_plugin : Error<
    "unable to load plugin '%0': '%1'">;
def err_fe_unable_to_create_target : Error<
    "unable to create target: '%0'">;
def err_fe_unable_to_interface_with_target : Error<
    "unable to interface with target machine">;
def err_fe_unable_to_open_output : Error<
    "unable to open output file '%0': '%1'">;
def warn_fe_macro_contains_embedded_newline : Warning<
    "macro '%0' contains embedded newline; text after the newline is ignored">;
def warn_fe_cc_print_header_failure : Warning<
    "unable to open CC_PRINT_HEADERS file: %0 (using stderr)">;
def warn_fe_cc_log_diagnostics_failure : Warning<
    "unable to open CC_LOG_DIAGNOSTICS file: %0 (using stderr)">;
def warn_fe_unable_to_open_stats_file : Warning<
    "unable to open statistics output file '%0': '%1'">,
    InGroup<DiagGroup<"unable-to-open-stats-file">>;
def err_fe_no_pch_in_dir : Error<
    "no suitable precompiled header file found in directory '%0'">;
def err_fe_action_not_available : Error<
    "action %0 not compiled in">;
def err_fe_invalid_multiple_actions : Error<
    "'%0' action ignored; '%1' action specified previously">;
def err_fe_invalid_alignment : Error<
    "invalid value '%1' in '%0'; alignment must be a power of 2">;
def err_fe_invalid_exception_model
   : Error<"invalid exception model '%select{none|sjlj|seh|dwarf|wasm}0' for target '%1'">;
def err_fe_invalid_source_date_epoch : Error<
    "environment variable 'SOURCE_DATE_EPOCH' ('%0') must be a non-negative decimal integer <= %1">;

def err_fe_unable_to_load_basic_block_sections_file : Error<
    "unable to load basic block sections function list: '%0'">;

def warn_fe_serialized_diag_merge_failure : Warning<
    "unable to merge a subprocess's serialized diagnostics">,
    InGroup<SerializedDiagnostics>;
def warn_fe_serialized_diag_failure : Warning<
    "unable to open file %0 for serializing diagnostics (%1)">,
    InGroup<SerializedDiagnostics>;
def warn_fe_serialized_diag_failure_during_finalization : Warning<
    "received warning after diagnostic serialization teardown was underway: %0">,
    InGroup<SerializedDiagnostics>;

def err_verify_missing_line : Error<
    "missing or invalid line number following '@' in expected %0">;
def err_verify_missing_file : Error<
    "file '%0' could not be located in expected %1">;
def err_verify_invalid_range : Error<
    "invalid range following '-' in expected %0">;
def err_verify_ambiguous_marker : Error<
    "reference to marker '%0' is ambiguous">;
def note_verify_ambiguous_marker : Note<
    "ambiguous marker '%0' is defined here">;
def err_verify_no_such_marker : Error<
    "use of undefined marker '%0'">;
def err_verify_missing_start : Error<
    "cannot find start ('{{') of expected %0">;
def err_verify_missing_end : Error<
    "cannot find end ('%1') of expected %0">;
def err_verify_invalid_content : Error<
    "invalid expected %0: %1">;
def err_verify_missing_regex : Error<
    "cannot find start of regex ('{{') in %0">;
def err_verify_inconsistent_diags : Error<
    "'%0' diagnostics %select{expected|seen}1 but not %select{seen|expected}1: "
    "%2">;
def err_verify_invalid_no_diags : Error<
    "%select{expected|'%0-no-diagnostics'}1 directive cannot follow "
    "%select{'%0-no-diagnostics' directive|other expected directives}1">;
def err_verify_no_directives : Error<
    "no expected directives found: consider use of '%0-no-diagnostics'">;
def err_verify_nonconst_addrspace : Error<
  "qualifier 'const' is needed for variables in address space '%0'">;

def note_fixit_applied : Note<"FIX-IT applied suggested code changes">;
def note_fixit_in_macro : Note<
    "FIX-IT unable to apply suggested code changes in a macro">;
def note_fixit_failed : Note<
    "FIX-IT unable to apply suggested code changes">;
def note_fixit_unfixed_error : Note<"FIX-IT detected an error it cannot fix">;
def warn_fixit_no_changes : Note<
    "FIX-IT detected errors it could not fix; no output will be generated">;

// PCH reader
def err_relocatable_without_isysroot : Error<
    "must specify system root with -isysroot when building a relocatable "
    "PCH file">;

def warn_unknown_diag_option : Warning<
    "unknown %select{warning|remark}0 option '%1'%select{|; did you mean '%3'?}2">,
    InGroup<UnknownWarningOption>;
def warn_unknown_warning_specifier : Warning<
    "unknown %0 warning specifier: '%1'">,
    InGroup<UnknownWarningOption>;

def warn_incompatible_analyzer_plugin_api : Warning<
    "checker plugin '%0' is not compatible with this version of the analyzer">,
    InGroup<DiagGroup<"analyzer-incompatible-plugin"> >;
def note_incompatible_analyzer_plugin_api : Note<
    "current API version is '%0', but plugin was compiled with version '%1'">;

def err_module_build_requires_fmodules : Error<
  "module compilation requires '-fmodules'">;
def err_module_interface_requires_cpp_modules : Error<
  "module interface compilation requires '-std=c++20'">;
def warn_module_config_mismatch : Warning<
  "module file %0 cannot be loaded due to a configuration mismatch with the current "
  "compilation">, InGroup<DiagGroup<"module-file-config-mismatch">>, DefaultError;
def err_module_map_not_found : Error<"module map file '%0' not found">,
  DefaultFatal;
def err_missing_module_name : Error<
  "no module name provided; specify one with -fmodule-name=">,
  DefaultFatal;
def err_file_is_not_module : Error<"file '%0' is not a module file">, DefaultFatal;
def err_missing_module : Error<
  "no module named '%0' declared in module map file '%1'">, DefaultFatal;
def err_no_submodule : Error<"no submodule named %0 in module '%1'">;
def err_no_submodule_suggest : Error<
  "no submodule named %0 in module '%1'; did you mean '%2'?">;
def warn_no_priv_submodule_use_toplevel : Warning<
  "no submodule named %0 in module '%1'; using top level '%2'">,
  InGroup<PrivateModule>;
def note_private_top_level_defined : Note<
  "module defined here">;
def warn_missing_submodule : Warning<"missing submodule '%0'">,
  InGroup<IncompleteUmbrella>;
def note_module_import_here : Note<"module imported here">;
def err_module_cannot_create_includes : Error<
  "cannot create includes file for module %0: %1">;
def warn_module_config_macro_undef : Warning<
  "%select{definition|#undef}0 of configuration macro '%1' has no effect on "
  "the import of '%2'; pass '%select{-D%1=...|-U%1}0' on the command line "
  "to configure the module">,
  InGroup<ConfigMacros>;
def note_module_def_undef_here : Note<
  "macro was %select{defined|#undef'd}0 here">;
def remark_module_build : Remark<"building module '%0' as '%1'">,
  ShowInSystemHeader,
  InGroup<ModuleBuild>;
def remark_module_build_done : Remark<"finished building module '%0'">,
  ShowInSystemHeader,
  InGroup<ModuleBuild>;
def remark_module_lock : Remark<"locking '%0' to build module '%1'">,
  InGroup<ModuleLock>;
def err_modules_embed_file_not_found :
  Error<"file '%0' specified by '-fmodules-embed-file=' not found">,
  DefaultFatal;
def err_module_header_file_not_found :
  Error<"module header file '%0' not found">, DefaultFatal;

def err_test_module_file_extension_version : Error<
  "test module file extension '%0' has different version (%1.%2) than expected "
  "(%3.%4)">;

def warn_eagerly_load_for_standard_cplusplus_modules : Warning<
  "the form '-fmodule-file=<BMI-path>' is deprecated for standard C++ named modules;"
  "consider to use '-fmodule-file=<module-name>=<BMI-path>' instead">,
  InGroup<DiagGroup<"eager-load-cxx-named-modules">>;

def err_missing_vfs_overlay_file : Error<
  "virtual filesystem overlay file '%0' not found">, DefaultFatal;
def err_invalid_vfs_overlay : Error<
  "invalid virtual filesystem overlay file '%0'">, DefaultFatal;

def warn_option_invalid_ocl_version : Warning<
  "%0 does not support the option '%1'">, InGroup<Deprecated>;

def err_builtin_needs_feature : Error<"%0 needs target feature %1">;
def err_function_needs_feature : Error<
  "always_inline function %1 requires target feature '%2', but would "
  "be inlined into function %0 that is compiled without support for '%2'">;

let CategoryName = "Codegen ABI Check" in {
def err_function_always_inline_attribute_mismatch : Error<
  "always_inline function %1 and its caller %0 have mismatching %2 attributes">;
def warn_function_always_inline_attribute_mismatch : Warning<
  "always_inline function %1 and its caller %0 have mismatching %2 attributes, "
  "inlining may change runtime behaviour">, InGroup<AArch64SMEAttributes>;
def err_function_always_inline_new_za : Error<
  "always_inline function %0 has new za state">;

def warn_avx_calling_convention
    : Warning<"AVX vector %select{return|argument}0 of type %1 without '%2' "
              "enabled changes the ABI">,
      InGroup<DiagGroup<"psabi">>;
def err_avx_calling_convention : Error<warn_avx_calling_convention.Summary>;

def err_target_unsupported_type_for_abi
    : Error<"%0 requires %1 type support, but ABI '%2' does not support it">;
}

def err_alias_to_undefined : Error<
  "%select{alias|ifunc}0 must point to a defined "
  "%select{variable or |}1function">;
def err_alias_to_common : Error<
  "alias to a variable in a common section is not allowed">;
def note_alias_requires_mangled_name : Note<
  "the %select{function or variable|function}0 specified in an %select{alias|ifunc}1 must refer to its mangled name">;
def note_alias_mangled_name_alternative: Note<
  "function by that name is mangled as \"%0\"">;
def warn_alias_to_weak_alias : Warning<
  "%select{alias|ifunc}2 will always resolve to %0 even if weak definition of "
  "%1 is overridden">,
  InGroup<IgnoredAttributes>;
def err_duplicate_mangled_name : Error<
  "definition with same mangled name '%0' as another definition">;
def err_cyclic_alias : Error<
  "%select{alias|ifunc}0 definition is part of a cycle">;
def err_hidden_visibility_dllexport : Error<
  "hidden visibility cannot be applied to 'dllexport' declaration">;
def err_non_default_visibility_dllimport : Error<
  "non-default visibility cannot be applied to 'dllimport' declaration">;
def err_ifunc_resolver_return : Error<
  "ifunc resolver function must return a pointer">;

def warn_atomic_op_misaligned : Warning<
  "misaligned atomic operation may incur "
  "significant performance penalty"
  "; the expected alignment (%0 bytes) exceeds the actual alignment (%1 bytes)">,
  InGroup<AtomicAlignment>;

def warn_atomic_op_oversized : Warning<
  "large atomic operation may incur "
  "significant performance penalty"
  "; the access size (%0 bytes) exceeds the max lock-free size (%1  bytes)">,
InGroup<AtomicAlignment>;

def warn_sync_op_misaligned : Warning<
  "__sync builtin operation must have natural alignment (consider using __atomic)">,
  InGroup<SyncAlignment>;

def warn_alias_with_section : Warning<
  "%select{alias|ifunc}1 will not be in section '%0' but in the same section "
  "as the %select{aliasee|resolver}2">,
  InGroup<IgnoredAttributes>;

let CategoryName = "Instrumentation Issue" in {
def warn_profile_data_out_of_date : Warning<
  "profile data may be out of date: of %0 function%s0, %1 %plural{1:has|:have}1"
  " mismatched data that will be ignored">,
  InGroup<ProfileInstrOutOfDate>;
def warn_profile_data_missing : Warning<
  "profile data may be incomplete: of %0 function%s0, %1 %plural{1:has|:have}1"
  " no data">,
  InGroup<ProfileInstrMissing>,
  DefaultIgnore;
def warn_profile_data_unprofiled : Warning<
  "no profile data available for file \"%0\"">,
  InGroup<ProfileInstrUnprofiled>;
def warn_profile_data_misexpect : Warning<
  "potential performance regression from use of __builtin_expect(): "
  "annotation was correct on %0 of profiled executions">,
  BackendInfo, InGroup<MisExpect>;
} // end of instrumentation issue category

def err_extract_api_ignores_file_not_found :
  Error<"file '%0' specified by '--extract-api-ignores=' not found">, DefaultFatal;

def warn_missing_symbol_graph_dir : Warning<
  "missing symbol graph output directory, defaulting to working directory">,
  InGroup<ExtractAPIMisuse>;

def err_ast_action_on_llvm_ir : Error<
  "cannot apply AST actions to LLVM IR file '%0'">,
  DefaultFatal;
}
