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

#include "syscall-abi.h"

/*
 * The kernel defines a much larger SVE_VQ_MAX than is expressable in
 * the architecture, this creates a *lot* of overhead filling the
 * buffers (especially ZA) on emulated platforms so use the actual
 * architectural maximum instead.
 */
#define ARCH_SVE_VQ_MAX 16

static int default_sme_vl;

static int sve_vl_count;
static unsigned int sve_vls[ARCH_SVE_VQ_MAX];
static int sme_vl_count;
static unsigned int sme_vls[ARCH_SVE_VQ_MAX];

extern void do_syscall(int sve_vl, int sme_vl);

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

static void setup_gpr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		      uint64_t svcr)
{
	fill_random(gpr_in, sizeof(gpr_in));
	gpr_in[8] = cfg->syscall_nr;
	memset(gpr_out, 0, sizeof(gpr_out));
}

static int check_gpr(struct syscall_cfg *cfg, int sve_vl, int sme_vl, uint64_t svcr)
{
	int errors = 0;
	int i;

	/*
	 * GPR x0-x7 may be clobbered, and all others should be preserved.
	 */
	for (i = 9; i < ARRAY_SIZE(gpr_in); i++) {
		if (gpr_in[i] != gpr_out[i]) {
			ksft_print_msg("%s SVE VL %d mismatch in GPR %d: %lx != %lx\n",
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
uint64_t fpr_zero[NUM_FPR * 2];

static void setup_fpr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		      uint64_t svcr)
{
	fill_random(fpr_in, sizeof(fpr_in));
	memset(fpr_out, 0, sizeof(fpr_out));
}

static int check_fpr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		     uint64_t svcr)
{
	int errors = 0;
	int i;

	if (!sve_vl && !(svcr & SVCR_SM_MASK)) {
		for (i = 0; i < ARRAY_SIZE(fpr_in); i++) {
			if (fpr_in[i] != fpr_out[i]) {
				ksft_print_msg("%s Q%d/%d mismatch %lx != %lx\n",
					       cfg->name,
					       i / 2, i % 2,
					       fpr_in[i], fpr_out[i]);
				errors++;
			}
		}
	}

	/*
	 * In streaming mode the whole register set should be cleared
	 * by the transition out of streaming mode.
	 */
	if (svcr & SVCR_SM_MASK) {
		if (memcmp(fpr_zero, fpr_out, sizeof(fpr_out)) != 0) {
			ksft_print_msg("%s FPSIMD registers non-zero exiting SM\n",
				       cfg->name);
			errors++;
		}
	}

	return errors;
}

#define SVE_Z_SHARED_BYTES (128 / 8)

static uint8_t z_zero[__SVE_ZREG_SIZE(ARCH_SVE_VQ_MAX)];
uint8_t z_in[SVE_NUM_ZREGS * __SVE_ZREG_SIZE(ARCH_SVE_VQ_MAX)];
uint8_t z_out[SVE_NUM_ZREGS * __SVE_ZREG_SIZE(ARCH_SVE_VQ_MAX)];

static void setup_z(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	fill_random(z_in, sizeof(z_in));
	fill_random(z_out, sizeof(z_out));
}

static int check_z(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		   uint64_t svcr)
{
	size_t reg_size = sve_vl;
	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	for (i = 0; i < SVE_NUM_ZREGS; i++) {
		uint8_t *in = &z_in[reg_size * i];
		uint8_t *out = &z_out[reg_size * i];

		if (svcr & SVCR_SM_MASK) {
			/*
			 * In streaming mode the whole register should
			 * be cleared by the transition out of
			 * streaming mode.
			 */
			if (memcmp(z_zero, out, reg_size) != 0) {
				ksft_print_msg("%s SVE VL %d Z%d non-zero\n",
					       cfg->name, sve_vl, i);
				errors++;
			}
		} else {
			/*
			 * For standard SVE the low 128 bits should be
			 * preserved and any additional bits cleared.
			 */
			if (memcmp(in, out, SVE_Z_SHARED_BYTES) != 0) {
				ksft_print_msg("%s SVE VL %d Z%d low 128 bits changed\n",
					       cfg->name, sve_vl, i);
				errors++;
			}

			if (reg_size > SVE_Z_SHARED_BYTES &&
			    (memcmp(z_zero, out + SVE_Z_SHARED_BYTES,
				    reg_size - SVE_Z_SHARED_BYTES) != 0)) {
				ksft_print_msg("%s SVE VL %d Z%d high bits non-zero\n",
					       cfg->name, sve_vl, i);
				errors++;
			}
		}
	}

	return errors;
}

uint8_t p_in[SVE_NUM_PREGS * __SVE_PREG_SIZE(ARCH_SVE_VQ_MAX)];
uint8_t p_out[SVE_NUM_PREGS * __SVE_PREG_SIZE(ARCH_SVE_VQ_MAX)];

static void setup_p(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	fill_random(p_in, sizeof(p_in));
	fill_random(p_out, sizeof(p_out));
}

static int check_p(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		   uint64_t svcr)
{
	size_t reg_size = sve_vq_from_vl(sve_vl) * 2; /* 1 bit per VL byte */

	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	/* After a syscall the P registers should be zeroed */
	for (i = 0; i < SVE_NUM_PREGS * reg_size; i++)
		if (p_out[i])
			errors++;
	if (errors)
		ksft_print_msg("%s SVE VL %d predicate registers non-zero\n",
			       cfg->name, sve_vl);

	return errors;
}

uint8_t ffr_in[__SVE_PREG_SIZE(ARCH_SVE_VQ_MAX)];
uint8_t ffr_out[__SVE_PREG_SIZE(ARCH_SVE_VQ_MAX)];

static void setup_ffr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		      uint64_t svcr)
{
	/*
	 * If we are in streaming mode and do not have FA64 then FFR
	 * is unavailable.
	 */
	if ((svcr & SVCR_SM_MASK) &&
	    !(getauxval(AT_HWCAP2) & HWCAP2_SME_FA64)) {
		memset(&ffr_in, 0, sizeof(ffr_in));
		return;
	}

	/*
	 * It is only valid to set a contiguous set of bits starting
	 * at 0.  For now since we're expecting this to be cleared by
	 * a syscall just set all bits.
	 */
	memset(ffr_in, 0xff, sizeof(ffr_in));
	fill_random(ffr_out, sizeof(ffr_out));
}

static int check_ffr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		     uint64_t svcr)
{
	size_t reg_size = sve_vq_from_vl(sve_vl) * 2;  /* 1 bit per VL byte */
	int errors = 0;
	int i;

	if (!sve_vl)
		return 0;

	if ((svcr & SVCR_SM_MASK) &&
	    !(getauxval(AT_HWCAP2) & HWCAP2_SME_FA64))
		return 0;

	/* After a syscall FFR should be zeroed */
	for (i = 0; i < reg_size; i++)
		if (ffr_out[i])
			errors++;
	if (errors)
		ksft_print_msg("%s SVE VL %d FFR non-zero\n",
			       cfg->name, sve_vl);

	return errors;
}

