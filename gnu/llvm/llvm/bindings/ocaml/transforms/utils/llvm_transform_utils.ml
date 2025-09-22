(*===-- llvm_transform_utils.ml - LLVM OCaml Interface --------*- OCaml -*-===*
 *
 * Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
 * See https://llvm.org/LICENSE.txt for license information.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 *===----------------------------------------------------------------------===*)

external clone_module : Llvm.llmodule -> Llvm.llmodule = "llvm_clone_module"
