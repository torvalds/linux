// SPDX-License-Identifier: GPL-2.0-only
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/compiler.h>

#include "debug.h"
#include "tests.h"

#ifdef HAVE_JITDUMP
#include <libelf.h>
#include "../util/genelf.h"
#endif

#define TEMPL "/tmp/perf-test-XXXXXX"

static int test__jit_write_elf(struct test_suite *test __maybe_unused,
			       int subtest __maybe_unused)
{
#ifdef HAVE_JITDUMP
	static unsigned char x86_code[] = {
		0xBB, 0x2A, 0x00, 0x00, 0x00, /* movl $42, %ebx */
		0xB8, 0x01, 0x00, 0x00, 0x00, /* movl $1, %eax */
		0xCD, 0x80            /* int $0x80 */
	};
	char path[PATH_MAX];
	int fd, ret;

	strcpy(path, TEMPL);

	fd = mkstemp(path);
	if (fd < 0) {
		perror("mkstemp failed");
		return TEST_FAIL;
	}

	pr_info("Writing jit code to: %s\n", path);

	ret = jit_write_elf(fd, 0, "main", x86_code, sizeof(x86_code),
			NULL, 0, NULL, 0, 0);
	close(fd);

	unlink(path);

	return ret ? TEST_FAIL : 0;
#else
	return TEST_SKIP;
#endif
}

DEFINE_SUITE("Test jit_write_elf", jit_write_elf);