uint64_t svcr_in, svcr_out;

static void setup_svcr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	svcr_in = svcr;
}

static int check_svcr(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		      uint64_t svcr)
{
	int errors = 0;

	if (svcr_out & SVCR_SM_MASK) {
		ksft_print_msg("%s Still in SM, SVCR %lx\n",
			       cfg->name, svcr_out);
		errors++;
	}

	if ((svcr_in & SVCR_ZA_MASK) != (svcr_out & SVCR_ZA_MASK)) {
		ksft_print_msg("%s PSTATE.ZA changed, SVCR %lx != %lx\n",
			       cfg->name, svcr_in, svcr_out);
		errors++;
	}

	return errors;
}

uint8_t za_in[ZA_SIG_REGS_SIZE(ARCH_SVE_VQ_MAX)];
uint8_t za_out[ZA_SIG_REGS_SIZE(ARCH_SVE_VQ_MAX)];

static void setup_za(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		     uint64_t svcr)
{
	fill_random(za_in, sizeof(za_in));
	memset(za_out, 0, sizeof(za_out));
}

static int check_za(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	size_t reg_size = sme_vl * sme_vl;
	int errors = 0;

	if (!(svcr & SVCR_ZA_MASK))
		return 0;

	if (memcmp(za_in, za_out, reg_size) != 0) {
		ksft_print_msg("SME VL %d ZA does not match\n", sme_vl);
		errors++;
	}

	return errors;
}

uint8_t zt_in[ZT_SIG_REG_BYTES] __attribute__((aligned(16)));
uint8_t zt_out[ZT_SIG_REG_BYTES] __attribute__((aligned(16)));

static void setup_zt(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		     uint64_t svcr)
{
	fill_random(zt_in, sizeof(zt_in));
	memset(zt_out, 0, sizeof(zt_out));
}

static int check_zt(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	int errors = 0;

	if (!(getauxval(AT_HWCAP2) & HWCAP2_SME2))
		return 0;

	if (!(svcr & SVCR_ZA_MASK))
		return 0;

	if (memcmp(zt_in, zt_out, sizeof(zt_in)) != 0) {
		ksft_print_msg("SME VL %d ZT does not match\n", sme_vl);
		errors++;
	}

	return errors;
}

typedef void (*setup_fn)(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
			 uint64_t svcr);
typedef int (*check_fn)(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
			uint64_t svcr);

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
	{ setup_svcr, check_svcr },
	{ setup_za, check_za },
	{ setup_zt, check_zt },
};

static bool do_test(struct syscall_cfg *cfg, int sve_vl, int sme_vl,
		    uint64_t svcr)
{
	int errors = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(regset); i++)
		regset[i].setup(cfg, sve_vl, sme_vl, svcr);

	do_syscall(sve_vl, sme_vl);

	for (i = 0; i < ARRAY_SIZE(regset); i++)
		errors += regset[i].check(cfg, sve_vl, sme_vl, svcr);

	return errors == 0;
}

