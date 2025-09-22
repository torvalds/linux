(*===-- llvm_irreader.ml - LLVM OCaml Interface ---------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)


exception Error of string

let _ = Callback.register_exception "Llvm_irreader.Error" (Error "")

external parse_ir : Llvm.llcontext -> Llvm.llmemorybuffer -> Llvm.llmodule
                  = "llvm_parse_ir"
