//==--- DiagnosticDriverKinds.td - libdriver diagnostics ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let Component = "Driver" in {

def err_drv_no_such_file : Error<"no such file or directory: '%0'">;
def err_drv_no_such_file_with_suggestion : Error<
  "no such file or directory: '%0'; did you mean '%1'?">;
def err_drv_unsupported_opt : Error<"unsupported option '%0'">;
def err_drv_unsupported_opt_with_suggestion : Error<
  "unsupported option '%0'; did you mean '%1'?">;
def err_drv_unsupported_opt_for_target : Error<
  "unsupported option '%0' for target '%1'">;
def err_drv_unsupported_opt_for_language_mode : Error<
  "unsupported option '%0' for language mode '%1'">;
def err_drv_unsupported_option_argument : Error<
  "unsupported argument '%1' to option '%0'">;
def err_drv_unsupported_option_argument_for_target : Error<
  "unsupported argument '%1' to option '%0' for target '%2'">;
def err_drv_unknown_stdin_type : Error<
  "-E or -x required when input is from standard input">;
def err_drv_unknown_stdin_type_clang_cl : Error<
  "use /Tc or /Tp to set input type for standard input">;
def err_drv_unknown_language : Error<"language not recognized: '%0'">;
def err_drv_invalid_arch_name : Error<
  "invalid arch name '%0'">;
def err_drv_invalid_riscv_arch_name : Error<
  "invalid arch name '%0', %1">;
def err_drv_invalid_riscv_cpu_name_for_target : Error<
  "cpu '%0' does not support rv%select{32|64}1">;
def warn_drv_invalid_arch_name_with_suggestion : Warning<
  "ignoring invalid /arch: argument '%0'; for %select{64|32}1-bit expected one of %2">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_avr_mcu_not_specified : Warning<
  "no target microcontroller specified on command line, cannot "
  "link standard libraries, please pass -mmcu=<mcu name>">,
  InGroup<AVRRtlibLinkingQuirks>;
def warn_drv_avr_libc_not_found: Warning<
  "no avr-libc installation can be found on the system, "
  "cannot link standard libraries">,
  InGroup<AVRRtlibLinkingQuirks>;
def warn_drv_avr_family_linking_stdlibs_not_implemented: Warning<
  "support for linking stdlibs for microcontroller '%0' is not implemented">,
  InGroup<AVRRtlibLinkingQuirks>;
def warn_drv_avr_linker_section_addresses_not_implemented: Warning<
  "support for passing the data section address to the linker for "
  "microcontroller '%0' is not implemented">,
  InGroup<AVRRtlibLinkingQuirks>;
def warn_drv_avr_stdlib_not_linked: Warning<
  "standard library not linked and so no interrupt vector table or "
  "compiler runtime routines will be linked">,
  InGroup<AVRRtlibLinkingQuirks>;
def err_drv_cuda_bad_gpu_arch : Error<"unsupported CUDA gpu architecture: %0">;
def err_drv_offload_bad_gpu_arch : Error<"unsupported %0 gpu architecture: %1">;
def err_drv_offload_missing_gpu_arch : Error<
  "must pass in an explicit %0 gpu architecture to '%1'">;
def err_drv_no_cuda_installation : Error<
  "cannot find CUDA installation; provide its path via '--cuda-path', or pass "
  "'-nocudainc' to build without CUDA includes">;
def err_drv_no_cuda_libdevice : Error<
  "cannot find libdevice for %0; provide path to different CUDA installation "
  "via '--cuda-path', or pass '-nocudalib' to build without linking with "
  "libdevice">;

def err_drv_no_rocm_device_lib : Error<
  "cannot find ROCm device library%select{| for %1|for ABI version %1}0; provide its path via "
  "'--rocm-path' or '--rocm-device-lib-path', or pass '-nogpulib' to build "
  "without ROCm device library">;
def err_drv_no_hip_runtime : Error<
  "cannot find HIP runtime; provide its path via '--rocm-path', or pass "
  "'-nogpuinc' to build without HIP runtime">;
def err_drv_no_hipstdpar_lib : Error<
  "cannot find HIP Standard Parallelism Acceleration library; provide it via "
  "'--hipstdpar-path'">;
def err_drv_no_hipstdpar_thrust_lib : Error<
  "cannot find rocThrust, which is required by the HIP Standard Parallelism "
  "Acceleration library; provide it via "
  "'--hipstdpar-thrust-path'">;
def err_drv_no_hipstdpar_prim_lib : Error<
  "cannot find rocPrim, which is required by the HIP Standard Parallelism "
  "Acceleration library; provide it via '--hipstdpar-prim-path'">;

def err_drv_no_hipspv_device_lib : Error<
  "cannot find HIP device library%select{| for %1}0; provide its path via "
  "'--hip-path' or '--hip-device-lib-path', or pass '-nogpulib' to build "
  "without HIP device library">;
def err_drv_hipspv_no_hip_path : Error<
  "'--hip-path' must be specified when offloading to SPIR-V unless '-nogpuinc' "
  "is given">;

// TODO: Remove when COV6 is fully supported by ROCm.
def warn_drv_amdgpu_cov6: Warning<
  "code object v6 is still in development and not ready for production use yet;"
  " use at your own risk">;
def err_drv_undetermined_gpu_arch : Error<
  "cannot determine %0 architecture: %1; consider passing it via "
  "'%2'">;
def warn_drv_multi_gpu_arch : Warning<
  "multiple %0 architectures are detected: %1; only the first one is used for "
  "'%2'">, InGroup<MultiGPU>;
