// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 ARM Limited.
 * Original author: Mark Brown <broonie@kernel.org>
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <linux/kernel.h>

#include <asm/sigcontext.h>
#include <asm/sve_context.h>
#include <asm/ptrace.h>

#include "../../kselftest.h"

#include "fp-ptrace.h"

#include <linux/bits.h>

#define FPMR_LSCALE2_MASK                               GENMASK(37, 32)
#define FPMR_NSCALE_MASK                                GENMASK(31, 24)
#define FPMR_LSCALE_MASK                                GENMASK(22, 16)
#define FPMR_OSC_MASK                                   GENMASK(15, 15)
#define FPMR_OSM_MASK                                   GENMASK(14, 14)

/* <linux/elf.h> and <sys/auxv.h> don't like each other, so: */
#ifndef NT_ARM_SVE
#define NT_ARM_SVE 0x405
#endif

#ifndef NT_ARM_SSVE
#define NT_ARM_SSVE 0x40b
#endif

#ifndef NT_ARM_ZA
#define NT_ARM_ZA 0x40c
#endif

#ifndef NT_ARM_ZT
#define NT_ARM_ZT 0x40d
#endif

#ifndef NT_ARM_FPMR
#define NT_ARM_FPMR 0x40e
#endif

#define ARCH_VQ_MAX 256

/* VL 128..2048 in powers of 2 */
#define MAX_NUM_VLS 5

/*
 * FPMR bits we can set without doing feature checks to see if values
 * are valid.
 */
#define FPMR_SAFE_BITS (FPMR_LSCALE2_MASK | FPMR_NSCALE_MASK | \
			FPMR_LSCALE_MASK | FPMR_OSC_MASK | FPMR_OSM_MASK)

#define NUM_FPR 32
__uint128_t v_in[NUM_FPR];
__uint128_t v_expected[NUM_FPR];
__uint128_t v_out[NUM_FPR];

char z_in[__SVE_ZREGS_SIZE(ARCH_VQ_MAX)];
char z_expected[__SVE_ZREGS_SIZE(ARCH_VQ_MAX)];
char z_out[__SVE_ZREGS_SIZE(ARCH_VQ_MAX)];

char p_in[__SVE_PREGS_SIZE(ARCH_VQ_MAX)];
char p_expected[__SVE_PREGS_SIZE(ARCH_VQ_MAX)];
char p_out[__SVE_PREGS_SIZE(ARCH_VQ_MAX)];

char ffr_in[__SVE_PREG_SIZE(ARCH_VQ_MAX)];
char ffr_expected[__SVE_PREG_SIZE(ARCH_VQ_MAX)];
char ffr_out[__SVE_PREG_SIZE(ARCH_VQ_MAX)];

char za_in[ZA_SIG_REGS_SIZE(ARCH_VQ_MAX)];
char za_expected[ZA_SIG_REGS_SIZE(ARCH_VQ_MAX)];
char za_out[ZA_SIG_REGS_SIZE(ARCH_VQ_MAX)];

char zt_in[ZT_SIG_REG_BYTES];
char zt_expected[ZT_SIG_REG_BYTES];
char zt_out[ZT_SIG_REG_BYTES];

uint64_t fpmr_in, fpmr_expected, fpmr_out;

uint64_t sve_vl_out;
uint64_t sme_vl_out;
uint64_t svcr_in, svcr_expected, svcr_out;

void load_and_save(int flags);

static bool got_alarm;

static void handle_alarm(int sig, siginfo_t *info, void *context)
{
	got_alarm = true;
}

#ifdef CONFIG_CPU_BIG_ENDIAN
static __uint128_t arm64_cpu_to_le128(__uint128_t x)
{
	u64 a = swab64(x);
	u64 b = swab64(x >> 64);

	return ((__uint128_t)a << 64) | b;
}
#else
static __uint128_t arm64_cpu_to_le128(__uint128_t x)
{
	return x;
}
#endif

#define arm64_le128_to_cpu(x) arm64_cpu_to_le128(x)

static bool sve_supported(void)
{
	return getauxval(AT_HWCAP) & HWCAP_SVE;
}

static bool sme_supported(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_SME;
}

static bool sme2_supported(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_SME2;
}

static bool fa64_supported(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_SME_FA64;
}

static bool fpmr_supported(void)
{
	return getauxval(AT_HWCAP2) & HWCAP2_FPMR;
}

static bool compare_buffer(const char *name, void *out,
			   void *expected, size_t size)
{
	void *tmp;

	if (memcmp(out, expected, size) == 0)
		return true;

	ksft_print_msg("Mismatch in %s\n", name);

	/* Did we just get zeros back? */
	tmp = malloc(size);
	if (!tmp) {
		ksft_print_msg("OOM allocating %lu bytes for %s\n",
			       size, name);
		ksft_exit_fail();
	}
	memset(tmp, 0, size);

	if (memcmp(out, tmp, size) == 0)
		ksft_print_msg("%s is zero\n", name);

	free(tmp);

	return false;
}

struct test_config {
	int sve_vl_in;
	int sve_vl_expected;
	int sme_vl_in;
	int sme_vl_expected;
	int svcr_in;
	int svcr_expected;
};

struct test_definition {
	const char *name;
	bool sve_vl_change;
	bool (*supported)(struct test_config *config);
	void (*set_expected_values)(struct test_config *config);
	void (*modify_values)(pid_t child, struct test_config *test_config);
};

static int vl_in(struct test_config *config)
{
	int vl;

	if (config->svcr_in & SVCR_SM)
		vl = config->sme_vl_in;
	else
		vl = config->sve_vl_in;

	return vl;
}

static int vl_expected(struct test_config *config)
{
	int vl;

	if (config->svcr_expected & SVCR_SM)
		vl = config->sme_vl_expected;
	else
		vl = config->sve_vl_expected;

	return vl;
}

