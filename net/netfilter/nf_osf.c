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
#include <linux/netfilter/nf_osf.h>

static inline int nf_osf_ttl(const struct sk_buff *skb,
			     int ttl_check, unsigned char f_ttl)
{
	const struct iphdr *ip = ip_hdr(skb);

	if (ttl_check != -1) {
		if (ttl_check == NF_OSF_TTL_TRUE)
			return ip->ttl == f_ttl;
		if (ttl_check == NF_OSF_TTL_NOCHECK)
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

	ttl_check = (info->flags & NF_OSF_TTL) ? info->ttl : -1;

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

MODULE_LICENSE("GPL");
