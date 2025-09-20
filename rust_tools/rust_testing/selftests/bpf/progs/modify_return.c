// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright 2020 Google LLC.
 */

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

static int sequence = 0;
__s32 input_retval = 0;

__u64 fentry_result = 0;
SEC("fentry/bpf_modify_return_test")
int BPF_PROG(fentry_test, int a, __u64 b)
{
	sequence++;
	fentry_result = (sequence == 1);
	return 0;
}

__u64 fmod_ret_result = 0;
SEC("fmod_ret/bpf_modify_return_test")
int BPF_PROG(fmod_ret_test, int a, int *b, int ret)
{
	sequence++;
	/* This is the first fmod_ret program, the ret passed should be 0 */
	fmod_ret_result = (sequence == 2 && ret == 0);
	return input_retval;
}

__u64 fexit_result = 0;
SEC("fexit/bpf_modify_return_test")
int BPF_PROG(fexit_test, int a, __u64 b, int ret)
{
	sequence++;
	/* If the input_reval is non-zero a successful modification should have
	 * occurred.
	 */
	if (input_retval)
		fexit_result = (sequence == 3 && ret == input_retval);
	else
		fexit_result = (sequence == 3 && ret == 4);

	return 0;
}

static int sequence2;

__u64 fentry_result2 = 0;
SEC("fentry/bpf_modify_return_test2")
int BPF_PROG(fentry_test2, int a, int *b, short c, int d, void *e, char f,
	     int g)
{
	sequence2++;
	fentry_result2 = (sequence2 == 1);
	return 0;
}

__u64 fmod_ret_result2 = 0;
SEC("fmod_ret/bpf_modify_return_test2")
int BPF_PROG(fmod_ret_test2, int a, int *b, short c, int d, void *e, char f,
	     int g, int ret)
{
	sequence2++;
	/* This is the first fmod_ret program, the ret passed should be 0 */
	fmod_ret_result2 = (sequence2 == 2 && ret == 0);
	return input_retval;
}

__u64 fexit_result2 = 0;
SEC("fexit/bpf_modify_return_test2")
int BPF_PROG(fexit_test2, int a, int *b, short c, int d, void *e, char f,
	     int g, int ret)
{
	sequence2++;
	/* If the input_reval is non-zero a successful modification should have
	 * occurred.
	 */
	if (input_retval)
		fexit_result2 = (sequence2 == 3 && ret == input_retval);
	else
		fexit_result2 = (sequence2 == 3 && ret == 29);

	return 0;
}
