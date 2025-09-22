(*===-- llvm/llvm.ml - LLVM OCaml Interface -------------------------------===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)


type llcontext
type llmodule
type llmetadata
type lltype
type llvalue
type lldbgrecord
type lluse
type llbasicblock
type llbuilder
type llattrkind
type llattribute
type llmemorybuffer
type llmdkind

exception FeatureDisabled of string

let () = Callback.register_exception "Llvm.FeatureDisabled" (FeatureDisabled "")

module TypeKind = struct
  type t =
  | Void
  | Half
  | Float
  | Double
  | X86fp80
  | Fp128
  | Ppc_fp128
  | Label
  | Integer
  | Function
  | Struct
  | Array
  | Pointer
  | Vector
  | Metadata
  | X86_mmx
  | Token
  | ScalableVector
  | BFloat
  | X86_amx
end

module Linkage = struct
  type t =
  | External
  | Available_externally
  | Link_once
  | Link_once_odr
  | Link_once_odr_auto_hide
  | Weak
  | Weak_odr
  | Appending
  | Internal
  | Private
  | Dllimport
  | Dllexport
  | External_weak
  | Ghost
  | Common
  | Linker_private
  | Linker_private_weak
end

module Visibility = struct
  type t =
  | Default
  | Hidden
  | Protected
end

module DLLStorageClass = struct
  type t =
  | Default
  | DLLImport
  | DLLExport
end

module CallConv = struct
  let c = 0
  let fast = 8
  let cold = 9
  let x86_stdcall = 64
  let x86_fastcall = 65
end

module AttrRepr = struct
  type t =
  | Enum of llattrkind * int64
  | String of string * string
end

module AttrIndex = struct
  type t =
  | Function
  | Return
  | Param of int

  let to_int index =
    match index with
    | Function -> -1
    | Return -> 0
    | Param(n) -> 1 + n
end

module Attribute = struct
  type t =
  | Zext
  | Sext
  | Noreturn
  | Inreg
  | Structret
  | Nounwind
  | Noalias
  | Byval
  | Nest
  | Readnone
  | Readonly
  | Noinline
  | Alwaysinline
  | Optsize
  | Ssp
  | Sspreq
  | Alignment of int
  | Nocapture
  | Noredzone
  | Noimplicitfloat
  | Naked
  | Inlinehint
  | Stackalignment of int
  | ReturnsTwice
  | UWTable
  | NonLazyBind
end

module Icmp = struct
  type t =
  | Eq
  | Ne
  | Ugt
  | Uge
  | Ult
  | Ule
  | Sgt
  | Sge
  | Slt
  | Sle
end

module Fcmp = struct
  type t =
  | False
  | Oeq
  | Ogt
  | Oge
  | Olt
  | Ole
  | One
  | Ord
  | Uno
  | Ueq
  | Ugt
  | Uge
  | Ult
  | Ule
  | Une
  | True
end

module Opcode  = struct
  type t =
  | Invalid (* not an instruction *)
  (* Terminator Instructions *)
  | Ret
  | Br
  | Switch
  | IndirectBr
  | Invoke
  | Invalid2
  | Unreachable
  (* Standard Binary Operators *)
  | Add
  | FAdd
  | Sub
  | FSub
  | Mul
  | FMul
  | UDiv
  | SDiv
  | FDiv
  | URem
  | SRem
  | FRem
  (* Logical Operators *)
  | Shl
  | LShr
  | AShr
  | And
  | Or
  | Xor
  (* Memory Operators *)
  | Alloca
  | Load
  | Store
  | GetElementPtr
  (* Cast Operators *)
  | Trunc
  | ZExt
  | SExt
  | FPToUI
  | FPToSI
  | UIToFP
  | SIToFP
  | FPTrunc
  | FPExt
  | PtrToInt
  | IntToPtr
  | BitCast
  (* Other Operators *)
  | ICmp
  | FCmp
  | PHI
  | Call
  | Select
  | UserOp1
  | UserOp2
  | VAArg
  | ExtractElement
  | InsertElement
  | ShuffleVector
  | ExtractValue
  | InsertValue
  | Fence
  | AtomicCmpXchg
  | AtomicRMW
  | Resume
  | LandingPad
  | AddrSpaceCast
  | CleanupRet
  | CatchRet
  | CatchPad
  | CleanupPad
  | CatchSwitch
  | FNeg
  | CallBr
  | Freeze
end

module LandingPadClauseTy = struct
  type t =
  | Catch
  | Filter
end

module ThreadLocalMode = struct
  type t =
  | None
  | GeneralDynamic
  | LocalDynamic
  | InitialExec
  | LocalExec
end

module AtomicOrdering = struct
  type t =
  | NotAtomic
  | Unordered
  | Monotonic
  | Invalid
  | Acquire
  | Release
  | AcqiureRelease
  | SequentiallyConsistent
end

module AtomicRMWBinOp = struct
  type t =
  | Xchg
  | Add
  | Sub
  | And
  | Nand
  | Or
  | Xor
  | Max
  | Min
  | UMax
  | UMin
  | FAdd
  | FSub
end

module ValueKind = struct
  type t =
  | NullValue
  | Argument
  | BasicBlock
  | InlineAsm
  | MDNode
  | MDString
  | BlockAddress
  | ConstantAggregateZero
  | ConstantArray
  | ConstantDataArray
  | ConstantDataVector
  | ConstantExpr
  | ConstantFP
  | ConstantInt
  | ConstantPointerNull
  | ConstantStruct
  | ConstantVector
  | Function
  | GlobalAlias
  | GlobalIFunc
  | GlobalVariable
  | UndefValue
  | PoisonValue
  | Instruction of Opcode.t
end

module DiagnosticSeverity = struct
  type t =
  | Error
  | Warning
  | Remark
  | Note
end

module ModuleFlagBehavior = struct
  type t =
  | Error
  | Warning
  | Require
  | Override
  | Append
  | AppendUnique
end

exception IoError of string

let () = Callback.register_exception "Llvm.IoError" (IoError "")

external install_fatal_error_handler : (string -> unit) -> unit
                                     = "llvm_install_fatal_error_handler"
external reset_fatal_error_handler : unit -> unit
                                   = "llvm_reset_fatal_error_handler"
external enable_pretty_stacktrace : unit -> unit
                                  = "llvm_enable_pretty_stacktrace"
external parse_command_line_options : ?overview:string -> string array -> unit
                                    = "llvm_parse_command_line_options"

