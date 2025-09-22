(*===-- llvm_passbuilder.mli - LLVM OCaml Interface ------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

type llpassbuilder_options

(** [run_passes m passes tm opts] runs a set of passes over a module. The
    format of the string [passes] is the same as opt's -passes argument for
    the new pass manager. Individual passes may be specified, separated by
    commas. Full pipelines may also be invoked. See [LLVMRunPasses]. *)
val run_passes
  : Llvm.llmodule
  -> string
  -> Llvm_target.TargetMachine.t
  -> llpassbuilder_options
  -> (unit, string) result

(** Creates a new set of options for a PassBuilder. See
    [llvm::LLVMPassBuilderOptions::LLVMPassBuilderOptions]. *)
val create_passbuilder_options : unit -> llpassbuilder_options

(** Toggles adding the VerifierPass for the PassBuilder. See
    [llvm::LLVMPassBuilderOptions::VerifyEach]. *)
val passbuilder_options_set_verify_each
  : llpassbuilder_options -> bool -> unit

(** Toggles debug logging. See [llvm::LLVMPassBuilderOptions::DebugLogging]. *)
val passbuilder_options_set_debug_logging
  : llpassbuilder_options -> bool -> unit

(** Tuning option to set loop interleaving on/off, set based on opt level.
    See [llvm::PipelineTuningOptions::LoopInterleaving]. *)
val passbuilder_options_set_loop_interleaving
  : llpassbuilder_options -> bool -> unit

(** Tuning option to enable/disable loop vectorization, set based on opt level.
    See [llvm::PipelineTuningOptions::LoopVectorization]. *)
val passbuilder_options_set_loop_vectorization
  : llpassbuilder_options -> bool -> unit

(** Tuning option to enable/disable slp loop vectorization, set based on opt
    level. See [llvm::PipelineTuningOptions::SLPVectorization]. *)
val passbuilder_options_set_slp_vectorization
  : llpassbuilder_options -> bool -> unit

(** Tuning option to enable/disable loop unrolling. Its default value is true.
    See [llvm::PipelineTuningOptions::LoopUnrolling]. *)
val passbuilder_options_set_loop_unrolling
  : llpassbuilder_options -> bool -> unit

(** Tuning option to forget all SCEV loops in LoopUnroll.
    See [llvm::PipelineTuningOptions::ForgetAllSCEVInLoopUnroll]. *)
val passbuilder_options_set_forget_all_scev_in_loop_unroll
  : llpassbuilder_options -> bool -> unit

(** Tuning option to cap the number of calls to retrive clobbering accesses in
    MemorySSA, in LICM. See [llvm::PipelineTuningOptions::LicmMssaOptCap]. *)
val passbuilder_options_set_licm_mssa_opt_cap
  : llpassbuilder_options -> int -> unit

(** Tuning option to disable promotion to scalars in LICM with MemorySSA, if
    the number of accesses is too large. See
    [llvm::PipelineTuningOptions::LicmMssaNoAccForPromotionCap]. *)
val passbuilder_options_set_licm_mssa_no_acc_for_promotion_cap
  : llpassbuilder_options -> int -> unit

(** Tuning option to enable/disable call graph profile. See
    [llvm::PipelineTuningOptions::CallGraphProfile]. *)
val passbuilder_options_set_call_graph_profile
  : llpassbuilder_options -> bool -> unit

(** Tuning option to enable/disable function merging. See
    [llvm::PipelineTuningOptions::MergeFunctions]. *)
val passbuilder_options_set_merge_functions
  : llpassbuilder_options -> bool -> unit

(** Tuning option to override the default inliner threshold. See
    [llvm::PipelineTuningOptions::InlinerThreshold]. *)
val passbuilder_options_set_inliner_threshold
  : llpassbuilder_options -> int -> unit

(** Disposes of the options. *)
val dispose_passbuilder_options : llpassbuilder_options -> unit
