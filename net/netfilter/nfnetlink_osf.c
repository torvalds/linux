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
#include <linux/netfilter/nfnetlink_osf.h>

/*
 * Indexed by dont-fragment bit.
 * It is the only constant value in the fingerprint.
 */
struct list_head nf_osf_fingers[2];
EXPORT_SYMBOL_GPL(nf_osf_fingers);

static inline int nf_osf_ttl(const struct sk_buff *skb,
			     int ttl_check, unsigned char f_ttl)
{
	struct in_device *in_dev = __in_dev_get_rcu(skb->dev);
	const struct iphdr *ip = ip_hdr(skb);
	int ret = 0;

	if (ttl_check == NF_OSF_TTL_TRUE)
		return ip->ttl == f_ttl;
	if (ttl_check == NF_OSF_TTL_NOCHECK)
		return 1;
	else if (ip->ttl <= f_ttl)
		return 1;

	for_ifa(in_dev) {
		if (inet_ifa_match(ip->saddr, ifa)) {
			ret = (ip->ttl == f_ttl);
			break;
		}
	}

	endfor_ifa(in_dev);

	return ret;
}

struct nf_osf_hdr_ctx {
	bool			df;
	u16			window;
	u16			totlen;
	const unsigned char	*optp;
	unsigned int		optsize;
};

static bool nf_osf_match_one(const struct sk_buff *skb,
			     const struct nf_osf_user_finger *f,
			     int ttl_check,
			     struct nf_osf_hdr_ctx *ctx)
{
	const __u8 *optpinit = ctx->optp;
	unsigned int check_WSS = 0;
	int fmatch = FMATCH_WRONG;
	int foptsize, optnum;
	u16 mss = 0;

	if (ctx->totlen != f->ss || !nf_osf_ttl(skb, ttl_check, f->ttl))
		return false;

	/*
	 * Should not happen if userspace parser was written correctly.
	 */
	if (f->wss.wc >= OSF_WSS_MAX)
		return false;

	/* Check options */

	foptsize = 0;
	for (optnum = 0; optnum < f->opt_num; ++optnum)
		foptsize += f->opt[optnum].length;

	if (foptsize > MAX_IPOPTLEN ||
	    ctx->optsize > MAX_IPOPTLEN ||
	    ctx->optsize != foptsize)
		return false;

	check_WSS = f->wss.wc;

	for (optnum = 0; optnum < f->opt_num; ++optnum) {
		if (f->opt[optnum].kind == *ctx->optp) {
			__u32 len = f->opt[optnum].length;
			const __u8 *optend = ctx->optp + len;

			fmatch = FMATCH_OK;

			switch (*ctx->optp) {
			case OSFOPT_MSS:
				mss = ctx->optp[3];
				mss <<= 8;
				mss |= ctx->optp[2];

				mss = ntohs((__force __be16)mss);
				break;
			case OSFOPT_TS:
				break;
			}

			ctx->optp = optend;
		} else
			fmatch = FMATCH_OPT_WRONG;

		if (fmatch != FMATCH_OK)
			break;
	}

	if (fmatch != FMATCH_OPT_WRONG) {
		fmatch = FMATCH_WRONG;

		switch (check_WSS) {
		case OSF_WSS_PLAIN:
			if (f->wss.val == 0 || ctx->window == f->wss.val)
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
			if (ctx->window == f->wss.val * mss ||
			    ctx->window == f->wss.val * SMART_MSS_1 ||
			    ctx->window == f->wss.val * SMART_MSS_2)
				fmatch = FMATCH_OK;
			break;
		case OSF_WSS_MTU:
			if (ctx->window == f->wss.val * (mss + 40) ||
			    ctx->window == f->wss.val * (SMART_MSS_1 + 40) ||
			    ctx->window == f->wss.val * (SMART_MSS_2 + 40))
				fmatch = FMATCH_OK;
			break;
		case OSF_WSS_MODULO:
			if ((ctx->window % f->wss.val) == 0)
				fmatch = FMATCH_OK;
			break;
		}
	}

	if (fmatch != FMATCH_OK)
		ctx->optp = optpinit;

	return fmatch == FMATCH_OK;
}