type ('a, 'b) llpos =
| At_end of 'a
| Before of 'b

type ('a, 'b) llrev_pos =
| At_start of 'a
| After of 'b


(*===-- Context error handling --------------------------------------------===*)
module Diagnostic = struct
  type t

  external description : t -> string = "llvm_get_diagnostic_description"
  external severity : t -> DiagnosticSeverity.t
                    = "llvm_get_diagnostic_severity"
end

external set_diagnostic_handler
  : llcontext -> (Diagnostic.t -> unit) option -> unit
  = "llvm_set_diagnostic_handler"

(*===-- Contexts ----------------------------------------------------------===*)
external create_context : unit -> llcontext = "llvm_create_context"
external dispose_context : llcontext -> unit = "llvm_dispose_context"
external global_context : unit -> llcontext = "llvm_global_context"
external mdkind_id : llcontext -> string -> llmdkind = "llvm_mdkind_id"

(*===-- Attributes --------------------------------------------------------===*)
exception UnknownAttribute of string

let () = Callback.register_exception "Llvm.UnknownAttribute"
                                     (UnknownAttribute "")

external enum_attr_kind : string -> llattrkind = "llvm_enum_attr_kind"
external llvm_create_enum_attr : llcontext -> llattrkind -> int64 ->
                                 llattribute
                               = "llvm_create_enum_attr_by_kind"
external is_enum_attr : llattribute -> bool = "llvm_is_enum_attr"
external get_enum_attr_kind : llattribute -> llattrkind
                            = "llvm_get_enum_attr_kind"
external get_enum_attr_value : llattribute -> int64
                             = "llvm_get_enum_attr_value"
external llvm_create_string_attr : llcontext -> string -> string ->
                                   llattribute
                                 = "llvm_create_string_attr"
external is_string_attr : llattribute -> bool = "llvm_is_string_attr"
external get_string_attr_kind : llattribute -> string
                              = "llvm_get_string_attr_kind"
external get_string_attr_value : llattribute -> string
                               = "llvm_get_string_attr_value"

let create_enum_attr context name value =
  llvm_create_enum_attr context (enum_attr_kind name) value
let create_string_attr context kind value =
  llvm_create_string_attr context kind value

let attr_of_repr context repr =
  match repr with
  | AttrRepr.Enum(kind, value) -> llvm_create_enum_attr context kind value
  | AttrRepr.String(key, value) -> llvm_create_string_attr context key value

let repr_of_attr attr =
  if is_enum_attr attr then
    AttrRepr.Enum(get_enum_attr_kind attr, get_enum_attr_value attr)
  else if is_string_attr attr then
    AttrRepr.String(get_string_attr_kind attr, get_string_attr_value attr)
  else assert false

(*===-- Modules -----------------------------------------------------------===*)
external create_module : llcontext -> string -> llmodule = "llvm_create_module"
external dispose_module : llmodule -> unit = "llvm_dispose_module"
external target_triple: llmodule -> string
                      = "llvm_target_triple"
external set_target_triple: string -> llmodule -> unit
                          = "llvm_set_target_triple"
external data_layout: llmodule -> string
                    = "llvm_data_layout"
external set_data_layout: string -> llmodule -> unit
                        = "llvm_set_data_layout"
external dump_module : llmodule -> unit = "llvm_dump_module"
external print_module : string -> llmodule -> unit = "llvm_print_module"
external string_of_llmodule : llmodule -> string = "llvm_string_of_llmodule"
external set_module_inline_asm : llmodule -> string -> unit
                               = "llvm_set_module_inline_asm"
external module_context : llmodule -> llcontext = "llvm_get_module_context"

external get_module_identifier : llmodule -> string
                               = "llvm_get_module_identifier"

external set_module_identifer : llmodule -> string -> unit
                              = "llvm_set_module_identifier"

external get_module_flag : llmodule -> string -> llmetadata option
                         = "llvm_get_module_flag"
external add_module_flag : llmodule -> ModuleFlagBehavior.t ->
            string -> llmetadata -> unit = "llvm_add_module_flag"

(*===-- Types -------------------------------------------------------------===*)
external classify_type : lltype -> TypeKind.t = "llvm_classify_type"
external type_context : lltype -> llcontext = "llvm_type_context"
external type_is_sized : lltype -> bool = "llvm_type_is_sized"
external dump_type : lltype -> unit = "llvm_dump_type"
external string_of_lltype : lltype -> string = "llvm_string_of_lltype"

(*--... Operations on integer types ........................................--*)
external i1_type : llcontext -> lltype = "llvm_i1_type"
external i8_type : llcontext -> lltype = "llvm_i8_type"
external i16_type : llcontext -> lltype = "llvm_i16_type"
external i32_type : llcontext -> lltype = "llvm_i32_type"
external i64_type : llcontext -> lltype = "llvm_i64_type"

external integer_type : llcontext -> int -> lltype = "llvm_integer_type"
external integer_bitwidth : lltype -> int = "llvm_integer_bitwidth"

(*--... Operations on real types ...........................................--*)
external float_type : llcontext -> lltype = "llvm_float_type"
external double_type : llcontext -> lltype = "llvm_double_type"
external x86fp80_type : llcontext -> lltype = "llvm_x86fp80_type"
external fp128_type : llcontext -> lltype = "llvm_fp128_type"
external ppc_fp128_type : llcontext -> lltype = "llvm_ppc_fp128_type"

(*--... Operations on function types .......................................--*)
external function_type : lltype -> lltype array -> lltype = "llvm_function_type"
external var_arg_function_type : lltype -> lltype array -> lltype
                               = "llvm_var_arg_function_type"
external is_var_arg : lltype -> bool = "llvm_is_var_arg"
external return_type : lltype -> lltype = "llvm_return_type"
external param_types : lltype -> lltype array = "llvm_param_types"

(*--... Operations on struct types .........................................--*)
external struct_type : llcontext -> lltype array -> lltype = "llvm_struct_type"
external packed_struct_type : llcontext -> lltype array -> lltype
                            = "llvm_packed_struct_type"
external struct_name : lltype -> string option = "llvm_struct_name"
external named_struct_type : llcontext -> string -> lltype =
    "llvm_named_struct_type"
external struct_set_body : lltype -> lltype array -> bool -> unit =
    "llvm_struct_set_body"
external struct_element_types : lltype -> lltype array
                              = "llvm_struct_element_types"
