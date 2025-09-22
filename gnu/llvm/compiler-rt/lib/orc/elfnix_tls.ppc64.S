//===-- orc_rt_elfnix_tls.ppc64.s -------------------------------*- ASM -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of the ORC runtime support library.
//
//===----------------------------------------------------------------------===//

// The content of this file is PowerPC64 only.
#if defined(__powerpc64__)

  .text
  // TODO: add fast-path for repeat access.
  // See https://github.com/llvm/llvm-project/issues/51162.
  .global ___orc_rt_elfnix_tls_get_addr
___orc_rt_elfnix_tls_get_addr:
  addis 2, 12, .TOC.-___orc_rt_elfnix_tls_get_addr@ha
  addi 2, 2, .TOC.-___orc_rt_elfnix_tls_get_addr@l
  mflr 0
  std 0, 16(1)
  stdu 1, -32(1)
  bl __orc_rt_elfnix_tls_get_addr_impl
  nop
  addi 1, 1, 32
  ld 0, 16(1)
  mtlr 0
  blr

#endif
