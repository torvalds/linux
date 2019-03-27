/*-
 * Copyright (C) 2004 NVIDIA Corporation.
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
 *	$FreeBSD$
 */

.file "elftls.S"

.globl __gl_tls_offsets
    .type __gl_tls_offsets,@object

.section .tbss,"awT",@nobits

.globl __gl_tls_var0
    .align 4
    .type   __gl_tls_var0, @object
    .size   __gl_tls_var0, 4
__gl_tls_var0:
    .zero   4

.globl __gl_tls_var1
    .align 4
    .type   __gl_tls_var1, @object
    .size   __gl_tls_var1, 4
__gl_tls_var1:
    .zero   4

.globl __gl_tls_var2
    .align 4
    .type   __gl_tls_var2, @object
    .size   __gl_tls_var2, 4
__gl_tls_var2:
    .zero   4

.globl __gl_tls_var3
    .align 4
    .type   __gl_tls_var3, @object
    .size   __gl_tls_var3, 4
__gl_tls_var3:
    .zero   4

.globl __gl_tls_var4
    .align 4
    .type   __gl_tls_var4, @object
    .size   __gl_tls_var4, 4
__gl_tls_var4:
    .zero   4

.globl __gl_tls_var5
    .align 4
    .type   __gl_tls_var5, @object
    .size   __gl_tls_var5, 4
__gl_tls_var5:
    .zero   4

.globl __gl_tls_var6
    .align 4
    .type   __gl_tls_var6, @object
    .size   __gl_tls_var6, 4
__gl_tls_var6:
    .zero   4

.globl __gl_tls_var7
    .align 4
    .type   __gl_tls_var7, @object
    .size   __gl_tls_var7, 4
__gl_tls_var7:
    .zero   4

.text
    .p2align 4,,15
.globl __gl_tls_init_offsets
    .type __gl_tls_init_offsets, @function
__gl_tls_init_offsets:
    movl $__gl_tls_offsets, %eax
    movl $__gl_tls_var0@ntpoff, %ecx
    movl %ecx, 0(%eax)
    movl $__gl_tls_var1@ntpoff, %ecx
    movl %ecx, 4(%eax)
    movl $__gl_tls_var2@ntpoff, %ecx
    movl %ecx, 8(%eax)
    movl $__gl_tls_var3@ntpoff, %ecx
    movl %ecx, 12(%eax)
    movl $__gl_tls_var4@ntpoff, %ecx
    movl %ecx, 16(%eax)
    movl $__gl_tls_var5@ntpoff, %ecx
    movl %ecx, 20(%eax)
    movl $__gl_tls_var6@ntpoff, %ecx
    movl %ecx, 24(%eax)
    movl $__gl_tls_var7@ntpoff, %ecx
    movl %ecx, 28(%eax)
    ret