external is_packed : lltype -> bool = "llvm_is_packed"
external is_opaque : lltype -> bool = "llvm_is_opaque"
external is_literal : lltype -> bool = "llvm_is_literal"

(*--... Operations on pointer, vector, and array types .....................--*)

external subtypes : lltype -> lltype array = "llvm_subtypes"
external array_type : lltype -> int -> lltype = "llvm_array_type"
external pointer_type : llcontext -> lltype = "llvm_pointer_type"
external qualified_pointer_type : llcontext -> int -> lltype
                                = "llvm_qualified_pointer_type"
external vector_type : lltype -> int -> lltype = "llvm_vector_type"

external element_type : lltype -> lltype = "llvm_get_element_type"
external array_length : lltype -> int = "llvm_array_length"
external address_space : lltype -> int = "llvm_address_space"
external vector_size : lltype -> int = "llvm_vector_size"

(*--... Operations on other types ..........................................--*)
external void_type : llcontext -> lltype = "llvm_void_type"
external label_type : llcontext -> lltype = "llvm_label_type"
external x86_mmx_type : llcontext -> lltype = "llvm_x86_mmx_type"
external type_by_name : llmodule -> string -> lltype option = "llvm_type_by_name"

external classify_value : llvalue -> ValueKind.t = "llvm_classify_value"
(*===-- Values ------------------------------------------------------------===*)
external type_of : llvalue -> lltype = "llvm_type_of"
external value_name : llvalue -> string = "llvm_value_name"
external set_value_name : string -> llvalue -> unit = "llvm_set_value_name"
external dump_value : llvalue -> unit = "llvm_dump_value"
external string_of_llvalue : llvalue -> string = "llvm_string_of_llvalue"
external string_of_lldbgrecord : lldbgrecord -> string = "llvm_string_of_lldbgrecord"
external replace_all_uses_with : llvalue -> llvalue -> unit
                               = "llvm_replace_all_uses_with"

(*--... Operations on uses .................................................--*)
external use_begin : llvalue -> lluse option = "llvm_use_begin"
external use_succ : lluse -> lluse option = "llvm_use_succ"
external user : lluse -> llvalue = "llvm_user"
external used_value : lluse -> llvalue = "llvm_used_value"

let iter_uses f v =
  let rec aux = function
    | None -> ()
    | Some u ->
        f u;
        aux (use_succ u)
  in
  aux (use_begin v)

let fold_left_uses f init v =
  let rec aux init u =
    match u with
    | None -> init
    | Some u -> aux (f init u) (use_succ u)
  in
  aux init (use_begin v)

let fold_right_uses f v init =
  let rec aux u init =
    match u with
    | None -> init
    | Some u -> f u (aux (use_succ u) init)
  in
  aux (use_begin v) init


(*--... Operations on users ................................................--*)
external operand : llvalue -> int -> llvalue = "llvm_operand"
external operand_use : llvalue -> int -> lluse = "llvm_operand_use"
external set_operand : llvalue -> int -> llvalue -> unit = "llvm_set_operand"
external num_operands : llvalue -> int = "llvm_num_operands"
external indices : llvalue -> int array = "llvm_indices"

(*--... Operations on constants of (mostly) any type .......................--*)
external is_constant : llvalue -> bool = "llvm_is_constant"
external const_null : lltype -> llvalue = "llvm_const_null"
external const_all_ones : (*int|vec*)lltype -> llvalue = "llvm_const_all_ones"
external const_pointer_null : lltype -> llvalue = "llvm_const_pointer_null"
external undef : lltype -> llvalue = "llvm_get_undef"
external poison : lltype -> llvalue = "llvm_get_poison"
external is_null : llvalue -> bool = "llvm_is_null"
external is_undef : llvalue -> bool = "llvm_is_undef"
external is_poison : llvalue -> bool = "llvm_is_poison"
external constexpr_opcode : llvalue -> Opcode.t = "llvm_constexpr_get_opcode"

(*--... Operations on instructions .........................................--*)
external has_metadata : llvalue -> bool = "llvm_has_metadata"
external metadata : llvalue -> llmdkind -> llvalue option = "llvm_metadata"
external set_metadata : llvalue -> llmdkind -> llvalue -> unit = "llvm_set_metadata"
external clear_metadata : llvalue -> llmdkind -> unit = "llvm_clear_metadata"

(*--... Operations on metadata .......,.....................................--*)
external mdstring : llcontext -> string -> llvalue = "llvm_mdstring"
external mdnode : llcontext -> llvalue array -> llvalue = "llvm_mdnode"
external mdnull : llcontext -> llvalue = "llvm_mdnull"
external get_mdstring : llvalue -> string option = "llvm_get_mdstring"
external get_mdnode_operands : llvalue -> llvalue array
                            = "llvm_get_mdnode_operands"
external get_named_metadata : llmodule -> string -> llvalue array
                            = "llvm_get_namedmd"
external add_named_metadata_operand : llmodule -> string -> llvalue -> unit
                                    = "llvm_append_namedmd"
external value_as_metadata : llvalue -> llmetadata = "llvm_value_as_metadata"
external metadata_as_value : llcontext -> llmetadata -> llvalue
                        = "llvm_metadata_as_value"

(*--... Operations on scalar constants .....................................--*)
external const_int : lltype -> int -> llvalue = "llvm_const_int"
external const_of_int64 : lltype -> Int64.t -> bool -> llvalue
                        = "llvm_const_of_int64"
external int64_of_const : llvalue -> Int64.t option
                        = "llvm_int64_of_const"
external const_int_of_string : lltype -> string -> int -> llvalue
                             = "llvm_const_int_of_string"
external const_float : lltype -> float -> llvalue = "llvm_const_float"
external float_of_const : llvalue -> float option
                        = "llvm_float_of_const"
external const_float_of_string : lltype -> string -> llvalue
                               = "llvm_const_float_of_string"

(*--... Operations on composite constants ..................................--*)
external const_string : llcontext -> string -> llvalue = "llvm_const_string"
external const_stringz : llcontext -> string -> llvalue = "llvm_const_stringz"
external const_array : lltype -> llvalue array -> llvalue = "llvm_const_array"
external const_struct : llcontext -> llvalue array -> llvalue
                      = "llvm_const_struct"
external const_named_struct : lltype -> llvalue array -> llvalue
                      = "llvm_const_named_struct"
external const_packed_struct : llcontext -> llvalue array -> llvalue
                             = "llvm_const_packed_struct"
