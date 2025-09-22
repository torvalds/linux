/*	$OpenBSD: bpf_filter.c,v 1.35 2025/07/07 02:28:50 jsg Exp $	*/
/*	$NetBSD: bpf_filter.c,v 1.12 1996/02/13 22:00:00 christos Exp $	*/

/*
 * Copyright (c) 1990, 1991, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)bpf_filter.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#ifndef _KERNEL
#include <stdlib.h>
#include <string.h>
#include "pcap.h"
#else
#include <sys/systm.h>
#endif

#ifdef _KERNEL
extern int bpf_maxbufsize;
#define Static
#else /* _KERNEL */
#define Static static
#endif /* _KERNEL */

#include <net/bpf.h>

struct bpf_mem {
	const u_char	*pkt;
	u_int		 len;
};

Static u_int32_t	bpf_mem_ldw(const void *, u_int32_t, int *);
Static u_int32_t	bpf_mem_ldh(const void *, u_int32_t, int *);
Static u_int32_t	bpf_mem_ldb(const void *, u_int32_t, int *);

static const struct bpf_ops bpf_mem_ops = {
	bpf_mem_ldw,
	bpf_mem_ldh,
	bpf_mem_ldb,
};

Static u_int32_t
bpf_mem_ldw(const void *mem, u_int32_t k, int *err)
{
	const struct bpf_mem *bm = mem;
	u_int32_t v;

	*err = 1;

	if (k + sizeof(v) > bm->len)
		return (0);

	memcpy(&v, bm->pkt + k, sizeof(v));

	*err = 0;
	return ntohl(v);
}

Static u_int32_t
bpf_mem_ldh(const void *mem, u_int32_t k, int *err)
{
	const struct bpf_mem *bm = mem;
	u_int16_t v;

	*err = 1;

	if (k + sizeof(v) > bm->len)
		return (0);

	memcpy(&v, bm->pkt + k, sizeof(v));

	*err = 0;
	return ntohs(v);
}

Static u_int32_t
bpf_mem_ldb(const void *mem, u_int32_t k, int *err)
{
	const struct bpf_mem *bm = mem;

	*err = 1;

	if (k >= bm->len)
		return (0);

	*err = 0;
	return bm->pkt[k];
}

/*
 * Execute the filter program starting at pc on the packet p
 * wirelen is the length of the original packet
 * buflen is the amount of data present
 */
u_int
bpf_filter(const struct bpf_insn *pc, const u_char *pkt,
    u_int wirelen, u_int buflen)
{
	struct bpf_mem bm;

	bm.pkt = pkt;
	bm.len = buflen;

	return _bpf_filter(pc, &bpf_mem_ops, &bm, wirelen);
}