def err_drv_cuda_version_unsupported : Error<
  "GPU arch %0 is supported by CUDA versions between %1 and %2 (inclusive), "
  "but installation at %3 is %4; use '--cuda-path' to specify a different CUDA "
  "install, pass a different GPU arch with '--cuda-gpu-arch', or pass "
  "'--no-cuda-version-check'">;
def warn_drv_new_cuda_version: Warning<
  "CUDA version%0 is newer than the latest%select{| partially}1 supported version %2">,
  InGroup<CudaUnknownVersion>;
def warn_drv_partially_supported_cuda_version: Warning<
  "CUDA version %0 is only partially supported">,
  InGroup<CudaUnknownVersion>;
def err_drv_cuda_host_arch : Error<
  "unsupported architecture '%0' for host compilation">;
def err_drv_mix_cuda_hip : Error<
  "mixed CUDA and HIP compilation is not supported">;
def err_drv_bad_target_id : Error<
  "invalid target ID '%0'; format is a processor name followed by an optional "
  "colon-delimited list of features followed by an enable/disable sign (e.g., "
  "'gfx908:sramecc+:xnack-')">;
def err_drv_bad_offload_arch_combo : Error<
  "invalid offload arch combinations: '%0' and '%1' (for a specific processor, "
  "a feature should either exist in all offload archs, or not exist in any "
  "offload archs)">;
def warn_drv_unsupported_option_for_offload_arch_req_feature : Warning<
  "ignoring '%0' option for offload arch '%1' as it is not currently supported "
  "there. Use it with an offload arch containing '%2' instead">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_option_for_target : Warning<
  "ignoring '%0' option as it is not currently supported for target '%1'">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_option_for_flang : Warning<
  "the argument '%0' is not supported for option '%1'. Mapping to '%1%2'">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_diag_option_for_flang : Warning<
  "the warning option '-%0' is not supported">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_option_for_processor : Warning<
  "ignoring '%0' option as it is not currently supported for processor '%1'">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_openmp_library : Warning<
  "the library '%0=%1' is not supported, OpenMP will not be enabled">,
  InGroup<OptionIgnored>;

def err_drv_invalid_thread_model_for_target : Error<
  "invalid thread model '%0' in '%1' for this target">;
def err_drv_invalid_linker_name : Error<
  "invalid linker name in argument '%0'">;
def err_drv_invalid_rtlib_name : Error<
  "invalid runtime library name in argument '%0'">;
def err_drv_unsupported_rtlib_for_platform : Error<
  "unsupported runtime library '%0' for platform '%1'">;
def err_drv_invalid_unwindlib_name : Error<
  "invalid unwind library name in argument '%0'">;
def err_drv_unsupported_unwind_for_platform : Error<
  "unsupported unwind library '%0' for platform '%1'">;
def err_drv_incompatible_unwindlib : Error<
  "--rtlib=libgcc requires --unwindlib=libgcc">;
def err_drv_incompatible_options : Error<
  "the combination of '%0' and '%1' is incompatible">;
def err_drv_invalid_stdlib_name : Error<
  "invalid library name in argument '%0'">;
def err_drv_invalid_output_with_multiple_archs : Error<
  "cannot use '%0' output with multiple -arch options">;
def err_drv_no_input_files : Error<"no input files">;
def err_drv_output_argument_with_multiple_files : Error<
  "cannot specify -o when generating multiple output files">;
def err_drv_out_file_argument_with_multiple_sources : Error<
  "cannot specify '%0%1' when compiling multiple source files">;
def err_no_external_assembler : Error<
  "there is no external assembler that can be used on this platform">;
def err_drv_unable_to_remove_file : Error<
  "unable to remove file: %0">;
def err_drv_unable_to_set_working_directory : Error <
  "unable to set working directory: %0">;
def err_drv_command_failure : Error<
  "unable to execute command: %0">;
def err_drv_invalid_darwin_version : Error<
  "invalid Darwin version number: %0">;
def err_drv_invalid_diagnotics_hotness_threshold : Error<
  "invalid argument in '%0', only integer or 'auto' is supported">;
def err_drv_invalid_diagnotics_misexpect_tolerance : Error<
  "invalid argument in '%0', only integers are supported">;
def err_drv_missing_argument : Error<
  "argument to '%0' is missing (expected %1 value%s1)">;
def err_drv_invalid_Xarch_argument_with_args : Error<
  "invalid Xarch argument: '%0', options requiring arguments are unsupported">;
def err_drv_Xopenmp_target_missing_triple : Error<
  "cannot deduce implicit triple value for -Xopenmp-target, specify triple using -Xopenmp-target=<triple>">;
def err_drv_invalid_Xopenmp_target_with_args : Error<
  "invalid -Xopenmp-target argument: '%0', options requiring arguments are unsupported">;
def err_drv_argument_only_allowed_with : Error<
  "invalid argument '%0' only allowed with '%1'">;
def err_drv_opt_unsupported_input_type : Error<
  "'%0' invalid for input of type %1">;
def err_drv_amdgpu_ieee_without_no_honor_nans : Error<
  "invalid argument '-mno-amdgpu-ieee' only allowed with relaxed NaN handling">;
def err_drv_argument_not_allowed_with : Error<
  "invalid argument '%0' not allowed with '%1'">;
def err_drv_cannot_open_randomize_layout_seed_file : Error<
  "cannot read randomize layout seed file '%0'">;
def err_drv_invalid_version_number : Error<
  "invalid version number in '%0'">;
def err_drv_no_linker_llvm_support : Error<
  "'%0': unable to pass LLVM bit-code files to linker">;
def err_drv_no_ast_support : Error<
  "'%0': unable to use AST files with this tool">;
def err_drv_no_module_support : Error<
  "'%0': unable to use module files with this tool">;
def err_drv_clang_unsupported : Error<
  "the clang compiler does not support '%0'">;
