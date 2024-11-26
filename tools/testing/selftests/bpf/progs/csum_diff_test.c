// SPDX-License-Identifier: GPL-2.0
/* Copyright Amazon.com Inc. or its affiliates */
#include <linux/types.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#define BUFF_SZ 512

/* Will be updated by benchmark before program loading */
char to_buff[BUFF_SZ];
const volatile unsigned int to_buff_len = 0;
char from_buff[BUFF_SZ];
const volatile unsigned int from_buff_len = 0;
unsigned short seed = 0;

short result;

char _license[] SEC("license") = "GPL";

SEC("tc")
int compute_checksum(void *ctx)
{
	int to_len_half = to_buff_len / 2;
	int from_len_half = from_buff_len / 2;
	short result2;

	/* Calculate checksum in one go */
	result2 = bpf_csum_diff((void *)from_buff, from_buff_len,
				(void *)to_buff, to_buff_len, seed);

	/* Calculate checksum by concatenating bpf_csum_diff()*/
	result = bpf_csum_diff((void *)from_buff, from_buff_len - from_len_half,
			       (void *)to_buff, to_buff_len - to_len_half, seed);

	result = bpf_csum_diff((void *)from_buff + (from_buff_len - from_len_half), from_len_half,
			       (void *)to_buff + (to_buff_len - to_len_half), to_len_half, result);

	result = (result == result2) ? result : 0;

	return 0;
}
