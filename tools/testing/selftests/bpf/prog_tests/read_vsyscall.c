// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2024. Huawei Technologies Co., Ltd */
#include "test_progs.h"
#include "read_vsyscall.skel.h"

#if defined(__x86_64__)
/* For VSYSCALL_ADDR */
#include <asm/vsyscall.h>
#else
/* To prevent build failure on non-x86 arch */
#define VSYSCALL_ADDR 0UL
#endif

struct read_ret_desc {
	const char *name;
	int ret;
} all_read[] = {
	{ .name = "probe_read_kernel", .ret = -ERANGE },
	{ .name = "probe_read_kernel_str", .ret = -ERANGE },
	{ .name = "probe_read", .ret = -ERANGE },
	{ .name = "probe_read_str", .ret = -ERANGE },
	{ .name = "probe_read_user", .ret = -EFAULT },
	{ .name = "probe_read_user_str", .ret = -EFAULT },
	{ .name = "copy_from_user", .ret = -EFAULT },
	{ .name = "copy_from_user_task", .ret = -EFAULT },
	{ .name = "copy_from_user_str", .ret = -EFAULT },
};

void test_read_vsyscall(void)
{
	struct read_vsyscall *skel;
	unsigned int i;
	int err;

#if !defined(__x86_64__)
	test__skip();
	return;
#endif
	skel = read_vsyscall__open_and_load();
	if (!ASSERT_OK_PTR(skel, "read_vsyscall open_load"))
		return;

	skel->bss->target_pid = getpid();
	err = read_vsyscall__attach(skel);
	if (!ASSERT_EQ(err, 0, "read_vsyscall attach"))
		goto out;

	/* userspace may don't have vsyscall page due to LEGACY_VSYSCALL_NONE,
	 * but it doesn't affect the returned error codes.
	 */
	skel->bss->user_ptr = (void *)VSYSCALL_ADDR;
	usleep(1);

	for (i = 0; i < ARRAY_SIZE(all_read); i++)
		ASSERT_EQ(skel->bss->read_ret[i], all_read[i].ret, all_read[i].name);
out:
	read_vsyscall__destroy(skel);
}
