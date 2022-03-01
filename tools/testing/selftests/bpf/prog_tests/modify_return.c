// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include <test_progs.h>
#include "modify_return.skel.h"

#define LOWER(x) ((x) & 0xffff)
#define UPPER(x) ((x) >> 16)


static void run_test(__u32 input_retval, __u16 want_side_effect, __s16 want_ret)
{
	struct modify_return *skel = NULL;
	int err, prog_fd;
	__u32 duration = 0, retval;
	__u16 side_effect;
	__s16 ret;

	skel = modify_return__open_and_load();
	if (CHECK(!skel, "skel_load", "modify_return skeleton failed\n"))
		goto cleanup;

	err = modify_return__attach(skel);
	if (CHECK(err, "modify_return", "attach failed: %d\n", err))
		goto cleanup;

	skel->bss->input_retval = input_retval;
	prog_fd = bpf_program__fd(skel->progs.fmod_ret_test);
	err = bpf_prog_test_run(prog_fd, 1, NULL, 0, NULL, 0,
				&retval, &duration);

	CHECK(err, "test_run", "err %d errno %d\n", err, errno);

	side_effect = UPPER(retval);
	ret  = LOWER(retval);

	CHECK(ret != want_ret, "test_run",
	      "unexpected ret: %d, expected: %d\n", ret, want_ret);
	CHECK(side_effect != want_side_effect, "modify_return",
	      "unexpected side_effect: %d\n", side_effect);

	CHECK(skel->bss->fentry_result != 1, "modify_return",
	      "fentry failed\n");
	CHECK(skel->bss->fexit_result != 1, "modify_return",
	      "fexit failed\n");
	CHECK(skel->bss->fmod_ret_result != 1, "modify_return",
	      "fmod_ret failed\n");

cleanup:
	modify_return__destroy(skel);
}

/* TODO: conflict with get_func_ip_test */
void serial_test_modify_return(void)
{
	run_test(0 /* input_retval */,
		 1 /* want_side_effect */,
		 4 /* want_ret */);
	run_test(-EINVAL /* input_retval */,
		 0 /* want_side_effect */,
		 -EINVAL /* want_ret */);
}

