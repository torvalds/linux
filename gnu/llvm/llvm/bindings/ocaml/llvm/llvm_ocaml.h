/*===-- llvm_ocaml.h - LLVM OCaml Glue --------------------------*- C++ -*-===*\
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

#ifndef LLVM_LLVM_OCAML_H
#define LLVM_LLVM_OCAML_H

#include "caml/alloc.h"
#include "caml/custom.h"
#include "caml/version.h"

#if OCAML_VERSION < 41200
/* operations on OCaml option values, defined by OCaml 4.12 */
#define Val_none Val_int(0)
#define Some_val(v) Field(v, 0)
#define Tag_some 0
#define Is_none(v) ((v) == Val_none)
#define Is_some(v) Is_block(v)
value caml_alloc_some(value);
#endif

/* to_val and from_val convert between an OCaml value and a pointer from LLVM,
   which points outside the OCaml heap. They assume that all pointers from LLVM
   are 2-byte aligned: to_val sets the low bit so that OCaml treats the value
   as an integer, and from_val clears the low bit. */
value to_val(void *ptr);

void *from_val(value v);

/* from_val_array takes an OCaml array value of LLVM references encoded with
   the representation described above and returns a malloc'd array
   of decoded LLVM references. The returned array must be deallocated using
   free. */
void *from_val_array(value Elements);

#define DiagnosticInfo_val(v) ((LLVMDiagnosticInfoRef)from_val(v))
#define Context_val(v) ((LLVMContextRef)from_val(v))
#define Attribute_val(v) ((LLVMAttributeRef)from_val(v))
#define Module_val(v) ((LLVMModuleRef)from_val(v))
#define Metadata_val(v) ((LLVMMetadataRef)from_val(v))
#define Type_val(v) ((LLVMTypeRef)from_val(v))
#define Value_val(v) ((LLVMValueRef)from_val(v))
#define DbgRecord_val(v) ((LLVMDbgRecordRef)from_val(v))
#define Use_val(v) ((LLVMUseRef)from_val(v))
#define BasicBlock_val(v) ((LLVMBasicBlockRef)from_val(v))
#define MemoryBuffer_val(v) ((LLVMMemoryBufferRef)from_val(v))

/* Convert a C pointer to an OCaml option */
value ptr_to_option(void *Ptr);

/* Convert a C string into an OCaml string */
value cstr_to_string(const char *Str, mlsize_t Len);

#endif // LLVM_LLVM_OCAML_H