external const_vector : llvalue array -> llvalue = "llvm_const_vector"
external string_of_const : llvalue -> string option = "llvm_string_of_const"
external aggregate_element : llvalue -> int -> llvalue option
                           = "llvm_aggregate_element"

(*--... Constant expressions ...............................................--*)
external align_of : lltype -> llvalue = "llvm_align_of"
external size_of : lltype -> llvalue = "llvm_size_of"
external const_neg : llvalue -> llvalue = "llvm_const_neg"
external const_nsw_neg : llvalue -> llvalue = "llvm_const_nsw_neg"
external const_nuw_neg : llvalue -> llvalue = "llvm_const_nuw_neg"
external const_not : llvalue -> llvalue = "llvm_const_not"
external const_add : llvalue -> llvalue -> llvalue = "llvm_const_add"
external const_nsw_add : llvalue -> llvalue -> llvalue = "llvm_const_nsw_add"
external const_nuw_add : llvalue -> llvalue -> llvalue = "llvm_const_nuw_add"
external const_sub : llvalue -> llvalue -> llvalue = "llvm_const_sub"
external const_nsw_sub : llvalue -> llvalue -> llvalue = "llvm_const_nsw_sub"
external const_nuw_sub : llvalue -> llvalue -> llvalue = "llvm_const_nuw_sub"
external const_mul : llvalue -> llvalue -> llvalue = "llvm_const_mul"
external const_nsw_mul : llvalue -> llvalue -> llvalue = "llvm_const_nsw_mul"
external const_nuw_mul : llvalue -> llvalue -> llvalue = "llvm_const_nuw_mul"
external const_xor : llvalue -> llvalue -> llvalue = "llvm_const_xor"
external const_gep : lltype -> llvalue -> llvalue array -> llvalue
                   = "llvm_const_gep"
external const_in_bounds_gep : lltype -> llvalue -> llvalue array -> llvalue
                             = "llvm_const_in_bounds_gep"
external const_trunc : llvalue -> lltype -> llvalue = "llvm_const_trunc"
external const_ptrtoint : llvalue -> lltype -> llvalue = "llvm_const_ptrtoint"
external const_inttoptr : llvalue -> lltype -> llvalue = "llvm_const_inttoptr"
external const_bitcast : llvalue -> lltype -> llvalue = "llvm_const_bitcast"
external const_trunc_or_bitcast : llvalue -> lltype -> llvalue
                                = "llvm_const_trunc_or_bitcast"
external const_pointercast : llvalue -> lltype -> llvalue
                           = "llvm_const_pointercast"
external const_extractelement : llvalue -> llvalue -> llvalue
                              = "llvm_const_extractelement"
external const_insertelement : llvalue -> llvalue -> llvalue -> llvalue
                             = "llvm_const_insertelement"
external const_shufflevector : llvalue -> llvalue -> llvalue -> llvalue
                             = "llvm_const_shufflevector"
external const_inline_asm : lltype -> string -> string -> bool -> bool ->
                            llvalue
                          = "llvm_const_inline_asm"
external block_address : llvalue -> llbasicblock -> llvalue
                       = "llvm_blockaddress"

(*--... Operations on global variables, functions, and aliases (globals) ...--*)
external global_parent : llvalue -> llmodule = "llvm_global_parent"
external is_declaration : llvalue -> bool = "llvm_is_declaration"
external linkage : llvalue -> Linkage.t = "llvm_linkage"
external set_linkage : Linkage.t -> llvalue -> unit = "llvm_set_linkage"
external unnamed_addr : llvalue -> bool = "llvm_unnamed_addr"
external set_unnamed_addr : bool -> llvalue -> unit = "llvm_set_unnamed_addr"
external section : llvalue -> string = "llvm_section"
external set_section : string -> llvalue -> unit = "llvm_set_section"
external visibility : llvalue -> Visibility.t = "llvm_visibility"
external set_visibility : Visibility.t -> llvalue -> unit = "llvm_set_visibility"
external dll_storage_class : llvalue -> DLLStorageClass.t = "llvm_dll_storage_class"
external set_dll_storage_class : DLLStorageClass.t -> llvalue -> unit = "llvm_set_dll_storage_class"
external alignment : llvalue -> int = "llvm_alignment"
external set_alignment : int -> llvalue -> unit = "llvm_set_alignment"
external global_copy_all_metadata : llvalue -> (llmdkind * llmetadata) array
                                  = "llvm_global_copy_all_metadata"
external is_global_constant : llvalue -> bool = "llvm_is_global_constant"
external set_global_constant : bool -> llvalue -> unit
                             = "llvm_set_global_constant"

(*--... Operations on global variables .....................................--*)
external declare_global : lltype -> string -> llmodule -> llvalue
                        = "llvm_declare_global"
external declare_qualified_global : lltype -> string -> int -> llmodule ->
                                    llvalue
                                  = "llvm_declare_qualified_global"
external define_global : string -> llvalue -> llmodule -> llvalue
                       = "llvm_define_global"
external define_qualified_global : string -> llvalue -> int -> llmodule ->
                                   llvalue
                                 = "llvm_define_qualified_global"
external lookup_global : string -> llmodule -> llvalue option
                       = "llvm_lookup_global"
external delete_global : llvalue -> unit = "llvm_delete_global"
external global_initializer : llvalue -> llvalue option = "llvm_global_initializer"
external set_initializer : llvalue -> llvalue -> unit = "llvm_set_initializer"
external remove_initializer : llvalue -> unit = "llvm_remove_initializer"
external is_thread_local : llvalue -> bool = "llvm_is_thread_local"
external set_thread_local : bool -> llvalue -> unit = "llvm_set_thread_local"
external thread_local_mode : llvalue -> ThreadLocalMode.t
                           = "llvm_thread_local_mode"
external set_thread_local_mode : ThreadLocalMode.t -> llvalue -> unit
                               = "llvm_set_thread_local_mode"
external is_externally_initialized : llvalue -> bool
                                   = "llvm_is_externally_initialized"
external set_externally_initialized : bool -> llvalue -> unit
                                    = "llvm_set_externally_initialized"
external global_begin : llmodule -> (llmodule, llvalue) llpos
                      = "llvm_global_begin"
external global_succ : llvalue -> (llmodule, llvalue) llpos
                     = "llvm_global_succ"
external global_end : llmodule -> (llmodule, llvalue) llrev_pos
                    = "llvm_global_end"
