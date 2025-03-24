// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015-2021 ARM Limited.
 * Original author: Dave Martin <Dave.Martin@arm.com>
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
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/sigcontext.h>
#include <asm/ptrace.h>

#include "../../kselftest.h"

/* <linux/elf.h> and <sys/auxv.h> don't like each other, so: */
#ifndef NT_ARM_SVE
#define NT_ARM_SVE 0x405
#endif

#ifndef NT_ARM_SSVE
#define NT_ARM_SSVE 0x40b
#endif

/*
 * The architecture defines the maximum VQ as 16 but for extensibility
 * the kernel specifies the SVE_VQ_MAX as 512 resulting in us running
 * a *lot* more tests than are useful if we use it.  Until the
 * architecture is extended let's limit our coverage to what is
 * currently allowed, plus one extra to ensure we cover constraining
 * the VL as expected.
 */
#define TEST_VQ_MAX 17

struct vec_type {
	const char *name;
	unsigned long hwcap_type;
	unsigned long hwcap;
	int regset;
	int prctl_set;
};

static const struct vec_type vec_types[] = {
	{
		.name = "SVE",
		.hwcap_type = AT_HWCAP,
		.hwcap = HWCAP_SVE,
		.regset = NT_ARM_SVE,
		.prctl_set = PR_SVE_SET_VL,
	},
	{
		.name = "Streaming SVE",
		.hwcap_type = AT_HWCAP2,
		.hwcap = HWCAP2_SME,
		.regset = NT_ARM_SSVE,
		.prctl_set = PR_SME_SET_VL,
	},
};

#define VL_TESTS (((TEST_VQ_MAX - SVE_VQ_MIN) + 1) * 4)
#define FLAG_TESTS 2
#define FPSIMD_TESTS 2

#define EXPECTED_TESTS ((VL_TESTS + FLAG_TESTS + FPSIMD_TESTS) * ARRAY_SIZE(vec_types))

static void fill_buf(char *buf, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		buf[i] = random();
}

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
		ksft_exit_fail_msg("ptrace(PTRACE_TRACEME) failed: %s (%d)\n",
				   strerror(errno), errno);

	if (raise(SIGSTOP))
		ksft_exit_fail_msg("raise(SIGSTOP) failed: %s (%d)\n",
				   strerror(errno), errno);

	return EXIT_SUCCESS;
}

static int get_fpsimd(pid_t pid, struct user_fpsimd_state *fpsimd)
{
	struct iovec iov;

	iov.iov_base = fpsimd;
	iov.iov_len = sizeof(*fpsimd);
	return ptrace(PTRACE_GETREGSET, pid, NT_PRFPREG, &iov);
}

static int set_fpsimd(pid_t pid, struct user_fpsimd_state *fpsimd)
{
	struct iovec iov;

	iov.iov_base = fpsimd;
	iov.iov_len = sizeof(*fpsimd);
	return ptrace(PTRACE_SETREGSET, pid, NT_PRFPREG, &iov);
}

static struct user_sve_header *get_sve(pid_t pid, const struct vec_type *type,
				       void **buf, size_t *size)
{
	struct user_sve_header *sve;
	void *p;
	size_t sz = sizeof *sve;
	struct iovec iov;

	while (1) {
		if (*size < sz) {
			p = realloc(*buf, sz);
			if (!p) {
				errno = ENOMEM;
				goto error;
			}

			*buf = p;
			*size = sz;
		}

		iov.iov_base = *buf;
		iov.iov_len = sz;
		if (ptrace(PTRACE_GETREGSET, pid, type->regset, &iov))
			goto error;

		sve = *buf;
		if (sve->size <= sz)
			break;

		sz = sve->size;
	}

	return sve;

error:
	return NULL;
}

static int set_sve(pid_t pid, const struct vec_type *type,
		   const struct user_sve_header *sve)
{
	struct iovec iov;

	iov.iov_base = (void *)sve;
	iov.iov_len = sve->size;
	return ptrace(PTRACE_SETREGSET, pid, type->regset, &iov);
}

