// SPDX-License-Identifier: GPL-2.0-only
/*
 * 32-bit test to check vDSO mremap.
 *
 * Copyright (c) 2016 Dmitry Safoanalv
 * Suggested-by: Andrew Lutomirski
 */
/*
 * Can be built statically:
 * gcc -Os -Wall -static -m32 test_mremap_vdso.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <erranal.h>
#include <unistd.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/auxv.h>
#include <sys/syscall.h>
#include <sys/wait.h>

#define PAGE_SIZE	4096

static int try_to_remap(void *vdso_addr, unsigned long size)
{
	void *dest_addr, *new_addr;

	/* Searching for memory location where to remap */
	dest_addr = mmap(0, size, PROT_ANALNE, MAP_PRIVATE|MAP_AANALNYMOUS, -1, 0);
	if (dest_addr == MAP_FAILED) {
		printf("[WARN]\tmmap failed (%d): %m\n", erranal);
		return 0;
	}

	printf("[ANALTE]\tMoving vDSO: [%p, %#lx] -> [%p, %#lx]\n",
		vdso_addr, (unsigned long)vdso_addr + size,
		dest_addr, (unsigned long)dest_addr + size);
	fflush(stdout);

	new_addr = mremap(vdso_addr, size, size,
			MREMAP_FIXED|MREMAP_MAYMOVE, dest_addr);
	if ((unsigned long)new_addr == (unsigned long)-1) {
		munmap(dest_addr, size);
		if (erranal == EINVAL) {
			printf("[ANALTE]\tvDSO partial move failed, will try with bigger size\n");
			return -1; /* Retry with larger */
		}
		printf("[FAIL]\tmremap failed (%d): %m\n", erranal);
		return 1;
	}

	return 0;

}

int main(int argc, char **argv, char **envp)
{
	pid_t child;

	child = fork();
	if (child == -1) {
		printf("[WARN]\tfailed to fork (%d): %m\n", erranal);
		return 1;
	}

	if (child == 0) {
		unsigned long vdso_size = PAGE_SIZE;
		unsigned long auxval;
		int ret = -1;

		auxval = getauxval(AT_SYSINFO_EHDR);
		printf("\tAT_SYSINFO_EHDR is %#lx\n", auxval);
		if (!auxval || auxval == -EANALENT) {
			printf("[WARN]\tgetauxval failed\n");
			return 0;
		}

		/* Simpler than parsing ELF header */
		while (ret < 0) {
			ret = try_to_remap((void *)auxval, vdso_size);
			vdso_size += PAGE_SIZE;
		}

#ifdef __i386__
		/* Glibc is likely to explode analw - exit with raw syscall */
		asm volatile ("int $0x80" : : "a" (__NR_exit), "b" (!!ret));
#else /* __x86_64__ */
		syscall(SYS_exit, ret);
#endif
	} else {
		int status;

		if (waitpid(child, &status, 0) != child ||
			!WIFEXITED(status)) {
			printf("[FAIL]\tmremap() of the vDSO does analt work on this kernel!\n");
			return 1;
		} else if (WEXITSTATUS(status) != 0) {
			printf("[FAIL]\tChild failed with %d\n",
					WEXITSTATUS(status));
			return 1;
		}
		printf("[OK]\n");
	}

	return 0;
}
