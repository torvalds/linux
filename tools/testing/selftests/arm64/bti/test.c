// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019,2021  Arm Limited
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */

#include "system.h"

#include <linux/errno.h>
#include <linux/auxvec.h>
#include <linux/signal.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>

typedef struct ucontext ucontext_t;

#include "btitest.h"
#include "compiler.h"
#include "signal.h"

#define EXPECTED_TESTS 18

static volatile unsigned int test_num = 1;
static unsigned int test_passed;
static unsigned int test_failed;
static unsigned int test_skipped;

static void fdputs(int fd, const char *str)
{
	size_t len = 0;
	const char *p = str;

	while (*p++)
		++len;

	write(fd, str, len);
}

static void putstr(const char *str)
{
	fdputs(1, str);
}

static void putnum(unsigned int num)
{
	char c;

	if (num / 10)
		putnum(num / 10);

	c = '0' + (num % 10);
	write(1, &c, 1);
}

#define puttestname(test_name, trampoline_name) do {	\
	putstr(test_name);				\
	putstr("/");					\
	putstr(trampoline_name);			\
} while (0)

void print_summary(void)
{
	putstr("# Totals: pass:");
	putnum(test_passed);
	putstr(" fail:");
	putnum(test_failed);
	putstr(" xfail:0 xpass:0 skip:");
	putnum(test_skipped);
	putstr(" error:0\n");
}

static const char *volatile current_test_name;
static const char *volatile current_trampoline_name;
static volatile int sigill_expected, sigill_received;

static void handler(int n, siginfo_t *si __always_unused,
		    void *uc_ __always_unused)
{
	ucontext_t *uc = uc_;

	putstr("# \t[SIGILL in ");
	puttestname(current_test_name, current_trampoline_name);
	putstr(", BTYPE=");
	write(1, &"00011011"[((uc->uc_mcontext.pstate & PSR_BTYPE_MASK)
			      >> PSR_BTYPE_SHIFT) * 2], 2);
	if (!sigill_expected) {
		putstr("]\n");
		putstr("not ok ");
		putnum(test_num);
		putstr(" ");
		puttestname(current_test_name, current_trampoline_name);
		putstr("(unexpected SIGILL)\n");
		print_summary();
		exit(128 + n);
	}

	putstr(" (expected)]\n");
	sigill_received = 1;
	/* zap BTYPE so that resuming the faulting code will work */
	uc->uc_mcontext.pstate &= ~PSR_BTYPE_MASK;
}

static int skip_all;

static void __do_test(void (*trampoline)(void (*)(void)),
		      void (*fn)(void),
		      const char *trampoline_name,
		      const char *name,
		      int expect_sigill)
{
	if (skip_all) {
		test_skipped++;
		putstr("ok ");
		putnum(test_num);
		putstr(" ");
		puttestname(name, trampoline_name);
		putstr(" # SKIP\n");

		return;
	}

	/* Branch Target exceptions should only happen in BTI binaries: */
	if (!BTI)
		expect_sigill = 0;

	sigill_expected = expect_sigill;
	sigill_received = 0;
	current_test_name = name;
	current_trampoline_name = trampoline_name;

	trampoline(fn);

	if (expect_sigill && !sigill_received) {
		putstr("not ok ");
		test_failed++;
	} else {
		putstr("ok ");
		test_passed++;
	}
	putnum(test_num++);
	putstr(" ");
	puttestname(name, trampoline_name);
	putstr("\n");
}

#define do_test(expect_sigill_br_x0,					\
		expect_sigill_br_x16,					\
		expect_sigill_blr,					\
		name)							\
do {									\
	__do_test(call_using_br_x0, name, "call_using_br_x0", #name,	\
		  expect_sigill_br_x0);					\
	__do_test(call_using_br_x16, name, "call_using_br_x16", #name,	\
		  expect_sigill_br_x16);				\
	__do_test(call_using_blr, name, "call_using_blr", #name,	\
		  expect_sigill_blr);					\
} while (0)

void start(int *argcp)
{
	struct sigaction sa;
	void *const *p;
	const struct auxv_entry {
		unsigned long type;
		unsigned long val;
	} *auxv;
	unsigned long hwcap = 0, hwcap2 = 0;

	putstr("TAP version 13\n");
	putstr("1..");
	putnum(EXPECTED_TESTS);
	putstr("\n");

	/* Gross hack for finding AT_HWCAP2 from the initial process stack: */
	p = (void *const *)argcp + 1 + *argcp + 1; /* start of environment */
	/* step over environment */
	while (*p++)
		;
	for (auxv = (const struct auxv_entry *)p; auxv->type != AT_NULL; ++auxv) {
		switch (auxv->type) {
		case AT_HWCAP:
			hwcap = auxv->val;
			break;
		case AT_HWCAP2:
			hwcap2 = auxv->val;
			break;
		default:
			break;
		}
	}

	if (hwcap & HWCAP_PACA)
		putstr("# HWCAP_PACA present\n");
	else
		putstr("# HWCAP_PACA not present\n");

	if (hwcap2 & HWCAP2_BTI) {
		putstr("# HWCAP2_BTI present\n");
		if (!(hwcap & HWCAP_PACA))
			putstr("# Bad hardware?  Expect problems.\n");
	} else {
		putstr("# HWCAP2_BTI not present\n");
		skip_all = 1;
	}

	putstr("# Test binary");
	if (!BTI)
		putstr(" not");
	putstr(" built for BTI\n");

	sa.sa_handler = (sighandler_t)(void *)handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGILL, &sa, NULL);
	sigaddset(&sa.sa_mask, SIGILL);
	sigprocmask(SIG_UNBLOCK, &sa.sa_mask, NULL);

	do_test(1, 1, 1, nohint_func);
	do_test(1, 1, 1, bti_none_func);
	do_test(1, 0, 0, bti_c_func);
	do_test(0, 0, 1, bti_j_func);
	do_test(0, 0, 0, bti_jc_func);
	do_test(1, 0, 0, paciasp_func);

	print_summary();

	if (test_num - 1 != EXPECTED_TESTS)
		putstr("# WARNING - EXPECTED TEST COUNT WRONG\n");

	if (test_failed)
		exit(1);
	else
		exit(0);
}