static void run_child(struct test_config *config)
{
	int ret, flags;

	/* Let the parent attach to us */
	ret = ptrace(PTRACE_TRACEME, 0, 0, 0);
	if (ret < 0)
		ksft_exit_fail_msg("PTRACE_TRACEME failed: %s (%d)\n",
				   strerror(errno), errno);

	/* VL setup */
	if (sve_supported()) {
		ret = prctl(PR_SVE_SET_VL, config->sve_vl_in);
		if (ret != config->sve_vl_in) {
			ksft_print_msg("Failed to set SVE VL %d: %d\n",
				       config->sve_vl_in, ret);
		}
	}

	if (sme_supported()) {
		ret = prctl(PR_SME_SET_VL, config->sme_vl_in);
		if (ret != config->sme_vl_in) {
			ksft_print_msg("Failed to set SME VL %d: %d\n",
				       config->sme_vl_in, ret);
		}
	}

	/* Load values and wait for the parent */
	flags = 0;
	if (sve_supported())
		flags |= HAVE_SVE;
	if (sme_supported())
		flags |= HAVE_SME;
	if (sme2_supported())
		flags |= HAVE_SME2;
	if (fa64_supported())
		flags |= HAVE_FA64;
	if (fpmr_supported())
		flags |= HAVE_FPMR;

	load_and_save(flags);

	exit(0);
}

static void read_one_child_regs(pid_t child, char *name,
				struct iovec *iov_parent,
				struct iovec *iov_child)
{
	int len = iov_parent->iov_len;
	int ret;

	ret = process_vm_readv(child, iov_parent, 1, iov_child, 1, 0);
	if (ret == -1)
		ksft_print_msg("%s read failed: %s (%d)\n",
			       name, strerror(errno), errno);
	else if (ret != len)
		ksft_print_msg("Short read of %s: %d\n", name, ret);
}

static void read_child_regs(pid_t child)
{
	struct iovec iov_parent, iov_child;

	/*
	 * Since the child fork()ed from us the buffer addresses are
	 * the same in parent and child.
	 */
	iov_parent.iov_base = &v_out;
	iov_parent.iov_len = sizeof(v_out);
	iov_child.iov_base = &v_out;
	iov_child.iov_len = sizeof(v_out);
	read_one_child_regs(child, "FPSIMD", &iov_parent, &iov_child);

	if (sve_supported() || sme_supported()) {
		iov_parent.iov_base = &sve_vl_out;
		iov_parent.iov_len = sizeof(sve_vl_out);
		iov_child.iov_base = &sve_vl_out;
		iov_child.iov_len = sizeof(sve_vl_out);
		read_one_child_regs(child, "SVE VL", &iov_parent, &iov_child);

		iov_parent.iov_base = &z_out;
		iov_parent.iov_len = sizeof(z_out);
		iov_child.iov_base = &z_out;
		iov_child.iov_len = sizeof(z_out);
		read_one_child_regs(child, "Z", &iov_parent, &iov_child);

		iov_parent.iov_base = &p_out;
		iov_parent.iov_len = sizeof(p_out);
		iov_child.iov_base = &p_out;
		iov_child.iov_len = sizeof(p_out);
		read_one_child_regs(child, "P", &iov_parent, &iov_child);

		iov_parent.iov_base = &ffr_out;
		iov_parent.iov_len = sizeof(ffr_out);
		iov_child.iov_base = &ffr_out;
		iov_child.iov_len = sizeof(ffr_out);
		read_one_child_regs(child, "FFR", &iov_parent, &iov_child);
	}

	if (sme_supported()) {
		iov_parent.iov_base = &sme_vl_out;
		iov_parent.iov_len = sizeof(sme_vl_out);
		iov_child.iov_base = &sme_vl_out;
		iov_child.iov_len = sizeof(sme_vl_out);
		read_one_child_regs(child, "SME VL", &iov_parent, &iov_child);

		iov_parent.iov_base = &svcr_out;
		iov_parent.iov_len = sizeof(svcr_out);
		iov_child.iov_base = &svcr_out;
		iov_child.iov_len = sizeof(svcr_out);
		read_one_child_regs(child, "SVCR", &iov_parent, &iov_child);

		iov_parent.iov_base = &za_out;
		iov_parent.iov_len = sizeof(za_out);
		iov_child.iov_base = &za_out;
		iov_child.iov_len = sizeof(za_out);
		read_one_child_regs(child, "ZA", &iov_parent, &iov_child);
	}

	if (sme2_supported()) {
		iov_parent.iov_base = &zt_out;
		iov_parent.iov_len = sizeof(zt_out);
		iov_child.iov_base = &zt_out;
		iov_child.iov_len = sizeof(zt_out);
		read_one_child_regs(child, "ZT", &iov_parent, &iov_child);
	}

	if (fpmr_supported()) {
		iov_parent.iov_base = &fpmr_out;
		iov_parent.iov_len = sizeof(fpmr_out);
		iov_child.iov_base = &fpmr_out;
		iov_child.iov_len = sizeof(fpmr_out);
		read_one_child_regs(child, "FPMR", &iov_parent, &iov_child);
	}
}

static bool continue_breakpoint(pid_t child,
				enum __ptrace_request restart_type)
{
	struct user_pt_regs pt_regs;
	struct iovec iov;
	int ret;

