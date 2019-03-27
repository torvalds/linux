/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 Jake Burkholder.
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

#ifndef	___SPARC_UTRAP_PRIVATE_H_
#define	___SPARC_UTRAP_PRIVATE_H_

#define	UF_DONE(uf) do {						\
	uf->uf_pc = uf->uf_npc;						\
	uf->uf_npc = uf->uf_pc + 4;					\
} while (0)

struct utrapframe {
	u_long	uf_global[8];
	u_long	uf_out[8];
	u_long	uf_pc;
	u_long	uf_npc;
	u_long	uf_sfar;
	u_long	uf_sfsr;
	u_long	uf_tar;
	u_long	uf_type;
	u_long	uf_state;
	u_long	uf_fsr;
};

extern char __sparc_utrap_fp_disabled[];
extern char __sparc_utrap_gen[];

int __emul_insn(struct utrapframe *uf);
u_long __emul_fetch_reg(struct utrapframe *uf, int reg);
void __emul_store_reg(struct utrapframe *uf, int reg, u_long val);
u_long __emul_f3_op2(struct utrapframe *uf, u_int insn);
u_long __emul_f3_memop_addr(struct utrapframe *uf, u_int insn);
int __unaligned_fixup(struct utrapframe *uf);

void __sparc_utrap(struct utrapframe *);

void __utrap_write(const char *);
void __utrap_kill_self(int);
void __utrap_panic(const char *);

#endif