def err_drv_clang_unsupported_opt_cxx_darwin_i386 : Error<
  "the clang compiler does not support '%0' for C++ on Darwin/i386">;
def err_drv_clang_unsupported_opt_pg_darwin: Error<
  "the clang compiler does not support -pg option on %select{Darwin|versions of OS X 10.9 and later}0">;
def err_drv_clang_unsupported_opt_faltivec : Error<
  "the clang compiler does not support '%0', %1">;
def err_drv_command_failed : Error<
  "%0 command failed with exit code %1 (use -v to see invocation)">;
def err_drv_compilationdatabase : Error<
  "compilation database '%0' could not be opened: %1">;
def err_drv_command_signalled : Error<
  "%0 command failed due to signal (use -v to see invocation)">;
def err_drv_force_crash : Error<
  "failing because %select{environment variable 'FORCE_CLANG_DIAGNOSTICS_CRASH' is set|'-gen-reproducer' is used}0">;
def err_drv_invalid_mfloat_abi : Error<
  "invalid float ABI '%0'">;
def err_drv_invalid_mtp : Error<
  "invalid thread pointer reading mode '%0'">;
def err_drv_missing_arg_mtp : Error<
  "missing argument to '%0'">;
def warn_drv_missing_plugin_name : Warning<
  "missing plugin name in %0">,
  InGroup<InvalidCommandLineArgument>;
def warn_drv_missing_plugin_arg : Warning<
  "missing plugin argument for plugin %0 in %1">,
  InGroup<InvalidCommandLineArgument>;
def err_drv_invalid_argument_to_option : Error<
  "invalid argument '%0' to -%1">;
def err_drv_missing_sanitizer_ignorelist : Error<
  "missing sanitizer ignorelist: '%0'">;
def err_drv_malformed_sanitizer_ignorelist : Error<
  "malformed sanitizer ignorelist: '%0'">;
def err_drv_malformed_sanitizer_coverage_allowlist : Error<
  "malformed sanitizer coverage allowlist: '%0'">;
def err_drv_malformed_sanitizer_coverage_ignorelist : Error<
  "malformed sanitizer coverage ignorelist: '%0'">;
def err_drv_malformed_sanitizer_metadata_ignorelist : Error<
  "malformed sanitizer metadata ignorelist: '%0'">;
def err_drv_unsupported_static_sanitizer_darwin : Error<
  "static %0 runtime is not supported on darwin">;
def err_drv_duplicate_config : Error<
  "no more than one option '--config' is allowed">;
def err_drv_cannot_open_config_file : Error<
  "configuration file '%0' cannot be opened: %1">;
def err_drv_config_file_not_found : Error<
  "configuration file '%0' cannot be found">;
def note_drv_config_file_searched_in : Note<
  "was searched for in the directory: %0">;
def err_drv_cannot_read_config_file : Error<
  "cannot read configuration file '%0': %1">;
def err_drv_arg_requires_bitcode_input: Error<
  "option '%0' requires input to be LLVM bitcode">;

def err_target_unsupported_arch
  : Error<"the target architecture '%0' is not supported by the target '%1'">;
def err_cpu_unsupported_isa
  : Error<"CPU '%0' does not support '%1' execution mode">;
def err_arch_unsupported_isa
  : Error<"architecture '%0' does not support '%1' execution mode">;

def err_drv_I_dash_not_supported : Error<
  "'%0' not supported, please use -iquote instead">;
def err_drv_unknown_argument : Error<"unknown argument: '%0'">;
def err_drv_unknown_argument_with_suggestion : Error<
  "unknown argument '%0'; did you mean '%1'?">;
def warn_drv_unknown_argument_clang_cl : Warning<
  "unknown argument ignored in clang-cl: '%0'">,
  InGroup<UnknownArgument>;
def warn_drv_unknown_argument_clang_cl_with_suggestion : Warning<
  "unknown argument ignored in clang-cl '%0'; did you mean '%1'?">,
  InGroup<UnknownArgument>;
def err_drv_unknown_target_triple : Error<"unknown target triple '%0'">;

def warn_drv_ycyu_different_arg_clang_cl : Warning<
  "support for '/Yc' and '/Yu' with different filenames not implemented yet; flags ignored">,
  InGroup<ClangClPch>;
def warn_drv_yc_multiple_inputs_clang_cl : Warning<
  "support for '/Yc' with more than one source file not implemented yet; flag ignored">,
  InGroup<ClangClPch>;

def warn_drv_potentially_misspelled_joined_argument : Warning<
  "joined argument treated as '%0'; did you mean '%1'?">, InGroup<UnknownArgument>;

def err_drv_invalid_value : Error<"invalid value '%1' in '%0'">;
def err_drv_invalid_int_value : Error<"invalid integral value '%1' in '%0'">;
def err_drv_invalid_value_with_suggestion : Error<
    "invalid value '%1' in '%0', expected one of: %2">;
def err_drv_alignment_not_power_of_two : Error<"alignment is not a power of 2 in '%0'">;
def err_drv_invalid_remap_file : Error<
    "invalid option '%0' not of the form <from-file>;<to-file>">;
def err_drv_invalid_gcc_install_dir : Error<"'%0' does not contain a GCC installation">;
def err_drv_invalid_gcc_output_type : Error<
    "invalid output type '%0' for use with gcc tool">;
def err_drv_cc_print_options_failure : Error<
    "unable to open CC_PRINT_OPTIONS file: %0">;
def err_drv_lto_without_lld : Error<"LTO requires -fuse-ld=lld">;
def err_drv_preamble_format : Error<
    "incorrect format for -preamble-bytes=N,END">;
def err_drv_header_unit_extra_inputs : Error<
    "multiple inputs are not valid for header units (first extra '%0')">;
