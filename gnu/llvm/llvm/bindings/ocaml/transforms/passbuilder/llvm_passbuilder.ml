(*===-- llvm_passbuilder.ml - LLVM OCaml Interface -------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

type llpassbuilder_options

external run_passes
  : Llvm.llmodule
  -> string
  -> Llvm_target.TargetMachine.t
  -> llpassbuilder_options
  -> (unit, string) result
  = "llvm_run_passes"

external create_passbuilder_options : unit -> llpassbuilder_options =
  "llvm_create_passbuilder_options"

external passbuilder_options_set_verify_each
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_verify_each"

external passbuilder_options_set_debug_logging
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_debug_logging"

external passbuilder_options_set_loop_interleaving
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_loop_interleaving"

external passbuilder_options_set_loop_vectorization
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_loop_vectorization"

external passbuilder_options_set_slp_vectorization
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_slp_vectorization"

external passbuilder_options_set_loop_unrolling
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_loop_unrolling"

external passbuilder_options_set_forget_all_scev_in_loop_unroll
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_forget_all_scev_in_loop_unroll"

external passbuilder_options_set_licm_mssa_opt_cap
  : llpassbuilder_options -> int -> unit =
  "llvm_passbuilder_options_set_licm_mssa_opt_cap"

external passbuilder_options_set_licm_mssa_no_acc_for_promotion_cap
  : llpassbuilder_options -> int -> unit =
  "llvm_passbuilder_options_set_licm_mssa_opt_cap"

external passbuilder_options_set_call_graph_profile
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_call_graph_profile"

external passbuilder_options_set_merge_functions
  : llpassbuilder_options -> bool -> unit =
  "llvm_passbuilder_options_set_merge_functions"

external passbuilder_options_set_inliner_threshold
  : llpassbuilder_options -> int -> unit =
  "llvm_passbuilder_options_set_inliner_threshold"

external dispose_passbuilder_options : llpassbuilder_options -> unit =
  "llvm_dispose_passbuilder_options"