/* Validate setting and getting the inherit flag */
static void ptrace_set_get_inherit(pid_t child, const struct vec_type *type)
{
	struct user_sve_header sve;
	struct user_sve_header *new_sve = NULL;
	size_t new_sve_size = 0;
	int ret;

	/* First set the flag */
	memset(&sve, 0, sizeof(sve));
	sve.size = sizeof(sve);
	sve.vl = sve_vl_from_vq(SVE_VQ_MIN);
	sve.flags = SVE_PT_VL_INHERIT;
	ret = set_sve(child, type, &sve);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set %s SVE_PT_VL_INHERIT\n",
				      type->name);
		return;
	}

	/*
	 * Read back the new register state and verify that we have
	 * set the flags we expected.
	 */
	if (!get_sve(child, type, (void **)&new_sve, &new_sve_size)) {
		ksft_test_result_fail("Failed to read %s SVE flags\n",
				      type->name);
		return;
	}

	ksft_test_result(new_sve->flags & SVE_PT_VL_INHERIT,
			 "%s SVE_PT_VL_INHERIT set\n", type->name);

	/* Now clear */
	sve.flags &= ~SVE_PT_VL_INHERIT;
	ret = set_sve(child, type, &sve);
	if (ret != 0) {
		ksft_test_result_fail("Failed to clear %s SVE_PT_VL_INHERIT\n",
				      type->name);
		return;
	}

	if (!get_sve(child, type, (void **)&new_sve, &new_sve_size)) {
		ksft_test_result_fail("Failed to read %s SVE flags\n",
				      type->name);
		return;
	}

	ksft_test_result(!(new_sve->flags & SVE_PT_VL_INHERIT),
			 "%s SVE_PT_VL_INHERIT cleared\n", type->name);

	free(new_sve);
}

/* Validate attempting to set the specfied VL via ptrace */
static void ptrace_set_get_vl(pid_t child, const struct vec_type *type,
			      unsigned int vl, bool *supported)
{
	struct user_sve_header sve;
	struct user_sve_header *new_sve = NULL;
	size_t new_sve_size = 0;
	int ret, prctl_vl;

	*supported = false;

	/* Check if the VL is supported in this process */
	prctl_vl = prctl(type->prctl_set, vl);
	if (prctl_vl == -1)
		ksft_exit_fail_msg("prctl(PR_%s_SET_VL) failed: %s (%d)\n",
				   type->name, strerror(errno), errno);

	/* If the VL is not supported then a supported VL will be returned */
	*supported = (prctl_vl == vl);

	/* Set the VL by doing a set with no register payload */
	memset(&sve, 0, sizeof(sve));
	sve.size = sizeof(sve);
	sve.vl = vl;
	ret = set_sve(child, type, &sve);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set %s VL %u\n",
				      type->name, vl);
		return;
	}

	/*
	 * Read back the new register state and verify that we have the
	 * same VL that we got from prctl() on ourselves.
	 */
	if (!get_sve(child, type, (void **)&new_sve, &new_sve_size)) {
		ksft_test_result_fail("Failed to read %s VL %u\n",
				      type->name, vl);
		return;
	}

	ksft_test_result(new_sve->vl = prctl_vl, "Set %s VL %u\n",
			 type->name, vl);

	free(new_sve);
}

static void check_u32(unsigned int vl, const char *reg,
		      uint32_t *in, uint32_t *out, int *errors)
{
	if (*in != *out) {
		printf("# VL %d %s wrote %x read %x\n",
		       vl, reg, *in, *out);
		(*errors)++;
	}
}