	/* Get PC */
	iov.iov_base = &pt_regs;
	iov.iov_len = sizeof(pt_regs);
	ret = ptrace(PTRACE_GETREGSET, child, NT_PRSTATUS, &iov);
	if (ret < 0) {
		ksft_print_msg("Failed to get PC: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	/* Skip over the BRK */
	pt_regs.pc += 4;
	ret = ptrace(PTRACE_SETREGSET, child, NT_PRSTATUS, &iov);
	if (ret < 0) {
		ksft_print_msg("Failed to skip BRK: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	/* Restart */
	ret = ptrace(restart_type, child, 0, 0);
	if (ret < 0) {
		ksft_print_msg("Failed to restart child: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	return true;
}

static bool check_ptrace_values_sve(pid_t child, struct test_config *config)
{
	struct user_sve_header *sve;
	struct user_fpsimd_state *fpsimd;
	struct iovec iov;
	int ret, vq;
	bool pass = true;

	if (!sve_supported())
		return true;

	vq = __sve_vq_from_vl(config->sve_vl_in);

	iov.iov_len = SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		ksft_print_msg("OOM allocating %lu byte SVE buffer\n",
			       iov.iov_len);
		return false;
	}

	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_SVE, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read initial SVE: %s (%d)\n",
			       strerror(errno), errno);
		pass = false;
		goto out;
	}

	sve = iov.iov_base;

	if (sve->vl != config->sve_vl_in) {
		ksft_print_msg("Mismatch in initial SVE VL: %d != %d\n",
			       sve->vl, config->sve_vl_in);
		pass = false;
	}

	/* If we are in streaming mode we should just read FPSIMD */
	if ((config->svcr_in & SVCR_SM) && (sve->flags & SVE_PT_REGS_SVE)) {
		ksft_print_msg("NT_ARM_SVE reports SVE with PSTATE.SM\n");
		pass = false;
	}

	if (sve->size != SVE_PT_SIZE(vq, sve->flags)) {
		ksft_print_msg("Mismatch in SVE header size: %d != %lu\n",
			       sve->size, SVE_PT_SIZE(vq, sve->flags));
		pass = false;
	}

	/* The registers might be in completely different formats! */
	if (sve->flags & SVE_PT_REGS_SVE) {
		if (!compare_buffer("initial SVE Z",
				    iov.iov_base + SVE_PT_SVE_ZREG_OFFSET(vq, 0),
				    z_in, SVE_PT_SVE_ZREGS_SIZE(vq)))
			pass = false;

		if (!compare_buffer("initial SVE P",
				    iov.iov_base + SVE_PT_SVE_PREG_OFFSET(vq, 0),
				    p_in, SVE_PT_SVE_PREGS_SIZE(vq)))
			pass = false;

		if (!compare_buffer("initial SVE FFR",
				    iov.iov_base + SVE_PT_SVE_FFR_OFFSET(vq),
				    ffr_in, SVE_PT_SVE_PREG_SIZE(vq)))
			pass = false;
	} else {
		fpsimd = iov.iov_base + SVE_PT_FPSIMD_OFFSET;
		if (!compare_buffer("initial V via SVE", &fpsimd->vregs[0],
				    v_in, sizeof(v_in)))
			pass = false;
	}

out:
	free(iov.iov_base);
	return pass;
}

static bool check_ptrace_values_ssve(pid_t child, struct test_config *config)
{
	struct user_sve_header *sve;
	struct user_fpsimd_state *fpsimd;
	struct iovec iov;
	int ret, vq;
	bool pass = true;

	if (!sme_supported())
		return true;

	vq = __sve_vq_from_vl(config->sme_vl_in);

	iov.iov_len = SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		ksft_print_msg("OOM allocating %lu byte SSVE buffer\n",
			       iov.iov_len);
		return false;
	}

	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_SSVE, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read initial SSVE: %s (%d)\n",
			       strerror(errno), errno);
		pass = false;
		goto out;
	}

	sve = iov.iov_base;

	if (sve->vl != config->sme_vl_in) {
		ksft_print_msg("Mismatch in initial SSVE VL: %d != %d\n",
			       sve->vl, config->sme_vl_in);
		pass = false;
	}

	if ((config->svcr_in & SVCR_SM) && !(sve->flags & SVE_PT_REGS_SVE)) {
		ksft_print_msg("NT_ARM_SSVE reports FPSIMD with PSTATE.SM\n");
		pass = false;
	}

	if (sve->size != SVE_PT_SIZE(vq, sve->flags)) {
		ksft_print_msg("Mismatch in SSVE header size: %d != %lu\n",
			       sve->size, SVE_PT_SIZE(vq, sve->flags));
		pass = false;
	}

	/* The registers might be in completely different formats! */
	if (sve->flags & SVE_PT_REGS_SVE) {
		if (!compare_buffer("initial SSVE Z",
				    iov.iov_base + SVE_PT_SVE_ZREG_OFFSET(vq, 0),
				    z_in, SVE_PT_SVE_ZREGS_SIZE(vq)))
			pass = false;

		if (!compare_buffer("initial SSVE P",
				    iov.iov_base + SVE_PT_SVE_PREG_OFFSET(vq, 0),
				    p_in, SVE_PT_SVE_PREGS_SIZE(vq)))
			pass = false;

		if (!compare_buffer("initial SSVE FFR",
				    iov.iov_base + SVE_PT_SVE_FFR_OFFSET(vq),
				    ffr_in, SVE_PT_SVE_PREG_SIZE(vq)))
			pass = false;
	} else {
		fpsimd = iov.iov_base + SVE_PT_FPSIMD_OFFSET;
		if (!compare_buffer("initial V via SSVE",
				    &fpsimd->vregs[0], v_in, sizeof(v_in)))
			pass = false;
	}

out:
	free(iov.iov_base);
	return pass;
}

static bool check_ptrace_values_za(pid_t child, struct test_config *config)
{
	struct user_za_header *za;
	struct iovec iov;
	int ret, vq;
	bool pass = true;

	if (!sme_supported())
		return true;

	vq = __sve_vq_from_vl(config->sme_vl_in);

	iov.iov_len = ZA_SIG_CONTEXT_SIZE(vq);
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		ksft_print_msg("OOM allocating %lu byte ZA buffer\n",
			       iov.iov_len);
		return false;
	}

	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_ZA, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read initial ZA: %s (%d)\n",
			       strerror(errno), errno);
		pass = false;
		goto out;
	}

	za = iov.iov_base;

	if (za->vl != config->sme_vl_in) {
		ksft_print_msg("Mismatch in initial SME VL: %d != %d\n",
			       za->vl, config->sme_vl_in);
		pass = false;
	}

	/* If PSTATE.ZA is not set we should just read the header */
	if (config->svcr_in & SVCR_ZA) {
		if (za->size != ZA_PT_SIZE(vq)) {
			ksft_print_msg("Unexpected ZA ptrace read size: %d != %lu\n",
				       za->size, ZA_PT_SIZE(vq));
			pass = false;
		}

		if (!compare_buffer("initial ZA",
				    iov.iov_base + ZA_PT_ZA_OFFSET,
				    za_in, ZA_PT_ZA_SIZE(vq)))
			pass = false;
	} else {
		if (za->size != sizeof(*za)) {
			ksft_print_msg("Unexpected ZA ptrace read size: %d != %lu\n",
				       za->size, sizeof(*za));
			pass = false;
		}
	}

out:
	free(iov.iov_base);
	return pass;
}

static bool check_ptrace_values_zt(pid_t child, struct test_config *config)
{
	uint8_t buf[512];
	struct iovec iov;
	int ret;

	if (!sme2_supported())
		return true;

	iov.iov_base = &buf;
	iov.iov_len = ZT_SIG_REG_BYTES;
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_ZT, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read initial ZT: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	return compare_buffer("initial ZT", buf, zt_in, ZT_SIG_REG_BYTES);
}

