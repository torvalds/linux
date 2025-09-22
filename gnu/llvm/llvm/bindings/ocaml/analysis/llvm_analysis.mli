(*===-- llvm_analysis.mli - LLVM OCaml Interface --------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

(** Intermediate representation analysis.

    This interface provides an OCaml API for LLVM IR analyses, the classes in
    the Analysis library. *)

(** [verify_module m] returns [None] if the module [m] is valid, and
    [Some reason] if it is invalid. [reason] is a string containing a
    human-readable validation report. See [llvm::verifyModule]. *)
external verify_module : Llvm.llmodule -> string option = "llvm_verify_module"

(** [verify_function f] returns [true] if the function [f] is valid, and
    [false] if it is invalid. See [llvm::verifyFunction]. *)
external verify_function : Llvm.llvalue -> bool = "llvm_verify_function"

(** [verify_module m] returns if the module [m] is valid, but prints a
    validation report to [stderr] and aborts the program if it is invalid. See
    [llvm::verifyModule]. *)
external assert_valid_module : Llvm.llmodule -> unit
                             = "llvm_assert_valid_module"

(** [verify_function f] returns if the function [f] is valid, but prints a
    validation report to [stderr] and aborts the program if it is invalid. See
    [llvm::verifyFunction]. *)
external assert_valid_function : Llvm.llvalue -> unit
                               = "llvm_assert_valid_function"

(** [view_function_cfg f] opens up a ghostscript window displaying the CFG of
    the current function with the code for each basic block inside.
    See [llvm::Function::viewCFG]. *)
external view_function_cfg : Llvm.llvalue -> unit = "llvm_view_function_cfg"

(** [view_function_cfg_only f] works just like [view_function_cfg], but does not
    include the contents of basic blocks into the nodes.
    See [llvm::Function::viewCFGOnly]. *)
external view_function_cfg_only : Llvm.llvalue -> unit
                                = "llvm_view_function_cfg_only"
