// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

struct nf_conn *bpf_skb_ct_lookup(struct __sk_buff *skb_ctx,
				  struct bpf_sock_tuple *bpf_tuple,
				  u32 tuple__sz, struct bpf_ct_opts *opts,
				  u32 opts__sz) __ksym;
void bpf_ct_release(struct nf_conn *nfct) __ksym;

char _license[] SEC("license") = "GPL";

SEC("tc")
__description("kfunc packet write requests writable skb")
__success
/* bpf_unclone_prologue() */
__xlated("r6 = *(u8 *)(r1 +{{[0-9]+}})")
__xlated("...")
__xlated("w6 &= {{(1|128)}}")
__xlated("...")
__xlated("if r6 == 0x0 goto")
__xlated("r6 = r1")
__xlated("r2 ^= r2")
__xlated("call")
__xlated("if r0 == 0x0 goto")
__xlated("w0 = 2")
__xlated("...")
__xlated("exit")
__xlated("r1 = r6")
int kfunc_packet_write(struct __sk_buff *skb)
{
	void *data_end = (void *)(long)skb->data_end;
	void *data = (void *)(long)skb->data;
	struct bpf_sock_tuple tuple = {};
	struct nf_conn *nfct;

	if (data + sizeof(struct bpf_ct_opts) > data_end)
		return 0;

	/* An invalid tuple size makes bpf_skb_ct_lookup() write opts->error. */
	nfct = bpf_skb_ct_lookup(skb, &tuple, 1, data, sizeof(struct bpf_ct_opts));
	if (nfct)
		bpf_ct_release(nfct);
	return 0;
}
