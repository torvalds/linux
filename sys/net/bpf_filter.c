/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1991, 1993
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
 *      @(#)bpf_filter.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#if !defined(_KERNEL)
#include <strings.h>
#endif
#if !defined(_KERNEL) || defined(sun)
#include <netinet/in.h>
#endif

#ifndef __i386__
#define BPF_ALIGN
#endif

#ifndef BPF_ALIGN
#define EXTRACT_SHORT(p)	((u_int16_t)ntohs(*(u_int16_t *)p))
#define EXTRACT_LONG(p)		(ntohl(*(u_int32_t *)p))
#else
#define EXTRACT_SHORT(p)\
	((u_int16_t)\
		((u_int16_t)*((u_char *)p+0)<<8|\
		 (u_int16_t)*((u_char *)p+1)<<0))
#define EXTRACT_LONG(p)\
		((u_int32_t)*((u_char *)p+0)<<24|\
		 (u_int32_t)*((u_char *)p+1)<<16|\
		 (u_int32_t)*((u_char *)p+2)<<8|\
		 (u_int32_t)*((u_char *)p+3)<<0)
#endif

#ifdef _KERNEL
#include <sys/mbuf.h>
#else
#include <stdlib.h>
#endif
#include <net/bpf.h>
#ifdef _KERNEL
#define MINDEX(m, k) \
{ \
	int len = m->m_len; \
 \
	while (k >= len) { \
		k -= len; \
		m = m->m_next; \
		if (m == 0) \
			return (0); \
		len = m->m_len; \
	} \
}

static u_int16_t	m_xhalf(struct mbuf *m, bpf_u_int32 k, int *err);
static u_int32_t	m_xword(struct mbuf *m, bpf_u_int32 k, int *err);

static u_int32_t
m_xword(struct mbuf *m, bpf_u_int32 k, int *err)
{
	size_t len;
	u_char *cp, *np;
	struct mbuf *m0;

	len = m->m_len;
	while (k >= len) {
		k -= len;
		m = m->m_next;
		if (m == NULL)
			goto bad;
		len = m->m_len;
	}
	cp = mtod(m, u_char *) + k;
	if (len - k >= 4) {
		*err = 0;
		return (EXTRACT_LONG(cp));
	}
	m0 = m->m_next;
	if (m0 == NULL || m0->m_len + len - k < 4)
		goto bad;
	*err = 0;
	np = mtod(m0, u_char *);
	switch (len - k) {
	case 1:
		return (((u_int32_t)cp[0] << 24) |
		    ((u_int32_t)np[0] << 16) |
		    ((u_int32_t)np[1] << 8)  |
		    (u_int32_t)np[2]);

	case 2:
		return (((u_int32_t)cp[0] << 24) |
		    ((u_int32_t)cp[1] << 16) |
		    ((u_int32_t)np[0] << 8) |
		    (u_int32_t)np[1]);

	default:
		return (((u_int32_t)cp[0] << 24) |
		    ((u_int32_t)cp[1] << 16) |
		    ((u_int32_t)cp[2] << 8) |
		    (u_int32_t)np[0]);
	}
    bad:
	*err = 1;
	return (0);
}

static u_int16_t
m_xhalf(struct mbuf *m, bpf_u_int32 k, int *err)
{
	size_t len;
	u_char *cp;
	struct mbuf *m0;

	len = m->m_len;
	while (k >= len) {
		k -= len;
		m = m->m_next;
		if (m == NULL)
			goto bad;
		len = m->m_len;
	}
	cp = mtod(m, u_char *) + k;
	if (len - k >= 2) {
		*err = 0;
		return (EXTRACT_SHORT(cp));
	}
	m0 = m->m_next;
	if (m0 == NULL)
		goto bad;
	*err = 0;
	return ((cp[0] << 8) | mtod(m0, u_char *)[0]);
 bad:
	*err = 1;
	return (0);
}
#endif

/*
 * Execute the filter program starting at pc on the packet p
 * wirelen is the length of the original packet
 * buflen is the amount of data present
 */
u_int
bpf_filter(const struct bpf_insn *pc, u_char *p, u_int wirelen, u_int buflen)
{
	u_int32_t A = 0, X = 0;
	bpf_u_int32 k;
	u_int32_t mem[BPF_MEMWORDS];

	bzero(mem, sizeof(mem));

	if (pc == NULL)
		/*
		 * No filter means accept all.
		 */
		return ((u_int)-1);

	--pc;
	while (1) {
		++pc;
		switch (pc->code) {
		default:
#ifdef _KERNEL
			return (0);
#else
			abort();
#endif

		case BPF_RET|BPF_K:
			return ((u_int)pc->k);

		case BPF_RET|BPF_A:
			return ((u_int)A);

		case BPF_LD|BPF_W|BPF_ABS:
			k = pc->k;
			if (k > buflen || sizeof(int32_t) > buflen - k) {
#ifdef _KERNEL
				int merr;

				if (buflen != 0)
					return (0);
				A = m_xword((struct mbuf *)p, k, &merr);
				if (merr != 0)
					return (0);
				continue;
#else
				return (0);
#endif
			}
#ifdef BPF_ALIGN
			if (((intptr_t)(p + k) & 3) != 0)
				A = EXTRACT_LONG(&p[k]);
			else
#endif
				A = ntohl(*(int32_t *)(p + k));
			continue;

		case BPF_LD|BPF_H|BPF_ABS:
			k = pc->k;
			if (k > buflen || sizeof(int16_t) > buflen - k) {
#ifdef _KERNEL
				int merr;

				if (buflen != 0)
					return (0);
				A = m_xhalf((struct mbuf *)p, k, &merr);
				continue;
#else
				return (0);
#endif
			}
			A = EXTRACT_SHORT(&p[k]);
			continue;

		case BPF_LD|BPF_B|BPF_ABS:
			k = pc->k;
			if (k >= buflen) {
#ifdef _KERNEL
				struct mbuf *m;

				if (buflen != 0)
					return (0);
				m = (struct mbuf *)p;
				MINDEX(m, k);
				A = mtod(m, u_char *)[k];
				continue;
#else
				return (0);
#endif
			}
			A = p[k];
			continue;

		case BPF_LD|BPF_W|BPF_LEN:
			A = wirelen;
			continue;

		case BPF_LDX|BPF_W|BPF_LEN:
			X = wirelen;
			continue;

		case BPF_LD|BPF_W|BPF_IND:
			k = X + pc->k;
			if (pc->k > buflen || X > buflen - pc->k ||
			    sizeof(int32_t) > buflen - k) {
#ifdef _KERNEL
				int merr;

				if (buflen != 0)
					return (0);
				A = m_xword((struct mbuf *)p, k, &merr);
				if (merr != 0)
					return (0);
				continue;
#else
				return (0);
#endif
			}
#ifdef BPF_ALIGN
			if (((intptr_t)(p + k) & 3) != 0)
				A = EXTRACT_LONG(&p[k]);
			else
#endif
				A = ntohl(*(int32_t *)(p + k));
			continue;

		case BPF_LD|BPF_H|BPF_IND:
			k = X + pc->k;
			if (X > buflen || pc->k > buflen - X ||
			    sizeof(int16_t) > buflen - k) {
#ifdef _KERNEL
				int merr;

				if (buflen != 0)
					return (0);
				A = m_xhalf((struct mbuf *)p, k, &merr);
				if (merr != 0)
					return (0);
				continue;
#else
				return (0);
#endif
			}
			A = EXTRACT_SHORT(&p[k]);
			continue;

		case BPF_LD|BPF_B|BPF_IND:
			k = X + pc->k;
			if (pc->k >= buflen || X >= buflen - pc->k) {
#ifdef _KERNEL
				struct mbuf *m;

				if (buflen != 0)
					return (0);
				m = (struct mbuf *)p;
				MINDEX(m, k);
				A = mtod(m, u_char *)[k];
				continue;
#else
				return (0);
#endif
			}
			A = p[k];
			continue;

		case BPF_LDX|BPF_MSH|BPF_B:
			k = pc->k;
			if (k >= buflen) {
#ifdef _KERNEL
				struct mbuf *m;

				if (buflen != 0)
					return (0);
				m = (struct mbuf *)p;
				MINDEX(m, k);
				X = (mtod(m, u_char *)[k] & 0xf) << 2;
				continue;
#else
				return (0);
#endif
			}
			X = (p[pc->k] & 0xf) << 2;
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
				return (0);
			A /= X;
			continue;

		case BPF_ALU|BPF_MOD|BPF_X:
			if (X == 0)
				return (0);
			A %= X;
			continue;

		case BPF_ALU|BPF_AND|BPF_X:
			A &= X;
			continue;

		case BPF_ALU|BPF_OR|BPF_X:
			A |= X;
			continue;

		case BPF_ALU|BPF_XOR|BPF_X:
			A ^= X;
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

		case BPF_ALU|BPF_MOD|BPF_K:
			A %= pc->k;
			continue;

		case BPF_ALU|BPF_AND|BPF_K:
			A &= pc->k;
			continue;

		case BPF_ALU|BPF_OR|BPF_K:
			A |= pc->k;
			continue;

		case BPF_ALU|BPF_XOR|BPF_K:
			A ^= pc->k;
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
static const u_short	bpf_code_map[] = {
	0x10ff,	/* 0x00-0x0f: 1111111100001000 */
	0x3070,	/* 0x10-0x1f: 0000111000001100 */
	0x3131,	/* 0x20-0x2f: 1000110010001100 */
	0x3031,	/* 0x30-0x3f: 1000110000001100 */
	0x3131,	/* 0x40-0x4f: 1000110010001100 */
	0x1011,	/* 0x50-0x5f: 1000100000001000 */
	0x1013,	/* 0x60-0x6f: 1100100000001000 */
	0x1010,	/* 0x70-0x7f: 0000100000001000 */
	0x0093,	/* 0x80-0x8f: 1100100100000000 */
	0x1010,	/* 0x90-0x9f: 0000100000001000 */
	0x1010,	/* 0xa0-0xaf: 0000100000001000 */
	0x0002,	/* 0xb0-0xbf: 0100000000000000 */
	0x0000,	/* 0xc0-0xcf: 0000000000000000 */
	0x0000,	/* 0xd0-0xdf: 0000000000000000 */
	0x0000,	/* 0xe0-0xef: 0000000000000000 */
	0x0000	/* 0xf0-0xff: 0000000000000000 */
};

#define	BPF_VALIDATE_CODE(c)	\
    ((c) <= 0xff && (bpf_code_map[(c) >> 4] & (1 << ((c) & 0xf))) != 0)

/*
 * Return true if the 'fcode' is a valid filter program.
 * The constraints are that each jump be forward and to a valid
 * code.  The code must terminate with either an accept or reject.
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
int
bpf_validate(const struct bpf_insn *f, int len)
{
	int i;
	const struct bpf_insn *p;

	/* Do not accept negative length filter. */
	if (len < 0)
		return (0);

	/* An empty filter means accept all. */
	if (len == 0)
		return (1);

	for (i = 0; i < len; ++i) {
		p = &f[i];
		/*
		 * Check that the code is valid.
		 */
		if (!BPF_VALIDATE_CODE(p->code))
			return (0);
		/*
		 * Check that that jumps are forward, and within
		 * the code block.
		 */
		if (BPF_CLASS(p->code) == BPF_JMP) {
			u_int offset;

			if (p->code == (BPF_JMP|BPF_JA))
				offset = p->k;
			else
				offset = p->jt > p->jf ? p->jt : p->jf;
			if (offset >= (u_int)(len - i) - 1)
				return (0);
			continue;
		}
		/*
		 * Check that memory operations use valid addresses.
		 */
		if (p->code == BPF_ST || p->code == BPF_STX ||
		    p->code == (BPF_LD|BPF_MEM) ||
		    p->code == (BPF_LDX|BPF_MEM)) {
			if (p->k >= BPF_MEMWORDS)
				return (0);
			continue;
		}
		/*
		 * Check for constant division by 0.
		 */
		if ((p->code == (BPF_ALU|BPF_DIV|BPF_K) ||
		    p->code == (BPF_ALU|BPF_MOD|BPF_K)) && p->k == 0)
			return (0);
	}
	return (BPF_CLASS(f[len - 1].code) == BPF_RET);
}
#endif
