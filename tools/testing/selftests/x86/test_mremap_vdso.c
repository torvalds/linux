// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32-bit test to check vDSO mremap.
 *
 * Copyright (c) 2016 Dmitry Safonov
 * Suggested-by: Andrew Lutomirski
 */
/*
 * Can be built statically:
 * gcc -Os -Wall -static -m32 test_mremap_vdso.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>

#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include "../kselftest.h"

#define PAGE_SIZE	4096

static int try_to_remap(void *vdso_addr, unsigned long size)
{
	void *dest_addr, *new_addr;

	/* Searching for memory location where to remap */
	dest_addr = mmap(0, size, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
	if (dest_addr == MAP_FAILED) {
		ksft_print_msg("WARN: mmap failed (%d): %m\n", errno);
		return 0;
	}

	ksft_print_msg("Moving vDSO: [%p, %#lx] -> [%p, %#lx]\n",
		       vdso_addr, (unsigned long)vdso_addr + size,
		       dest_addr, (unsigned long)dest_addr + size);
	fflush(stdout);

	new_addr = mremap(vdso_addr, size, size,
			MREMAP_FIXED|MREMAP_MAYMOVE, dest_addr);
	if ((unsigned long)new_addr == (unsigned long)-1) {
		munmap(dest_addr, size);
		if (errno == EINVAL) {
			ksft_print_msg("vDSO partial move failed, will try with bigger size\n");
			return -1; /* Retry with larger */
		}
		ksft_print_msg("[FAIL]\tmremap failed (%d): %m\n", errno);
		return 1;
	}

	return 0;

}

#define VDSO_NAME "[vdso]"
#define VMFLAGS "VmFlags:"
#define MSEAL_FLAGS "sl"
#define MAX_LINE_LEN 512

bool vdso_sealed(FILE *maps)
{
	char line[MAX_LINE_LEN];
	bool has_vdso = false;

	while (fgets(line, sizeof(line), maps)) {
		if (strstr(line, VDSO_NAME))
			has_vdso = true;

		if (has_vdso && !strncmp(line, VMFLAGS, strlen(VMFLAGS))) {
			if (strstr(line, MSEAL_FLAGS))
				return true;

			return false;
		}
	}

	return false;
}

int main(int argc, char **argv, char **envp)
{
	pid_t child;
	FILE *maps;

	ksft_print_header();
	ksft_set_plan(1);

	maps = fopen("/proc/self/smaps", "r");
	if (!maps) {
		ksft_test_result_skip(
			"Could not open /proc/self/smaps, errno=%d\n",
			 errno);

		return 0;
	}

	if (vdso_sealed(maps)) {
		ksft_test_result_skip("vdso is sealed\n");
		return 0;
	}

	fclose(maps);

	child = fork();
	if (child == -1)
		ksft_exit_fail_msg("failed to fork (%d): %m\n", errno);

	if (child == 0) {
		unsigned long vdso_size = PAGE_SIZE;
		unsigned long auxval;
		int ret = -1;

		auxval = getauxval(AT_SYSINFO_EHDR);
		ksft_print_msg("AT_SYSINFO_EHDR is %#lx\n", auxval);
		if (!auxval || auxval == -ENOENT) {
			ksft_print_msg("WARN: getauxval failed\n");
			return 0;
		}

		/* Simpler than parsing ELF header */
		while (ret < 0) {
			ret = try_to_remap((void *)auxval, vdso_size);
			vdso_size += PAGE_SIZE;
		}

#ifdef __i386__
		/* Glibc is likely to explode now - exit with raw syscall */
		asm volatile ("int $0x80" : : "a" (__NR_exit), "b" (!!ret));
#else /* __x86_64__ */
		syscall(SYS_exit, ret);
#endif
	} else {
		int status;

		if (waitpid(child, &status, 0) != child ||
			!WIFEXITED(status))
			ksft_test_result_fail("mremap() of the vDSO does not work on this kernel!\n");
		else if (WEXITSTATUS(status) != 0)
			ksft_test_result_fail("Child failed with %d\n", WEXITSTATUS(status));
		else
			ksft_test_result_pass("%s\n", __func__);
	}

	ksft_finished();
}
