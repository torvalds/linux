(*===-- llvm/llvm.mli - LLVM OCaml Interface ------------------------------===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

(** Core API.

    This interface provides an OCaml API for the LLVM intermediate
    representation, the classes in the VMCore library. *)


(** {6 Abstract types}

    These abstract types correlate directly to the LLVMCore classes. *)

(** The top-level container for all LLVM global data. See the
    [llvm::LLVMContext] class. *)
type llcontext

(** The top-level container for all other LLVM Intermediate Representation (IR)
    objects. See the [llvm::Module] class. *)
type llmodule

(** Opaque representation of Metadata nodes. See the [llvm::Metadata] class. *)
type llmetadata

(** Each value in the LLVM IR has a type, an instance of [lltype]. See the
    [llvm::Type] class. *)
type lltype

(** Any value in the LLVM IR. Functions, instructions, global variables,
    constants, and much more are all [llvalues]. See the [llvm::Value] class.
    This type covers a wide range of subclasses. *)
type llvalue

(** Non-instruction debug info record. See the [llvm::DbgRecord] class.*)
type lldbgrecord

(** Used to store users and usees of values. See the [llvm::Use] class. *)
type lluse

(** A basic block in LLVM IR. See the [llvm::BasicBlock] class. *)
type llbasicblock

(** Used to generate instructions in the LLVM IR. See the [llvm::LLVMBuilder]
    class. *)
type llbuilder

(** Used to represent attribute kinds. *)
type llattrkind

(** An attribute in LLVM IR. See the [llvm::Attribute] class. *)
type llattribute

(** Used to efficiently handle large buffers of read-only binary data.
    See the [llvm::MemoryBuffer] class. *)
type llmemorybuffer

(** The kind id of metadata attached to an instruction. *)
type llmdkind

(** The kind of an [lltype], the result of [classify_type ty]. See the
    [llvm::Type::TypeID] enumeration. *)
module TypeKind : sig
  type t =
    Void
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

(** The linkage of a global value, accessed with {!linkage} and
    {!set_linkage}. See [llvm::GlobalValue::LinkageTypes]. *)
module Linkage : sig
  type t =
    External
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

(** The linker visibility of a global value, accessed with {!visibility} and
    {!set_visibility}. See [llvm::GlobalValue::VisibilityTypes]. *)
module Visibility : sig
  type t =
    Default
  | Hidden
  | Protected
end

(** The DLL storage class of a global value, accessed with {!dll_storage_class} and
    {!set_dll_storage_class}. See [llvm::GlobalValue::DLLStorageClassTypes]. *)
module DLLStorageClass : sig
  type t =
  | Default
  | DLLImport
  | DLLExport
end

(** The following calling convention values may be accessed with
    {!function_call_conv} and {!set_function_call_conv}. Calling
    conventions are open-ended. *)
module CallConv : sig
  val c : int             (** [c] is the C calling convention. *)
  val fast : int          (** [fast] is the calling convention to allow LLVM
                              maximum optimization opportunities. Use only with
                              internal linkage. *)
  val cold : int          (** [cold] is the calling convention for
                              callee-save. *)
  val x86_stdcall : int   (** [x86_stdcall] is the familiar stdcall calling
                              convention from C. *)
  val x86_fastcall : int  (** [x86_fastcall] is the familiar fastcall calling
                              convention from C. *)
end

(** The logical representation of an attribute. *)
module AttrRepr : sig
  type t =
  | Enum of llattrkind * int64
  | String of string * string
end

(** The position of an attribute. See [LLVMAttributeIndex]. *)
module AttrIndex : sig
  type t =
  | Function
  | Return
  | Param of int
end

(** The predicate for an integer comparison ([icmp]) instruction.
    See the [llvm::ICmpInst::Predicate] enumeration. *)
module Icmp : sig
  type t =
  | Eq  (** Equal *)
  | Ne  (** Not equal *)
  | Ugt (** Unsigned greater than *)
  | Uge (** Unsigned greater or equal *)
  | Ult (** Unsigned less than *)
  | Ule (** Unsigned less or equal *)
  | Sgt (** Signed greater than *)
  | Sge (** Signed greater or equal *)
  | Slt (** Signed less than *)
  | Sle (** Signed less or equal *)
end

(** The predicate for a floating-point comparison ([fcmp]) instruction.
    Ordered means that neither operand is a QNAN while unordered means
    that either operand may be a QNAN.
    See the [llvm::FCmpInst::Predicate] enumeration. *)
module Fcmp : sig
  type t =
  | False (** Always false *)
  | Oeq   (** Ordered and equal *)
  | Ogt   (** Ordered and greater than *)
  | Oge   (** Ordered and greater or equal *)
  | Olt   (** Ordered and less than *)
  | Ole   (** Ordered and less or equal *)
  | One   (** Ordered and not equal *)
  | Ord   (** Ordered (no operand is NaN) *)
  | Uno   (** Unordered (one operand at least is NaN) *)
  | Ueq   (** Unordered and equal *)
  | Ugt   (** Unordered and greater than *)
  | Uge   (** Unordered and greater or equal *)
  | Ult   (** Unordered and less than *)
  | Ule   (** Unordered and less or equal *)
  | Une   (** Unordered and not equal *)
  | True  (** Always true *)
end

(** The opcodes for LLVM instructions and constant expressions. *)
module Opcode : sig
  type t =
  | Invalid (** Not an instruction *)

  | Ret (** Terminator Instructions *)
  | Br
  | Switch
  | IndirectBr
  | Invoke
  | Invalid2
  | Unreachable

  | Add (** Standard Binary Operators *)
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

  | Shl (** Logical Operators *)
  | LShr
  | AShr
  | And
  | Or
  | Xor

  | Alloca (** Memory Operators *)
  | Load
  | Store
  | GetElementPtr

  | Trunc (** Cast Operators *)
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

  | ICmp (** Other Operators *)
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

(** The type of a clause of a [landingpad] instruction.
    See [llvm::LandingPadInst::ClauseType]. *)
module LandingPadClauseTy : sig
  type t =
  | Catch
  | Filter
end

(** The thread local mode of a global value, accessed with {!thread_local_mode}
    and {!set_thread_local_mode}.
    See [llvm::GlobalVariable::ThreadLocalMode]. *)
module ThreadLocalMode : sig
  type t =
  | None
  | GeneralDynamic
  | LocalDynamic
  | InitialExec
  | LocalExec
end

(** The ordering of an atomic [load], [store], [cmpxchg], [atomicrmw] or
    [fence] instruction. See [llvm::AtomicOrdering]. *)
module AtomicOrdering : sig
  type t =
  | NotAtomic
  | Unordered
  | Monotonic
  | Invalid (** removed due to API changes *)
  | Acquire
  | Release
  | AcqiureRelease
  | SequentiallyConsistent
end

(** The opcode of an [atomicrmw] instruction.
    See [llvm::AtomicRMWInst::BinOp]. *)
module AtomicRMWBinOp : sig
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

(** The kind of an [llvalue], the result of [classify_value v].
    See the various [LLVMIsA*] functions. *)
module ValueKind : sig
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

(** The kind of [Diagnostic], the result of [Diagnostic.severity d].
    See [llvm::DiagnosticSeverity]. *)
module DiagnosticSeverity : sig
  type t =
  | Error
  | Warning
  | Remark
  | Note
end

module ModuleFlagBehavior :sig
  type t =
  | Error
  | Warning
  | Require
  | Override
  | Append
  | AppendUnique
end

(** {6 Iteration} *)

(** [Before b] and [At_end a] specify positions from the start of the ['b] list
    of [a]. [llpos] is used to specify positions in and for forward iteration
    through the various value lists maintained by the LLVM IR. *)
type ('a, 'b) llpos =
| At_end of 'a
| Before of 'b

(** [After b] and [At_start a] specify positions from the end of the ['b] list
    of [a]. [llrev_pos] is used for reverse iteration through the various value
    lists maintained by the LLVM IR. *)
type ('a, 'b) llrev_pos =
| At_start of 'a
| After of 'b


(** {6 Exceptions} *)

exception FeatureDisabled of string

exception IoError of string


(** {6 Global configuration} *)

(** [enable_pretty_stacktraces ()] enables LLVM's built-in stack trace code.
    This intercepts the OS's crash signals and prints which component of LLVM
    you were in at the time of the crash. *)
val enable_pretty_stacktrace : unit -> unit

(** [install_fatal_error_handler f] installs [f] as LLVM's fatal error handler.
    The handler will receive the reason for termination as a string. After
    the handler has been executed, LLVM calls [exit(1)]. *)
val install_fatal_error_handler : (string -> unit) -> unit

(** [reset_fatal_error_handler ()] resets LLVM's fatal error handler. *)
val reset_fatal_error_handler : unit -> unit

(** [parse_command_line_options ?overview args] parses [args] using
    the LLVM command line parser. Note that the only stable thing about this
    function is its signature; you cannot rely on any particular set of command
    line arguments being interpreted the same way across LLVM versions.

    See the function [llvm::cl::ParseCommandLineOptions()]. *)
val parse_command_line_options : ?overview:string -> string array -> unit

(** {6 Context error handling} *)

module Diagnostic : sig
  type t

  (** [description d] returns a textual description of [d]. *)
  val description : t -> string

  (** [severity d] returns the severity of [d]. *)
  val severity : t -> DiagnosticSeverity.t
end

(** [set_diagnostic_handler c h] set the diagnostic handler of [c] to [h].
    See the method [llvm::LLVMContext::setDiagnosticHandler]. *)
val set_diagnostic_handler : llcontext -> (Diagnostic.t -> unit) option -> unit

(** {6 Contexts} *)

(** [create_context ()] creates a context for storing the "global" state in
    LLVM. See the constructor [llvm::LLVMContext]. *)
val create_context : unit -> llcontext

(** [destroy_context ()] destroys a context. See the destructor
    [llvm::LLVMContext::~LLVMContext]. *)
val dispose_context : llcontext -> unit

(** See the function [LLVMGetGlobalContext]. *)
val global_context : unit -> llcontext

(** [mdkind_id context name] returns the MDKind ID that corresponds to the
    name [name] in the context [context].  See the function
    [llvm::LLVMContext::getMDKindID]. *)
val mdkind_id : llcontext -> string -> llmdkind


(** {6 Attributes} *)

(** [UnknownAttribute attr] is raised when a enum attribute name [name]
    is not recognized by LLVM. *)
exception UnknownAttribute of string

(** [enum_attr_kind name] returns the kind of enum attributes named [name].
    May raise [UnknownAttribute]. *)
val enum_attr_kind : string -> llattrkind

(** [create_enum_attr context value kind] creates an enum attribute
    with the supplied [kind] and [value] in [context]; if the value
    is not required (as for the majority of attributes), use [0L].
    May raise [UnknownAttribute].
    See the constructor [llvm::Attribute::get]. *)
val create_enum_attr : llcontext -> string -> int64 -> llattribute

(** [create_string_attr context kind value] creates a string attribute
    with the supplied [kind] and [value] in [context].
    See the constructor [llvm::Attribute::get]. *)
val create_string_attr : llcontext -> string -> string -> llattribute

(** [attr_of_repr context repr] creates an attribute with the supplied
    representation [repr] in [context]. *)
val attr_of_repr : llcontext -> AttrRepr.t -> llattribute

(** [repr_of_attr attr] describes the representation of attribute [attr]. *)
val repr_of_attr : llattribute -> AttrRepr.t


(** {6 Modules} *)

(** [create_module context id] creates a module with the supplied module ID in
    the context [context].  Modules are not garbage collected; it is mandatory
    to call {!dispose_module} to free memory. See the constructor
    [llvm::Module::Module]. *)
val create_module : llcontext -> string -> llmodule

(** [dispose_module m] destroys a module [m] and all of the IR objects it
    contained. All references to subordinate objects are invalidated;
    referencing them will invoke undefined behavior. See the destructor
    [llvm::Module::~Module]. *)
val dispose_module : llmodule -> unit

(** [target_triple m] is the target specifier for the module [m], something like
    [i686-apple-darwin8]. See the method [llvm::Module::getTargetTriple]. *)
val target_triple: llmodule -> string

(** [target_triple triple m] changes the target specifier for the module [m] to
    the string [triple]. See the method [llvm::Module::setTargetTriple]. *)
val set_target_triple: string -> llmodule -> unit

(** [data_layout m] is the data layout specifier for the module [m], something
    like [e-p:32:32:32-i1:8:8-i8:8:8-i16:16:16-...-a0:0:64-f80:128:128]. See the
    method [llvm::Module::getDataLayout]. *)
val data_layout: llmodule -> string

(** [set_data_layout s m] changes the data layout specifier for the module [m]
    to the string [s]. See the method [llvm::Module::setDataLayout]. *)
val set_data_layout: string -> llmodule -> unit

(** [dump_module m] prints the .ll representation of the module [m] to standard
    error. See the method [llvm::Module::dump]. *)
val dump_module : llmodule -> unit

(** [print_module f m] prints the .ll representation of the module [m]
    to file [f]. See the method [llvm::Module::print]. *)
val print_module : string -> llmodule -> unit

(** [string_of_llmodule m] returns the .ll representation of the module [m]
    as a string. See the method [llvm::Module::print]. *)
val string_of_llmodule : llmodule -> string

(** [set_module_inline_asm m asm] sets the inline assembler for the module. See
    the method [llvm::Module::setModuleInlineAsm]. *)
val set_module_inline_asm : llmodule -> string -> unit

(** [module_context m] returns the context of the specified module.
    See the method [llvm::Module::getContext] *)
val module_context : llmodule -> llcontext

(** [get_module_identifier m] returns the module identifier of the
    specified module. See the method [llvm::Module::getModuleIdentifier] *)
val get_module_identifier : llmodule -> string

(** [set_module_identifier m id] sets the module identifier of [m]
    to [id]. See the method [llvm::Module::setModuleIdentifier] *)
val set_module_identifer : llmodule -> string -> unit

(** [get_module_flag m k] Return the corresponding value if key [k] appears in
    the module flags of [m], otherwise return None
    See the method [llvm::Module::getModuleFlag] *)
val get_module_flag : llmodule -> string -> llmetadata option

(** [add_module_flag m b k v] Add a module-level flag b, with key [k] and
    value [v] to the flags metadata of module [m]. It will create the 
    module-level flags named metadata if it doesn't already exist. *)
val add_module_flag : llmodule -> ModuleFlagBehavior.t ->
                        string -> llmetadata -> unit
(** {6 Types} *)

(** [classify_type ty] returns the {!TypeKind.t} corresponding to the type [ty].
    See the method [llvm::Type::getTypeID]. *)
val classify_type : lltype -> TypeKind.t

(** [type_is_sized ty] returns whether the type has a size or not.
    If it doesn't then it is not safe to call the [DataLayout::] methods on it.
    *)
val type_is_sized : lltype -> bool

(** [type_context ty] returns the {!llcontext} corresponding to the type [ty].
    See the method [llvm::Type::getContext]. *)
val type_context : lltype -> llcontext

(** [dump_type ty] prints the .ll representation of the type [ty] to standard
    error. See the method [llvm::Type::dump]. *)
val dump_type : lltype -> unit

(** [string_of_lltype ty] returns a string describing the type [ty]. *)
val string_of_lltype : lltype -> string


(** {7 Operations on integer types} *)

(** [i1_type c] returns an integer type of bitwidth 1 in the context [c]. See
    [llvm::Type::Int1Ty]. *)
val i1_type : llcontext -> lltype

(** [i8_type c] returns an integer type of bitwidth 8 in the context [c]. See
    [llvm::Type::Int8Ty]. *)
val i8_type : llcontext -> lltype

(** [i16_type c] returns an integer type of bitwidth 16 in the context [c]. See
    [llvm::Type::Int16Ty]. *)
val i16_type : llcontext -> lltype

(** [i32_type c] returns an integer type of bitwidth 32 in the context [c]. See
    [llvm::Type::Int32Ty]. *)
val i32_type : llcontext -> lltype

(** [i64_type c] returns an integer type of bitwidth 64 in the context [c]. See
    [llvm::Type::Int64Ty]. *)
val i64_type : llcontext -> lltype

(** [integer_type c n] returns an integer type of bitwidth [n] in the context
    [c]. See the method [llvm::IntegerType::get]. *)
val integer_type : llcontext -> int -> lltype

(** [integer_bitwidth c ty] returns the number of bits in the integer type [ty]
    in the context [c].  See the method [llvm::IntegerType::getBitWidth]. *)
val integer_bitwidth : lltype -> int


(** {7 Operations on real types} *)

(** [float_type c] returns the IEEE 32-bit floating point type in the context
    [c]. See [llvm::Type::FloatTy]. *)
val float_type : llcontext -> lltype

(** [double_type c] returns the IEEE 64-bit floating point type in the context
    [c]. See [llvm::Type::DoubleTy]. *)
val double_type : llcontext -> lltype

(** [x86fp80_type c] returns the x87 80-bit floating point type in the context
    [c]. See [llvm::Type::X86_FP80Ty]. *)
val x86fp80_type : llcontext -> lltype

(** [fp128_type c] returns the IEEE 128-bit floating point type in the context
    [c]. See [llvm::Type::FP128Ty]. *)
val fp128_type : llcontext -> lltype

(** [ppc_fp128_type c] returns the PowerPC 128-bit floating point type in the
    context [c]. See [llvm::Type::PPC_FP128Ty]. *)
val ppc_fp128_type : llcontext -> lltype


(** {7 Operations on function types} *)

(** [function_type ret_ty param_tys] returns the function type returning
    [ret_ty] and taking [param_tys] as parameters.
    See the method [llvm::FunctionType::get]. *)
val function_type : lltype -> lltype array -> lltype

(** [var_arg_function_type ret_ty param_tys] is just like
    [function_type ret_ty param_tys] except that it returns the function type
    which also takes a variable number of arguments.
    See the method [llvm::FunctionType::get]. *)
val var_arg_function_type : lltype -> lltype array -> lltype

(** [is_var_arg fty] returns [true] if [fty] is a varargs function type, [false]
    otherwise. See the method [llvm::FunctionType::isVarArg]. *)
val is_var_arg : lltype -> bool

(** [return_type fty] gets the return type of the function type [fty].
    See the method [llvm::FunctionType::getReturnType]. *)
val return_type : lltype -> lltype

(** [param_types fty] gets the parameter types of the function type [fty].
    See the method [llvm::FunctionType::getParamType]. *)
val param_types : lltype -> lltype array


(** {7 Operations on struct types} *)

(** [struct_type context tys] returns the structure type in the context
    [context] containing in the types in the array [tys]. See the method
    [llvm::StructType::get]. *)
val struct_type : llcontext -> lltype array -> lltype

(** [packed_struct_type context ys] returns the packed structure type in the
    context [context] containing in the types in the array [tys]. See the method
    [llvm::StructType::get]. *)
val packed_struct_type : llcontext -> lltype array -> lltype

(** [struct_name ty] returns the name of the named structure type [ty],
    or None if the structure type is not named *)
val struct_name : lltype -> string option

(** [named_struct_type context name] returns the named structure type [name]
    in the context [context].
    See the method [llvm::StructType::get]. *)
val named_struct_type : llcontext -> string -> lltype

(** [struct_set_body ty elts ispacked] sets the body of the named struct [ty]
    to the [elts] elements.
    See the moethd [llvm::StructType::setBody]. *)
val struct_set_body : lltype -> lltype array -> bool -> unit

(** [struct_element_types sty] returns the constituent types of the struct type
    [sty]. See the method [llvm::StructType::getElementType]. *)
val struct_element_types : lltype -> lltype array

(** [is_packed sty] returns [true] if the structure type [sty] is packed,
    [false] otherwise. See the method [llvm::StructType::isPacked]. *)
val is_packed : lltype -> bool

(** [is_opaque sty] returns [true] if the structure type [sty] is opaque.
    [false] otherwise. See the method [llvm::StructType::isOpaque]. *)
val is_opaque : lltype -> bool

(** [is_literal sty] returns [true] if the structure type [sty] is literal.
    [false] otherwise. See the method [llvm::StructType::isLiteral]. *)
val is_literal : lltype -> bool


(** {7 Operations on pointer, vector, and array types} *)

(** [subtypes ty] returns [ty]'s subtypes *)
val subtypes : lltype -> lltype array

(** [array_type ty n] returns the array type containing [n] elements of type
    [ty]. See the method [llvm::ArrayType::get]. *)
val array_type : lltype -> int -> lltype

(** [pointer_type context] returns the pointer type in the default
    address space (0).
    See the method [llvm::PointerType::getUnqual]. *)
val pointer_type : llcontext -> lltype

(** [qualified_pointer_type context sp] returns the pointer type referencing
    objects in address space [sp].
    See the method [llvm::PointerType::get]. *)
val qualified_pointer_type : llcontext -> int -> lltype

(** [vector_type ty n] returns the array type containing [n] elements of the
    primitive type [ty]. See the method [llvm::ArrayType::get]. *)
val vector_type : lltype -> int -> lltype

(** [element_type ty] returns the element type of the pointer, vector, or array
    type [ty]. See the method [llvm::SequentialType::get]. *)
val element_type : lltype -> lltype

(** [element_type aty] returns the element count of the array type [aty].
    See the method [llvm::ArrayType::getNumElements]. *)
val array_length : lltype -> int

(** [address_space pty] returns the address space qualifier of the pointer type
    [pty]. See the method [llvm::PointerType::getAddressSpace]. *)
val address_space : lltype -> int

(** [element_type ty] returns the element count of the vector type [ty].
    See the method [llvm::VectorType::getNumElements]. *)
val vector_size : lltype -> int


(** {7 Operations on other types} *)

(** [void_type c] creates a type of a function which does not return any
    value in the context [c]. See [llvm::Type::VoidTy]. *)
val void_type : llcontext -> lltype

(** [label_type c] creates a type of a basic block in the context [c]. See
    [llvm::Type::LabelTy]. *)
val label_type : llcontext -> lltype

(** [x86_mmx_type c] returns the x86 64-bit MMX register type in the
    context [c]. See [llvm::Type::X86_MMXTy]. *)
val x86_mmx_type : llcontext -> lltype

(** [type_by_name m name] returns the specified type from the current module
    if it exists.
    See the method [llvm::Module::getTypeByName] *)
val type_by_name : llmodule -> string -> lltype option


(** {6 Values} *)

(** [type_of v] returns the type of the value [v].
    See the method [llvm::Value::getType]. *)
val type_of : llvalue -> lltype

(** [classify_value v] returns the kind of the value [v]. *)
val classify_value : llvalue -> ValueKind.t

(** [value_name v] returns the name of the value [v]. For global values, this is
    the symbol name. For instructions and basic blocks, it is the SSA register
    name. It is meaningless for constants.
    See the method [llvm::Value::getName]. *)
val value_name : llvalue -> string

(** [set_value_name n v] sets the name of the value [v] to [n]. See the method
    [llvm::Value::setName]. *)
val set_value_name : string -> llvalue -> unit

(** [dump_value v] prints the .ll representation of the value [v] to standard
    error. See the method [llvm::Value::dump]. *)
val dump_value : llvalue -> unit

(** [string_of_llvalue v] returns a string describing the value [v]. *)
val string_of_llvalue : llvalue -> string

(** [string_of_lldbgrecord r] returns a string describing the DbgRecord [r]. *)
val string_of_lldbgrecord : lldbgrecord -> string

(** [replace_all_uses_with old new] replaces all uses of the value [old]
    with the value [new]. See the method [llvm::Value::replaceAllUsesWith]. *)
val replace_all_uses_with : llvalue -> llvalue -> unit


(** {6 Uses} *)

(** [use_begin v] returns the first position in the use list for the value [v].
    [use_begin] and [use_succ] can e used to iterate over the use list in order.
    See the method [llvm::Value::use_begin]. *)
val use_begin : llvalue -> lluse option

(** [use_succ u] returns the use list position succeeding [u].
    See the method [llvm::use_value_iterator::operator++]. *)
val use_succ : lluse -> lluse option

(** [user u] returns the user of the use [u].
    See the method [llvm::Use::getUser]. *)
val user : lluse -> llvalue

(** [used_value u] returns the usee of the use [u].
    See the method [llvm::Use::getUsedValue]. *)
val used_value : lluse -> llvalue

(** [iter_uses f v] applies function [f] to each of the users of the value [v]
    in order. Tail recursive. *)
val iter_uses : (lluse -> unit) -> llvalue -> unit

(** [fold_left_uses f init v] is [f (... (f init u1) ...) uN] where
    [u1,...,uN] are the users of the value [v]. Tail recursive. *)
val fold_left_uses : ('a -> lluse -> 'a) -> 'a -> llvalue -> 'a

(** [fold_right_uses f v init] is [f u1 (... (f uN init) ...)] where
    [u1,...,uN] are the users of the value [v]. Not tail recursive. *)
val fold_right_uses : (lluse -> 'a -> 'a) -> llvalue -> 'a -> 'a


(** {6 Users} *)

(** [operand v i] returns the operand at index [i] for the value [v]. See the
    method [llvm::User::getOperand]. *)
val operand : llvalue -> int -> llvalue

(** [operand_use v i] returns the use of the operand at index [i] for the value [v]. See the
    method [llvm::User::getOperandUse]. *)
val operand_use : llvalue -> int -> lluse


(** [set_operand v i o] sets the operand of the value [v] at the index [i] to
    the value [o].
    See the method [llvm::User::setOperand]. *)
val set_operand : llvalue -> int -> llvalue -> unit

(** [num_operands v] returns the number of operands for the value [v].
    See the method [llvm::User::getNumOperands]. *)
val num_operands : llvalue -> int


(** [indices i] returns the indices for the ExtractValue or InsertValue
    instruction [i].
    See the [llvm::getIndices] methods. *)
val indices : llvalue -> int array

(** {7 Operations on constants of (mostly) any type} *)

(** [is_constant v] returns [true] if the value [v] is a constant, [false]
    otherwise. Similar to [llvm::isa<Constant>]. *)
val is_constant : llvalue -> bool

(** [const_null ty] returns the constant null (zero) of the type [ty].
    See the method [llvm::Constant::getNullValue]. *)
val const_null : lltype -> llvalue

(** [const_all_ones ty] returns the constant '-1' of the integer or vector type
    [ty]. See the method [llvm::Constant::getAllOnesValue]. *)
val const_all_ones : (*int|vec*)lltype -> llvalue

(** [const_pointer_null ty] returns the constant null (zero) pointer of the type
    [ty]. See the method [llvm::ConstantPointerNull::get]. *)
val const_pointer_null : lltype -> llvalue

(** [undef ty] returns the undefined value of the type [ty].
    See the method [llvm::UndefValue::get]. *)
val undef : lltype -> llvalue

(** [poison ty] returns the poison value of the type [ty].
    See the method [llvm::PoisonValue::get]. *)
val poison : lltype -> llvalue

(** [is_null v] returns [true] if the value [v] is the null (zero) value.
    See the method [llvm::Constant::isNullValue]. *)
val is_null : llvalue -> bool

(** [is_undef v] returns [true] if the value [v] is an undefined value, [false]
    otherwise. Similar to [llvm::isa<UndefValue>]. *)
val is_undef : llvalue -> bool

(** [is_poison v] returns [true] if the value [v] is a poison value, [false]
    otherwise. Similar to [llvm::isa<PoisonValue>]. *)
val is_poison : llvalue -> bool

(** [constexpr_opcode v] returns an [Opcode.t] corresponding to constexpr
    value [v], or [Opcode.Invalid] if [v] is not a constexpr. *)
val constexpr_opcode : llvalue -> Opcode.t


(** {7 Operations on instructions} *)

(** [has_metadata i] returns whether or not the instruction [i] has any
    metadata attached to it. See the function
    [llvm::Instruction::hasMetadata]. *)
val has_metadata : llvalue -> bool

(** [metadata i kind] optionally returns the metadata associated with the
    kind [kind] in the instruction [i] See the function
    [llvm::Instruction::getMetadata]. *)
val metadata : llvalue -> llmdkind -> llvalue option

(** [set_metadata i kind md] sets the metadata [md] of kind [kind] in the
    instruction [i]. See the function [llvm::Instruction::setMetadata]. *)
val set_metadata : llvalue -> llmdkind -> llvalue -> unit

(** [clear_metadata i kind] clears the metadata of kind [kind] in the
    instruction [i]. See the function [llvm::Instruction::setMetadata]. *)
val clear_metadata : llvalue -> llmdkind -> unit


(** {7 Operations on metadata} *)

(** [mdstring c s] returns the MDString of the string [s] in the context [c].
    See the method [llvm::MDNode::get]. *)
val mdstring : llcontext -> string -> llvalue

(** [mdnode c elts] returns the MDNode containing the values [elts] in the
    context [c].
    See the method [llvm::MDNode::get]. *)
val mdnode : llcontext -> llvalue array -> llvalue

(** [mdnull c ] returns a null MDNode in context [c].  *)
val mdnull : llcontext -> llvalue

(** [get_mdstring v] returns the MDString.
    See the method [llvm::MDString::getString] *)
val get_mdstring : llvalue -> string option

(** [get_mdnode_operands v] returns the operands in the MDNode. *)
(*     See the method [llvm::MDNode::getOperand] *)
val get_mdnode_operands : llvalue -> llvalue array

(** [get_named_metadata m name] returns all the MDNodes belonging to the named
    metadata (if any).
    See the method [llvm::NamedMDNode::getOperand]. *)
val get_named_metadata : llmodule -> string -> llvalue array

(** [add_named_metadata_operand m name v] adds [v] as the last operand of
    metadata named [name] in module [m]. If the metadata does not exist,
    it is created.
    See the methods [llvm::Module::getNamedMetadata()] and
    [llvm::MDNode::addOperand()]. *)
val add_named_metadata_operand : llmodule -> string -> llvalue -> unit

(** Obtain a Metadata as a Value.
    See the method [llvm::ValueAsMetadata::get()]. *)
val value_as_metadata : llvalue -> llmetadata

(** Obtain a Value as a Metadata.
    See the method [llvm::MetadataAsValue::get()]. *)
val metadata_as_value : llcontext -> llmetadata -> llvalue

(** {7 Operations on scalar constants} *)

(** [const_int ty i] returns the integer constant of type [ty] and value [i].
    See the method [llvm::ConstantInt::get]. *)
val const_int : lltype -> int -> llvalue

(** [const_of_int64 ty i s] returns the integer constant of type [ty] and value
    [i]. [s] indicates whether the integer is signed or not.
    See the method [llvm::ConstantInt::get]. *)
val const_of_int64 : lltype -> Int64.t -> bool -> llvalue

(** [int64_of_const c] returns the int64 value of the [c] constant integer.
    None is returned if this is not an integer constant, or bitwidth exceeds 64.
    See the method [llvm::ConstantInt::getSExtValue].*)
val int64_of_const : llvalue -> Int64.t option

(** [const_int_of_string ty s r] returns the integer constant of type [ty] and
    value [s], with the radix [r]. See the method [llvm::ConstantInt::get]. *)
val const_int_of_string : lltype -> string -> int -> llvalue

(** [const_float ty n] returns the floating point constant of type [ty] and
    value [n]. See the method [llvm::ConstantFP::get]. *)
val const_float : lltype -> float -> llvalue

(** [float_of_const c] returns the float value of the [c] constant float.
    None is returned if this is not an float constant.
    See the method [llvm::ConstantFP::getDoubleValue].*)
val float_of_const : llvalue -> float option

(** [const_float_of_string ty s] returns the floating point constant of type
    [ty] and value [n]. See the method [llvm::ConstantFP::get]. *)
val const_float_of_string : lltype -> string -> llvalue

(** {7 Operations on composite constants} *)

(** [const_string c s] returns the constant [i8] array with the values of the
    characters in the string [s] in the context [c]. The array is not
    null-terminated (but see {!const_stringz}). This value can in turn be used
    as the initializer for a global variable. See the method
    [llvm::ConstantArray::get]. *)
val const_string : llcontext -> string -> llvalue

(** [const_stringz c s] returns the constant [i8] array with the values of the
    characters in the string [s] and a null terminator in the context [c]. This
    value can in turn be used as the initializer for a global variable.
    See the method [llvm::ConstantArray::get]. *)
val const_stringz : llcontext -> string -> llvalue

(** [const_array ty elts] returns the constant array of type
    [array_type ty (Array.length elts)] and containing the values [elts].
    This value can in turn be used as the initializer for a global variable.
    See the method [llvm::ConstantArray::get]. *)
val const_array : lltype -> llvalue array -> llvalue

(** [const_struct context elts] returns the structured constant of type
    [struct_type (Array.map type_of elts)] and containing the values [elts]
    in the context [context]. This value can in turn be used as the initializer
    for a global variable. See the method [llvm::ConstantStruct::getAnon]. *)
val const_struct : llcontext -> llvalue array -> llvalue

(** [const_named_struct namedty elts] returns the structured constant of type
    [namedty] (which must be a named structure type) and containing the values [elts].
    This value can in turn be used as the initializer
    for a global variable. See the method [llvm::ConstantStruct::get]. *)
val const_named_struct : lltype -> llvalue array -> llvalue

(** [const_packed_struct context elts] returns the structured constant of
    type {!packed_struct_type} [(Array.map type_of elts)] and containing the
    values [elts] in the context [context]. This value can in turn be used as
    the initializer for a global variable. See the method
    [llvm::ConstantStruct::get]. *)
val const_packed_struct : llcontext -> llvalue array -> llvalue

(** [const_vector elts] returns the vector constant of type
    [vector_type (type_of elts.(0)) (Array.length elts)] and containing the
    values [elts]. See the method [llvm::ConstantVector::get]. *)
val const_vector : llvalue array -> llvalue

(** [string_of_const c] returns [Some str] if [c] is a string constant,
    or [None] if this is not a string constant. *)
val string_of_const : llvalue -> string option

(** [aggregate_element c idx] returns [Some elt] where [elt] is the element of
    constant aggregate [c] at the specified index [idx], or [None] if [idx] is
    out of range or it's not possible to determine the element.
    See the method [llvm::Constant::getAggregateElement]. *)
val aggregate_element : llvalue -> int -> llvalue option


(** {7 Constant expressions} *)

(** [align_of ty] returns the alignof constant for the type [ty]. This is
    equivalent to [const_ptrtoint (const_gep (const_null (pointer_type {i8,ty}))
    (const_int i32_type 0) (const_int i32_type 1)) i32_type], but considerably
    more readable.  See the method [llvm::ConstantExpr::getAlignOf]. *)
val align_of : lltype -> llvalue

(** [size_of ty] returns the sizeof constant for the type [ty]. This is
    equivalent to [const_ptrtoint (const_gep (const_null (pointer_type ty))
    (const_int i32_type 1)) i64_type], but considerably more readable.
    See the method [llvm::ConstantExpr::getSizeOf]. *)
val size_of : lltype -> llvalue

(** [const_neg c] returns the arithmetic negation of the constant [c].
    See the method [llvm::ConstantExpr::getNeg]. *)
val const_neg : llvalue -> llvalue

(** [const_nsw_neg c] returns the arithmetic negation of the constant [c] with
    no signed wrapping. The result is undefined if the negation overflows.
    See the method [llvm::ConstantExpr::getNSWNeg]. *)
val const_nsw_neg : llvalue -> llvalue

(** [const_nuw_neg c] returns the arithmetic negation of the constant [c] with
    no unsigned wrapping. The result is undefined if the negation overflows.
    See the method [llvm::ConstantExpr::getNUWNeg]. *)
val const_nuw_neg : llvalue -> llvalue

(** [const_not c] returns the bitwise inverse of the constant [c].
    See the method [llvm::ConstantExpr::getNot]. *)
val const_not : llvalue -> llvalue

(** [const_add c1 c2] returns the constant sum of two constants.
    See the method [llvm::ConstantExpr::getAdd]. *)
val const_add : llvalue -> llvalue -> llvalue

(** [const_nsw_add c1 c2] returns the constant sum of two constants with no
    signed wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWAdd]. *)
val const_nsw_add : llvalue -> llvalue -> llvalue

(** [const_nuw_add c1 c2] returns the constant sum of two constants with no
    unsigned wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWAdd]. *)
val const_nuw_add : llvalue -> llvalue -> llvalue

(** [const_sub c1 c2] returns the constant difference, [c1 - c2], of two
    constants. See the method [llvm::ConstantExpr::getSub]. *)
val const_sub : llvalue -> llvalue -> llvalue

(** [const_nsw_sub c1 c2] returns the constant difference of two constants with
    no signed wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWSub]. *)
val const_nsw_sub : llvalue -> llvalue -> llvalue

(** [const_nuw_sub c1 c2] returns the constant difference of two constants with
    no unsigned wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWSub]. *)
val const_nuw_sub : llvalue -> llvalue -> llvalue

(** [const_mul c1 c2] returns the constant product of two constants.
    See the method [llvm::ConstantExpr::getMul]. *)
val const_mul : llvalue -> llvalue -> llvalue

(** [const_nsw_mul c1 c2] returns the constant product of two constants with
    no signed wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWMul]. *)
val const_nsw_mul : llvalue -> llvalue -> llvalue

(** [const_nuw_mul c1 c2] returns the constant product of two constants with
    no unsigned wrapping. The result is undefined if the sum overflows.
    See the method [llvm::ConstantExpr::getNSWMul]. *)
val const_nuw_mul : llvalue -> llvalue -> llvalue

(** [const_xor c1 c2] returns the constant bitwise [XOR] of two integer
    constants.
    See the method [llvm::ConstantExpr::getXor]. *)
val const_xor : llvalue -> llvalue -> llvalue

(** [const_gep srcty pc indices] returns the constant [getElementPtr] of [pc]
    with source element type [srcty] and the constant integers indices from the
    array [indices].
    See the method [llvm::ConstantExpr::getGetElementPtr]. *)
val const_gep : lltype -> llvalue -> llvalue array -> llvalue

(** [const_in_bounds_gep ty pc indices] returns the constant [getElementPtr] of
    [pc] with the constant integers indices from the array [indices].
    See the method [llvm::ConstantExpr::getInBoundsGetElementPtr]. *)
val const_in_bounds_gep : lltype -> llvalue -> llvalue array -> llvalue

(** [const_trunc c ty] returns the constant truncation of integer constant [c]
    to the smaller integer type [ty].
    See the method [llvm::ConstantExpr::getTrunc]. *)
val const_trunc : llvalue -> lltype -> llvalue

(** [const_ptrtoint c ty] returns the constant integer conversion of
    pointer constant [c] to integer type [ty].
    See the method [llvm::ConstantExpr::getPtrToInt]. *)
val const_ptrtoint : llvalue -> lltype -> llvalue

(** [const_inttoptr c ty] returns the constant pointer conversion of
    integer constant [c] to pointer type [ty].
    See the method [llvm::ConstantExpr::getIntToPtr]. *)
val const_inttoptr : llvalue -> lltype -> llvalue

(** [const_bitcast c ty] returns the constant bitwise conversion of constant [c]
    to type [ty] of equal size.
    See the method [llvm::ConstantExpr::getBitCast]. *)
val const_bitcast : llvalue -> lltype -> llvalue

(** [const_trunc_or_bitcast c ty] returns a constant trunc or bitwise cast
    conversion of constant [c] to type [ty].
    See the method [llvm::ConstantExpr::getTruncOrBitCast]. *)
val const_trunc_or_bitcast : llvalue -> lltype -> llvalue

(** [const_pointercast c ty] returns a constant bitcast or a pointer-to-int
    cast conversion of constant [c] to type [ty] of equal size.
    See the method [llvm::ConstantExpr::getPointerCast]. *)
val const_pointercast : llvalue -> lltype -> llvalue

(** [const_extractelement vec i] returns the constant [i]th element of
    constant vector [vec]. [i] must be a constant [i32] value unsigned less than
    the size of the vector.
    See the method [llvm::ConstantExpr::getExtractElement]. *)
val const_extractelement : llvalue -> llvalue -> llvalue

(** [const_insertelement vec v i] returns the constant vector with the same
    elements as constant vector [v] but the [i]th element replaced by the
    constant [v]. [v] must be a constant value with the type of the vector
    elements. [i] must be a constant [i32] value unsigned less than the size
    of the vector.
    See the method [llvm::ConstantExpr::getInsertElement]. *)
val const_insertelement : llvalue -> llvalue -> llvalue -> llvalue

(** [const_shufflevector a b mask] returns a constant [shufflevector].
    See the LLVM Language Reference for details on the [shufflevector]
    instruction.
    See the method [llvm::ConstantExpr::getShuffleVector]. *)
val const_shufflevector : llvalue -> llvalue -> llvalue -> llvalue

(** [const_inline_asm ty asm con side align] inserts a inline assembly string.
    See the method [llvm::InlineAsm::get]. *)
val const_inline_asm : lltype -> string -> string -> bool -> bool -> llvalue

(** [block_address f bb] returns the address of the basic block [bb] in the
    function [f]. See the method [llvm::BasicBlock::get]. *)
val block_address : llvalue -> llbasicblock -> llvalue


(** {7 Operations on global variables, functions, and aliases (globals)} *)

(** [global_parent g] is the enclosing module of the global value [g].
    See the method [llvm::GlobalValue::getParent]. *)
val global_parent : llvalue -> llmodule

(** [is_declaration g] returns [true] if the global value [g] is a declaration
    only. Returns [false] otherwise.
    See the method [llvm::GlobalValue::isDeclaration]. *)
val is_declaration : llvalue -> bool

(** [linkage g] returns the linkage of the global value [g].
    See the method [llvm::GlobalValue::getLinkage]. *)
val linkage : llvalue -> Linkage.t

(** [set_linkage l g] sets the linkage of the global value [g] to [l].
    See the method [llvm::GlobalValue::setLinkage]. *)
val set_linkage : Linkage.t -> llvalue -> unit

(** [unnamed_addr g] returns [true] if the global value [g] has the unnamed_addr
    attribute. Returns [false] otherwise.
    See the method [llvm::GlobalValue::getUnnamedAddr]. *)
val unnamed_addr : llvalue -> bool

(** [set_unnamed_addr b g] if [b] is [true], sets the unnamed_addr attribute of
    the global value [g]. Unset it otherwise.
    See the method [llvm::GlobalValue::setUnnamedAddr]. *)
val set_unnamed_addr : bool -> llvalue -> unit

(** [section g] returns the linker section of the global value [g].
    See the method [llvm::GlobalValue::getSection]. *)
val section : llvalue -> string

(** [set_section s g] sets the linker section of the global value [g] to [s].
    See the method [llvm::GlobalValue::setSection]. *)
val set_section : string -> llvalue -> unit

(** [visibility g] returns the linker visibility of the global value [g].
    See the method [llvm::GlobalValue::getVisibility]. *)
val visibility : llvalue -> Visibility.t

(** [set_visibility v g] sets the linker visibility of the global value [g] to
    [v]. See the method [llvm::GlobalValue::setVisibility]. *)
val set_visibility : Visibility.t -> llvalue -> unit

(** [dll_storage_class g] returns the DLL storage class of the global value [g].
    See the method [llvm::GlobalValue::getDLLStorageClass]. *)
val dll_storage_class : llvalue -> DLLStorageClass.t

(** [set_dll_storage_class v g] sets the DLL storage class of the global value [g] to
    [v]. See the method [llvm::GlobalValue::setDLLStorageClass]. *)
val set_dll_storage_class : DLLStorageClass.t -> llvalue -> unit

(** [alignment g] returns the required alignment of the global value [g].
    See the method [llvm::GlobalValue::getAlignment]. *)
val alignment : llvalue -> int

(** [set_alignment n g] sets the required alignment of the global value [g] to
    [n] bytes. See the method [llvm::GlobalValue::setAlignment]. *)
val set_alignment : int -> llvalue -> unit

(** [global_copy_all_metadata g] returns all the metadata associated with [g],
    which must be an [Instruction] or [GlobalObject].
    See the [llvm::Instruction::getAllMetadata()] and
    [llvm::GlobalObject::getAllMetadata()] methods. *)
val global_copy_all_metadata : llvalue -> (llmdkind * llmetadata) array


(** {7 Operations on global variables} *)

(** [declare_global ty name m] returns a new global variable of type [ty] and
    with name [name] in module [m] in the default address space (0). If such a
    global variable already exists, it is returned. If the type of the existing
    global differs, then a bitcast to [ty] is returned. *)
val declare_global : lltype -> string -> llmodule -> llvalue

(** [declare_qualified_global ty name addrspace m] returns a new global variable
    of type [ty] and with name [name] in module [m] in the address space
    [addrspace]. If such a global variable already exists, it is returned. If
    the type of the existing global differs, then a bitcast to [ty] is
    returned. *)
val declare_qualified_global : lltype -> string -> int -> llmodule -> llvalue

(** [define_global name init m] returns a new global with name [name] and
    initializer [init] in module [m] in the default address space (0). If the
    named global already exists, it is renamed.
    See the constructor of [llvm::GlobalVariable]. *)
val define_global : string -> llvalue -> llmodule -> llvalue

(** [define_qualified_global name init addrspace m] returns a new global with
    name [name] and initializer [init] in module [m] in the address space
    [addrspace]. If the named global already exists, it is renamed.
    See the constructor of [llvm::GlobalVariable]. *)
val define_qualified_global : string -> llvalue -> int -> llmodule -> llvalue

(** [lookup_global name m] returns [Some g] if a global variable with name
    [name] exists in module [m]. If no such global exists, returns [None].
    See the [llvm::GlobalVariable] constructor. *)
val lookup_global : string -> llmodule -> llvalue option

(** [delete_global gv] destroys the global variable [gv].
    See the method [llvm::GlobalVariable::eraseFromParent]. *)
val delete_global : llvalue -> unit

(** [global_begin m] returns the first position in the global variable list of
    the module [m]. [global_begin] and [global_succ] can be used to iterate
    over the global list in order.
    See the method [llvm::Module::global_begin]. *)
val global_begin : llmodule -> (llmodule, llvalue) llpos

(** [global_succ gv] returns the global variable list position succeeding
    [Before gv].
    See the method [llvm::Module::global_iterator::operator++]. *)
val global_succ : llvalue -> (llmodule, llvalue) llpos

(** [iter_globals f m] applies function [f] to each of the global variables of
    module [m] in order. Tail recursive. *)
val iter_globals : (llvalue -> unit) -> llmodule -> unit

(** [fold_left_globals f init m] is [f (... (f init g1) ...) gN] where
    [g1,...,gN] are the global variables of module [m]. Tail recursive. *)
val fold_left_globals : ('a -> llvalue -> 'a) -> 'a -> llmodule -> 'a

(** [global_end m] returns the last position in the global variable list of the
    module [m]. [global_end] and [global_pred] can be used to iterate over the
    global list in reverse.
    See the method [llvm::Module::global_end]. *)
val global_end : llmodule -> (llmodule, llvalue) llrev_pos

(** [global_pred gv] returns the global variable list position preceding
    [After gv].
    See the method [llvm::Module::global_iterator::operator--]. *)
val global_pred : llvalue -> (llmodule, llvalue) llrev_pos

(** [rev_iter_globals f m] applies function [f] to each of the global variables
    of module [m] in reverse order. Tail recursive. *)
val rev_iter_globals : (llvalue -> unit) -> llmodule -> unit

(** [fold_right_globals f m init] is [f g1 (... (f gN init) ...)] where
    [g1,...,gN] are the global variables of module [m]. Tail recursive. *)
val fold_right_globals : (llvalue -> 'a -> 'a) -> llmodule -> 'a -> 'a

(** [is_global_constant gv] returns [true] if the global variabile [gv] is a
    constant. Returns [false] otherwise.
    See the method [llvm::GlobalVariable::isConstant]. *)
val is_global_constant : llvalue -> bool

(** [set_global_constant c gv] sets the global variable [gv] to be a constant if
    [c] is [true] and not if [c] is [false].
    See the method [llvm::GlobalVariable::setConstant]. *)
val set_global_constant : bool -> llvalue -> unit

(** [global_initializer gv] If global variable [gv] has an initializer it is returned,
    otherwise returns [None]. See the method [llvm::GlobalVariable::getInitializer]. *)
val global_initializer : llvalue -> llvalue option

(** [set_initializer c gv] sets the initializer for the global variable
    [gv] to the constant [c].
    See the method [llvm::GlobalVariable::setInitializer]. *)
val set_initializer : llvalue -> llvalue -> unit

(** [remove_initializer gv] unsets the initializer for the global variable
    [gv].
    See the method [llvm::GlobalVariable::setInitializer]. *)
val remove_initializer : llvalue -> unit

(** [is_thread_local gv] returns [true] if the global variable [gv] is
    thread-local and [false] otherwise.
    See the method [llvm::GlobalVariable::isThreadLocal]. *)
val is_thread_local : llvalue -> bool

(** [set_thread_local c gv] sets the global variable [gv] to be thread local if
    [c] is [true] and not otherwise.
    See the method [llvm::GlobalVariable::setThreadLocal]. *)
val set_thread_local : bool -> llvalue -> unit

(** [is_thread_local gv] returns the thread local mode of the global
    variable [gv].
    See the method [llvm::GlobalVariable::getThreadLocalMode]. *)
val thread_local_mode : llvalue -> ThreadLocalMode.t

(** [set_thread_local c gv] sets the thread local mode of the global
    variable [gv].
    See the method [llvm::GlobalVariable::setThreadLocalMode]. *)
val set_thread_local_mode : ThreadLocalMode.t -> llvalue -> unit

(** [is_externally_initialized gv] returns [true] if the global
    variable [gv] is externally initialized and [false] otherwise.
    See the method [llvm::GlobalVariable::isExternallyInitialized]. *)
val is_externally_initialized : llvalue -> bool

(** [set_externally_initialized c gv] sets the global variable [gv] to be
    externally initialized if [c] is [true] and not otherwise.
    See the method [llvm::GlobalVariable::setExternallyInitialized]. *)
val set_externally_initialized : bool -> llvalue -> unit


(** {7 Operations on aliases} *)

(** [add_alias m vt sp a n] inserts an alias in the module [m] with the value
    type [vt] the address space [sp] the aliasee [a] with the name [n].
    See the constructor for [llvm::GlobalAlias]. *)
val add_alias : llmodule -> lltype -> int -> llvalue -> string -> llvalue

(** {7 Operations on functions} *)

(** [declare_function name ty m] returns a new function of type [ty] and
    with name [name] in module [m]. If such a function already exists,
    it is returned. If the type of the existing function differs, then a bitcast
    to [ty] is returned. *)
val declare_function : string -> lltype -> llmodule -> llvalue

(** [define_function name ty m] creates a new function with name [name] and
    type [ty] in module [m]. If the named function already exists, it is
    renamed. An entry basic block is created in the function.
    See the constructor of [llvm::GlobalVariable]. *)
val define_function : string -> lltype -> llmodule -> llvalue

(** [lookup_function name m] returns [Some f] if a function with name
    [name] exists in module [m]. If no such function exists, returns [None].
    See the method [llvm::Module] constructor. *)
val lookup_function : string -> llmodule -> llvalue option

(** [delete_function f] destroys the function [f].
    See the method [llvm::Function::eraseFromParent]. *)
val delete_function : llvalue -> unit

(** [function_begin m] returns the first position in the function list of the
    module [m]. [function_begin] and [function_succ] can be used to iterate over
    the function list in order.
    See the method [llvm::Module::begin]. *)
val function_begin : llmodule -> (llmodule, llvalue) llpos

(** [function_succ gv] returns the function list position succeeding
    [Before gv].
    See the method [llvm::Module::iterator::operator++]. *)
val function_succ : llvalue -> (llmodule, llvalue) llpos

(** [iter_functions f m] applies function [f] to each of the functions of module
    [m] in order. Tail recursive. *)
val iter_functions : (llvalue -> unit) -> llmodule -> unit

(** [fold_left_function f init m] is [f (... (f init f1) ...) fN] where
    [f1,...,fN] are the functions of module [m]. Tail recursive. *)
val fold_left_functions : ('a -> llvalue -> 'a) -> 'a -> llmodule -> 'a

(** [function_end m] returns the last position in the function list of
    the module [m]. [function_end] and [function_pred] can be used to iterate
    over the function list in reverse.
    See the method [llvm::Module::end]. *)
val function_end : llmodule -> (llmodule, llvalue) llrev_pos

(** [function_pred gv] returns the function list position preceding [After gv].
    See the method [llvm::Module::iterator::operator--]. *)
val function_pred : llvalue -> (llmodule, llvalue) llrev_pos

(** [rev_iter_functions f fn] applies function [f] to each of the functions of
    module [m] in reverse order. Tail recursive. *)
val rev_iter_functions : (llvalue -> unit) -> llmodule -> unit

(** [fold_right_functions f m init] is [f (... (f init fN) ...) f1] where
    [f1,...,fN] are the functions of module [m]. Tail recursive. *)
val fold_right_functions : (llvalue -> 'a -> 'a) -> llmodule -> 'a -> 'a

(** [is_intrinsic f] returns true if the function [f] is an intrinsic.
    See the method [llvm::Function::isIntrinsic]. *)
val is_intrinsic : llvalue -> bool

(** [function_call_conv f] returns the calling convention of the function [f].
    See the method [llvm::Function::getCallingConv]. *)
val function_call_conv : llvalue -> int

(** [set_function_call_conv cc f] sets the calling convention of the function
    [f] to the calling convention numbered [cc].
    See the method [llvm::Function::setCallingConv]. *)
val set_function_call_conv : int -> llvalue -> unit

(** [gc f] returns [Some name] if the function [f] has a garbage
    collection algorithm specified and [None] otherwise.
    See the method [llvm::Function::getGC]. *)
val gc : llvalue -> string option

(** [set_gc gc f] sets the collection algorithm for the function [f] to
    [gc]. See the method [llvm::Function::setGC]. *)
val set_gc : string option -> llvalue -> unit

(** [add_function_attr f a i] adds attribute [a] to the function [f]
    at position [i]. *)
val add_function_attr : llvalue -> llattribute -> AttrIndex.t -> unit

(** [function_attrs f i] returns the attributes for the function [f]
    at position [i]. *)
val function_attrs : llvalue -> AttrIndex.t -> llattribute array

(** [remove_enum_function_attr f k i] removes enum attribute with kind [k]
    from the function [f] at position [i]. *)
val remove_enum_function_attr : llvalue -> llattrkind -> AttrIndex.t -> unit

(** [remove_string_function_attr f k i] removes string attribute with kind [k]
    from the function [f] at position [i]. *)
val remove_string_function_attr : llvalue -> string -> AttrIndex.t -> unit


(** {7 Operations on params} *)

(** [params f] returns the parameters of function [f].
    See the method [llvm::Function::getArgumentList]. *)
val params : llvalue -> llvalue array

(** [param f n] returns the [n]th parameter of function [f].
    See the method [llvm::Function::getArgumentList]. *)
val param : llvalue -> int -> llvalue

(** [param_parent p] returns the parent function that owns the parameter.
    See the method [llvm::Argument::getParent]. *)
val param_parent : llvalue -> llvalue

(** [param_begin f] returns the first position in the parameter list of the
    function [f]. [param_begin] and [param_succ] can be used to iterate over
    the parameter list in order.
    See the method [llvm::Function::arg_begin]. *)
val param_begin : llvalue -> (llvalue, llvalue) llpos

(** [param_succ bb] returns the parameter list position succeeding
    [Before bb].
    See the method [llvm::Function::arg_iterator::operator++]. *)
val param_succ : llvalue -> (llvalue, llvalue) llpos

(** [iter_params f fn] applies function [f] to each of the parameters
    of function [fn] in order. Tail recursive. *)
val iter_params : (llvalue -> unit) -> llvalue -> unit

(** [fold_left_params f init fn] is [f (... (f init b1) ...) bN] where
    [b1,...,bN] are the parameters of function [fn]. Tail recursive. *)
val fold_left_params : ('a -> llvalue -> 'a) -> 'a -> llvalue -> 'a

(** [param_end f] returns the last position in the parameter list of
    the function [f]. [param_end] and [param_pred] can be used to iterate
    over the parameter list in reverse.
    See the method [llvm::Function::arg_end]. *)
val param_end : llvalue -> (llvalue, llvalue) llrev_pos

(** [param_pred gv] returns the function list position preceding [After gv].
    See the method [llvm::Function::arg_iterator::operator--]. *)
val param_pred : llvalue -> (llvalue, llvalue) llrev_pos

(** [rev_iter_params f fn] applies function [f] to each of the parameters
    of function [fn] in reverse order. Tail recursive. *)
val rev_iter_params : (llvalue -> unit) -> llvalue -> unit

(** [fold_right_params f fn init] is [f (... (f init bN) ...) b1] where
    [b1,...,bN] are the parameters of function [fn]. Tail recursive. *)
val fold_right_params : (llvalue -> 'a -> 'a) -> llvalue -> 'a -> 'a


(** {7 Operations on basic blocks} *)

(** [basic_blocks fn] returns the basic blocks of the function [f].
    See the method [llvm::Function::getBasicBlockList]. *)
val basic_blocks : llvalue -> llbasicblock array

(** [entry_block fn] returns the entry basic block of the function [f].
    See the method [llvm::Function::getEntryBlock]. *)
val entry_block : llvalue -> llbasicblock

(** [delete_block bb] deletes the basic block [bb].
    See the method [llvm::BasicBlock::eraseFromParent]. *)
val delete_block : llbasicblock -> unit

(** [remove_block bb] removes the basic block [bb] from its parent function.
    See the method [llvm::BasicBlock::removeFromParent]. *)
val remove_block : llbasicblock -> unit

(** [move_block_before pos bb] moves the basic block [bb] before [pos].
    See the method [llvm::BasicBlock::moveBefore]. *)
val move_block_before : llbasicblock -> llbasicblock -> unit

(** [move_block_after pos bb] moves the basic block [bb] after [pos].
    See the method [llvm::BasicBlock::moveAfter]. *)
val move_block_after : llbasicblock -> llbasicblock -> unit

(** [append_block c name f] creates a new basic block named [name] at the end of
    function [f] in the context [c].
    See the constructor of [llvm::BasicBlock]. *)
val append_block : llcontext -> string -> llvalue -> llbasicblock

(** [insert_block c name bb] creates a new basic block named [name] before the
    basic block [bb] in the context [c].
    See the constructor of [llvm::BasicBlock]. *)
val insert_block : llcontext -> string -> llbasicblock -> llbasicblock

(** [block_parent bb] returns the parent function that owns the basic block.
    See the method [llvm::BasicBlock::getParent]. *)
val block_parent : llbasicblock -> llvalue

(** [block_begin f] returns the first position in the basic block list of the
    function [f]. [block_begin] and [block_succ] can be used to iterate over
    the basic block list in order.
    See the method [llvm::Function::begin]. *)
val block_begin : llvalue -> (llvalue, llbasicblock) llpos

(** [block_succ bb] returns the basic block list position succeeding
    [Before bb].
    See the method [llvm::Function::iterator::operator++]. *)
val block_succ : llbasicblock -> (llvalue, llbasicblock) llpos

(** [iter_blocks f fn] applies function [f] to each of the basic blocks
    of function [fn] in order. Tail recursive. *)
val iter_blocks : (llbasicblock -> unit) -> llvalue -> unit

(** [fold_left_blocks f init fn] is [f (... (f init b1) ...) bN] where
    [b1,...,bN] are the basic blocks of function [fn]. Tail recursive. *)
val fold_left_blocks : ('a -> llbasicblock -> 'a) -> 'a -> llvalue -> 'a

(** [block_end f] returns the last position in the basic block list of
    the function [f]. [block_end] and [block_pred] can be used to iterate
    over the basic block list in reverse.
    See the method [llvm::Function::end]. *)
val block_end : llvalue -> (llvalue, llbasicblock) llrev_pos

(** [block_pred bb] returns the basic block list position preceding [After bb].
    See the method [llvm::Function::iterator::operator--]. *)
val block_pred : llbasicblock -> (llvalue, llbasicblock) llrev_pos

(** [block_terminator bb] returns the terminator of the basic block [bb]. *)
val block_terminator : llbasicblock -> llvalue option

(** [rev_iter_blocks f fn] applies function [f] to each of the basic blocks
    of function [fn] in reverse order. Tail recursive. *)
val rev_iter_blocks : (llbasicblock -> unit) -> llvalue -> unit

(** [fold_right_blocks f fn init] is [f (... (f init bN) ...) b1] where
    [b1,...,bN] are the basic blocks of function [fn]. Tail recursive. *)
val fold_right_blocks : (llbasicblock -> 'a -> 'a) -> llvalue -> 'a -> 'a

(** [value_of_block bb] losslessly casts [bb] to an [llvalue]. *)
val value_of_block : llbasicblock -> llvalue

(** [value_is_block v] returns [true] if the value [v] is a basic block and
    [false] otherwise.
    Similar to [llvm::isa<BasicBlock>]. *)
val value_is_block : llvalue -> bool

(** [block_of_value v] losslessly casts [v] to an [llbasicblock]. *)
val block_of_value : llvalue -> llbasicblock


(** {7 Operations on instructions} *)

(** [instr_parent i] is the enclosing basic block of the instruction [i].
    See the method [llvm::Instruction::getParent]. *)
val instr_parent : llvalue -> llbasicblock

(** [delete_instruction i] deletes the instruction [i].
 * See the method [llvm::Instruction::eraseFromParent]. *)
val delete_instruction : llvalue -> unit

(** [instr_begin bb] returns the first position in the instruction list of the
    basic block [bb]. [instr_begin] and [instr_succ] can be used to iterate over
    the instruction list in order.
    See the method [llvm::BasicBlock::begin]. *)
val instr_begin : llbasicblock -> (llbasicblock, llvalue) llpos

(** [instr_succ i] returns the instruction list position succeeding [Before i].
    See the method [llvm::BasicBlock::iterator::operator++]. *)
val instr_succ : llvalue -> (llbasicblock, llvalue) llpos

(** [iter_instrs f bb] applies function [f] to each of the instructions of basic
    block [bb] in order. Tail recursive. *)
val iter_instrs: (llvalue -> unit) -> llbasicblock -> unit

(** [fold_left_instrs f init bb] is [f (... (f init g1) ...) gN] where
    [g1,...,gN] are the instructions of basic block [bb]. Tail recursive. *)
val fold_left_instrs: ('a -> llvalue -> 'a) -> 'a -> llbasicblock -> 'a

(** [instr_end bb] returns the last position in the instruction list of the
    basic block [bb]. [instr_end] and [instr_pred] can be used to iterate over
    the instruction list in reverse.
    See the method [llvm::BasicBlock::end]. *)
val instr_end : llbasicblock -> (llbasicblock, llvalue) llrev_pos

(** [instr_pred i] returns the instruction list position preceding [After i].
    See the method [llvm::BasicBlock::iterator::operator--]. *)
val instr_pred : llvalue -> (llbasicblock, llvalue) llrev_pos

(** [fold_right_instrs f bb init] is [f (... (f init fN) ...) f1] where
    [f1,...,fN] are the instructions of basic block [bb]. Tail recursive. *)
val fold_right_instrs: (llvalue -> 'a -> 'a) -> llbasicblock -> 'a -> 'a

(** [inst_opcode i] returns the [Opcode.t] corresponding to instruction [i],
    or [Opcode.Invalid] if [i] is not an instruction. *)
val instr_opcode : llvalue -> Opcode.t

(** [icmp_predicate i] returns the [Icmp.t] corresponding to an [icmp]
    instruction [i]. *)
val icmp_predicate : llvalue -> Icmp.t option

(** [fcmp_predicate i] returns the [fcmp.t] corresponding to an [fcmp]
    instruction [i]. *)
val fcmp_predicate : llvalue -> Fcmp.t option

(** [inst_clone i] returns a copy of instruction [i],
    The instruction has no parent, and no name.
    See the method [llvm::Instruction::clone]. *)
val instr_clone : llvalue -> llvalue


(** {7 Operations on call sites} *)

(** [instruction_call_conv ci] is the calling convention for the call or invoke
    instruction [ci], which may be one of the values from the module
    {!CallConv}. See the method [llvm::CallInst::getCallingConv] and
    [llvm::InvokeInst::getCallingConv]. *)
val instruction_call_conv: llvalue -> int

(** [set_instruction_call_conv cc ci] sets the calling convention for the call
    or invoke instruction [ci] to the integer [cc], which can be one of the
    values from the module {!CallConv}.
    See the method [llvm::CallInst::setCallingConv]
    and [llvm::InvokeInst::setCallingConv]. *)
val set_instruction_call_conv: int -> llvalue -> unit

(** [add_call_site_attr f a i] adds attribute [a] to the call instruction [ci]
    at position [i]. *)
val add_call_site_attr : llvalue -> llattribute -> AttrIndex.t -> unit

(** [call_site_attr f i] returns the attributes for the call instruction [ci]
    at position [i]. *)
val call_site_attrs : llvalue -> AttrIndex.t -> llattribute array

(** [remove_enum_call_site_attr f k i] removes enum attribute with kind [k]
    from the call instruction [ci] at position [i]. *)
val remove_enum_call_site_attr : llvalue -> llattrkind -> AttrIndex.t -> unit

(** [remove_string_call_site_attr f k i] removes string attribute with kind [k]
    from the call instruction [ci] at position [i]. *)
val remove_string_call_site_attr : llvalue -> string -> AttrIndex.t -> unit


(** {7 Operations on call and invoke instructions (only)} *)

(** [num_arg_operands ci] returns the number of arguments for the call or
    invoke instruction [ci].  See the method
    [llvm::CallInst::getNumArgOperands]. *)
val num_arg_operands : llvalue -> int

(** [is_tail_call ci] is [true] if the call instruction [ci] is flagged as
    eligible for tail call optimization, [false] otherwise.
    See the method [llvm::CallInst::isTailCall]. *)
val is_tail_call : llvalue -> bool

(** [set_tail_call tc ci] flags the call instruction [ci] as eligible for tail
    call optimization if [tc] is [true], clears otherwise.
    See the method [llvm::CallInst::setTailCall]. *)
val set_tail_call : bool -> llvalue -> unit

(** [get_normal_dest ii] is the normal destination basic block of an invoke
    instruction. See the method [llvm::InvokeInst::getNormalDest()]. *)
val get_normal_dest : llvalue -> llbasicblock

(** [get_unwind_dest ii] is the unwind destination basic block of an invoke
    instruction. See the method [llvm::InvokeInst::getUnwindDest()]. *)
val get_unwind_dest : llvalue -> llbasicblock


(** {7 Operations on load/store instructions (only)} *)

(** [is_volatile i] is [true] if the load or store instruction [i] is marked
    as volatile.
    See the methods [llvm::LoadInst::isVolatile] and
    [llvm::StoreInst::isVolatile]. *)
val is_volatile : llvalue -> bool

(** [set_volatile v i] marks the load or store instruction [i] as volatile
    if [v] is [true], unmarks otherwise.
    See the methods [llvm::LoadInst::setVolatile] and
    [llvm::StoreInst::setVolatile]. *)
val set_volatile : bool -> llvalue -> unit

(** {7 Operations on terminators} *)

(** [is_terminator v] returns true if the instruction [v] is a terminator. *)
val is_terminator : llvalue -> bool

(** [successor v i] returns the successor at index [i] for the value [v].
    See the method [llvm::Instruction::getSuccessor]. *)
val successor : llvalue -> int -> llbasicblock

(** [set_successor v i o] sets the successor of the value [v] at the index [i] to
    the value [o].
    See the method [llvm::Instruction::setSuccessor]. *)
val set_successor : llvalue -> int -> llbasicblock -> unit

(** [num_successors v] returns the number of successors for the value [v].
    See the method [llvm::Instruction::getNumSuccessors]. *)
val num_successors : llvalue -> int

(** [successors v] returns the successors of [v]. *)
val successors : llvalue -> llbasicblock array

(** [iter_successors f v] applies function f to each successor [v] in order. Tail recursive. *)
val iter_successors : (llbasicblock -> unit) -> llvalue -> unit

(** [fold_successors f v init] is [f (... (f init vN) ...) v1] where [v1,...,vN] are the successors of [v]. Tail recursive. *)
val fold_successors : (llbasicblock -> 'a -> 'a) -> llvalue -> 'a -> 'a

(** {7 Operations on branches} *)

(** [is_conditional v] returns true if the branch instruction [v] is conditional.
    See the method [llvm::BranchInst::isConditional]. *)
val is_conditional : llvalue -> bool

(** [condition v] return the condition of the branch instruction [v].
    See the method [llvm::BranchInst::getCondition]. *)
val condition : llvalue -> llvalue

(** [set_condition v c] sets the condition of the branch instruction [v] to the value [c].
    See the method [llvm::BranchInst::setCondition]. *)
val set_condition : llvalue -> llvalue -> unit

(** [get_branch c] returns a description of the branch instruction [c]. *)
val get_branch : llvalue ->
  [ `Conditional of llvalue * llbasicblock * llbasicblock
  | `Unconditional of llbasicblock ]
    option

(** {7 Operations on phi nodes} *)

(** [add_incoming (v, bb) pn] adds the value [v] to the phi node [pn] for use
    with branches from [bb]. See the method [llvm::PHINode::addIncoming]. *)
val add_incoming : (llvalue * llbasicblock) -> llvalue -> unit

(** [incoming pn] returns the list of value-block pairs for phi node [pn].
    See the method [llvm::PHINode::getIncomingValue]. *)
val incoming : llvalue -> (llvalue * llbasicblock) list



(** {6 Instruction builders} *)

(** [builder context] creates an instruction builder with no position in
    the context [context]. It is invalid to use this builder until its position
    is set with {!position_before} or {!position_at_end}. See the constructor
    for [llvm::LLVMBuilder]. *)
val builder : llcontext -> llbuilder

(** [builder_at ip] creates an instruction builder positioned at [ip].
    See the constructor for [llvm::LLVMBuilder]. *)
val builder_at : llcontext -> (llbasicblock, llvalue) llpos -> llbuilder

(** [builder_before ins] creates an instruction builder positioned before the
    instruction [isn]. See the constructor for [llvm::LLVMBuilder]. *)
val builder_before : llcontext -> llvalue -> llbuilder

(** [builder_at_end bb] creates an instruction builder positioned at the end of
    the basic block [bb]. See the constructor for [llvm::LLVMBuilder]. *)
val builder_at_end : llcontext -> llbasicblock -> llbuilder

(** [position_builder ip bb] moves the instruction builder [bb] to the position
    [ip].
    See the constructor for [llvm::LLVMBuilder]. *)
val position_builder : (llbasicblock, llvalue) llpos -> llbuilder -> unit

(** [position_builder_before_dbg_records ip bb before_dbg_records] moves the
    instruction builder [bb] to the position [ip], before any debug records
    there.
    See the constructor for [llvm::LLVMBuilder]. *)
val position_builder_before_dbg_records : (llbasicblock, llvalue) llpos ->
                                          llbuilder -> unit

(** [position_before ins b] moves the instruction builder [b] to before the
    instruction [isn]. See the method [llvm::LLVMBuilder::SetInsertPoint]. *)
val position_before : llvalue -> llbuilder -> unit

(** [position_before_dbg_records ins b] moves the instruction builder [b]
    to before the instruction [isn] and any debug records attached to it.
    See the method [llvm::LLVMBuilder::SetInsertPoint]. *)
val position_before_dbg_records : llvalue -> llbuilder -> unit

(** [position_at_end bb b] moves the instruction builder [b] to the end of the
    basic block [bb]. See the method [llvm::LLVMBuilder::SetInsertPoint]. *)
val position_at_end : llbasicblock -> llbuilder -> unit

(** [insertion_block b] returns the basic block that the builder [b] is
    positioned to insert into. Raises [Not_Found] if the instruction builder is
    uninitialized.
    See the method [llvm::LLVMBuilder::GetInsertBlock]. *)
val insertion_block : llbuilder -> llbasicblock

(** [insert_into_builder i name b] inserts the specified instruction [i] at the
    position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::Insert]. *)
