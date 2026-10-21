// SPDX-License-Identifier: GPL-2.0-only
/* Kernel module to match FRAG parameters. */

/* (C) 2001-2002 Andras Kis-Szabo <kisza@sch.bme.hu>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ipv6.h>
#include <linux/types.h>
#include <net/checksum.h>
#include <net/ipv6.h>

#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter_ipv6/ip6t_frag.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Xtables: IPv6 fragment match");
MODULE_AUTHOR("Andras Kis-Szabo <kisza@sch.bme.hu>");

/* Returns 1 if the id is matched by the range, 0 otherwise */
static inline bool
id_match(u_int32_t min, u_int32_t max, u_int32_t id, bool invert)
{
	return (id >= min && id <= max) ^ invert;
}

static bool
frag_mt6(const struct sk_buff *skb, struct xt_action_param *par)
{
	struct frag_hdr _frag;
	const struct frag_hdr *fh;
	const struct ip6t_frag *fraginfo = par->matchinfo;
	unsigned int ptr = 0;
	int err;

	err = ipv6_find_hdr(skb, &ptr, NEXTHDR_FRAGMENT, NULL, NULL);
	if (err < 0) {
		if (err != -ENOENT)
			par->hotdrop = true;
		return false;
	}

	fh = skb_header_pointer(skb, ptr, sizeof(_frag), &_frag);
	if (fh == NULL) {
		par->hotdrop = true;
		return false;
	}

	return id_match(fraginfo->ids[0], fraginfo->ids[1],
			 ntohl(fh->identification),
			 !!(fraginfo->invflags & IP6T_FRAG_INV_IDS)) &&
		!((fraginfo->flags & IP6T_FRAG_RES) &&
		  (fh->reserved || (ntohs(fh->frag_off) & 0x6))) &&
		!((fraginfo->flags & IP6T_FRAG_FST) &&
		  (ntohs(fh->frag_off) & ~0x7)) &&
		!((fraginfo->flags & IP6T_FRAG_MF) &&
		  !(ntohs(fh->frag_off) & IP6_MF)) &&
		!((fraginfo->flags & IP6T_FRAG_NMF) &&
		  (ntohs(fh->frag_off) & IP6_MF));
}

static int frag_mt6_check(const struct xt_mtchk_param *par)
{
	const struct ip6t_frag *fraginfo = par->matchinfo;

	if (fraginfo->invflags & ~IP6T_FRAG_INV_MASK) {
		pr_info_ratelimited("unknown flags %X\n", fraginfo->invflags);
		return -EINVAL;
	}
	return 0;
}

static struct xt_match frag_mt6_reg __read_mostly = {
	.name		= "frag",
	.family		= NFPROTO_IPV6,
	.match		= frag_mt6,
	.matchsize	= sizeof(struct ip6t_frag),
	.checkentry	= frag_mt6_check,
	.me		= THIS_MODULE,
};

static int __init frag_mt6_init(void)
{
	return xt_register_match(&frag_mt6_reg);
}

static void __exit frag_mt6_exit(void)
{
	xt_unregister_match(&frag_mt6_reg);
}

module_init(frag_mt6_init);
module_exit(frag_mt6_exit);
