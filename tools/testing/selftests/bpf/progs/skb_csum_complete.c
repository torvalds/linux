// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

SEC("tc")
int change_tail_trim(struct __sk_buff *skb)
{
	/*
	 * Drops CHECKSUM_COMPLETE. skb->csum still covers the trimmed bytes,
	 * which hold the non-zero tcp.urg_ptr of pkt_v4, so it no longer
	 * matches the packet.
	 */
	if (bpf_skb_change_tail(skb, skb->len - 4, 0))
		return 1;
	return 0;
}

SEC("tc")
int change_tail_grow_write(struct __sk_buff *skb)
{
	__u8 val = 0xab;

	/*
	 * Drops CHECKSUM_COMPLETE. The new bytes are zero and do not change
	 * the sum, but the write below can no longer update skb->csum.
	 */
	if (bpf_skb_change_tail(skb, skb->len + 4, 0))
		return 1;
	if (bpf_skb_store_bytes(skb, skb->len - 1, &val, sizeof(val),
				BPF_F_RECOMPUTE_CSUM))
		return 2;
	return 0;
}

SEC("tc")
int store_no_recompute(struct __sk_buff *skb)
{
	__u8 val = 0xab;

	/* skb stays CHECKSUM_COMPLETE, but skb->csum is not updated. */
	if (bpf_skb_store_bytes(skb, skb->len - 1, &val, sizeof(val), 0))
		return 1;
	return 0;
}

char _license[] SEC("license") = "GPL";
