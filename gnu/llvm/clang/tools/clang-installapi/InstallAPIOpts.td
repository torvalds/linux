//===--- InstallAPIOpts.td ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the specific options for InstallAPI.
//
//===----------------------------------------------------------------------===//

// Include the common option parsing interfaces.
include "llvm/Option/OptParser.td"


/////////
// Options

//
/// TextAPI options.
//
def filetype : Joined<["--"], "filetype=">,
  HelpText<"Specify the output file type (tbd-v4 or tbd-v5)">;
def not_for_dyld_shared_cache : Joined<["-"], "not_for_dyld_shared_cache">,
  HelpText<"Mark library as shared cache ineligible">;

//
/// Debugging or logging options.
//
def t: Flag<["-"], "t">,
  HelpText<"Logs each dylib loaded for InstallAPI. Useful for debugging problems with search paths where the wrong library is loaded.">;

//
/// Verification options.
//
def verify_against : Separate<["-"], "verify-against">,
  HelpText<"Verify the specified dynamic library/framework against the headers">;
def verify_against_EQ : Joined<["--"], "verify-against=">, Alias<verify_against>;
def verify_mode_EQ : Joined<["--"], "verify-mode=">,
  HelpText<"Specify the severity and extend of the validation. Valid modes are ErrorsOnly, ErrorsAndWarnings, and Pedantic.">;
def demangle : Flag<["--", "-"], "demangle">,
  HelpText<"Demangle symbols when printing warnings and errors">;
def dsym: Joined<["--"], "dsym=">,
  MetaVarName<"<path>">, HelpText<"Specify dSYM path for enriched diagnostics.">;

//
/// Additional input options.
//
def extra_project_header : Separate<["-"], "extra-project-header">,
  MetaVarName<"<path>">,
  HelpText<"Add additional project header location for parsing">;
def extra_project_header_EQ : Joined<["--"], "extra-project-header=">,
  Alias<extra_project_header>;
def exclude_project_header : Separate<["-"], "exclude-project-header">,
  MetaVarName<"<glob>">,
  HelpText<"Exclude project header from parsing">;
def exclude_project_header_EQ : Joined<["--"], "exclude-project-header=">,
  Alias<exclude_project_header>;
def extra_public_header : Separate<["-"], "extra-public-header">,
  MetaVarName<"<path>">,
  HelpText<"Add additional public header location for parsing">;
def extra_public_header_EQ : Joined<["--"], "extra-public-header=">,
  Alias<extra_public_header>;
def extra_private_header : Separate<["-"], "extra-private-header">,
  MetaVarName<"<path>">,
  HelpText<"Add additional private header location for parsing">;
def extra_private_header_EQ : Joined<["--"], "extra-private-header=">,
  Alias<extra_private_header>;
def exclude_public_header : Separate<["-"], "exclude-public-header">,
  MetaVarName<"<glob>">,
  HelpText<"Exclude public header from parsing">;
def exclude_public_header_EQ : Joined<["--"], "exclude-public-header=">,
  Alias<exclude_public_header>;
def exclude_private_header : Separate<["-"], "exclude-private-header">,
  MetaVarName<"<glob>">,
  HelpText<"Exclude private header from parsing">;
def exclude_private_header_EQ : Joined<["--"], "exclude-private-header=">,
  Alias<exclude_private_header>;
def public_umbrella_header : Separate<["-"], "public-umbrella-header">,
  MetaVarName<"<path>">, HelpText<"Specify the public umbrella header location">;
def public_umbrella_header_EQ : Joined<["--"], "public-umbrella-header=">,
  Alias<public_umbrella_header>;
def private_umbrella_header : Separate<["-"], "private-umbrella-header">,
  MetaVarName<"<path>">, HelpText<"Specify the private umbrella header location">;
def private_umbrella_header_EQ : Joined<["--"], "private-umbrella-header=">,
  Alias<private_umbrella_header>;
def project_umbrella_header : Separate<["-"], "project-umbrella-header">,
  MetaVarName<"<path>">, HelpText<"Specify the project umbrella header location">;
def project_umbrella_header_EQ : Joined<["--"], "project-umbrella-header=">,
  Alias<project_umbrella_header>;

//
/// X<label> overrides.
//
def Xplatform__ : Joined<["-"], "Xplatform_">;
def Xproject : Joined<["-"], "Xproject">;
def X__ : Joined<["-"], "X">,
  HelpText<"Pass <arg> to run unique clang invocation identified as <label>">, 
  MetaVarName<"<label> <arg>">;

def option_list : Separate<["-"], "optionlist">, MetaVarName<"<path>">, 
  HelpText<"Specifies the <path> to a file that contains X<label> arguments to parse.">;

//
/// Overidden clang options for different behavior.
//

// Clang's Xarch does not support options that require arguments. 
// But is supported for InstallAPI generation.
def Xarch__ : Joined<["-"], "Xarch_">;
def allowable_client : Separate<["-"], "allowable_client">,
  HelpText<"Restricts what can link against the dynamic library being created">;
def rpath: Separate<["-"], "rpath">,
  HelpText<"Add path to the runpath search path list for the dynamic library being created.">;
def reexport_l : Joined<["-"], "reexport-l">,
  HelpText<"Re-export the specified library">;
def reexport_library : Separate<["-"], "reexport_library">, MetaVarName<"<path>">,
  HelpText<"Re-export the specified library">;
def reexport_framework : Separate<["-"], "reexport_framework">,
  HelpText<"Re-export the specified framework">;

// Xproject supported options.
def fobjc_arc : Flag<["-"], "fobjc-arc">,
  HelpText<"Synthesize retain and release calls for Objective-C pointers">;
def include_ : JoinedOrSeparate<["-", "--"], "include">,
  MetaVarName<"<file>">, HelpText<"Include file before parsing, can only be used with -Xproject">;
def fvisibility_EQ : Joined<["-"], "fvisibility=">,
  HelpText<"Set the default symbol visibility for all global declarations">;
def fmodules : Flag <["-"], "fmodules">,
  HelpText<"Enable the 'modules' language feature">;
def fmodules_cache_path : Joined<["-"], "fmodules-cache-path=">,
  MetaVarName<"<directory>">,
  HelpText<"Specify the module cache path">;

// Xplatform supported options.
def iframework : JoinedOrSeparate<["-"], "iframework">,
  HelpText<"Add directory to SYSTEM framework search path">;

// X<label> prefixes supported options.
def D : JoinedOrSeparate<["-"], "D">, HelpText<"Define macro">;
def U : JoinedOrSeparate<["-"], "U">, HelpText<"Undefine macro">;


