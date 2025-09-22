//==--- DiagnosticCommonKinds.td - common diagnostics ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Common Helpers
//===----------------------------------------------------------------------===//

let Component = "Common" in {

// Substitutions.

def select_constexpr_spec_kind : TextSubstitution<
  "%select{<ERROR>|constexpr|consteval|constinit}0">;

// Basic.

def fatal_too_many_errors
  : Error<"too many errors emitted, stopping now">, DefaultFatal;

def warn_stack_exhausted : Warning<
  "stack nearly exhausted; compilation time may suffer, and "
  "crashes due to stack overflow are likely">,
  InGroup<DiagGroup<"stack-exhausted">>, NoSFINAE;

def note_declared_at : Note<"declared here">;
def note_previous_definition : Note<"previous definition is here">;
def note_previous_declaration : Note<"previous declaration is here">;
def note_previous_implicit_declaration : Note<
  "previous implicit declaration is here">;
def note_previous_use : Note<"previous use is here">;
def note_duplicate_case_prev : Note<"previous case defined here">;
def note_forward_declaration : Note<"forward declaration of %0">;
def note_type_being_defined : Note<
  "definition of %0 is not complete until the closing '}'">;
/// note_matching - this is used as a continuation of a previous diagnostic,
/// e.g. to specify the '(' when we expected a ')'.
def note_matching : Note<"to match this %0">;

def note_using : Note<"using">;
def note_possibility : Note<"one possibility">;
def note_also_found : Note<"also found">;

def warn_next_larger_fp_type_same_size_than_fp : Warning<
  "higher precision floating-point type size has the same size than "
  "floating-point type size">,
  InGroup<DiagGroup<"higher-precision-fp">>;

// Parse && Lex

let CategoryName = "Lexical or Preprocessor Issue" in {

def err_expected_colon_after_setter_name : Error<
  "method name referenced in property setter attribute "
  "must end with ':'">;
def err_expected_string_literal : Error<"expected string literal "
  "%select{in %1|for diagnostic message in static_assert|"
          "for optional message in 'availability' attribute|"
          "for %select{language name|source container name|USR}1 in "
          "'external_source_symbol' attribute|"
          "as argument of '%1' attribute}0">;

def err_invalid_string_udl : Error<
  "string literal with user-defined suffix cannot be used here">;
def err_invalid_character_udl : Error<
  "character literal with user-defined suffix cannot be used here">;
def err_invalid_numeric_udl : Error<
  "numeric literal with user-defined suffix cannot be used here">;
def warn_pragma_debug_missing_argument : Warning<
  "missing argument to debug command '%0'">, InGroup<IgnoredPragmas>;
def warn_pragma_debug_unexpected_argument : Warning<
  "unexpected argument to debug command">, InGroup<IgnoredPragmas>;

def warn_fp_nan_inf_when_disabled : Warning<
  "use of %select{infinity|NaN}0%select{| via a macro}1 is undefined behavior "
  "due to the currently enabled floating-point options">,
  InGroup<DiagGroup<"nan-infinity-disabled", [], NanInfDisabledDocs>>;
}

// Parse && Sema

let CategoryName = "Parse Issue" in {

def err_expected : Error<"expected %0">;
def err_expected_either : Error<"expected %0 or %1">;
def err_expected_after : Error<"expected %1 after %0">;

def err_param_redefinition : Error<"redefinition of parameter %0">;
def warn_method_param_redefinition : Warning<"redefinition of method parameter %0">;
def warn_method_param_declaration : Warning<"redeclaration of method parameter %0">,
  InGroup<DuplicateArgDecl>, DefaultIgnore;
def err_invalid_storage_class_in_func_decl : Error<
  "invalid storage class specifier in function declarator">;
def err_expected_namespace_name : Error<"expected namespace name">;
def ext_variadic_templates : ExtWarn<
  "variadic templates are a C++11 extension">, InGroup<CXX11>;
def warn_cxx98_compat_variadic_templates :
  Warning<"variadic templates are incompatible with C++98">,
  InGroup<CXX98Compat>, DefaultIgnore;
def err_default_special_members : Error<
  "only special member functions %select{|and comparison operators }0"
  "may be defaulted">;
def err_deleted_non_function : Error<
  "only functions can have deleted definitions">;
def err_module_not_found : Error<"module '%0' not found">, DefaultFatal;
def err_module_not_built : Error<"could not build module '%0'">, DefaultFatal;
def err_module_build_disabled: Error<
  "module '%0' is needed but has not been provided, and implicit use of module "
  "files is disabled">, DefaultFatal;
def err_module_unavailable : Error<
  "module '%0' %select{is incompatible with|requires}1 feature '%2'">;
def err_module_header_missing : Error<
  "%select{|umbrella }0header '%1' not found">;
def remark_module_lock_failure : Remark<
  "could not acquire lock file for module '%0': %1">, InGroup<ModuleBuild>;
def remark_module_lock_timeout : Remark<
  "timed out waiting to acquire lock file for module '%0'">, InGroup<ModuleBuild>;
def err_module_shadowed : Error<"import of shadowed module '%0'">, DefaultFatal;
def err_module_build_shadowed_submodule : Error<
  "build a shadowed submodule '%0'">, DefaultFatal;
def err_module_cycle : Error<"cyclic dependency in module '%0': %1">,
  DefaultFatal;
def err_module_prebuilt : Error<
  "error in loading module '%0' from prebuilt module path">, DefaultFatal;
def err_module_rebuild_finalized : Error<
  "cannot rebuild module '%0' as it is already finalized">, DefaultFatal;
def note_pragma_entered_here : Note<"#pragma entered here">;
def note_decl_hiding_tag_type : Note<
  "%1 %0 is hidden by a non-type declaration of %0 here">;
def err_attribute_not_type_attr : Error<
  "%0%select{ attribute|}1 cannot be applied to types">;
def err_enum_template : Error<"enumeration cannot be a template">;

def warn_cxx20_compat_consteval : Warning<
  "'consteval' specifier is incompatible with C++ standards before C++20">,
  InGroup<CXX20Compat>, DefaultIgnore;
def warn_missing_type_specifier : Warning<
  "type specifier missing, defaults to 'int'">,
  InGroup<ImplicitInt>, DefaultIgnore;

def ext_c_empty_initializer : Extension<
  "use of an empty initializer is a C23 extension">, InGroup<C23>;
def warn_c23_compat_empty_initializer : Warning<
  "use of an empty initializer is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
}

let CategoryName = "Nullability Issue" in {

def warn_nullability_duplicate : Warning<
  "duplicate nullability specifier %0">,
  InGroup<Nullability>;

def warn_conflicting_nullability_attr_overriding_ret_types : Warning<
  "conflicting nullability specifier on return types, %0 "
  "conflicts with existing specifier %1">,
  InGroup<Nullability>;

def warn_conflicting_nullability_attr_overriding_param_types : Warning<
  "conflicting nullability specifier on parameter types, %0 "
  "conflicts with existing specifier %1">,
  InGroup<Nullability>;

def err_nullability_conflicting : Error<
  "nullability specifier %0 conflicts with existing specifier %1">;

def warn_incompatible_branch_protection_option: Warning <
  "'-mbranch-protection=' option is incompatible with the '%0' architecture">,
  InGroup<BranchProtection>;

def warn_target_unsupported_branch_protection_attribute: Warning <
  "ignoring the 'branch-protection' attribute because the '%0' architecture does not support it">,
  InGroup<BranchProtection>;
}

// OpenCL Section 6.8.g
def err_opencl_unknown_type_specifier : Error<
  "%0 does not support the '%1' "
  "%select{type qualifier|storage class specifier}2">;

def warn_unknown_attribute_ignored : Warning<
  "unknown attribute %0 ignored">, InGroup<UnknownAttributes>;
def warn_attribute_ignored : Warning<"%0 attribute ignored">,
  InGroup<IgnoredAttributes>;
def err_keyword_not_supported_on_target : Error<
  "%0 is not supported on this target">;
def err_use_of_tag_name_without_tag : Error<
  "must use '%1' tag to refer to type %0%select{| in this scope}2">;

def duplicate_declspec : TextSubstitution<
  "duplicate '%0' declaration specifier">;

def ext_duplicate_declspec : Extension<"%sub{duplicate_declspec}0">,
  InGroup<DuplicateDeclSpecifier>;
def ext_warn_duplicate_declspec : ExtWarn<"%sub{duplicate_declspec}0">,
  InGroup<DuplicateDeclSpecifier>;
def warn_duplicate_declspec : Warning<"%sub{duplicate_declspec}0">,
  InGroup<DuplicateDeclSpecifier>;

def err_duplicate_declspec : Error<"%sub{duplicate_declspec}0">;

def err_friend_decl_spec : Error<"'%0' is invalid in friend declarations">;

def err_invalid_member_in_interface : Error<
  "%select{data member |non-public member function |static member function |"
          "user-declared constructor|user-declared destructor|operator |"
          "nested class }0%1 is not permitted within an interface type">;

def err_attribute_uuid_malformed_guid : Error<
  "uuid attribute contains a malformed GUID">;

// Sema && Lex
def ext_c99_longlong : Extension<
  "'long long' is an extension when C99 mode is not enabled">,
  InGroup<LongLong>;
def ext_cxx11_longlong : Extension<
  "'long long' is a C++11 extension">,
  InGroup<CXX11LongLong>;
def warn_cxx98_compat_longlong : Warning<
  "'long long' is incompatible with C++98">,
  InGroup<CXX98CompatPedantic>, DefaultIgnore;
def ext_cxx23_size_t_suffix : ExtWarn<
  "'size_t' suffix for literals is a C++23 extension">,
  InGroup<CXX23>;
def warn_cxx20_compat_size_t_suffix : Warning<
  "'size_t' suffix for literals is incompatible with C++ standards before "
  "C++23">, InGroup<CXXPre23Compat>, DefaultIgnore;
def err_cxx23_size_t_suffix: Error<
  "'size_t' suffix for literals is a C++23 feature">;
def err_size_t_literal_too_large: Error<
  "%select{signed |}0'size_t' literal is out of range of possible "
  "%select{signed |}0'size_t' values">;
def ext_cxx_bitint_suffix : Extension<
  "'_BitInt' suffix for literals is a Clang extension">,
  InGroup<BitIntExtension>;
def ext_c23_bitint_suffix : ExtWarn<
  "'_BitInt' suffix for literals is a C23 extension">,
  InGroup<C23>;
def warn_c23_compat_bitint_suffix : Warning<
  "'_BitInt' suffix for literals is incompatible with C standards before C23">,
  InGroup<CPre23Compat>, DefaultIgnore;
def err_integer_literal_too_large : Error<
  "integer literal is too large to be represented in any %select{signed |}0"
  "integer type">;
def ext_integer_literal_too_large_for_signed : ExtWarn<
  "integer literal is too large to be represented in a signed integer type, "
  "interpreting as unsigned">,
  InGroup<ImplicitlyUnsignedLiteral>;
def warn_old_implicitly_unsigned_long : Warning<
  "integer literal is too large to be represented in type 'long', "
  "interpreting as 'unsigned long' per C89; this literal will "
  "%select{have type 'long long'|be ill-formed}0 in C99 onwards">,
  InGroup<C99Compat>;
def warn_old_implicitly_unsigned_long_cxx : Warning<
  "integer literal is too large to be represented in type 'long', "
  "interpreting as 'unsigned long' per C++98; this literal will "
  "%select{have type 'long long'|be ill-formed}0 in C++11 onwards">,
  InGroup<CXX11Compat>;
def ext_old_implicitly_unsigned_long_cxx : ExtWarn<
  "integer literal is too large to be represented in type 'long' and is "
  "subject to undefined behavior under C++98, interpreting as 'unsigned long'; "
  "this literal will %select{have type 'long long'|be ill-formed}0 "
  "in C++11 onwards">,
  InGroup<CXX11Compat>;
def ext_clang_enable_if : Extension<"'enable_if' is a clang extension">,
                          InGroup<GccCompat>;
def ext_clang_diagnose_if : Extension<"'diagnose_if' is a clang extension">,
                            InGroup<GccCompat>;
def err_too_large_for_fixed_point : Error<
  "this value is too large for this fixed point type">;
def err_unimplemented_conversion_with_fixed_point_type : Error<
  "conversion between fixed point and %0 is not yet supported">;

def err_requires_positive_value : Error<
  "%select{invalid value '%0'; must be positive|value '%0' is too large}1">;

// SEH
def err_seh_expected_handler : Error<
  "expected '__except' or '__finally' block">;
def err_seh___except_block : Error<
  "%0 only allowed in __except block or filter expression">;
def err_seh___except_filter : Error<
  "%0 only allowed in __except filter expression">;
def err_seh___finally_block : Error<
  "%0 only allowed in __finally block">;

// Sema && AST
def note_invalid_subexpr_in_const_expr : Note<
  "subexpression not valid in a constant expression">;
def note_constexpr_invalid_template_arg : Note<
  "%select{pointer|reference}0 to %select{|subobject of }1"
  "%select{type_info object|string literal|temporary object|"
  "predefined '%3' variable}2 is not allowed in a template argument">;
def err_constexpr_invalid_template_arg : Error<
  note_constexpr_invalid_template_arg.Summary>;

// Sema && Frontend
let CategoryName = "Inline Assembly Issue" in {
def err_asm_invalid_type_in_input : Error<
  "invalid type %0 in asm input for constraint '%1'">;

def err_asm_invalid_type : Error<
  "invalid type %0 in asm %select{input|output}1">;

def err_ms_asm_bitfield_unsupported : Error<
  "an inline asm block cannot have an operand which is a bit-field">;

def warn_stack_clash_protection_inline_asm : Warning<
  "unable to protect inline asm that clobbers stack pointer against stack "
  "clash">, InGroup<DiagGroup<"stack-protector">>;

def warn_slh_does_not_support_asm_goto : Warning<
  "speculative load hardening does not protect functions with asm goto">,
  InGroup<DiagGroup<"slh-asm-goto">>;
}

// Sema && Serialization
def warn_dup_category_def : Warning<
  "duplicate definition of category %1 on interface %0">,
  InGroup<DiagGroup<"objc-duplicate-category-definition">>;

// Targets

def err_target_unknown_triple : Error<
  "unknown target triple '%0'">;
def err_target_unknown_cpu : Error<"unknown target CPU '%0'">;
def note_valid_options : Note<"valid target CPU values are: %0">;
def err_target_unsupported_cpu_for_micromips : Error<
  "micromips is not supported for target CPU '%0'">;
def err_target_unknown_abi : Error<"unknown target ABI '%0'">;
def err_target_unsupported_abi : Error<"ABI '%0' is not supported on CPU '%1'">;
def err_target_unsupported_abi_for_triple : Error<
  "ABI '%0' is not supported for '%1'">;
def err_unsupported_abi_for_opt : Error<"'%0' can only be used with the '%1' ABI">;
def err_mips_fp64_req : Error<
    "'%0' can only be used if the target supports the mfhc1 and mthc1 instructions">;
def err_target_unknown_fpmath : Error<"unknown FP unit '%0'">;
def err_target_unsupported_fpmath : Error<
    "the '%0' unit is not supported with this instruction set">;
def err_target_unsupported_unaligned : Error<
  "the %0 sub-architecture does not support unaligned accesses">;
def err_target_unsupported_execute_only : Error<
  "execute only is not supported for the %0 sub-architecture">;
def err_target_unsupported_tp_hard : Error<
  "hardware TLS register is not supported for the %0 sub-architecture">;
def err_target_unsupported_mcmse : Error<
  "-mcmse is not supported for %0">;
def err_opt_not_valid_with_opt : Error<
  "option '%0' cannot be specified with '%1'">;
def err_opt_not_valid_with_opt_on_target : Error<
  "option '%0' cannot be specified with '%1' for the %2 sub-architecture">;
def err_opt_not_valid_without_opt : Error<
  "option '%0' cannot be specified without '%1'">;
def err_opt_not_valid_on_target : Error<
  "option '%0' cannot be specified on this target">;
def err_invalid_feature_combination : Error<
  "invalid feature combination: %0">;
def warn_invalid_feature_combination : Warning<
  "invalid feature combination: %0">, InGroup<DiagGroup<"invalid-feature-combination">>;
def warn_target_unrecognized_env : Warning<
  "mismatch between architecture and environment in target triple '%0'; did you mean '%1'?">,
  InGroup<InvalidCommandLineArgument>;
def err_target_unsupported_abi_with_fpu : Error<
  "'%0' ABI is not supported with FPU">;

def err_ppc_impossible_musttail: Error<
  "'musttail' attribute for this call is impossible because %select{"
  "long calls can not be tail called on PPC|"
  "indirect calls can not be tail called on PPC|"
  "external calls can not be tail called on PPC}0"
  >;
def err_aix_musttail_unsupported: Error<
  "'musttail' attribute is not supported on AIX">;

// Source manager
def err_cannot_open_file : Error<"cannot open file '%0': %1">, DefaultFatal;
def err_file_modified : Error<
  "file '%0' modified since it was first processed">, DefaultFatal;
def err_file_too_large : Error<
  "file '%0' is too large for Clang to process">;
def err_sloc_space_too_large : Error<
  "translation unit is too large for Clang to process: ran out of source locations">, DefaultFatal;
def err_unsupported_bom : Error<"%0 byte order mark detected in '%1', but "
  "encoding is not supported">, DefaultFatal;
def err_unable_to_rename_temp : Error<
  "unable to rename temporary '%0' to output file '%1': '%2'">;
def err_unable_to_make_temp : Error<
  "unable to make temporary file: %0">;
def remark_sloc_usage : Remark<
  "source manager location address space usage:">,
  InGroup<DiagGroup<"sloc-usage">>, DefaultRemark, ShowInSystemHeader;
def note_total_sloc_usage : Note<
  "%0B in local locations, %1B in locations loaded from AST files, for a total "
  "of %2B (%3%% of available space)">;
def note_file_sloc_usage : Note<
  "file entered %0 time%s0 using %1B of space"
  "%plural{0:|: plus %2B for macro expansions}2">;
def note_file_misc_sloc_usage : Note<
  "%0 additional files entered using a total of %1B of space">;

// Modules
def err_module_format_unhandled : Error<
  "no handler registered for module format '%0'">, DefaultFatal;

// TransformActions
// TODO: Use a custom category name to distinguish rewriter errors.
def err_mt_message : Error<"[rewriter] %0">, SuppressInSystemHeader;
def warn_mt_message : Warning<"[rewriter] %0">;
def note_mt_message : Note<"[rewriter] %0">;

// ARCMigrate
def warn_arcmt_nsalloc_realloc : Warning<"[rewriter] call returns pointer to GC managed memory; it will become unmanaged in ARC">;
def err_arcmt_nsinvocation_ownership : Error<"NSInvocation's %0 is not safe to be used with an object with ownership other than __unsafe_unretained">;

// API notes
def err_apinotes_message : Error<"%0">;
def warn_apinotes_message : Warning<"%0">, InGroup<DiagGroup<"apinotes">>;
def note_apinotes_message : Note<"%0">;

class NonportablePrivateAPINotesPath  : Warning<
  "private API notes file for module '%0' should be named "
  "'%0_private.apinotes', not '%1'">;
def warn_apinotes_private_case : NonportablePrivateAPINotesPath,
  InGroup<DiagGroup<"nonportable-private-apinotes-path">>;
def warn_apinotes_private_case_system : NonportablePrivateAPINotesPath,
  DefaultIgnore, InGroup<DiagGroup<"nonportable-private-system-apinotes-path">>;

// C++ for OpenCL.
def err_openclcxx_not_supported : Error<
  "'%0' is not supported in C++ for OpenCL">;

// HIP
def warn_ignored_hip_only_option : Warning<
  "'%0' is ignored since it is only supported for HIP">,
  InGroup<HIPOnly>;

// OpenMP
def err_omp_more_one_clause : Error<
  "directive '#pragma omp %0' cannot contain more than one '%1' clause%select{| with '%3' name modifier| with 'source' dependence}2">;
def err_omp_required_clause : Error<
  "directive '#pragma omp %0' requires the '%1' clause">;

// Static Analyzer Core
def err_unknown_analyzer_checker_or_package : Error<
    "no analyzer checkers or packages are associated with '%0'">;
def note_suggest_disabling_all_checkers : Note<
    "use -analyzer-disable-all-checks to disable all static analyzer checkers">;

// Poison system directories.
def warn_poison_system_directories : Warning <
  "include location '%0' is unsafe for cross-compilation">,
  InGroup<DiagGroup<"poison-system-directories">>, DefaultIgnore;

def warn_opencl_unsupported_core_feature : Warning<
  "%0 is a core feature in %select{OpenCL C|C++ for OpenCL}1 version %2 but not supported on this target">,
  InGroup<OpenCLCoreFeaturesDiagGroup>, DefaultIgnore;

def err_opencl_extension_and_feature_differs : Error<
  "options %0 and %1 are set to different values">;
def err_opencl_feature_requires : Error<
  "feature %0 requires support of %1 feature">;

def warn_throw_not_valid_on_target : Warning<
  "target '%0' does not support exception handling;"
  " 'throw' is assumed to be never reached">,
  InGroup<OpenMPTargetException>;
def warn_try_not_valid_on_target : Warning<
  "target '%0' does not support exception handling;"
  " 'catch' block is ignored">,
  InGroup<OpenMPTargetException>;
}