val insert_into_builder : llvalue -> string -> llbuilder -> unit


(** {7 Metadata} *)

(** [set_current_debug_location b md] sets the current debug location [md] in
    the builder [b].
    See the method [llvm::IRBuilder::SetDebugLocation]. *)
val set_current_debug_location : llbuilder -> llvalue -> unit

(** [clear_current_debug_location b] clears the current debug location in the
    builder [b]. *)
val clear_current_debug_location : llbuilder -> unit

(** [current_debug_location b] returns the current debug location, or None
    if none is currently set.
    See the method [llvm::IRBuilder::GetDebugLocation]. *)
val current_debug_location : llbuilder -> llvalue option

(** [set_inst_debug_location b i] sets the current debug location of the builder
    [b] to the instruction [i].
    See the method [llvm::IRBuilder::SetInstDebugLocation]. *)
val set_inst_debug_location : llbuilder -> llvalue -> unit


(** {7 Terminators} *)

(** [build_ret_void b] creates a
    [ret void]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateRetVoid]. *)
val build_ret_void : llbuilder -> llvalue

(** [build_ret v b] creates a
    [ret %v]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateRet]. *)
val build_ret : llvalue -> llbuilder -> llvalue

(** [build_aggregate_ret vs b] creates a
    [ret {...} { %v1, %v2, ... } ]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAggregateRet]. *)
