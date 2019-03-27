/*
 * Copyright (C) 2017 Michal Meloun
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
 */

#include <machine/asm.h>
__FBSDID("$FreeBSD$");

/*
 * Due to bug in libcompiler_rt, all symbols declared by
 * DEFINE_AEABI_FUNCTION_ALIAS() are not hidden. All these but
 * __aeabi_uidiv_compat and/or __aeabi_idiv_compat  are explicitly
 * exported from libc and don't causes problems.
 *
 * As workaround, export these from libc as compatible symbols,
 * in global namespace
 */

ENTRY(__aeabi_uidiv_compat)
	.symver __aeabi_uidiv_compat, __aeabi_uidiv@
	b __udivsi3
END(__aeabi_uidiv_compat)

ENTRY(__aeabi_idiv_compat)
	.symver __aeabi_idiv_compat, __aeabi_idiv@
	b __divsi3
END(__aeabi_idiv_compat)

	.section .note.GNU-stack,"",%progbits