external global_pred : llvalue -> (llmodule, llvalue) llrev_pos
                     = "llvm_global_pred"

let rec iter_global_range f i e =
  if i = e then () else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid global variable range.")
  | Before bb ->
      f bb;
      iter_global_range f (global_succ bb) e

let iter_globals f m =
  iter_global_range f (global_begin m) (At_end m)

let rec fold_left_global_range f init i e =
  if i = e then init else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid global variable range.")
  | Before bb -> fold_left_global_range f (f init bb) (global_succ bb) e

let fold_left_globals f init m =
  fold_left_global_range f init (global_begin m) (At_end m)

let rec rev_iter_global_range f i e =
  if i = e then () else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid global variable range.")
  | After bb ->
      f bb;
      rev_iter_global_range f (global_pred bb) e

let rev_iter_globals f m =
  rev_iter_global_range f (global_end m) (At_start m)

let rec fold_right_global_range f i e init =
  if i = e then init else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid global variable range.")
  | After bb -> fold_right_global_range f (global_pred bb) e (f bb init)

let fold_right_globals f m init =
  fold_right_global_range f (global_end m) (At_start m) init

(*--... Operations on aliases ..............................................--*)
external add_alias : llmodule -> lltype -> int -> llvalue -> string -> llvalue
                   = "llvm_add_alias"

(*--... Operations on functions ............................................--*)
external declare_function : string -> lltype -> llmodule -> llvalue
                          = "llvm_declare_function"
external define_function : string -> lltype -> llmodule -> llvalue
                         = "llvm_define_function"
external lookup_function : string -> llmodule -> llvalue option
                         = "llvm_lookup_function"
external delete_function : llvalue -> unit = "llvm_delete_function"
external is_intrinsic : llvalue -> bool = "llvm_is_intrinsic"
external function_call_conv : llvalue -> int = "llvm_function_call_conv"
external set_function_call_conv : int -> llvalue -> unit
                                = "llvm_set_function_call_conv"
external gc : llvalue -> string option = "llvm_gc"
external set_gc : string option -> llvalue -> unit = "llvm_set_gc"
external function_begin : llmodule -> (llmodule, llvalue) llpos
                        = "llvm_function_begin"
external function_succ : llvalue -> (llmodule, llvalue) llpos
                       = "llvm_function_succ"
external function_end : llmodule -> (llmodule, llvalue) llrev_pos
                      = "llvm_function_end"
external function_pred : llvalue -> (llmodule, llvalue) llrev_pos
                       = "llvm_function_pred"

let rec iter_function_range f i e =
  if i = e then () else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid function range.")
  | Before fn ->
      f fn;
      iter_function_range f (function_succ fn) e

let iter_functions f m =
  iter_function_range f (function_begin m) (At_end m)

let rec fold_left_function_range f init i e =
  if i = e then init else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid function range.")
  | Before fn -> fold_left_function_range f (f init fn) (function_succ fn) e

let fold_left_functions f init m =
  fold_left_function_range f init (function_begin m) (At_end m)

let rec rev_iter_function_range f i e =
  if i = e then () else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid function range.")
  | After fn ->
      f fn;
      rev_iter_function_range f (function_pred fn) e

let rev_iter_functions f m =
  rev_iter_function_range f (function_end m) (At_start m)

let rec fold_right_function_range f i e init =
  if i = e then init else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid function range.")
  | After fn -> fold_right_function_range f (function_pred fn) e (f fn init)

let fold_right_functions f m init =
  fold_right_function_range f (function_end m) (At_start m) init

external llvm_add_function_attr : llvalue -> llattribute -> int -> unit
                                = "llvm_add_function_attr"
external llvm_function_attrs : llvalue -> int -> llattribute array
                             = "llvm_function_attrs"
external llvm_remove_enum_function_attr : llvalue -> llattrkind -> int -> unit
                                        = "llvm_remove_enum_function_attr"
external llvm_remove_string_function_attr : llvalue -> string -> int -> unit
                                          = "llvm_remove_string_function_attr"

let add_function_attr f a i =
  llvm_add_function_attr f a (AttrIndex.to_int i)
let function_attrs f i =
  llvm_function_attrs f (AttrIndex.to_int i)
let remove_enum_function_attr f k i =
  llvm_remove_enum_function_attr f k (AttrIndex.to_int i)
let remove_string_function_attr f k i =
  llvm_remove_string_function_attr f k (AttrIndex.to_int i)

(*--... Operations on params ...............................................--*)
external params : llvalue -> llvalue array = "llvm_params"
external param : llvalue -> int -> llvalue = "llvm_param"
external param_parent : llvalue -> llvalue = "llvm_param_parent"
external param_begin : llvalue -> (llvalue, llvalue) llpos = "llvm_param_begin"
external param_succ : llvalue -> (llvalue, llvalue) llpos = "llvm_param_succ"
external param_end : llvalue -> (llvalue, llvalue) llrev_pos = "llvm_param_end"
external param_pred : llvalue -> (llvalue, llvalue) llrev_pos ="llvm_param_pred"

let rec iter_param_range f i e =
  if i = e then () else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid parameter range.")
  | Before p ->
      f p;
      iter_param_range f (param_succ p) e

let iter_params f fn =
  iter_param_range f (param_begin fn) (At_end fn)

let rec fold_left_param_range f init i e =
  if i = e then init else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid parameter range.")
  | Before p -> fold_left_param_range f (f init p) (param_succ p) e

let fold_left_params f init fn =
  fold_left_param_range f init (param_begin fn) (At_end fn)

let rec rev_iter_param_range f i e =
  if i = e then () else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid parameter range.")
  | After p ->
      f p;
      rev_iter_param_range f (param_pred p) e

let rev_iter_params f fn =
  rev_iter_param_range f (param_end fn) (At_start fn)

let rec fold_right_param_range f init i e =
  if i = e then init else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid parameter range.")
  | After p -> fold_right_param_range f (f p init) (param_pred p) e

let fold_right_params f fn init =
  fold_right_param_range f init (param_end fn) (At_start fn)

(*--... Operations on basic blocks .........................................--*)
external value_of_block : llbasicblock -> llvalue = "llvm_value_of_block"
external value_is_block : llvalue -> bool = "llvm_value_is_block"
external block_of_value : llvalue -> llbasicblock = "llvm_block_of_value"
external block_parent : llbasicblock -> llvalue = "llvm_block_parent"
external basic_blocks : llvalue -> llbasicblock array = "llvm_basic_blocks"
external entry_block : llvalue -> llbasicblock = "llvm_entry_block"
external delete_block : llbasicblock -> unit = "llvm_delete_block"
external remove_block : llbasicblock -> unit = "llvm_remove_block"
external move_block_before : llbasicblock -> llbasicblock -> unit
                           = "llvm_move_block_before"