static bool check_ptrace_values_fpmr(pid_t child, struct test_config *config)
{
	uint64_t val;
	struct iovec iov;
	int ret;

	if (!fpmr_supported())
		return true;

	iov.iov_base = &val;
	iov.iov_len = sizeof(val);
	ret = ptrace(PTRACE_GETREGSET, child, NT_ARM_FPMR, &iov);
	if (ret != 0) {
		ksft_print_msg("Failed to read initial FPMR: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	return compare_buffer("initial FPMR", &val, &fpmr_in, sizeof(val));
}

static bool check_ptrace_values(pid_t child, struct test_config *config)
{
	bool pass = true;
	struct user_fpsimd_state fpsimd;
	struct iovec iov;
	int ret;

	iov.iov_base = &fpsimd;
	iov.iov_len = sizeof(fpsimd);
	ret = ptrace(PTRACE_GETREGSET, child, NT_PRFPREG, &iov);
	if (ret == 0) {
		if (!compare_buffer("initial V", &fpsimd.vregs, v_in,
				    sizeof(v_in))) {
			pass = false;
		}
	} else {
		ksft_print_msg("Failed to read initial V: %s (%d)\n",
			       strerror(errno), errno);
		pass = false;
	}

	if (!check_ptrace_values_sve(child, config))
		pass = false;

	if (!check_ptrace_values_ssve(child, config))
		pass = false;

	if (!check_ptrace_values_za(child, config))
		pass = false;

	if (!check_ptrace_values_zt(child, config))
		pass = false;

	if (!check_ptrace_values_fpmr(child, config))
		pass = false;

	return pass;
}

static bool run_parent(pid_t child, struct test_definition *test,
		       struct test_config *config)
{
	int wait_status, ret;
	pid_t pid;
	bool pass;

	/* Initial attach */
	while (1) {
		pid = waitpid(child, &wait_status, 0);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			ksft_exit_fail_msg("waitpid() failed: %s (%d)\n",
					   strerror(errno), errno);
		}

		if (pid == child)
			break;
	}

	if (WIFEXITED(wait_status)) {
		ksft_print_msg("Child exited loading values with status %d\n",
			       WEXITSTATUS(wait_status));
		pass = false;
		goto out;
	}

	if (WIFSIGNALED(wait_status)) {
		ksft_print_msg("Child died from signal %d loading values\n",
			       WTERMSIG(wait_status));
		pass = false;
		goto out;
	}

	/* Read initial values via ptrace */
	pass = check_ptrace_values(child, config);

	/* Do whatever writes we want to do */
	if (test->modify_values)
		test->modify_values(child, config);

	if (!continue_breakpoint(child, PTRACE_CONT))
		goto cleanup;

	while (1) {
		pid = waitpid(child, &wait_status, 0);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			ksft_exit_fail_msg("waitpid() failed: %s (%d)\n",
					   strerror(errno), errno);
		}

		if (pid == child)
			break;
	}

	if (WIFEXITED(wait_status)) {
		ksft_print_msg("Child exited saving values with status %d\n",
			       WEXITSTATUS(wait_status));
		pass = false;
		goto out;
	}

	if (WIFSIGNALED(wait_status)) {
		ksft_print_msg("Child died from signal %d saving values\n",
			       WTERMSIG(wait_status));
		pass = false;
		goto out;
	}

	/* See what happened as a result */
	read_child_regs(child);

	if (!continue_breakpoint(child, PTRACE_DETACH))
		goto cleanup;

	/* The child should exit cleanly */
	got_alarm = false;
	alarm(1);
	while (1) {
		if (got_alarm) {
			ksft_print_msg("Wait for child timed out\n");
			goto cleanup;
		}

		pid = waitpid(child, &wait_status, 0);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			ksft_exit_fail_msg("waitpid() failed: %s (%d)\n",
					   strerror(errno), errno);
		}

		if (pid == child)
			break;
	}
	alarm(0);

	if (got_alarm) {
		ksft_print_msg("Timed out waiting for child\n");
		pass = false;
		goto cleanup;
	}

	if (pid == child && WIFSIGNALED(wait_status)) {
		ksft_print_msg("Child died from signal %d cleaning up\n",
			       WTERMSIG(wait_status));
		pass = false;
		goto out;
	}

	if (pid == child && WIFEXITED(wait_status)) {
		if (WEXITSTATUS(wait_status) != 0) {
			ksft_print_msg("Child exited with error %d\n",
				       WEXITSTATUS(wait_status));
			pass = false;
		}
	} else {
		ksft_print_msg("Child did not exit cleanly\n");
		pass = false;
		goto cleanup;
	}

	goto out;

cleanup:
	ret = kill(child, SIGKILL);
	if (ret != 0) {
		ksft_print_msg("kill() failed: %s (%d)\n",
			       strerror(errno), errno);
		return false;
	}

	while (1) {
		pid = waitpid(child, &wait_status, 0);
		if (pid < 0) {
			if (errno == EINTR)
				continue;
			ksft_exit_fail_msg("waitpid() failed: %s (%d)\n",
					   strerror(errno), errno);
		}

		if (pid == child)
			break;
	}

out:
	return pass;
}

static void fill_random(void *buf, size_t size)
{
	int i;
	uint32_t *lbuf = buf;

	/* random() returns a 32 bit number regardless of the size of long */
	for (i = 0; i < size / sizeof(uint32_t); i++)
		lbuf[i] = random();
}

static void fill_random_ffr(void *buf, size_t vq)
{
	uint8_t *lbuf = buf;
	int bits, i;

	/*
	 * Only values with a continuous set of 0..n bits set are
	 * valid for FFR, set all bits then clear a random number of
	 * high bits.
	 */
	memset(buf, 0, __SVE_FFR_SIZE(vq));

	bits = random() % (__SVE_FFR_SIZE(vq) * 8);
	for (i = 0; i < bits / 8; i++)
		lbuf[i] = 0xff;
	if (bits / 8 != __SVE_FFR_SIZE(vq))
		lbuf[i] = (1 << (bits % 8)) - 1;
}