static const struct tcphdr *nf_osf_hdr_ctx_init(struct nf_osf_hdr_ctx *ctx,
						const struct sk_buff *skb,
						const struct iphdr *ip,
						unsigned char *opts)
{
	const struct tcphdr *tcp;
	struct tcphdr _tcph;

	tcp = skb_header_pointer(skb, ip_hdrlen(skb), sizeof(struct tcphdr), &_tcph);
	if (!tcp)
		return NULL;

	if (!tcp->syn)
		return NULL;

	ctx->totlen = ntohs(ip->tot_len);
	ctx->df = ntohs(ip->frag_off) & IP_DF;
	ctx->window = ntohs(tcp->window);

	if (tcp->doff * 4 > sizeof(struct tcphdr)) {
		ctx->optsize = tcp->doff * 4 - sizeof(struct tcphdr);

		ctx->optp = skb_header_pointer(skb, ip_hdrlen(skb) +
				sizeof(struct tcphdr), ctx->optsize, opts);
	}

	return tcp;
}

bool
nf_osf_match(const struct sk_buff *skb, u_int8_t family,
	     int hooknum, struct net_device *in, struct net_device *out,
	     const struct nf_osf_info *info, struct net *net,
	     const struct list_head *nf_osf_fingers)
{
	const struct iphdr *ip = ip_hdr(skb);
	const struct nf_osf_user_finger *f;
	unsigned char opts[MAX_IPOPTLEN];
	const struct nf_osf_finger *kf;
	int fcount = 0, ttl_check;
	int fmatch = FMATCH_WRONG;
	struct nf_osf_hdr_ctx ctx;
	const struct tcphdr *tcp;

	memset(&ctx, 0, sizeof(ctx));

	tcp = nf_osf_hdr_ctx_init(&ctx, skb, ip, opts);
	if (!tcp)
		return false;

	ttl_check = (info->flags & NF_OSF_TTL) ? info->ttl : 0;

	list_for_each_entry_rcu(kf, &nf_osf_fingers[ctx.df], finger_entry) {

		f = &kf->finger;

		if (!(info->flags & NF_OSF_LOG) && strcmp(info->genre, f->genre))
			continue;

		if (!nf_osf_match_one(skb, f, ttl_check, &ctx))
			continue;

		fmatch = FMATCH_OK;

		fcount++;

		if (info->flags & NF_OSF_LOG)
			nf_log_packet(net, family, hooknum, skb,
				      in, out, NULL,
				      "%s [%s:%s] : %pI4:%d -> %pI4:%d hops=%d\n",
				      f->genre, f->version, f->subtype,
				      &ip->saddr, ntohs(tcp->source),
				      &ip->daddr, ntohs(tcp->dest),
				      f->ttl - ip->ttl);

		if ((info->flags & NF_OSF_LOG) &&
		    info->loglevel == NF_OSF_LOGLEVEL_FIRST)
			break;
	}

	if (!fcount && (info->flags & NF_OSF_LOG))
		nf_log_packet(net, family, hooknum, skb, in, out, NULL,
			      "Remote OS is not known: %pI4:%u -> %pI4:%u\n",
			      &ip->saddr, ntohs(tcp->source),
			      &ip->daddr, ntohs(tcp->dest));

	if (fcount)
		fmatch = FMATCH_OK;

	return fmatch == FMATCH_OK;
}
EXPORT_SYMBOL_GPL(nf_osf_match);

const char *nf_osf_find(const struct sk_buff *skb,
			const struct list_head *nf_osf_fingers,
			const int ttl_check)
{
	const struct iphdr *ip = ip_hdr(skb);
	const struct nf_osf_user_finger *f;
	unsigned char opts[MAX_IPOPTLEN];
	const struct nf_osf_finger *kf;
	struct nf_osf_hdr_ctx ctx;
	const struct tcphdr *tcp;
	const char *genre = NULL;

	memset(&ctx, 0, sizeof(ctx));

	tcp = nf_osf_hdr_ctx_init(&ctx, skb, ip, opts);
	if (!tcp)
		return NULL;

	list_for_each_entry_rcu(kf, &nf_osf_fingers[ctx.df], finger_entry) {
		f = &kf->finger;
		if (!nf_osf_match_one(skb, f, ttl_check, &ctx))
			continue;

		genre = f->genre;
		break;
	}

	return genre;
}
EXPORT_SYMBOL_GPL(nf_osf_find);

