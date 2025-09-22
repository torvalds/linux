(*===-- llvm_target.ml - LLVM OCaml Interface ------------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

module Endian = struct
  type t =
  | Big
  | Little
end

module CodeGenOptLevel = struct
  type t =
  | None
  | Less
  | Default
  | Aggressive
end

module RelocMode = struct
  type t =
  | Default
  | Static
  | PIC
  | DynamicNoPIC
end

module CodeModel = struct
  type t =
  | Default
  | JITDefault
  | Small
  | Kernel
  | Medium
  | Large
end

module CodeGenFileType = struct
  type t =
  | AssemblyFile
  | ObjectFile
end

module GlobalISelAbortMode = struct
  type t =
  | Enable
  | Disable
  | DisableWithDiag
end

exception Error of string

let () = Callback.register_exception "Llvm_target.Error" (Error "")

module DataLayout = struct
  type t

  external of_string : string -> t = "llvm_datalayout_of_string"
  external as_string : t -> string = "llvm_datalayout_as_string"
  external byte_order : t -> Endian.t = "llvm_datalayout_byte_order"
  external pointer_size : t -> int = "llvm_datalayout_pointer_size"
  external intptr_type : Llvm.llcontext -> t -> Llvm.lltype
                       = "llvm_datalayout_intptr_type"
  external qualified_pointer_size : int -> t -> int
                                  = "llvm_datalayout_qualified_pointer_size"
  external qualified_intptr_type : Llvm.llcontext -> int -> t -> Llvm.lltype
                                 = "llvm_datalayout_qualified_intptr_type"
  external size_in_bits : Llvm.lltype -> t -> Int64.t
                        = "llvm_datalayout_size_in_bits"
  external store_size : Llvm.lltype -> t -> Int64.t
                      = "llvm_datalayout_store_size"
  external abi_size : Llvm.lltype -> t -> Int64.t
                    = "llvm_datalayout_abi_size"
  external abi_align : Llvm.lltype -> t -> int
                     = "llvm_datalayout_abi_align"
  external stack_align : Llvm.lltype -> t -> int
                       = "llvm_datalayout_stack_align"
  external preferred_align : Llvm.lltype -> t -> int
                           = "llvm_datalayout_preferred_align"
  external preferred_align_of_global : Llvm.llvalue -> t -> int
                                   = "llvm_datalayout_preferred_align_of_global"
  external element_at_offset : Llvm.lltype -> Int64.t -> t -> int
                             = "llvm_datalayout_element_at_offset"
  external offset_of_element : Llvm.lltype -> int -> t -> Int64.t
                             = "llvm_datalayout_offset_of_element"
end

module Target = struct
  type t

  external default_triple : unit -> string = "llvm_target_default_triple"
  external first : unit -> t option = "llvm_target_first"
  external succ : t -> t option = "llvm_target_succ"
  external by_name : string -> t option = "llvm_target_by_name"
  external by_triple : string -> t = "llvm_target_by_triple"
  external name : t -> string = "llvm_target_name"
  external description : t -> string = "llvm_target_description"
  external has_jit : t -> bool = "llvm_target_has_jit"
  external has_target_machine : t -> bool = "llvm_target_has_target_machine"
  external has_asm_backend : t -> bool = "llvm_target_has_asm_backend"

  let all () =
    let rec step elem lst =
      match elem with
      | Some target -> step (succ target) (target :: lst)
      | None        -> lst
    in
    step (first ()) []
end

module TargetMachine = struct
  type t

  external create : triple:string -> ?cpu:string -> ?features:string ->
                    ?level:CodeGenOptLevel.t -> ?reloc_mode:RelocMode.t ->
                    ?code_model:CodeModel.t -> Target.t -> t
                  = "llvm_create_targetmachine_bytecode"
                    "llvm_create_targetmachine_native"
  external target : t -> Target.t
                  = "llvm_targetmachine_target"
  external triple : t -> string
                  = "llvm_targetmachine_triple"
  external cpu : t -> string
               = "llvm_targetmachine_cpu"
  external features : t -> string
                    = "llvm_targetmachine_features"
  external data_layout : t -> DataLayout.t
                       = "llvm_targetmachine_data_layout"
  external set_verbose_asm : bool -> t -> unit
                           = "llvm_targetmachine_set_verbose_asm"
  external set_fast_isel : bool -> t -> unit
                           = "llvm_targetmachine_set_fast_isel"
  external set_global_isel : bool -> t -> unit
                           = "llvm_targetmachine_set_global_isel"
  external set_global_isel_abort : ?mode:GlobalISelAbortMode.t -> t -> unit
                                 = "llvm_targetmachine_set_global_isel_abort"
  external set_machine_outliner : bool -> t -> unit
                                = "llvm_targetmachine_set_machine_outliner"
  external emit_to_file : Llvm.llmodule -> CodeGenFileType.t -> string ->
                          t -> unit
                        = "llvm_targetmachine_emit_to_file"
  external emit_to_memory_buffer : Llvm.llmodule -> CodeGenFileType.t ->
                                   t -> Llvm.llmemorybuffer
                                 = "llvm_targetmachine_emit_to_memory_buffer"
end
