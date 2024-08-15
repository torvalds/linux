// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int probe_res;

char input[4] = {};
int test_pid;

SEC("tracepoint/syscalls/sys_enter_nanosleep")
int probe(void *ctx)
{
	/* This BPF program performs variable-offset reads and writes on a
	 * stack-allocated buffer.
	 */
	char stack_buf[16];
	unsigned long len;
	unsigned long last;

	if ((bpf_get_current_pid_tgid() >> 32) != test_pid)
		return 0;

	/* Copy the input to the stack. */
	__builtin_memcpy(stack_buf, input, 4);

	/* The first byte in the buffer indicates the length. */
	len = stack_buf[0] & 0xf;
	last = (len - 1) & 0xf;

	/* Append something to the buffer. The offset where we write is not
	 * statically known; this is a variable-offset stack write.
	 */
	stack_buf[len] = 42;

	/* Index into the buffer at an unknown offset. This is a
	 * variable-offset stack read.
	 *
	 * Note that if it wasn't for the preceding variable-offset write, this
	 * read would be rejected because the stack slot cannot be verified as
	 * being initialized. With the preceding variable-offset write, the
	 * stack slot still cannot be verified, but the write inhibits the
	 * respective check on the reasoning that, if there was a
	 * variable-offset to a higher-or-equal spot, we're probably reading
	 * what we just wrote.
	 */
	probe_res = stack_buf[last];
	return 0;
}

char _license[] SEC("license") = "GPL";
