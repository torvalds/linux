/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Anish Gupta (akgupt3@gmail.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/assym.h>
#include <x86/specialreg.h>

#include "svm.h"

ASSYM(SCTX_RBX, offsetof(struct svm_regctx, sctx_rbx));
ASSYM(SCTX_RCX, offsetof(struct svm_regctx, sctx_rcx));
ASSYM(SCTX_RBP, offsetof(struct svm_regctx, sctx_rbp));
ASSYM(SCTX_RDX, offsetof(struct svm_regctx, sctx_rdx));
ASSYM(SCTX_RDI, offsetof(struct svm_regctx, sctx_rdi));
ASSYM(SCTX_RSI, offsetof(struct svm_regctx, sctx_rsi));
ASSYM(SCTX_R8,  offsetof(struct svm_regctx, sctx_r8));
ASSYM(SCTX_R9,  offsetof(struct svm_regctx, sctx_r9));
ASSYM(SCTX_R10, offsetof(struct svm_regctx, sctx_r10));
ASSYM(SCTX_R11, offsetof(struct svm_regctx, sctx_r11));
ASSYM(SCTX_R12, offsetof(struct svm_regctx, sctx_r12));
ASSYM(SCTX_R13, offsetof(struct svm_regctx, sctx_r13));
ASSYM(SCTX_R14, offsetof(struct svm_regctx, sctx_r14));
ASSYM(SCTX_R15, offsetof(struct svm_regctx, sctx_r15));
ASSYM(MSR_GSBASE, MSR_GSBASE);