u_int
_bpf_filter(const struct bpf_insn *pc, const struct bpf_ops *ops,
    const void *pkt, u_int wirelen)
{
	u_int32_t A = 0, X = 0;
	u_int32_t k;
	int32_t mem[BPF_MEMWORDS];
	int err;

	if (pc == NULL) {
		/*
		 * No filter means accept all.
		 */
		return (u_int)-1;
	}

	memset(mem, 0, sizeof(mem));

	--pc;
	while (1) {
		++pc;
		switch (pc->code) {

		default:
#ifdef _KERNEL
			return 0;
#else
			abort();
#endif
		case BPF_RET|BPF_K:
			return (u_int)pc->k;

		case BPF_RET|BPF_A:
			return (u_int)A;

		case BPF_LD|BPF_W|BPF_ABS:
			A = ops->ldw(pkt, pc->k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LD|BPF_H|BPF_ABS:
			A = ops->ldh(pkt, pc->k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LD|BPF_B|BPF_ABS:
			A = ops->ldb(pkt, pc->k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LD|BPF_W|BPF_LEN:
			A = wirelen;
			continue;

		case BPF_LDX|BPF_W|BPF_LEN:
			X = wirelen;
			continue;

		case BPF_LD|BPF_W|BPF_RND:
			A = arc4random();
			continue;

		case BPF_LD|BPF_W|BPF_IND:
			k = X + pc->k;
			A = ops->ldw(pkt, k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LD|BPF_H|BPF_IND:
			k = X + pc->k;
			A = ops->ldh(pkt, k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LD|BPF_B|BPF_IND:
			k = X + pc->k;
			A = ops->ldb(pkt, k, &err);
			if (err != 0)
				return 0;
			continue;

		case BPF_LDX|BPF_MSH|BPF_B:
			X = ops->ldb(pkt, pc->k, &err);
			if (err != 0)
				return 0;
			X &= 0xf;
			X <<= 2;
			continue;

		case BPF_LD|BPF_IMM:
			A = pc->k;
			continue;

		case BPF_LDX|BPF_IMM:
			X = pc->k;
			continue;

		case BPF_LD|BPF_MEM:
			A = mem[pc->k];
			continue;

		case BPF_LDX|BPF_MEM:
			X = mem[pc->k];
			continue;

		case BPF_ST:
			mem[pc->k] = A;
			continue;

		case BPF_STX:
			mem[pc->k] = X;
			continue;

		case BPF_JMP|BPF_JA:
			pc += pc->k;
			continue;

		case BPF_JMP|BPF_JGT|BPF_K:
			pc += (A > pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGE|BPF_K:
			pc += (A >= pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JEQ|BPF_K:
			pc += (A == pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JSET|BPF_K:
			pc += (A & pc->k) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGT|BPF_X:
			pc += (A > X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JGE|BPF_X:
			pc += (A >= X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JEQ|BPF_X:
			pc += (A == X) ? pc->jt : pc->jf;
			continue;

		case BPF_JMP|BPF_JSET|BPF_X:
			pc += (A & X) ? pc->jt : pc->jf;
			continue;

		case BPF_ALU|BPF_ADD|BPF_X:
			A += X;
			continue;

		case BPF_ALU|BPF_SUB|BPF_X:
			A -= X;
			continue;

		case BPF_ALU|BPF_MUL|BPF_X:
			A *= X;
			continue;

		case BPF_ALU|BPF_DIV|BPF_X:
			if (X == 0)
				return 0;
			A /= X;
			continue;

		case BPF_ALU|BPF_AND|BPF_X:
			A &= X;
			continue;

		case BPF_ALU|BPF_OR|BPF_X:
			A |= X;
			continue;

		case BPF_ALU|BPF_LSH|BPF_X:
			A <<= X;
			continue;

		case BPF_ALU|BPF_RSH|BPF_X:
			A >>= X;
			continue;

		case BPF_ALU|BPF_ADD|BPF_K:
			A += pc->k;
			continue;

		case BPF_ALU|BPF_SUB|BPF_K:
			A -= pc->k;
			continue;

		case BPF_ALU|BPF_MUL|BPF_K:
			A *= pc->k;
			continue;

		case BPF_ALU|BPF_DIV|BPF_K:
			A /= pc->k;
			continue;

		case BPF_ALU|BPF_AND|BPF_K:
			A &= pc->k;
			continue;

		case BPF_ALU|BPF_OR|BPF_K:
			A |= pc->k;
			continue;

		case BPF_ALU|BPF_LSH|BPF_K:
			A <<= pc->k;
			continue;

		case BPF_ALU|BPF_RSH|BPF_K:
			A >>= pc->k;
			continue;

		case BPF_ALU|BPF_NEG:
			A = -A;
			continue;

		case BPF_MISC|BPF_TAX:
			X = A;
			continue;

		case BPF_MISC|BPF_TXA:
			A = X;
			continue;
		}
	}
}

#ifdef _KERNEL
/*
 * Return true if the 'fcode' is a valid filter program.
 * The constraints are that each jump be forward and to a valid
 * code and memory operations use valid addresses.  The code
 * must terminate with either an accept or reject.
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
int
bpf_validate(struct bpf_insn *f, int len)
{
	u_int i, from;
	struct bpf_insn *p;

	if (len < 1 || len > BPF_MAXINSNS)
		return 0;

	for (i = 0; i < len; ++i) {
		p = &f[i];
		switch (BPF_CLASS(p->code)) {
		/*
		 * Check that memory operations use valid addresses.
		 */
		case BPF_LD:
		case BPF_LDX:
			switch (BPF_MODE(p->code)) {
			case BPF_IMM:
				break;
			case BPF_ABS:
			case BPF_IND:
			case BPF_MSH:
				/*
				 * More strict check with actual packet length
				 * is done runtime.
				 */
				if (p->k >= bpf_maxbufsize)
					return 0;
				break;
			case BPF_MEM:
				if (p->k >= BPF_MEMWORDS)
					return 0;
				break;
			case BPF_LEN:
			case BPF_RND:
				break;
			default:
				return 0;
			}
			break;
		case BPF_ST:
		case BPF_STX:
			if (p->k >= BPF_MEMWORDS)
				return 0;
			break;
		case BPF_ALU:
			switch (BPF_OP(p->code)) {
			case BPF_ADD:
			case BPF_SUB:
			case BPF_MUL:
			case BPF_OR:
			case BPF_AND:
			case BPF_LSH:
			case BPF_RSH:
			case BPF_NEG:
				break;
			case BPF_DIV:
				/*
				 * Check for constant division by 0.
				 */
				if (BPF_SRC(p->code) == BPF_K && p->k == 0)
					return 0;
				break;
			default:
				return 0;
			}
			break;
		case BPF_JMP:
			/*
			 * Check that jumps are forward, and within
			 * the code block.
			 */
			from = i + 1;
			switch (BPF_OP(p->code)) {
			case BPF_JA:
				if (from + p->k < from || from + p->k >= len)
					return 0;
				break;
			case BPF_JEQ:
			case BPF_JGT:
			case BPF_JGE:
			case BPF_JSET:
				if (from + p->jt >= len || from + p->jf >= len)
					return 0;
				break;
			default:
				return 0;
			}
			break;
		case BPF_RET:
			break;
		case BPF_MISC:
			break;
		default:
			return 0;
		}

	}
	return BPF_CLASS(f[len - 1].code) == BPF_RET;
}
#endif
