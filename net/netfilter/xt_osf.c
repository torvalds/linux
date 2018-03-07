/*
 * Copyright (c) 2003+ Evgeniy Polyakov <zbr@ioremap.net>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/capability.h>
#include <linux/if.h>
#include <linux/inetdevice.h>
#include <linux/ip.h>
#include <linux/list.h>
#include <linux/rculist.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/tcp.h>

#include <net/ip.h>
#include <net/tcp.h>

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/x_tables.h>
#include <net/netfilter/nf_log.h>
#include <linux/netfilter/xt_osf.h>

struct xt_osf_finger {
	struct rcu_head			rcu_head;
	struct list_head		finger_entry;
	struct xt_osf_user_finger	finger;
};

enum osf_fmatch_states {
	/* Packet does not match the fingerprint */
	FMATCH_WRONG = 0,
	/* Packet matches the fingerprint */
	FMATCH_OK,
	/* Options do not match the fingerprint, but header does */
	FMATCH_OPT_WRONG,
};

/*
 * Indexed by dont-fragment bit.
 * It is the only constant value in the fingerprint.
 */
static struct list_head xt_osf_fingers[2];

static const struct nla_policy xt_osf_policy[OSF_ATTR_MAX + 1] = {
	[OSF_ATTR_FINGER]	= { .len = sizeof(struct xt_osf_user_finger) },
};

static int xt_osf_add_callback(struct net *net, struct sock *ctnl,
			       struct sk_buff *skb, const struct nlmsghdr *nlh,
			       const struct nlattr * const osf_attrs[],
			       struct netlink_ext_ack *extack)
{
	struct xt_osf_user_finger *f;
	struct xt_osf_finger *kf = NULL, *sf;
	int err = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!osf_attrs[OSF_ATTR_FINGER])
		return -EINVAL;

	if (!(nlh->nlmsg_flags & NLM_F_CREATE))
		return -EINVAL;

	f = nla_data(osf_attrs[OSF_ATTR_FINGER]);

	kf = kmalloc(sizeof(struct xt_osf_finger), GFP_KERNEL);
	if (!kf)
		return -ENOMEM;

	memcpy(&kf->finger, f, sizeof(struct xt_osf_user_finger));

	list_for_each_entry(sf, &xt_osf_fingers[!!f->df], finger_entry) {
		if (memcmp(&sf->finger, f, sizeof(struct xt_osf_user_finger)))
			continue;

		kfree(kf);
		kf = NULL;

		if (nlh->nlmsg_flags & NLM_F_EXCL)
			err = -EEXIST;
		break;
	}

	/*
	 * We are protected by nfnl mutex.
	 */
	if (kf)
		list_add_tail_rcu(&kf->finger_entry, &xt_osf_fingers[!!f->df]);

	return err;
}

static int xt_osf_remove_callback(struct net *net, struct sock *ctnl,
				  struct sk_buff *skb,
				  const struct nlmsghdr *nlh,
				  const struct nlattr * const osf_attrs[],
				  struct netlink_ext_ack *extack)
{
	struct xt_osf_user_finger *f;
	struct xt_osf_finger *sf;
	int err = -ENOENT;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!osf_attrs[OSF_ATTR_FINGER])
		return -EINVAL;

	f = nla_data(osf_attrs[OSF_ATTR_FINGER]);

	list_for_each_entry(sf, &xt_osf_fingers[!!f->df], finger_entry) {
		if (memcmp(&sf->finger, f, sizeof(struct xt_osf_user_finger)))
			continue;

		/*
		 * We are protected by nfnl mutex.
		 */
		list_del_rcu(&sf->finger_entry);
		kfree_rcu(sf, rcu_head);

		err = 0;
		break;
	}

	return err;
}

static const struct nfnl_callback xt_osf_nfnetlink_callbacks[OSF_MSG_MAX] = {
	[OSF_MSG_ADD]	= {
		.call		= xt_osf_add_callback,
		.attr_count	= OSF_ATTR_MAX,
		.policy		= xt_osf_policy,
	},
	[OSF_MSG_REMOVE]	= {
		.call		= xt_osf_remove_callback,
		.attr_count	= OSF_ATTR_MAX,
		.policy		= xt_osf_policy,
	},
};