def warn_invalid_ios_deployment_target : Warning<
  "invalid iOS deployment version '%0', iOS 10 is the maximum deployment "
  "target for 32-bit targets">, InGroup<InvalidIOSDeploymentTarget>,
  DefaultError;
def err_invalid_macos_32bit_deployment_target : Error<
  "32-bit targets are not supported when building for Mac Catalyst">;
def err_drv_invalid_os_in_arg : Error<"invalid OS value '%0' in '%1'">;
def err_drv_conflicting_deployment_targets : Error<
  "conflicting deployment targets, both '%0' and '%1' are present in environment">;
def err_arc_unsupported_on_runtime : Error<
  "-fobjc-arc is not supported on platforms using the legacy runtime">;
def err_arc_unsupported_on_toolchain : Error< // feel free to generalize this
  "-fobjc-arc is not supported on versions of OS X prior to 10.6">;
def err_objc_weak_with_gc : Error<
  "-fobjc-weak is not supported in Objective-C garbage collection">;
def err_objc_weak_unsupported : Error<
  "-fobjc-weak is not supported on the current deployment target">;
def err_drv_mg_requires_m_or_mm : Error<
  "option '-MG' requires '-M' or '-MM'">;
def err_drv_unknown_objc_runtime : Error<
  "unknown or ill-formed Objective-C runtime '%0'">;
def err_drv_invalid_cf_runtime_abi
  : Error<"invalid CoreFoundation Runtime ABI '%0'; must be one of "
          "'objc', 'standalone', 'swift', 'swift-5.0', 'swift-4.2', 'swift-4.1'">;
def err_drv_gnustep_objc_runtime_incompatible_binary : Error<
  "GNUstep Objective-C runtime version %0 incompatible with target binary format">;
def err_drv_emit_llvm_link : Error<
   "-emit-llvm cannot be used when linking">;
def err_drv_optimization_remark_pattern : Error<
  "in pattern '%1': %0">;
def err_drv_optimization_remark_format : Error<
  "unknown remark serializer format: '%0'">;
def err_drv_no_neon_modifier : Error<"[no]neon is not accepted as modifier, please use [no]simd instead">;
def err_drv_invalid_omp_target : Error<"OpenMP target is invalid: '%0'">;
def err_drv_incompatible_omp_arch : Error<"OpenMP target architecture '%0' pointer size is incompatible with host '%1'">;
def err_drv_omp_host_ir_file_not_found : Error<
  "provided host compiler IR file '%0' is required to generate code for OpenMP "
  "target regions but cannot be found">;
def err_drv_omp_host_target_not_supported : Error<
  "target '%0' is not a supported OpenMP host target">;
def err_drv_expecting_fopenmp_with_fopenmp_targets : Error<
  "'-fopenmp-targets' must be used in conjunction with a '-fopenmp' option "
  "compatible with offloading; e.g., '-fopenmp=libomp' or '-fopenmp=libiomp5'">;
def err_drv_failed_to_deduce_target_from_arch : Error<
  "failed to deduce triple for target architecture '%0'; specify the triple "
  "using '-fopenmp-targets' and '-Xopenmp-target' instead">;
def err_drv_omp_offload_target_missingbcruntime : Error<
  "no library '%0' found in the default clang lib directory or in LIBRARY_PATH"
  "; use '--libomptarget-%1-bc-path' to specify %1 bitcode library">;
def err_drv_omp_offload_target_bcruntime_not_found : Error<
  "bitcode library '%0' does not exist">;
def err_drv_omp_offload_target_cuda_version_not_support : Error<
  "NVPTX target requires CUDA 9.2 or above; CUDA %0 detected">;
def warn_drv_omp_offload_target_duplicate : Warning<
  "OpenMP offloading target '%0' is similar to target '%1' already specified; "
  "will be ignored">, InGroup<OpenMPTarget>;
def err_drv_unsupported_embed_bitcode
    : Error<"%0 is not supported with -fembed-bitcode">;
def err_drv_bitcode_unsupported_on_toolchain : Error<
  "-fembed-bitcode is not supported on versions of iOS prior to 6.0">;
def err_drv_negative_columns : Error<
  "invalid value '%1' in '%0', value must be 'none' or a positive integer">;
def err_drv_small_columns : Error<
  "invalid value '%1' in '%0', value must be '%2' or greater">;
def warn_drv_fraw_string_literals_in_cxx11 : Warning<
  "ignoring '-f%select{no-|}0raw-string-literals', which is only valid for C and C++ standards before C++11">,
  InGroup<UnusedCommandLineArgument>;

def err_drv_invalid_malign_branch_EQ : Error<
  "invalid argument '%0' to -malign-branch=; each element must be one of: %1">;

def err_drv_print_header_env_var : Error<
  "environment variable CC_PRINT_HEADERS_%select{FORMAT|FILTERING}0 has invalid value %1">;
def err_drv_print_header_env_var_combination : Error<
  "unsupported combination: CC_PRINT_HEADERS_FORMAT=%0 and CC_PRINT_HEADERS_FILTERING=%1">;
def err_drv_print_header_env_var_combination_cc1 : Error<
  "unsupported combination: -header-include-format=%0 and -header-include-filtering=%1">;

def warn_O4_is_O3 : Warning<"-O4 is equivalent to -O3">, InGroup<Deprecated>;
def warn_drv_optimization_value : Warning<"optimization level '%0' is not supported; using '%1%2' instead">,
  InGroup<InvalidCommandLineArgument>;
def warn_ignored_gcc_optimization : Warning<"optimization flag '%0' is not supported">,
  InGroup<IgnoredOptimizationArgument>;
