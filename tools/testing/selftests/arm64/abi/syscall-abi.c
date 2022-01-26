// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 ARM Limited.
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <asm/hwcap.h>
#include <asm/sigcontext.h>
#include <asm/unistd.h>

#include "../../kselftest.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define NUM_VL ((SVE_VQ_MAX - SVE_VQ_MIN) + 1)

extern void do_syscall(int sve_vl);

static void fill_random(void *buf, size_t size)
{
	int i;
	uint32_t *lbuf = buf;

	/* random() returns a 32 bit number regardless of the size of long */
	for (i = 0; i < size / sizeof(uint32_t); i++)
		lbuf[i] = random();
}

/*
 * We also repeat the test for several syscalls to try to expose different
 * behaviour.
 */
static struct syscall_cfg {
	int syscall_nr;
	const char *name;
} syscalls[] = {
	{ __NR_getpid,		"getpid()" },
	{ __NR_sched_yield,	"sched_yield()" },
};

#define NUM_GPR 31
uint64_t gpr_in[NUM_GPR];
uint64_t gpr_out[NUM_GPR];

static void setup_gpr(struct syscall_cfg *cfg, int sve_vl)
{
	fill_random(gpr_in, sizeof(gpr_in));
	gpr_in[8] = cfg->syscall_nr;
	memset(gpr_out, 0, sizeof(gpr_out));
}

static int check_gpr(struct syscall_cfg *cfg, int sve_vl)
{
	int errors = 0;
	int i;

	/*
	 * GPR x0-x7 may be clobbered, and all others should be preserved.
	 */
	for (i = 9; i < ARRAY_SIZE(gpr_in); i++) {
		if (gpr_in[i] != gpr_out[i]) {
			ksft_print_msg("%s SVE VL %d mismatch in GPR %d: %llx != %llx\n",
				       cfg->name, sve_vl, i,
				       gpr_in[i], gpr_out[i]);
			errors++;
		}
	}

	return errors;
}

#define NUM_FPR 32
uint64_t fpr_in[NUM_FPR * 2];
uint64_t fpr_out[NUM_FPR * 2];

static void setup_fpr(struct syscall_cfg *cfg, int sve_vl)
{
	fill_random(fpr_in, sizeof(fpr_in));
	memset(fpr_out, 0, sizeof(fpr_out));
}

static int check_fpr(struct syscall_cfg *cfg, int sve_vl)
{
	int errors = 0;
	int i;

	if (!sve_vl) {
		for (i = 0; i < ARRAY_SIZE(fpr_in); i++) {
			if (fpr_in[i] != fpr_out[i]) {
				ksft_print_msg("%s Q%d/%d mismatch %llx != %llx\n",
					       cfg->name,
					       i / 2, i % 2,
					       fpr_in[i], fpr_out[i]);
				errors++;
			}
		}
	}

	return errors;
}

static uint8_t z_zero[__SVE_ZREG_SIZE(SVE_VQ_MAX)];
uint8_t z_in[SVE_NUM_PREGS * __SVE_ZREG_SIZE(SVE_VQ_MAX)];
uint8_t z_out[SVE_NUM_PREGS * __SVE_ZREG_SIZE(SVE_VQ_MAX)];

static void setup_z(struct syscall_cfg *cfg, int sve_vl)
{
	fill_random(z_in, sizeof(z_in));
	fill_random(z_out, sizeof(z_out));
}

static int check_z(struct syscall_cfg *cfg, int sve_vl)
{
	size_t reg_size = sve_vl;
	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	/*
	 * After a syscall the low 128 bits of the Z registers should
	 * be preserved and the rest be zeroed or preserved.
	 */
	for (i = 0; i < SVE_NUM_ZREGS; i++) {
		void *in = &z_in[reg_size * i];
		void *out = &z_out[reg_size * i];

		if (memcmp(in, out, SVE_VQ_BYTES) != 0) {
			ksft_print_msg("%s SVE VL %d Z%d low 128 bits changed\n",
				       cfg->name, sve_vl, i);
			errors++;
		}
	}

	return errors;
}

uint8_t p_in[SVE_NUM_PREGS * __SVE_PREG_SIZE(SVE_VQ_MAX)];
uint8_t p_out[SVE_NUM_PREGS * __SVE_PREG_SIZE(SVE_VQ_MAX)];

static void setup_p(struct syscall_cfg *cfg, int sve_vl)
{
	fill_random(p_in, sizeof(p_in));
	fill_random(p_out, sizeof(p_out));
}

