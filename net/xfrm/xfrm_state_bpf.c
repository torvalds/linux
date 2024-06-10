// SPDX-License-Identifier: GPL-2.0-only
/* Unstable XFRM state BPF helpers.
 *
 * Note that it is allowed to break compatibility for these functions since the
 * interface they are exposed through to BPF programs is explicitly unstable.
 */

#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/btf_ids.h>
#include <net/xdp.h>
#include <net/xfrm.h>

/* bpf_xfrm_state_opts - Options for XFRM state lookup helpers
 *
 * Members:
 * @error      - Out parameter, set for any errors encountered
 *		 Values:
 *		   -EINVAL - netns_id is less than -1
 *		   -EINVAL - opts__sz isn't BPF_XFRM_STATE_OPTS_SZ
 *		   -ENONET - No network namespace found for netns_id
 *		   -ENOENT - No xfrm_state found
 * @netns_id	- Specify the network namespace for lookup
 *		 Values:
 *		   BPF_F_CURRENT_NETNS (-1)
 *		     Use namespace associated with ctx
 *		   [0, S32_MAX]
 *		     Network Namespace ID
 * @mark	- XFRM mark to match on
 * @daddr	- Destination address to match on
 * @spi		- Security parameter index to match on
 * @proto	- IP protocol to match on (eg. IPPROTO_ESP)
 * @family	- Protocol family to match on (AF_INET/AF_INET6)
 */
struct bpf_xfrm_state_opts {
	s32 error;
	s32 netns_id;
	u32 mark;
	xfrm_address_t daddr;
	__be32 spi;
	u8 proto;
	u16 family;
};

enum {
	BPF_XFRM_STATE_OPTS_SZ = sizeof(struct bpf_xfrm_state_opts),
};

__bpf_kfunc_start_defs();

/* bpf_xdp_get_xfrm_state - Get XFRM state
 *
 * A `struct xfrm_state *`, if found, must be released with a corresponding
 * bpf_xdp_xfrm_state_release.
 *
 * Parameters:
 * @ctx	- Pointer to ctx (xdp_md) in XDP program
 *		    Cannot be NULL
 * @opts	- Options for lookup (documented above)
 *		    Cannot be NULL
 * @opts__sz	- Length of the bpf_xfrm_state_opts structure
 *		    Must be BPF_XFRM_STATE_OPTS_SZ
 */
__bpf_kfunc struct xfrm_state *
bpf_xdp_get_xfrm_state(struct xdp_md *ctx, struct bpf_xfrm_state_opts *opts, u32 opts__sz)
{
	struct xdp_buff *xdp = (struct xdp_buff *)ctx;
	struct net *net = dev_net(xdp->rxq->dev);
	struct xfrm_state *x;

	if (!opts || opts__sz < sizeof(opts->error))
		return NULL;

	if (opts__sz != BPF_XFRM_STATE_OPTS_SZ) {
		opts->error = -EINVAL;
		return NULL;
	}

	if (unlikely(opts->netns_id < BPF_F_CURRENT_NETNS)) {
		opts->error = -EINVAL;
		return NULL;
	}

	if (opts->netns_id >= 0) {
		net = get_net_ns_by_id(net, opts->netns_id);
		if (unlikely(!net)) {
			opts->error = -ENONET;
			return NULL;
		}
	}

	x = xfrm_state_lookup(net, opts->mark, &opts->daddr, opts->spi,
			      opts->proto, opts->family);

	if (opts->netns_id >= 0)
		put_net(net);
	if (!x)
		opts->error = -ENOENT;

	return x;
}

/* bpf_xdp_xfrm_state_release - Release acquired xfrm_state object
 *
 * This must be invoked for referenced PTR_TO_BTF_ID, and the verifier rejects
 * the program if any references remain in the program in all of the explored
 * states.
 *
 * Parameters:
 * @x		- Pointer to referenced xfrm_state object, obtained using
 *		  bpf_xdp_get_xfrm_state.
 */
__bpf_kfunc void bpf_xdp_xfrm_state_release(struct xfrm_state *x)
{
	xfrm_state_put(x);
}

__bpf_kfunc_end_defs();

BTF_KFUNCS_START(xfrm_state_kfunc_set)
BTF_ID_FLAGS(func, bpf_xdp_get_xfrm_state, KF_RET_NULL | KF_ACQUIRE)
BTF_ID_FLAGS(func, bpf_xdp_xfrm_state_release, KF_RELEASE)
BTF_KFUNCS_END(xfrm_state_kfunc_set)

static const struct btf_kfunc_id_set xfrm_state_xdp_kfunc_set = {
	.owner = THIS_MODULE,
	.set   = &xfrm_state_kfunc_set,
};

int __init register_xfrm_state_bpf(void)
{
	return register_btf_kfunc_id_set(BPF_PROG_TYPE_XDP,
					 &xfrm_state_xdp_kfunc_set);
}