val build_aggregate_ret : llvalue array -> llbuilder -> llvalue

(** [build_br bb b] creates a
    [br %bb]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateBr]. *)
val build_br : llbasicblock -> llbuilder -> llvalue

(** [build_cond_br cond tbb fbb b] creates a
    [br %cond, %tbb, %fbb]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateCondBr]. *)
val build_cond_br : llvalue -> llbasicblock -> llbasicblock -> llbuilder ->
                         llvalue

(** [build_switch case elsebb count b] creates an empty
    [switch %case, %elsebb]
    instruction at the position specified by the instruction builder [b] with
    space reserved for [count] cases.
    See the method [llvm::LLVMBuilder::CreateSwitch]. *)
val build_switch : llvalue -> llbasicblock -> int -> llbuilder -> llvalue

(** [build_malloc ty name b] creates an [malloc]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::IRBuilderBase::CreateMalloc]. *)
val build_malloc : lltype -> string -> llbuilder -> llvalue

(** [build_array_malloc ty val name b] creates an [array malloc]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::CallInst::CreateArrayMalloc]. *)
val build_array_malloc : lltype -> llvalue -> string -> llbuilder -> llvalue

(** [build_free p b] creates a [free]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFree]. *)
val build_free : llvalue -> llbuilder -> llvalue