def warn_ignored_clang_option : Warning<"the flag '%0' has been deprecated and will be ignored">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_unsupported_opt_for_target : Warning<
  "optimization flag '%0' is not supported for target '%1'">,
  InGroup<IgnoredOptimizationArgument>;
def warn_drv_unsupported_debug_info_opt_for_target : Warning<
  "debug information option '%0' is not supported for target '%1'">,
  InGroup<UnsupportedTargetOpt>;
def warn_drv_dwarf_version_limited_by_target : Warning<
  "debug information option '%0' is not supported; requires DWARF-%2 but "
  "target '%1' only provides DWARF-%3">,
  InGroup<UnsupportedTargetOpt>;
def warn_c_kext : Warning<
  "ignoring -fapple-kext which is valid for C++ and Objective-C++ only">;
def warn_ignoring_fdiscard_for_bitcode : Warning<
  "ignoring -fdiscard-value-names for LLVM Bitcode">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_input_file_unused : Warning<
  "%0: '%1' input unused%select{ when '%3' is present|}2">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_input_file_unused_by_cpp : Warning<
  "%0: '%1' input unused in cpp mode">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_preprocessed_input_file_unused : Warning<
  "%0: previously preprocessed input%select{ unused when '%2' is present|}1">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_unused_argument : Warning<
  "argument unused during compilation: '%0'">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_unused_x : Warning<
  "'-x %0' after last input file has no effect">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_empty_joined_argument : Warning<
  "joined argument expects additional value: '%0'">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_diagnostics_hotness_requires_pgo : Warning<
  "argument '%0' requires profile-guided optimization information">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_diagnostics_misexpect_requires_pgo : Warning<
  "argument '%0' requires profile-guided optimization information">,
  InGroup<UnusedCommandLineArgument>;
def warn_drv_clang_unsupported : Warning<
  "the clang compiler does not support '%0'">;
def warn_drv_deprecated_arg : Warning<
  "argument '%0' is deprecated%select{|, use '%2' instead}1">, InGroup<Deprecated>;
def warn_drv_deprecated_arg_no_relaxed_template_template_args : Warning<
  "argument '-fno-relaxed-template-template-args' is deprecated">,
  InGroup<DeprecatedNoRelaxedTemplateTemplateArgs>;
def warn_drv_deprecated_arg_ofast : Warning<
  "argument '-Ofast' is deprecated; use '-O3 -ffast-math' for the same behavior,"
  " or '-O3' to enable only conforming optimizations">,
  InGroup<DeprecatedOFast>;
def warn_drv_deprecated_custom : Warning<
  "argument '%0' is deprecated, %1">, InGroup<Deprecated>;
def warn_drv_assuming_mfloat_abi_is : Warning<
  "unknown platform, assuming -mfloat-abi=%0">;
def warn_drv_unsupported_float_abi_by_lib : Warning<
  "float ABI '%0' is not supported by current library">,
  InGroup<UnsupportedABI>;
def warn_drv_no_floating_point_registers: Warning<
  "'%0': selected processor lacks floating point registers">,
  InGroup<UnsupportedABI>;
def warn_ignoring_ftabstop_value : Warning<
  "ignoring invalid -ftabstop value '%0', using default value %1">;
def warn_drv_overriding_option : Warning<
  "overriding '%0' option with '%1'">,
  InGroup<DiagGroup<"overriding-option">>;
def warn_drv_treating_input_as_cxx : Warning<
  "treating '%0' input as '%1' when in C++ mode, this behavior is deprecated">,
  InGroup<Deprecated>;
def warn_drv_pch_not_first_include : Warning<
  "precompiled header '%0' was ignored because '%1' is not first '-include'">;
def warn_drv_pch_ignoring_gch_file : Warning<
  "precompiled header '%0' was ignored because it is not a clang PCH file">,
  InGroup<IgnoredGCH>;
def warn_drv_pch_ignoring_gch_dir : Warning<
  "precompiled header directory '%0' was ignored because it contains no clang PCH files">,
  InGroup<IgnoredGCH>;
def warn_missing_sysroot : Warning<"no such sysroot directory: '%0'">,
  InGroup<DiagGroup<"missing-sysroot">>;
def warn_incompatible_sysroot : Warning<"using sysroot for '%0' but targeting '%1'">,
  InGroup<DiagGroup<"incompatible-sysroot">>;
def warn_debug_compression_unavailable : Warning<"cannot compress debug sections (%0 not enabled)">,
  InGroup<DiagGroup<"debug-compression-unavailable">>;
def warn_drv_disabling_vptr_no_rtti_default : Warning<
  "implicitly disabling vptr sanitizer because rtti wasn't enabled">,
  InGroup<AutoDisableVptrSanitizer>;
def warn_drv_object_size_disabled_O0 : Warning<
  "the object size sanitizer has no effect at -O0, but is explicitly enabled: %0">,
  InGroup<InvalidCommandLineArgument>, DefaultWarnNoWerror;
def warn_ignoring_verify_debuginfo_preserve_export : Warning<
  "ignoring -fverify-debuginfo-preserve-export=%0 because "
  "-fverify-debuginfo-preserve wasn't enabled">,
  InGroup<UnusedCommandLineArgument>;
def warn_unsupported_branch_protection: Warning <
  "invalid branch protection option '%0' in '%1'">, InGroup<BranchProtection>;
def err_sls_hardening_arm_not_supported : Error<
  "-mharden-sls is only supported on armv7-a or later">;
def warn_drv_large_data_threshold_invalid_code_model: Warning<
  "'%0' only applies to medium and large code models">,
  InGroup<UnusedCommandLineArgument>;

def note_drv_command_failed_diag_msg : Note<
  "diagnostic msg: %0">;
