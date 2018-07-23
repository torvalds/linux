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
			     const struct nf_osf_info *info,
			     unsigned char f_ttl)
{
	const struct iphdr *ip = ip_hdr(skb);

	if (info->flags & NF_OSF_TTL) {
		if (info->ttl == NF_OSF_TTL_TRUE)
			return ip->ttl == f_ttl;
		if (info->ttl == NF_OSF_TTL_NOCHECK)
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

bool
nf_osf_match(const struct sk_buff *skb, u_int8_t family,
	     int hooknum, struct net_device *in, struct net_device *out,
	     const struct nf_osf_info *info, struct net *net,
	     const struct list_head *nf_osf_fingers)
{
	const unsigned char *optp = NULL, *_optp = NULL;
	unsigned int optsize = 0, check_WSS = 0;
	int fmatch = FMATCH_WRONG, fcount = 0;
	const struct iphdr *ip = ip_hdr(skb);
	const struct nf_osf_user_finger *f;
	unsigned char opts[MAX_IPOPTLEN];
	const struct nf_osf_finger *kf;
	u16 window, totlen, mss = 0;
	const struct tcphdr *tcp;
	struct tcphdr _tcph;
	bool df;

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

	list_for_each_entry_rcu(kf, &nf_osf_fingers[df], finger_entry) {
		int foptsize, optnum;

		f = &kf->finger;

		if (!(info->flags & NF_OSF_LOG) && strcmp(info->genre, f->genre))
			continue;

		optp = _optp;
		fmatch = FMATCH_WRONG;

		if (totlen != f->ss || !nf_osf_ttl(skb, info, f->ttl))
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