static void fpsimd_to_sve(__uint128_t *v, char *z, int vl)
{
	int vq = __sve_vq_from_vl(vl);
	int i;
	__uint128_t *p;

	if (!vl)
		return;

	for (i = 0; i < __SVE_NUM_ZREGS; i++) {
		p = (__uint128_t *)&z[__SVE_ZREG_OFFSET(vq, i)];
		*p = arm64_cpu_to_le128(v[i]);
	}
}

static void set_initial_values(struct test_config *config)
{
	int vq = __sve_vq_from_vl(vl_in(config));
	int sme_vq = __sve_vq_from_vl(config->sme_vl_in);
	bool sm_change;

	svcr_in = config->svcr_in;
	svcr_expected = config->svcr_expected;
	svcr_out = 0;

	if (sme_supported() &&
	    (svcr_in & SVCR_SM) != (svcr_expected & SVCR_SM))
		sm_change = true;
	else
		sm_change = false;

	fill_random(&v_in, sizeof(v_in));
	memcpy(v_expected, v_in, sizeof(v_in));
	memset(v_out, 0, sizeof(v_out));

	/* Changes will be handled in the test case */
	if (sve_supported() || (config->svcr_in & SVCR_SM)) {
		/* The low 128 bits of Z are shared with the V registers */
		fill_random(&z_in, __SVE_ZREGS_SIZE(vq));
		fpsimd_to_sve(v_in, z_in, vl_in(config));
		memcpy(z_expected, z_in, __SVE_ZREGS_SIZE(vq));
		memset(z_out, 0, sizeof(z_out));

		fill_random(&p_in, __SVE_PREGS_SIZE(vq));
		memcpy(p_expected, p_in, __SVE_PREGS_SIZE(vq));
		memset(p_out, 0, sizeof(p_out));

		if ((config->svcr_in & SVCR_SM) && !fa64_supported())
			memset(ffr_in, 0, __SVE_PREG_SIZE(vq));
		else
			fill_random_ffr(&ffr_in, vq);
		memcpy(ffr_expected, ffr_in, __SVE_PREG_SIZE(vq));
		memset(ffr_out, 0, __SVE_PREG_SIZE(vq));
	}

	if (config->svcr_in & SVCR_ZA)
		fill_random(za_in, ZA_SIG_REGS_SIZE(sme_vq));
	else
		memset(za_in, 0, ZA_SIG_REGS_SIZE(sme_vq));
	if (config->svcr_expected & SVCR_ZA)
		memcpy(za_expected, za_in, ZA_SIG_REGS_SIZE(sme_vq));
	else
		memset(za_expected, 0, ZA_SIG_REGS_SIZE(sme_vq));
	if (sme_supported())
		memset(za_out, 0, sizeof(za_out));

	if (sme2_supported()) {
		if (config->svcr_in & SVCR_ZA)
			fill_random(zt_in, ZT_SIG_REG_BYTES);
		else
			memset(zt_in, 0, ZT_SIG_REG_BYTES);
		if (config->svcr_expected & SVCR_ZA)
			memcpy(zt_expected, zt_in, ZT_SIG_REG_BYTES);
		else
			memset(zt_expected, 0, ZT_SIG_REG_BYTES);
		memset(zt_out, 0, sizeof(zt_out));
	}

	if (fpmr_supported()) {
		fill_random(&fpmr_in, sizeof(fpmr_in));
		fpmr_in &= FPMR_SAFE_BITS;

		/* Entering or exiting streaming mode clears FPMR */
		if (sm_change)
			fpmr_expected = 0;
		else
			fpmr_expected = fpmr_in;
	} else {
		fpmr_in = 0;
		fpmr_expected = 0;
		fpmr_out = 0;
	}
}

static bool check_memory_values(struct test_config *config)
{
	bool pass = true;
	int vq, sme_vq;

	if (!compare_buffer("saved V", v_out, v_expected, sizeof(v_out)))
		pass = false;

	vq = __sve_vq_from_vl(vl_expected(config));
	sme_vq = __sve_vq_from_vl(config->sme_vl_expected);

	if (svcr_out != svcr_expected) {
		ksft_print_msg("Mismatch in saved SVCR %lx != %lx\n",
			       svcr_out, svcr_expected);
		pass = false;
	}

	if (sve_vl_out != config->sve_vl_expected) {
		ksft_print_msg("Mismatch in SVE VL: %ld != %d\n",
			       sve_vl_out, config->sve_vl_expected);
		pass = false;
	}

	if (sme_vl_out != config->sme_vl_expected) {
		ksft_print_msg("Mismatch in SME VL: %ld != %d\n",
			       sme_vl_out, config->sme_vl_expected);
		pass = false;
	}

	if (!compare_buffer("saved Z", z_out, z_expected,
			    __SVE_ZREGS_SIZE(vq)))
		pass = false;

	if (!compare_buffer("saved P", p_out, p_expected,
			    __SVE_PREGS_SIZE(vq)))
		pass = false;

	if (!compare_buffer("saved FFR", ffr_out, ffr_expected,
			    __SVE_PREG_SIZE(vq)))
		pass = false;

	if (!compare_buffer("saved ZA", za_out, za_expected,
			    ZA_PT_ZA_SIZE(sme_vq)))
		pass = false;

	if (!compare_buffer("saved ZT", zt_out, zt_expected, ZT_SIG_REG_BYTES))
		pass = false;

	if (fpmr_out != fpmr_expected) {
		ksft_print_msg("Mismatch in saved FPMR: %lx != %lx\n",
			       fpmr_out, fpmr_expected);
		pass = false;
	}

	return pass;
}

static bool sve_sme_same(struct test_config *config)
{
	if (config->sve_vl_in != config->sve_vl_expected)
		return false;

	if (config->sme_vl_in != config->sme_vl_expected)
		return false;

	if (config->svcr_in != config->svcr_expected)
		return false;

	return true;
}

static bool sve_write_supported(struct test_config *config)
{
	if (!sve_supported() && !sme_supported())
		return false;

	if ((config->svcr_in & SVCR_ZA) != (config->svcr_expected & SVCR_ZA))
		return false;

	if (config->svcr_expected & SVCR_SM) {
		if (config->sve_vl_in != config->sve_vl_expected) {
			return false;
		}

		/* Changing the SME VL disables ZA */
		if ((config->svcr_expected & SVCR_ZA) &&
		    (config->sme_vl_in != config->sme_vl_expected)) {
			return false;
		}
	} else {
		if (config->sme_vl_in != config->sme_vl_expected) {
			return false;
		}
	}

	return true;
}

