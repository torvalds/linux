/*-
 * Copyright (C) 2008-2009 Jung-uk Kim <jkim@FreeBSD.org>. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>

#include <net/bpf.h>

#include BPF_TEST_H

#define	PASSED		0
#define	FAILED		1
#define	FATAL		-1

#ifndef LOG_LEVEL
#define	LOG_LEVEL	1
#endif

#ifdef BPF_BENCHMARK
#define	BPF_NRUNS	10000000
#else
#define	BPF_NRUNS	1
#endif

static void	sig_handler(int);

static int	nins = sizeof(pc) / sizeof(pc[0]);
static int	verbose = LOG_LEVEL;

#ifdef BPF_JIT_COMPILER

#include <libutil.h>

#include <net/bpf_jitter.h>

static u_int
bpf_compile_and_filter(void)
{
	bpf_jit_filter	*filter;
	u_int		i, ret;

	/* Compile the BPF filter program and generate native code. */
	if ((filter = bpf_jitter(pc, nins)) == NULL) {
		if (verbose > 1)
			printf("Failed to allocate memory:\t");
		if (verbose > 0)
			printf("FATAL\n");
		exit(FATAL);
	}
	if (verbose > 2) {
		printf("\n");
		hexdump(filter->func, filter->size, NULL, HD_OMIT_CHARS);
	}

	for (i = 0; i < BPF_NRUNS; i++)
		ret = (*(filter->func))(pkt, wirelen, buflen);

	bpf_destroy_jit_filter(filter);

	return (ret);
}

#else

u_int	bpf_filter(const struct bpf_insn *, u_char *, u_int, u_int);

#endif

#ifdef BPF_VALIDATE
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
 * XXX Copied from sys/net/bpf_filter.c and modified.
 *
 * Return true if the 'fcode' is a valid filter program.
 * The constraints are that each jump be forward and to a valid
 * code.  The code must terminate with either an accept or reject.
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
static int
bpf_validate(const struct bpf_insn *f, int len)
{
	register int i;
	register const struct bpf_insn *p;

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
		 * Check that jumps are forward, and within
		 * the code block.
		 */
		if (BPF_CLASS(p->code) == BPF_JMP) {
			register u_int offset;

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

int
main(void)
{
#ifndef BPF_JIT_COMPILER
	u_int	i;
#endif
	u_int   ret;
	int     sig;
#ifdef BPF_VALIDATE
	int	valid;
#endif

	/* Try to catch all signals */
	for (sig = SIGHUP; sig <= SIGUSR2; sig++)
		signal(sig, sig_handler);

#ifdef BPF_VALIDATE
	valid = bpf_validate(pc, nins);
	if (valid != 0 && invalid != 0) {
		if (verbose > 1)
			printf("Validated invalid instruction(s):\t");
		if (verbose > 0)
			printf("FAILED\n");
		return (FAILED);
	} else if (valid == 0 && invalid == 0) {
		if (verbose > 1)
			printf("Invalidated valid instruction(s):\t");
		if (verbose > 0)
			printf("FAILED\n");
		return (FAILED);
	} else if (invalid != 0) {
		if (verbose > 1)
			printf("Expected and invalidated:\t");
		if (verbose > 0)
			printf("PASSED\n");
		return (PASSED);
	}
#endif

#ifdef BPF_JIT_COMPILER
	ret = bpf_compile_and_filter();
#else
	for (i = 0; i < BPF_NRUNS; i++)
		ret = bpf_filter(nins != 0 ? pc : NULL, pkt, wirelen, buflen);
#endif
	if (expect_signal != 0) {
		if (verbose > 1)
			printf("Expected signal %d but got none:\t",
			    expect_signal);
		if (verbose > 0)
			printf("FAILED\n");
		return (FAILED);
	}
	if (ret != expect) {
		if (verbose > 1)
			printf("Expected 0x%x but got 0x%x:\t", expect, ret);
		if (verbose > 0)
			printf("FAILED\n");
		return (FAILED);
	}
	if (verbose > 1)
		printf("Expected and got 0x%x:\t", ret);
	if (verbose > 0)
		printf("PASSED\n");

	return (PASSED);
}

static void
sig_handler(int sig)
{

	if (expect_signal == 0) {
		if (verbose > 1)
			printf("Received unexpected signal %d:\t", sig);
		if (verbose > 0)
			printf("FATAL\n");
		exit(FATAL);
	}
	if (expect_signal != sig) {
		if (verbose > 1)
			printf("Expected signal %d but got %d:\t",
			    expect_signal, sig);
		if (verbose > 0)
			printf("FAILED\n");
		exit(FAILED);
	}

	if (verbose > 1)
		printf("Expected and got signal %d:\t", sig);
	if (verbose > 0)
		printf("PASSED\n");

	exit(PASSED);
}
