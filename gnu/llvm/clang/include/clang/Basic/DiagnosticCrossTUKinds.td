//==--- DiagnosticCrossTUKinds.td - Cross Translation Unit diagnostics ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let Component = "CrossTU" in {

def err_ctu_error_opening : Error<
  "error opening '%0': required by the CrossTU functionality">;

def err_extdefmap_parsing : Error<
  "error parsing index file: '%0' line: %1 '<USR-Length>:<USR> <File-Path>' "
  "format expected">;

def err_multiple_def_index : Error<
  "multiple definitions are found for the same key in index ">;

def warn_ctu_incompat_triple : Warning<
  "imported AST from '%0' had been generated for a different target, "
  "current: %1, imported: %2">, InGroup<CrossTU>;
}
