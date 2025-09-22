
//===-- orc_rt_elfnix_tls_x86-64.s -------------------------------*- ASM -*-===//
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

// The content of this file is x86_64-only
#if defined(__x86_64__)

#define REGISTER_SAVE_SPACE_SIZE        512

        .text

	// returns address of TLV in %rax, all other registers preserved
	.globl ___orc_rt_elfnix_tls_get_addr
___orc_rt_elfnix_tls_get_addr:
        pushq           %rbp
        movq            %rsp,        %rbp
        subq            $REGISTER_SAVE_SPACE_SIZE, %rsp
        movq            %rcx,    -16(%rbp)
        movq            %rdx,    -24(%rbp)
        movq            %rsi,    -32(%rbp)
        movq            %rdi,    -40(%rbp)
        movq            %r8,     -48(%rbp)
        movq            %r9,     -56(%rbp)
        movq            %r10,    -64(%rbp)
        movq            %r11,    -72(%rbp)
	movdqa          %xmm0,  -128(%rbp)
	movdqa          %xmm1,  -144(%rbp)
	movdqa          %xmm2,  -160(%rbp)
	movdqa          %xmm3,  -176(%rbp)
	movdqa          %xmm4,  -192(%rbp)
	movdqa          %xmm5,  -208(%rbp)
	movdqa          %xmm6,  -224(%rbp)
	movdqa          %xmm7,  -240(%rbp)
        call           __orc_rt_elfnix_tls_get_addr_impl
        movq            -16(%rbp),      %rcx
        movq            -24(%rbp),      %rdx
        movq            -32(%rbp),      %rsi
        movq            -40(%rbp),      %rdi
        movq            -48(%rbp),      %r8
        movq            -56(%rbp),      %r9
        movq            -64(%rbp),      %r10
        movq            -72(%rbp),      %r11
  movdqa          -128(%rbp),     %xmm0
	movdqa          -144(%rbp),     %xmm1
	movdqa          -160(%rbp),     %xmm2
	movdqa          -176(%rbp),     %xmm3
	movdqa          -192(%rbp),     %xmm4
	movdqa          -208(%rbp),     %xmm5
	movdqa          -224(%rbp),     %xmm6
	movdqa          -240(%rbp),     %xmm7
        addq            $REGISTER_SAVE_SPACE_SIZE, %rsp
        popq            %rbp
        ret

#endif // defined(__x86_64__)