def note_drv_t_option_is_global : Note<
  "the last '/TC' or '/TP' option takes precedence over earlier instances">;
def note_drv_address_sanitizer_debug_runtime : Note<
  "AddressSanitizer doesn't support linking with debug runtime libraries yet">;
def note_drv_use_standard : Note<"use '%0'"
  "%select{| or '%3'|, '%3', or '%4'|, '%3', '%4', or '%5'}2 "
  "for '%1' standard">;

def err_analyzer_config_no_value : Error<
  "analyzer-config option '%0' has a key but no value">;
def err_analyzer_config_multiple_values : Error<
  "analyzer-config option '%0' should contain only one '='">;
def err_analyzer_config_invalid_input : Error<
  "invalid input for analyzer-config option '%0', that expects %1 value">;
def err_analyzer_config_unknown : Error<"unknown analyzer-config '%0'">;
def err_analyzer_checker_option_unknown : Error<
  "checker '%0' has no option called '%1'">;
def err_analyzer_checker_option_invalid_input : Error<
  "invalid input for checker option '%0', that expects %1">;
def err_analyzer_checker_incompatible_analyzer_option : Error<
  "checker cannot be enabled with analyzer option '%0' == %1">;
def err_analyzer_not_built_with_z3 : Error<
  "analyzer constraint manager 'z3' is only available if LLVM was built with "
  "-DLLVM_ENABLE_Z3_SOLVER=ON">;

def warn_drv_needs_hvx : Warning<
  "%0 requires HVX, use -mhvx/-mhvx= to enable it">,
  InGroup<OptionIgnored>;
def err_drv_needs_hvx : Error<
  "%0 requires HVX, use -mhvx/-mhvx= to enable it">;
def err_drv_needs_hvx_version : Error<
  "%0 is not supported on HVX %1">;

def err_drv_module_header_wrong_kind : Error<
  "header file '%0' input type '%1' does not match type of prior input "
  "in module compilation; use '-x %2' to override">;
def err_drv_modules_validate_once_requires_timestamp : Error<
  "option '-fmodules-validate-once-per-build-session' requires "
  "'-fbuild-session-timestamp=<seconds since Epoch>' or '-fbuild-session-file=<file>'">;

def err_test_module_file_extension_format : Error<
  "-ftest-module-file-extension argument '%0' is not of the required form "
  "'blockname:major:minor:hashed:user info'">;

def err_drv_module_output_with_multiple_arch : Error<
  "option '-fmodule-output' can't be used with multiple arch options">;

def warn_drv_delayed_template_parsing_after_cxx20 : Warning<
  "-fdelayed-template-parsing is deprecated after C++20">,
  InGroup<DiagGroup<"delayed-template-parsing-in-cxx20">>;

def err_drv_extract_api_wrong_kind : Error<
  "header file '%0' input '%1' does not match the type of prior input "
  "in api extraction; use '-x %2' to override">;

def err_drv_missing_symbol_graph_dir: Error<
  "must provide a symbol graph output directory using "
  "'--symbol-graph-dir=<directory>'">;

def err_drv_unexpected_symbol_graph_output : Error<
  "unexpected output symbol graph '%1'; please provide "
  "'--symbol-graph-dir=<directory>' instead">;

def warn_slash_u_filename : Warning<"'/U%0' treated as the '/U' option">,
  InGroup<DiagGroup<"slash-u-filename">>;
def note_use_dashdash : Note<
  "use '--' to treat subsequent arguments as filenames">;

def err_drv_ropi_rwpi_incompatible_with_pic : Error<
  "embedded and GOT-based position independence are incompatible">;
def err_drv_ropi_incompatible_with_cxx : Error<
  "ROPI is not compatible with c++">;

def err_stack_tagging_requires_hardware_feature : Error<
  "'-fsanitize=memtag-stack' requires hardware support (+memtag). For Armv8 or "
  "Armv9, try compiling with -march=armv8a+memtag or -march=armv9a+memtag">;

def err_cmse_pi_are_incompatible : Error<
  "cmse is not compatible with %select{RWPI|ROPI}0">;

def warn_target_unsupported_nan2008 : Warning<
  "ignoring '-mnan=2008' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedNan>;
def warn_target_unsupported_nanlegacy : Warning<
  "ignoring '-mnan=legacy' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedNan>;
def warn_target_unsupported_abslegacy : Warning<
  "ignoring '-mabs=legacy' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedAbs>;
def warn_target_unsupported_abs2008 : Warning<
  "ignoring '-mabs=2008' option because the '%0' architecture does not support it">,
  InGroup<UnsupportedAbs>;
def warn_target_unsupported_compact_branches : Warning<
  "ignoring '-mcompact-branches=' option because the '%0' architecture does not"
  " support it">, InGroup<UnsupportedCB>;
def warn_target_unsupported_extension : Warning<
  "ignoring extension '%0' because the '%1' architecture does not support it">,
   InGroup<InvalidCommandLineArgument>;
def warn_drv_unsupported_gpopt : Warning<
  "ignoring '-mgpopt' option as it cannot be used with %select{|the implicit"
  " usage of }0-mabicalls">,
  InGroup<UnsupportedGPOpt>;
def warn_drv_unsupported_sdata : Warning<
  "ignoring '-msmall-data-limit=' with -mcmodel=large for -fpic or RV64">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_longcalls : Warning<
  "ignoring '-mlong-calls' option as it is not currently supported with "
  "%select{|the implicit usage of }0-mabicalls">,
  InGroup<OptionIgnored>;
def warn_drv_unsupported_pic_with_mabicalls : Warning<
  "ignoring '%0' option as it cannot be used with "
  "%select{implicit usage of|}1 -mabicalls and the N64 ABI">,
  InGroup<OptionIgnored>;
