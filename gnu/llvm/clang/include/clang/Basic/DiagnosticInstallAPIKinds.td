//==--- DiagnosticInstallAPIKinds.td - installapi diagnostics -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// InstallAPI Diagnostics
//===----------------------------------------------------------------------===//

let Component = "InstallAPI" in {
let CategoryName = "Command line" in {
def err_cannot_write_file : Error<"cannot write file '%0': %1">;
def err_no_install_name : Error<"no install name specified: add -install_name <path>">;
def err_no_output_file: Error<"no output file specified">;
def err_no_such_header_file : Error<"no such %select{public|private|project}1 header file: '%0'">;
def warn_no_such_excluded_header_file : Warning<"no such excluded %select{public|private}0 header file: '%1'">, InGroup<InstallAPIViolation>;
def warn_glob_did_not_match: Warning<"glob '%0' did not match any header file">, InGroup<InstallAPIViolation>;
def err_no_such_umbrella_header_file : Error<"%select{public|private|project}1 umbrella header file not found in input: '%0'">;
def err_cannot_find_reexport : Error<"cannot find re-exported %select{framework|library}0: '%1'">;
def err_no_matching_target : Error<"no matching target found for target variant '%0'">;
def err_unsupported_vendor : Error<"vendor '%0' is not supported: '%1'">;
def err_unsupported_environment : Error<"environment '%0' is not supported: '%1'">;
def err_unsupported_os : Error<"os '%0' is not supported: '%1'">;
def err_cannot_read_input_list : Error<"could not read %0 input list '%1': %2">;
def err_invalid_label: Error<"label '%0' is reserved: use a different label name for -X<label>">;
def err_directory_scanning: Error<"could not read directory '%0': %1">;
def err_more_than_one_library: Error<"more than one framework/dynamic library found">;
} // end of command line category.

let CategoryName = "Verification" in {
// Diagnostics about symbols.
def warn_target: Warning<"violations found for %0">, InGroup<InstallAPIViolation>;
def err_library_missing_symbol : Error<"declaration has external linkage, but dynamic library doesn't have symbol '%0'">;
def warn_library_missing_symbol : Warning<"declaration has external linkage, but dynamic library doesn't have symbol '%0'">, InGroup<InstallAPIViolation>;
def err_library_hidden_symbol : Error<"declaration has external linkage, but symbol has internal linkage in dynamic library '%0'">;
def warn_library_hidden_symbol : Warning<"declaration has external linkage, but symbol has internal linkage in dynamic library '%0'">, InGroup<InstallAPIViolation>;
def warn_header_hidden_symbol : Warning<"symbol exported in dynamic library, but marked hidden in declaration '%0'">, InGroup<InstallAPIViolation>;
def err_header_hidden_symbol : Error<"symbol exported in dynamic library, but marked hidden in declaration '%0'">;
def err_header_symbol_missing : Error<"no declaration found for exported symbol '%0' in dynamic library">;
def warn_header_symbol_missing : Warning<"no declaration was found for exported symbol '%0' in dynamic library">, InGroup<InstallAPIViolation>;
def warn_header_availability_mismatch : Warning<"declaration '%0' is marked %select{available|unavailable}1,"
  " but symbol is %select{not |}2exported in dynamic library">, InGroup<InstallAPIViolation>;
def err_header_availability_mismatch : Error<"declaration '%0' is marked %select{available|unavailable}1,"
  " but symbol is %select{not |}2exported in dynamic library">;
def warn_dylib_symbol_flags_mismatch : Warning<"dynamic library symbol '%0' is "
  "%select{weak defined|thread local}1, but its declaration is not">, InGroup<InstallAPIViolation>;
def warn_header_symbol_flags_mismatch : Warning<"declaration '%0' is "
  "%select{weak defined|thread local}1, but symbol is not in dynamic library">, InGroup<InstallAPIViolation>;
def err_dylib_symbol_flags_mismatch : Error<"dynamic library symbol '%0' is "
  "%select{weak defined|thread local}1, but its declaration is not">;
def err_header_symbol_flags_mismatch : Error<"declaration '%0' is "
  "%select{weak defined|thread local}1, but symbol is not in dynamic library">;

// Diagnostics about load commands.
def err_architecture_mismatch : Error<"architectures do not match: '%0' (provided) vs '%1' (found)">;
def warn_platform_mismatch : Warning<"platform does not match: '%0' (provided) vs '%1' (found)">, InGroup<InstallAPIViolation>;
def err_platform_mismatch : Error<"platform does not match: '%0' (provided) vs '%1' (found)">;
def err_install_name_mismatch : Error<"install_name does not match: '%0' (provided) vs '%1' (found)">;
def err_current_version_mismatch : Error<"current_version does not match: '%0' (provided) vs '%1' (found)">;
def err_compatibility_version_mismatch : Error<"compatibility_version does not match: '%0' (provided) vs '%1' (found)">;
def err_appextension_safe_mismatch : Error<"the ApplicationExtensionSafe flag does not match: '%0' (provided) vs '%1' (found)">;
def err_shared_cache_eligiblity_mismatch : Error<"the NotForDyldSharedCache flag does not match: '%0' (provided) vs '%1' (found)">;
def err_no_twolevel_namespace : Error<"flat namespace libraries are not supported">;
def err_parent_umbrella_missing: Error<"parent umbrella missing from %0: '%1'">;
def err_parent_umbrella_mismatch : Error<"parent umbrella does not match: '%0' (provided) vs '%1' (found)">;
def err_reexported_libraries_missing : Error<"re-exported library missing from %0: '%1'">;
def err_reexported_libraries_mismatch : Error<"re-exported libraries do not match: '%0' (provided) vs '%1' (found)">;
def err_allowable_clients_missing : Error<"allowable client missing from %0: '%1'">;
def err_allowable_clients_mismatch : Error<"allowable clients do not match: '%0' (provided) vs '%1' (found)">;
def warn_rpaths_missing : Warning<"runpath search paths missing from %0: '%1'">, InGroup<InstallAPIViolation>;
def warn_rpaths_mismatch : Warning<"runpath search paths do not match: '%0' (provided) vs '%1' (found)">, InGroup<InstallAPIViolation>;
} // end of Verification category.

} // end of InstallAPI component