static void fpsimd_write_expected(struct test_config *config)
{
	int vl;

	fill_random(&v_expected, sizeof(v_expected));

	/* The SVE registers are flushed by a FPSIMD write */
	vl = vl_expected(config);

	memset(z_expected, 0, __SVE_ZREGS_SIZE(__sve_vq_from_vl(vl)));
	memset(p_expected, 0, __SVE_PREGS_SIZE(__sve_vq_from_vl(vl)));
	memset(ffr_expected, 0, __SVE_PREG_SIZE(__sve_vq_from_vl(vl)));

	fpsimd_to_sve(v_expected, z_expected, vl);
}

static void fpsimd_write(pid_t child, struct test_config *test_config)
{
	struct user_fpsimd_state fpsimd;
	struct iovec iov;
	int ret;

	memset(&fpsimd, 0, sizeof(fpsimd));
	memcpy(&fpsimd.vregs, v_expected, sizeof(v_expected));

	iov.iov_base = &fpsimd;
	iov.iov_len = sizeof(fpsimd);
	ret = ptrace(PTRACE_SETREGSET, child, NT_PRFPREG, &iov);
	if (ret == -1)
		ksft_print_msg("FPSIMD set failed: (%s) %d\n",
			       strerror(errno), errno);
}

static bool fpmr_write_supported(struct test_config *config)
{
	if (!fpmr_supported())
		return false;

	if (!sve_sme_same(config))
		return false;

	return true;
}

static void fpmr_write_expected(struct test_config *config)
{
	fill_random(&fpmr_expected, sizeof(fpmr_expected));
	fpmr_expected &= FPMR_SAFE_BITS;
}

static void fpmr_write(pid_t child, struct test_config *config)
{
	struct iovec iov;
	int ret;

	iov.iov_len = sizeof(fpmr_expected);
	iov.iov_base = &fpmr_expected;
	ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_FPMR, &iov);
	if (ret != 0)
		ksft_print_msg("Failed to write FPMR: %s (%d)\n",
			       strerror(errno), errno);
}

static void sve_write_expected(struct test_config *config)
{
	int vl = vl_expected(config);
	int sme_vq = __sve_vq_from_vl(config->sme_vl_expected);

	fill_random(z_expected, __SVE_ZREGS_SIZE(__sve_vq_from_vl(vl)));
	fill_random(p_expected, __SVE_PREGS_SIZE(__sve_vq_from_vl(vl)));

	if ((svcr_expected & SVCR_SM) && !fa64_supported())
		memset(ffr_expected, 0, __SVE_PREG_SIZE(sme_vq));
	else
		fill_random_ffr(ffr_expected, __sve_vq_from_vl(vl));

	/* Share the low bits of Z with V */
	fill_random(&v_expected, sizeof(v_expected));
	fpsimd_to_sve(v_expected, z_expected, vl);

	if (config->sme_vl_in != config->sme_vl_expected) {
		memset(za_expected, 0, ZA_PT_ZA_SIZE(sme_vq));
		memset(zt_expected, 0, sizeof(zt_expected));
	}
}

static void sve_write(pid_t child, struct test_config *config)
{
	struct user_sve_header *sve;
	struct iovec iov;
	int ret, vl, vq, regset;

	vl = vl_expected(config);
	vq = __sve_vq_from_vl(vl);

	iov.iov_len = SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		ksft_print_msg("Failed allocating %lu byte SVE write buffer\n",
			       iov.iov_len);
		return;
	}
	memset(iov.iov_base, 0, iov.iov_len);

	sve = iov.iov_base;
	sve->size = iov.iov_len;
	sve->flags = SVE_PT_REGS_SVE;
	sve->vl = vl;

	memcpy(iov.iov_base + SVE_PT_SVE_ZREG_OFFSET(vq, 0),
	       z_expected, SVE_PT_SVE_ZREGS_SIZE(vq));
	memcpy(iov.iov_base + SVE_PT_SVE_PREG_OFFSET(vq, 0),
	       p_expected, SVE_PT_SVE_PREGS_SIZE(vq));
	memcpy(iov.iov_base + SVE_PT_SVE_FFR_OFFSET(vq),
	       ffr_expected, SVE_PT_SVE_PREG_SIZE(vq));

	if (svcr_expected & SVCR_SM)
		regset = NT_ARM_SSVE;
	else
		regset = NT_ARM_SVE;

	ret = ptrace(PTRACE_SETREGSET, child, regset, &iov);
	if (ret != 0)
		ksft_print_msg("Failed to write SVE: %s (%d)\n",
			       strerror(errno), errno);

	free(iov.iov_base);
}

static bool za_write_supported(struct test_config *config)
{
	if (config->sme_vl_in != config->sme_vl_expected) {
		/* Changing the SME VL exits streaming mode. */
		if (config->svcr_expected & SVCR_SM) {
			return false;
		}
	} else {
		/* Otherwise we can't change streaming mode */
		if ((config->svcr_in & SVCR_SM) !=
		    (config->svcr_expected & SVCR_SM)) {
			return false;
		}
	}

	return true;
}

static void za_write_expected(struct test_config *config)
{
	int sme_vq, sve_vq;

	sme_vq = __sve_vq_from_vl(config->sme_vl_expected);

	if (config->svcr_expected & SVCR_ZA) {
		fill_random(za_expected, ZA_PT_ZA_SIZE(sme_vq));
	} else {
		memset(za_expected, 0, ZA_PT_ZA_SIZE(sme_vq));
		memset(zt_expected, 0, sizeof(zt_expected));
	}

	/* Changing the SME VL flushes ZT, SVE state and exits SM */
	if (config->sme_vl_in != config->sme_vl_expected) {
		svcr_expected &= ~SVCR_SM;

		sve_vq = __sve_vq_from_vl(vl_expected(config));
		memset(z_expected, 0, __SVE_ZREGS_SIZE(sve_vq));
		memset(p_expected, 0, __SVE_PREGS_SIZE(sve_vq));
		memset(ffr_expected, 0, __SVE_PREG_SIZE(sve_vq));
		memset(zt_expected, 0, sizeof(zt_expected));

		fpsimd_to_sve(v_expected, z_expected, vl_expected(config));
	}
}

