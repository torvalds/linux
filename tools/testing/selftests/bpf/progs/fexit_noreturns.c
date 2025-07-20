// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

SEC("fexit/do_exit")
__failure __msg("Attaching fexit/fmod_ret to __noreturn functions is rejected.")
int BPF_PROG(noreturns)
{
	return 0;
}