(** [add_case sw onval bb] causes switch instruction [sw] to branch to [bb]
    when its input matches the constant [onval].
    See the method [llvm::SwitchInst::addCase]. **)
val add_case : llvalue -> llvalue -> llbasicblock -> unit

(** [switch_default_dest sw] returns the default destination of the [switch]
    instruction.
    See the method [llvm:;SwitchInst::getDefaultDest]. **)
val switch_default_dest : llvalue -> llbasicblock

(** [build_indirect_br addr count b] creates a
    [indirectbr %addr]
    instruction at the position specified by the instruction builder [b] with
    space reserved for [count] destinations.
    See the method [llvm::LLVMBuilder::CreateIndirectBr]. *)
val build_indirect_br : llvalue -> int -> llbuilder -> llvalue

(** [add_destination br bb] adds the basic block [bb] as a possible branch
    location for the indirectbr instruction [br].
    See the method [llvm::IndirectBrInst::addDestination]. **)
val add_destination : llvalue -> llbasicblock -> unit

(** [build_invoke fnty fn args tobb unwindbb name b] creates an
    [%name = invoke %fn(args) to %tobb unwind %unwindbb]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateInvoke]. *)
val build_invoke : lltype -> llvalue -> llvalue array -> llbasicblock ->
                   llbasicblock -> string -> llbuilder -> llvalue

