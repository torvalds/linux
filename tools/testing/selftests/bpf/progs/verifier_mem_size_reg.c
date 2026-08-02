// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"

char _license[] SEC("license") = "GPL";

/*
 * The __szk size of a kfunc memory/size pair must be marked precise even when
 * the nullable buffer is passed as NULL.
 */
SEC("?tc")
__success __log_level(2)
__msg("mark_precise: frame0: regs=r4 stack= before")
int dynptr_slice_null_buf_size_precise(struct __sk_buff *skb)
{
	struct bpf_dynptr dptr;
	char *p;

	bpf_dynptr_from_skb(skb, 0, &dptr);

	p = bpf_dynptr_slice(&dptr, 0, NULL, 8);
	if (p)
		return p[0];
	return 0;
}
