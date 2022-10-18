// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2022 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Roberto Sassu <roberto.sassu@huawei.com>
 */

#include <linux/keyctl.h>
#include <test_progs.h>

#include "test_lookup_key.skel.h"

#define KEY_LOOKUP_CREATE	0x01
#define KEY_LOOKUP_PARTIAL	0x02

static bool kfunc_not_supported;

static int libbpf_print_cb(enum libbpf_print_level level, const char *fmt,
			   va_list args)
{
	char *func;

	if (strcmp(fmt, "libbpf: extern (func ksym) '%s': not found in kernel or module BTFs\n"))
		return 0;

	func = va_arg(args, char *);

	if (strcmp(func, "bpf_lookup_user_key") && strcmp(func, "bpf_key_put") &&
	    strcmp(func, "bpf_lookup_system_key"))
		return 0;

	kfunc_not_supported = true;
	return 0;
}

void test_lookup_key(void)
{
	libbpf_print_fn_t old_print_cb;
	struct test_lookup_key *skel;
	__u32 next_id;
	int ret;

	skel = test_lookup_key__open();
	if (!ASSERT_OK_PTR(skel, "test_lookup_key__open"))
		return;

	old_print_cb = libbpf_set_print(libbpf_print_cb);
	ret = test_lookup_key__load(skel);
	libbpf_set_print(old_print_cb);

	if (ret < 0 && kfunc_not_supported) {
		printf("%s:SKIP:bpf_lookup_*_key(), bpf_key_put() kfuncs not supported\n",
		       __func__);
		test__skip();
		goto close_prog;
	}

	if (!ASSERT_OK(ret, "test_lookup_key__load"))
		goto close_prog;

	ret = test_lookup_key__attach(skel);
	if (!ASSERT_OK(ret, "test_lookup_key__attach"))
		goto close_prog;

	skel->bss->monitored_pid = getpid();
	skel->bss->key_serial = KEY_SPEC_THREAD_KEYRING;

	/* The thread-specific keyring does not exist, this test fails. */
	skel->bss->flags = 0;

	ret = bpf_prog_get_next_id(0, &next_id);
	if (!ASSERT_LT(ret, 0, "bpf_prog_get_next_id"))
		goto close_prog;

	/* Force creation of the thread-specific keyring, this test succeeds. */
	skel->bss->flags = KEY_LOOKUP_CREATE;

	ret = bpf_prog_get_next_id(0, &next_id);
	if (!ASSERT_OK(ret, "bpf_prog_get_next_id"))
		goto close_prog;

	/* Pass both lookup flags for parameter validation. */
	skel->bss->flags = KEY_LOOKUP_CREATE | KEY_LOOKUP_PARTIAL;

	ret = bpf_prog_get_next_id(0, &next_id);
	if (!ASSERT_OK(ret, "bpf_prog_get_next_id"))
		goto close_prog;

	/* Pass invalid flags. */
	skel->bss->flags = UINT64_MAX;

	ret = bpf_prog_get_next_id(0, &next_id);
	if (!ASSERT_LT(ret, 0, "bpf_prog_get_next_id"))
		goto close_prog;

	skel->bss->key_serial = 0;
	skel->bss->key_id = 1;

	ret = bpf_prog_get_next_id(0, &next_id);
	if (!ASSERT_OK(ret, "bpf_prog_get_next_id"))
		goto close_prog;

	skel->bss->key_id = UINT32_MAX;

	ret = bpf_prog_get_next_id(0, &next_id);
	ASSERT_LT(ret, 0, "bpf_prog_get_next_id");

close_prog:
	skel->bss->monitored_pid = 0;
	test_lookup_key__destroy(skel);
}
