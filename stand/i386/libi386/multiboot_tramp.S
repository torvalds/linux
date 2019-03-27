/*-
 * Copyright (c) 2014  Roger Pau Monné <royger@FreeBSD.org>
 * All rights reserved.
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

#define ASM_FILE
#include "multiboot.h"

/*
 * The multiboot specification requires the executable to be launched
 * with %cs set to a flat read/execute segment with offset 0 and limit
 * 0xFFFFFFFF, and the rest of the segment registers (%ds, %es, %fs,
 * %gs, %ss) to flat read/write segments with the same offset and limit.
 * This is already done by the BTX code before calling multiboot_tramp,
 * so there is no need to do anything here.
 */

	.globl	multiboot_tramp
multiboot_tramp:
	/* Be sure that interrupts are disabled. */
	cli

	movl    $MULTIBOOT_BOOTLOADER_MAGIC, %eax
	/* Get the entry point and address of the multiboot_info parameter. */
	movl	8(%esp), %ebx
	movl	4(%esp), %ecx

	call    *%ecx