static const struct nfnetlink_subsystem xt_osf_nfnetlink = {
	.name			= "osf",
	.subsys_id		= NFNL_SUBSYS_OSF,
	.cb_count		= OSF_MSG_MAX,
	.cb			= xt_osf_nfnetlink_callbacks,
};

static inline int xt_osf_ttl(const struct sk_buff *skb, const struct xt_osf_info *info,
			    unsigned char f_ttl)
{
	const struct iphdr *ip = ip_hdr(skb);

	if (info->flags & XT_OSF_TTL) {
		if (info->ttl == XT_OSF_TTL_TRUE)
			return ip->ttl == f_ttl;
		if (info->ttl == XT_OSF_TTL_NOCHECK)
			return 1;
		else if (ip->ttl <= f_ttl)
			return 1;
		else {
			struct in_device *in_dev = __in_dev_get_rcu(skb->dev);
			int ret = 0;

			for_ifa(in_dev) {
				if (inet_ifa_match(ip->saddr, ifa)) {
					ret = (ip->ttl == f_ttl);
					break;
				}
			}
			endfor_ifa(in_dev);

			return ret;
		}
	}

	return ip->ttl == f_ttl;
}

static bool
xt_osf_match_packet(const struct sk_buff *skb, struct xt_action_param *p)
{
	const struct xt_osf_info *info = p->matchinfo;
	const struct iphdr *ip = ip_hdr(skb);
	const struct tcphdr *tcp;
	struct tcphdr _tcph;
	int fmatch = FMATCH_WRONG, fcount = 0;
	unsigned int optsize = 0, check_WSS = 0;
	u16 window, totlen, mss = 0;
	bool df;
	const unsigned char *optp = NULL, *_optp = NULL;
	unsigned char opts[MAX_IPOPTLEN];
	const struct xt_osf_finger *kf;
	const struct xt_osf_user_finger *f;
	struct net *net = xt_net(p);

	if (!info)
		return false;

	tcp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(struct tcphdr), &_tcph);
	if (!tcp)
		return false;

	if (!tcp->syn)
		return false;

	totlen = ntohs(ip->tot_len);
	df = ntohs(ip->frag_off) & IP_DF;
	window = ntohs(tcp->window);

	if (tcp->doff * 4 > sizeof(struct tcphdr)) {
		optsize = tcp->doff * 4 - sizeof(struct tcphdr);

		_optp = optp = skb_header_pointer(skb, ip_hdrlen(skb) +
				sizeof(struct tcphdr), optsize, opts);
	}

	list_for_each_entry_rcu(kf, &xt_osf_fingers[df], finger_entry) {
		int foptsize, optnum;

		f = &kf->finger;

		if (!(info->flags & XT_OSF_LOG) && strcmp(info->genre, f->genre))
			continue;

		optp = _optp;
		fmatch = FMATCH_WRONG;

		if (totlen != f->ss || !xt_osf_ttl(skb, info, f->ttl))
			continue;

		/*
		 * Should not happen if userspace parser was written correctly.
		 */
		if (f->wss.wc >= OSF_WSS_MAX)
			continue;

		/* Check options */

		foptsize = 0;
		for (optnum = 0; optnum < f->opt_num; ++optnum)
			foptsize += f->opt[optnum].length;

		if (foptsize > MAX_IPOPTLEN ||
		    optsize > MAX_IPOPTLEN ||
		    optsize != foptsize)
			continue;

		check_WSS = f->wss.wc;

		for (optnum = 0; optnum < f->opt_num; ++optnum) {
			if (f->opt[optnum].kind == (*optp)) {
				__u32 len = f->opt[optnum].length;
				const __u8 *optend = optp + len;

				fmatch = FMATCH_OK;

				switch (*optp) {
				case OSFOPT_MSS:
					mss = optp[3];
					mss <<= 8;
					mss |= optp[2];

					mss = ntohs((__force __be16)mss);
					break;
				case OSFOPT_TS:
					break;
				}

				optp = optend;
			} else
				fmatch = FMATCH_OPT_WRONG;

			if (fmatch != FMATCH_OK)
				break;
		}

		if (fmatch != FMATCH_OPT_WRONG) {
			fmatch = FMATCH_WRONG;

			switch (check_WSS) {
			case OSF_WSS_PLAIN:
				if (f->wss.val == 0 || window == f->wss.val)
					fmatch = FMATCH_OK;
				break;
			case OSF_WSS_MSS:
				/*
				 * Some smart modems decrease mangle MSS to
				 * SMART_MSS_2, so we check standard, decreased
				 * and the one provided in the fingerprint MSS
				 * values.
				 */
#define SMART_MSS_1	1460
#define SMART_MSS_2	1448
				if (window == f->wss.val * mss ||
				    window == f->wss.val * SMART_MSS_1 ||
				    window == f->wss.val * SMART_MSS_2)
					fmatch = FMATCH_OK;
				break;
			case OSF_WSS_MTU:
				if (window == f->wss.val * (mss + 40) ||
				    window == f->wss.val * (SMART_MSS_1 + 40) ||
				    window == f->wss.val * (SMART_MSS_2 + 40))
					fmatch = FMATCH_OK;
				break;
			case OSF_WSS_MODULO:
				if ((window % f->wss.val) == 0)
					fmatch = FMATCH_OK;
				break;
			}
		}

		if (fmatch != FMATCH_OK)
			continue;

		fcount++;

		if (info->flags & XT_OSF_LOG)
			nf_log_packet(net, xt_family(p), xt_hooknum(p), skb,
				      xt_in(p), xt_out(p), NULL,
				      "%s [%s:%s] : %pI4:%d -> %pI4:%d hops=%d\n",
				      f->genre, f->version, f->subtype,
				      &ip->saddr, ntohs(tcp->source),
				      &ip->daddr, ntohs(tcp->dest),
				      f->ttl - ip->ttl);

		if ((info->flags & XT_OSF_LOG) &&
		    info->loglevel == XT_OSF_LOGLEVEL_FIRST)
			break;
	}

	if (!fcount && (info->flags & XT_OSF_LOG))
		nf_log_packet(net, xt_family(p), xt_hooknum(p), skb, xt_in(p),
			      xt_out(p), NULL,
			"Remote OS is not known: %pI4:%u -> %pI4:%u\n",
				&ip->saddr, ntohs(tcp->source),
				&ip->daddr, ntohs(tcp->dest));

	if (fcount)
		fmatch = FMATCH_OK;

	return fmatch == FMATCH_OK;
}

