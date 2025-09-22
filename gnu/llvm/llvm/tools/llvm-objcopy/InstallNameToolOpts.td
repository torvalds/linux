//===-- InstallNameToolOpts.td - llvm-install-name-tool options  --------*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes the command line options of llvm-install-name.
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def help : Flag<["--"], "help">;
def h : Flag<["-"], "h">, Alias<help>;

def add_rpath : Option<["-", "--"], "add_rpath", KIND_SEPARATE>,
                HelpText<"Add new rpath">;

def prepend_rpath : Option<["-", "--"], "prepend_rpath", KIND_SEPARATE>,
                    HelpText<"Add new rpath before other rpaths">;

def delete_rpath: Option<["-", "--"], "delete_rpath", KIND_SEPARATE>,
                  HelpText<"Delete specified rpath">;

def delete_all_rpaths: Flag<["-", "--"], "delete_all_rpaths">,
              HelpText<"Delete all rpath directives">;

def rpath: MultiArg<["-", "--"], "rpath", 2>,
           HelpText<"Change rpath path name">;

def id : Option<["-","--"], "id", KIND_SEPARATE>,
                 HelpText<"Change dynamic shared library id">;

def change: MultiArg<["-", "--"], "change", 2>,
            HelpText<"Change dependent shared library install name">;

def version : Flag<["--"], "version">,
              HelpText<"Print the version and exit.">;

def V : Flag<["-"], "V">,
        Alias<version>,
        HelpText<"Alias for --version">;