def err_drv_unsupported_noabicalls_pic : Error<
  "position-independent code requires '-mabicalls'">;
def err_drv_unsupported_indirect_jump_opt : Error<
  "'-mindirect-jump=%0' is unsupported with the '%1' architecture">;
def err_drv_unknown_indirect_jump_opt : Error<
  "unknown '-mindirect-jump=' option '%0'">;
def err_drv_unsupported_fpatchable_function_entry_argument : Error<
  "the second argument of '-fpatchable-function-entry' must be smaller than the first argument">;

def warn_drv_unable_to_find_directory_expected : Warning<
  "unable to find %0 directory, expected to be in '%1' found via %2">,
  InGroup<InvalidOrNonExistentDirectory>, DefaultIgnore;

def warn_drv_ps_force_pic : Warning<
  "option '%0' was ignored by the %1 toolchain, using '-fPIC'">,
  InGroup<OptionIgnored>;

def err_drv_defsym_invalid_format : Error<"defsym must be of the form: sym=value: %0">;
def err_drv_defsym_invalid_symval : Error<"value is not an integer: %0">;
def warn_drv_msvc_not_found : Warning<
  "unable to find a Visual Studio installation; "
  "try running Clang from a developer command prompt">,
  InGroup<DiagGroup<"msvc-not-found">>;

def warn_drv_fuse_ld_path : Warning<
  "'-fuse-ld=' taking a path is deprecated; use '--ld-path=' instead">,
  InGroup<FUseLdPath>, DefaultIgnore;

def warn_drv_fine_grained_bitfield_accesses_ignored : Warning<
  "option '-ffine-grained-bitfield-accesses' cannot be enabled together with a sanitizer; flag ignored">,
  InGroup<OptionIgnored>;

def note_drv_verify_prefix_spelling : Note<
  "-verify prefixes must start with a letter and contain only alphanumeric"
  " characters, hyphens, and underscores">;

def warn_drv_global_isel_incomplete : Warning<
  "-fglobal-isel support for the '%0' architecture is incomplete">,
  InGroup<GlobalISel>;

def warn_drv_global_isel_incomplete_opt : Warning<
  "-fglobal-isel support is incomplete for this architecture at the current optimization level">,
  InGroup<GlobalISel>;

def warn_drv_moutline_unsupported_opt : Warning<
  "'%0' does not support '-moutline'; flag ignored">,
  InGroup<OptionIgnored>;

def warn_drv_moutline_atomics_unsupported_opt : Warning<
  "'%0' does not support '-%1'; flag ignored">,
  InGroup<OptionIgnored>;

def warn_drv_darwin_sdk_invalid_settings : Warning<
  "SDK settings were ignored as 'SDKSettings.json' could not be parsed">,
  InGroup<DiagGroup<"darwin-sdk-settings">>;

def err_missing_sysroot : Error<"no such sysroot directory: '%0'">;
def err_drv_darwin_sdk_missing_arclite : Error<
  "SDK does not contain 'libarclite' at the path '%0'; try increasing the minimum deployment target">;

def err_drv_trivial_auto_var_init_stop_after_missing_dependency : Error<
  "'-ftrivial-auto-var-init-stop-after=*' is used without "
  "'-ftrivial-auto-var-init=zero' or '-ftrivial-auto-var-init=pattern'">;

def err_drv_trivial_auto_var_init_stop_after_invalid_value : Error<
  "'-ftrivial-auto-var-init-stop-after=*' only accepts positive integers">;

def err_drv_trivial_auto_var_init_max_size_missing_dependency : Error<
  "'-ftrivial-auto-var-init-max-size=*' is used without "
  "'-ftrivial-auto-var-init=zero' or '-ftrivial-auto-var-init=pattern'">;

def err_drv_trivial_auto_var_init_max_size_invalid_value : Error<
  "'-ftrivial-auto-var-init-max-size=*' only accepts positive integers (in bytes)">;

def warn_drv_msp430_hwmult_unsupported : Warning<
  "the given MCU does not support hardware multiply, but '-mhwmult' is set to "
  "%0">, InGroup<InvalidCommandLineArgument>;
def warn_drv_msp430_hwmult_mismatch : Warning<
  "the given MCU supports %0 hardware multiply, but '-mhwmult' is set to %1">,
   InGroup<InvalidCommandLineArgument>;
def warn_drv_msp430_hwmult_no_device : Warning<
  "no MCU device specified, but '-mhwmult' is set to 'auto', assuming no "
  "hardware multiply; use '-mmcu' to specify an MSP430 device, or '-mhwmult' "
  "to set the hardware multiply type explicitly">,
  InGroup<InvalidCommandLineArgument>;

def warn_drv_libstdcxx_not_found : Warning<
  "include path for libstdc++ headers not found; pass '-stdlib=libc++' on the "
  "command line to use the libc++ standard library instead">,
  InGroup<DiagGroup<"stdlibcxx-not-found">>;

def err_drv_cannot_mix_options : Error<"cannot specify '%1' along with '%0'">;

def err_drv_invalid_object_mode : Error<
  "OBJECT_MODE setting %0 is not recognized and is not a valid setting">;

def err_roptr_requires_data_sections: Error<"-mxcoff-roptr is supported only with -fdata-sections">;
def err_roptr_cannot_build_shared: Error<"-mxcoff-roptr is not supported with -shared">;

def err_invalid_cxx_abi : Error<"invalid C++ ABI name '%0'">;
def err_unsupported_cxx_abi : Error<"C++ ABI '%0' is not supported on target triple '%1'">;

def note_cc1_round_trip_original : Note<"original arguments in round-trip: %0">;
def note_cc1_round_trip_generated : Note<
  "generated arguments #%0 in round-trip: %1">;
