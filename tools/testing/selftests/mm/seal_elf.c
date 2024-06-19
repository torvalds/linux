// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "../kselftest.h"
#include <syscall.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <sys/stat.h>

/*
 * need those definition for manually build using gcc.
 * gcc -I ../../../../usr/include   -DDEBUG -O3  -DDEBUG -O3 seal_elf.c -o seal_elf
 */
#define FAIL_TEST_IF_FALSE(c) do {\
		if (!(c)) {\
			ksft_test_result_fail("%s, line:%d\n", __func__, __LINE__);\
			goto test_end;\
		} \
	} \
	while (0)

#define SKIP_TEST_IF_FALSE(c) do {\
		if (!(c)) {\
			ksft_test_result_skip("%s, line:%d\n", __func__, __LINE__);\
			goto test_end;\
		} \
	} \
	while (0)


#define TEST_END_CHECK() {\
		ksft_test_result_pass("%s\n", __func__);\
		return;\
test_end:\
		return;\
}

#ifndef u64
#define u64 unsigned long long
#endif

/*
 * define sys_xyx to call syscall directly.
 */
static int sys_mseal(void *start, size_t len)
{
	int sret;

	errno = 0;
	sret = syscall(__NR_mseal, start, len, 0);
	return sret;
}

static void *sys_mmap(void *addr, unsigned long len, unsigned long prot,
	unsigned long flags, unsigned long fd, unsigned long offset)
{
	void *sret;

	errno = 0;
	sret = (void *) syscall(__NR_mmap, addr, len, prot,
		flags, fd, offset);
	return sret;
}

static inline int sys_mprotect(void *ptr, size_t size, unsigned long prot)
{
	int sret;

	errno = 0;
	sret = syscall(__NR_mprotect, ptr, size, prot);
	return sret;
}

static bool seal_support(void)
{
	int ret;
	void *ptr;
	unsigned long page_size = getpagesize();

	ptr = sys_mmap(NULL, page_size, PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (ptr == (void *) -1)
		return false;

	ret = sys_mseal(ptr, page_size);
	if (ret < 0)
		return false;

	return true;
}

const char somestr[4096] = {"READONLY"};

static void test_seal_elf(void)
{
	int ret;
	FILE *maps;
	char line[512];
	uintptr_t  addr_start, addr_end;
	char prot[5];
	char filename[256];
	unsigned long page_size = getpagesize();
	unsigned long long ptr = (unsigned long long) somestr;
	char *somestr2 = (char *)somestr;

	/*
	 * Modify the protection of readonly somestr
	 */
	if (((unsigned long long)ptr % page_size) != 0)
		ptr = (unsigned long long)ptr & ~(page_size - 1);

	ksft_print_msg("somestr = %s\n", somestr);
	ksft_print_msg("change protection to rw\n");
	ret = sys_mprotect((void *)ptr, page_size, PROT_READ|PROT_WRITE);
	FAIL_TEST_IF_FALSE(!ret);
	*somestr2 = 'A';
	ksft_print_msg("somestr is modified to: %s\n", somestr);
	ret = sys_mprotect((void *)ptr, page_size, PROT_READ);
	FAIL_TEST_IF_FALSE(!ret);

	maps = fopen("/proc/self/maps", "r");
	FAIL_TEST_IF_FALSE(maps);

	/*
	 * apply sealing to elf binary
	 */
	while (fgets(line, sizeof(line), maps)) {
		if (sscanf(line, "%lx-%lx %4s %*x %*x:%*x %*u %255[^\n]",
			&addr_start, &addr_end, prot, filename) == 4) {
			if (strlen(filename)) {
				/*
				 * seal the mapping if read only.
				 */
				if (strstr(prot, "r-")) {
					ret = sys_mseal((void *)addr_start, addr_end - addr_start);
					FAIL_TEST_IF_FALSE(!ret);
					ksft_print_msg("sealed: %lx-%lx %s %s\n",
						addr_start, addr_end, prot, filename);
					if ((uintptr_t) somestr >= addr_start &&
						(uintptr_t) somestr <= addr_end)
						ksft_print_msg("mapping for somestr found\n");
				}
			}
		}
	}
	fclose(maps);

	ret = sys_mprotect((void *)ptr, page_size, PROT_READ | PROT_WRITE);
	FAIL_TEST_IF_FALSE(ret < 0);
	ksft_print_msg("somestr is sealed, mprotect is rejected\n");

	TEST_END_CHECK();
}

int main(int argc, char **argv)
{
	bool test_seal = seal_support();

	ksft_print_header();
	ksft_print_msg("pid=%d\n", getpid());

	if (!test_seal)
		ksft_exit_skip("sealing not supported, check CONFIG_64BIT\n");

	ksft_set_plan(1);

	test_seal_elf();

	ksft_finished();
}