(** [build_landingpad ty persfn numclauses name b] creates an
    [landingpad]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateLandingPad]. *)
val build_landingpad : lltype -> llvalue -> int -> string -> llbuilder ->
                         llvalue

(** [is_cleanup lp] returns [true] if [landingpad] instruction lp is a cleanup.
    See the method [llvm::LandingPadInst::isCleanup]. *)
val is_cleanup : llvalue -> bool

(** [set_cleanup lp] sets the cleanup flag in the [landingpad]instruction.
    See the method [llvm::LandingPadInst::setCleanup]. *)
val set_cleanup : llvalue -> bool -> unit

(** [add_clause lp clause] adds the clause to the [landingpad]instruction.
    See the method [llvm::LandingPadInst::addClause]. *)
val add_clause : llvalue -> llvalue -> unit

(** [build_resume exn b] builds a [resume exn] instruction
    at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateResume] *)
val build_resume : llvalue -> llbuilder -> llvalue

(** [build_unreachable b] creates an
    [unreachable]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateUnwind]. *)
val build_unreachable : llbuilder -> llvalue


(** {7 Arithmetic} *)

(** [build_add x y name b] creates a
    [%name = add %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAdd]. *)
val build_add : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nsw_add x y name b] creates a
    [%name = nsw add %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNSWAdd]. *)