/* Access the FPSIMD registers via the SVE regset */
static void ptrace_sve_fpsimd(pid_t child, const struct vec_type *type)
{
	void *svebuf;
	struct user_sve_header *sve;
	struct user_fpsimd_state *fpsimd, new_fpsimd;
	unsigned int i, j;
	unsigned char *p;
	int ret;

	svebuf = malloc(SVE_PT_SIZE(0, SVE_PT_REGS_FPSIMD));
	if (!svebuf) {
		ksft_test_result_fail("Failed to allocate FPSIMD buffer\n");
		return;
	}

	memset(svebuf, 0, SVE_PT_SIZE(0, SVE_PT_REGS_FPSIMD));
	sve = svebuf;
	sve->flags = SVE_PT_REGS_FPSIMD;
	sve->size = SVE_PT_SIZE(0, SVE_PT_REGS_FPSIMD);
	sve->vl = 16;  /* We don't care what the VL is */

	/* Try to set a known FPSIMD state via PT_REGS_SVE */
	fpsimd = (struct user_fpsimd_state *)((char *)sve +
					      SVE_PT_FPSIMD_OFFSET);
	for (i = 0; i < 32; ++i) {
		p = (unsigned char *)&fpsimd->vregs[i];

		for (j = 0; j < sizeof(fpsimd->vregs[i]); ++j)
			p[j] = j;
	}

	ret = set_sve(child, type, sve);
	ksft_test_result(ret == 0, "%s FPSIMD set via SVE: %d\n",
			 type->name, ret);
	if (ret)
		goto out;

	/* Verify via the FPSIMD regset */
	if (get_fpsimd(child, &new_fpsimd)) {
		ksft_test_result_fail("get_fpsimd(): %s\n",
				      strerror(errno));
		goto out;
	}
	if (memcmp(fpsimd, &new_fpsimd, sizeof(*fpsimd)) == 0)
		ksft_test_result_pass("%s get_fpsimd() gave same state\n",
				      type->name);
	else
		ksft_test_result_fail("%s get_fpsimd() gave different state\n",
				      type->name);

out:
	free(svebuf);
}

/* Validate attempting to set SVE data and read SVE data */
static void ptrace_set_sve_get_sve_data(pid_t child,
					const struct vec_type *type,
					unsigned int vl)
{
	void *write_buf;
	void *read_buf = NULL;
	struct user_sve_header *write_sve;
	struct user_sve_header *read_sve;
	size_t read_sve_size = 0;
	unsigned int vq = sve_vq_from_vl(vl);
	int ret, i;
	size_t data_size;
	int errors = 0;

	data_size = SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
	write_buf = malloc(data_size);
	if (!write_buf) {
		ksft_test_result_fail("Error allocating %ld byte buffer for %s VL %u\n",
				      data_size, type->name, vl);
		return;
	}
	write_sve = write_buf;

	/* Set up some data and write it out */
	memset(write_sve, 0, data_size);
	write_sve->size = data_size;
	write_sve->vl = vl;
	write_sve->flags = SVE_PT_REGS_SVE;

	for (i = 0; i < __SVE_NUM_ZREGS; i++)
		fill_buf(write_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
			 SVE_PT_SVE_ZREG_SIZE(vq));

	for (i = 0; i < __SVE_NUM_PREGS; i++)
		fill_buf(write_buf + SVE_PT_SVE_PREG_OFFSET(vq, i),
			 SVE_PT_SVE_PREG_SIZE(vq));

	fill_buf(write_buf + SVE_PT_SVE_FPSR_OFFSET(vq), SVE_PT_SVE_FPSR_SIZE);
	fill_buf(write_buf + SVE_PT_SVE_FPCR_OFFSET(vq), SVE_PT_SVE_FPCR_SIZE);

	/* TODO: Generate a valid FFR pattern */

	ret = set_sve(child, type, write_sve);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set %s VL %u data\n",
				      type->name, vl);
		goto out;
	}

	/* Read the data back */
	if (!get_sve(child, type, (void **)&read_buf, &read_sve_size)) {
		ksft_test_result_fail("Failed to read %s VL %u data\n",
				      type->name, vl);
		goto out;
	}
	read_sve = read_buf;

	/* We might read more data if there's extensions we don't know */
	if (read_sve->size < write_sve->size) {
		ksft_test_result_fail("%s wrote %d bytes, only read %d\n",
				      type->name, write_sve->size,
				      read_sve->size);
		goto out_read;
	}

	for (i = 0; i < __SVE_NUM_ZREGS; i++) {
		if (memcmp(write_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
			   read_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
			   SVE_PT_SVE_ZREG_SIZE(vq)) != 0) {
			printf("# Mismatch in %u Z%d\n", vl, i);
			errors++;
		}
	}

	for (i = 0; i < __SVE_NUM_PREGS; i++) {
		if (memcmp(write_buf + SVE_PT_SVE_PREG_OFFSET(vq, i),
			   read_buf + SVE_PT_SVE_PREG_OFFSET(vq, i),
			   SVE_PT_SVE_PREG_SIZE(vq)) != 0) {
			printf("# Mismatch in %u P%d\n", vl, i);
			errors++;
		}
	}

	check_u32(vl, "FPSR", write_buf + SVE_PT_SVE_FPSR_OFFSET(vq),
		  read_buf + SVE_PT_SVE_FPSR_OFFSET(vq), &errors);
	check_u32(vl, "FPCR", write_buf + SVE_PT_SVE_FPCR_OFFSET(vq),
		  read_buf + SVE_PT_SVE_FPCR_OFFSET(vq), &errors);

	ksft_test_result(errors == 0, "Set and get %s data for VL %u\n",
			 type->name, vl);

