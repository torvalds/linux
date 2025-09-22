(*===-- llvm_bitreader.ml - LLVM OCaml Interface --------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

exception Error of string

let () = Callback.register_exception "Llvm_bitreader.Error" (Error "")

external get_module
  : Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule
  = "llvm_get_module"
external parse_bitcode
  : Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule
  = "llvm_parse_bitcode"