static void test_one_syscall(struct syscall_cfg *cfg)
{
	int sve, sme;
	int ret;

	/* FPSIMD only case */
	ksft_test_result(do_test(cfg, 0, default_sme_vl, 0),
			 "%s FPSIMD\n", cfg->name);

	for (sve = 0; sve < sve_vl_count; sve++) {
		ret = prctl(PR_SVE_SET_VL, sve_vls[sve]);
		if (ret == -1)
			ksft_exit_fail_msg("PR_SVE_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		ksft_test_result(do_test(cfg, sve_vls[sve], default_sme_vl, 0),
				 "%s SVE VL %d\n", cfg->name, sve_vls[sve]);

		for (sme = 0; sme < sme_vl_count; sme++) {
			ret = prctl(PR_SME_SET_VL, sme_vls[sme]);
			if (ret == -1)
				ksft_exit_fail_msg("PR_SME_SET_VL failed: %s (%d)\n",
						   strerror(errno), errno);

			ksft_test_result(do_test(cfg, sve_vls[sve],
						 sme_vls[sme],
						 SVCR_ZA_MASK | SVCR_SM_MASK),
					 "%s SVE VL %d/SME VL %d SM+ZA\n",
					 cfg->name, sve_vls[sve],
					 sme_vls[sme]);
			ksft_test_result(do_test(cfg, sve_vls[sve],
						 sme_vls[sme], SVCR_SM_MASK),
					 "%s SVE VL %d/SME VL %d SM\n",
					 cfg->name, sve_vls[sve],
					 sme_vls[sme]);
			ksft_test_result(do_test(cfg, sve_vls[sve],
						 sme_vls[sme], SVCR_ZA_MASK),
					 "%s SVE VL %d/SME VL %d ZA\n",
					 cfg->name, sve_vls[sve],
					 sme_vls[sme]);
		}
	}

	for (sme = 0; sme < sme_vl_count; sme++) {
		ret = prctl(PR_SME_SET_VL, sme_vls[sme]);
		if (ret == -1)
			ksft_exit_fail_msg("PR_SME_SET_VL failed: %s (%d)\n",
						   strerror(errno), errno);

		ksft_test_result(do_test(cfg, 0, sme_vls[sme],
					 SVCR_ZA_MASK | SVCR_SM_MASK),
				 "%s SME VL %d SM+ZA\n",
				 cfg->name, sme_vls[sme]);
		ksft_test_result(do_test(cfg, 0, sme_vls[sme], SVCR_SM_MASK),
				 "%s SME VL %d SM\n",
				 cfg->name, sme_vls[sme]);
		ksft_test_result(do_test(cfg, 0, sme_vls[sme], SVCR_ZA_MASK),
				 "%s SME VL %d ZA\n",
				 cfg->name, sme_vls[sme]);
	}
}

void sve_count_vls(void)
{
	unsigned int vq;
	int vl;

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		return;

	/*
	 * Enumerate up to ARCH_SVE_VQ_MAX vector lengths
	 */
	for (vq = ARCH_SVE_VQ_MAX; vq > 0; vq /= 2) {
		vl = prctl(PR_SVE_SET_VL, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("PR_SVE_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SVE_VL_LEN_MASK;

		if (vq != sve_vq_from_vl(vl))
			vq = sve_vq_from_vl(vl);

		sve_vls[sve_vl_count++] = vl;
	}
}

void sme_count_vls(void)
{
	unsigned int vq;
	int vl;

	if (!(getauxval(AT_HWCAP2) & HWCAP2_SME))
		return;

	/*
	 * Enumerate up to ARCH_SVE_VQ_MAX vector lengths
	 */
	for (vq = ARCH_SVE_VQ_MAX; vq > 0; vq /= 2) {
		vl = prctl(PR_SME_SET_VL, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("PR_SME_SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SME_VL_LEN_MASK;

		/* Found lowest VL */
		if (sve_vq_from_vl(vl) > vq)
			break;

		if (vq != sve_vq_from_vl(vl))
			vq = sve_vq_from_vl(vl);

		sme_vls[sme_vl_count++] = vl;
	}

	/* Ensure we configure a SME VL, used to flag if SVCR is set */
	default_sme_vl = sme_vls[0];
}

int main(void)
{
	int i;
	int tests = 1;  /* FPSIMD */
	int sme_ver;

	srandom(getpid());

	ksft_print_header();

	sve_count_vls();
	sme_count_vls();

	tests += sve_vl_count;
	tests += sme_vl_count * 3;
	tests += (sve_vl_count * sme_vl_count) * 3;
	ksft_set_plan(ARRAY_SIZE(syscalls) * tests);

	if (getauxval(AT_HWCAP2) & HWCAP2_SME2)
		sme_ver = 2;
	else
		sme_ver = 1;

	if (getauxval(AT_HWCAP2) & HWCAP2_SME_FA64)
		ksft_print_msg("SME%d with FA64\n", sme_ver);
	else if (getauxval(AT_HWCAP2) & HWCAP2_SME)
		ksft_print_msg("SME%d without FA64\n", sme_ver);

	for (i = 0; i < ARRAY_SIZE(syscalls); i++)
		test_one_syscall(&syscalls[i]);

	ksft_print_cnts();

	return 0;
}