external move_block_after : llbasicblock -> llbasicblock -> unit
                          = "llvm_move_block_after"
external append_block : llcontext -> string -> llvalue -> llbasicblock
                      = "llvm_append_block"
external insert_block : llcontext -> string -> llbasicblock -> llbasicblock
                      = "llvm_insert_block"
external block_begin : llvalue -> (llvalue, llbasicblock) llpos
                     = "llvm_block_begin"
external block_succ : llbasicblock -> (llvalue, llbasicblock) llpos
                    = "llvm_block_succ"
external block_end : llvalue -> (llvalue, llbasicblock) llrev_pos
                   = "llvm_block_end"
external block_pred : llbasicblock -> (llvalue, llbasicblock) llrev_pos
                    = "llvm_block_pred"
external block_terminator : llbasicblock -> llvalue option =
    "llvm_block_terminator"

let rec iter_block_range f i e =
  if i = e then () else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid block range.")
  | Before bb ->
      f bb;
      iter_block_range f (block_succ bb) e

let iter_blocks f fn =
  iter_block_range f (block_begin fn) (At_end fn)

let rec fold_left_block_range f init i e =
  if i = e then init else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid block range.")
  | Before bb -> fold_left_block_range f (f init bb) (block_succ bb) e

let fold_left_blocks f init fn =
  fold_left_block_range f init (block_begin fn) (At_end fn)

let rec rev_iter_block_range f i e =
  if i = e then () else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid block range.")
  | After bb ->
      f bb;
      rev_iter_block_range f (block_pred bb) e

let rev_iter_blocks f fn =
  rev_iter_block_range f (block_end fn) (At_start fn)

let rec fold_right_block_range f init i e =
  if i = e then init else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid block range.")
  | After bb -> fold_right_block_range f (f bb init) (block_pred bb) e

let fold_right_blocks f fn init =
  fold_right_block_range f init (block_end fn) (At_start fn)

(*--... Operations on instructions .........................................--*)
external instr_parent : llvalue -> llbasicblock = "llvm_instr_parent"
external instr_begin : llbasicblock -> (llbasicblock, llvalue) llpos
                     = "llvm_instr_begin"
external instr_succ : llvalue -> (llbasicblock, llvalue) llpos
                     = "llvm_instr_succ"
external instr_end : llbasicblock -> (llbasicblock, llvalue) llrev_pos
                     = "llvm_instr_end"
external instr_pred : llvalue -> (llbasicblock, llvalue) llrev_pos
                     = "llvm_instr_pred"

external instr_opcode : llvalue -> Opcode.t = "llvm_instr_get_opcode"
external icmp_predicate : llvalue -> Icmp.t option = "llvm_instr_icmp_predicate"
external fcmp_predicate : llvalue -> Fcmp.t option = "llvm_instr_fcmp_predicate"
external instr_clone : llvalue -> llvalue = "llvm_instr_clone"

let rec iter_instrs_range f i e =
  if i = e then () else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid instruction range.")
  | Before i ->
      f i;
      iter_instrs_range f (instr_succ i) e

let iter_instrs f bb =
  iter_instrs_range f (instr_begin bb) (At_end bb)

let rec fold_left_instrs_range f init i e =
  if i = e then init else
  match i with
  | At_end _ -> raise (Invalid_argument "Invalid instruction range.")
  | Before i -> fold_left_instrs_range f (f init i) (instr_succ i) e

let fold_left_instrs f init bb =
  fold_left_instrs_range f init (instr_begin bb) (At_end bb)

let rec rev_iter_instrs_range f i e =
  if i = e then () else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid instruction range.")
  | After i ->
      f i;
      rev_iter_instrs_range f (instr_pred i) e

let rev_iter_instrs f bb =
  rev_iter_instrs_range f (instr_end bb) (At_start bb)

let rec fold_right_instr_range f i e init =
  if i = e then init else
  match i with
  | At_start _ -> raise (Invalid_argument "Invalid instruction range.")
  | After i -> fold_right_instr_range f (instr_pred i) e (f i init)

let fold_right_instrs f bb init =
  fold_right_instr_range f (instr_end bb) (At_start bb) init


(*--... Operations on call sites ...........................................--*)
external instruction_call_conv: llvalue -> int
                              = "llvm_instruction_call_conv"
external set_instruction_call_conv: int -> llvalue -> unit
                                  = "llvm_set_instruction_call_conv"

external llvm_add_call_site_attr : llvalue -> llattribute -> int -> unit
                                = "llvm_add_call_site_attr"
external llvm_call_site_attrs : llvalue -> int -> llattribute array
                             = "llvm_call_site_attrs"
external llvm_remove_enum_call_site_attr : llvalue -> llattrkind -> int -> unit
                                        = "llvm_remove_enum_call_site_attr"
external llvm_remove_string_call_site_attr : llvalue -> string -> int -> unit
                                          = "llvm_remove_string_call_site_attr"

let add_call_site_attr f a i =
  llvm_add_call_site_attr f a (AttrIndex.to_int i)
let call_site_attrs f i =
  llvm_call_site_attrs f (AttrIndex.to_int i)
let remove_enum_call_site_attr f k i =
  llvm_remove_enum_call_site_attr f k (AttrIndex.to_int i)
let remove_string_call_site_attr f k i =
  llvm_remove_string_call_site_attr f k (AttrIndex.to_int i)

(*--... Operations on call and invoke instructions (only) ..................--*)
external num_arg_operands : llvalue -> int = "llvm_num_arg_operands"
external is_tail_call : llvalue -> bool = "llvm_is_tail_call"
external set_tail_call : bool -> llvalue -> unit = "llvm_set_tail_call"
external get_normal_dest : llvalue -> llbasicblock = "llvm_get_normal_dest"
external get_unwind_dest : llvalue -> llbasicblock = "llvm_get_unwind_dest"

(*--... Operations on load/store instructions (only) .......................--*)
external is_volatile : llvalue -> bool = "llvm_is_volatile"
external set_volatile : bool -> llvalue -> unit = "llvm_set_volatile"

(*--... Operations on terminators ..........................................--*)

