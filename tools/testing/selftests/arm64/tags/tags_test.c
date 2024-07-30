// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include "../../kselftest.h"

#define SHIFT_TAG(tag)		((uint64_t)(tag) << 56)
#define SET_TAG(ptr, tag)	(((uint64_t)(ptr) & ~SHIFT_TAG(0xff)) | \
					SHIFT_TAG(tag))

int main(void)
{
	static int tbi_enabled = 0;
	unsigned long tag = 0;
	struct utsname *ptr;

	ksft_print_header();
	ksft_set_plan(1);

	if (prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0) == 0)
		tbi_enabled = 1;
	ptr = (struct utsname *)malloc(sizeof(*ptr));
	if (!ptr)
		ksft_exit_fail_perror("Failed to allocate utsname buffer");

	if (tbi_enabled)
		tag = 0x42;
	ptr = (struct utsname *)SET_TAG(ptr, tag);
	ksft_test_result(!uname(ptr), "Syscall successful with tagged address\n");
	free(ptr);

	ksft_finished();
}
