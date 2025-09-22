/*===-- llvm_ocaml.c - LLVM OCaml Glue --------------------------*- C++ -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This file glues LLVM's OCaml interface to its C interface. These functions *|
|* are by and large transparent wrappers to the corresponding C functions.    *|
|*                                                                            *|
|* Note that these functions intentionally take liberties with the CAMLparamX *|
|* macros, since most of the parameters are not GC heap objects.              *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "llvm-c/Core.h"
#include "llvm-c/Support.h"
#include "llvm/Config/llvm-config.h"
#include "caml/memory.h"
#include "caml/fail.h"
#include "caml/callback.h"
#include "llvm_ocaml.h"

#if OCAML_VERSION < 41200
value caml_alloc_some(value v) {
  CAMLparam1(v);
  value Some = caml_alloc_small(1, 0);
  Field(Some, 0) = v;
  CAMLreturn(Some);
}
#endif

value caml_alloc_tuple_uninit(mlsize_t wosize) {
  if (wosize <= Max_young_wosize) {
    return caml_alloc_small(wosize, 0);
  } else {
    return caml_alloc_shr(wosize, 0);
  }
}

value to_val(void *ptr) {
  assert((((value)ptr) & 1) == 0 &&
         "OCaml bindings assume LLVM objects are at least 2-byte aligned");
  return ((value)ptr) | 1;
}

void *from_val(value v) {
  assert(Is_long(v) && "OCaml values representing LLVM objects should have the "
                       "low bit set so that the OCaml GC "
                       "treats them as tagged integers");
  return (void *)(v ^ 1);
}

value llvm_string_of_message(char *Message) {
  value String = caml_copy_string(Message);
  LLVMDisposeMessage(Message);

  return String;
}

value ptr_to_option(void *Ptr) {
  if (!Ptr)
    return Val_none;
  return caml_alloc_some(to_val(Ptr));
}

value cstr_to_string(const char *Str, mlsize_t Len) {
  if (!Str)
    return caml_alloc_string(0);
  value String = caml_alloc_string(Len);
  memcpy((char *)String_val(String), Str, Len);
  return String;
}

value cstr_to_string_option(const char *CStr, mlsize_t Len) {
  if (!CStr)
    return Val_none;
  value String = caml_alloc_string(Len);
  memcpy((char *)String_val(String), CStr, Len);
  return caml_alloc_some(String);
}

void llvm_raise(value Prototype, char *Message) {
  caml_raise_with_arg(Prototype, llvm_string_of_message(Message));
}

static value llvm_fatal_error_handler;

static void llvm_fatal_error_trampoline(const char *Reason) {
  caml_callback(llvm_fatal_error_handler, caml_copy_string(Reason));
}

value llvm_install_fatal_error_handler(value Handler) {
  LLVMInstallFatalErrorHandler(llvm_fatal_error_trampoline);
  llvm_fatal_error_handler = Handler;
  caml_register_global_root(&llvm_fatal_error_handler);
  return Val_unit;
}

value llvm_reset_fatal_error_handler(value Unit) {
  caml_remove_global_root(&llvm_fatal_error_handler);
  LLVMResetFatalErrorHandler();
  return Val_unit;
}

value llvm_enable_pretty_stacktrace(value Unit) {
  LLVMEnablePrettyStackTrace();
  return Val_unit;
}

value llvm_parse_command_line_options(value Overview, value Args) {
  const char *COverview;
  if (Overview == Val_int(0)) {
    COverview = NULL;
  } else {
    COverview = String_val(Field(Overview, 0));
  }
  LLVMParseCommandLineOptions(Wosize_val(Args),
                              (const char *const *)Op_val(Args), COverview);
  return Val_unit;
}

void *from_val_array(value Elements) {
  mlsize_t Length = Wosize_val(Elements);
  void **Temp = malloc(sizeof(void *) * Length);
  if (Temp == NULL)
    caml_raise_out_of_memory();
  for (unsigned I = 0; I < Length; ++I) {
    Temp[I] = from_val(Field(Elements, I));
  }
  return Temp;
}

static value alloc_variant(int tag, value Value) {
  value Iter = caml_alloc_small(1, tag);
  Field(Iter, 0) = Value;
  return Iter;
}

/* Macro to convert the C first/next/last/prev idiom to the Ocaml llpos/
   llrev_pos idiom. */
#define DEFINE_ITERATORS(camlname, cname, pty_val, cty, cty_val, pfun)         \
  /* llmodule -> ('a, 'b) llpos */                                             \
  value llvm_##camlname##_begin(value Mom) {                                   \
    cty First = LLVMGetFirst##cname(pty_val(Mom));                             \
    if (First)                                                                 \
      return alloc_variant(1, to_val(First));                                  \
    return alloc_variant(0, Mom);                                              \
  }                                                                            \
                                                                               \
  /* llvalue -> ('a, 'b) llpos */                                              \
  value llvm_##camlname##_succ(value Kid) {                                    \
    cty Next = LLVMGetNext##cname(cty_val(Kid));                               \
    if (Next)                                                                  \
      return alloc_variant(1, to_val(Next));                                   \
    return alloc_variant(0, to_val(pfun(cty_val(Kid))));                       \
  }                                                                            \
                                                                               \
  /* llmodule -> ('a, 'b) llrev_pos */                                         \
  value llvm_##camlname##_end(value Mom) {                                     \
    cty Last = LLVMGetLast##cname(pty_val(Mom));                               \
    if (Last)                                                                  \
      return alloc_variant(1, to_val(Last));                                   \
    return alloc_variant(0, Mom);                                              \
  }                                                                            \
                                                                               \
  /* llvalue -> ('a, 'b) llrev_pos */                                          \
  value llvm_##camlname##_pred(value Kid) {                                    \
    cty Prev = LLVMGetPrevious##cname(cty_val(Kid));                           \
    if (Prev)                                                                  \
      return alloc_variant(1, to_val(Prev));                                   \
    return alloc_variant(0, to_val(pfun(cty_val(Kid))));                       \
  }

/*===-- Context error handling --------------------------------------------===*/

void llvm_diagnostic_handler_trampoline(LLVMDiagnosticInfoRef DI,
                                        void *DiagnosticContext) {
  caml_callback(*((value *)DiagnosticContext), to_val(DI));
}

/* Diagnostic.t -> string */
value llvm_get_diagnostic_description(value Diagnostic) {
  return llvm_string_of_message(
      LLVMGetDiagInfoDescription(DiagnosticInfo_val(Diagnostic)));
}

/* Diagnostic.t -> DiagnosticSeverity.t */
value llvm_get_diagnostic_severity(value Diagnostic) {
  return Val_int(LLVMGetDiagInfoSeverity(DiagnosticInfo_val(Diagnostic)));
}

static void llvm_remove_diagnostic_handler(value C) {
  CAMLparam1(C);
  LLVMContextRef context = Context_val(C);
  if (LLVMContextGetDiagnosticHandler(context) ==
      llvm_diagnostic_handler_trampoline) {
    value *Handler = (value *)LLVMContextGetDiagnosticContext(context);
    caml_remove_global_root(Handler);
    free(Handler);
  }
  CAMLreturn0;
}

/* llcontext -> (Diagnostic.t -> unit) option -> unit */
value llvm_set_diagnostic_handler(value C, value Handler) {
  CAMLparam2(C, Handler);
  LLVMContextRef context = Context_val(C);
  llvm_remove_diagnostic_handler(C);
  if (Handler == Val_none) {
    LLVMContextSetDiagnosticHandler(context, NULL, NULL);
  } else {
    value *DiagnosticContext = malloc(sizeof(value));
    if (DiagnosticContext == NULL)
      caml_raise_out_of_memory();
    caml_register_global_root(DiagnosticContext);
    *DiagnosticContext = Field(Handler, 0);
    LLVMContextSetDiagnosticHandler(context, llvm_diagnostic_handler_trampoline,
                                    DiagnosticContext);
  }
  CAMLreturn(Val_unit);
}

/*===-- Contexts ----------------------------------------------------------===*/

/* unit -> llcontext */
value llvm_create_context(value Unit) { return to_val(LLVMContextCreate()); }

/* llcontext -> unit */
value llvm_dispose_context(value C) {
  llvm_remove_diagnostic_handler(C);
  LLVMContextDispose(Context_val(C));
  return Val_unit;
}

/* unit -> llcontext */
value llvm_global_context(value Unit) { return to_val(LLVMGetGlobalContext()); }

/* llcontext -> string -> int */
value llvm_mdkind_id(value C, value Name) {
  unsigned MDKindID = LLVMGetMDKindIDInContext(Context_val(C), String_val(Name),
                                               caml_string_length(Name));
  return Val_int(MDKindID);
}

/*===-- Attributes --------------------------------------------------------===*/

/* string -> llattrkind */
value llvm_enum_attr_kind(value Name) {
  unsigned Kind = LLVMGetEnumAttributeKindForName(String_val(Name),
                                                  caml_string_length(Name));
  if (Kind == 0)
    caml_raise_with_arg(*caml_named_value("Llvm.UnknownAttribute"), Name);
  return Val_int(Kind);
}

/* llcontext -> int -> int64 -> llattribute */
value llvm_create_enum_attr_by_kind(value C, value Kind, value Value) {
  return to_val(
      LLVMCreateEnumAttribute(Context_val(C), Int_val(Kind), Int64_val(Value)));
}

/* llattribute -> bool */
value llvm_is_enum_attr(value A) {
  return Val_int(LLVMIsEnumAttribute(Attribute_val(A)));
}

/* llattribute -> llattrkind */
value llvm_get_enum_attr_kind(value A) {
  return Val_int(LLVMGetEnumAttributeKind(Attribute_val(A)));
}

/* llattribute -> int64 */
value llvm_get_enum_attr_value(value A) {
  return caml_copy_int64(LLVMGetEnumAttributeValue(Attribute_val(A)));
}

/* llcontext -> kind:string -> name:string -> llattribute */
value llvm_create_string_attr(value C, value Kind, value Value) {
  return to_val(LLVMCreateStringAttribute(
      Context_val(C), String_val(Kind), caml_string_length(Kind),
      String_val(Value), caml_string_length(Value)));
}

/* llattribute -> bool */
value llvm_is_string_attr(value A) {
  return Val_int(LLVMIsStringAttribute(Attribute_val(A)));
}

/* llattribute -> string */
value llvm_get_string_attr_kind(value A) {
  unsigned Length;
  const char *String = LLVMGetStringAttributeKind(Attribute_val(A), &Length);
  return cstr_to_string(String, Length);
}

/* llattribute -> string */
value llvm_get_string_attr_value(value A) {
  unsigned Length;
  const char *String = LLVMGetStringAttributeValue(Attribute_val(A), &Length);
  return cstr_to_string(String, Length);
}

/*===-- Modules -----------------------------------------------------------===*/

/* llcontext -> string -> llmodule */
value llvm_create_module(value C, value ModuleID) {
  return to_val(
      LLVMModuleCreateWithNameInContext(String_val(ModuleID), Context_val(C)));
}

/* llmodule -> unit */
value llvm_dispose_module(value M) {
  LLVMDisposeModule(Module_val(M));
  return Val_unit;
}

/* llmodule -> string */
value llvm_target_triple(value M) {
  return caml_copy_string(LLVMGetTarget(Module_val(M)));
}