out_read:
	free(read_buf);
out:
	free(write_buf);
}

/* Validate attempting to set SVE data and read it via the FPSIMD regset */
static void ptrace_set_sve_get_fpsimd_data(pid_t child,
					   const struct vec_type *type,
					   unsigned int vl)
{
	void *write_buf;
	struct user_sve_header *write_sve;
	unsigned int vq = sve_vq_from_vl(vl);
	struct user_fpsimd_state fpsimd_state;
	int ret, i;
	size_t data_size;
	int errors = 0;

	if (__BYTE_ORDER == __BIG_ENDIAN) {
		ksft_test_result_skip("Big endian not supported\n");
		return;
	}

	data_size = SVE_PT_SVE_OFFSET + SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
	write_buf = malloc(data_size);
	if (!write_buf) {
		ksft_test_result_fail("Error allocating %ld byte buffer for %s VL %u\n",
				      data_size, type->name, vl);
		return;
	}
	write_sve = write_buf;

	/* Set up some data and write it out */
	memset(write_sve, 0, data_size);
	write_sve->size = data_size;
	write_sve->vl = vl;
	write_sve->flags = SVE_PT_REGS_SVE;

	for (i = 0; i < __SVE_NUM_ZREGS; i++)
		fill_buf(write_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
			 SVE_PT_SVE_ZREG_SIZE(vq));

	fill_buf(write_buf + SVE_PT_SVE_FPSR_OFFSET(vq), SVE_PT_SVE_FPSR_SIZE);
	fill_buf(write_buf + SVE_PT_SVE_FPCR_OFFSET(vq), SVE_PT_SVE_FPCR_SIZE);

	ret = set_sve(child, type, write_sve);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set %s VL %u data\n",
				      type->name, vl);
		goto out;
	}

	/* Read the data back */
	if (get_fpsimd(child, &fpsimd_state)) {
		ksft_test_result_fail("Failed to read %s VL %u FPSIMD data\n",
				      type->name, vl);
		goto out;
	}

	for (i = 0; i < __SVE_NUM_ZREGS; i++) {
		__uint128_t tmp = 0;

		/*
		 * Z regs are stored endianness invariant, this won't
		 * work for big endian
		 */
		memcpy(&tmp, write_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
		       sizeof(tmp));

		if (tmp != fpsimd_state.vregs[i]) {
			printf("# Mismatch in FPSIMD for %s VL %u Z%d\n",
			       type->name, vl, i);
			errors++;
		}
	}

	check_u32(vl, "FPSR", write_buf + SVE_PT_SVE_FPSR_OFFSET(vq),
		  &fpsimd_state.fpsr, &errors);
	check_u32(vl, "FPCR", write_buf + SVE_PT_SVE_FPCR_OFFSET(vq),
		  &fpsimd_state.fpcr, &errors);

	ksft_test_result(errors == 0, "Set and get FPSIMD data for %s VL %u\n",
			 type->name, vl);

out:
	free(write_buf);
}