static void za_write(pid_t child, struct test_config *config)
{
	struct user_za_header *za;
	struct iovec iov;
	int ret, vq;

	vq = __sve_vq_from_vl(config->sme_vl_expected);

	if (config->svcr_expected & SVCR_ZA)
		iov.iov_len = ZA_PT_SIZE(vq);
	else
		iov.iov_len = sizeof(*za);
	iov.iov_base = malloc(iov.iov_len);
	if (!iov.iov_base) {
		ksft_print_msg("Failed allocating %lu byte ZA write buffer\n",
			       iov.iov_len);
		return;
	}
	memset(iov.iov_base, 0, iov.iov_len);

	za = iov.iov_base;
	za->size = iov.iov_len;
	za->vl = config->sme_vl_expected;
	if (config->svcr_expected & SVCR_ZA)
		memcpy(iov.iov_base + ZA_PT_ZA_OFFSET, za_expected,
		       ZA_PT_ZA_SIZE(vq));

	ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_ZA, &iov);
	if (ret != 0)
		ksft_print_msg("Failed to write ZA: %s (%d)\n",
			       strerror(errno), errno);

	free(iov.iov_base);
}

static bool zt_write_supported(struct test_config *config)
{
	if (!sme2_supported())
		return false;
	if (config->sme_vl_in != config->sme_vl_expected)
		return false;
	if (!(config->svcr_expected & SVCR_ZA))
		return false;
	if ((config->svcr_in & SVCR_SM) != (config->svcr_expected & SVCR_SM))
		return false;

	return true;
}

static void zt_write_expected(struct test_config *config)
{
	int sme_vq;

	sme_vq = __sve_vq_from_vl(config->sme_vl_expected);

	if (config->svcr_expected & SVCR_ZA) {
		fill_random(zt_expected, sizeof(zt_expected));
	} else {
		memset(za_expected, 0, ZA_PT_ZA_SIZE(sme_vq));
		memset(zt_expected, 0, sizeof(zt_expected));
	}
}

static void zt_write(pid_t child, struct test_config *config)
{
	struct iovec iov;
	int ret;

	iov.iov_len = ZT_SIG_REG_BYTES;
	iov.iov_base = zt_expected;
	ret = ptrace(PTRACE_SETREGSET, child, NT_ARM_ZT, &iov);
	if (ret != 0)
		ksft_print_msg("Failed to write ZT: %s (%d)\n",
			       strerror(errno), errno);
}

/* Actually run a test */
static void run_test(struct test_definition *test, struct test_config *config)
{
	pid_t child;
	char name[1024];
	bool pass;

	if (sve_supported() && sme_supported())
		snprintf(name, sizeof(name), "%s, SVE %d->%d, SME %d/%x->%d/%x",
			 test->name,
			 config->sve_vl_in, config->sve_vl_expected,
			 config->sme_vl_in, config->svcr_in,
			 config->sme_vl_expected, config->svcr_expected);
	else if (sve_supported())
		snprintf(name, sizeof(name), "%s, SVE %d->%d", test->name,
			 config->sve_vl_in, config->sve_vl_expected);
	else if (sme_supported())
		snprintf(name, sizeof(name), "%s, SME %d/%x->%d/%x",
			 test->name,
			 config->sme_vl_in, config->svcr_in,
			 config->sme_vl_expected, config->svcr_expected);
	else
		snprintf(name, sizeof(name), "%s", test->name);

	if (test->supported && !test->supported(config)) {
		ksft_test_result_skip("%s\n", name);
		return;
	}

	set_initial_values(config);

	if (test->set_expected_values)
		test->set_expected_values(config);

	child = fork();
	if (child < 0)
		ksft_exit_fail_msg("fork() failed: %s (%d)\n",
				   strerror(errno), errno);
	/* run_child() never returns */
	if (child == 0)
		run_child(config);

	pass = run_parent(child, test, config);
	if (!check_memory_values(config))
		pass = false;

	ksft_test_result(pass, "%s\n", name);
}

static void run_tests(struct test_definition defs[], int count,
		      struct test_config *config)
{
	int i;

	for (i = 0; i < count; i++)
		run_test(&defs[i], config);
}

static struct test_definition base_test_defs[] = {
	{
		.name = "No writes",
		.supported = sve_sme_same,
	},
	{
		.name = "FPSIMD write",
		.supported = sve_sme_same,
		.set_expected_values = fpsimd_write_expected,
		.modify_values = fpsimd_write,
	},
	{
		.name = "FPMR write",
		.supported = fpmr_write_supported,
		.set_expected_values = fpmr_write_expected,
		.modify_values = fpmr_write,
	},
};

static struct test_definition sve_test_defs[] = {
	{
		.name = "SVE write",
		.supported = sve_write_supported,
		.set_expected_values = sve_write_expected,
		.modify_values = sve_write,
	},
};

static struct test_definition za_test_defs[] = {
	{
		.name = "ZA write",
		.supported = za_write_supported,
		.set_expected_values = za_write_expected,
		.modify_values = za_write,
	},
};

static struct test_definition zt_test_defs[] = {
	{
		.name = "ZT write",
		.supported = zt_write_supported,
		.set_expected_values = zt_write_expected,
		.modify_values = zt_write,
	},
};

static int sve_vls[MAX_NUM_VLS], sme_vls[MAX_NUM_VLS];
static int sve_vl_count, sme_vl_count;

static void probe_vls(const char *name, int vls[], int *vl_count, int set_vl)
{
	unsigned int vq;
	int vl;

	*vl_count = 0;

	for (vq = ARCH_VQ_MAX; vq > 0; vq /= 2) {
		vl = prctl(set_vl, vq * 16);
		if (vl == -1)
			ksft_exit_fail_msg("SET_VL failed: %s (%d)\n",
					   strerror(errno), errno);

		vl &= PR_SVE_VL_LEN_MASK;

		if (*vl_count && (vl == vls[*vl_count - 1]))
			break;

		vq = sve_vq_from_vl(vl);

		vls[*vl_count] = vl;
		*vl_count += 1;
	}

	if (*vl_count > 2) {
		/* Just use the minimum and maximum */
		vls[1] = vls[*vl_count - 1];
		ksft_print_msg("%d %s VLs, using %d and %d\n",
			       *vl_count, name, vls[0], vls[1]);
		*vl_count = 2;
	} else {
		ksft_print_msg("%d %s VLs\n", *vl_count, name);
	}
}

