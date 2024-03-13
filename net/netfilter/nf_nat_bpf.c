// SPDX-License-Identifier: GPL-2.0-only
/* Unstable NAT Helpers for XDP and TC-BPF hook
 *
 * These are called from the XDP and SCHED_CLS BPF programs. Note that it is
 * allowed to break compatibility for these functions since the interface they
 * are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf.h>
#include <linux/btf_ids.h>
#include <net/netfilter/nf_conntrack_bpf.h>
#include <net/netfilter/nf_conntrack_core.h>
#include <net/netfilter/nf_nat.h>

__diag_push();
__diag_ignore_all("-Wmissing-prototypes",
		  "Global functions as their definitions will be in nf_nat BTF");

/* bpf_ct_set_nat_info - Set source or destination nat address
 *
 * Set source or destination nat address of the newly allocated
 * nf_conn before insertion. This must be invoked for referenced
 * PTR_TO_BTF_ID to nf_conn___init.
 *
 * Parameters:
 * @nfct	- Pointer to referenced nf_conn object, obtained using
 *		  bpf_xdp_ct_alloc or bpf_skb_ct_alloc.
 * @addr	- Nat source/destination address
 * @port	- Nat source/destination port. Non-positive values are
 *		  interpreted as select a random port.
 * @manip	- NF_NAT_MANIP_SRC or NF_NAT_MANIP_DST
 */
int bpf_ct_set_nat_info(struct nf_conn___init *nfct,
			union nf_inet_addr *addr, int port,
			enum nf_nat_manip_type manip)
{
	struct nf_conn *ct = (struct nf_conn *)nfct;
	u16 proto = nf_ct_l3num(ct);
	struct nf_nat_range2 range;

	if (proto != NFPROTO_IPV4 && proto != NFPROTO_IPV6)
		return -EINVAL;

	memset(&range, 0, sizeof(struct nf_nat_range2));
	range.flags = NF_NAT_RANGE_MAP_IPS;
	range.min_addr = *addr;
	range.max_addr = range.min_addr;
	if (port > 0) {
		range.flags |= NF_NAT_RANGE_PROTO_SPECIFIED;
		range.min_proto.all = cpu_to_be16(port);
		range.max_proto.all = range.min_proto.all;
	}

	return nf_nat_setup_info(ct, &range, manip) == NF_DROP ? -ENOMEM : 0;
}

__diag_pop()

BTF_SET8_START(nf_nat_kfunc_set)
BTF_ID_FLAGS(func, bpf_ct_set_nat_info, KF_TRUSTED_ARGS)
BTF_SET8_END(nf_nat_kfunc_set)

static const struct btf_kfunc_id_set nf_bpf_nat_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &nf_nat_kfunc_set,
};

int register_nf_nat_bpf(void)
{
	int ret;

	ret = register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP,
					&nf_bpf_nat_kfunc_set);
	if (ret)
		return ret;

	return register_btf_kfunc_id_set(BPF_PROG_TYPE_SCHED_CLS,
					 &nf_bpf_nat_kfunc_set);
}
