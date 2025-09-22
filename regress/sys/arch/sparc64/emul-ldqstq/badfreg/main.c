/*	$OpenBSD: main.c,v 1.1 2003/07/12 04:08:33 jason Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <unistd.h>

struct fpquad {
	u_int64_t x1;
	u_int64_t x2;
	u_int64_t x3;
	u_int64_t x4;
};

void test_ldq_f2_g0(struct fpquad *);
void test_ldq_f2_simm13(struct fpquad *);
void test_stq_f2_g0(struct fpquad *);
void test_stq_f2_simm13(struct fpquad *);
void test_ldq_f6_g0(struct fpquad *);
void test_ldq_f6_simm13(struct fpquad *);
void test_stq_f6_g0(struct fpquad *);
void test_stq_f6_simm13(struct fpquad *);
void test_ldq_f10_g0(struct fpquad *);
void test_ldq_f10_simm13(struct fpquad *);
void test_stq_f10_g0(struct fpquad *);
void test_stq_f10_simm13(struct fpquad *);
void test_ldq_f14_g0(struct fpquad *);
void test_ldq_f14_simm13(struct fpquad *);
void test_stq_f14_g0(struct fpquad *);
void test_stq_f14_simm13(struct fpquad *);
void test_ldq_f18_g0(struct fpquad *);
void test_ldq_f18_simm13(struct fpquad *);
void test_stq_f18_g0(struct fpquad *);
void test_stq_f18_simm13(struct fpquad *);
void test_ldq_f22_g0(struct fpquad *);
void test_ldq_f22_simm13(struct fpquad *);
void test_stq_f22_g0(struct fpquad *);
void test_stq_f22_simm13(struct fpquad *);
void test_ldq_f26_g0(struct fpquad *);
void test_ldq_f26_simm13(struct fpquad *);
void test_stq_f26_g0(struct fpquad *);
void test_stq_f26_simm13(struct fpquad *);
void test_ldq_f30_g0(struct fpquad *);
void test_ldq_f30_simm13(struct fpquad *);
void test_stq_f30_g0(struct fpquad *);
void test_stq_f30_simm13(struct fpquad *);
void test_ldq_f34_g0(struct fpquad *);
void test_ldq_f34_simm13(struct fpquad *);
void test_stq_f34_g0(struct fpquad *);
void test_stq_f34_simm13(struct fpquad *);
void test_ldq_f38_g0(struct fpquad *);
void test_ldq_f38_simm13(struct fpquad *);
void test_stq_f38_g0(struct fpquad *);
void test_stq_f38_simm13(struct fpquad *);
void test_ldq_f42_g0(struct fpquad *);
void test_ldq_f42_simm13(struct fpquad *);
void test_stq_f42_g0(struct fpquad *);
void test_stq_f42_simm13(struct fpquad *);
void test_ldq_f46_g0(struct fpquad *);
void test_ldq_f46_simm13(struct fpquad *);
void test_stq_f46_g0(struct fpquad *);
void test_stq_f46_simm13(struct fpquad *);
void test_ldq_f50_g0(struct fpquad *);
void test_ldq_f50_simm13(struct fpquad *);
void test_stq_f50_g0(struct fpquad *);
void test_stq_f50_simm13(struct fpquad *);
void test_ldq_f54_g0(struct fpquad *);
void test_ldq_f54_simm13(struct fpquad *);
void test_stq_f54_g0(struct fpquad *);
void test_stq_f54_simm13(struct fpquad *);
void test_ldq_f58_g0(struct fpquad *);
void test_ldq_f58_simm13(struct fpquad *);
void test_stq_f58_g0(struct fpquad *);
void test_stq_f58_simm13(struct fpquad *);
void test_ldq_f62_g0(struct fpquad *);
void test_ldq_f62_simm13(struct fpquad *);
void test_stq_f62_g0(struct fpquad *);
void test_stq_f62_simm13(struct fpquad *);

struct fptest {
	char *reg;
	char *ldst;
	char *dir;
	void (*func)(struct fpquad *);
} thetests[] = {
	{"2", "st", "reg", test_stq_f2_g0},
	{"2", "ld", "reg", test_ldq_f2_g0},
	{"2", "st", "imm", test_stq_f2_simm13},
	{"2", "ld", "imm", test_ldq_f2_simm13},
	{"6", "st", "reg", test_stq_f6_g0},
	{"6", "ld", "reg", test_ldq_f6_g0},
	{"6", "st", "imm", test_stq_f6_simm13},
	{"6", "ld", "imm", test_ldq_f6_simm13},
	{"10", "st", "reg", test_stq_f10_g0},
	{"10", "ld", "reg", test_ldq_f10_g0},
	{"10", "st", "imm", test_stq_f10_simm13},
	{"10", "ld", "imm", test_ldq_f10_simm13},
	{"14", "st", "reg", test_stq_f14_g0},
	{"14", "ld", "reg", test_ldq_f14_g0},
	{"14", "st", "imm", test_stq_f14_simm13},
	{"14", "ld", "imm", test_ldq_f14_simm13},
	{"18", "st", "reg", test_stq_f18_g0},
	{"18", "ld", "reg", test_ldq_f18_g0},
	{"18", "st", "imm", test_stq_f18_simm13},
	{"18", "ld", "imm", test_ldq_f18_simm13},
	{"22", "st", "reg", test_stq_f22_g0},
	{"22", "ld", "reg", test_ldq_f22_g0},
	{"22", "st", "imm", test_stq_f22_simm13},
	{"22", "ld", "imm", test_ldq_f22_simm13},
	{"26", "st", "reg", test_stq_f26_g0},
	{"26", "ld", "reg", test_ldq_f26_g0},
	{"26", "st", "imm", test_stq_f26_simm13},
	{"26", "ld", "imm", test_ldq_f26_simm13},
	{"30", "st", "reg", test_stq_f30_g0},
	{"30", "ld", "reg", test_ldq_f30_g0},
	{"30", "st", "imm", test_stq_f30_simm13},
	{"30", "ld", "imm", test_ldq_f30_simm13},
	{"34", "st", "reg", test_stq_f34_g0},
	{"34", "ld", "reg", test_ldq_f34_g0},
	{"34", "st", "imm", test_stq_f34_simm13},
	{"34", "ld", "imm", test_ldq_f34_simm13},
	{"38", "st", "reg", test_stq_f38_g0},
	{"38", "ld", "reg", test_ldq_f38_g0},
	{"38", "st", "imm", test_stq_f38_simm13},
	{"38", "ld", "imm", test_ldq_f38_simm13},
	{"42", "st", "reg", test_stq_f42_g0},
	{"42", "ld", "reg", test_ldq_f42_g0},
	{"42", "st", "imm", test_stq_f42_simm13},
	{"42", "ld", "imm", test_ldq_f42_simm13},
	{"46", "st", "reg", test_stq_f46_g0},
	{"46", "ld", "reg", test_ldq_f46_g0},
	{"46", "st", "imm", test_stq_f46_simm13},
	{"46", "ld", "imm", test_ldq_f46_simm13},
	{"50", "st", "reg", test_stq_f50_g0},
	{"50", "ld", "reg", test_ldq_f50_g0},
	{"50", "st", "imm", test_stq_f50_simm13},
	{"50", "ld", "imm", test_ldq_f50_simm13},
	{"54", "st", "reg", test_stq_f54_g0},
	{"54", "ld", "reg", test_ldq_f54_g0},
	{"54", "st", "imm", test_stq_f54_simm13},
	{"54", "ld", "imm", test_ldq_f54_simm13},
	{"58", "st", "reg", test_stq_f58_g0},
	{"58", "ld", "reg", test_ldq_f58_g0},
	{"58", "st", "imm", test_stq_f58_simm13},
	{"58", "ld", "imm", test_ldq_f58_simm13},
	{"62", "st", "reg", test_stq_f62_g0},
	{"62", "ld", "reg", test_ldq_f62_g0},
	{"62", "st", "imm", test_stq_f62_simm13},
	{"62", "ld", "imm", test_ldq_f62_simm13},
};
#define	NTESTS	(sizeof(thetests)/sizeof(thetests[0]))

void ill_catcher(int, siginfo_t *, void *);

int
main(int argc, char *argv[])
{
	struct fptest *fpt;
	struct fpquad fpq;
	struct sigaction sa;
	int i;

	if (argc != 4) {
		fprintf(stderr, "badreg regnum [ld|st] [reg|imm]\n");
		return (1);
	}

	for (i = 0; i < NTESTS; i++) {
		fpt = thetests + i;
		if (strcmp(fpt->reg, argv[1]) == 0 &&
		    strcmp(fpt->ldst, argv[2]) == 0 &&
		    strcmp(fpt->dir, argv[3]) == 0)
			break;
	}
	if (i == NTESTS)
		errx(1, "unknown test: %s %s %s", argv[1], argv[2], argv[3]);

	sa.sa_sigaction = ill_catcher;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGILL, &sa, NULL) == -1)
		err(1, "sigaction");

	(*fpt->func)(&fpq);
	err(1, "%s %s %s did not generate sigill", argv[1], argv[2], argv[3]);
	return (0);
}

void
ill_catcher(int sig, siginfo_t *si, void *v)
{
	_exit(0);
}
