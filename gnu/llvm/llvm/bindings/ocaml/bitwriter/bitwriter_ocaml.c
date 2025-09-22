/*===-- bitwriter_ocaml.c - LLVM OCaml Glue ---------------------*- C++ -*-===*\
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

#include "caml/alloc.h"
#include "caml/memory.h"
#include "caml/mlvalues.h"
#include "llvm_ocaml.h"
#include "llvm-c/BitWriter.h"
#include "llvm-c/Core.h"

/* Llvm.llmodule -> string -> bool */
value llvm_write_bitcode_file(value M, value Path) {
  int Result = LLVMWriteBitcodeToFile(Module_val(M), String_val(Path));
  return Val_bool(Result == 0);
}

/* ?unbuffered:bool -> Llvm.llmodule -> Unix.file_descr -> bool */
value llvm_write_bitcode_to_fd(value U, value M, value FD) {
  int Unbuffered;
  int Result;

  if (U == Val_int(0)) {
    Unbuffered = 0;
  } else {
    Unbuffered = Bool_val(Field(U, 0));
  }

  Result = LLVMWriteBitcodeToFD(Module_val(M), Int_val(FD), 0, Unbuffered);
  return Val_bool(Result == 0);
}

/* Llvm.llmodule -> Llvm.llmemorybuffer */
value llvm_write_bitcode_to_memory_buffer(value M) {
  return to_val(LLVMWriteBitcodeToMemoryBuffer(Module_val(M)));
}
