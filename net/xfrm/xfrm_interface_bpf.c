// SPDX-License-Identifier: GPL-2.0-only
/* Unstable XFRM Helpers for TC-BPF hook
 *
 * These are called from SCHED_CLS BPF programs. Note that it is
 * allowed to break compatibility for these functions since the interface they
 * are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf.h>
#include <linux/btf_ids.h>

#include <net/dst_metadata.h>
#include <net/xfrm.h>

/* bpf_xfrm_info - XFRM metadata information
 *
 * Members:
 * @if_id	- XFRM if_id:
 *		    Transmit: if_id to be used in policy and state lookups
 *		    Receive: if_id of the state matched for the incoming packet
 * @link	- Underlying device ifindex:
 *		    Transmit: used as the underlying device in VRF routing
 *		    Receive: the device on which the packet had been received
 */
struct bpf_xfrm_info {
	u32 if_id;
	int link;
};

__bpf_kfunc_start_defs();

/* bpf_skb_get_xfrm_info - Get XFRM metadata
 *
 * Parameters:
 * @skb_ctx	- Pointer to ctx (__sk_buff) in TC program
 *		    Cannot be NULL
 * @to		- Pointer to memory to which the metadata will be copied
 *		    Cannot be NULL
 */
__bpf_kfunc int bpf_skb_get_xfrm_info(struct __sk_buff *skb_ctx, struct bpf_xfrm_info *to)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct xfrm_md_info *info;

	info = skb_xfrm_md_info(skb);
	if (!info)
		return -EINVAL;

	to->if_id = info->if_id;
	to->link = info->link;
	return 0;
}

/* bpf_skb_get_xfrm_info - Set XFRM metadata
 *
 * Parameters:
 * @skb_ctx	- Pointer to ctx (__sk_buff) in TC program
 *		    Cannot be NULL
 * @from	- Pointer to memory from which the metadata will be copied
 *		    Cannot be NULL
 */
__bpf_kfunc int bpf_skb_set_xfrm_info(struct __sk_buff *skb_ctx, const struct bpf_xfrm_info *from)
{
	struct sk_buff *skb = (struct sk_buff *)skb_ctx;
	struct metadata_dst *md_dst;
	struct xfrm_md_info *info;

	if (unlikely(skb_metadata_dst(skb)))
		return -EINVAL;

	if (!xfrm_bpf_md_dst) {
		struct metadata_dst __percpu *tmp;

		tmp = metadata_dst_alloc_percpu(0, METADATA_XFRM, GFP_ATOMIC);
		if (!tmp)
			return -ENOMEM;
		if (cmpxchg(&xfrm_bpf_md_dst, NULL, tmp))
			metadata_dst_free_percpu(tmp);
	}
	md_dst = this_cpu_ptr(xfrm_bpf_md_dst);

	info = &md_dst->u.xfrm_info;

	info->if_id = from->if_id;
	info->link = from->link;
	skb_dst_force(skb);
	info->dst_orig = skb_dst(skb);

	dst_hold((struct dst_entry *)md_dst);
	skb_dst_set(skb, (struct dst_entry *)md_dst);
	return 0;
}

__bpf_kfunc_end_defs();

BTF_SET8_START(xfrm_ifc_kfunc_set)
BTF_ID_FLAGS(func, bpf_skb_get_xfrm_info)
BTF_ID_FLAGS(func, bpf_skb_set_xfrm_info)
BTF_SET8_END(xfrm_ifc_kfunc_set)

static const struct btf_kfunc_id_set xfrm_interface_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &xfrm_ifc_kfunc_set,
};

int __init register_xfrm_interface_bpf(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS,
					 &xfrm_interface_kfunc_set);
}
