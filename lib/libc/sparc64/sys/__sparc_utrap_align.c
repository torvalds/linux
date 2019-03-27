/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Jake Burkholder.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <machine/cpufunc.h>
#include <machine/instr.h>

#include <signal.h>

#include "__sparc_utrap_private.h"

static u_long
__unaligned_load(u_char *p, int size)
{
	u_long val;
	int i;

	val = 0;
	for (i = 0; i < size; i++)
		val = (val << 8) | p[i];
	return (val);
}

static void
__unaligned_store(u_char *p, u_long val, int size)
{
	int i;

	for (i = 0; i < size; i++)
		p[i] = val >> ((size - i - 1) * 8);
}

int
__unaligned_fixup(struct utrapframe *uf)
{
	u_char *addr;
	u_long val;
	u_int insn;
	int sig;

	sig = 0;
	addr = (u_char *)uf->uf_sfar;
	insn = *(u_int *)uf->uf_pc;
	flushw();
	switch (IF_OP(insn)) {
	case IOP_LDST:
		switch (IF_F3_OP3(insn)) {
		case INS3_LDUH:
			val = __unaligned_load(addr, 2);
			__emul_store_reg(uf, IF_F3_RD(insn), val);
			break;
		case INS3_LDUW:
			val = __unaligned_load(addr, 4);
			__emul_store_reg(uf, IF_F3_RD(insn), val);
			break;
		case INS3_LDX:
			val = __unaligned_load(addr, 8);
			__emul_store_reg(uf, IF_F3_RD(insn), val);
			break;
		case INS3_LDSH:
			val = __unaligned_load(addr, 2);
			__emul_store_reg(uf, IF_F3_RD(insn),
			    IF_SEXT(val, 16));
			break;
		case INS3_LDSW:
			val = __unaligned_load(addr, 4);
			__emul_store_reg(uf, IF_F3_RD(insn),
			    IF_SEXT(val, 32));
			break;
		case INS3_STH:
			val = __emul_fetch_reg(uf, IF_F3_RD(insn));
			__unaligned_store(addr, val, 2);
			break;
		case INS3_STW:
			val = __emul_fetch_reg(uf, IF_F3_RD(insn));
			__unaligned_store(addr, val, 4);
			break;
		case INS3_STX:
			val = __emul_fetch_reg(uf, IF_F3_RD(insn));
			__unaligned_store(addr, val, 8);
			break;
		default:
			sig = SIGILL;
			break;
		}
		break;
	default:
		sig = SIGILL;
		break;
	}
	return (sig);
}