val build_nsw_add : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nuw_add x y name b] creates a
    [%name = nuw add %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNUWAdd]. *)
val build_nuw_add : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_fadd x y name b] creates a
    [%name = fadd %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFAdd]. *)
val build_fadd : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_sub x y name b] creates a
    [%name = sub %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSub]. *)
val build_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nsw_sub x y name b] creates a
    [%name = nsw sub %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNSWSub]. *)
val build_nsw_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nuw_sub x y name b] creates a
    [%name = nuw sub %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNUWSub]. *)
val build_nuw_sub : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_fsub x y name b] creates a
    [%name = fsub %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFSub]. *)
val build_fsub : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_mul x y name b] creates a
    [%name = mul %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateMul]. *)
val build_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nsw_mul x y name b] creates a
    [%name = nsw mul %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNSWMul]. *)
val build_nsw_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_nuw_mul x y name b] creates a
    [%name = nuw mul %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateNUWMul]. *)
val build_nuw_mul : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_fmul x y name b] creates a
    [%name = fmul %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFMul]. *)
val build_fmul : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_udiv x y name b] creates a
    [%name = udiv %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateUDiv]. *)
val build_udiv : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_sdiv x y name b] creates a
    [%name = sdiv %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSDiv]. *)
val build_sdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_exact_sdiv x y name b] creates a
    [%name = exact sdiv %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateExactSDiv]. *)
