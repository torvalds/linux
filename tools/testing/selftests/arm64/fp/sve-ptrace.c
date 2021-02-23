// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015-2020 ARM Limited.
 * Original author: Dave Martin <Dave.Martin@arm.com>
 */
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/auxv.h>
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

/* Number of registers filled in by sve_store_patterns */
#define NR_VREGS 5

void sve_store_patterns(__uint128_t v[NR_VREGS]);

static void dump(const void *buf, size_t size)
{
	size_t i;
	const unsigned char *p = buf;

	for (i = 0; i < size; ++i)
		printf(" %.2x", *p++);
}

static int check_vregs(const __uint128_t vregs[NR_VREGS])
{
	int i;
	int ok = 1;

	for (i = 0; i < NR_VREGS; ++i) {
		printf("# v[%d]:", i);
		dump(&vregs[i], sizeof vregs[i]);
		putchar('\n');

		if (vregs[i] != vregs[0])
			ok = 0;
	}

	return ok;
}

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
		ksft_exit_fail_msg("PTRACE_TRACEME", strerror(errno));

	if (raise(SIGSTOP))
		ksft_exit_fail_msg("raise(SIGSTOP)", strerror(errno));

	return EXIT_SUCCESS;
}

static struct user_sve_header *get_sve(pid_t pid, void **buf, size_t *size)
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
		if (ptrace(PTRACE_GETREGSET, pid, NT_ARM_SVE, &iov))
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

static int set_sve(pid_t pid, const struct user_sve_header *sve)
{
	struct iovec iov;

	iov.iov_base = (void *)sve;
	iov.iov_len = sve->size;
	return ptrace(PTRACE_SETREGSET, pid, NT_ARM_SVE, &iov);
}

static void dump_sve_regs(const struct user_sve_header *sve, unsigned int num,
			  unsigned int vlmax)
{
	unsigned int vq;
	unsigned int i;

	if ((sve->flags & SVE_PT_REGS_MASK) != SVE_PT_REGS_SVE)
		ksft_exit_fail_msg("Dumping non-SVE register\n");

	if (vlmax > sve->vl)
		vlmax = sve->vl;

	vq = sve_vq_from_vl(sve->vl);
	for (i = 0; i < num; ++i) {
		printf("# z%u:", i);
		dump((const char *)sve + SVE_PT_SVE_ZREG_OFFSET(vq, i),
		     vlmax);
		printf("%s\n", vlmax == sve->vl ? "" : " ...");
	}
}

static int do_parent(pid_t child)
{
	int ret = EXIT_FAILURE;
	pid_t pid;
	int status;
	siginfo_t si;
	void *svebuf = NULL, *newsvebuf;
	size_t svebufsz = 0, newsvebufsz;
	struct user_sve_header *sve, *new_sve;
	struct user_fpsimd_state *fpsimd;
	unsigned int i, j;
	unsigned char *p;
	unsigned int vq;

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

		ksft_test_result(WIFSTOPPED(status), "WIFSTOPPED(%d)\n",
				 status);
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

	sve = get_sve(pid, &svebuf, &svebufsz);
	if (!sve) {
		int e = errno;

		ksft_test_result_fail("get_sve: %s\n", strerror(errno));
		if (e == ESRCH)
			goto disappeared;

		goto error;
	} else {
		ksft_test_result_pass("get_sve\n");
	}

	ksft_test_result((sve->flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_FPSIMD,
			 "FPSIMD registers\n");
	if ((sve->flags & SVE_PT_REGS_MASK) != SVE_PT_REGS_FPSIMD)
		goto error;

	fpsimd = (struct user_fpsimd_state *)((char *)sve +
					      SVE_PT_FPSIMD_OFFSET);
	for (i = 0; i < 32; ++i) {
		p = (unsigned char *)&fpsimd->vregs[i];

		for (j = 0; j < sizeof fpsimd->vregs[i]; ++j)
			p[j] = j;
	}

	if (set_sve(pid, sve)) {
		int e = errno;

		ksft_test_result_fail("set_sve(FPSIMD): %s\n",
				      strerror(errno));
		if (e == ESRCH)
			goto disappeared;

		goto error;
	}

	vq = sve_vq_from_vl(sve->vl);

	newsvebufsz = SVE_PT_SVE_ZREG_OFFSET(vq, 1);
	new_sve = newsvebuf = malloc(newsvebufsz);
	if (!new_sve) {
		errno = ENOMEM;
		perror(NULL);
		goto error;
	}

	*new_sve = *sve;
	new_sve->flags &= ~SVE_PT_REGS_MASK;
	new_sve->flags |= SVE_PT_REGS_SVE;
	memset((char *)new_sve + SVE_PT_SVE_ZREG_OFFSET(vq, 0),
	       0, SVE_PT_SVE_ZREG_SIZE(vq));
	new_sve->size = SVE_PT_SVE_ZREG_OFFSET(vq, 1);
	if (set_sve(pid, new_sve)) {
		int e = errno;

		ksft_test_result_fail("set_sve(ZREG): %s\n", strerror(errno));
		if (e == ESRCH)
			goto disappeared;

		goto error;
	}

	new_sve = get_sve(pid, &newsvebuf, &newsvebufsz);
	if (!new_sve) {
		int e = errno;

		ksft_test_result_fail("get_sve(ZREG): %s\n", strerror(errno));
		if (e == ESRCH)
			goto disappeared;

		goto error;
	}

	ksft_test_result((new_sve->flags & SVE_PT_REGS_MASK) == SVE_PT_REGS_SVE,
			 "SVE registers\n");
	if ((new_sve->flags & SVE_PT_REGS_MASK) != SVE_PT_REGS_SVE)
		goto error;

	dump_sve_regs(new_sve, 3, sizeof fpsimd->vregs[0]);

	p = (unsigned char *)new_sve + SVE_PT_SVE_ZREG_OFFSET(vq, 1);
	for (i = 0; i < sizeof fpsimd->vregs[0]; ++i) {
		unsigned char expected = i;

		if (__BYTE_ORDER == __BIG_ENDIAN)
			expected = sizeof fpsimd->vregs[0] - 1 - expected;

		ksft_test_result(p[i] == expected, "p[%d] == expected\n", i);
		if (p[i] != expected)
			goto error;
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
	__uint128_t v[NR_VREGS];
	pid_t child;

	ksft_print_header();
	ksft_set_plan(20);

	if (!(getauxval(AT_HWCAP) & HWCAP_SVE))
		ksft_exit_skip("SVE not available\n");

	sve_store_patterns(v);

	if (!check_vregs(v))
		ksft_exit_fail_msg("Initial check_vregs() failed\n");

	child = fork();
	if (!child)
		return do_child();

	if (do_parent(child))
		ret = EXIT_FAILURE;

	ksft_print_cnts();

	return 0;
}