static int check_p(struct syscall_cfg *cfg, int sve_vl)
{
	size_t reg_size = sve_vq_from_vl(sve_vl) * 2; /* 1 bit per VL byte */

	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	/* After a syscall the P registers should be preserved or zeroed */
	for (i = 0; i < SVE_NUM_PREGS * reg_size; i++)
		if (p_out[i] && (p_in[i] != p_out[i]))
			errors++;
	if (errors)
		ksft_print_msg("%s SVE VL %d predicate registers non-zero\n",
			       cfg->name, sve_vl);

	return errors;
}

uint8_t ffr_in[__SVE_PREG_SIZE(SVE_VQ_MAX)];
uint8_t ffr_out[__SVE_PREG_SIZE(SVE_VQ_MAX)];

static void setup_ffr(struct syscall_cfg *cfg, int sve_vl)
{
	/*
	 * It is only valid to set a contiguous set of bits starting
	 * at 0.  For now since we're expecting this to be cleared by
	 * a syscall just set all bits.
	 */
	memset(ffr_in, 0xff, sizeof(ffr_in));
	fill_random(ffr_out, sizeof(ffr_out));
}

static int check_ffr(struct syscall_cfg *cfg, int sve_vl)
{
	size_t reg_size = sve_vq_from_vl(sve_vl) * 2;  /* 1 bit per VL byte */
	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	/* After a syscall the P registers should be preserved or zeroed */
	for (i = 0; i < reg_size; i++)
		if (ffr_out[i] && (ffr_in[i] != ffr_out[i]))
			errors++;
	if (errors)
		ksft_print_msg("%s SVE VL %d FFR non-zero\n",
			       cfg->name, sve_vl);

	return errors;
}

typedef void (*setup_fn)(struct syscall_cfg *cfg, int sve_vl);
typedef int (*check_fn)(struct syscall_cfg *cfg, int sve_vl);

/*
 * Each set of registers has a setup function which is called before
 * the syscall to fill values in a global variable for loading by the
 * test code and a check function which validates that the results are
 * as expected.  Vector lengths are passed everywhere, a vector length
 * of 0 should be treated as do not test.
 */
static struct {
	setup_fn setup;
	check_fn check;
} regset[] = {
	{ setup_gpr, check_gpr },
	{ setup_fpr, check_fpr },
	{ setup_z, check_z },
	{ setup_p, check_p },
	{ setup_ffr, check_ffr },
};

static bool do_test(struct syscall_cfg *cfg, int sve_vl)
{
	int errors = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(regset); i++)
		regset[i].setup(cfg, sve_vl);

	do_syscall(sve_vl);

	for (i = 0; i < ARRAY_SIZE(regset); i++)
		errors += regset[i].check(cfg, sve_vl);

	return errors == 0;
}

static void test_one_syscall(struct syscall_cfg *cfg)
{
	int sve_vq, sve_vl;

	/* FPSIMD only case */
	ksft_test_result(do_test(cfg, 0),
			 "%s FPSIMD\n", cfg->name);

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		return;

	for (sve_vq = SVE_VQ_MAX; sve_vq > 0; --sve_vq) {
		sve_vl = prctl(PR_SVE_SET_VL, sve_vq * 16);
		if (sve_vl == -1)
			ksft_exit_fail_msg("PR_SVE_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		sve_vl &= PR_SVE_VL_LEN_MASK;

		if (sve_vq != sve_vq_from_vl(sve_vl))
			sve_vq = sve_vq_from_vl(sve_vl);

		ksft_test_result(do_test(cfg, sve_vl),
				 "%s SVE VL %d\n", cfg->name, sve_vl);
	}
}

int sve_count_vls(void)
{
	unsigned int vq;
	int vl_count = 0;
	int vl;

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		return 0;

	/*
	 * Enumerate up to SVE_VQ_MAX vector lengths
	 */
	for (vq = SVE_VQ_MAX; vq > 0; --vq) {
		vl = prctl(PR_SVE_SET_VL, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("PR_SVE_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SVE_VL_LEN_MASK;

		if (vq != sve_vq_from_vl(vl))
			vq = sve_vq_from_vl(vl);

		vl_count++;
	}

	return vl_count;
}

int main(void)
{
	int i;

	srandom(getpid());

	ksft_print_header();
	ksft_set_plan(ARRAY_SIZE(syscalls) * (sve_count_vls() + 1));

	for (i = 0; i < ARRAY_SIZE(syscalls); i++)
		test_one_syscall(&syscalls[i]);

	ksft_print_cnts();

	return 0;
}