/* Validate attempting to set FPSIMD data and read it via the SVE regset */
static void ptrace_set_fpsimd_get_sve_data(pid_t child,
					   const struct vec_type *type,
					   unsigned int vl)
{
	void *read_buf = NULL;
	unsigned char *p;
	struct user_sve_header *read_sve;
	unsigned int vq = sve_vq_from_vl(vl);
	struct user_fpsimd_state write_fpsimd;
	int ret, i, j;
	size_t read_sve_size = 0;
	size_t expected_size;
	int errors = 0;

	if (__BYTE_ORDER == __BIG_ENDIAN) {
		ksft_test_result_skip("Big endian not supported\n");
		return;
	}

	for (i = 0; i < 32; ++i) {
		p = (unsigned char *)&write_fpsimd.vregs[i];

		for (j = 0; j < sizeof(write_fpsimd.vregs[i]); ++j)
			p[j] = j;
	}

	ret = set_fpsimd(child, &write_fpsimd);
	if (ret != 0) {
		ksft_test_result_fail("Failed to set FPSIMD state: %d\n)",
				      ret);
		return;
	}

	if (!get_sve(child, type, (void **)&read_buf, &read_sve_size)) {
		ksft_test_result_fail("Failed to read %s VL %u data\n",
				      type->name, vl);
		return;
	}
	read_sve = read_buf;

	if (read_sve->vl != vl) {
		ksft_test_result_fail("Child VL != expected VL: %u != %u\n",
				      read_sve->vl, vl);
		goto out;
	}

	/* The kernel may return either SVE or FPSIMD format */
	switch (read_sve->flags & SVE_PT_REGS_MASK) {
	case SVE_PT_REGS_FPSIMD:
		expected_size = SVE_PT_FPSIMD_SIZE(vq, SVE_PT_REGS_FPSIMD);
		if (read_sve_size < expected_size) {
			ksft_test_result_fail("Read %ld bytes, expected %ld\n",
					      read_sve_size, expected_size);
			goto out;
		}

		ret = memcmp(&write_fpsimd, read_buf + SVE_PT_FPSIMD_OFFSET,
			     sizeof(write_fpsimd));
		if (ret != 0) {
			ksft_print_msg("Read FPSIMD data mismatch\n");
			errors++;
		}
		break;

	case SVE_PT_REGS_SVE:
		expected_size = SVE_PT_SVE_SIZE(vq, SVE_PT_REGS_SVE);
		if (read_sve_size < expected_size) {
			ksft_test_result_fail("Read %ld bytes, expected %ld\n",
					      read_sve_size, expected_size);
			goto out;
		}

		for (i = 0; i < __SVE_NUM_ZREGS; i++) {
			__uint128_t tmp = 0;

			/*
			 * Z regs are stored endianness invariant, this won't
			 * work for big endian
			 */
			memcpy(&tmp, read_buf + SVE_PT_SVE_ZREG_OFFSET(vq, i),
			       sizeof(tmp));

			if (tmp != write_fpsimd.vregs[i]) {
				ksft_print_msg("Mismatch in FPSIMD for %s VL %u Z%d/V%d\n",
					       type->name, vl, i, i);
				errors++;
			}
		}

		check_u32(vl, "FPSR", &write_fpsimd.fpsr,
			  read_buf + SVE_PT_SVE_FPSR_OFFSET(vq), &errors);
		check_u32(vl, "FPCR", &write_fpsimd.fpcr,
			  read_buf + SVE_PT_SVE_FPCR_OFFSET(vq), &errors);
		break;
	default:
		ksft_print_msg("Unexpected regs type %d\n",
			       read_sve->flags & SVE_PT_REGS_MASK);
		errors++;
		break;
	}

	ksft_test_result(errors == 0, "Set FPSIMD, read via SVE for %s VL %u\n",
			 type->name, vl);

out:
	free(read_buf);
}