val build_exact_sdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_fdiv x y name b] creates a
    [%name = fdiv %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFDiv]. *)
val build_fdiv : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_urem x y name b] creates a
    [%name = urem %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateURem]. *)
val build_urem : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_SRem x y name b] creates a
    [%name = srem %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSRem]. *)
val build_srem : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_frem x y name b] creates a
    [%name = frem %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFRem]. *)
val build_frem : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_shl x y name b] creates a
    [%name = shl %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateShl]. *)
val build_shl : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_lshr x y name b] creates a
    [%name = lshr %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateLShr]. *)
val build_lshr : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_ashr x y name b] creates a
    [%name = ashr %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAShr]. *)
val build_ashr : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_and x y name b] creates a
    [%name = and %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAnd]. *)
val build_and : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_or x y name b] creates a
    [%name = or %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateOr]. *)
val build_or : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_xor x y name b] creates a
    [%name = xor %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateXor]. *)
val build_xor : llvalue -> llvalue -> string -> llbuilder -> llvalue

(** [build_neg x name b] creates a
    [%name = sub 0, %x]
    instruction at the position specified by the instruction builder [b].
    [-0.0] is used for floating point types to compute the correct sign.
    See the method [llvm::LLVMBuilder::CreateNeg]. *)
val build_neg : llvalue -> string -> llbuilder -> llvalue

