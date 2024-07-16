// SPDX-License-Identifier: GPL-2.0
/*
 * Seccomp filter example for x86 (32-bit and 64-bit) with BPF macros
 *
 * Copyright (c) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 * Author: Will Drewry <wad@chromium.org>
 *
 * The code may be used by anyone for any purpose,
 * and can serve as a starting point for developing
 * applications using prctl(PR_SET_SECCOMP, 2, ...).
 */
#if defined(__i386__) || defined(__x86_64__)
#define SUPPORTED_ARCH 1
#endif

#if defined(SUPPORTED_ARCH)
#define __USE_GNU 1
#define _GNU_SOURCE 1

#include <linux/types.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

#define syscall_arg(_n) (offsetof(struct seccomp_data, args[_n]))
#define syscall_nr (offsetof(struct seccomp_data, nr))

#if defined(__i386__)
#define REG_RESULT	REG_EAX
#define REG_SYSCALL	REG_EAX
#define REG_ARG0	REG_EBX
#define REG_ARG1	REG_ECX
#define REG_ARG2	REG_EDX
#define REG_ARG3	REG_ESI
#define REG_ARG4	REG_EDI
#define REG_ARG5	REG_EBP
#elif defined(__x86_64__)
#define REG_RESULT	REG_RAX
#define REG_SYSCALL	REG_RAX
#define REG_ARG0	REG_RDI
#define REG_ARG1	REG_RSI
#define REG_ARG2	REG_RDX
#define REG_ARG3	REG_R10
#define REG_ARG4	REG_R8
#define REG_ARG5	REG_R9
#endif

#ifndef PR_SET_NO_NEW_PRIVS
#define PR_SET_NO_NEW_PRIVS 38
#endif

#ifndef SYS_SECCOMP
#define SYS_SECCOMP 1
#endif

static void emulator(int nr, siginfo_t *info, void *void_context)
{
	ucontext_t *ctx = (ucontext_t *)(void_context);
	int syscall;
	char *buf;
	ssize_t bytes;
	size_t len;
	if (info->si_code != SYS_SECCOMP)
		return;
	if (!ctx)
		return;
	syscall = ctx->uc_mcontext.gregs[REG_SYSCALL];
	buf = (char *) ctx->uc_mcontext.gregs[REG_ARG1];
	len = (size_t) ctx->uc_mcontext.gregs[REG_ARG2];

	if (syscall != __NR_write)
		return;
	if (ctx->uc_mcontext.gregs[REG_ARG0] != STDERR_FILENO)
		return;
	/* Redirect stderr messages to stdout. Doesn't handle EINTR, etc */
	ctx->uc_mcontext.gregs[REG_RESULT] = -1;
	if (write(STDOUT_FILENO, "[ERR] ", 6) > 0) {
		bytes = write(STDOUT_FILENO, buf, len);
		ctx->uc_mcontext.gregs[REG_RESULT] = bytes;
	}
	return;
}

static int install_emulator(void)
{
	struct sigaction act;
	sigset_t mask;
	memset(&act, 0, sizeof(act));
	sigemptyset(&mask);
	sigaddset(&mask, SIGSYS);

	act.sa_sigaction = &emulator;
	act.sa_flags = SA_SIGINFO;
	if (sigaction(SIGSYS, &act, NULL) < 0) {
		perror("sigaction");
		return -1;
	}
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL)) {
		perror("sigprocmask");
		return -1;
	}
	return 0;
}

static int install_filter(void)
{
	struct sock_filter filter[] = {
		/* Grab the system call number */
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, syscall_nr),
		/* Jump table for the allowed syscalls */
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_rt_sigreturn, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
#ifdef __NR_sigreturn
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_sigreturn, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
#endif
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_exit_group, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_exit, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_read, 1, 0),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, __NR_write, 3, 2),

		/* Check that read is only using stdin. */
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, syscall_arg(0)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, STDIN_FILENO, 4, 0),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL),

		/* Check that write is only using stdout */
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS, syscall_arg(0)),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, STDOUT_FILENO, 1, 0),
		/* Trap attempts to write to stderr */
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, STDERR_FILENO, 1, 2),

		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_TRAP),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_KILL),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
		.filter = filter,
	};

	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		perror("prctl(NO_NEW_PRIVS)");
		return 1;
	}


	if (prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog)) {
		perror("prctl");
		return 1;
	}
	return 0;
}

#define payload(_c) (_c), sizeof((_c))
int main(int argc, char **argv)
{
	char buf[4096];
	ssize_t bytes = 0;
	if (install_emulator())
		return 1;
	if (install_filter())
		return 1;
	syscall(__NR_write, STDOUT_FILENO,
		payload("OHAI! WHAT IS YOUR NAME? "));
	bytes = syscall(__NR_read, STDIN_FILENO, buf, sizeof(buf));
	syscall(__NR_write, STDOUT_FILENO, payload("HELLO, "));
	syscall(__NR_write, STDOUT_FILENO, buf, bytes);
	syscall(__NR_write, STDERR_FILENO,
		payload("Error message going to STDERR\n"));
	return 0;
}
#else	/* SUPPORTED_ARCH */
/*
 * This sample is x86-only.  Since kernel samples are compiled with the
 * host toolchain, a non-x86 host will result in using only the main()
 * below.
 */
int main(void)
{
	return 1;
}
#endif	/* SUPPORTED_ARCH */