static struct {
	int svcr_in, svcr_expected;
} svcr_combinations[] = {
	{ .svcr_in = 0, .svcr_expected = 0, },
	{ .svcr_in = 0, .svcr_expected = SVCR_SM, },
	{ .svcr_in = 0, .svcr_expected = SVCR_ZA, },
	/* Can't enable both SM and ZA with a single ptrace write */

	{ .svcr_in = SVCR_SM, .svcr_expected = 0, },
	{ .svcr_in = SVCR_SM, .svcr_expected = SVCR_SM, },
	{ .svcr_in = SVCR_SM, .svcr_expected = SVCR_ZA, },
	{ .svcr_in = SVCR_SM, .svcr_expected = SVCR_SM | SVCR_ZA, },

	{ .svcr_in = SVCR_ZA, .svcr_expected = 0, },
	{ .svcr_in = SVCR_ZA, .svcr_expected = SVCR_SM, },
	{ .svcr_in = SVCR_ZA, .svcr_expected = SVCR_ZA, },
	{ .svcr_in = SVCR_ZA, .svcr_expected = SVCR_SM | SVCR_ZA, },

	{ .svcr_in = SVCR_SM | SVCR_ZA, .svcr_expected = 0, },
	{ .svcr_in = SVCR_SM | SVCR_ZA, .svcr_expected = SVCR_SM, },
	{ .svcr_in = SVCR_SM | SVCR_ZA, .svcr_expected = SVCR_ZA, },
	{ .svcr_in = SVCR_SM | SVCR_ZA, .svcr_expected = SVCR_SM | SVCR_ZA, },
};

static void run_sve_tests(void)
{
	struct test_config test_config;
	int i, j;

	if (!sve_supported())
		return;

	test_config.sme_vl_in = sme_vls[0];
	test_config.sme_vl_expected = sme_vls[0];
	test_config.svcr_in = 0;
	test_config.svcr_expected = 0;

	for (i = 0; i < sve_vl_count; i++) {
		test_config.sve_vl_in = sve_vls[i];

		for (j = 0; j < sve_vl_count; j++) {
			test_config.sve_vl_expected = sve_vls[j];

			run_tests(base_test_defs,
				  ARRAY_SIZE(base_test_defs),
				  &test_config);
			if (sve_supported())
				run_tests(sve_test_defs,
					  ARRAY_SIZE(sve_test_defs),
					  &test_config);
		}
	}

}

static void run_sme_tests(void)
{
	struct test_config test_config;
	int i, j, k;

	if (!sme_supported())
		return;

	test_config.sve_vl_in = sve_vls[0];
	test_config.sve_vl_expected = sve_vls[0];

	/*
	 * Every SME VL/SVCR combination
	 */
	for (i = 0; i < sme_vl_count; i++) {
		test_config.sme_vl_in = sme_vls[i];

		for (j = 0; j < sme_vl_count; j++) {
			test_config.sme_vl_expected = sme_vls[j];

			for (k = 0; k < ARRAY_SIZE(svcr_combinations); k++) {
				test_config.svcr_in = svcr_combinations[k].svcr_in;
				test_config.svcr_expected = svcr_combinations[k].svcr_expected;

				run_tests(base_test_defs,
					  ARRAY_SIZE(base_test_defs),
					  &test_config);
				run_tests(sve_test_defs,
					  ARRAY_SIZE(sve_test_defs),
					  &test_config);
				run_tests(za_test_defs,
					  ARRAY_SIZE(za_test_defs),
					  &test_config);

				if (sme2_supported())
					run_tests(zt_test_defs,
						  ARRAY_SIZE(zt_test_defs),
						  &test_config);
			}
		}
	}
}

int main(void)
{
	struct test_config test_config;
	struct sigaction sa;
	int tests, ret, tmp;

	srandom(getpid());

	ksft_print_header();

	if (sve_supported()) {
		probe_vls("SVE", sve_vls, &sve_vl_count, PR_SVE_SET_VL);

		tests = ARRAY_SIZE(base_test_defs) +
			ARRAY_SIZE(sve_test_defs);
		tests *= sve_vl_count * sve_vl_count;
	} else {
		/* Only run the FPSIMD tests */
		sve_vl_count = 1;
		tests = ARRAY_SIZE(base_test_defs);
	}

	if (sme_supported()) {
		probe_vls("SME", sme_vls, &sme_vl_count, PR_SME_SET_VL);

		tmp = ARRAY_SIZE(base_test_defs) + ARRAY_SIZE(sve_test_defs)
			+ ARRAY_SIZE(za_test_defs);

		if (sme2_supported())
			tmp += ARRAY_SIZE(zt_test_defs);

		tmp *= sme_vl_count * sme_vl_count;
		tmp *= ARRAY_SIZE(svcr_combinations);
		tests += tmp;
	} else {
		sme_vl_count = 1;
	}

	if (sme2_supported())
		ksft_print_msg("SME2 supported\n");

	if (fa64_supported())
		ksft_print_msg("FA64 supported\n");

	if (fpmr_supported())
		ksft_print_msg("FPMR supported\n");

	ksft_set_plan(tests);

	/* Get signal handers ready before we start any children */
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = handle_alarm;
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	ret = sigaction(SIGALRM, &sa, NULL);
	if (ret < 0)
		ksft_print_msg("Failed to install SIGALRM handler: %s (%d)\n",
			       strerror(errno), errno);

	/*
	 * Run the test set if there is no SVE or SME, with those we
	 * have to pick a VL for each run.
	 */
	if (!sve_supported()) {
		test_config.sve_vl_in = 0;
		test_config.sve_vl_expected = 0;
		test_config.sme_vl_in = 0;
		test_config.sme_vl_expected = 0;
		test_config.svcr_in = 0;
		test_config.svcr_expected = 0;

		run_tests(base_test_defs, ARRAY_SIZE(base_test_defs),
			  &test_config);
	}

	run_sve_tests();
	run_sme_tests();

	ksft_finished();
}
