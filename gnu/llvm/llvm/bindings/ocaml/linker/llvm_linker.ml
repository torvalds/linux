(*===-- llvm_linker.ml - LLVM OCaml Interface ------------------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

exception Error of string

let () = Callback.register_exception "Llvm_linker.Error" (Error "")

external link_modules : Llvm.llmodule -> Llvm.llmodule -> unit
                      = "llvm_link_modules"
