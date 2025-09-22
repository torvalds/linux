(*===-- llvm_debuginfo.ml - LLVM OCaml Interface --------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

type lldibuilder

(** Source languages known by DWARF. *)
module DWARFSourceLanguageKind = struct
  type t =
    | C89
    | C
    | Ada83
    | C_plus_plus
    | Cobol74
    | Cobol85
    | Fortran77
    | Fortran90
    | Pascal83
    | Modula2
    (*  New in DWARF v3: *)
    | LLVMJava
    | C99
    | Ada95
    | Fortran95
    | PLI
    | ObjC
    | ObjC_plus_plus
    | UPC
    | D
    (*  New in DWARF v4: *)
    | LLVMPython
    (*  New in DWARF v5: *)
    | LLVMOpenCL
    | Go
    | Modula3
    | Haskell
    | C_plus_plus_03
    | C_plus_plus_11
    | OCaml
    | Rust
    | C11
    | Swift
    | Julia
    | Dylan
    | C_plus_plus_14
    | Fortran03
    | Fortran08
    | RenderScript
    | BLISS
    (*  Vendor extensions: *)
    | LLVMMips_Assembler
    | GOOGLE_RenderScript
    | BORLAND_Delphi
end

module DIFlag = struct
  type t =
    | Zero
    | Private
    | Protected
    | Public
    | FwdDecl
    | AppleBlock
    | ReservedBit4
    | Virtual
    | Artificial
    | Explicit
    | Prototyped
    | ObjcClassComplete
    | ObjectPointer
    | Vector
    | StaticMember
    | LValueReference
    | RValueReference
    | Reserved
    | SingleInheritance
    | MultipleInheritance
    | VirtualInheritance
    | IntroducedVirtual
    | BitField
    | NoReturn
    | TypePassByValue
    | TypePassByReference
    | EnumClass
    | FixedEnum
    | Thunk
    | NonTrivial
    | BigEndian
    | LittleEndian
    | IndirectVirtualBase
    | Accessibility
    | PtrToMemberRep
end

type lldiflags

external diflags_get : DIFlag.t -> lldiflags = "llvm_diflags_get"

external diflags_set : lldiflags -> DIFlag.t -> lldiflags = "llvm_diflags_set"

external diflags_test : lldiflags -> DIFlag.t -> bool = "llvm_diflags_test"

(** The kind of metadata nodes. *)
module MetadataKind = struct
  type t =
    | MDStringMetadataKind
    | ConstantAsMetadataMetadataKind
    | LocalAsMetadataMetadataKind
    | DistinctMDOperandPlaceholderMetadataKind
    | MDTupleMetadataKind
    | DILocationMetadataKind
    | DIExpressionMetadataKind
    | DIGlobalVariableExpressionMetadataKind
    | GenericDINodeMetadataKind
    | DISubrangeMetadataKind
    | DIEnumeratorMetadataKind
    | DIBasicTypeMetadataKind
    | DIDerivedTypeMetadataKind
    | DICompositeTypeMetadataKind
    | DISubroutineTypeMetadataKind
    | DIFileMetadataKind
    | DICompileUnitMetadataKind
    | DISubprogramMetadataKind
    | DILexicalBlockMetadataKind
    | DILexicalBlockFileMetadataKind
    | DINamespaceMetadataKind
    | DIModuleMetadataKind
    | DITemplateTypeParameterMetadataKind
    | DITemplateValueParameterMetadataKind
    | DIGlobalVariableMetadataKind
    | DILocalVariableMetadataKind
    | DILabelMetadataKind
    | DIObjCPropertyMetadataKind
    | DIImportedEntityMetadataKind
    | DIMacroMetadataKind
    | DIMacroFileMetadataKind
    | DICommonBlockMetadataKind
end

(** The amount of debug information to emit. *)
module DWARFEmissionKind = struct
  type t = None | Full | LineTablesOnly
end

external debug_metadata_version : unit -> int = "llvm_debug_metadata_version"

external get_module_debug_metadata_version : Llvm.llmodule -> int
  = "llvm_get_module_debug_metadata_version"

external dibuilder : Llvm.llmodule -> lldibuilder = "llvm_dibuilder"

external dibuild_finalize : lldibuilder -> unit = "llvm_dibuild_finalize"

(* See LLVMDIBuilderCreateCompileUnit for argument details. *)
external dibuild_create_compile_unit :
  lldibuilder ->
  DWARFSourceLanguageKind.t ->
  file_ref:Llvm.llmetadata ->
  producer:string ->
  is_optimized:bool ->
  flags:string ->
  runtime_ver:int ->
  split_name:string ->
  DWARFEmissionKind.t ->
  dwoid:int ->
  di_inlining:bool ->
  di_profiling:bool ->
  sys_root:string ->
  sdk:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_compile_unit_bytecode" "llvm_dibuild_create_compile_unit_native"

external dibuild_create_file :
  lldibuilder -> filename:string -> directory:string -> Llvm.llmetadata
  = "llvm_dibuild_create_file"

external dibuild_create_module :
  lldibuilder ->
  parent_ref:Llvm.llmetadata ->
  name:string ->
  config_macros:string ->
  include_path:string ->
  sys_root:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_module_bytecode" "llvm_dibuild_create_module_native"

external dibuild_create_namespace :
  lldibuilder ->
  parent_ref:Llvm.llmetadata ->
  name:string ->
  export_symbols:bool ->
  Llvm.llmetadata = "llvm_dibuild_create_namespace"

external dibuild_create_function :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  linkage_name:string ->
  file:Llvm.llmetadata ->
  line_no:int ->
  ty:Llvm.llmetadata ->
  is_local_to_unit:bool ->
  is_definition:bool ->
  scope_line:int ->
  flags:lldiflags ->
  is_optimized:bool ->
  Llvm.llmetadata
  = "llvm_dibuild_create_function_bytecode" "llvm_dibuild_create_function_native"

external dibuild_create_lexical_block :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  file:Llvm.llmetadata ->
  line:int ->
  column:int ->
  Llvm.llmetadata = "llvm_dibuild_create_lexical_block"

external dibuild_create_debug_location_helper :
  Llvm.llcontext ->
  line:int ->
  column:int ->
  scope:Llvm.llmetadata ->
  inlined_at:Llvm.llmetadata ->
  Llvm.llmetadata = "llvm_dibuild_create_debug_location"

external llmetadata_null : unit -> Llvm.llmetadata = "llvm_metadata_null"

let dibuild_create_debug_location ?(inlined_at = llmetadata_null ()) llctx ~line
    ~column ~scope =
  dibuild_create_debug_location_helper llctx ~line ~column ~scope ~inlined_at

external di_location_get_line : location:Llvm.llmetadata -> int
  = "llvm_di_location_get_line"

external di_location_get_column : location:Llvm.llmetadata -> int
  = "llvm_di_location_get_column"

external di_location_get_scope : location:Llvm.llmetadata -> Llvm.llmetadata
  = "llvm_di_location_get_scope"

external di_location_get_inlined_at :
  location:Llvm.llmetadata -> Llvm.llmetadata option
  = "llvm_di_location_get_inlined_at"

external di_scope_get_file : scope:Llvm.llmetadata -> Llvm.llmetadata option
  = "llvm_di_scope_get_file"

external di_file_get_directory : file:Llvm.llmetadata -> string
  = "llvm_di_file_get_directory"

external di_file_get_filename : file:Llvm.llmetadata -> string
  = "llvm_di_file_get_filename"

external di_file_get_source : file:Llvm.llmetadata -> string
  = "llvm_di_file_get_source"

external dibuild_get_or_create_type_array :
  lldibuilder -> data:Llvm.llmetadata array -> Llvm.llmetadata
  = "llvm_dibuild_get_or_create_type_array"

external dibuild_get_or_create_array :
  lldibuilder -> data:Llvm.llmetadata array -> Llvm.llmetadata
  = "llvm_dibuild_get_or_create_array"

external dibuild_create_subroutine_type :
  lldibuilder ->
  file:Llvm.llmetadata ->
  param_types:Llvm.llmetadata array ->
  lldiflags ->
  Llvm.llmetadata = "llvm_dibuild_create_subroutine_type"

external dibuild_create_enumerator :
  lldibuilder -> name:string -> value:int -> is_unsigned:bool -> Llvm.llmetadata
  = "llvm_dibuild_create_enumerator"

external dibuild_create_enumeration_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  elements:Llvm.llmetadata array ->
  class_ty:Llvm.llmetadata ->
  Llvm.llmetadata
  = "llvm_dibuild_create_enumeration_type_bytecode" "llvm_dibuild_create_enumeration_type_native"

external dibuild_create_union_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  lldiflags ->
  elements:Llvm.llmetadata array ->
  run_time_language:int ->
  unique_id:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_union_type_bytecode" "llvm_dibuild_create_union_type_native"

external dibuild_create_array_type :
  lldibuilder ->
  size:int ->
  align_in_bits:int ->
  ty:Llvm.llmetadata ->
  subscripts:Llvm.llmetadata array ->
  Llvm.llmetadata = "llvm_dibuild_create_array_type"

external dibuild_create_vector_type :
  lldibuilder ->
  size:int ->
  align_in_bits:int ->
  ty:Llvm.llmetadata ->
  subscripts:Llvm.llmetadata array ->
  Llvm.llmetadata = "llvm_dibuild_create_array_type"

external dibuild_create_unspecified_type :
  lldibuilder -> name:string -> Llvm.llmetadata
  = "llvm_dibuild_create_unspecified_type"

external dibuild_create_basic_type :
  lldibuilder ->
  name:string ->
  size_in_bits:int ->
  encoding:int ->
  lldiflags ->
  Llvm.llmetadata = "llvm_dibuild_create_basic_type"

external dibuild_create_pointer_type :
  lldibuilder ->
  pointee_ty:Llvm.llmetadata ->
  size_in_bits:int ->
  align_in_bits:int ->
  address_space:int ->
  name:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_pointer_type_bytecode" "llvm_dibuild_create_pointer_type_native"

external dibuild_create_struct_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  lldiflags ->
  derived_from:Llvm.llmetadata ->
  elements:Llvm.llmetadata array ->
  DWARFSourceLanguageKind.t ->
  vtable_holder:Llvm.llmetadata ->
  unique_id:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_struct_type_bytecode" "llvm_dibuild_create_struct_type_native"

external dibuild_create_member_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  offset_in_bits:int ->
  lldiflags ->
  ty:Llvm.llmetadata ->
  Llvm.llmetadata
  = "llvm_dibuild_create_member_type_bytecode" "llvm_dibuild_create_member_type_native"

external dibuild_create_static_member_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  ty:Llvm.llmetadata ->
  lldiflags ->
  const_val:Llvm.llvalue ->
  align_in_bits:int ->
  Llvm.llmetadata
  = "llvm_dibuild_create_static_member_type_bytecode" "llvm_dibuild_create_static_member_type_native"

external dibuild_create_member_pointer_type :
  lldibuilder ->
  pointee_type:Llvm.llmetadata ->
  class_type:Llvm.llmetadata ->
  size_in_bits:int ->
  align_in_bits:int ->
  lldiflags ->
  Llvm.llmetadata
  = "llvm_dibuild_create_member_pointer_type_bytecode" "llvm_dibuild_create_member_pointer_type_native"

external dibuild_create_object_pointer_type :
  lldibuilder -> Llvm.llmetadata -> Llvm.llmetadata
  = "llvm_dibuild_create_object_pointer_type"

external dibuild_create_qualified_type :
  lldibuilder -> tag:int -> Llvm.llmetadata -> Llvm.llmetadata
  = "llvm_dibuild_create_qualified_type"

external dibuild_create_reference_type :
  lldibuilder -> tag:int -> Llvm.llmetadata -> Llvm.llmetadata
  = "llvm_dibuild_create_reference_type"

external dibuild_create_null_ptr_type : lldibuilder -> Llvm.llmetadata
  = "llvm_dibuild_create_null_ptr_type"

external dibuild_create_typedef :
  lldibuilder ->
  ty:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_no:int ->
  scope:Llvm.llmetadata ->
  align_in_bits:int ->
  Llvm.llmetadata
  = "llvm_dibuild_create_typedef_bytecode" "llvm_dibuild_create_typedef_native"

external dibuild_create_inheritance :
  lldibuilder ->
  ty:Llvm.llmetadata ->
  base_ty:Llvm.llmetadata ->
  base_offset:int ->
  vb_ptr_offset:int ->
  lldiflags ->
  Llvm.llmetadata
  = "llvm_dibuild_create_inheritance_bytecode" "llvm_dibuild_create_inheritance_native"

external dibuild_create_forward_decl :
  lldibuilder ->
  tag:int ->
  name:string ->
  scope:Llvm.llmetadata ->
  file:Llvm.llmetadata ->
  line:int ->
  runtime_lang:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  unique_identifier:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_forward_decl_bytecode" "llvm_dibuild_create_forward_decl_native"

external dibuild_create_replaceable_composite_type :
  lldibuilder ->
  tag:int ->
  name:string ->
  scope:Llvm.llmetadata ->
  file:Llvm.llmetadata ->
  line:int ->
  runtime_lang:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  lldiflags ->
  unique_identifier:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_replaceable_composite_type_bytecode" "llvm_dibuild_create_replaceable_composite_type_native"

external dibuild_create_bit_field_member_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_num:int ->
  size_in_bits:int ->
  offset_in_bits:int ->
  storage_offset_in_bits:int ->
  lldiflags ->
  ty:Llvm.llmetadata ->
  Llvm.llmetadata
  = "llvm_dibuild_create_bit_field_member_type_bytecode" "llvm_dibuild_create_bit_field_member_type_native"

external dibuild_create_class_type :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_number:int ->
  size_in_bits:int ->
  align_in_bits:int ->
  offset_in_bits:int ->
  lldiflags ->
  derived_from:Llvm.llmetadata ->
  elements:Llvm.llmetadata array ->
  vtable_holder:Llvm.llmetadata ->
  template_params_node:Llvm.llmetadata ->
  unique_identifier:string ->
  Llvm.llmetadata
  = "llvm_dibuild_create_class_type_bytecode" "llvm_dibuild_create_class_type_native"

external dibuild_create_artificial_type :
  lldibuilder -> ty:Llvm.llmetadata -> Llvm.llmetadata
  = "llvm_dibuild_create_artificial_type"

external di_type_get_name : Llvm.llmetadata -> string = "llvm_di_type_get_name"

external di_type_get_size_in_bits : Llvm.llmetadata -> int
  = "llvm_di_type_get_size_in_bits"

external di_type_get_offset_in_bits : Llvm.llmetadata -> int
  = "llvm_di_type_get_offset_in_bits"

external di_type_get_align_in_bits : Llvm.llmetadata -> int
  = "llvm_di_type_get_align_in_bits"

external di_type_get_line : Llvm.llmetadata -> int = "llvm_di_type_get_line"

external di_type_get_flags : Llvm.llmetadata -> lldiflags
  = "llvm_di_type_get_flags"

external get_subprogram : Llvm.llvalue -> Llvm.llmetadata option
  = "llvm_get_subprogram"

external set_subprogram : Llvm.llvalue -> Llvm.llmetadata -> unit
  = "llvm_set_subprogram"

external di_subprogram_get_line : Llvm.llmetadata -> int
  = "llvm_di_subprogram_get_line"

external instr_get_debug_loc : Llvm.llvalue -> Llvm.llmetadata option
  = "llvm_instr_get_debug_loc"

external instr_set_debug_loc_helper : Llvm.llvalue -> Llvm.llmetadata -> unit
  = "llvm_instr_set_debug_loc"

let instr_set_debug_loc i mopt =
  match mopt with
  | None -> instr_set_debug_loc_helper i (llmetadata_null ())
  | Some m -> instr_set_debug_loc_helper i m

external dibuild_create_constant_value_expression :
  lldibuilder -> int -> Llvm.llmetadata
  = "llvm_dibuild_create_constant_value_expression"

external dibuild_create_global_variable_expression :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  linkage:string ->
  file:Llvm.llmetadata ->
  line:int ->
  ty:Llvm.llmetadata ->
  is_local_to_unit:bool ->
  expr:Llvm.llmetadata ->
  decl:Llvm.llmetadata ->
  align_in_bits:int ->
  Llvm.llmetadata
  = "llvm_dibuild_create_global_variable_expression_bytecode" "llvm_dibuild_create_global_variable_expression_native"

external di_global_variable_expression_get_variable :
  Llvm.llmetadata -> Llvm.llmetadata option
  = "llvm_di_global_variable_expression_get_variable"

external di_variable_get_line : Llvm.llmetadata -> int
  = "llvm_di_variable_get_line"

external di_variable_get_file : Llvm.llmetadata -> Llvm.llmetadata option
  = "llvm_di_variable_get_file"

external get_metadata_kind : Llvm.llmetadata -> MetadataKind.t
  = "llvm_get_metadata_kind"

external dibuild_create_auto_variable :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line:int ->
  ty:Llvm.llmetadata ->
  always_preserve:bool ->
  lldiflags ->
  align_in_bits:int ->
  Llvm.llmetadata
  = "llvm_dibuild_create_auto_variable_bytecode" "llvm_dibuild_create_auto_variable_native"

external dibuild_create_parameter_variable :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  name:string ->
  argno:int ->
  file:Llvm.llmetadata ->
  line:int ->
  ty:Llvm.llmetadata ->
  always_preserve:bool ->
  lldiflags ->
  Llvm.llmetadata
  = "llvm_dibuild_create_parameter_variable_bytecode" "llvm_dibuild_create_parameter_variable_native"

external dibuild_insert_declare_before :
  lldibuilder ->
  storage:Llvm.llvalue ->
  var_info:Llvm.llmetadata ->
  expr:Llvm.llmetadata ->
  location:Llvm.llmetadata ->
  instr:Llvm.llvalue ->
  Llvm.lldbgrecord
  = "llvm_dibuild_insert_declare_before_bytecode" "llvm_dibuild_insert_declare_before_native"

external dibuild_insert_declare_at_end :
  lldibuilder ->
  storage:Llvm.llvalue ->
  var_info:Llvm.llmetadata ->
  expr:Llvm.llmetadata ->
  location:Llvm.llmetadata ->
  block:Llvm.llbasicblock ->
  Llvm.lldbgrecord
  = "llvm_dibuild_insert_declare_at_end_bytecode" "llvm_dibuild_insert_declare_at_end_native"

external dibuild_expression :
  lldibuilder ->
  Int64.t array ->
  Llvm.llmetadata
  = "llvm_dibuild_expression"

external is_new_dbg_info_format : Llvm.llmodule -> bool
                                = "llvm_is_new_dbg_info_format"

external set_is_new_dbg_info_format : Llvm.llmodule -> bool -> unit
                                    = "llvm_set_is_new_dbg_info_format"