(** [build_nsw_neg x name b] creates a
    [%name = nsw sub 0, %x]
    instruction at the position specified by the instruction builder [b].
    [-0.0] is used for floating point types to compute the correct sign.
    See the method [llvm::LLVMBuilder::CreateNeg]. *)
val build_nsw_neg : llvalue -> string -> llbuilder -> llvalue

(** [build_nuw_neg x name b] creates a
    [%name = nuw sub 0, %x]
    instruction at the position specified by the instruction builder [b].
    [-0.0] is used for floating point types to compute the correct sign.
    See the method [llvm::LLVMBuilder::CreateNeg]. *)
val build_nuw_neg : llvalue -> string -> llbuilder -> llvalue

(** [build_fneg x name b] creates a
    [%name = fsub 0, %x]
    instruction at the position specified by the instruction builder [b].
    [-0.0] is used for floating point types to compute the correct sign.
    See the method [llvm::LLVMBuilder::CreateFNeg]. *)
val build_fneg : llvalue -> string -> llbuilder -> llvalue

(** [build_xor x name b] creates a
    [%name = xor %x, -1]
    instruction at the position specified by the instruction builder [b].
    [-1] is the correct "all ones" value for the type of [x].
    See the method [llvm::LLVMBuilder::CreateXor]. *)
val build_not : llvalue -> string -> llbuilder -> llvalue


(** {7 Memory} *)

(** [build_alloca ty name b] creates a
    [%name = alloca %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAlloca]. *)
val build_alloca : lltype -> string -> llbuilder -> llvalue

(** [build_array_alloca ty n name b] creates a
    [%name = alloca %ty, %n]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateAlloca]. *)
val build_array_alloca : lltype -> llvalue -> string -> llbuilder ->
                              llvalue

(** [build_load ty v name b] creates a
    [%name = load %ty, %v]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateLoad]. *)
val build_load : lltype -> llvalue -> string -> llbuilder -> llvalue

(** [build_store v p b] creates a
    [store %v, %p]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateStore]. *)
val build_store : llvalue -> llvalue -> llbuilder -> llvalue

(** [build_atomicrmw op ptr val o st b] creates an [atomicrmw] instruction with
    operation [op] performed on pointer [ptr] and value [val] with ordering [o]
    and singlethread flag set to [st] at the position specified by
    the instruction builder [b].
    See the method [llvm::IRBuilder::CreateAtomicRMW]. *)
val build_atomicrmw : AtomicRMWBinOp.t -> llvalue -> llvalue ->
                      AtomicOrdering.t -> bool -> string -> llbuilder -> llvalue

(** [build_gep srcty p indices name b] creates a
    [%name = getelementptr srcty, %p, indices...]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateGetElementPtr]. *)
val build_gep : lltype -> llvalue -> llvalue array -> string -> llbuilder ->
                      llvalue

(** [build_in_bounds_gep srcty p indices name b] creates a
    [%name = gelementptr inbounds srcty, %p, indices...]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateInBoundsGetElementPtr]. *)
val build_in_bounds_gep : lltype -> llvalue -> llvalue array -> string ->
                               llbuilder -> llvalue

(** [build_struct_gep srcty p idx name b] creates a
    [%name = getelementptr srcty, %p, 0, idx]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateStructGetElementPtr]. *)
val build_struct_gep : lltype -> llvalue -> int -> string -> llbuilder ->
                           llvalue

(** [build_global_string str name b] creates a series of instructions that adds
    a global string at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateGlobalString]. *)
val build_global_string : string -> string -> llbuilder -> llvalue

(** [build_global_stringptr str name b] creates a series of instructions that
    adds a global string pointer at the position specified by the instruction
    builder [b].
    See the method [llvm::LLVMBuilder::CreateGlobalStringPtr]. *)
val build_global_stringptr : string -> string -> llbuilder -> llvalue


(** {7 Casts} *)

(** [build_trunc v ty name b] creates a
    [%name = trunc %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateTrunc]. *)
val build_trunc : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_zext v ty name b] creates a
    [%name = zext %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateZExt]. *)
val build_zext : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_sext v ty name b] creates a
    [%name = sext %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSExt]. *)
val build_sext : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_fptoui v ty name b] creates a
    [%name = fptoui %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFPToUI]. *)
val build_fptoui : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_fptosi v ty name b] creates a
    [%name = fptosi %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFPToSI]. *)
val build_fptosi : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_uitofp v ty name b] creates a
    [%name = uitofp %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateUIToFP]. *)
val build_uitofp : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_sitofp v ty name b] creates a
    [%name = sitofp %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSIToFP]. *)
val build_sitofp : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_fptrunc v ty name b] creates a
    [%name = fptrunc %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFPTrunc]. *)
val build_fptrunc : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_fpext v ty name b] creates a
    [%name = fpext %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFPExt]. *)
val build_fpext : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_ptrtoint v ty name b] creates a
    [%name = prtotint %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreatePtrToInt]. *)
val build_ptrtoint : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_inttoptr v ty name b] creates a
    [%name = inttoptr %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateIntToPtr]. *)
val build_inttoptr : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_bitcast v ty name b] creates a
    [%name = bitcast %p to %ty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateBitCast]. *)
val build_bitcast : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_zext_or_bitcast v ty name b] creates a zext or bitcast
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateZExtOrBitCast]. *)
val build_zext_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                 llvalue

(** [build_sext_or_bitcast v ty name b] creates a sext or bitcast
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSExtOrBitCast]. *)
val build_sext_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                 llvalue

(** [build_trunc_or_bitcast v ty name b] creates a trunc or bitcast
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateZExtOrBitCast]. *)
val build_trunc_or_bitcast : llvalue -> lltype -> string -> llbuilder ->
                                  llvalue

(** [build_pointercast v ty name b] creates a bitcast or pointer-to-int
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreatePointerCast]. *)
val build_pointercast : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_intcast v ty name b] creates a zext, bitcast, or trunc
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateIntCast]. *)
val build_intcast : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_fpcast v ty name b] creates a fpext, bitcast, or fptrunc
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFPCast]. *)
val build_fpcast : llvalue -> lltype -> string -> llbuilder -> llvalue


(** {7 Comparisons} *)

(** [build_icmp pred x y name b] creates a
    [%name = icmp %pred %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateICmp]. *)
val build_icmp : Icmp.t -> llvalue -> llvalue -> string ->
                      llbuilder -> llvalue

(** [build_fcmp pred x y name b] creates a
    [%name = fcmp %pred %x, %y]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFCmp]. *)
val build_fcmp : Fcmp.t -> llvalue -> llvalue -> string ->
                      llbuilder -> llvalue


(** {7 Miscellaneous instructions} *)

(** [build_phi incoming name b] creates a
    [%name = phi %incoming]
    instruction at the position specified by the instruction builder [b].
    [incoming] is a list of [(llvalue, llbasicblock)] tuples.
    See the method [llvm::LLVMBuilder::CreatePHI]. *)
val build_phi : (llvalue * llbasicblock) list -> string -> llbuilder ->
                     llvalue

(** [build_empty_phi ty name b] creates a
    [%name = phi %ty] instruction at the position specified by
    the instruction builder [b]. [ty] is the type of the instruction.
    See the method [llvm::LLVMBuilder::CreatePHI]. *)
val build_empty_phi : lltype -> string -> llbuilder -> llvalue

(** [build_call fnty fn args name b] creates a
    [%name = call %fn(args...)]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateCall]. *)
val build_call : lltype -> llvalue -> llvalue array -> string -> llbuilder ->
                       llvalue

(** [build_select cond thenv elsev name b] creates a
    [%name = select %cond, %thenv, %elsev]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateSelect]. *)
val build_select : llvalue -> llvalue -> llvalue -> string -> llbuilder ->
                        llvalue

(** [build_va_arg valist argty name b] creates a
    [%name = va_arg %valist, %argty]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateVAArg]. *)
val build_va_arg : llvalue -> lltype -> string -> llbuilder -> llvalue

(** [build_extractelement vec i name b] creates a
    [%name = extractelement %vec, %i]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateExtractElement]. *)
val build_extractelement : llvalue -> llvalue -> string -> llbuilder ->
                                llvalue

(** [build_insertelement vec elt i name b] creates a
    [%name = insertelement %vec, %elt, %i]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateInsertElement]. *)
val build_insertelement : llvalue -> llvalue -> llvalue -> string ->
                               llbuilder -> llvalue

(** [build_shufflevector veca vecb mask name b] creates a
    [%name = shufflevector %veca, %vecb, %mask]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateShuffleVector]. *)
val build_shufflevector : llvalue -> llvalue -> llvalue -> string ->
                               llbuilder -> llvalue

(** [build_extractvalue agg idx name b] creates a
    [%name = extractvalue %agg, %idx]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateExtractValue]. *)
val build_extractvalue : llvalue -> int -> string -> llbuilder -> llvalue


(** [build_insertvalue agg val idx name b] creates a
    [%name = insertvalue %agg, %val, %idx]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateInsertValue]. *)
val build_insertvalue : llvalue -> llvalue -> int -> string -> llbuilder ->
                             llvalue

(** [build_is_null val name b] creates a
    [%name = icmp eq %val, null]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateIsNull]. *)
val build_is_null : llvalue -> string -> llbuilder -> llvalue

(** [build_is_not_null val name b] creates a
    [%name = icmp ne %val, null]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateIsNotNull]. *)
val build_is_not_null : llvalue -> string -> llbuilder -> llvalue

(** [build_ptrdiff elemty lhs rhs name b] creates a series of instructions
    that measure the difference between two pointer values in multiples of
    [elemty] at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreatePtrDiff]. *)
val build_ptrdiff : lltype -> llvalue -> llvalue -> string -> llbuilder ->
                    llvalue

(** [build_freeze x name b] creates a
    [%name = freeze %x]
    instruction at the position specified by the instruction builder [b].
    See the method [llvm::LLVMBuilder::CreateFreeze]. *)
val build_freeze : llvalue -> string -> llbuilder -> llvalue


(** {6 Memory buffers} *)

module MemoryBuffer : sig
  (** [of_file p] is the memory buffer containing the contents of the file at
      path [p]. If the file could not be read, then [IoError msg] is
      raised. *)
  val of_file : string -> llmemorybuffer

  (** [of_stdin ()] is the memory buffer containing the contents of standard input.
      If standard input is empty, then [IoError msg] is raised. *)
  val of_stdin : unit -> llmemorybuffer

  (** [of_string ~name s] is the memory buffer containing the contents of string [s].
      The name of memory buffer is set to [name] if it is provided. *)
  val of_string : ?name:string -> string -> llmemorybuffer

  (** [as_string mb] is the string containing the contents of memory buffer [mb]. *)
  val as_string : llmemorybuffer -> string

  (** Disposes of a memory buffer. *)
  val dispose : llmemorybuffer -> unit
end