def remark_cc1_round_trip_generated : Remark<
  "generated arguments #%0 in round-trip: %1">, InGroup<RoundTripCC1Args>;
def err_cc1_round_trip_fail_then_ok : Error<
  "original arguments parse failed, then succeeded in round-trip">;
def err_cc1_round_trip_ok_then_fail : Error<
  "generated arguments parse failed in round-trip">;
def err_cc1_round_trip_mismatch : Error<
  "generated arguments do not match in round-trip">;
def err_cc1_unbounded_vscale_min : Error<
  "minimum vscale must be an unsigned integer greater than 0">;

def err_drv_using_omit_rtti_component_without_no_rtti : Error<
  "-fexperimental-omit-vtable-rtti call only be used with -fno-rtti">;

def err_drv_ssp_missing_offset_argument : Error<
  "'%0' is used without '-mstack-protector-guard-offset', and there is no default">;

def err_drv_only_one_offload_target_supported : Error<
  "only one offload target is supported">;
def err_drv_invalid_or_unsupported_offload_target : Error<
  "invalid or unsupported offload target: '%0'">;
def err_drv_cuda_offload_only_emit_bc : Error<
  "CUDA offload target is supported only along with --emit-llvm">;

def warn_drv_jmc_requires_debuginfo : Warning<
  "%0 requires debug info. Use %1 or debug options that enable debugger's "
  "stepping function; option ignored">,
  InGroup<OptionIgnored>;

def warn_drv_fjmc_for_elf_only : Warning<
  "-fjmc works only for ELF; option ignored">,
  InGroup<OptionIgnored>;

def warn_target_override_arm64ec : Warning<
  "/arm64EC has been overridden by specified target: %0; option ignored">,
  InGroup<OptionIgnored>;

def err_drv_target_variant_invalid : Error<
  "unsupported '%0' value '%1'; use 'ios-macabi' instead">;

def err_drv_invalid_directx_shader_module : Error<
  "invalid profile : %0">;
def err_drv_dxc_missing_target_profile : Error<
  "target profile option (-T) is missing">;
def err_drv_hlsl_unsupported_target : Error<
  "HLSL code generation is unsupported for target '%0'">;
def err_drv_hlsl_bad_shader_required_in_target : Error<
  "%select{shader model|Vulkan environment|shader stage}0 is required as %select{OS|environment}1 in target '%2' for HLSL code generation">;
def err_drv_hlsl_16bit_types_unsupported: Error<
  "'%0' option requires target HLSL Version >= 2018%select{| and shader model >= 6.2}1, but HLSL Version is '%2'%select{| and shader model is '%3'}1">;
def err_drv_hlsl_bad_shader_unsupported : Error<
  "%select{shader model|Vulkan environment|shader stage}0 '%1' in target '%2' is invalid for HLSL code generation">;
def warn_drv_dxc_missing_dxv : Warning<
  "dxv not found; resulting DXIL will not be validated or signed for use in "
  "release environment">, InGroup<DXILValidation>;

def err_drv_invalid_range_dxil_validator_version : Error<
  "invalid validator version : %0; validator version must be less than or "
  "equal to current internal version">;
def err_drv_invalid_format_dxil_validator_version : Error<
  "invalid validator version : %0; format of validator version is "
  "\"<major>.<minor>\" (ex:\"1.4\")">;
def err_drv_invalid_empty_dxil_validator_version : Error<
  "invalid validator version : %0; if validator major version is 0, minor "
  "version must also be 0">;

def warn_drv_sarif_format_unstable : Warning<
  "diagnostic formatting in SARIF mode is currently unstable">,
  InGroup<DiagGroup<"sarif-format-unstable">>;

def err_drv_riscv_unsupported_with_linker_relaxation : Error<
  "%0 is unsupported with RISC-V linker relaxation (-mrelax)">;

def warn_drv_loongarch_conflicting_implied_val : Warning<
  "ignoring '%0' as it conflicts with that implied by '%1' (%2)">,
  InGroup<OptionIgnored>;
def err_drv_loongarch_invalid_mfpu_EQ : Error<
  "invalid argument '%0' to -mfpu=; must be one of: 64, 32, none, 0 (alias for none)">;
def err_drv_loongarch_wrong_fpu_width : Error<
  "wrong fpu width; %select{LSX|LASX}0 depends on 64-bit FPU">;
def err_drv_loongarch_invalid_simd_option_combination : Error<
  "invalid option combination; LASX depends on LSX">;
def err_drv_loongarch_invalid_msimd_EQ : Error<
  "invalid argument '%0' to -msimd=; must be one of: none, lsx, lasx">;

def err_drv_expand_response_file : Error<
  "failed to expand response file: %0">;

def warn_drv_missing_multilib : Warning<
  "no multilib found matching flags: %0">,
  InGroup<DiagGroup<"missing-multilib">>;
def note_drv_available_multilibs : Note<
  "available multilibs are:%0">;

def err_drv_experimental_crel : Error<
  "-Wa,--allow-experimental-crel must be specified to use -Wa,--crel. "
  "CREL is experimental and uses a non-standard section type code">;

def warn_android_unversioned_fallback : Warning<
  "using unversioned Android target directory %0 for target %1; unversioned "
  "directories will not be used in Clang 19 -- provide a versioned directory "
  "for the target version or lower instead">,
  InGroup<DiagGroup<"android-unversioned-fallback">>;

def err_drv_triple_version_invalid : Error<
  "version '%0' in target triple '%1' is invalid">;

def warn_missing_include_dirs : Warning<
  "no such include directory: '%0'">, InGroup<MissingIncludeDirs>, DefaultIgnore;
}