static struct xt_match xt_osf_match = {
	.name 		= "osf",
	.revision	= 0,
	.family		= NFPROTO_IPV4,
	.proto		= IPPROTO_TCP,
	.hooks      	= (1 << NF_INET_LOCAL_IN) |
				(1 << NF_INET_PRE_ROUTING) |
				(1 << NF_INET_FORWARD),
	.match 		= xt_osf_match_packet,
	.matchsize	= sizeof(struct xt_osf_info),
	.me		= THIS_MODULE,
};

static int __init xt_osf_init(void)
{
	int err = -EINVAL;
	int i;

	for (i=0; i<ARRAY_SIZE(xt_osf_fingers); ++i)
		INIT_LIST_HEAD(&xt_osf_fingers[i]);

	err = nfnetlink_subsys_register(&xt_osf_nfnetlink);
	if (err < 0) {
		pr_err("Failed to register OSF nsfnetlink helper (%d)\n", err);
		goto err_out_exit;
	}

	err = xt_register_match(&xt_osf_match);
	if (err) {
		pr_err("Failed to register OS fingerprint "
		       "matching module (%d)\n", err);
		goto err_out_remove;
	}

	return 0;

err_out_remove:
	nfnetlink_subsys_unregister(&xt_osf_nfnetlink);
err_out_exit:
	return err;
}

static void __exit xt_osf_fini(void)
{
	struct xt_osf_finger *f;
	int i;

	nfnetlink_subsys_unregister(&xt_osf_nfnetlink);
	xt_unregister_match(&xt_osf_match);

	rcu_read_lock();
	for (i=0; i<ARRAY_SIZE(xt_osf_fingers); ++i) {

		list_for_each_entry_rcu(f, &xt_osf_fingers[i], finger_entry) {
			list_del_rcu(&f->finger_entry);
			kfree_rcu(f, rcu_head);
		}
	}
	rcu_read_unlock();

	rcu_barrier();
}

module_init(xt_osf_init);
module_exit(xt_osf_fini);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Evgeniy Polyakov <zbr@ioremap.net>");
MODULE_DESCRIPTION("Passive OS fingerprint matching.");
MODULE_ALIAS("ipt_osf");
MODULE_ALIAS("ip6t_osf");
MODULE_ALIAS_NFNL_SUBSYS(NFNL_SUBSYS_OSF);
