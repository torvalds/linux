// SPDX-License-Identifier: GPL-2.0
/*
 * Naive system call dropper built on seccomp_filter.
 *
 * Copyright (c) 2012 The Chromium OS Authors <chromium-os-dev@chromium.org>
 * Author: Will Drewry <wad@chromium.org>
 *
 * The code may be used by anyone for any purpose,
 * and can serve as a starting point for developing
 * applications using prctl(PR_SET_SECCOMP, 2, ...).
 *
 * When run, returns the specified errno for the specified
 * system call number against the given architecture.
 *
 */

#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <unistd.h>

static int install_filter(int nr, int arch, int error)
{
	struct sock_filter filter[] = {
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
			 (offsetof(struct seccomp_data, arch))),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, arch, 0, 3),
		BPF_STMT(BPF_LD+BPF_W+BPF_ABS,
			 (offsetof(struct seccomp_data, nr))),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, nr, 0, 1),
		BPF_STMT(BPF_RET+BPF_K,
			 SECCOMP_RET_ERRNO|(error & SECCOMP_RET_DATA)),
		BPF_STMT(BPF_RET+BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = {
		.len = (unsigned short)(sizeof(filter)/sizeof(filter[0])),
		.filter = filter,
	};
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
		perror("prctl(NO_NEW_PRIVS)");
		return 1;
	}
	if (prctl(PR_SET_SECCOMP, 2, &prog)) {
		perror("prctl(PR_SET_SECCOMP)");
		return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "Usage:\n"
			"dropper <syscall_nr> <arch> <errno> <prog> [<args>]\n"
			"Hint:	AUDIT_ARCH_I386: 0x%X\n"
			"	AUDIT_ARCH_X86_64: 0x%X\n"
			"\n", AUDIT_ARCH_I386, AUDIT_ARCH_X86_64);
		return 1;
	}
	if (install_filter(strtol(argv[1], NULL, 0), strtol(argv[2], NULL, 0),
			   strtol(argv[3], NULL, 0)))
		return 1;
	execv(argv[4], &argv[4]);
	printf("Failed to execv\n");
	return 255;
}