/* string -> llmodule -> unit */
value llvm_set_target_triple(value Trip, value M) {
  LLVMSetTarget(Module_val(M), String_val(Trip));
  return Val_unit;
}

/* llmodule -> string */
value llvm_data_layout(value M) {
  return caml_copy_string(LLVMGetDataLayout(Module_val(M)));
}

/* string -> llmodule -> unit */
value llvm_set_data_layout(value Layout, value M) {
  LLVMSetDataLayout(Module_val(M), String_val(Layout));
  return Val_unit;
}

/* llmodule -> unit */
value llvm_dump_module(value M) {
  LLVMDumpModule(Module_val(M));
  return Val_unit;
}

/* string -> llmodule -> unit */
value llvm_print_module(value Filename, value M) {
  char *Message;

  if (LLVMPrintModuleToFile(Module_val(M), String_val(Filename), &Message))
    llvm_raise(*caml_named_value("Llvm.IoError"), Message);

  return Val_unit;
}

/* llmodule -> string */
value llvm_string_of_llmodule(value M) {
  char *ModuleCStr = LLVMPrintModuleToString(Module_val(M));
  value ModuleStr = caml_copy_string(ModuleCStr);
  LLVMDisposeMessage(ModuleCStr);

  return ModuleStr;
}

/* llmodule -> llcontext */
value llvm_get_module_context(value M) {
  LLVMContextRef C = LLVMGetModuleContext(Module_val(M));
  return to_val(C);
}

/* llmodule -> string */
value llvm_get_module_identifier(value M) {
  size_t Len;
  const char *Name = LLVMGetModuleIdentifier(Module_val(M), &Len);
  return cstr_to_string(Name, (mlsize_t)Len);
}

/* llmodule -> string -> unit */
value llvm_set_module_identifier(value M, value Id) {
  LLVMSetModuleIdentifier(Module_val(M), String_val(Id),
                          caml_string_length(Id));
  return Val_unit;
}

/* llmodule -> string -> unit */
value llvm_set_module_inline_asm(value M, value Asm) {
  LLVMSetModuleInlineAsm(Module_val(M), String_val(Asm));
  return Val_unit;
}

/* llmodule -> string -> llmetadata option */
value llvm_get_module_flag(value M, value Key) {
  return ptr_to_option(LLVMGetModuleFlag(Module_val(M), String_val(Key),
                                         caml_string_length(Key)));
}

/* llmodule -> ModuleFlagBehavior.t -> string -> llmetadata -> unit */
value llvm_add_module_flag(value M, value Behaviour, value Key, value Val) {
  LLVMAddModuleFlag(Module_val(M), Int_val(Behaviour), String_val(Key),
                    caml_string_length(Key), Metadata_val(Val));
  return Val_unit;
}

/*===-- Types -------------------------------------------------------------===*/

/* lltype -> TypeKind.t */
value llvm_classify_type(value Ty) {
  return Val_int(LLVMGetTypeKind(Type_val(Ty)));
}

/* lltype -> bool */
value llvm_type_is_sized(value Ty) {
  return Val_bool(LLVMTypeIsSized(Type_val(Ty)));
}

/* lltype -> llcontext */
value llvm_type_context(value Ty) {
  return to_val(LLVMGetTypeContext(Type_val(Ty)));
}

/* lltype -> unit */
value llvm_dump_type(value Val) {
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  LLVMDumpType(Type_val(Val));
#else
  caml_raise_with_arg(*caml_named_value("Llvm.FeatureDisabled"),
                      caml_copy_string("dump"));
#endif
  return Val_unit;
}

/* lltype -> string */
value llvm_string_of_lltype(value M) {
  char *TypeCStr = LLVMPrintTypeToString(Type_val(M));
  value TypeStr = caml_copy_string(TypeCStr);
  LLVMDisposeMessage(TypeCStr);

  return TypeStr;
}

/*--... Operations on integer types ........................................--*/

