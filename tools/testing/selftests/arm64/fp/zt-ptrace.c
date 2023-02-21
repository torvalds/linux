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
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <asm/sigcontext.h>
#include <asm/ptrace.h>

#include "../../kselftest.h"

/* <linux/elf.h> and <sys/auxv.h> don't like each other, so: */
#ifndef NT_ARM_ZA
#define NT_ARM_ZA 0x40c
#endif
#ifndef NT_ARM_ZT
#define NT_ARM_ZT 0x40d
#endif

#define EXPECTED_TESTS 3

static int sme_vl;

static void fill_buf(char *buf, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		buf[i] = random();
}

static int do_child(void)
{
	if (ptrace(PTRACE_TRACEME, -1, NULL, NULL))
		ksft_exit_fail_msg("PTRACE_TRACEME", strerror(errno));

	if (raise(SIGSTOP))
		ksft_exit_fail_msg("raise(SIGSTOP)", strerror(errno));

	return EXIT_SUCCESS;
}

static struct user_za_header *get_za(pid_t pid, void **buf, size_t *size)
{
	struct user_za_header *za;
	void *p;
	size_t sz = sizeof(*za);
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
		if (ptrace(PTRACE_GETREGSET, pid, NT_ARM_ZA, &iov))
			goto error;

		za = *buf;
		if (za->size <= sz)
			break;

		sz = za->size;
	}

	return za;

error:
	return NULL;
}

static int set_za(pid_t pid, const struct user_za_header *za)
{
	struct iovec iov;

	iov.iov_base = (void *)za;
	iov.iov_len = za->size;
	return ptrace(PTRACE_SETREGSET, pid, NT_ARM_ZA, &iov);
}

static int get_zt(pid_t pid, char zt[ZT_SIG_REG_BYTES])
{
	struct iovec iov;

	iov.iov_base = zt;
	iov.iov_len = ZT_SIG_REG_BYTES;
	return ptrace(PTRACE_GETREGSET, pid, NT_ARM_ZT, &iov);
}


static int set_zt(pid_t pid, const char zt[ZT_SIG_REG_BYTES])
{
	struct iovec iov;

	iov.iov_base = (void *)zt;
	iov.iov_len = ZT_SIG_REG_BYTES;
	return ptrace(PTRACE_SETREGSET, pid, NT_ARM_ZT, &iov);
}

/* Reading with ZA disabled returns all zeros */
static void ptrace_za_disabled_read_zt(pid_t child)
{
	struct user_za_header za;
	char zt[ZT_SIG_REG_BYTES];
	int ret, i;
	bool fail = false;

	/* Disable PSTATE.ZA using the ZA interface */
	memset(&za, 0, sizeof(za));
	za.vl = sme_vl;
	za.size = sizeof(za);

	ret = set_za(child, &za);
	if (ret != 0) {
		ksft_print_msg("Failed to disable ZA\n");
		fail = true;
	}

	/* Read back ZT */
	ret = get_zt(child, zt);
	if (ret != 0) {
		ksft_print_msg("Failed to read ZT\n");
		fail = true;
	}

	for (i = 0; i < ARRAY_SIZE(zt); i++) {
		if (zt[i]) {
			ksft_print_msg("zt[%d]: 0x%x != 0\n", i, zt[i]);
			fail = true;
		}
	}

	ksft_test_result(!fail, "ptrace_za_disabled_read_zt\n");
}

/* Writing then reading ZT should return the data written */
static void ptrace_set_get_zt(pid_t child)
{
	char zt_in[ZT_SIG_REG_BYTES];
	char zt_out[ZT_SIG_REG_BYTES];
	int ret, i;
	bool fail = false;

	fill_buf(zt_in, sizeof(zt_in));

	ret = set_zt(child, zt_in);
	if (ret != 0) {
		ksft_print_msg("Failed to set ZT\n");
		fail = true;
	}

	ret = get_zt(child, zt_out);
	if (ret != 0) {
		ksft_print_msg("Failed to read ZT\n");
		fail = true;
	}

	for (i = 0; i < ARRAY_SIZE(zt_in); i++) {
		if (zt_in[i] != zt_out[i]) {
			ksft_print_msg("zt[%d]: 0x%x != 0x%x\n", i, 
				       zt_in[i], zt_out[i]);
			fail = true;
		}
	}

	ksft_test_result(!fail, "ptrace_set_get_zt\n");
}

/* Writing ZT should set PSTATE.ZA */
static void ptrace_enable_za_via_zt(pid_t child)
{
	struct user_za_header za_in;
	struct user_za_header *za_out;
	char zt[ZT_SIG_REG_BYTES];
	char *za_data;
	size_t za_out_size;
	int ret, i, vq;
	bool fail = false;

	/* Disable PSTATE.ZA using the ZA interface */
	memset(&za_in, 0, sizeof(za_in));
	za_in.vl = sme_vl;
	za_in.size = sizeof(za_in);

	ret = set_za(child, &za_in);
	if (ret != 0) {
		ksft_print_msg("Failed to disable ZA\n");
		fail = true;
	}

	/* Write ZT */
	fill_buf(zt, sizeof(zt));
	ret = set_zt(child, zt);
	if (ret != 0) {
		ksft_print_msg("Failed to set ZT\n");
		fail = true;
	}

	/* Read back ZA and check for register data */
	za_out = NULL;
	za_out_size = 0;
	if (get_za(child, (void **)&za_out, &za_out_size)) {
		/* Should have an unchanged VL */
		if (za_out->vl != sme_vl) {
			ksft_print_msg("VL changed from %d to %d\n",
				       sme_vl, za_out->vl);
			fail = true;
		}
		vq = __sve_vq_from_vl(za_out->vl);
		za_data = (char *)za_out + ZA_PT_ZA_OFFSET;

		/* Should have register data */
		if (za_out->size < ZA_PT_SIZE(vq)) {
			ksft_print_msg("ZA data less than expected: %u < %u\n",
				       za_out->size, ZA_PT_SIZE(vq));
			fail = true;
			vq = 0;
		}

		/* That register data should be non-zero */
		for (i = 0; i < ZA_PT_ZA_SIZE(vq); i++) {
			if (za_data[i]) {
				ksft_print_msg("ZA byte %d is %x\n",
					       i, za_data[i]);
				fail = true;
			}
		}
	} else {
		ksft_print_msg("Failed to read ZA\n");
		fail = true;
	}

	ksft_test_result(!fail, "ptrace_enable_za_via_zt\n");
}

static int do_parent(pid_t child)
{
	int ret = EXIT_FAILURE;
	pid_t pid;
	int status;
	siginfo_t si;

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

	ksft_print_msg("Parent is %d, child is %d\n", getpid(), child);

	ptrace_za_disabled_read_zt(child);
	ptrace_set_get_zt(child);
	ptrace_enable_za_via_zt(child);

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

	if (!(getauxval(AT_HWCAP2) & HWCAP2_SME2)) {
		ksft_set_plan(1);
		ksft_exit_skip("SME2 not available\n");
	}

	/* We need a valid SME VL to enable/disable ZA */
	sme_vl = prctl(PR_SME_GET_VL);
	if (sme_vl == -1) {
		ksft_set_plan(1);
		ksft_exit_skip("Failed to read SME VL: %d (%s)\n",
			       errno, strerror(errno));
	}

	ksft_set_plan(EXPECTED_TESTS);

	child = fork();
	if (!child)
		return do_child();

	if (do_parent(child))
		ret = EXIT_FAILURE;

	ksft_print_cnts();

	return ret;
}