static const struct nla_policy nfnl_osf_policy[OSF_ATTR_MAX + 1] = {
	[OSF_ATTR_FINGER]	= { .len = sizeof(struct nf_osf_user_finger) },
};

static int nfnl_osf_add_callback(struct net *net, struct sock *ctnl,
				 struct sk_buff *skb, const struct nlmsghdr *nlh,
				 const struct nlattr * const osf_attrs[],
				 struct netlink_ext_ack *extack)
{
	struct nf_osf_user_finger *f;
	struct nf_osf_finger *kf = NULL, *sf;
	int err = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!osf_attrs[OSF_ATTR_FINGER])
		return -EINVAL;

	if (!(nlh->nlmsg_flags & NLM_F_CREATE))
		return -EINVAL;

	f = nla_data(osf_attrs[OSF_ATTR_FINGER]);

	kf = kmalloc(sizeof(struct nf_osf_finger), GFP_KERNEL);
	if (!kf)
		return -ENOMEM;

	memcpy(&kf->finger, f, sizeof(struct nf_osf_user_finger));

	list_for_each_entry(sf, &nf_osf_fingers[!!f->df], finger_entry) {
		if (memcmp(&sf->finger, f, sizeof(struct nf_osf_user_finger)))
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
		list_add_tail_rcu(&kf->finger_entry, &nf_osf_fingers[!!f->df]);

	return err;
}

static int nfnl_osf_remove_callback(struct net *net, struct sock *ctnl,
				    struct sk_buff *skb,
				    const struct nlmsghdr *nlh,
				    const struct nlattr * const osf_attrs[],
				    struct netlink_ext_ack *extack)
{
	struct nf_osf_user_finger *f;
	struct nf_osf_finger *sf;
	int err = -ENOENT;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!osf_attrs[OSF_ATTR_FINGER])
		return -EINVAL;

	f = nla_data(osf_attrs[OSF_ATTR_FINGER]);

	list_for_each_entry(sf, &nf_osf_fingers[!!f->df], finger_entry) {
		if (memcmp(&sf->finger, f, sizeof(struct nf_osf_user_finger)))
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

static const struct nfnl_callback nfnl_osf_callbacks[OSF_MSG_MAX] = {
	[OSF_MSG_ADD]	= {
		.call		= nfnl_osf_add_callback,
		.attr_count	= OSF_ATTR_MAX,
		.policy		= nfnl_osf_policy,
	},
	[OSF_MSG_REMOVE]	= {
		.call		= nfnl_osf_remove_callback,
		.attr_count	= OSF_ATTR_MAX,
		.policy		= nfnl_osf_policy,
	},
};

static const struct nfnetlink_subsystem nfnl_osf_subsys = {
	.name			= "osf",
	.subsys_id		= NFNL_SUBSYS_OSF,
	.cb_count		= OSF_MSG_MAX,
	.cb			= nfnl_osf_callbacks,
};

static int __init nfnl_osf_init(void)
{
	int err = -EINVAL;
	int i;

	for (i = 0; i < ARRAY_SIZE(nf_osf_fingers); ++i)
		INIT_LIST_HEAD(&nf_osf_fingers[i]);

	err = nfnetlink_subsys_register(&nfnl_osf_subsys);
	if (err < 0) {
		pr_err("Failed to register OSF nsfnetlink helper (%d)\n", err);
		goto err_out_exit;
	}
	return 0;

err_out_exit:
	return err;
}

static void __exit nfnl_osf_fini(void)
{
	struct nf_osf_finger *f;
	int i;

	nfnetlink_subsys_unregister(&nfnl_osf_subsys);

	rcu_read_lock();
	for (i = 0; i < ARRAY_SIZE(nf_osf_fingers); ++i) {
		list_for_each_entry_rcu(f, &nf_osf_fingers[i], finger_entry) {
			list_del_rcu(&f->finger_entry);
			kfree_rcu(f, rcu_head);
		}
	}
	rcu_read_unlock();

	rcu_barrier();
}

module_init(nfnl_osf_init);
module_exit(nfnl_osf_fini);

MODULE_LICENSE("GPL");
