// SPDX-License-Identifier: GPL-2.0-only
/* Unstable Fou Helpers for TC-BPF hook
 *
 * These are called from SCHED_CLS BPF programs. Note that it is
 * allowed to break compatibility for these functions since the interface they
 * are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf.h>
#include <linux/btf_ids.h>

#include <net/dst_metadata.h>
#include <net/fou.h>

struct bpf_fou_encap {
	__be16 sport;
	__be16 dport;
};

enum bpf_fou_encap_type {
	FOU_BPF_ENCAP_FOU,
	FOU_BPF_ENCAP_GUE,
};

__bpf_kfunc_start_defs();

/* bpf_skb_set_fou_encap - Set FOU encap parameters
 *
 * This function allows for using GUE or FOU encapsulation together with an
 * ipip device in collect-metadata mode.
 *
 * It is meant to be used in BPF tc-hooks and after a call to the
 * bpf_skb_set_tunnel_key helper, responsible for setting IP addresses.
 *
 * Parameters:
 * @skb_ctx	Pointer to ctx (__sk_buff) in TC program. Cannot be NULL
 * @encap	Pointer to a `struct bpf_fou_encap` storing UDP src and
 * 		dst ports. If sport is set to 0 the kernel will auto-assign a
 * 		port. This is similar to using `encap-sport auto`.
 * 		Cannot be NULL
 * @type	Encapsulation type for the packet. Their definitions are
 * 		specified in `enum bpf_fou_encap_type`
 */
__bpf_kfunc int bpf_skb_set_fou_encap(struct __sk_buff *skb_ctx,
				      struct bpf_fou_encap *encap, int type)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct ip_tunnel_info *info = skb_tunnel_info(skb);

	if (unlikely(!encap))
		return -EINVAL;

	if (unlikely(!info || !(info->mode & IP_TUNNEL_INFO_TX)))
		return -EINVAL;

	switch (type) {
	case FOU_BPF_ENCAP_FOU:
		info->encap.type = TUNNEL_ENCAP_FOU;
		break;
	case FOU_BPF_ENCAP_GUE:
		info->encap.type = TUNNEL_ENCAP_GUE;
		break;
	default:
		info->encap.type = TUNNEL_ENCAP_NONE;
	}

	if (test_bit(IP_TUNNEL_CSUM_BIT, info->key.tun_flags))
		info->encap.flags |= TUNNEL_ENCAP_FLAG_CSUM;

	info->encap.sport = encap->sport;
	info->encap.dport = encap->dport;

	return 0;
}

/* bpf_skb_get_fou_encap - Get FOU encap parameters
 *
 * This function allows for reading encap metadata from a packet received
 * on an ipip device in collect-metadata mode.
 *
 * Parameters:
 * @skb_ctx	Pointer to ctx (__sk_buff) in TC program. Cannot be NULL
 * @encap	Pointer to a struct bpf_fou_encap storing UDP source and
 * 		destination port. Cannot be NULL
 */
__bpf_kfunc int bpf_skb_get_fou_encap(struct __sk_buff *skb_ctx,
				      struct bpf_fou_encap *encap)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct ip_tunnel_info *info = skb_tunnel_info(skb);

	if (unlikely(!info))
		return -EINVAL;

	encap->sport = info->encap.sport;
	encap->dport = info->encap.dport;

	return 0;
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(fou_kfunc_set)
BTF_ID_FLAGS(func, bpf_skb_set_fou_encap)
BTF_ID_FLAGS(func, bpf_skb_get_fou_encap)
BTF_KFUNCS_END(fou_kfunc_set)

static const struct btf_kfunc_id_set fou_bpf_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &fou_kfunc_set,
};

int register_fou_bpf(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS,
					 &fou_bpf_kfunc_set);
}