static int do_parent(pid_t child)
{
	int ret = EXIT_FAILURE;
	pid_t pid;
	int status, i;
	siginfo_t si;
	unsigned int vq, vl;
	bool vl_supported;

	ksft_print_msg("Parent is %d, child is %d\n", getpid(), child);

	/* Attach to the child */
	while (1) {
		int sig;

		pid = wait(&status);
		if (pid == -1) {
			perror("wait");
			goto error;
		}

		/*
		 * This should never happen but it's hard to flag in
		 * the framework.
		 */
		if (pid != child)
			continue;

		if (WIFEXITED(status) || WIFSIGNALED(status))
			ksft_exit_fail_msg("Child died unexpectedly\n");

		if (!WIFSTOPPED(status))
			goto error;

		sig = WSTOPSIG(status);

		if (ptrace(PTRACE_GETSIGINFO, pid, NULL, &si)) {
			if (errno == ESRCH)
				goto disappeared;

			if (errno == EINVAL) {
				sig = 0; /* bust group-stop */
				goto cont;
			}

			ksft_test_result_fail("PTRACE_GETSIGINFO: %s\n",
					      strerror(errno));
			goto error;
		}

		if (sig == SIGSTOP && si.si_code == SI_TKILL &&
		    si.si_pid == pid)
			break;

	cont:
		if (ptrace(PTRACE_CONT, pid, NULL, sig)) {
			if (errno == ESRCH)
				goto disappeared;

			ksft_test_result_fail("PTRACE_CONT: %s\n",
					      strerror(errno));
			goto error;
		}
	}

	for (i = 0; i < ARRAY_SIZE(vec_types); i++) {
		/* FPSIMD via SVE regset */
		if (getauxval(vec_types[i].hwcap_type) & vec_types[i].hwcap) {
			ptrace_sve_fpsimd(child, &vec_types[i]);
		} else {
			ksft_test_result_skip("%s FPSIMD set via SVE\n",
					      vec_types[i].name);
			ksft_test_result_skip("%s FPSIMD read\n",
					      vec_types[i].name);
		}

		/* prctl() flags */
		if (getauxval(vec_types[i].hwcap_type) & vec_types[i].hwcap) {
			ptrace_set_get_inherit(child, &vec_types[i]);
		} else {
			ksft_test_result_skip("%s SVE_PT_VL_INHERIT set\n",
					      vec_types[i].name);
			ksft_test_result_skip("%s SVE_PT_VL_INHERIT cleared\n",
					      vec_types[i].name);
		}

		/* Step through every possible VQ */
		for (vq = SVE_VQ_MIN; vq <= TEST_VQ_MAX; vq++) {
			vl = sve_vl_from_vq(vq);

			/* First, try to set this vector length */
			if (getauxval(vec_types[i].hwcap_type) &
			    vec_types[i].hwcap) {
				ptrace_set_get_vl(child, &vec_types[i], vl,
						  &vl_supported);
			} else {
				ksft_test_result_skip("%s get/set VL %d\n",
						      vec_types[i].name, vl);
				vl_supported = false;
			}

			/* If the VL is supported validate data set/get */
			if (vl_supported) {
				ptrace_set_sve_get_sve_data(child, &vec_types[i], vl);
				ptrace_set_sve_get_fpsimd_data(child, &vec_types[i], vl);
				ptrace_set_fpsimd_get_sve_data(child, &vec_types[i], vl);
			} else {
				ksft_test_result_skip("%s set SVE get SVE for VL %d\n",
						      vec_types[i].name, vl);
				ksft_test_result_skip("%s set SVE get FPSIMD for VL %d\n",
						      vec_types[i].name, vl);
				ksft_test_result_skip("%s set FPSIMD get SVE for VL %d\n",
						      vec_types[i].name, vl);
			}
		}
	}

	ret = EXIT_SUCCESS;

error:
	kill(child, SIGKILL);

disappeared:
	return ret;
}

int main(void)
{
	int ret = EXIT_SUCCESS;
	pid_t child;

	srandom(getpid());

	ksft_print_header();
	ksft_set_plan(EXPECTED_TESTS);

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		ksft_exit_skip("SVE not available\n");

	child = fork();
	if (!child)
		return do_child();

	if (do_parent(child))
		ret = EXIT_FAILURE;

	ksft_print_cnts();

	return ret;
}