let is_terminator llv =
  let open ValueKind in
  let open Opcode in
  match classify_value llv with
    | Instruction (Br | IndirectBr | Invoke | Resume | Ret | Switch | Unreachable)
      -> true
    | _ -> false

external successor : llvalue -> int -> llbasicblock = "llvm_successor"
external set_successor : llvalue -> int -> llbasicblock -> unit
                       = "llvm_set_successor"
external num_successors : llvalue -> int = "llvm_num_successors"

let successors llv =
  if not (is_terminator llv) then
    raise (Invalid_argument "Llvm.successors can only be used on terminators")
  else
    Array.init (num_successors llv) (successor llv)

let iter_successors f llv =
  if not (is_terminator llv) then
    raise (Invalid_argument "Llvm.iter_successors can only be used on terminators")
  else
    for i = 0 to num_successors llv - 1 do
      f (successor llv i)
    done

let fold_successors f llv z =
  if not (is_terminator llv) then
    raise (Invalid_argument "Llvm.fold_successors can only be used on terminators")
  else
    let n = num_successors llv in
    let rec aux i acc =
      if i >= n then acc
      else begin
        let llb = successor llv i in
        aux (i+1) (f llb acc)
      end
    in aux 0 z


(*--... Operations on branches .............................................--*)
external condition : llvalue -> llvalue = "llvm_condition"
external set_condition : llvalue -> llvalue -> unit
                       = "llvm_set_condition"
external is_conditional : llvalue -> bool = "llvm_is_conditional"

let get_branch llv =
  if classify_value llv <> ValueKind.Instruction Opcode.Br then
    None
  else if is_conditional llv then
    Some (`Conditional (condition llv, successor llv 0, successor llv 1))
  else
    Some (`Unconditional (successor llv 0))

(*--... Operations on phi nodes ............................................--*)
external add_incoming : (llvalue * llbasicblock) -> llvalue -> unit
                      = "llvm_add_incoming"
external incoming : llvalue -> (llvalue * llbasicblock) list = "llvm_incoming"

external delete_instruction : llvalue -> unit = "llvm_delete_instruction"

(*===-- Instruction builders ----------------------------------------------===*)
external builder : llcontext -> llbuilder = "llvm_builder"
external position_builder : (llbasicblock, llvalue) llpos -> llbuilder -> unit
                          = "llvm_position_builder"
external position_builder_before_dbg_records : (llbasicblock, llvalue) llpos ->
                                               llbuilder -> unit
                                  = "llvm_position_builder_before_dbg_records"
external insertion_block : llbuilder -> llbasicblock = "llvm_insertion_block"
external insert_into_builder : llvalue -> string -> llbuilder -> unit
                             = "llvm_insert_into_builder"

let builder_at context ip =
  let b = builder context in
  position_builder ip b;
  b

let builder_before context i = builder_at context (Before i)
let builder_at_end context bb = builder_at context (At_end bb)

let position_before i = position_builder (Before i)
let position_before_dbg_records i =
  position_builder_before_dbg_records (Before i)
let position_at_end bb = position_builder (At_end bb)


(*--... Metadata ...........................................................--*)
external set_current_debug_location : llbuilder -> llvalue -> unit
                                    = "llvm_set_current_debug_location"
external clear_current_debug_location : llbuilder -> unit
                                      = "llvm_clear_current_debug_location"
external current_debug_location : llbuilder -> llvalue option
                                    = "llvm_current_debug_location"
external set_inst_debug_location : llbuilder -> llvalue -> unit
                                 = "llvm_set_inst_debug_location"


(*--... Terminators ........................................................--*)
external build_ret_void : llbuilder -> llvalue = "llvm_build_ret_void"
external build_ret : llvalue -> llbuilder -> llvalue = "llvm_build_ret"
external build_aggregate_ret : llvalue array -> llbuilder -> llvalue
                             = "llvm_build_aggregate_ret"
external build_br : llbasicblock -> llbuilder -> llvalue = "llvm_build_br"
external build_cond_br : llvalue -> llbasicblock -> llbasicblock -> llbuilder ->
                         llvalue = "llvm_build_cond_br"
external build_switch : llvalue -> llbasicblock -> int -> llbuilder -> llvalue
                      = "llvm_build_switch"
external build_malloc : lltype -> string -> llbuilder -> llvalue =
    "llvm_build_malloc"
external build_array_malloc : lltype -> llvalue -> string -> llbuilder ->
    llvalue = "llvm_build_array_malloc"
external build_free : llvalue -> llbuilder -> llvalue = "llvm_build_free"
external add_case : llvalue -> llvalue -> llbasicblock -> unit
                  = "llvm_add_case"
external switch_default_dest : llvalue -> llbasicblock =
    "llvm_switch_default_dest"
external build_indirect_br : llvalue -> int -> llbuilder -> llvalue
                           = "llvm_build_indirect_br"
external add_destination : llvalue -> llbasicblock -> unit
                         = "llvm_add_destination"
external build_invoke : lltype -> llvalue -> llvalue array -> llbasicblock ->
                        llbasicblock -> string -> llbuilder -> llvalue
                      = "llvm_build_invoke_bc" "llvm_build_invoke_nat"
external build_landingpad : lltype -> llvalue -> int -> string -> llbuilder ->
                            llvalue = "llvm_build_landingpad"
external is_cleanup : llvalue -> bool = "llvm_is_cleanup"
external set_cleanup : llvalue -> bool -> unit = "llvm_set_cleanup"
external add_clause : llvalue -> llvalue -> unit = "llvm_add_clause"
external build_resume : llvalue -> llbuilder -> llvalue = "llvm_build_resume"
external build_unreachable : llbuilder -> llvalue = "llvm_build_unreachable"

(*--... Arithmetic .........................................................--*)
external build_add : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_add"
external build_nsw_add : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nsw_add"
external build_nuw_add : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nuw_add"
external build_fadd : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_fadd"
external build_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_sub"
external build_nsw_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nsw_sub"
external build_nuw_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nuw_sub"
external build_fsub : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_fsub"
external build_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_mul"
external build_nsw_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nsw_mul"
external build_nuw_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nuw_mul"
external build_fmul : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_fmul"
external build_udiv : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_udiv"
external build_sdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_sdiv"
external build_exact_sdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue
                          = "llvm_build_exact_sdiv"
