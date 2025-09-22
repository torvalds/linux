//===-- BitcodeStripOpts.td - llvm-bitcode-strip options  ---------------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the command line options of llvm-bitcode-strip.
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def help : Flag<["--"], "help">;

def h : Flag<["-"], "h">, Alias<help>;

def version : Flag<["--"], "version">,
              HelpText<"Print the version and exit">;

def V : Flag<["-"], "V">,
        Alias<version>,
        HelpText<"Alias for --version">;

def remove : Flag<["-"], "r">,
        HelpText<"Remove the __LLVM bitcode segment entirely">;

def output : JoinedOrSeparate<["-"], "o">, HelpText<"Write output to <file>">,
             MetaVarName<"<file>">;
