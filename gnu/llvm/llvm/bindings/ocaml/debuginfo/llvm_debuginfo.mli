(*===-- llvm_debuginfo.mli - LLVM OCaml Interface -------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

type lldibuilder

(** Source languages known by DWARF. *)
module DWARFSourceLanguageKind : sig
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

module DIFlag : sig
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
(** An opaque type to represent OR of multiple DIFlag.t. *)

val diflags_get : DIFlag.t -> lldiflags
(** [diflags_set f] Construct an lldiflags value with a single flag [f]. *)

val diflags_set : lldiflags -> DIFlag.t -> lldiflags
(** [diflags_set fs f] Include flag [f] in [fs] and return the new value. *)

val diflags_test : lldiflags -> DIFlag.t -> bool
(** [diflags_test fs f] Does [fs] contain flag [f]? *)

(** The kind of metadata nodes. *)
module MetadataKind : sig
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
module DWARFEmissionKind : sig
  type t = None | Full | LineTablesOnly
end

val debug_metadata_version : unit -> int
(** [debug_metadata_version ()] The current debug metadata version number *)

val get_module_debug_metadata_version : Llvm.llmodule -> int
(** [get_module_debug_metadata_version m] Version of metadata present in [m]. *)

val dibuilder : Llvm.llmodule -> lldibuilder
(** [dibuilder m] Create a debug info builder for [m]. *)

val dibuild_finalize : lldibuilder -> unit
(** [dibuild_finalize dib] Construct any deferred debug info descriptors. *)

val dibuild_create_compile_unit :
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
(** [dibuild_create_compile_unit] A CompileUnit provides an anchor for all
    debugging information generated during this instance of compilation.
    See LLVMDIBuilderCreateCompileUnit. *)

val dibuild_create_file :
  lldibuilder -> filename:string -> directory:string -> Llvm.llmetadata
(** [dibuild_create_file] Create a file descriptor to hold debugging information
    for a file. See LLVMDIBuilderCreateFile. *)

val dibuild_create_module :
  lldibuilder ->
  parent_ref:Llvm.llmetadata ->
  name:string ->
  config_macros:string ->
  include_path:string ->
  sys_root:string ->
  Llvm.llmetadata
(** [dibuild_create_module] Create a new descriptor for a module with the
    specified parent scope. See LLVMDIBuilderCreateModule. *)

val dibuild_create_namespace :
  lldibuilder ->
  parent_ref:Llvm.llmetadata ->
  name:string ->
  export_symbols:bool ->
  Llvm.llmetadata
(** [dibuild_create_namespace] Create a new descriptor for a namespace with
    the specified parent scope. See LLVMDIBuilderCreateNameSpace *)

val dibuild_create_function :
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
(** [dibuild_create_function] Create a new descriptor for the specified
    subprogram. See LLVMDIBuilderCreateFunction. *)

val dibuild_create_lexical_block :
  lldibuilder ->
  scope:Llvm.llmetadata ->
  file:Llvm.llmetadata ->
  line:int ->
  column:int ->
  Llvm.llmetadata
(** [dibuild_create_lexical_block] Create a descriptor for a lexical block with
    the specified parent context. See LLVMDIBuilderCreateLexicalBlock *)

val llmetadata_null : unit -> Llvm.llmetadata
(** [llmetadata_null ()] llmetadata is a wrapper around "llvm::Metadata *".
    This function returns a nullptr valued llmetadata. For example, it
    can be used to convey an llmetadata for "void" type. *)

val dibuild_create_debug_location :
  ?inlined_at:Llvm.llmetadata ->
  Llvm.llcontext ->
  line:int ->
  column:int ->
  scope:Llvm.llmetadata ->
  Llvm.llmetadata
(** [dibuild_create] Create a new DebugLocation that describes a source
    location. See LLVMDIBuilderCreateDebugLocation *)

val di_location_get_line : location:Llvm.llmetadata -> int
(** [di_location_get_line l] Get the line number of debug location [l]. *)

val di_location_get_column : location:Llvm.llmetadata -> int
(** [di_location_get_column l] Get the column number of debug location [l]. *)

val di_location_get_scope : location:Llvm.llmetadata -> Llvm.llmetadata
(** [di_location_get_scope l] Get the local scope associated with
    debug location [l]. *)

val di_location_get_inlined_at :
  location:Llvm.llmetadata -> Llvm.llmetadata option
(** [di_location_get_inlined_at l] Get the "inlined at" location associated with
    debug location [l], if it exists. *)

val di_scope_get_file : scope:Llvm.llmetadata -> Llvm.llmetadata option
(** [di_scope_get_file l] Get the metadata of the file associated with scope [s]
    if it exists. *)

val di_file_get_directory : file:Llvm.llmetadata -> string
(** [di_file_get_directory f] Get the directory of file [f]. *)

val di_file_get_filename : file:Llvm.llmetadata -> string
(** [di_file_get_filename f] Get the name of file [f]. *)

val di_file_get_source : file:Llvm.llmetadata -> string
(** [di_file_get_source f] Get the source of file [f]. *)

val dibuild_get_or_create_type_array :
  lldibuilder -> data:Llvm.llmetadata array -> Llvm.llmetadata
(** [dibuild_get_or_create_type_array] Create a type array.
    See LLVMDIBuilderGetOrCreateTypeArray. *)

val dibuild_get_or_create_array :
  lldibuilder -> data:Llvm.llmetadata array -> Llvm.llmetadata
(** [dibuild_get_or_create_array] Create an array of DI Nodes.
    See LLVMDIBuilderGetOrCreateArray. *)

val dibuild_create_constant_value_expression :
  lldibuilder -> int -> Llvm.llmetadata
(** [dibuild_create_constant_value_expression] Create a new descriptor for
    the specified variable that does not have an address, but does have
    a constant value. See LLVMDIBuilderCreateConstantValueExpression. *)

val dibuild_create_global_variable_expression :
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
(** [dibuild_create_global_variable_expression] Create a new descriptor for
    the specified variable. See LLVMDIBuilderCreateGlobalVariableExpression. *)

val di_global_variable_expression_get_variable :
  Llvm.llmetadata -> Llvm.llmetadata option
(** [di_global_variable_expression_get_variable gve] returns the debug variable
    of [gve], which must be a [DIGlobalVariableExpression].
    See LLVMDIGlobalVariableExpressionGetVariable. *)

val di_variable_get_line : Llvm.llmetadata -> int
(** [di_variable_get_line v] returns the line number of the variable [v].
    See LLVMDIVariableGetLine. *)

val di_variable_get_file : Llvm.llmetadata -> Llvm.llmetadata option
(** [di_variable_get_file v] returns the file of the variable [v].
    See LLVMDIVariableGetFile. *)

val dibuild_create_subroutine_type :
  lldibuilder ->
  file:Llvm.llmetadata ->
  param_types:Llvm.llmetadata array ->
  lldiflags ->
  Llvm.llmetadata
(** [dibuild_create_subroutine_type] Create subroutine type.
    See LLVMDIBuilderCreateSubroutineType *)

val dibuild_create_enumerator :
  lldibuilder -> name:string -> value:int -> is_unsigned:bool -> Llvm.llmetadata
(** [dibuild_create_enumerator] Create debugging information entry for an
    enumerator. See LLVMDIBuilderCreateEnumerator *)

val dibuild_create_enumeration_type :
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
(** [dibuild_create_enumeration_type] Create debugging information entry for
    an enumeration. See LLVMDIBuilderCreateEnumerationType. *)

val dibuild_create_union_type :
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
(** [dibuild_create_union_type] Create debugging information entry for a union.
    See LLVMDIBuilderCreateUnionType. *)

val dibuild_create_array_type :
  lldibuilder ->
  size:int ->
  align_in_bits:int ->
  ty:Llvm.llmetadata ->
  subscripts:Llvm.llmetadata array ->
  Llvm.llmetadata
(** [dibuild_create_array_type] Create debugging information entry for an array.
    See LLVMDIBuilderCreateArrayType. *)

val dibuild_create_vector_type :
  lldibuilder ->
  size:int ->
  align_in_bits:int ->
  ty:Llvm.llmetadata ->
  subscripts:Llvm.llmetadata array ->
  Llvm.llmetadata
(** [dibuild_create_vector_type] Create debugging information entry for a
    vector type. See LLVMDIBuilderCreateVectorType. *)

val dibuild_create_unspecified_type :
  lldibuilder -> name:string -> Llvm.llmetadata
(** [dibuild_create_unspecified_type] Create a DWARF unspecified type. *)

val dibuild_create_basic_type :
  lldibuilder ->
  name:string ->
  size_in_bits:int ->
  encoding:int ->
  lldiflags ->
  Llvm.llmetadata
(** [dibuild_create_basic_type] Create debugging information entry for a basic
    type. See LLVMDIBuilderCreateBasicType. *)

val dibuild_create_pointer_type :
  lldibuilder ->
  pointee_ty:Llvm.llmetadata ->
  size_in_bits:int ->
  align_in_bits:int ->
  address_space:int ->
  name:string ->
  Llvm.llmetadata
(** [dibuild_create_pointer_type] Create debugging information entry for a
    pointer. See LLVMDIBuilderCreatePointerType. *)

val dibuild_create_struct_type :
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
(** [dibuild_create_struct_type] Create debugging information entry for a
    struct. See LLVMDIBuilderCreateStructType *)

val dibuild_create_member_type :
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
(** [dibuild_create_member_type] Create debugging information entry for a
    member. See LLVMDIBuilderCreateMemberType. *)

val dibuild_create_static_member_type :
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
(** [dibuild_create_static_member_type] Create debugging information entry for
    a C++ static data member. See LLVMDIBuilderCreateStaticMemberType *)

val dibuild_create_member_pointer_type :
  lldibuilder ->
  pointee_type:Llvm.llmetadata ->
  class_type:Llvm.llmetadata ->
  size_in_bits:int ->
  align_in_bits:int ->
  lldiflags ->
  Llvm.llmetadata
(** [dibuild_create_member_pointer_type] Create debugging information entry for
    a pointer to member. See LLVMDIBuilderCreateMemberPointerType *)

val dibuild_create_object_pointer_type :
  lldibuilder -> Llvm.llmetadata -> Llvm.llmetadata
(** [dibuild_create_object_pointer_type dib ty] Create a uniqued DIType* clone
  with FlagObjectPointer and FlagArtificial set. [dib] is the dibuilder
  value and [ty] the underlying type to which this pointer points. *)

val dibuild_create_qualified_type :
  lldibuilder -> tag:int -> Llvm.llmetadata -> Llvm.llmetadata
(** [dibuild_create_qualified_type dib tag ty] Create debugging information
    entry for a qualified type, e.g. 'const int'. [dib] is the dibuilder value,
    [tag] identifyies the type and [ty] is the base type. *)

val dibuild_create_reference_type :
  lldibuilder -> tag:int -> Llvm.llmetadata -> Llvm.llmetadata
(** [dibuild_create_reference_type dib tag ty] Create debugging information
    entry for a reference type. [dib] is the dibuilder value, [tag] identifyies
    the type and [ty] is the base type. *)

val dibuild_create_null_ptr_type : lldibuilder -> Llvm.llmetadata
(** [dibuild_create_null_ptr_type dib] Create C++11 nullptr type. *)

val dibuild_create_typedef :
  lldibuilder ->
  ty:Llvm.llmetadata ->
  name:string ->
  file:Llvm.llmetadata ->
  line_no:int ->
  scope:Llvm.llmetadata ->
  align_in_bits:int ->
  Llvm.llmetadata
(** [dibuild_create_typedef] Create debugging information entry for a typedef.
    See LLVMDIBuilderCreateTypedef. *)

val dibuild_create_inheritance :
  lldibuilder ->
  ty:Llvm.llmetadata ->
  base_ty:Llvm.llmetadata ->
  base_offset:int ->
  vb_ptr_offset:int ->
  lldiflags ->
  Llvm.llmetadata
(** [dibuild_create_inheritance] Create debugging information entry
    to establish inheritance relationship between two types.
    See LLVMDIBuilderCreateInheritance. *)

val dibuild_create_forward_decl :
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
(** [dibuild_create_forward_decl] Create a permanent forward-declared type.
    See LLVMDIBuilderCreateForwardDecl. *)

val dibuild_create_replaceable_composite_type :
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
(** [dibuild_create_replaceable_composite_type] Create a temporary
    forward-declared type. See LLVMDIBuilderCreateReplaceableCompositeType. *)

val dibuild_create_bit_field_member_type :
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
(** [dibuild_create_bit_field_member_type] Create debugging information entry
    for a bit field member. See LLVMDIBuilderCreateBitFieldMemberType. *)

val dibuild_create_class_type :
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
(** [dibuild_create_class_type] Create debugging information entry for a class.
    See LLVMDIBuilderCreateClassType. *)

val dibuild_create_artificial_type :
  lldibuilder -> ty:Llvm.llmetadata -> Llvm.llmetadata
(** [dibuild_create_artificial_type dib ty] Create a uniqued DIType* clone with
    FlagArtificial set.
    [dib] is the dibuilder value and [ty] the underlying type. *)

val di_type_get_name : Llvm.llmetadata -> string
(** [di_type_get_name m] Get the name of DIType [m]. *)

val di_type_get_size_in_bits : Llvm.llmetadata -> int
(** [di_type_get_size_in_bits m] Get size in bits of DIType [m]. *)

val di_type_get_offset_in_bits : Llvm.llmetadata -> int
(** [di_type_get_offset_in_bits m] Get offset in bits of DIType [m]. *)

val di_type_get_align_in_bits : Llvm.llmetadata -> int
(** [di_type_get_align_in_bits m] Get alignment in bits of DIType [m]. *)

val di_type_get_line : Llvm.llmetadata -> int
(** [di_type_get_line m] Get source line where DIType [m] is declared. *)

val di_type_get_flags : Llvm.llmetadata -> lldiflags
(** [di_type_get_flags m] Get the flags associated with DIType [m]. *)

val get_subprogram : Llvm.llvalue -> Llvm.llmetadata option
(** [get_subprogram f] Get the metadata of the subprogram attached to
    function [f]. *)

val set_subprogram : Llvm.llvalue -> Llvm.llmetadata -> unit
(** [set_subprogram f m] Set the subprogram [m] attached to function [f]. *)

val di_subprogram_get_line : Llvm.llmetadata -> int
(** [di_subprogram_get_line m] Get the line associated with subprogram [m]. *)

val instr_get_debug_loc : Llvm.llvalue -> Llvm.llmetadata option
(** [instr_get_debug_loc i] Get the debug location for instruction [i]. *)

val instr_set_debug_loc : Llvm.llvalue -> Llvm.llmetadata option -> unit
(** [instr_set_debug_loc i mopt] If [mopt] is None location metadata of [i]
    is cleared, Otherwise location of [i] is set to the value in [mopt]. *)

val get_metadata_kind : Llvm.llmetadata -> MetadataKind.t
(** [get_metadata_kind] Obtain the enumerated type of a Metadata instance. *)

val dibuild_create_auto_variable :
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
(** [dibuild_create_auto_variable] Create a new descriptor for a
    local auto variable. *)

val dibuild_create_parameter_variable :
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
(** [dibuild_create_parameter_variable] Create a new descriptor for a
    function parameter variable. *)

val dibuild_insert_declare_before :
  lldibuilder ->
  storage:Llvm.llvalue ->
  var_info:Llvm.llmetadata ->
  expr:Llvm.llmetadata ->
  location:Llvm.llmetadata ->
  instr:Llvm.llvalue ->
  Llvm.lldbgrecord
(** [dibuild_insert_declare_before]  Insert a new llvm.dbg.declare
    intrinsic call before the given instruction [instr]. *)

val dibuild_insert_declare_at_end :
  lldibuilder ->
  storage:Llvm.llvalue ->
  var_info:Llvm.llmetadata ->
  expr:Llvm.llmetadata ->
  location:Llvm.llmetadata ->
  block:Llvm.llbasicblock ->
  Llvm.lldbgrecord
(** [dibuild_insert_declare_at_end] Insert a new llvm.dbg.declare
    intrinsic call at the end of basic block [block]. If [block]
    has a terminator instruction, the intrinsic is inserted
    before that terminator instruction. *)

val dibuild_expression : lldibuilder -> Int64.t array -> Llvm.llmetadata
(** [dibuild_expression] Create a new descriptor for the specified variable
    which has a complex address expression for its address.
    See LLVMDIBuilderCreateExpression. *)

val is_new_dbg_info_format : Llvm.llmodule -> bool
(** [is_new_dbg_info_format] See LLVMIsNewDbgInfoFormat *)

val set_is_new_dbg_info_format : Llvm.llmodule -> bool -> unit
(** [set_is_new_dbg_info_format] See LLVMSetIsNewDbgInfoFormat *)
