/*-
 * Copyright (c) 2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <machine/asmacros.h>
#include <amd64/sgx/sgxvar.h>

#include "assym.inc"

	.text

/*
 * int
 * sgx_encls(uint32_t eax, uint64_t rbx, uint64_t rcx, uint64_t rdx);
 *           %edi,         %rsi,         %rdx,         %rcx
 */
ENTRY(sgx_encls)
	PUSH_FRAME_POINTER
	pushq	%rbx
	movq	PCPU(CURPCB),%r8
	movl	%edi,%eax
	movq	%rsi,%rbx
	xchgq	%rdx,%rcx
	movq	$sgx_onfault,PCB_ONFAULT(%r8)
	.byte 0x0f, 0x01, 0xcf
	movq	$0,PCB_ONFAULT(%r8)
	popq	%rbx
	POP_FRAME_POINTER
	ret
END(sgx_encls)

/*
 * SGX operations fault handler
 */
	ALIGN_TEXT
sgx_onfault:
	movq	$0,PCB_ONFAULT(%r8)
	movl	$SGX_EFAULT,%eax
	popq	%rbx
	POP_FRAME_POINTER
	ret
