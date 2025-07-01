// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Google LLC. */

#include <test_progs.h>
#include "test_snprintf.skel.h"
#include "test_snprintf_single.skel.h"

#define EXP_NUM_OUT  "-8 9 96 -424242 1337 DABBAD00"
#define EXP_NUM_RET  sizeof(EXP_NUM_OUT)

#define EXP_IP_OUT   "127.000.000.001 0000:0000:0000:0000:0000:0000:0000:0001"
#define EXP_IP_RET   sizeof(EXP_IP_OUT)

/* The third specifier, %pB, depends on compiler inlining so don't check it */
#define EXP_SYM_OUT  "schedule schedule+0x0/"
#define MIN_SYM_RET  sizeof(EXP_SYM_OUT)

/* The third specifier, %p, is a hashed pointer which changes on every reboot */
#define EXP_ADDR_OUT "0000000000000000 ffff00000add4e55 "
#define EXP_ADDR_RET sizeof(EXP_ADDR_OUT "unknownhashedptr")

#define EXP_STR_OUT  "str1         a  b c      d e longstr"
#define EXP_STR_RET  sizeof(EXP_STR_OUT)

#define EXP_OVER_OUT "%over"
#define EXP_OVER_RET 10

#define EXP_PAD_OUT "    4 000"
#define EXP_PAD_RET 900007

#define EXP_NO_ARG_OUT "simple case"
#define EXP_NO_ARG_RET 12

#define EXP_NO_BUF_RET 29

static void test_snprintf_positive(void)
{
	char exp_addr_out[] = EXP_ADDR_OUT;
	char exp_sym_out[]  = EXP_SYM_OUT;
	struct test_snprintf *skel;

	skel = test_snprintf__open_and_load();
	if (!ASSERT_OK_PTR(skel, "skel_open"))
		return;

	skel->bss->pid = getpid();

	if (!ASSERT_OK(test_snprintf__attach(skel), "skel_attach"))
		goto cleanup;

	/* trigger tracepoint */
	usleep(1);

	ASSERT_STREQ(skel->bss->num_out, EXP_NUM_OUT, "num_out");
	ASSERT_EQ(skel->bss->num_ret, EXP_NUM_RET, "num_ret");

	ASSERT_STREQ(skel->bss->ip_out, EXP_IP_OUT, "ip_out");
	ASSERT_EQ(skel->bss->ip_ret, EXP_IP_RET, "ip_ret");

	ASSERT_OK(memcmp(skel->bss->sym_out, exp_sym_out,
			 sizeof(exp_sym_out) - 1), "sym_out");
	ASSERT_LT(MIN_SYM_RET, skel->bss->sym_ret, "sym_ret");

	ASSERT_OK(memcmp(skel->bss->addr_out, exp_addr_out,
			 sizeof(exp_addr_out) - 1), "addr_out");
	ASSERT_EQ(skel->bss->addr_ret, EXP_ADDR_RET, "addr_ret");

	ASSERT_STREQ(skel->bss->str_out, EXP_STR_OUT, "str_out");
	ASSERT_EQ(skel->bss->str_ret, EXP_STR_RET, "str_ret");

	ASSERT_STREQ(skel->bss->over_out, EXP_OVER_OUT, "over_out");
	ASSERT_EQ(skel->bss->over_ret, EXP_OVER_RET, "over_ret");

	ASSERT_STREQ(skel->bss->pad_out, EXP_PAD_OUT, "pad_out");
	ASSERT_EQ(skel->bss->pad_ret, EXP_PAD_RET, "pad_ret");

	ASSERT_STREQ(skel->bss->noarg_out, EXP_NO_ARG_OUT, "no_arg_out");
	ASSERT_EQ(skel->bss->noarg_ret, EXP_NO_ARG_RET, "no_arg_ret");

	ASSERT_EQ(skel->bss->nobuf_ret, EXP_NO_BUF_RET, "no_buf_ret");

cleanup:
	test_snprintf__destroy(skel);
}

/* Loads an eBPF object calling bpf_snprintf with up to 10 characters of fmt */
static int load_single_snprintf(char *fmt)
{
	struct test_snprintf_single *skel;
	int ret;

	skel = test_snprintf_single__open();
	if (!skel)
		return -EINVAL;

	memcpy(skel->rodata->fmt, fmt, MIN(strlen(fmt) + 1, 10));

	ret = test_snprintf_single__load(skel);
	test_snprintf_single__destroy(skel);

	return ret;
}

static void test_snprintf_negative(void)
{
	ASSERT_OK(load_single_snprintf("valid %d"), "valid usage");

	ASSERT_ERR(load_single_snprintf("0123456789"), "no terminating zero");
	ASSERT_ERR(load_single_snprintf("%d %d"), "too many specifiers");
	ASSERT_ERR(load_single_snprintf("%pi5"), "invalid specifier 1");
	ASSERT_ERR(load_single_snprintf("%a"), "invalid specifier 2");
	ASSERT_ERR(load_single_snprintf("%"), "invalid specifier 3");
	ASSERT_ERR(load_single_snprintf("%12345678"), "invalid specifier 4");
	ASSERT_ERR(load_single_snprintf("%--------"), "invalid specifier 5");
	ASSERT_ERR(load_single_snprintf("%lc"), "invalid specifier 6");
	ASSERT_ERR(load_single_snprintf("%llc"), "invalid specifier 7");
	ASSERT_ERR(load_single_snprintf("\x80"), "non ascii character");
	ASSERT_ERR(load_single_snprintf("\x1"), "non printable character");
	ASSERT_ERR(load_single_snprintf("%p%"), "invalid specifier 8");
	ASSERT_ERR(load_single_snprintf("%s%"), "invalid specifier 9");
}

void test_snprintf(void)
{
	if (test__start_subtest("snprintf_positive"))
		test_snprintf_positive();
	if (test__start_subtest("snprintf_negative"))
		test_snprintf_negative();
}