external build_fdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_fdiv"
external build_urem : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_urem"
external build_srem : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_srem"
external build_frem : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_frem"
external build_shl : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_shl"
external build_lshr : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_lshr"
external build_ashr : llvalue -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_ashr"
external build_and : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_and"
external build_or : llvalue -> llvalue -> string -> llbuilder -> llvalue
                  = "llvm_build_or"
external build_xor : llvalue -> llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_xor"
external build_neg : llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_neg"
external build_nsw_neg : llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nsw_neg"
external build_nuw_neg : llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_nuw_neg"
external build_fneg : llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_fneg"
external build_not : llvalue -> string -> llbuilder -> llvalue
                   = "llvm_build_not"

(*--... Memory .............................................................--*)
external build_alloca : lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_alloca"
external build_array_alloca : lltype -> llvalue -> string -> llbuilder ->
                              llvalue = "llvm_build_array_alloca"
external build_load : lltype -> llvalue -> string -> llbuilder -> llvalue
                    = "llvm_build_load"
external build_store : llvalue -> llvalue -> llbuilder -> llvalue
                     = "llvm_build_store"
external build_atomicrmw : AtomicRMWBinOp.t -> llvalue -> llvalue ->
                           AtomicOrdering.t -> bool -> string -> llbuilder ->
                           llvalue
                         = "llvm_build_atomicrmw_bytecode"
                           "llvm_build_atomicrmw_native"
external build_gep : lltype -> llvalue -> llvalue array -> string -> llbuilder
                   -> llvalue = "llvm_build_gep"
external build_in_bounds_gep : lltype -> llvalue -> llvalue array -> string ->
                             llbuilder -> llvalue = "llvm_build_in_bounds_gep"
external build_struct_gep : lltype -> llvalue -> int -> string -> llbuilder ->
                          llvalue = "llvm_build_struct_gep"

external build_global_string : string -> string -> llbuilder -> llvalue
                             = "llvm_build_global_string"
external build_global_stringptr  : string -> string -> llbuilder -> llvalue
                                 = "llvm_build_global_stringptr"

(*--... Casts ..............................................................--*)
external build_trunc : llvalue -> lltype -> string -> llbuilder -> llvalue
                     = "llvm_build_trunc"
external build_zext : llvalue -> lltype -> string -> llbuilder -> llvalue
                    = "llvm_build_zext"
external build_sext : llvalue -> lltype -> string -> llbuilder -> llvalue
                    = "llvm_build_sext"
external build_fptoui : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_fptoui"
external build_fptosi : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_fptosi"
external build_uitofp : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_uitofp"
external build_sitofp : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_sitofp"
external build_fptrunc : llvalue -> lltype -> string -> llbuilder -> llvalue
                       = "llvm_build_fptrunc"
external build_fpext : llvalue -> lltype -> string -> llbuilder -> llvalue
                     = "llvm_build_fpext"
external build_ptrtoint : llvalue -> lltype -> string -> llbuilder -> llvalue
                        = "llvm_build_prttoint"
external build_inttoptr : llvalue -> lltype -> string -> llbuilder -> llvalue
                        = "llvm_build_inttoptr"
external build_bitcast : llvalue -> lltype -> string -> llbuilder -> llvalue
                       = "llvm_build_bitcast"
external build_zext_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                 llvalue = "llvm_build_zext_or_bitcast"
external build_sext_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                 llvalue = "llvm_build_sext_or_bitcast"
external build_trunc_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                  llvalue = "llvm_build_trunc_or_bitcast"
external build_pointercast : llvalue -> lltype -> string -> llbuilder -> llvalue
                           = "llvm_build_pointercast"
external build_intcast : llvalue -> lltype -> string -> llbuilder -> llvalue
                       = "llvm_build_intcast"
external build_fpcast : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_fpcast"

(*--... Comparisons ........................................................--*)
external build_icmp : Icmp.t -> llvalue -> llvalue -> string ->
                      llbuilder -> llvalue = "llvm_build_icmp"
external build_fcmp : Fcmp.t -> llvalue -> llvalue -> string ->
                      llbuilder -> llvalue = "llvm_build_fcmp"

(*--... Miscellaneous instructions .........................................--*)
external build_phi : (llvalue * llbasicblock) list -> string -> llbuilder ->
                     llvalue = "llvm_build_phi"
external build_empty_phi : lltype -> string -> llbuilder -> llvalue
                         = "llvm_build_empty_phi"
external build_call : lltype -> llvalue -> llvalue array -> string ->
                      llbuilder -> llvalue = "llvm_build_call"
external build_select : llvalue -> llvalue -> llvalue -> string -> llbuilder ->
                        llvalue = "llvm_build_select"
external build_va_arg : llvalue -> lltype -> string -> llbuilder -> llvalue
                      = "llvm_build_va_arg"
external build_extractelement : llvalue -> llvalue -> string -> llbuilder ->
                                llvalue = "llvm_build_extractelement"
external build_insertelement : llvalue -> llvalue -> llvalue -> string ->
                               llbuilder -> llvalue = "llvm_build_insertelement"
external build_shufflevector : llvalue -> llvalue -> llvalue -> string ->
                               llbuilder -> llvalue = "llvm_build_shufflevector"
external build_extractvalue : llvalue -> int -> string -> llbuilder -> llvalue
                            = "llvm_build_extractvalue"
external build_insertvalue : llvalue -> llvalue -> int -> string -> llbuilder ->
                             llvalue = "llvm_build_insertvalue"

external build_is_null : llvalue -> string -> llbuilder -> llvalue
                       = "llvm_build_is_null"
external build_is_not_null : llvalue -> string -> llbuilder -> llvalue
                           = "llvm_build_is_not_null"
external build_ptrdiff : lltype -> llvalue -> llvalue -> string -> llbuilder ->
                         llvalue = "llvm_build_ptrdiff"
external build_freeze : llvalue -> string -> llbuilder -> llvalue
                      = "llvm_build_freeze"


(*===-- Memory buffers ----------------------------------------------------===*)

module MemoryBuffer = struct
  external of_file : string -> llmemorybuffer = "llvm_memorybuffer_of_file"
  external of_stdin : unit -> llmemorybuffer = "llvm_memorybuffer_of_stdin"
  external of_string : ?name:string -> string -> llmemorybuffer
                     = "llvm_memorybuffer_of_string"
  external as_string : llmemorybuffer -> string = "llvm_memorybuffer_as_string"
  external dispose : llmemorybuffer -> unit = "llvm_memorybuffer_dispose"
end