/* llcontext -> lltype */
value llvm_i1_type(value Context) {
  return to_val(LLVMInt1TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_i8_type(value Context) {
  return to_val(LLVMInt8TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_i16_type(value Context) {
  return to_val(LLVMInt16TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_i32_type(value Context) {
  return to_val(LLVMInt32TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_i64_type(value Context) {
  return to_val(LLVMInt64TypeInContext(Context_val(Context)));
}

/* llcontext -> int -> lltype */
value llvm_integer_type(value Context, value Width) {
  return to_val(LLVMIntTypeInContext(Context_val(Context), Int_val(Width)));
}

/* lltype -> int */
value llvm_integer_bitwidth(value IntegerTy) {
  return Val_int(LLVMGetIntTypeWidth(Type_val(IntegerTy)));
}

/*--... Operations on real types ...........................................--*/

/* llcontext -> lltype */
value llvm_float_type(value Context) {
  return to_val(LLVMFloatTypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_double_type(value Context) {
  return to_val(LLVMDoubleTypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_x86fp80_type(value Context) {
  return to_val(LLVMX86FP80TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_fp128_type(value Context) {
  return to_val(LLVMFP128TypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_ppc_fp128_type(value Context) {
  return to_val(LLVMPPCFP128TypeInContext(Context_val(Context)));
}

/*--... Operations on function types .......................................--*/

/* lltype -> lltype array -> lltype */
value llvm_function_type(value RetTy, value ParamTys) {
  mlsize_t len = Wosize_val(ParamTys);
  LLVMTypeRef *Temp = from_val_array(ParamTys);
  LLVMTypeRef Type = LLVMFunctionType(Type_val(RetTy), Temp, len, 0);
  free(Temp);
  return to_val(Type);
}

/* lltype -> lltype array -> lltype */
value llvm_var_arg_function_type(value RetTy, value ParamTys) {
  mlsize_t len = Wosize_val(ParamTys);
  LLVMTypeRef *Temp = from_val_array(ParamTys);
  LLVMTypeRef Type = LLVMFunctionType(Type_val(RetTy), Temp, len, 1);
  free(Temp);
  return to_val(Type);
}

/* lltype -> bool */
value llvm_is_var_arg(value FunTy) {
  return Val_bool(LLVMIsFunctionVarArg(Type_val(FunTy)));
}

/* lltype -> lltype */
value llvm_return_type(value FunTy) {
  LLVMTypeRef Type = LLVMGetReturnType(Type_val(FunTy));
  return to_val(Type);
}

/* lltype -> lltype array */
value llvm_param_types(value FunTy) {
  unsigned Length = LLVMCountParamTypes(Type_val(FunTy));
  value Tys = caml_alloc_tuple_uninit(Length);
  LLVMGetParamTypes(Type_val(FunTy), (LLVMTypeRef *)Op_val(Tys));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Tys, I) = to_val((LLVMTypeRef)Field(Tys, I));
  }
  return Tys;
}

/*--... Operations on struct types .........................................--*/

/* llcontext -> lltype array -> lltype */
value llvm_struct_type(value C, value ElementTypes) {
  mlsize_t Length = Wosize_val(ElementTypes);
  LLVMTypeRef *Temp = from_val_array(ElementTypes);
  LLVMTypeRef Type = LLVMStructTypeInContext(Context_val(C), Temp, Length, 0);
  free(Temp);
  return to_val(Type);
}

/* llcontext -> lltype array -> lltype */
value llvm_packed_struct_type(value C, value ElementTypes) {
  mlsize_t Length = Wosize_val(ElementTypes);
  LLVMTypeRef *Temp = from_val_array(ElementTypes);
  LLVMTypeRef Type = LLVMStructTypeInContext(Context_val(C), Temp, Length, 1);
  free(Temp);
  return to_val(Type);
}

/* llcontext -> string -> lltype */
value llvm_named_struct_type(value C, value Name) {
  return to_val(LLVMStructCreateNamed(Context_val(C), String_val(Name)));
}

/* lltype -> lltype array -> bool -> unit */
value llvm_struct_set_body(value Ty, value ElementTypes, value Packed) {
  mlsize_t Length = Wosize_val(ElementTypes);
  LLVMTypeRef *Temp = from_val_array(ElementTypes);
  LLVMStructSetBody(Type_val(Ty), Temp, Length, Bool_val(Packed));
  return Val_unit;
}

/* lltype -> string option */
value llvm_struct_name(value Ty) {
  const char *CStr = LLVMGetStructName(Type_val(Ty));
  size_t Len;
  if (!CStr)
    return Val_none;
  Len = strlen(CStr);
  return cstr_to_string_option(CStr, Len);
}

/* lltype -> lltype array */
value llvm_struct_element_types(value StructTy) {
  unsigned Length = LLVMCountStructElementTypes(Type_val(StructTy));
  value Tys = caml_alloc_tuple_uninit(Length);
  LLVMGetStructElementTypes(Type_val(StructTy), (LLVMTypeRef *)Op_val(Tys));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Tys, I) = to_val((LLVMTypeRef)Field(Tys, I));
  }
  return Tys;
}

/* lltype -> bool */
value llvm_is_packed(value StructTy) {
  return Val_bool(LLVMIsPackedStruct(Type_val(StructTy)));
}

/* lltype -> bool */
value llvm_is_opaque(value StructTy) {
  return Val_bool(LLVMIsOpaqueStruct(Type_val(StructTy)));
}

/* lltype -> bool */
value llvm_is_literal(value StructTy) {
  return Val_bool(LLVMIsLiteralStruct(Type_val(StructTy)));
}

/*--... Operations on array, pointer, and vector types .....................--*/

/* lltype -> lltype array */
value llvm_subtypes(value Ty) {
  unsigned Length = LLVMGetNumContainedTypes(Type_val(Ty));
  value Arr = caml_alloc_tuple_uninit(Length);
  LLVMGetSubtypes(Type_val(Ty), (LLVMTypeRef *)Op_val(Arr));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Arr, I) = to_val((LLVMTypeRef)Field(Arr, I));
  }
  return Arr;
}

/* lltype -> int -> lltype */
value llvm_array_type(value ElementTy, value Count) {
  return to_val(LLVMArrayType(Type_val(ElementTy), Int_val(Count)));
}

/* llcontext -> lltype */
value llvm_pointer_type(value C) {
  LLVMTypeRef Type = LLVMPointerTypeInContext(Context_val(C), 0);
  return to_val(Type);
}

/* llcontext -> int -> lltype */
value llvm_qualified_pointer_type(value C, value AddressSpace) {
  LLVMTypeRef Type =
      LLVMPointerTypeInContext(Context_val(C), Int_val(AddressSpace));
  return to_val(Type);
}

/* lltype -> int -> lltype */
value llvm_vector_type(value ElementTy, value Count) {
  return to_val(LLVMVectorType(Type_val(ElementTy), Int_val(Count)));
}

/* lltype -> lltype */
value llvm_get_element_type(value Ty) {
  return to_val(LLVMGetElementType(Type_val(Ty)));
}

/* lltype -> int */
value llvm_array_length(value ArrayTy) {
  return Val_int(LLVMGetArrayLength2(Type_val(ArrayTy)));
}

/* lltype -> int */
value llvm_address_space(value PtrTy) {
  return Val_int(LLVMGetPointerAddressSpace(Type_val(PtrTy)));
}

/* lltype -> int */
value llvm_vector_size(value VectorTy) {
  return Val_int(LLVMGetVectorSize(Type_val(VectorTy)));
}

/*--... Operations on other types ..........................................--*/

/* llcontext -> lltype */
value llvm_void_type(value Context) {
  return to_val(LLVMVoidTypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_label_type(value Context) {
  return to_val(LLVMLabelTypeInContext(Context_val(Context)));
}

/* llcontext -> lltype */
value llvm_x86_mmx_type(value Context) {
  return to_val(LLVMX86MMXTypeInContext(Context_val(Context)));
}

/* llmodule -> string -> lltype option */
value llvm_type_by_name(value M, value Name) {
  return ptr_to_option(LLVMGetTypeByName(Module_val(M), String_val(Name)));
}

/*===-- VALUES ------------------------------------------------------------===*/

/* llvalue -> lltype */
value llvm_type_of(value Val) { return to_val(LLVMTypeOf(Value_val(Val))); }

/* keep in sync with ValueKind.t */
enum ValueKind {
  NullValue = 0,
  Argument,
  BasicBlock,
  InlineAsm,
  MDNode,
  MDString,
  BlockAddress,
  ConstantAggregateZero,
  ConstantArray,
  ConstantDataArray,
  ConstantDataVector,
  ConstantExpr,
  ConstantFP,
  ConstantInt,
  ConstantPointerNull,
  ConstantStruct,
  ConstantVector,
  Function,
  GlobalAlias,
  GlobalIFunc,
  GlobalVariable,
  UndefValue,
  PoisonValue,
  Instruction
};

/* llvalue -> ValueKind.t */
#define DEFINE_CASE(Val, Kind)                                                 \
  do {                                                                         \
    if (LLVMIsA##Kind(Val))                                                    \
      return Val_int(Kind);                                                    \
  } while (0)

value llvm_classify_value(value V) {
  LLVMValueRef Val = Value_val(V);
  if (!Val)
    return Val_int(NullValue);
  if (LLVMIsAConstant(Val)) {
    DEFINE_CASE(Val, BlockAddress);
    DEFINE_CASE(Val, ConstantAggregateZero);
    DEFINE_CASE(Val, ConstantArray);
    DEFINE_CASE(Val, ConstantDataArray);
    DEFINE_CASE(Val, ConstantDataVector);
    DEFINE_CASE(Val, ConstantExpr);
    DEFINE_CASE(Val, ConstantFP);
    DEFINE_CASE(Val, ConstantInt);
    DEFINE_CASE(Val, ConstantPointerNull);
    DEFINE_CASE(Val, ConstantStruct);
    DEFINE_CASE(Val, ConstantVector);
  }
  if (LLVMIsAInstruction(Val)) {
    value result = caml_alloc_small(1, 0);
    Field(result, 0) = Val_int(LLVMGetInstructionOpcode(Val));
    return result;
  }
  if (LLVMIsAGlobalValue(Val)) {
    DEFINE_CASE(Val, Function);
    DEFINE_CASE(Val, GlobalAlias);
    DEFINE_CASE(Val, GlobalIFunc);
    DEFINE_CASE(Val, GlobalVariable);
  }
  DEFINE_CASE(Val, Argument);
  DEFINE_CASE(Val, BasicBlock);
  DEFINE_CASE(Val, InlineAsm);
  DEFINE_CASE(Val, MDNode);
  DEFINE_CASE(Val, MDString);
  DEFINE_CASE(Val, UndefValue);
  DEFINE_CASE(Val, PoisonValue);
  caml_failwith("Unknown Value class");
}

/* llvalue -> string */
value llvm_value_name(value Val) {
  return caml_copy_string(LLVMGetValueName(Value_val(Val)));
}

/* string -> llvalue -> unit */
value llvm_set_value_name(value Name, value Val) {
  LLVMSetValueName(Value_val(Val), String_val(Name));
  return Val_unit;
}

/* llvalue -> unit */
value llvm_dump_value(value Val) {
  LLVMDumpValue(Value_val(Val));
  return Val_unit;
}

/* llvalue -> string */
value llvm_string_of_llvalue(value M) {
  char *ValueCStr = LLVMPrintValueToString(Value_val(M));
  value ValueStr = caml_copy_string(ValueCStr);
  LLVMDisposeMessage(ValueCStr);

  return ValueStr;
}

/* lldbgrecord -> string */
value llvm_string_of_lldbgrecord(value Record) {
  char *ValueCStr = LLVMPrintDbgRecordToString(DbgRecord_val(Record));
  value ValueStr = caml_copy_string(ValueCStr);
  LLVMDisposeMessage(ValueCStr);

  return ValueStr;
}

/* llvalue -> llvalue -> unit */
value llvm_replace_all_uses_with(value OldVal, value NewVal) {
  LLVMReplaceAllUsesWith(Value_val(OldVal), Value_val(NewVal));
  return Val_unit;
}

/*--... Operations on users ................................................--*/

/* llvalue -> int -> llvalue */
value llvm_operand(value V, value I) {
  return to_val(LLVMGetOperand(Value_val(V), Int_val(I)));
}

/* llvalue -> int -> lluse */
value llvm_operand_use(value V, value I) {
  return to_val(LLVMGetOperandUse(Value_val(V), Int_val(I)));
}

/* llvalue -> int -> llvalue -> unit */
value llvm_set_operand(value U, value I, value V) {
  LLVMSetOperand(Value_val(U), Int_val(I), Value_val(V));
  return Val_unit;
}

/* llvalue -> int */
value llvm_num_operands(value V) {
  return Val_int(LLVMGetNumOperands(Value_val(V)));
}

/* llvalue -> int array */
value llvm_indices(value Instr) {
  unsigned Length = LLVMGetNumIndices(Value_val(Instr));
  const unsigned *Indices = LLVMGetIndices(Value_val(Instr));
  value Array = caml_alloc_tuple_uninit(Length);
  for (unsigned I = 0; I < Length; ++I) {
    Field(Array, I) = Val_int(Indices[I]);
  }
  return Array;
}

/*--... Operations on constants of (mostly) any type .......................--*/

/* llvalue -> bool */
value llvm_is_constant(value Val) {
  return Val_bool(LLVMIsConstant(Value_val(Val)));
}

/* lltype -> llvalue */
value llvm_const_null(value Ty) {
  LLVMValueRef Value = LLVMConstNull(Type_val(Ty));
  return to_val(Value);
}

/* lltype -> llvalue */
value llvm_const_all_ones(value Ty) {
  LLVMValueRef Value = LLVMConstAllOnes(Type_val(Ty));
  return to_val(Value);
}

/* lltype -> llvalue */
value llvm_const_pointer_null(value Ty) {
  LLVMValueRef Value = LLVMConstPointerNull(Type_val(Ty));
  return to_val(Value);
}

/* lltype -> llvalue */
value llvm_get_undef(value Ty) {
  LLVMValueRef Value = LLVMGetUndef(Type_val(Ty));
  return to_val(Value);
}

/* lltype -> llvalue */
value llvm_get_poison(value Ty) {
  LLVMValueRef Value = LLVMGetPoison(Type_val(Ty));
  return to_val(Value);
}

/* llvalue -> bool */
value llvm_is_null(value Val) { return Val_bool(LLVMIsNull(Value_val(Val))); }

/* llvalue -> bool */
value llvm_is_undef(value Val) { return Val_bool(LLVMIsUndef(Value_val(Val))); }

/* llvalue -> bool */
value llvm_is_poison(value Val) {
  return Val_bool(LLVMIsPoison(Value_val(Val)));
}

/* llvalue -> Opcode.t */
value llvm_constexpr_get_opcode(value Val) {
  return LLVMIsAConstantExpr(Value_val(Val))
             ? Val_int(LLVMGetConstOpcode(Value_val(Val)))
             : Val_int(0);
}

/*--... Operations on instructions .........................................--*/

/* llvalue -> bool */
value llvm_has_metadata(value Val) {
  return Val_bool(LLVMHasMetadata(Value_val(Val)));
}

/* llvalue -> int -> llvalue option */
value llvm_metadata(value Val, value MDKindID) {
  return ptr_to_option(LLVMGetMetadata(Value_val(Val), Int_val(MDKindID)));
}

/* llvalue -> int -> llvalue -> unit */
value llvm_set_metadata(value Val, value MDKindID, value MD) {
  LLVMSetMetadata(Value_val(Val), Int_val(MDKindID), Value_val(MD));
  return Val_unit;
}

/* llvalue -> int -> unit */
value llvm_clear_metadata(value Val, value MDKindID) {
  LLVMSetMetadata(Value_val(Val), Int_val(MDKindID), NULL);
  return Val_unit;
}

/*--... Operations on metadata .............................................--*/

/* llcontext -> string -> llvalue */
value llvm_mdstring(value C, value S) {
  return to_val(LLVMMDStringInContext(Context_val(C), String_val(S),
                                      caml_string_length(S)));
}

/* llcontext -> llvalue array -> llvalue */
value llvm_mdnode(value C, value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value = LLVMMDNodeInContext(Context_val(C), Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llcontext -> llvalue */
value llvm_mdnull(value C) { return to_val(NULL); }

/* llvalue -> string option */
value llvm_get_mdstring(value V) {
  unsigned Len;
  const char *CStr = LLVMGetMDString(Value_val(V), &Len);
  return cstr_to_string_option(CStr, Len);
}

/* llvalue -> llvalue array */
value llvm_get_mdnode_operands(value Value) {
  LLVMValueRef V = Value_val(Value);
  unsigned Length = LLVMGetMDNodeNumOperands(V);
  value Operands = caml_alloc_tuple_uninit(Length);
  LLVMGetMDNodeOperands(V, (LLVMValueRef *)Op_val(Operands));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Operands, I) = to_val((LLVMValueRef)Field(Operands, I));
  }
  return Operands;
}

/* llmodule -> string -> llvalue array */
value llvm_get_namedmd(value M, value Name) {
  CAMLparam1(Name);
  unsigned Length =
      LLVMGetNamedMetadataNumOperands(Module_val(M), String_val(Name));
  value Nodes = caml_alloc_tuple_uninit(Length);
  LLVMGetNamedMetadataOperands(Module_val(M), String_val(Name),
                               (LLVMValueRef *)Op_val(Nodes));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Nodes, I) = to_val((LLVMValueRef)Field(Nodes, I));
  }
  CAMLreturn(Nodes);
}

/* llmodule -> string -> llvalue -> unit */
value llvm_append_namedmd(value M, value Name, value Val) {
  LLVMAddNamedMetadataOperand(Module_val(M), String_val(Name), Value_val(Val));
  return Val_unit;
}

/* llvalue -> llmetadata */
value llvm_value_as_metadata(value Val) {
  return to_val(LLVMValueAsMetadata(Value_val(Val)));
}

/* llcontext -> llmetadata -> llvalue */
value llvm_metadata_as_value(value C, value MD) {
  return to_val(LLVMMetadataAsValue(Context_val(C), Metadata_val(MD)));
}

/*--... Operations on scalar constants .....................................--*/

/* lltype -> int -> llvalue */
value llvm_const_int(value IntTy, value N) {
  return to_val(LLVMConstInt(Type_val(IntTy), (long long)Long_val(N), 1));
}

/* lltype -> Int64.t -> bool -> llvalue */
value llvm_const_of_int64(value IntTy, value N, value SExt) {
  return to_val(LLVMConstInt(Type_val(IntTy), Int64_val(N), Bool_val(SExt)));
}

/* llvalue -> Int64.t option */
value llvm_int64_of_const(value C) {
  LLVMValueRef Const = Value_val(C);
  if (!(LLVMIsAConstantInt(Const)) ||
      !(LLVMGetIntTypeWidth(LLVMTypeOf(Const)) <= 64))
    return Val_none;
  return caml_alloc_some(caml_copy_int64(LLVMConstIntGetSExtValue(Const)));
}

/* lltype -> string -> int -> llvalue */
value llvm_const_int_of_string(value IntTy, value S, value Radix) {
  return to_val(LLVMConstIntOfStringAndSize(
      Type_val(IntTy), String_val(S), caml_string_length(S), Int_val(Radix)));
}

/* lltype -> float -> llvalue */
value llvm_const_float(value RealTy, value N) {
  return to_val(LLVMConstReal(Type_val(RealTy), Double_val(N)));
}

/* llvalue -> float option */
value llvm_float_of_const(value C) {
  LLVMValueRef Const = Value_val(C);
  LLVMBool LosesInfo;
  double Result;
  if (!LLVMIsAConstantFP(Const))
    return Val_none;
  Result = LLVMConstRealGetDouble(Const, &LosesInfo);
  if (LosesInfo)
    return Val_none;
  return caml_alloc_some(caml_copy_double(Result));
}

/* lltype -> string -> llvalue */
value llvm_const_float_of_string(value RealTy, value S) {
  return to_val(LLVMConstRealOfStringAndSize(Type_val(RealTy), String_val(S),
                                             caml_string_length(S)));
}

/*--... Operations on composite constants ..................................--*/

/* llcontext -> string -> llvalue */
value llvm_const_string(value Context, value Str) {
  return to_val(LLVMConstStringInContext2(Context_val(Context), String_val(Str),
                                          caml_string_length(Str), 1));
}

/* llcontext -> string -> llvalue */
value llvm_const_stringz(value Context, value Str) {
  return to_val(LLVMConstStringInContext2(Context_val(Context), String_val(Str),
                                          caml_string_length(Str), 0));
}

/* lltype -> llvalue array -> llvalue */
value llvm_const_array(value ElementTy, value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value = LLVMConstArray(Type_val(ElementTy), Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llcontext -> llvalue array -> llvalue */
value llvm_const_struct(value C, value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value =
      LLVMConstStructInContext(Context_val(C), Temp, Length, 0);
  free(Temp);
  return to_val(Value);
}

/* lltype -> llvalue array -> llvalue */
value llvm_const_named_struct(value Ty, value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value =
      LLVMConstNamedStruct(Type_val(Ty), (LLVMValueRef *)Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llcontext -> llvalue array -> llvalue */
value llvm_const_packed_struct(value C, value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value =
      LLVMConstStructInContext(Context_val(C), Temp, Length, 1);
  free(Temp);
  return to_val(Value);
}

/* llvalue array -> llvalue */
value llvm_const_vector(value ElementVals) {
  mlsize_t Length = Wosize_val(ElementVals);
  LLVMValueRef *Temp = from_val_array(ElementVals);
  LLVMValueRef Value = LLVMConstVector(Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llvalue -> string option */
value llvm_string_of_const(value C) {
  size_t Len;
  const char *CStr;
  LLVMValueRef Const = Value_val(C);
  if (!LLVMIsAConstantDataSequential(Const) || !LLVMIsConstantString(Const))
    return Val_none;
  CStr = LLVMGetAsString(Const, &Len);
  return cstr_to_string_option(CStr, Len);
}

/* llvalue -> int -> llvalue option */
value llvm_aggregate_element(value Const, value N) {
  return ptr_to_option(LLVMGetAggregateElement(Value_val(Const), Int_val(N)));
}

/*--... Constant expressions ...............................................--*/

/* lltype -> llvalue */
value llvm_align_of(value Type) {
  LLVMValueRef Value = LLVMAlignOf(Type_val(Type));
  return to_val(Value);
}

/* lltype -> llvalue */
value llvm_size_of(value Type) {
  LLVMValueRef Value = LLVMSizeOf(Type_val(Type));
  return to_val(Value);
}

/* llvalue -> llvalue */
value llvm_const_neg(value Value) {
  LLVMValueRef NegValue = LLVMConstNeg(Value_val(Value));
  return to_val(NegValue);
}

/* llvalue -> llvalue */
value llvm_const_nsw_neg(value Value) {
  LLVMValueRef NegValue = LLVMConstNSWNeg(Value_val(Value));
  return to_val(NegValue);
}

/* llvalue -> llvalue */
value llvm_const_nuw_neg(value Value) {
  LLVMValueRef NegValue = LLVMConstNUWNeg(Value_val(Value));
  return to_val(NegValue);
}

/* llvalue -> llvalue */
value llvm_const_not(value Value) {
  LLVMValueRef NotValue = LLVMConstNot(Value_val(Value));
  return to_val(NotValue);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_add(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstAdd(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nsw_add(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNSWAdd(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nuw_add(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNUWAdd(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_sub(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstSub(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nsw_sub(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNSWSub(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nuw_sub(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNUWSub(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_mul(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstMul(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nsw_mul(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNSWMul(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_nuw_mul(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstNUWMul(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_xor(value LHS, value RHS) {
  LLVMValueRef Value = LLVMConstXor(Value_val(LHS), Value_val(RHS));
  return to_val(Value);
}

/* lltype -> llvalue -> llvalue array -> llvalue */
value llvm_const_gep(value Ty, value ConstantVal, value Indices) {
  mlsize_t Length = Wosize_val(Indices);
  LLVMValueRef *Temp = from_val_array(Indices);
  LLVMValueRef Value =
      LLVMConstGEP2(Type_val(Ty), Value_val(ConstantVal), Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* lltype -> llvalue -> llvalue array -> llvalue */
value llvm_const_in_bounds_gep(value Ty, value ConstantVal, value Indices) {
  mlsize_t Length = Wosize_val(Indices);
  LLVMValueRef *Temp = from_val_array(Indices);
  LLVMValueRef Value =
      LLVMConstInBoundsGEP2(Type_val(Ty), Value_val(ConstantVal), Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_trunc(value CV, value T) {
  LLVMValueRef Value = LLVMConstTrunc(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_ptrtoint(value CV, value T) {
  LLVMValueRef Value = LLVMConstPtrToInt(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_inttoptr(value CV, value T) {
  LLVMValueRef Value = LLVMConstIntToPtr(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_bitcast(value CV, value T) {
  LLVMValueRef Value = LLVMConstBitCast(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_trunc_or_bitcast(value CV, value T) {
  LLVMValueRef Value = LLVMConstTruncOrBitCast(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> lltype -> llvalue */
value llvm_const_pointercast(value CV, value T) {
  LLVMValueRef Value = LLVMConstPointerCast(Value_val(CV), Type_val(T));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue */
value llvm_const_extractelement(value V, value I) {
  LLVMValueRef Value = LLVMConstExtractElement(Value_val(V), Value_val(I));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue -> llvalue */
value llvm_const_insertelement(value V, value E, value I) {
  LLVMValueRef Value =
      LLVMConstInsertElement(Value_val(V), Value_val(E), Value_val(I));
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue -> llvalue */
value llvm_const_shufflevector(value VA, value VB, value Mask) {
  LLVMValueRef Value =
      LLVMConstShuffleVector(Value_val(VA), Value_val(VB), Value_val(Mask));
  return to_val(Value);
}

/* lltype -> string -> string -> bool -> bool -> llvalue */
value llvm_const_inline_asm(value Ty, value Asm, value Constraints,
                            value HasSideEffects, value IsAlignStack) {
  return to_val(
      LLVMConstInlineAsm(Type_val(Ty), String_val(Asm), String_val(Constraints),
                         Bool_val(HasSideEffects), Bool_val(IsAlignStack)));
}

/* llvalue -> llbasicblock -> llvalue */
value llvm_blockaddress(value V, value B) {
  LLVMValueRef Value = LLVMBlockAddress(Value_val(V), BasicBlock_val(B));
  return to_val(Value);
}

/*--... Operations on global variables, functions, and aliases (globals) ...--*/

/* llvalue -> llmodule */
value llvm_global_parent(value Value) {
  LLVMModuleRef Module = LLVMGetGlobalParent(Value_val(Value));
  return to_val(Module);
}

/* llvalue -> bool */
value llvm_is_declaration(value Global) {
  return Val_bool(LLVMIsDeclaration(Value_val(Global)));
}

/* llvalue -> Linkage.t */
value llvm_linkage(value Global) {
  return Val_int(LLVMGetLinkage(Value_val(Global)));
}

/* Linkage.t -> llvalue -> unit */
value llvm_set_linkage(value Linkage, value Global) {
  LLVMSetLinkage(Value_val(Global), Int_val(Linkage));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_unnamed_addr(value Global) {
  return Val_bool(LLVMHasUnnamedAddr(Value_val(Global)));
}

/* bool -> llvalue -> unit */
value llvm_set_unnamed_addr(value UseUnnamedAddr, value Global) {
  LLVMSetUnnamedAddr(Value_val(Global), Bool_val(UseUnnamedAddr));
  return Val_unit;
}

/* llvalue -> string */
value llvm_section(value Global) {
  return caml_copy_string(LLVMGetSection(Value_val(Global)));
}

/* string -> llvalue -> unit */
value llvm_set_section(value Section, value Global) {
  LLVMSetSection(Value_val(Global), String_val(Section));
  return Val_unit;
}

/* llvalue -> Visibility.t */
value llvm_visibility(value Global) {
  return Val_int(LLVMGetVisibility(Value_val(Global)));
}

/* Visibility.t -> llvalue -> unit */
value llvm_set_visibility(value Viz, value Global) {
  LLVMSetVisibility(Value_val(Global), Int_val(Viz));
  return Val_unit;
}

/* llvalue -> DLLStorageClass.t */
value llvm_dll_storage_class(value Global) {
  return Val_int(LLVMGetDLLStorageClass(Value_val(Global)));
}

/* DLLStorageClass.t -> llvalue -> unit */
value llvm_set_dll_storage_class(value Viz, value Global) {
  LLVMSetDLLStorageClass(Value_val(Global), Int_val(Viz));
  return Val_unit;
}

/* llvalue -> int */
value llvm_alignment(value Global) {
  return Val_int(LLVMGetAlignment(Value_val(Global)));
}

/* int -> llvalue -> unit */
value llvm_set_alignment(value Bytes, value Global) {
  LLVMSetAlignment(Value_val(Global), Int_val(Bytes));
  return Val_unit;
}

/* llvalue -> (llmdkind * llmetadata) array */
value llvm_global_copy_all_metadata(value Global) {
  CAMLparam0();
  CAMLlocal1(Array);
  size_t NumEntries;
  LLVMValueMetadataEntry *Entries =
      LLVMGlobalCopyAllMetadata(Value_val(Global), &NumEntries);
  Array = caml_alloc_tuple(NumEntries);
  for (int i = 0; i < NumEntries; ++i) {
    value Pair = caml_alloc_small(2, 0);
    Field(Pair, 0) = Val_int(LLVMValueMetadataEntriesGetKind(Entries, i));
    Field(Pair, 1) = to_val(LLVMValueMetadataEntriesGetMetadata(Entries, i));
    Store_field(Array, i, Pair);
  }
  LLVMDisposeValueMetadataEntries(Entries);
  CAMLreturn(Array);
}

/*--... Operations on uses .................................................--*/

/* llvalue -> lluse option */
value llvm_use_begin(value Val) {
  return ptr_to_option(LLVMGetFirstUse(Value_val(Val)));
}

/* lluse -> lluse option */
value llvm_use_succ(value U) {
  return ptr_to_option(LLVMGetNextUse(Use_val(U)));
}

/* lluse -> llvalue */
value llvm_user(value UR) { return to_val(LLVMGetUser(Use_val(UR))); }

/* lluse -> llvalue */
value llvm_used_value(value UR) {
  return to_val(LLVMGetUsedValue(Use_val(UR)));
}

/*--... Operations on global variables .....................................--*/

DEFINE_ITERATORS(global, Global, Module_val, LLVMValueRef, Value_val,
                 LLVMGetGlobalParent)

/* lltype -> string -> llmodule -> llvalue */
value llvm_declare_global(value Ty, value Name, value M) {
  LLVMValueRef GlobalVar;
  if ((GlobalVar = LLVMGetNamedGlobal(Module_val(M), String_val(Name)))) {
    if (LLVMGlobalGetValueType(GlobalVar) != Type_val(Ty))
      return to_val(
          LLVMConstBitCast(GlobalVar, LLVMPointerType(Type_val(Ty), 0)));
    return to_val(GlobalVar);
  }
  return to_val(LLVMAddGlobal(Module_val(M), Type_val(Ty), String_val(Name)));
}

/* lltype -> string -> int -> llmodule -> llvalue */
value llvm_declare_qualified_global(value Ty, value Name, value AddressSpace,
                                    value M) {
  LLVMValueRef GlobalVar;
  if ((GlobalVar = LLVMGetNamedGlobal(Module_val(M), String_val(Name)))) {
    if (LLVMGlobalGetValueType(GlobalVar) != Type_val(Ty))
      return to_val(LLVMConstBitCast(
          GlobalVar, LLVMPointerType(Type_val(Ty), Int_val(AddressSpace))));
    return to_val(GlobalVar);
  }
  return to_val(LLVMAddGlobalInAddressSpace(
      Module_val(M), Type_val(Ty), String_val(Name), Int_val(AddressSpace)));
}

/* string -> llmodule -> llvalue option */
value llvm_lookup_global(value Name, value M) {
  return ptr_to_option(LLVMGetNamedGlobal(Module_val(M), String_val(Name)));
}

/* string -> llvalue -> llmodule -> llvalue */
value llvm_define_global(value Name, value Initializer, value M) {
  LLVMValueRef GlobalVar = LLVMAddGlobal(
      Module_val(M), LLVMTypeOf(Value_val(Initializer)), String_val(Name));
  LLVMSetInitializer(GlobalVar, Value_val(Initializer));
  return to_val(GlobalVar);
}

/* string -> llvalue -> int -> llmodule -> llvalue */
value llvm_define_qualified_global(value Name, value Initializer,
                                   value AddressSpace, value M) {
  LLVMValueRef GlobalVar = LLVMAddGlobalInAddressSpace(
      Module_val(M), LLVMTypeOf(Value_val(Initializer)), String_val(Name),
      Int_val(AddressSpace));
  LLVMSetInitializer(GlobalVar, Value_val(Initializer));
  return to_val(GlobalVar);
}

/* llvalue -> unit */
value llvm_delete_global(value GlobalVar) {
  LLVMDeleteGlobal(Value_val(GlobalVar));
  return Val_unit;
}

/* llvalue -> llvalue option */
value llvm_global_initializer(value GlobalVar) {
  return ptr_to_option(LLVMGetInitializer(Value_val(GlobalVar)));
}

/* llvalue -> llvalue -> unit */
value llvm_set_initializer(value ConstantVal, value GlobalVar) {
  LLVMSetInitializer(Value_val(GlobalVar), Value_val(ConstantVal));
  return Val_unit;
}

/* llvalue -> unit */
value llvm_remove_initializer(value GlobalVar) {
  LLVMSetInitializer(Value_val(GlobalVar), NULL);
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_thread_local(value GlobalVar) {
  return Val_bool(LLVMIsThreadLocal(Value_val(GlobalVar)));
}

/* bool -> llvalue -> unit */
value llvm_set_thread_local(value IsThreadLocal, value GlobalVar) {
  LLVMSetThreadLocal(Value_val(GlobalVar), Bool_val(IsThreadLocal));
  return Val_unit;
}

/* llvalue -> ThreadLocalMode.t */
value llvm_thread_local_mode(value GlobalVar) {
  return Val_int(LLVMGetThreadLocalMode(Value_val(GlobalVar)));
}

/* ThreadLocalMode.t -> llvalue -> unit */
value llvm_set_thread_local_mode(value ThreadLocalMode, value GlobalVar) {
  LLVMSetThreadLocalMode(Value_val(GlobalVar), Int_val(ThreadLocalMode));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_externally_initialized(value GlobalVar) {
  return Val_bool(LLVMIsExternallyInitialized(Value_val(GlobalVar)));
}

/* bool -> llvalue -> unit */
value llvm_set_externally_initialized(value IsExternallyInitialized,
                                      value GlobalVar) {
  LLVMSetExternallyInitialized(Value_val(GlobalVar),
                               Bool_val(IsExternallyInitialized));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_global_constant(value GlobalVar) {
  return Val_bool(LLVMIsGlobalConstant(Value_val(GlobalVar)));
}

/* bool -> llvalue -> unit */
value llvm_set_global_constant(value Flag, value GlobalVar) {
  LLVMSetGlobalConstant(Value_val(GlobalVar), Bool_val(Flag));
  return Val_unit;
}

/*--... Operations on aliases ..............................................--*/

/* llmodule -> lltype -> int -> llvalue -> string -> llvalue */
value llvm_add_alias(value M, value ValueTy, value AddrSpace, value Aliasee,
                     value Name) {
  return to_val(LLVMAddAlias2(Module_val(M), Type_val(ValueTy),
                              Int_val(AddrSpace), Value_val(Aliasee),
                              String_val(Name)));
}

/*--... Operations on functions ............................................--*/

DEFINE_ITERATORS(function, Function, Module_val, LLVMValueRef, Value_val,
                 LLVMGetGlobalParent)

/* string -> lltype -> llmodule -> llvalue */
value llvm_declare_function(value Name, value Ty, value M) {
  LLVMValueRef Fn;
  if ((Fn = LLVMGetNamedFunction(Module_val(M), String_val(Name)))) {
    if (LLVMGlobalGetValueType(Fn) != Type_val(Ty))
      return to_val(LLVMConstBitCast(Fn, LLVMPointerType(Type_val(Ty), 0)));
    return to_val(Fn);
  }
  return to_val(LLVMAddFunction(Module_val(M), String_val(Name), Type_val(Ty)));
}

/* string -> llmodule -> llvalue option */
value llvm_lookup_function(value Name, value M) {
  return ptr_to_option(LLVMGetNamedFunction(Module_val(M), String_val(Name)));
}

/* string -> lltype -> llmodule -> llvalue */
value llvm_define_function(value Name, value Ty, value M) {
  LLVMValueRef Fn =
      LLVMAddFunction(Module_val(M), String_val(Name), Type_val(Ty));
  LLVMAppendBasicBlockInContext(LLVMGetTypeContext(Type_val(Ty)), Fn, "entry");
  return to_val(Fn);
}

/* llvalue -> unit */
value llvm_delete_function(value Fn) {
  LLVMDeleteFunction(Value_val(Fn));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_intrinsic(value Fn) {
  return Val_bool(LLVMGetIntrinsicID(Value_val(Fn)));
}

/* llvalue -> int */
value llvm_function_call_conv(value Fn) {
  return Val_int(LLVMGetFunctionCallConv(Value_val(Fn)));
}

/* int -> llvalue -> unit */
value llvm_set_function_call_conv(value Id, value Fn) {
  LLVMSetFunctionCallConv(Value_val(Fn), Int_val(Id));
  return Val_unit;
}

/* llvalue -> string option */
value llvm_gc(value Fn) {
  const char *GC = LLVMGetGC(Value_val(Fn));
  if (!GC)
    return Val_none;
  return caml_alloc_some(caml_copy_string(GC));
}

/* string option -> llvalue -> unit */
value llvm_set_gc(value GC, value Fn) {
  LLVMSetGC(Value_val(Fn), GC == Val_none ? 0 : String_val(Field(GC, 0)));
  return Val_unit;
}

/* llvalue -> llattribute -> int -> unit */
value llvm_add_function_attr(value F, value A, value Index) {
  LLVMAddAttributeAtIndex(Value_val(F), Int_val(Index), Attribute_val(A));
  return Val_unit;
}

/* llvalue -> int -> llattribute array */
value llvm_function_attrs(value F, value Index) {
  unsigned Length = LLVMGetAttributeCountAtIndex(Value_val(F), Int_val(Index));
  value Array = caml_alloc_tuple_uninit(Length);
  LLVMGetAttributesAtIndex(Value_val(F), Int_val(Index),
                           (LLVMAttributeRef *)Op_val(Array));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Array, I) = to_val((LLVMAttributeRef)Field(Array, I));
  }
  return Array;
}

/* llvalue -> llattrkind -> int -> unit */
value llvm_remove_enum_function_attr(value F, value Kind, value Index) {
  LLVMRemoveEnumAttributeAtIndex(Value_val(F), Int_val(Index), Int_val(Kind));
  return Val_unit;
}

/* llvalue -> string -> int -> unit */
value llvm_remove_string_function_attr(value F, value Kind, value Index) {
  LLVMRemoveStringAttributeAtIndex(Value_val(F), Int_val(Index),
                                   String_val(Kind), caml_string_length(Kind));
  return Val_unit;
}

/*--... Operations on parameters ...........................................--*/

DEFINE_ITERATORS(param, Param, Value_val, LLVMValueRef, Value_val,
                 LLVMGetParamParent)

/* llvalue -> int -> llvalue */
value llvm_param(value Fn, value Index) {
  return to_val(LLVMGetParam(Value_val(Fn), Int_val(Index)));
}

/* llvalue -> llvalue array */
value llvm_params(value Fn) {
  unsigned Length = LLVMCountParams(Value_val(Fn));
  value Params = caml_alloc_tuple_uninit(Length);
  LLVMGetParams(Value_val(Fn), (LLVMValueRef *)Op_val(Params));
  for (unsigned I = 0; I < Length; ++I) {
    Field(Params, I) = to_val((LLVMValueRef)Field(Params, I));
  }
  return Params;
}

/* llvalue -> llvalue */
value llvm_param_parent(value Value) {
  LLVMValueRef Parent = LLVMGetParamParent(Value_val(Value));
  return to_val(Parent);
}

/*--... Operations on basic blocks .........................................--*/

DEFINE_ITERATORS(block, BasicBlock, Value_val, LLVMBasicBlockRef,
                 BasicBlock_val, LLVMGetBasicBlockParent)

/* llbasicblock -> llvalue option */
value llvm_block_terminator(value Block) {
  return ptr_to_option(LLVMGetBasicBlockTerminator(BasicBlock_val(Block)));
}

/* llvalue -> llbasicblock array */
value llvm_basic_blocks(value Fn) {
  unsigned Length = LLVMCountBasicBlocks(Value_val(Fn));
  value MLArray = caml_alloc_tuple_uninit(Length);
  LLVMGetBasicBlocks(Value_val(Fn), (LLVMBasicBlockRef *)Op_val(MLArray));
  for (unsigned I = 0; I < Length; ++I) {
    Field(MLArray, I) = to_val((LLVMBasicBlockRef)Field(MLArray, I));
  }
  return MLArray;
}

/* llbasicblock -> unit */
value llvm_delete_block(value BB) {
  LLVMDeleteBasicBlock(BasicBlock_val(BB));
  return Val_unit;
}

/* llbasicblock -> unit */
value llvm_remove_block(value BB) {
  LLVMRemoveBasicBlockFromParent(BasicBlock_val(BB));
  return Val_unit;
}

/* llbasicblock -> llbasicblock -> unit */
value llvm_move_block_before(value Pos, value BB) {
  LLVMMoveBasicBlockBefore(BasicBlock_val(BB), BasicBlock_val(Pos));
  return Val_unit;
}

/* llbasicblock -> llbasicblock -> unit */
value llvm_move_block_after(value Pos, value BB) {
  LLVMMoveBasicBlockAfter(BasicBlock_val(BB), BasicBlock_val(Pos));
  return Val_unit;
}

/* string -> llvalue -> llbasicblock */
value llvm_append_block(value Context, value Name, value Fn) {
  return to_val(LLVMAppendBasicBlockInContext(Context_val(Context),
                                              Value_val(Fn), String_val(Name)));
}

/* llcontext -> string -> llbasicblock -> llbasicblock */
value llvm_insert_block(value Context, value Name, value BB) {
  return to_val(LLVMInsertBasicBlockInContext(
      Context_val(Context), BasicBlock_val(BB), String_val(Name)));
}

/* llbasicblock -> llvalue */
value llvm_value_of_block(value BB) {
  return to_val(LLVMBasicBlockAsValue(BasicBlock_val(BB)));
}

/* llvalue -> bool */
value llvm_value_is_block(value Val) {
  return Val_bool(LLVMValueIsBasicBlock(Value_val(Val)));
}

/* llbasicblock -> llvalue */
value llvm_block_of_value(value Val) {
  return to_val(LLVMValueAsBasicBlock(Value_val(Val)));
}

/* llbasicblock -> llvalue */
value llvm_block_parent(value BB) {
  return to_val(LLVMGetBasicBlockParent(BasicBlock_val(BB)));
}

/* llvalue -> llbasicblock */
value llvm_entry_block(value Val) {
  LLVMBasicBlockRef BB = LLVMGetEntryBasicBlock(Value_val(Val));
  return to_val(BB);
}

/*--... Operations on instructions .........................................--*/

/* llvalue -> llbasicblock */
value llvm_instr_parent(value Inst) {
  LLVMBasicBlockRef BB = LLVMGetInstructionParent(Value_val(Inst));
  return to_val(BB);
}

DEFINE_ITERATORS(instr, Instruction, BasicBlock_val, LLVMValueRef, Value_val,
                 LLVMGetInstructionParent)

/* llvalue -> Opcode.t */
value llvm_instr_get_opcode(value Inst) {
  LLVMOpcode o;
  if (!LLVMIsAInstruction(Value_val(Inst)))
    caml_failwith("Not an instruction");
  o = LLVMGetInstructionOpcode(Value_val(Inst));
  assert(o <= LLVMFreeze);
  return Val_int(o);
}

/* llvalue -> ICmp.t option */
value llvm_instr_icmp_predicate(value Val) {
  int x = LLVMGetICmpPredicate(Value_val(Val));
  if (!x)
    return Val_none;
  return caml_alloc_some(Val_int(x - LLVMIntEQ));
}

/* llvalue -> FCmp.t option */
value llvm_instr_fcmp_predicate(value Val) {
  int x = LLVMGetFCmpPredicate(Value_val(Val));
  if (!x)
    return Val_none;
  return caml_alloc_some(Val_int(x - LLVMRealPredicateFalse));
}

/* llvalue -> llvalue */
value llvm_instr_clone(value Inst) {
  if (!LLVMIsAInstruction(Value_val(Inst)))
    caml_failwith("Not an instruction");
  return to_val(LLVMInstructionClone(Value_val(Inst)));
}

/*--... Operations on call sites ...........................................--*/

/* llvalue -> int */
value llvm_instruction_call_conv(value Inst) {
  return Val_int(LLVMGetInstructionCallConv(Value_val(Inst)));
}

/* int -> llvalue -> unit */
value llvm_set_instruction_call_conv(value CC, value Inst) {
  LLVMSetInstructionCallConv(Value_val(Inst), Int_val(CC));
  return Val_unit;
}

/* llvalue -> llattribute -> int -> unit */
value llvm_add_call_site_attr(value F, value A, value Index) {
  LLVMAddCallSiteAttribute(Value_val(F), Int_val(Index), Attribute_val(A));
  return Val_unit;
}

/* llvalue -> int -> llattribute array */
value llvm_call_site_attrs(value F, value Index) {
  unsigned Count = LLVMGetCallSiteAttributeCount(Value_val(F), Int_val(Index));
  value Array = caml_alloc_tuple_uninit(Count);
  LLVMGetCallSiteAttributes(Value_val(F), Int_val(Index),
                            (LLVMAttributeRef *)Op_val(Array));
  for (unsigned I = 0; I < Count; ++I) {
    Field(Array, I) = to_val((LLVMAttributeRef)Field(Array, I));
  }
  return Array;
}

/* llvalue -> llattrkind -> int -> unit */
value llvm_remove_enum_call_site_attr(value F, value Kind, value Index) {
  LLVMRemoveCallSiteEnumAttribute(Value_val(F), Int_val(Index), Int_val(Kind));
  return Val_unit;
}

/* llvalue -> string -> int -> unit */
value llvm_remove_string_call_site_attr(value F, value Kind, value Index) {
  LLVMRemoveCallSiteStringAttribute(Value_val(F), Int_val(Index),
                                    String_val(Kind), caml_string_length(Kind));
  return Val_unit;
}

/*--... Operations on call instructions (only) .............................--*/

/* llvalue -> int */
value llvm_num_arg_operands(value V) {
  return Val_int(LLVMGetNumArgOperands(Value_val(V)));
}

/* llvalue -> bool */
value llvm_is_tail_call(value CallInst) {
  return Val_bool(LLVMIsTailCall(Value_val(CallInst)));
}

/* bool -> llvalue -> unit */
value llvm_set_tail_call(value IsTailCall, value CallInst) {
  LLVMSetTailCall(Value_val(CallInst), Bool_val(IsTailCall));
  return Val_unit;
}

/* llvalue -> llbasicblock */
value llvm_get_normal_dest(value Val) {
  LLVMBasicBlockRef BB = LLVMGetNormalDest(Value_val(Val));
  return to_val(BB);
}

/* llvalue -> llbasicblock */
value llvm_get_unwind_dest(value Val) {
  LLVMBasicBlockRef BB = LLVMGetUnwindDest(Value_val(Val));
  return to_val(BB);
}

/*--... Operations on load/store instructions (only)........................--*/

/* llvalue -> bool */
value llvm_is_volatile(value MemoryInst) {
  return Val_bool(LLVMGetVolatile(Value_val(MemoryInst)));
}

/* bool -> llvalue -> unit */
value llvm_set_volatile(value IsVolatile, value MemoryInst) {
  LLVMSetVolatile(Value_val(MemoryInst), Bool_val(IsVolatile));
  return Val_unit;
}

/*--.. Operations on terminators ...........................................--*/

/* llvalue -> int -> llbasicblock */
value llvm_successor(value V, value I) {
  return to_val(LLVMGetSuccessor(Value_val(V), Int_val(I)));
}

/* llvalue -> int -> llvalue -> unit */
value llvm_set_successor(value U, value I, value B) {
  LLVMSetSuccessor(Value_val(U), Int_val(I), BasicBlock_val(B));
  return Val_unit;
}

/* llvalue -> int */
value llvm_num_successors(value V) {
  return Val_int(LLVMGetNumSuccessors(Value_val(V)));
}

/*--.. Operations on branch ................................................--*/

/* llvalue -> llvalue */
value llvm_condition(value V) { return to_val(LLVMGetCondition(Value_val(V))); }

/* llvalue -> llvalue -> unit */
value llvm_set_condition(value B, value C) {
  LLVMSetCondition(Value_val(B), Value_val(C));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_conditional(value V) {
  return Val_bool(LLVMIsConditional(Value_val(V)));
}

/*--... Operations on phi nodes ............................................--*/

/* (llvalue * llbasicblock) -> llvalue -> unit */
value llvm_add_incoming(value Incoming, value PhiNode) {
  LLVMValueRef V = Value_val(Field(Incoming, 0));
  LLVMBasicBlockRef BB = BasicBlock_val(Field(Incoming, 1));
  LLVMAddIncoming(Value_val(PhiNode), &V, &BB, 1);
  return Val_unit;
}

/* llvalue -> (llvalue * llbasicblock) list */
value llvm_incoming(value Phi) {
  CAMLparam0();
  CAMLlocal2(Hd, Tl);
  LLVMValueRef PhiNode = Value_val(Phi);

  /* Build a tuple list of them. */
  Tl = Val_int(0);
  for (unsigned I = LLVMCountIncoming(PhiNode); I != 0;) {
    Hd = caml_alloc_small(2, 0);
    Field(Hd, 0) = to_val(LLVMGetIncomingValue(PhiNode, --I));
    Field(Hd, 1) = to_val(LLVMGetIncomingBlock(PhiNode, I));

    value Tmp = caml_alloc_small(2, 0);
    Field(Tmp, 0) = Hd;
    Field(Tmp, 1) = Tl;
    Tl = Tmp;
  }

  CAMLreturn(Tl);
}

/* llvalue -> unit */
value llvm_delete_instruction(value Instruction) {
  LLVMInstructionEraseFromParent(Value_val(Instruction));
  return Val_unit;
}

/*===-- Instruction builders ----------------------------------------------===*/

#define Builder_val(v) (*(LLVMBuilderRef *)(Data_custom_val(v)))

static void llvm_finalize_builder(value B) {
  LLVMDisposeBuilder(Builder_val(B));
}

static struct custom_operations builder_ops = {
    (char *)"Llvm.llbuilder",  llvm_finalize_builder,
    custom_compare_default,    custom_hash_default,
    custom_serialize_default,  custom_deserialize_default,
    custom_compare_ext_default};

static value alloc_builder(LLVMBuilderRef B) {
  value V = caml_alloc_custom(&builder_ops, sizeof(LLVMBuilderRef), 0, 1);
  Builder_val(V) = B;
  return V;
}

/* llcontext -> llbuilder */
value llvm_builder(value C) {
  return alloc_builder(LLVMCreateBuilderInContext(Context_val(C)));
}

/* (llbasicblock, llvalue) llpos -> llbuilder -> unit */
value llvm_position_builder_before_dbg_records(value Pos, value B) {
  if (Tag_val(Pos) == 0) {
    LLVMBasicBlockRef BB = BasicBlock_val(Field(Pos, 0));
    LLVMPositionBuilderAtEnd(Builder_val(B), BB);
  } else {
    LLVMValueRef I = Value_val(Field(Pos, 0));
    LLVMPositionBuilderBeforeInstrAndDbgRecords(Builder_val(B), I);
  }
  return Val_unit;
}

/* (llbasicblock, llvalue) llpos -> llbuilder -> unit */
value llvm_position_builder(value Pos, value B) {
  if (Tag_val(Pos) == 0) {
    LLVMBasicBlockRef BB = BasicBlock_val(Field(Pos, 0));
    LLVMPositionBuilderAtEnd(Builder_val(B), BB);
  } else {
    LLVMValueRef I = Value_val(Field(Pos, 0));
    LLVMPositionBuilderBefore(Builder_val(B), I);
  }
  return Val_unit;
}

/* llbuilder -> llbasicblock */
value llvm_insertion_block(value B) {
  LLVMBasicBlockRef InsertBlock = LLVMGetInsertBlock(Builder_val(B));
  if (!InsertBlock)
    caml_raise_not_found();
  return to_val(InsertBlock);
}

/* llvalue -> string -> llbuilder -> unit */
value llvm_insert_into_builder(value I, value Name, value B) {
  LLVMInsertIntoBuilderWithName(Builder_val(B), Value_val(I), String_val(Name));
  return Val_unit;
}

/*--... Metadata ...........................................................--*/

/* llbuilder -> llvalue -> unit */
value llvm_set_current_debug_location(value B, value V) {
  LLVMSetCurrentDebugLocation(Builder_val(B), Value_val(V));
  return Val_unit;
}

/* llbuilder -> unit */
value llvm_clear_current_debug_location(value B) {
  LLVMSetCurrentDebugLocation(Builder_val(B), NULL);
  return Val_unit;
}

/* llbuilder -> llvalue option */
value llvm_current_debug_location(value B) {
  return ptr_to_option(LLVMGetCurrentDebugLocation(Builder_val(B)));
}

/* llbuilder -> llvalue -> unit */
value llvm_set_inst_debug_location(value B, value V) {
  LLVMSetInstDebugLocation(Builder_val(B), Value_val(V));
  return Val_unit;
}

/*--... Terminators ........................................................--*/

/* llbuilder -> llvalue */
value llvm_build_ret_void(value B) {
  return to_val(LLVMBuildRetVoid(Builder_val(B)));
}

/* llvalue -> llbuilder -> llvalue */
value llvm_build_ret(value Val, value B) {
  return to_val(LLVMBuildRet(Builder_val(B), Value_val(Val)));
}

/* llvalue array -> llbuilder -> llvalue */
value llvm_build_aggregate_ret(value RetVals, value B) {
  mlsize_t Length = Wosize_val(RetVals);
  LLVMValueRef *Temp = from_val_array(RetVals);
  LLVMValueRef Value = LLVMBuildAggregateRet(Builder_val(B), Temp, Length);
  free(Temp);
  return to_val(Value);
}

/* llbasicblock -> llbuilder -> llvalue */
value llvm_build_br(value BB, value B) {
  return to_val(LLVMBuildBr(Builder_val(B), BasicBlock_val(BB)));
}

/* llvalue -> llbasicblock -> llbasicblock -> llbuilder -> llvalue */
value llvm_build_cond_br(value If, value Then, value Else, value B) {
  return to_val(LLVMBuildCondBr(Builder_val(B), Value_val(If),
                                BasicBlock_val(Then), BasicBlock_val(Else)));
}

/* llvalue -> llbasicblock -> int -> llbuilder -> llvalue */
value llvm_build_switch(value Of, value Else, value EstimatedCount, value B) {
  return to_val(LLVMBuildSwitch(Builder_val(B), Value_val(Of),
                                BasicBlock_val(Else), Int_val(EstimatedCount)));
}

/* lltype -> string -> llbuilder -> llvalue */
value llvm_build_malloc(value Ty, value Name, value B) {
  return to_val(
      LLVMBuildMalloc(Builder_val(B), Type_val(Ty), String_val(Name)));
}

/* lltype -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_array_malloc(value Ty, value Val, value Name, value B) {
  return to_val(LLVMBuildArrayMalloc(Builder_val(B), Type_val(Ty),
                                     Value_val(Val), String_val(Name)));
}

/* llvalue -> llbuilder -> llvalue */
value llvm_build_free(value P, value B) {
  return to_val(LLVMBuildFree(Builder_val(B), Value_val(P)));
}

/* llvalue -> llvalue -> llbasicblock -> unit */
value llvm_add_case(value Switch, value OnVal, value Dest) {
  LLVMAddCase(Value_val(Switch), Value_val(OnVal), BasicBlock_val(Dest));
  return Val_unit;
}

/* llvalue -> llbasicblock */
value llvm_switch_default_dest(value Val) {
  LLVMBasicBlockRef BB = LLVMGetSwitchDefaultDest(Value_val(Val));
  return to_val(BB);
}

/* llvalue -> int -> llbuilder -> llvalue */
value llvm_build_indirect_br(value Addr, value EstimatedDests, value B) {
  return to_val(LLVMBuildIndirectBr(Builder_val(B), Value_val(Addr),
                                    Int_val(EstimatedDests)));
}

/* llvalue -> llbasicblock -> unit */
value llvm_add_destination(value IndirectBr, value Dest) {
  LLVMAddDestination(Value_val(IndirectBr), BasicBlock_val(Dest));
  return Val_unit;
}

/* lltype -> llvalue -> llvalue array -> llbasicblock -> llbasicblock ->
   string -> llbuilder -> llvalue */
value llvm_build_invoke_nat(value FnTy, value Fn, value Args, value Then,
                            value Catch, value Name, value B) {
  mlsize_t Length = Wosize_val(Args);
  LLVMValueRef *Temp = from_val_array(Args);
  LLVMValueRef Value = LLVMBuildInvoke2(
      Builder_val(B), Type_val(FnTy), Value_val(Fn), Temp, Length,
      BasicBlock_val(Then), BasicBlock_val(Catch), String_val(Name));
  free(Temp);
  return to_val(Value);
}

/* lltype -> llvalue -> llvalue array -> llbasicblock -> llbasicblock ->
   string -> llbuilder -> llvalue */
value llvm_build_invoke_bc(value Args[], int NumArgs) {
  return llvm_build_invoke_nat(Args[0], Args[1], Args[2], Args[3], Args[4],
                               Args[5], Args[6]);
}

/* lltype -> llvalue -> int -> string -> llbuilder -> llvalue */
value llvm_build_landingpad(value Ty, value PersFn, value NumClauses,
                            value Name, value B) {
  return to_val(LLVMBuildLandingPad(Builder_val(B), Type_val(Ty),
                                    Value_val(PersFn), Int_val(NumClauses),
                                    String_val(Name)));
}

/* llvalue -> llvalue -> unit */
value llvm_add_clause(value LandingPadInst, value ClauseVal) {
  LLVMAddClause(Value_val(LandingPadInst), Value_val(ClauseVal));
  return Val_unit;
}

/* llvalue -> bool */
value llvm_is_cleanup(value LandingPadInst) {
  return Val_bool(LLVMIsCleanup(Value_val(LandingPadInst)));
}

/* llvalue -> bool -> unit */
value llvm_set_cleanup(value LandingPadInst, value flag) {
  LLVMSetCleanup(Value_val(LandingPadInst), Bool_val(flag));
  return Val_unit;
}

/* llvalue -> llbuilder -> llvalue */
value llvm_build_resume(value Exn, value B) {
  return to_val(LLVMBuildResume(Builder_val(B), Value_val(Exn)));
}

/* llbuilder -> llvalue */
value llvm_build_unreachable(value B) {
  return to_val(LLVMBuildUnreachable(Builder_val(B)));
}

/*--... Arithmetic .........................................................--*/

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_add(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildAdd(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nsw_add(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNSWAdd(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nuw_add(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNUWAdd(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fadd(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFAdd(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_sub(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildSub(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nsw_sub(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNSWSub(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nuw_sub(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNUWSub(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fsub(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFSub(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_mul(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildMul(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nsw_mul(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNSWMul(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nuw_mul(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildNUWMul(Builder_val(B), Value_val(LHS), Value_val(RHS),
                                String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fmul(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFMul(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_udiv(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildUDiv(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_sdiv(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildSDiv(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_exact_sdiv(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildExactSDiv(Builder_val(B), Value_val(LHS),
                                   Value_val(RHS), String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fdiv(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFDiv(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_urem(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildURem(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_srem(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildSRem(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_frem(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFRem(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_shl(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildShl(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_lshr(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildLShr(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_ashr(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildAShr(Builder_val(B), Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_and(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildAnd(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_or(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildOr(Builder_val(B), Value_val(LHS), Value_val(RHS),
                            String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_xor(value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildXor(Builder_val(B), Value_val(LHS), Value_val(RHS),
                             String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_neg(value X, value Name, value B) {
  return to_val(LLVMBuildNeg(Builder_val(B), Value_val(X), String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nsw_neg(value X, value Name, value B) {
  return to_val(
      LLVMBuildNSWNeg(Builder_val(B), Value_val(X), String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_nuw_neg(value X, value Name, value B) {
  return to_val(
      LLVMBuildNUWNeg(Builder_val(B), Value_val(X), String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fneg(value X, value Name, value B) {
  return to_val(LLVMBuildFNeg(Builder_val(B), Value_val(X), String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_not(value X, value Name, value B) {
  return to_val(LLVMBuildNot(Builder_val(B), Value_val(X), String_val(Name)));
}

/*--... Memory .............................................................--*/

/* lltype -> string -> llbuilder -> llvalue */
value llvm_build_alloca(value Ty, value Name, value B) {
  return to_val(
      LLVMBuildAlloca(Builder_val(B), Type_val(Ty), String_val(Name)));
}

/* lltype -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_array_alloca(value Ty, value Size, value Name, value B) {
  return to_val(LLVMBuildArrayAlloca(Builder_val(B), Type_val(Ty),
                                     Value_val(Size), String_val(Name)));
}

/* lltype -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_load(value Ty, value Pointer, value Name, value B) {
  return to_val(LLVMBuildLoad2(Builder_val(B), Type_val(Ty), Value_val(Pointer),
                               String_val(Name)));
}

/* llvalue -> llvalue -> llbuilder -> llvalue */
value llvm_build_store(value Value, value Pointer, value B) {
  return to_val(
      LLVMBuildStore(Builder_val(B), Value_val(Value), Value_val(Pointer)));
}

/* AtomicRMWBinOp.t -> llvalue -> llvalue -> AtomicOrdering.t ->
   bool -> string -> llbuilder -> llvalue */
value llvm_build_atomicrmw_native(value BinOp, value Ptr, value Val, value Ord,
                                  value ST, value Name, value B) {
  LLVMValueRef Instr;
  Instr = LLVMBuildAtomicRMW(Builder_val(B), Int_val(BinOp), Value_val(Ptr),
                             Value_val(Val), Int_val(Ord), Bool_val(ST));
  LLVMSetValueName(Instr, String_val(Name));
  return to_val(Instr);
}

value llvm_build_atomicrmw_bytecode(value *argv, int argn) {
  return llvm_build_atomicrmw_native(argv[0], argv[1], argv[2], argv[3],
                                     argv[4], argv[5], argv[6]);
}

/* lltype -> llvalue -> llvalue array -> string -> llbuilder -> llvalue */
value llvm_build_gep(value Ty, value Pointer, value Indices, value Name,
                     value B) {
  mlsize_t Length = Wosize_val(Indices);
  LLVMValueRef *Temp = from_val_array(Indices);
  LLVMValueRef Value =
      LLVMBuildGEP2(Builder_val(B), Type_val(Ty), Value_val(Pointer), Temp,
                    Length, String_val(Name));
  free(Temp);
  return to_val(Value);
}

/* lltype -> llvalue -> llvalue array -> string -> llbuilder -> llvalue */
value llvm_build_in_bounds_gep(value Ty, value Pointer, value Indices,
                               value Name, value B) {
  mlsize_t Length = Wosize_val(Indices);
  LLVMValueRef *Temp = from_val_array(Indices);
  LLVMValueRef Value =
      LLVMBuildInBoundsGEP2(Builder_val(B), Type_val(Ty), Value_val(Pointer),
                            Temp, Length, String_val(Name));
  free(Temp);
  return to_val(Value);
}

/* lltype -> llvalue -> int -> string -> llbuilder -> llvalue */
value llvm_build_struct_gep(value Ty, value Pointer, value Index, value Name,
                            value B) {
  return to_val(LLVMBuildStructGEP2(Builder_val(B), Type_val(Ty),
                                    Value_val(Pointer), Int_val(Index),
                                    String_val(Name)));
}

/* string -> string -> llbuilder -> llvalue */
value llvm_build_global_string(value Str, value Name, value B) {
  return to_val(
      LLVMBuildGlobalString(Builder_val(B), String_val(Str), String_val(Name)));
}

/* string -> string -> llbuilder -> llvalue */
value llvm_build_global_stringptr(value Str, value Name, value B) {
  return to_val(LLVMBuildGlobalStringPtr(Builder_val(B), String_val(Str),
                                         String_val(Name)));
}

/*--... Casts ..............................................................--*/

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_trunc(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildTrunc(Builder_val(B), Value_val(X), Type_val(Ty),
                               String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_zext(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildZExt(Builder_val(B), Value_val(X), Type_val(Ty),
                              String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_sext(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildSExt(Builder_val(B), Value_val(X), Type_val(Ty),
                              String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_fptoui(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildFPToUI(Builder_val(B), Value_val(X), Type_val(Ty),
                                String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_fptosi(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildFPToSI(Builder_val(B), Value_val(X), Type_val(Ty),
                                String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_uitofp(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildUIToFP(Builder_val(B), Value_val(X), Type_val(Ty),
                                String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_sitofp(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildSIToFP(Builder_val(B), Value_val(X), Type_val(Ty),
                                String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_fptrunc(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildFPTrunc(Builder_val(B), Value_val(X), Type_val(Ty),
                                 String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_fpext(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildFPExt(Builder_val(B), Value_val(X), Type_val(Ty),
                               String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_prttoint(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildPtrToInt(Builder_val(B), Value_val(X), Type_val(Ty),
                                  String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_inttoptr(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildIntToPtr(Builder_val(B), Value_val(X), Type_val(Ty),
                                  String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_bitcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildBitCast(Builder_val(B), Value_val(X), Type_val(Ty),
                                 String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_zext_or_bitcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildZExtOrBitCast(Builder_val(B), Value_val(X),
                                       Type_val(Ty), String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_sext_or_bitcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildSExtOrBitCast(Builder_val(B), Value_val(X),
                                       Type_val(Ty), String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_trunc_or_bitcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildTruncOrBitCast(Builder_val(B), Value_val(X),
                                        Type_val(Ty), String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_pointercast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildPointerCast(Builder_val(B), Value_val(X), Type_val(Ty),
                                     String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_intcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildIntCast(Builder_val(B), Value_val(X), Type_val(Ty),
                                 String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_fpcast(value X, value Ty, value Name, value B) {
  return to_val(LLVMBuildFPCast(Builder_val(B), Value_val(X), Type_val(Ty),
                                String_val(Name)));
}

/*--... Comparisons ........................................................--*/

/* Icmp.t -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_icmp(value Pred, value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildICmp(Builder_val(B), Int_val(Pred) + LLVMIntEQ,
                              Value_val(LHS), Value_val(RHS),
                              String_val(Name)));
}

/* Fcmp.t -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_fcmp(value Pred, value LHS, value RHS, value Name, value B) {
  return to_val(LLVMBuildFCmp(Builder_val(B), Int_val(Pred), Value_val(LHS),
                              Value_val(RHS), String_val(Name)));
}

/*--... Miscellaneous instructions .........................................--*/

/* (llvalue * llbasicblock) list -> string -> llbuilder -> llvalue */
value llvm_build_phi(value Incoming, value Name, value B) {
  value Hd, Tl;
  LLVMValueRef FirstValue, PhiNode;

  assert(Incoming != Val_int(0) && "Empty list passed to Llvm.build_phi!");

  Hd = Field(Incoming, 0);
  FirstValue = Value_val(Field(Hd, 0));
  PhiNode =
      LLVMBuildPhi(Builder_val(B), LLVMTypeOf(FirstValue), String_val(Name));

  for (Tl = Incoming; Tl != Val_int(0); Tl = Field(Tl, 1)) {
    Hd = Field(Tl, 0);
    LLVMValueRef V = Value_val(Field(Hd, 0));
    LLVMBasicBlockRef BB = BasicBlock_val(Field(Hd, 1));
    LLVMAddIncoming(PhiNode, &V, &BB, 1);
  }

  return to_val(PhiNode);
}

/* lltype -> string -> llbuilder -> value */
value llvm_build_empty_phi(value Type, value Name, value B) {
  return to_val(LLVMBuildPhi(Builder_val(B), Type_val(Type), String_val(Name)));
}

/* lltype -> llvalue -> llvalue array -> string -> llbuilder -> llvalue */
value llvm_build_call(value FnTy, value Fn, value Params, value Name, value B) {
  mlsize_t Length = Wosize_val(Params);
  LLVMValueRef *Temp = from_val_array(Params);
  LLVMValueRef Value =
      LLVMBuildCall2(Builder_val(B), Type_val(FnTy), Value_val(Fn), Temp,
                     Length, String_val(Name));
  free(Temp);
  return to_val(Value);
}

/* llvalue -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_select(value If, value Then, value Else, value Name, value B) {
  return to_val(LLVMBuildSelect(Builder_val(B), Value_val(If), Value_val(Then),
                                Value_val(Else), String_val(Name)));
}

/* llvalue -> lltype -> string -> llbuilder -> llvalue */
value llvm_build_va_arg(value List, value Ty, value Name, value B) {
  return to_val(LLVMBuildVAArg(Builder_val(B), Value_val(List), Type_val(Ty),
                               String_val(Name)));
}

/* llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_extractelement(value Vec, value Idx, value Name, value B) {
  return to_val(LLVMBuildExtractElement(Builder_val(B), Value_val(Vec),
                                        Value_val(Idx), String_val(Name)));
}

/* llvalue -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_insertelement(value Vec, value Element, value Idx, value Name,
                               value B) {
  return to_val(LLVMBuildInsertElement(Builder_val(B), Value_val(Vec),
                                       Value_val(Element), Value_val(Idx),
                                       String_val(Name)));
}

/* llvalue -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_shufflevector(value V1, value V2, value Mask, value Name,
                               value B) {
  return to_val(LLVMBuildShuffleVector(Builder_val(B), Value_val(V1),
                                       Value_val(V2), Value_val(Mask),
                                       String_val(Name)));
}

/* llvalue -> int -> string -> llbuilder -> llvalue */
value llvm_build_extractvalue(value Aggregate, value Idx, value Name, value B) {
  return to_val(LLVMBuildExtractValue(Builder_val(B), Value_val(Aggregate),
                                      Int_val(Idx), String_val(Name)));
}

/* llvalue -> llvalue -> int -> string -> llbuilder -> llvalue */
value llvm_build_insertvalue(value Aggregate, value Val, value Idx, value Name,
                             value B) {
  return to_val(LLVMBuildInsertValue(Builder_val(B), Value_val(Aggregate),
                                     Value_val(Val), Int_val(Idx),
                                     String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_is_null(value Val, value Name, value B) {
  return to_val(
      LLVMBuildIsNull(Builder_val(B), Value_val(Val), String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_is_not_null(value Val, value Name, value B) {
  return to_val(
      LLVMBuildIsNotNull(Builder_val(B), Value_val(Val), String_val(Name)));
}

/* lltype -> llvalue -> llvalue -> string -> llbuilder -> llvalue */
value llvm_build_ptrdiff(value ElemTy, value LHS, value RHS, value Name,
                         value B) {
  return to_val(LLVMBuildPtrDiff2(Builder_val(B), Type_val(ElemTy),
                                  Value_val(LHS), Value_val(RHS),
                                  String_val(Name)));
}

/* llvalue -> string -> llbuilder -> llvalue */
value llvm_build_freeze(value X, value Name, value B) {
  return to_val(
      LLVMBuildFreeze(Builder_val(B), Value_val(X), String_val(Name)));
}

/*===-- Memory buffers ----------------------------------------------------===*/

/* string -> llmemorybuffer
   raises IoError msg on error */
value llvm_memorybuffer_of_file(value Path) {
  char *Message;
  LLVMMemoryBufferRef MemBuf;

  if (LLVMCreateMemoryBufferWithContentsOfFile(String_val(Path), &MemBuf,
                                               &Message))
    llvm_raise(*caml_named_value("Llvm.IoError"), Message);
  return to_val(MemBuf);
}

/* unit -> llmemorybuffer
   raises IoError msg on error */
value llvm_memorybuffer_of_stdin(value Unit) {
  char *Message;
  LLVMMemoryBufferRef MemBuf;

  if (LLVMCreateMemoryBufferWithSTDIN(&MemBuf, &Message))
    llvm_raise(*caml_named_value("Llvm.IoError"), Message);
  return to_val(MemBuf);
}

/* ?name:string -> string -> llmemorybuffer */
value llvm_memorybuffer_of_string(value Name, value String) {
  LLVMMemoryBufferRef MemBuf;
  const char *NameCStr;

  if (Name == Val_int(0))
    NameCStr = "";
  else
    NameCStr = String_val(Field(Name, 0));

  MemBuf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
      String_val(String), caml_string_length(String), NameCStr);
  return to_val(MemBuf);
}

/* llmemorybuffer -> string */
value llvm_memorybuffer_as_string(value MB) {
  LLVMMemoryBufferRef MemBuf = MemoryBuffer_val(MB);
  size_t BufferSize = LLVMGetBufferSize(MemBuf);
  const char *BufferStart = LLVMGetBufferStart(MemBuf);
  return cstr_to_string(BufferStart, BufferSize);
}

/* llmemorybuffer -> unit */
value llvm_memorybuffer_dispose(value MemBuf) {
  LLVMDisposeMemoryBuffer(MemoryBuffer_val(MemBuf));
  return Val_unit;
}
