/*
 *	Linux NET3:	Internet Group Management Protocol  [IGMP]
 *
 *	This code implements the IGMP protocol as defined in RFC1112. There has
 *	been a further revision of this protocol since which is now supported.
 *
 *	If you have trouble with this module be careful what gcc you have used,
 *	the older version didn't come out right using gcc 2.5.8, the newer one
 *	seems to fall out with gcc 2.6.2.
 *
 *	Authors:
 *		Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *
 *		Alan Cox	:	Added lots of __inline__ to optimise
 *					the memory usage of all the tiny little
 *					functions.
 *		Alan Cox	:	Dumped the header building experiment.
 *		Alan Cox	:	Minor tweaks ready for multicast routing
 *					and extended IGMP protocol.
 *		Alan Cox	:	Removed a load of inline directives. Gcc 2.5.8
 *					writes utterly bogus code otherwise (sigh)
 *					fixed IGMP loopback to behave in the manner
 *					desired by mrouted, fixed the fact it has been
 *					broken since 1.3.6 and cleaned up a few minor
 *					points.
 *
 *		Chih-Jen Chang	:	Tried to revise IGMP to Version 2
 *		Tsu-Sheng Tsao		E-mail: chihjenc@scf.usc.edu and tsusheng@scf.usc.edu
 *					The enhancements are mainly based on Steve Deering's
 * 					ipmulti-3.5 source code.
 *		Chih-Jen Chang	:	Added the igmp_get_mrouter_info and
 *		Tsu-Sheng Tsao		igmp_set_mrouter_info to keep track of
 *					the mrouted version on that device.
 *		Chih-Jen Chang	:	Added the max_resp_time parameter to
 *		Tsu-Sheng Tsao		igmp_heard_query(). Using this parameter
 *					to identify the multicast router version
 *					and do what the IGMP version 2 specified.
 *		Chih-Jen Chang	:	Added a timer to revert to IGMP V2 router
 *		Tsu-Sheng Tsao		if the specified time expired.
 *		Alan Cox	:	Stop IGMP from 0.0.0.0 being accepted.
 *		Alan Cox	:	Use GFP_ATOMIC in the right places.
 *		Christian Daudt :	igmp timer wasn't set for local group
 *					memberships but was being deleted,
 *					which caused a "del_timer() called
 *					from %p with timer not initialized\n"
 *					message (960131).
 *		Christian Daudt :	removed del_timer from
 *					igmp_timer_expire function (960205).
 *             Christian Daudt :       igmp_heard_report now only calls
 *                                     igmp_timer_expire if tm->running is
 *                                     true (960216).
 *		Malcolm Beattie :	ttl comparison wrong in igmp_rcv made
 *					igmp_heard_query never trigger. Expiry
 *					miscalculation fixed in igmp_heard_query
 *					and random() made to return unsigned to
 *					prevent negative expiry times.
 *		Alexey Kuznetsov:	Wrong group leaving behaviour, backport
 *					fix from pending 2.1.x patches.
 *		Alan Cox:		Forget to enable FDDI support earlier.
 *		Alexey Kuznetsov:	Fixed leaving groups on device down.
 *		Alexey Kuznetsov:	Accordance to igmp-v2-06 draft.
 *		David L Stevens:	IGMPv3 support, with help from
 *					Vinay Kulkarni
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/if_arp.h>
#include <linux/rtnetlink.h>
#include <linux/times.h>

#include <net/net_namespace.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>
#ifdef CONFIG_IP_MROUTE
#include <linux/mroute.h>
#endif
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#endif

#define IP_MAX_MEMBERSHIPS	20
#define IP_MAX_MSF		10

#ifdef CONFIG_IP_MULTICAST
/* Parameter names and values are taken from igmp-v2-06 draft */

#define IGMP_V1_Router_Present_Timeout		(400*HZ)
#define IGMP_V2_Router_Present_Timeout		(400*HZ)
#define IGMP_Unsolicited_Report_Interval	(10*HZ)
#define IGMP_Query_Response_Interval		(10*HZ)
#define IGMP_Unsolicited_Report_Count		2


#define IGMP_Initial_Report_Delay		(1)

/* IGMP_Initial_Report_Delay is not from IGMP specs!
 * IGMP specs require to report membership immediately after
 * joining a group, but we delay the first report by a
 * small interval. It seems more natural and still does not
 * contradict to specs provided this delay is small enough.
 */

#define IGMP_V1_SEEN(in_dev) \
	(IPV4_DEVCONF_ALL(dev_net(in_dev->dev), FORCE_IGMP_VERSION) == 1 || \
	 IN_DEV_CONF_GET((in_dev), FORCE_IGMP_VERSION) == 1 || \
	 ((in_dev)->mr_v1_seen && \
	  time_before(jiffies, (in_dev)->mr_v1_seen)))
#define IGMP_V2_SEEN(in_dev) \
	(IPV4_DEVCONF_ALL(dev_net(in_dev->dev), FORCE_IGMP_VERSION) == 2 || \
	 IN_DEV_CONF_GET((in_dev), FORCE_IGMP_VERSION) == 2 || \
	 ((in_dev)->mr_v2_seen && \
	  time_before(jiffies, (in_dev)->mr_v2_seen)))

static void igmpv3_add_delrec(struct in_device *in_dev, struct ip_mc_list *im);
static void igmpv3_del_delrec(struct in_device *in_dev, __be32 multiaddr);
static void igmpv3_clear_delrec(struct in_device *in_dev);
static int sf_setstate(struct ip_mc_list *pmc);
static void sf_markstate(struct ip_mc_list *pmc);
#endif
static void ip_mc_clear_src(struct ip_mc_list *pmc);
static int ip_mc_add_src(struct in_device *in_dev, __be32 *pmca, int sfmode,
			 int sfcount, __be32 *psfsrc, int delta);

static void ip_ma_put(struct ip_mc_list *im)
{
	if (atomic_dec_and_test(&im->refcnt)) {
		in_dev_put(im->interface);
		kfree(im);
	}
}

#ifdef CONFIG_IP_MULTICAST

/*
 *	Timer management
 */

static __inline__ void igmp_stop_timer(struct ip_mc_list *im)
{
	spin_lock_bh(&im->lock);
	if (del_timer(&im->timer))
		atomic_dec(&im->refcnt);
	im->tm_running = 0;
	im->reporter = 0;
	im->unsolicit_count = 0;
	spin_unlock_bh(&im->lock);
}

/* It must be called with locked im->lock */
static void igmp_start_timer(struct ip_mc_list *im, int max_delay)
{
	int tv = net_random() % max_delay;

	im->tm_running = 1;
	if (!mod_timer(&im->timer, jiffies+tv+2))
		atomic_inc(&im->refcnt);
}

static void igmp_gq_start_timer(struct in_device *in_dev)
{
	int tv = net_random() % in_dev->mr_maxdelay;

	in_dev->mr_gq_running = 1;
	if (!mod_timer(&in_dev->mr_gq_timer, jiffies+tv+2))
		in_dev_hold(in_dev);
}

static void igmp_ifc_start_timer(struct in_device *in_dev, int delay)
{
	int tv = net_random() % delay;

	if (!mod_timer(&in_dev->mr_ifc_timer, jiffies+tv+2))
		in_dev_hold(in_dev);
}

static void igmp_mod_timer(struct ip_mc_list *im, int max_delay)
{
	spin_lock_bh(&im->lock);
	im->unsolicit_count = 0;
	if (del_timer(&im->timer)) {
		if ((long)(im->timer.expires-jiffies) < max_delay) {
			add_timer(&im->timer);
			im->tm_running = 1;
			spin_unlock_bh(&im->lock);
			return;
		}
		atomic_dec(&im->refcnt);
	}
	igmp_start_timer(im, max_delay);
	spin_unlock_bh(&im->lock);
}


/*
 *	Send an IGMP report.
 */

#define IGMP_SIZE (sizeof(struct igmphdr)+sizeof(struct iphdr)+4)


static int is_in(struct ip_mc_list *pmc, struct ip_sf_list *psf, int type,
	int gdeleted, int sdeleted)
{
	switch (type) {
	case IGMPV3_MODE_IS_INCLUDE:
	case IGMPV3_MODE_IS_EXCLUDE:
		if (gdeleted || sdeleted)
			return 0;
		if (!(pmc->gsquery && !psf->sf_gsresp)) {
			if (pmc->sfmode == MCAST_INCLUDE)
				return 1;
			/* don't include if this source is excluded
			 * in all filters
			 */
			if (psf->sf_count[MCAST_INCLUDE])
				return type == IGMPV3_MODE_IS_INCLUDE;
			return pmc->sfcount[MCAST_EXCLUDE] ==
				psf->sf_count[MCAST_EXCLUDE];
		}
		return 0;
	case IGMPV3_CHANGE_TO_INCLUDE:
		if (gdeleted || sdeleted)
			return 0;
		return psf->sf_count[MCAST_INCLUDE] != 0;
	case IGMPV3_CHANGE_TO_EXCLUDE:
		if (gdeleted || sdeleted)
			return 0;
		if (pmc->sfcount[MCAST_EXCLUDE] == 0 ||
		    psf->sf_count[MCAST_INCLUDE])
			return 0;
		return pmc->sfcount[MCAST_EXCLUDE] ==
			psf->sf_count[MCAST_EXCLUDE];
	case IGMPV3_ALLOW_NEW_SOURCES:
		if (gdeleted || !psf->sf_crcount)
			return 0;
		return (pmc->sfmode == MCAST_INCLUDE) ^ sdeleted;
	case IGMPV3_BLOCK_OLD_SOURCES:
		if (pmc->sfmode == MCAST_INCLUDE)
			return gdeleted || (psf->sf_crcount && sdeleted);
		return psf->sf_crcount && !gdeleted && !sdeleted;
	}
	return 0;
}

static int
igmp_scount(struct ip_mc_list *pmc, int type, int gdeleted, int sdeleted)
{
	struct ip_sf_list *psf;
	int scount = 0;

	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (!is_in(pmc, psf, type, gdeleted, sdeleted))
			continue;
		scount++;
	}
	return scount;
}

static struct sk_buff *igmpv3_newpack(struct net_device *dev, int size)
{
	struct sk_buff *skb;
	struct rtable *rt;
	struct iphdr *pip;
	struct igmpv3_report *pig;
	struct net *net = dev_net(dev);

	skb = alloc_skb(size + LL_ALLOCATED_SPACE(dev), GFP_ATOMIC);
	if (skb == NULL)
		return NULL;

	{
		struct flowi fl = { .oif = dev->ifindex,
				    .nl_u = { .ip4_u = {
				    .daddr = IGMPV3_ALL_MCR } },
				    .proto = IPPROTO_IGMP };
		if (ip_route_output_key(net, &rt, &fl)) {
			kfree_skb(skb);
			return NULL;
		}
	}
	if (rt->rt_src == 0) {
		kfree_skb(skb);
		ip_rt_put(rt);
		return NULL;
	}

	skb_dst_set(skb, &rt->u.dst);
	skb->dev = dev;

	skb_reserve(skb, LL_RESERVED_SPACE(dev));

	skb_reset_network_header(skb);
	pip = ip_hdr(skb);
	skb_put(skb, sizeof(struct iphdr) + 4);

	pip->version  = 4;
	pip->ihl      = (sizeof(struct iphdr)+4)>>2;
	pip->tos      = 0xc0;
	pip->frag_off = htons(IP_DF);
	pip->ttl      = 1;
	pip->daddr    = rt->rt_dst;
	pip->saddr    = rt->rt_src;
	pip->protocol = IPPROTO_IGMP;
	pip->tot_len  = 0;	/* filled in later */
	ip_select_ident(pip, &rt->u.dst, NULL);
	((u8*)&pip[1])[0] = IPOPT_RA;
	((u8*)&pip[1])[1] = 4;
	((u8*)&pip[1])[2] = 0;
	((u8*)&pip[1])[3] = 0;

	skb->transport_header = skb->network_header + sizeof(struct iphdr) + 4;
	skb_put(skb, sizeof(*pig));
	pig = igmpv3_report_hdr(skb);
	pig->type = IGMPV3_HOST_MEMBERSHIP_REPORT;
	pig->resv1 = 0;
	pig->csum = 0;
	pig->resv2 = 0;
	pig->ngrec = 0;
	return skb;
}

static int igmpv3_sendpack(struct sk_buff *skb)
{
	struct igmphdr *pig = igmp_hdr(skb);
	const int igmplen = skb->tail - skb->transport_header;

	pig->csum = ip_compute_csum(igmp_hdr(skb), igmplen);

	return ip_local_out(skb);
}

static int grec_size(struct ip_mc_list *pmc, int type, int gdel, int sdel)
{
	return sizeof(struct igmpv3_grec) + 4*igmp_scount(pmc, type, gdel, sdel);
}

static struct sk_buff *add_grhead(struct sk_buff *skb, struct ip_mc_list *pmc,
	int type, struct igmpv3_grec **ppgr)
{
	struct net_device *dev = pmc->interface->dev;
	struct igmpv3_report *pih;
	struct igmpv3_grec *pgr;

	if (!skb)
		skb = igmpv3_newpack(dev, dev->mtu);
	if (!skb)
		return NULL;
	pgr = (struct igmpv3_grec *)skb_put(skb, sizeof(struct igmpv3_grec));
	pgr->grec_type = type;
	pgr->grec_auxwords = 0;
	pgr->grec_nsrcs = 0;
	pgr->grec_mca = pmc->multiaddr;
	pih = igmpv3_report_hdr(skb);
	pih->ngrec = htons(ntohs(pih->ngrec)+1);
	*ppgr = pgr;
	return skb;
}

#define AVAILABLE(skb) ((skb) ? ((skb)->dev ? (skb)->dev->mtu - (skb)->len : \
	skb_tailroom(skb)) : 0)

static struct sk_buff *add_grec(struct sk_buff *skb, struct ip_mc_list *pmc,
	int type, int gdeleted, int sdeleted)
{
	struct net_device *dev = pmc->interface->dev;
	struct igmpv3_report *pih;
	struct igmpv3_grec *pgr = NULL;
	struct ip_sf_list *psf, *psf_next, *psf_prev, **psf_list;
	int scount, stotal, first, isquery, truncate;

	if (pmc->multiaddr == IGMP_ALL_HOSTS)
		return skb;

	isquery = type == IGMPV3_MODE_IS_INCLUDE ||
		  type == IGMPV3_MODE_IS_EXCLUDE;
	truncate = type == IGMPV3_MODE_IS_EXCLUDE ||
		    type == IGMPV3_CHANGE_TO_EXCLUDE;

	stotal = scount = 0;

	psf_list = sdeleted ? &pmc->tomb : &pmc->sources;

	if (!*psf_list)
		goto empty_source;

	pih = skb ? igmpv3_report_hdr(skb) : NULL;

	/* EX and TO_EX get a fresh packet, if needed */
	if (truncate) {
		if (pih && pih->ngrec &&
		    AVAILABLE(skb) < grec_size(pmc, type, gdeleted, sdeleted)) {
			if (skb)
				igmpv3_sendpack(skb);
			skb = igmpv3_newpack(dev, dev->mtu);
		}
	}
	first = 1;
	psf_prev = NULL;
	for (psf=*psf_list; psf; psf=psf_next) {
		__be32 *psrc;

		psf_next = psf->sf_next;

		if (!is_in(pmc, psf, type, gdeleted, sdeleted)) {
			psf_prev = psf;
			continue;
		}

		/* clear marks on query responses */
		if (isquery)
			psf->sf_gsresp = 0;

		if (AVAILABLE(skb) < sizeof(__be32) +
		    first*sizeof(struct igmpv3_grec)) {
			if (truncate && !first)
				break;	 /* truncate these */
			if (pgr)
				pgr->grec_nsrcs = htons(scount);
			if (skb)
				igmpv3_sendpack(skb);
			skb = igmpv3_newpack(dev, dev->mtu);
			first = 1;
			scount = 0;
		}
		if (first) {
			skb = add_grhead(skb, pmc, type, &pgr);
			first = 0;
		}
		if (!skb)
			return NULL;
		psrc = (__be32 *)skb_put(skb, sizeof(__be32));
		*psrc = psf->sf_inaddr;
		scount++; stotal++;
		if ((type == IGMPV3_ALLOW_NEW_SOURCES ||
		     type == IGMPV3_BLOCK_OLD_SOURCES) && psf->sf_crcount) {
			psf->sf_crcount--;
			if ((sdeleted || gdeleted) && psf->sf_crcount == 0) {
				if (psf_prev)
					psf_prev->sf_next = psf->sf_next;
				else
					*psf_list = psf->sf_next;
				kfree(psf);
				continue;
			}
		}
		psf_prev = psf;
	}

empty_source:
	if (!stotal) {
		if (type == IGMPV3_ALLOW_NEW_SOURCES ||
		    type == IGMPV3_BLOCK_OLD_SOURCES)
			return skb;
		if (pmc->crcount || isquery) {
			/* make sure we have room for group header */
			if (skb && AVAILABLE(skb)<sizeof(struct igmpv3_grec)) {
				igmpv3_sendpack(skb);
				skb = NULL; /* add_grhead will get a new one */
			}
			skb = add_grhead(skb, pmc, type, &pgr);
		}
	}
	if (pgr)
		pgr->grec_nsrcs = htons(scount);

	if (isquery)
		pmc->gsquery = 0;	/* clear query state on report */
	return skb;
}

static int igmpv3_send_report(struct in_device *in_dev, struct ip_mc_list *pmc)
{
	struct sk_buff *skb = NULL;
	int type;

	if (!pmc) {
		read_lock(&in_dev->mc_list_lock);
		for (pmc=in_dev->mc_list; pmc; pmc=pmc->next) {
			if (pmc->multiaddr == IGMP_ALL_HOSTS)
				continue;
			spin_lock_bh(&pmc->lock);
			if (pmc->sfcount[MCAST_EXCLUDE])
				type = IGMPV3_MODE_IS_EXCLUDE;
			else
				type = IGMPV3_MODE_IS_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0);
			spin_unlock_bh(&pmc->lock);
		}
		read_unlock(&in_dev->mc_list_lock);
	} else {
		spin_lock_bh(&pmc->lock);
		if (pmc->sfcount[MCAST_EXCLUDE])
			type = IGMPV3_MODE_IS_EXCLUDE;
		else
			type = IGMPV3_MODE_IS_INCLUDE;
		skb = add_grec(skb, pmc, type, 0, 0);
		spin_unlock_bh(&pmc->lock);
	}
	if (!skb)
		return 0;
	return igmpv3_sendpack(skb);
}

/*
 * remove zero-count source records from a source filter list
 */
static void igmpv3_clear_zeros(struct ip_sf_list **ppsf)
{
	struct ip_sf_list *psf_prev, *psf_next, *psf;

	psf_prev = NULL;
	for (psf=*ppsf; psf; psf = psf_next) {
		psf_next = psf->sf_next;
		if (psf->sf_crcount == 0) {
			if (psf_prev)
				psf_prev->sf_next = psf->sf_next;
			else
				*ppsf = psf->sf_next;
			kfree(psf);
		} else
			psf_prev = psf;
	}
}

static void igmpv3_send_cr(struct in_device *in_dev)
{
	struct ip_mc_list *pmc, *pmc_prev, *pmc_next;
	struct sk_buff *skb = NULL;
	int type, dtype;

	read_lock(&in_dev->mc_list_lock);
	spin_lock_bh(&in_dev->mc_tomb_lock);

	/* deleted MCA's */
	pmc_prev = NULL;
	for (pmc=in_dev->mc_tomb; pmc; pmc=pmc_next) {
		pmc_next = pmc->next;
		if (pmc->sfmode == MCAST_INCLUDE) {
			type = IGMPV3_BLOCK_OLD_SOURCES;
			dtype = IGMPV3_BLOCK_OLD_SOURCES;
			skb = add_grec(skb, pmc, type, 1, 0);
			skb = add_grec(skb, pmc, dtype, 1, 1);
		}
		if (pmc->crcount) {
			if (pmc->sfmode == MCAST_EXCLUDE) {
				type = IGMPV3_CHANGE_TO_INCLUDE;
				skb = add_grec(skb, pmc, type, 1, 0);
			}
			pmc->crcount--;
			if (pmc->crcount == 0) {
				igmpv3_clear_zeros(&pmc->tomb);
				igmpv3_clear_zeros(&pmc->sources);
			}
		}
		if (pmc->crcount == 0 && !pmc->tomb && !pmc->sources) {
			if (pmc_prev)
				pmc_prev->next = pmc_next;
			else
				in_dev->mc_tomb = pmc_next;
			in_dev_put(pmc->interface);
			kfree(pmc);
		} else
			pmc_prev = pmc;
	}
	spin_unlock_bh(&in_dev->mc_tomb_lock);

	/* change recs */
	for (pmc=in_dev->mc_list; pmc; pmc=pmc->next) {
		spin_lock_bh(&pmc->lock);
		if (pmc->sfcount[MCAST_EXCLUDE]) {
			type = IGMPV3_BLOCK_OLD_SOURCES;
			dtype = IGMPV3_ALLOW_NEW_SOURCES;
		} else {
			type = IGMPV3_ALLOW_NEW_SOURCES;
			dtype = IGMPV3_BLOCK_OLD_SOURCES;
		}
		skb = add_grec(skb, pmc, type, 0, 0);
		skb = add_grec(skb, pmc, dtype, 0, 1);	/* deleted sources */

		/* filter mode changes */
		if (pmc->crcount) {
			if (pmc->sfmode == MCAST_EXCLUDE)
				type = IGMPV3_CHANGE_TO_EXCLUDE;
			else
				type = IGMPV3_CHANGE_TO_INCLUDE;
			skb = add_grec(skb, pmc, type, 0, 0);
			pmc->crcount--;
		}
		spin_unlock_bh(&pmc->lock);
	}
	read_unlock(&in_dev->mc_list_lock);

	if (!skb)
		return;
	(void) igmpv3_sendpack(skb);
}

static int igmp_send_report(struct in_device *in_dev, struct ip_mc_list *pmc,
	int type)
{
	struct sk_buff *skb;
	struct iphdr *iph;
	struct igmphdr *ih;
	struct rtable *rt;
	struct net_device *dev = in_dev->dev;
	struct net *net = dev_net(dev);
	__be32	group = pmc ? pmc->multiaddr : 0;
	__be32	dst;

	if (type == IGMPV3_HOST_MEMBERSHIP_REPORT)
		return igmpv3_send_report(in_dev, pmc);
	else if (type == IGMP_HOST_LEAVE_MESSAGE)
		dst = IGMP_ALL_ROUTER;
	else
		dst = group;

	{
		struct flowi fl = { .oif = dev->ifindex,
				    .nl_u = { .ip4_u = { .daddr = dst } },
				    .proto = IPPROTO_IGMP };
		if (ip_route_output_key(net, &rt, &fl))
			return -1;
	}
	if (rt->rt_src == 0) {
		ip_rt_put(rt);
		return -1;
	}

	skb = alloc_skb(IGMP_SIZE+LL_ALLOCATED_SPACE(dev), GFP_ATOMIC);
	if (skb == NULL) {
		ip_rt_put(rt);
		return -1;
	}

	skb_dst_set(skb, &rt->u.dst);

	skb_reserve(skb, LL_RESERVED_SPACE(dev));

	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	skb_put(skb, sizeof(struct iphdr) + 4);

	iph->version  = 4;
	iph->ihl      = (sizeof(struct iphdr)+4)>>2;
	iph->tos      = 0xc0;
	iph->frag_off = htons(IP_DF);
	iph->ttl      = 1;
	iph->daddr    = dst;
	iph->saddr    = rt->rt_src;
	iph->protocol = IPPROTO_IGMP;
	ip_select_ident(iph, &rt->u.dst, NULL);
	((u8*)&iph[1])[0] = IPOPT_RA;
	((u8*)&iph[1])[1] = 4;
	((u8*)&iph[1])[2] = 0;
	((u8*)&iph[1])[3] = 0;

	ih = (struct igmphdr *)skb_put(skb, sizeof(struct igmphdr));
	ih->type = type;
	ih->code = 0;
	ih->csum = 0;
	ih->group = group;
	ih->csum = ip_compute_csum((void *)ih, sizeof(struct igmphdr));

	return ip_local_out(skb);
}

static void igmp_gq_timer_expire(unsigned long data)
{
	struct in_device *in_dev = (struct in_device *)data;

	in_dev->mr_gq_running = 0;
	igmpv3_send_report(in_dev, NULL);
	__in_dev_put(in_dev);
}

static void igmp_ifc_timer_expire(unsigned long data)
{
	struct in_device *in_dev = (struct in_device *)data;

	igmpv3_send_cr(in_dev);
	if (in_dev->mr_ifc_count) {
		in_dev->mr_ifc_count--;
		igmp_ifc_start_timer(in_dev, IGMP_Unsolicited_Report_Interval);
	}
	__in_dev_put(in_dev);
}

static void igmp_ifc_event(struct in_device *in_dev)
{
	if (IGMP_V1_SEEN(in_dev) || IGMP_V2_SEEN(in_dev))
		return;
	in_dev->mr_ifc_count = in_dev->mr_qrv ? in_dev->mr_qrv :
		IGMP_Unsolicited_Report_Count;
	igmp_ifc_start_timer(in_dev, 1);
}


static void igmp_timer_expire(unsigned long data)
{
	struct ip_mc_list *im=(struct ip_mc_list *)data;
	struct in_device *in_dev = im->interface;

	spin_lock(&im->lock);
	im->tm_running = 0;

	if (im->unsolicit_count) {
		im->unsolicit_count--;
		igmp_start_timer(im, IGMP_Unsolicited_Report_Interval);
	}
	im->reporter = 1;
	spin_unlock(&im->lock);

	if (IGMP_V1_SEEN(in_dev))
		igmp_send_report(in_dev, im, IGMP_HOST_MEMBERSHIP_REPORT);
	else if (IGMP_V2_SEEN(in_dev))
		igmp_send_report(in_dev, im, IGMPV2_HOST_MEMBERSHIP_REPORT);
	else
		igmp_send_report(in_dev, im, IGMPV3_HOST_MEMBERSHIP_REPORT);

	ip_ma_put(im);
}

/* mark EXCLUDE-mode sources */
static int igmp_xmarksources(struct ip_mc_list *pmc, int nsrcs, __be32 *srcs)
{
	struct ip_sf_list *psf;
	int i, scount;

	scount = 0;
	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (scount == nsrcs)
			break;
		for (i=0; i<nsrcs; i++) {
			/* skip inactive filters */
			if (pmc->sfcount[MCAST_INCLUDE] ||
			    pmc->sfcount[MCAST_EXCLUDE] !=
			    psf->sf_count[MCAST_EXCLUDE])
				continue;
			if (srcs[i] == psf->sf_inaddr) {
				scount++;
				break;
			}
		}
	}
	pmc->gsquery = 0;
	if (scount == nsrcs)	/* all sources excluded */
		return 0;
	return 1;
}

static int igmp_marksources(struct ip_mc_list *pmc, int nsrcs, __be32 *srcs)
{
	struct ip_sf_list *psf;
	int i, scount;

	if (pmc->sfmode == MCAST_EXCLUDE)
		return igmp_xmarksources(pmc, nsrcs, srcs);

	/* mark INCLUDE-mode sources */
	scount = 0;
	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (scount == nsrcs)
			break;
		for (i=0; i<nsrcs; i++)
			if (srcs[i] == psf->sf_inaddr) {
				psf->sf_gsresp = 1;
				scount++;
				break;
			}
	}
	if (!scount) {
		pmc->gsquery = 0;
		return 0;
	}
	pmc->gsquery = 1;
	return 1;
}

static void igmp_heard_report(struct in_device *in_dev, __be32 group)
{
	struct ip_mc_list *im;

	/* Timers are only set for non-local groups */

	if (group == IGMP_ALL_HOSTS)
		return;

	read_lock(&in_dev->mc_list_lock);
	for (im=in_dev->mc_list; im!=NULL; im=im->next) {
		if (im->multiaddr == group) {
			igmp_stop_timer(im);
			break;
		}
	}
	read_unlock(&in_dev->mc_list_lock);
}

static void igmp_heard_query(struct in_device *in_dev, struct sk_buff *skb,
	int len)
{
	struct igmphdr 		*ih = igmp_hdr(skb);
	struct igmpv3_query *ih3 = igmpv3_query_hdr(skb);
	struct ip_mc_list	*im;
	__be32			group = ih->group;
	int			max_delay;
	int			mark = 0;


	if (len == 8) {
		if (ih->code == 0) {
			/* Alas, old v1 router presents here. */

			max_delay = IGMP_Query_Response_Interval;
			in_dev->mr_v1_seen = jiffies +
				IGMP_V1_Router_Present_Timeout;
			group = 0;
		} else {
			/* v2 router present */
			max_delay = ih->code*(HZ/IGMP_TIMER_SCALE);
			in_dev->mr_v2_seen = jiffies +
				IGMP_V2_Router_Present_Timeout;
		}
		/* cancel the interface change timer */
		in_dev->mr_ifc_count = 0;
		if (del_timer(&in_dev->mr_ifc_timer))
			__in_dev_put(in_dev);
		/* clear deleted report items */
		igmpv3_clear_delrec(in_dev);
	} else if (len < 12) {
		return;	/* ignore bogus packet; freed by caller */
	} else { /* v3 */
		if (!pskb_may_pull(skb, sizeof(struct igmpv3_query)))
			return;

		ih3 = igmpv3_query_hdr(skb);
		if (ih3->nsrcs) {
			if (!pskb_may_pull(skb, sizeof(struct igmpv3_query)
					   + ntohs(ih3->nsrcs)*sizeof(__be32)))
				return;
			ih3 = igmpv3_query_hdr(skb);
		}

		max_delay = IGMPV3_MRC(ih3->code)*(HZ/IGMP_TIMER_SCALE);
		if (!max_delay)
			max_delay = 1;	/* can't mod w/ 0 */
		in_dev->mr_maxdelay = max_delay;
		if (ih3->qrv)
			in_dev->mr_qrv = ih3->qrv;
		if (!group) { /* general query */
			if (ih3->nsrcs)
				return;	/* no sources allowed */
			igmp_gq_start_timer(in_dev);
			return;
		}
		/* mark sources to include, if group & source-specific */
		mark = ih3->nsrcs != 0;
	}

	/*
	 * - Start the timers in all of our membership records
	 *   that the query applies to for the interface on
	 *   which the query arrived excl. those that belong
	 *   to a "local" group (224.0.0.X)
	 * - For timers already running check if they need to
	 *   be reset.
	 * - Use the igmp->igmp_code field as the maximum
	 *   delay possible
	 */
	read_lock(&in_dev->mc_list_lock);
	for (im=in_dev->mc_list; im!=NULL; im=im->next) {
		int changed;

		if (group && group != im->multiaddr)
			continue;
		if (im->multiaddr == IGMP_ALL_HOSTS)
			continue;
		spin_lock_bh(&im->lock);
		if (im->tm_running)
			im->gsquery = im->gsquery && mark;
		else
			im->gsquery = mark;
		changed = !im->gsquery ||
			igmp_marksources(im, ntohs(ih3->nsrcs), ih3->srcs);
		spin_unlock_bh(&im->lock);
		if (changed)
			igmp_mod_timer(im, max_delay);
	}
	read_unlock(&in_dev->mc_list_lock);
}

int igmp_rcv(struct sk_buff *skb)
{
	/* This basically follows the spec line by line -- see RFC1112 */
	struct igmphdr *ih;
	struct in_device *in_dev = in_dev_get(skb->dev);
	int len = skb->len;

	if (in_dev == NULL)
		goto drop;

	if (!pskb_may_pull(skb, sizeof(struct igmphdr)))
		goto drop_ref;

	switch (skb->ip_summed) {
	case CHECKSUM_COMPLETE:
		if (!csum_fold(skb->csum))
			break;
		/* fall through */
	case CHECKSUM_NONE:
		skb->csum = 0;
		if (__skb_checksum_complete(skb))
			goto drop_ref;
	}

	ih = igmp_hdr(skb);
	switch (ih->type) {
	case IGMP_HOST_MEMBERSHIP_QUERY:
		igmp_heard_query(in_dev, skb, len);
		break;
	case IGMP_HOST_MEMBERSHIP_REPORT:
	case IGMPV2_HOST_MEMBERSHIP_REPORT:
		/* Is it our report looped back? */
		if (skb_rtable(skb)->fl.iif == 0)
			break;
		/* don't rely on MC router hearing unicast reports */
		if (skb->pkt_type == PACKET_MULTICAST ||
		    skb->pkt_type == PACKET_BROADCAST)
			igmp_heard_report(in_dev, ih->group);
		break;
	case IGMP_PIM:
#ifdef CONFIG_IP_PIMSM_V1
		in_dev_put(in_dev);
		return pim_rcv_v1(skb);
#endif
	case IGMPV3_HOST_MEMBERSHIP_REPORT:
	case IGMP_DVMRP:
	case IGMP_TRACE:
	case IGMP_HOST_LEAVE_MESSAGE:
	case IGMP_MTRACE:
	case IGMP_MTRACE_RESP:
		break;
	default:
		break;
	}

drop_ref:
	in_dev_put(in_dev);
drop:
	kfree_skb(skb);
	return 0;
}

#endif


/*
 *	Add a filter to a device
 */

static void ip_mc_filter_add(struct in_device *in_dev, __be32 addr)
{
	char buf[MAX_ADDR_LEN];
	struct net_device *dev = in_dev->dev;

	/* Checking for IFF_MULTICAST here is WRONG-WRONG-WRONG.
	   We will get multicast token leakage, when IFF_MULTICAST
	   is changed. This check should be done in dev->set_multicast_list
	   routine. Something sort of:
	   if (dev->mc_list && dev->flags&IFF_MULTICAST) { do it; }
	   --ANK
	   */
	if (arp_mc_map(addr, buf, dev, 0) == 0)
		dev_mc_add(dev, buf, dev->addr_len, 0);
}

/*
 *	Remove a filter from a device
 */

static void ip_mc_filter_del(struct in_device *in_dev, __be32 addr)
{
	char buf[MAX_ADDR_LEN];
	struct net_device *dev = in_dev->dev;

	if (arp_mc_map(addr, buf, dev, 0) == 0)
		dev_mc_delete(dev, buf, dev->addr_len, 0);
}

#ifdef CONFIG_IP_MULTICAST
/*
 * deleted ip_mc_list manipulation
 */
static void igmpv3_add_delrec(struct in_device *in_dev, struct ip_mc_list *im)
{
	struct ip_mc_list *pmc;

	/* this is an "ip_mc_list" for convenience; only the fields below
	 * are actually used. In particular, the refcnt and users are not
	 * used for management of the delete list. Using the same structure
	 * for deleted items allows change reports to use common code with
	 * non-deleted or query-response MCA's.
	 */
	pmc = kzalloc(sizeof(*pmc), GFP_KERNEL);
	if (!pmc)
		return;
	spin_lock_bh(&im->lock);
	pmc->interface = im->interface;
	in_dev_hold(in_dev);
	pmc->multiaddr = im->multiaddr;
	pmc->crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
		IGMP_Unsolicited_Report_Count;
	pmc->sfmode = im->sfmode;
	if (pmc->sfmode == MCAST_INCLUDE) {
		struct ip_sf_list *psf;

		pmc->tomb = im->tomb;
		pmc->sources = im->sources;
		im->tomb = im->sources = NULL;
		for (psf=pmc->sources; psf; psf=psf->sf_next)
			psf->sf_crcount = pmc->crcount;
	}
	spin_unlock_bh(&im->lock);

	spin_lock_bh(&in_dev->mc_tomb_lock);
	pmc->next = in_dev->mc_tomb;
	in_dev->mc_tomb = pmc;
	spin_unlock_bh(&in_dev->mc_tomb_lock);
}

static void igmpv3_del_delrec(struct in_device *in_dev, __be32 multiaddr)
{
	struct ip_mc_list *pmc, *pmc_prev;
	struct ip_sf_list *psf, *psf_next;

	spin_lock_bh(&in_dev->mc_tomb_lock);
	pmc_prev = NULL;
	for (pmc=in_dev->mc_tomb; pmc; pmc=pmc->next) {
		if (pmc->multiaddr == multiaddr)
			break;
		pmc_prev = pmc;
	}
	if (pmc) {
		if (pmc_prev)
			pmc_prev->next = pmc->next;
		else
			in_dev->mc_tomb = pmc->next;
	}
	spin_unlock_bh(&in_dev->mc_tomb_lock);
	if (pmc) {
		for (psf=pmc->tomb; psf; psf=psf_next) {
			psf_next = psf->sf_next;
			kfree(psf);
		}
		in_dev_put(pmc->interface);
		kfree(pmc);
	}
}

static void igmpv3_clear_delrec(struct in_device *in_dev)
{
	struct ip_mc_list *pmc, *nextpmc;

	spin_lock_bh(&in_dev->mc_tomb_lock);
	pmc = in_dev->mc_tomb;
	in_dev->mc_tomb = NULL;
	spin_unlock_bh(&in_dev->mc_tomb_lock);

	for (; pmc; pmc = nextpmc) {
		nextpmc = pmc->next;
		ip_mc_clear_src(pmc);
		in_dev_put(pmc->interface);
		kfree(pmc);
	}
	/* clear dead sources, too */
	read_lock(&in_dev->mc_list_lock);
	for (pmc=in_dev->mc_list; pmc; pmc=pmc->next) {
		struct ip_sf_list *psf, *psf_next;

		spin_lock_bh(&pmc->lock);
		psf = pmc->tomb;
		pmc->tomb = NULL;
		spin_unlock_bh(&pmc->lock);
		for (; psf; psf=psf_next) {
			psf_next = psf->sf_next;
			kfree(psf);
		}
	}
	read_unlock(&in_dev->mc_list_lock);
}
#endif

static void igmp_group_dropped(struct ip_mc_list *im)
{
	struct in_device *in_dev = im->interface;
#ifdef CONFIG_IP_MULTICAST
	int reporter;
#endif

	if (im->loaded) {
		im->loaded = 0;
		ip_mc_filter_del(in_dev, im->multiaddr);
	}

#ifdef CONFIG_IP_MULTICAST
	if (im->multiaddr == IGMP_ALL_HOSTS)
		return;

	reporter = im->reporter;
	igmp_stop_timer(im);

	if (!in_dev->dead) {
		if (IGMP_V1_SEEN(in_dev))
			goto done;
		if (IGMP_V2_SEEN(in_dev)) {
			if (reporter)
				igmp_send_report(in_dev, im, IGMP_HOST_LEAVE_MESSAGE);
			goto done;
		}
		/* IGMPv3 */
		igmpv3_add_delrec(in_dev, im);

		igmp_ifc_event(in_dev);
	}
done:
#endif
	ip_mc_clear_src(im);
}

static void igmp_group_added(struct ip_mc_list *im)
{
	struct in_device *in_dev = im->interface;

	if (im->loaded == 0) {
		im->loaded = 1;
		ip_mc_filter_add(in_dev, im->multiaddr);
	}

#ifdef CONFIG_IP_MULTICAST
	if (im->multiaddr == IGMP_ALL_HOSTS)
		return;

	if (in_dev->dead)
		return;
	if (IGMP_V1_SEEN(in_dev) || IGMP_V2_SEEN(in_dev)) {
		spin_lock_bh(&im->lock);
		igmp_start_timer(im, IGMP_Initial_Report_Delay);
		spin_unlock_bh(&im->lock);
		return;
	}
	/* else, v3 */

	im->crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
		IGMP_Unsolicited_Report_Count;
	igmp_ifc_event(in_dev);
#endif
}


/*
 *	Multicast list managers
 */


/*
 *	A socket has joined a multicast group on device dev.
 */

void ip_mc_inc_group(struct in_device *in_dev, __be32 addr)
{
	struct ip_mc_list *im;

	ASSERT_RTNL();

	for (im=in_dev->mc_list; im; im=im->next) {
		if (im->multiaddr == addr) {
			im->users++;
			ip_mc_add_src(in_dev, &addr, MCAST_EXCLUDE, 0, NULL, 0);
			goto out;
		}
	}

	im = kmalloc(sizeof(*im), GFP_KERNEL);
	if (!im)
		goto out;

	im->users = 1;
	im->interface = in_dev;
	in_dev_hold(in_dev);
	im->multiaddr = addr;
	/* initial mode is (EX, empty) */
	im->sfmode = MCAST_EXCLUDE;
	im->sfcount[MCAST_INCLUDE] = 0;
	im->sfcount[MCAST_EXCLUDE] = 1;
	im->sources = NULL;
	im->tomb = NULL;
	im->crcount = 0;
	atomic_set(&im->refcnt, 1);
	spin_lock_init(&im->lock);
#ifdef CONFIG_IP_MULTICAST
	im->tm_running = 0;
	setup_timer(&im->timer, &igmp_timer_expire, (unsigned long)im);
	im->unsolicit_count = IGMP_Unsolicited_Report_Count;
	im->reporter = 0;
	im->gsquery = 0;
#endif
	im->loaded = 0;
	write_lock_bh(&in_dev->mc_list_lock);
	im->next = in_dev->mc_list;
	in_dev->mc_list = im;
	in_dev->mc_count++;
	write_unlock_bh(&in_dev->mc_list_lock);
#ifdef CONFIG_IP_MULTICAST
	igmpv3_del_delrec(in_dev, im->multiaddr);
#endif
	igmp_group_added(im);
	if (!in_dev->dead)
		ip_rt_multicast_event(in_dev);
out:
	return;
}

/*
 *	Resend IGMP JOIN report; used for bonding.
 */
void ip_mc_rejoin_group(struct ip_mc_list *im)
{
#ifdef CONFIG_IP_MULTICAST
	struct in_device *in_dev = im->interface;

	if (im->multiaddr == IGMP_ALL_HOSTS)
		return;

	if (IGMP_V1_SEEN(in_dev) || IGMP_V2_SEEN(in_dev)) {
		igmp_mod_timer(im, IGMP_Initial_Report_Delay);
		return;
	}
	/* else, v3 */
	im->crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
		IGMP_Unsolicited_Report_Count;
	igmp_ifc_event(in_dev);
#endif
}

/*
 *	A socket has left a multicast group on device dev
 */

void ip_mc_dec_group(struct in_device *in_dev, __be32 addr)
{
	struct ip_mc_list *i, **ip;

	ASSERT_RTNL();

	for (ip=&in_dev->mc_list; (i=*ip)!=NULL; ip=&i->next) {
		if (i->multiaddr == addr) {
			if (--i->users == 0) {
				write_lock_bh(&in_dev->mc_list_lock);
				*ip = i->next;
				in_dev->mc_count--;
				write_unlock_bh(&in_dev->mc_list_lock);
				igmp_group_dropped(i);

				if (!in_dev->dead)
					ip_rt_multicast_event(in_dev);

				ip_ma_put(i);
				return;
			}
			break;
		}
	}
}

/* Device changing type */

void ip_mc_unmap(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	for (i = in_dev->mc_list; i; i = i->next)
		igmp_group_dropped(i);
}

void ip_mc_remap(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	for (i = in_dev->mc_list; i; i = i->next)
		igmp_group_added(i);
}

/* Device going down */

void ip_mc_down(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	for (i=in_dev->mc_list; i; i=i->next)
		igmp_group_dropped(i);

#ifdef CONFIG_IP_MULTICAST
	in_dev->mr_ifc_count = 0;
	if (del_timer(&in_dev->mr_ifc_timer))
		__in_dev_put(in_dev);
	in_dev->mr_gq_running = 0;
	if (del_timer(&in_dev->mr_gq_timer))
		__in_dev_put(in_dev);
	igmpv3_clear_delrec(in_dev);
#endif

	ip_mc_dec_group(in_dev, IGMP_ALL_HOSTS);
}

void ip_mc_init_dev(struct in_device *in_dev)
{
	ASSERT_RTNL();

	in_dev->mc_tomb = NULL;
#ifdef CONFIG_IP_MULTICAST
	in_dev->mr_gq_running = 0;
	setup_timer(&in_dev->mr_gq_timer, igmp_gq_timer_expire,
			(unsigned long)in_dev);
	in_dev->mr_ifc_count = 0;
	in_dev->mc_count     = 0;
	setup_timer(&in_dev->mr_ifc_timer, igmp_ifc_timer_expire,
			(unsigned long)in_dev);
	in_dev->mr_qrv = IGMP_Unsolicited_Report_Count;
#endif

	rwlock_init(&in_dev->mc_list_lock);
	spin_lock_init(&in_dev->mc_tomb_lock);
}

/* Device going up */

void ip_mc_up(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	ip_mc_inc_group(in_dev, IGMP_ALL_HOSTS);

	for (i=in_dev->mc_list; i; i=i->next)
		igmp_group_added(i);
}

/*
 *	Device is about to be destroyed: clean up.
 */

void ip_mc_destroy_dev(struct in_device *in_dev)
{
	struct ip_mc_list *i;

	ASSERT_RTNL();

	/* Deactivate timers */
	ip_mc_down(in_dev);

	write_lock_bh(&in_dev->mc_list_lock);
	while ((i = in_dev->mc_list) != NULL) {
		in_dev->mc_list = i->next;
		in_dev->mc_count--;
		write_unlock_bh(&in_dev->mc_list_lock);
		igmp_group_dropped(i);
		ip_ma_put(i);

		write_lock_bh(&in_dev->mc_list_lock);
	}
	write_unlock_bh(&in_dev->mc_list_lock);
}

static struct in_device *ip_mc_find_dev(struct net *net, struct ip_mreqn *imr)
{
	struct flowi fl = { .nl_u = { .ip4_u =
				      { .daddr = imr->imr_multiaddr.s_addr } } };
	struct rtable *rt;
	struct net_device *dev = NULL;
	struct in_device *idev = NULL;

	if (imr->imr_ifindex) {
		idev = inetdev_by_index(net, imr->imr_ifindex);
		if (idev)
			__in_dev_put(idev);
		return idev;
	}
	if (imr->imr_address.s_addr) {
		dev = ip_dev_find(net, imr->imr_address.s_addr);
		if (!dev)
			return NULL;
		dev_put(dev);
	}

	if (!dev && !ip_route_output_key(net, &rt, &fl)) {
		dev = rt->u.dst.dev;
		ip_rt_put(rt);
	}
	if (dev) {
		imr->imr_ifindex = dev->ifindex;
		idev = __in_dev_get_rtnl(dev);
	}
	return idev;
}

/*
 *	Join a socket to a group
 */
int sysctl_igmp_max_memberships __read_mostly = IP_MAX_MEMBERSHIPS;
int sysctl_igmp_max_msf __read_mostly = IP_MAX_MSF;


static int ip_mc_del1_src(struct ip_mc_list *pmc, int sfmode,
	__be32 *psfsrc)
{
	struct ip_sf_list *psf, *psf_prev;
	int rv = 0;

	psf_prev = NULL;
	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (psf->sf_inaddr == *psfsrc)
			break;
		psf_prev = psf;
	}
	if (!psf || psf->sf_count[sfmode] == 0) {
		/* source filter not found, or count wrong =>  bug */
		return -ESRCH;
	}
	psf->sf_count[sfmode]--;
	if (psf->sf_count[sfmode] == 0) {
		ip_rt_multicast_event(pmc->interface);
	}
	if (!psf->sf_count[MCAST_INCLUDE] && !psf->sf_count[MCAST_EXCLUDE]) {
#ifdef CONFIG_IP_MULTICAST
		struct in_device *in_dev = pmc->interface;
#endif

		/* no more filters for this source */
		if (psf_prev)
			psf_prev->sf_next = psf->sf_next;
		else
			pmc->sources = psf->sf_next;
#ifdef CONFIG_IP_MULTICAST
		if (psf->sf_oldin &&
		    !IGMP_V1_SEEN(in_dev) && !IGMP_V2_SEEN(in_dev)) {
			psf->sf_crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
				IGMP_Unsolicited_Report_Count;
			psf->sf_next = pmc->tomb;
			pmc->tomb = psf;
			rv = 1;
		} else
#endif
			kfree(psf);
	}
	return rv;
}

#ifndef CONFIG_IP_MULTICAST
#define igmp_ifc_event(x)	do { } while (0)
#endif

static int ip_mc_del_src(struct in_device *in_dev, __be32 *pmca, int sfmode,
			 int sfcount, __be32 *psfsrc, int delta)
{
	struct ip_mc_list *pmc;
	int	changerec = 0;
	int	i, err;

	if (!in_dev)
		return -ENODEV;
	read_lock(&in_dev->mc_list_lock);
	for (pmc=in_dev->mc_list; pmc; pmc=pmc->next) {
		if (*pmca == pmc->multiaddr)
			break;
	}
	if (!pmc) {
		/* MCA not found?? bug */
		read_unlock(&in_dev->mc_list_lock);
		return -ESRCH;
	}
	spin_lock_bh(&pmc->lock);
	read_unlock(&in_dev->mc_list_lock);
#ifdef CONFIG_IP_MULTICAST
	sf_markstate(pmc);
#endif
	if (!delta) {
		err = -EINVAL;
		if (!pmc->sfcount[sfmode])
			goto out_unlock;
		pmc->sfcount[sfmode]--;
	}
	err = 0;
	for (i=0; i<sfcount; i++) {
		int rv = ip_mc_del1_src(pmc, sfmode, &psfsrc[i]);

		changerec |= rv > 0;
		if (!err && rv < 0)
			err = rv;
	}
	if (pmc->sfmode == MCAST_EXCLUDE &&
	    pmc->sfcount[MCAST_EXCLUDE] == 0 &&
	    pmc->sfcount[MCAST_INCLUDE]) {
#ifdef CONFIG_IP_MULTICAST
		struct ip_sf_list *psf;
#endif

		/* filter mode change */
		pmc->sfmode = MCAST_INCLUDE;
#ifdef CONFIG_IP_MULTICAST
		pmc->crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
			IGMP_Unsolicited_Report_Count;
		in_dev->mr_ifc_count = pmc->crcount;
		for (psf=pmc->sources; psf; psf = psf->sf_next)
			psf->sf_crcount = 0;
		igmp_ifc_event(pmc->interface);
	} else if (sf_setstate(pmc) || changerec) {
		igmp_ifc_event(pmc->interface);
#endif
	}
out_unlock:
	spin_unlock_bh(&pmc->lock);
	return err;
}

/*
 * Add multicast single-source filter to the interface list
 */
static int ip_mc_add1_src(struct ip_mc_list *pmc, int sfmode,
	__be32 *psfsrc, int delta)
{
	struct ip_sf_list *psf, *psf_prev;

	psf_prev = NULL;
	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (psf->sf_inaddr == *psfsrc)
			break;
		psf_prev = psf;
	}
	if (!psf) {
		psf = kzalloc(sizeof(*psf), GFP_ATOMIC);
		if (!psf)
			return -ENOBUFS;
		psf->sf_inaddr = *psfsrc;
		if (psf_prev) {
			psf_prev->sf_next = psf;
		} else
			pmc->sources = psf;
	}
	psf->sf_count[sfmode]++;
	if (psf->sf_count[sfmode] == 1) {
		ip_rt_multicast_event(pmc->interface);
	}
	return 0;
}

#ifdef CONFIG_IP_MULTICAST
static void sf_markstate(struct ip_mc_list *pmc)
{
	struct ip_sf_list *psf;
	int mca_xcount = pmc->sfcount[MCAST_EXCLUDE];

	for (psf=pmc->sources; psf; psf=psf->sf_next)
		if (pmc->sfcount[MCAST_EXCLUDE]) {
			psf->sf_oldin = mca_xcount ==
				psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else
			psf->sf_oldin = psf->sf_count[MCAST_INCLUDE] != 0;
}

static int sf_setstate(struct ip_mc_list *pmc)
{
	struct ip_sf_list *psf, *dpsf;
	int mca_xcount = pmc->sfcount[MCAST_EXCLUDE];
	int qrv = pmc->interface->mr_qrv;
	int new_in, rv;

	rv = 0;
	for (psf=pmc->sources; psf; psf=psf->sf_next) {
		if (pmc->sfcount[MCAST_EXCLUDE]) {
			new_in = mca_xcount == psf->sf_count[MCAST_EXCLUDE] &&
				!psf->sf_count[MCAST_INCLUDE];
		} else
			new_in = psf->sf_count[MCAST_INCLUDE] != 0;
		if (new_in) {
			if (!psf->sf_oldin) {
				struct ip_sf_list *prev = NULL;

				for (dpsf=pmc->tomb; dpsf; dpsf=dpsf->sf_next) {
					if (dpsf->sf_inaddr == psf->sf_inaddr)
						break;
					prev = dpsf;
				}
				if (dpsf) {
					if (prev)
						prev->sf_next = dpsf->sf_next;
					else
						pmc->tomb = dpsf->sf_next;
					kfree(dpsf);
				}
				psf->sf_crcount = qrv;
				rv++;
			}
		} else if (psf->sf_oldin) {

			psf->sf_crcount = 0;
			/*
			 * add or update "delete" records if an active filter
			 * is now inactive
			 */
			for (dpsf=pmc->tomb; dpsf; dpsf=dpsf->sf_next)
				if (dpsf->sf_inaddr == psf->sf_inaddr)
					break;
			if (!dpsf) {
				dpsf = (struct ip_sf_list *)
					kmalloc(sizeof(*dpsf), GFP_ATOMIC);
				if (!dpsf)
					continue;
				*dpsf = *psf;
				/* pmc->lock held by callers */
				dpsf->sf_next = pmc->tomb;
				pmc->tomb = dpsf;
			}
			dpsf->sf_crcount = qrv;
			rv++;
		}
	}
	return rv;
}
#endif

/*
 * Add multicast source filter list to the interface list
 */
static int ip_mc_add_src(struct in_device *in_dev, __be32 *pmca, int sfmode,
			 int sfcount, __be32 *psfsrc, int delta)
{
	struct ip_mc_list *pmc;
	int	isexclude;
	int	i, err;

	if (!in_dev)
		return -ENODEV;
	read_lock(&in_dev->mc_list_lock);
	for (pmc=in_dev->mc_list; pmc; pmc=pmc->next) {
		if (*pmca == pmc->multiaddr)
			break;
	}
	if (!pmc) {
		/* MCA not found?? bug */
		read_unlock(&in_dev->mc_list_lock);
		return -ESRCH;
	}
	spin_lock_bh(&pmc->lock);
	read_unlock(&in_dev->mc_list_lock);

#ifdef CONFIG_IP_MULTICAST
	sf_markstate(pmc);
#endif
	isexclude = pmc->sfmode == MCAST_EXCLUDE;
	if (!delta)
		pmc->sfcount[sfmode]++;
	err = 0;
	for (i=0; i<sfcount; i++) {
		err = ip_mc_add1_src(pmc, sfmode, &psfsrc[i], delta);
		if (err)
			break;
	}
	if (err) {
		int j;

		pmc->sfcount[sfmode]--;
		for (j=0; j<i; j++)
			(void) ip_mc_del1_src(pmc, sfmode, &psfsrc[i]);
	} else if (isexclude != (pmc->sfcount[MCAST_EXCLUDE] != 0)) {
#ifdef CONFIG_IP_MULTICAST
		struct ip_sf_list *psf;
		in_dev = pmc->interface;
#endif

		/* filter mode change */
		if (pmc->sfcount[MCAST_EXCLUDE])
			pmc->sfmode = MCAST_EXCLUDE;
		else if (pmc->sfcount[MCAST_INCLUDE])
			pmc->sfmode = MCAST_INCLUDE;
#ifdef CONFIG_IP_MULTICAST
		/* else no filters; keep old mode for reports */

		pmc->crcount = in_dev->mr_qrv ? in_dev->mr_qrv :
			IGMP_Unsolicited_Report_Count;
		in_dev->mr_ifc_count = pmc->crcount;
		for (psf=pmc->sources; psf; psf = psf->sf_next)
			psf->sf_crcount = 0;
		igmp_ifc_event(in_dev);
	} else if (sf_setstate(pmc)) {
		igmp_ifc_event(in_dev);
#endif
	}
	spin_unlock_bh(&pmc->lock);
	return err;
}

static void ip_mc_clear_src(struct ip_mc_list *pmc)
{
	struct ip_sf_list *psf, *nextpsf;

	for (psf=pmc->tomb; psf; psf=nextpsf) {
		nextpsf = psf->sf_next;
		kfree(psf);
	}
	pmc->tomb = NULL;
	for (psf=pmc->sources; psf; psf=nextpsf) {
		nextpsf = psf->sf_next;
		kfree(psf);
	}
	pmc->sources = NULL;
	pmc->sfmode = MCAST_EXCLUDE;
	pmc->sfcount[MCAST_INCLUDE] = 0;
	pmc->sfcount[MCAST_EXCLUDE] = 1;
}


/*
 * Join a multicast group
 */
int ip_mc_join_group(struct sock *sk , struct ip_mreqn *imr)
{
	int err;
	__be32 addr = imr->imr_multiaddr.s_addr;
	struct ip_mc_socklist *iml = NULL, *i;
	struct in_device *in_dev;
	struct inet_sock *inet = inet_sk(sk);
	struct net *net = sock_net(sk);
	int ifindex;
	int count = 0;

	if (!ipv4_is_multicast(addr))
		return -EINVAL;

	rtnl_lock();

	in_dev = ip_mc_find_dev(net, imr);

	if (!in_dev) {
		iml = NULL;
		err = -ENODEV;
		goto done;
	}

	err = -EADDRINUSE;
	ifindex = imr->imr_ifindex;
	for (i = inet->mc_list; i; i = i->next) {
		if (i->multi.imr_multiaddr.s_addr == addr &&
		    i->multi.imr_ifindex == ifindex)
			goto done;
		count++;
	}
	err = -ENOBUFS;
	if (count >= sysctl_igmp_max_memberships)
		goto done;
	iml = sock_kmalloc(sk, sizeof(*iml), GFP_KERNEL);
	if (iml == NULL)
		goto done;

	memcpy(&iml->multi, imr, sizeof(*imr));
	iml->next = inet->mc_list;
	iml->sflist = NULL;
	iml->sfmode = MCAST_EXCLUDE;
	inet->mc_list = iml;
	ip_mc_inc_group(in_dev, addr);
	err = 0;
done:
	rtnl_unlock();
	return err;
}

static int ip_mc_leave_src(struct sock *sk, struct ip_mc_socklist *iml,
			   struct in_device *in_dev)
{
	int err;

	if (iml->sflist == NULL) {
		/* any-source empty exclude case */
		return ip_mc_del_src(in_dev, &iml->multi.imr_multiaddr.s_addr,
			iml->sfmode, 0, NULL, 0);
	}
	err = ip_mc_del_src(in_dev, &iml->multi.imr_multiaddr.s_addr,
			iml->sfmode, iml->sflist->sl_count,
			iml->sflist->sl_addr, 0);
	sock_kfree_s(sk, iml->sflist, IP_SFLSIZE(iml->sflist->sl_max));
	iml->sflist = NULL;
	return err;
}

/*
 *	Ask a socket to leave a group.
 */

int ip_mc_leave_group(struct sock *sk, struct ip_mreqn *imr)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ip_mc_socklist *iml, **imlp;
	struct in_device *in_dev;
	struct net *net = sock_net(sk);
	__be32 group = imr->imr_multiaddr.s_addr;
	u32 ifindex;
	int ret = -EADDRNOTAVAIL;

	rtnl_lock();
	in_dev = ip_mc_find_dev(net, imr);
	ifindex = imr->imr_ifindex;
	for (imlp = &inet->mc_list; (iml = *imlp) != NULL; imlp = &iml->next) {
		if (iml->multi.imr_multiaddr.s_addr != group)
			continue;
		if (ifindex) {
			if (iml->multi.imr_ifindex != ifindex)
				continue;
		} else if (imr->imr_address.s_addr && imr->imr_address.s_addr !=
				iml->multi.imr_address.s_addr)
			continue;

		(void) ip_mc_leave_src(sk, iml, in_dev);

		*imlp = iml->next;

		if (in_dev)
			ip_mc_dec_group(in_dev, group);
		rtnl_unlock();
		sock_kfree_s(sk, iml, sizeof(*iml));
		return 0;
	}
	if (!in_dev)
		ret = -ENODEV;
	rtnl_unlock();
	return ret;
}

int ip_mc_source(int add, int omode, struct sock *sk, struct
	ip_mreq_source *mreqs, int ifindex)
{
	int err;
	struct ip_mreqn imr;
	__be32 addr = mreqs->imr_multiaddr;
	struct ip_mc_socklist *pmc;
	struct in_device *in_dev = NULL;
	struct inet_sock *inet = inet_sk(sk);
	struct ip_sf_socklist *psl;
	struct net *net = sock_net(sk);
	int leavegroup = 0;
	int i, j, rv;

	if (!ipv4_is_multicast(addr))
		return -EINVAL;

	rtnl_lock();

	imr.imr_multiaddr.s_addr = mreqs->imr_multiaddr;
	imr.imr_address.s_addr = mreqs->imr_interface;
	imr.imr_ifindex = ifindex;
	in_dev = ip_mc_find_dev(net, &imr);

	if (!in_dev) {
		err = -ENODEV;
		goto done;
	}
	err = -EADDRNOTAVAIL;

	for (pmc=inet->mc_list; pmc; pmc=pmc->next) {
		if ((pmc->multi.imr_multiaddr.s_addr ==
		     imr.imr_multiaddr.s_addr) &&
		    (pmc->multi.imr_ifindex == imr.imr_ifindex))
			break;
	}
	if (!pmc) {		/* must have a prior join */
		err = -EINVAL;
		goto done;
	}
	/* if a source filter was set, must be the same mode as before */
	if (pmc->sflist) {
		if (pmc->sfmode != omode) {
			err = -EINVAL;
			goto done;
		}
	} else if (pmc->sfmode != omode) {
		/* allow mode switches for empty-set filters */
		ip_mc_add_src(in_dev, &mreqs->imr_multiaddr, omode, 0, NULL, 0);
		ip_mc_del_src(in_dev, &mreqs->imr_multiaddr, pmc->sfmode, 0,
			NULL, 0);
		pmc->sfmode = omode;
	}

	psl = pmc->sflist;
	if (!add) {
		if (!psl)
			goto done;	/* err = -EADDRNOTAVAIL */
		rv = !0;
		for (i=0; i<psl->sl_count; i++) {
			rv = memcmp(&psl->sl_addr[i], &mreqs->imr_sourceaddr,
				sizeof(__be32));
			if (rv == 0)
				break;
		}
		if (rv)		/* source not found */
			goto done;	/* err = -EADDRNOTAVAIL */

		/* special case - (INCLUDE, empty) == LEAVE_GROUP */
		if (psl->sl_count == 1 && omode == MCAST_INCLUDE) {
			leavegroup = 1;
			goto done;
		}

		/* update the interface filter */
		ip_mc_del_src(in_dev, &mreqs->imr_multiaddr, omode, 1,
			&mreqs->imr_sourceaddr, 1);

		for (j=i+1; j<psl->sl_count; j++)
			psl->sl_addr[j-1] = psl->sl_addr[j];
		psl->sl_count--;
		err = 0;
		goto done;
	}
	/* else, add a new source to the filter */

	if (psl && psl->sl_count >= sysctl_igmp_max_msf) {
		err = -ENOBUFS;
		goto done;
	}
	if (!psl || psl->sl_count == psl->sl_max) {
		struct ip_sf_socklist *newpsl;
		int count = IP_SFBLOCK;

		if (psl)
			count += psl->sl_max;
		newpsl = sock_kmalloc(sk, IP_SFLSIZE(count), GFP_KERNEL);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = count;
		newpsl->sl_count = count - IP_SFBLOCK;
		if (psl) {
			for (i=0; i<psl->sl_count; i++)
				newpsl->sl_addr[i] = psl->sl_addr[i];
			sock_kfree_s(sk, psl, IP_SFLSIZE(psl->sl_max));
		}
		pmc->sflist = psl = newpsl;
	}
	rv = 1;	/* > 0 for insert logic below if sl_count is 0 */
	for (i=0; i<psl->sl_count; i++) {
		rv = memcmp(&psl->sl_addr[i], &mreqs->imr_sourceaddr,
			sizeof(__be32));
		if (rv == 0)
			break;
	}
	if (rv == 0)		/* address already there is an error */
		goto done;
	for (j=psl->sl_count-1; j>=i; j--)
		psl->sl_addr[j+1] = psl->sl_addr[j];
	psl->sl_addr[i] = mreqs->imr_sourceaddr;
	psl->sl_count++;
	err = 0;
	/* update the interface list */
	ip_mc_add_src(in_dev, &mreqs->imr_multiaddr, omode, 1,
		&mreqs->imr_sourceaddr, 1);
done:
	rtnl_unlock();
	if (leavegroup)
		return ip_mc_leave_group(sk, &imr);
	return err;
}

int ip_mc_msfilter(struct sock *sk, struct ip_msfilter *msf, int ifindex)
{
	int err = 0;
	struct ip_mreqn	imr;
	__be32 addr = msf->imsf_multiaddr;
	struct ip_mc_socklist *pmc;
	struct in_device *in_dev;
	struct inet_sock *inet = inet_sk(sk);
	struct ip_sf_socklist *newpsl, *psl;
	struct net *net = sock_net(sk);
	int leavegroup = 0;

	if (!ipv4_is_multicast(addr))
		return -EINVAL;
	if (msf->imsf_fmode != MCAST_INCLUDE &&
	    msf->imsf_fmode != MCAST_EXCLUDE)
		return -EINVAL;

	rtnl_lock();

	imr.imr_multiaddr.s_addr = msf->imsf_multiaddr;
	imr.imr_address.s_addr = msf->imsf_interface;
	imr.imr_ifindex = ifindex;
	in_dev = ip_mc_find_dev(net, &imr);

	if (!in_dev) {
		err = -ENODEV;
		goto done;
	}

	/* special case - (INCLUDE, empty) == LEAVE_GROUP */
	if (msf->imsf_fmode == MCAST_INCLUDE && msf->imsf_numsrc == 0) {
		leavegroup = 1;
		goto done;
	}

	for (pmc=inet->mc_list; pmc; pmc=pmc->next) {
		if (pmc->multi.imr_multiaddr.s_addr == msf->imsf_multiaddr &&
		    pmc->multi.imr_ifindex == imr.imr_ifindex)
			break;
	}
	if (!pmc) {		/* must have a prior join */
		err = -EINVAL;
		goto done;
	}
	if (msf->imsf_numsrc) {
		newpsl = sock_kmalloc(sk, IP_SFLSIZE(msf->imsf_numsrc),
							   GFP_KERNEL);
		if (!newpsl) {
			err = -ENOBUFS;
			goto done;
		}
		newpsl->sl_max = newpsl->sl_count = msf->imsf_numsrc;
		memcpy(newpsl->sl_addr, msf->imsf_slist,
			msf->imsf_numsrc * sizeof(msf->imsf_slist[0]));
		err = ip_mc_add_src(in_dev, &msf->imsf_multiaddr,
			msf->imsf_fmode, newpsl->sl_count, newpsl->sl_addr, 0);
		if (err) {
			sock_kfree_s(sk, newpsl, IP_SFLSIZE(newpsl->sl_max));
			goto done;
		}
	} else {
		newpsl = NULL;
		(void) ip_mc_add_src(in_dev, &msf->imsf_multiaddr,
				     msf->imsf_fmode, 0, NULL, 0);
	}
	psl = pmc->sflist;
	if (psl) {
		(void) ip_mc_del_src(in_dev, &msf->imsf_multiaddr, pmc->sfmode,
			psl->sl_count, psl->sl_addr, 0);
		sock_kfree_s(sk, psl, IP_SFLSIZE(psl->sl_max));
	} else
		(void) ip_mc_del_src(in_dev, &msf->imsf_multiaddr, pmc->sfmode,
			0, NULL, 0);
	pmc->sflist = newpsl;
	pmc->sfmode = msf->imsf_fmode;
	err = 0;
done:
	rtnl_unlock();
	if (leavegroup)
		err = ip_mc_leave_group(sk, &imr);
	return err;
}

int ip_mc_msfget(struct sock *sk, struct ip_msfilter *msf,
	struct ip_msfilter __user *optval, int __user *optlen)
{
	int err, len, count, copycount;
	struct ip_mreqn	imr;
	__be32 addr = msf->imsf_multiaddr;
	struct ip_mc_socklist *pmc;
	struct in_device *in_dev;
	struct inet_sock *inet = inet_sk(sk);
	struct ip_sf_socklist *psl;
	struct net *net = sock_net(sk);

	if (!ipv4_is_multicast(addr))
		return -EINVAL;

	rtnl_lock();

	imr.imr_multiaddr.s_addr = msf->imsf_multiaddr;
	imr.imr_address.s_addr = msf->imsf_interface;
	imr.imr_ifindex = 0;
	in_dev = ip_mc_find_dev(net, &imr);

	if (!in_dev) {
		err = -ENODEV;
		goto done;
	}
	err = -EADDRNOTAVAIL;

	for (pmc=inet->mc_list; pmc; pmc=pmc->next) {
		if (pmc->multi.imr_multiaddr.s_addr == msf->imsf_multiaddr &&
		    pmc->multi.imr_ifindex == imr.imr_ifindex)
			break;
	}
	if (!pmc)		/* must have a prior join */
		goto done;
	msf->imsf_fmode = pmc->sfmode;
	psl = pmc->sflist;
	rtnl_unlock();
	if (!psl) {
		len = 0;
		count = 0;
	} else {
		count = psl->sl_count;
	}
	copycount = count < msf->imsf_numsrc ? count : msf->imsf_numsrc;
	len = copycount * sizeof(psl->sl_addr[0]);
	msf->imsf_numsrc = count;
	if (put_user(IP_MSFILTER_SIZE(copycount), optlen) ||
	    copy_to_user(optval, msf, IP_MSFILTER_SIZE(0))) {
		return -EFAULT;
	}
	if (len &&
	    copy_to_user(&optval->imsf_slist[0], psl->sl_addr, len))
		return -EFAULT;
	return 0;
done:
	rtnl_unlock();
	return err;
}

int ip_mc_gsfget(struct sock *sk, struct group_filter *gsf,
	struct group_filter __user *optval, int __user *optlen)
{
	int err, i, count, copycount;
	struct sockaddr_in *psin;
	__be32 addr;
	struct ip_mc_socklist *pmc;
	struct inet_sock *inet = inet_sk(sk);
	struct ip_sf_socklist *psl;

	psin = (struct sockaddr_in *)&gsf->gf_group;
	if (psin->sin_family != AF_INET)
		return -EINVAL;
	addr = psin->sin_addr.s_addr;
	if (!ipv4_is_multicast(addr))
		return -EINVAL;

	rtnl_lock();

	err = -EADDRNOTAVAIL;

	for (pmc=inet->mc_list; pmc; pmc=pmc->next) {
		if (pmc->multi.imr_multiaddr.s_addr == addr &&
		    pmc->multi.imr_ifindex == gsf->gf_interface)
			break;
	}
	if (!pmc)		/* must have a prior join */
		goto done;
	gsf->gf_fmode = pmc->sfmode;
	psl = pmc->sflist;
	rtnl_unlock();
	count = psl ? psl->sl_count : 0;
	copycount = count < gsf->gf_numsrc ? count : gsf->gf_numsrc;
	gsf->gf_numsrc = count;
	if (put_user(GROUP_FILTER_SIZE(copycount), optlen) ||
	    copy_to_user(optval, gsf, GROUP_FILTER_SIZE(0))) {
		return -EFAULT;
	}
	for (i=0; i<copycount; i++) {
		struct sockaddr_storage ss;

		psin = (struct sockaddr_in *)&ss;
		memset(&ss, 0, sizeof(ss));
		psin->sin_family = AF_INET;
		psin->sin_addr.s_addr = psl->sl_addr[i];
		if (copy_to_user(&optval->gf_slist[i], &ss, sizeof(ss)))
			return -EFAULT;
	}
	return 0;
done:
	rtnl_unlock();
	return err;
}

/*
 * check if a multicast source filter allows delivery for a given <src,dst,intf>
 */
int ip_mc_sf_allow(struct sock *sk, __be32 loc_addr, __be32 rmt_addr, int dif)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ip_mc_socklist *pmc;
	struct ip_sf_socklist *psl;
	int i;

	if (!ipv4_is_multicast(loc_addr))
		return 1;

	for (pmc=inet->mc_list; pmc; pmc=pmc->next) {
		if (pmc->multi.imr_multiaddr.s_addr == loc_addr &&
		    pmc->multi.imr_ifindex == dif)
			break;
	}
	if (!pmc)
		return inet->mc_all;
	psl = pmc->sflist;
	if (!psl)
		return pmc->sfmode == MCAST_EXCLUDE;

	for (i=0; i<psl->sl_count; i++) {
		if (psl->sl_addr[i] == rmt_addr)
			break;
	}
	if (pmc->sfmode == MCAST_INCLUDE && i >= psl->sl_count)
		return 0;
	if (pmc->sfmode == MCAST_EXCLUDE && i < psl->sl_count)
		return 0;
	return 1;
}

/*
 *	A socket is closing.
 */

void ip_mc_drop_socket(struct sock *sk)
{
	struct inet_sock *inet = inet_sk(sk);
	struct ip_mc_socklist *iml;
	struct net *net = sock_net(sk);

	if (inet->mc_list == NULL)
		return;

	rtnl_lock();
	while ((iml = inet->mc_list) != NULL) {
		struct in_device *in_dev;
		inet->mc_list = iml->next;

		in_dev = inetdev_by_index(net, iml->multi.imr_ifindex);
		(void) ip_mc_leave_src(sk, iml, in_dev);
		if (in_dev != NULL) {
			ip_mc_dec_group(in_dev, iml->multi.imr_multiaddr.s_addr);
			in_dev_put(in_dev);
		}
		sock_kfree_s(sk, iml, sizeof(*iml));
	}
	rtnl_unlock();
}

int ip_check_mc(struct in_device *in_dev, __be32 mc_addr, __be32 src_addr, u16 proto)
{
	struct ip_mc_list *im;
	struct ip_sf_list *psf;
	int rv = 0;

	read_lock(&in_dev->mc_list_lock);
	for (im=in_dev->mc_list; im; im=im->next) {
		if (im->multiaddr == mc_addr)
			break;
	}
	if (im && proto == IPPROTO_IGMP) {
		rv = 1;
	} else if (im) {
		if (src_addr) {
			for (psf=im->sources; psf; psf=psf->sf_next) {
				if (psf->sf_inaddr == src_addr)
					break;
			}
			if (psf)
				rv = psf->sf_count[MCAST_INCLUDE] ||
					psf->sf_count[MCAST_EXCLUDE] !=
					im->sfcount[MCAST_EXCLUDE];
			else
				rv = im->sfcount[MCAST_EXCLUDE] != 0;
		} else
			rv = 1; /* unspecified source; tentatively allow */
	}
	read_unlock(&in_dev->mc_list_lock);
	return rv;
}

#if defined(CONFIG_PROC_FS)
struct igmp_mc_iter_state {
	struct seq_net_private p;
	struct net_device *dev;
	struct in_device *in_dev;
};

#define	igmp_mc_seq_private(seq)	((struct igmp_mc_iter_state *)(seq)->private)

static inline struct ip_mc_list *igmp_mc_get_first(struct seq_file *seq)
{
	struct net *net = seq_file_net(seq);
	struct ip_mc_list *im = NULL;
	struct igmp_mc_iter_state *state = igmp_mc_seq_private(seq);

	state->in_dev = NULL;
	for_each_netdev_rcu(net, state->dev) {
		struct in_device *in_dev;

		in_dev = __in_dev_get_rcu(state->dev);
		if (!in_dev)
			continue;
		read_lock(&in_dev->mc_list_lock);
		im = in_dev->mc_list;
		if (im) {
			state->in_dev = in_dev;
			break;
		}
		read_unlock(&in_dev->mc_list_lock);
	}
	return im;
}

static struct ip_mc_list *igmp_mc_get_next(struct seq_file *seq, struct ip_mc_list *im)
{
	struct igmp_mc_iter_state *state = igmp_mc_seq_private(seq);
	im = im->next;
	while (!im) {
		if (likely(state->in_dev != NULL))
			read_unlock(&state->in_dev->mc_list_lock);

		state->dev = next_net_device_rcu(state->dev);
		if (!state->dev) {
			state->in_dev = NULL;
			break;
		}
		state->in_dev = __in_dev_get_rcu(state->dev);
		if (!state->in_dev)
			continue;
		read_lock(&state->in_dev->mc_list_lock);
		im = state->in_dev->mc_list;
	}
	return im;
}

static struct ip_mc_list *igmp_mc_get_idx(struct seq_file *seq, loff_t pos)
{
	struct ip_mc_list *im = igmp_mc_get_first(seq);
	if (im)
		while (pos && (im = igmp_mc_get_next(seq, im)) != NULL)
			--pos;
	return pos ? NULL : im;
}

static void *igmp_mc_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu)
{
	rcu_read_lock();
	return *pos ? igmp_mc_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *igmp_mc_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_mc_list *im;
	if (v == SEQ_START_TOKEN)
		im = igmp_mc_get_first(seq);
	else
		im = igmp_mc_get_next(seq, v);
	++*pos;
	return im;
}

static void igmp_mc_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	struct igmp_mc_iter_state *state = igmp_mc_seq_private(seq);
	if (likely(state->in_dev != NULL)) {
		read_unlock(&state->in_dev->mc_list_lock);
		state->in_dev = NULL;
	}
	state->dev = NULL;
	rcu_read_unlock();
}

static int igmp_mc_seq_show(struct seq_file *seq, void *v)
{
	if (v == SEQ_START_TOKEN)
		seq_puts(seq,
			 "Idx\tDevice    : Count Querier\tGroup    Users Timer\tReporter\n");
	else {
		struct ip_mc_list *im = (struct ip_mc_list *)v;
		struct igmp_mc_iter_state *state = igmp_mc_seq_private(seq);
		char   *querier;
#ifdef CONFIG_IP_MULTICAST
		querier = IGMP_V1_SEEN(state->in_dev) ? "V1" :
			  IGMP_V2_SEEN(state->in_dev) ? "V2" :
			  "V3";
#else
		querier = "NONE";
#endif

		if (state->in_dev->mc_list == im) {
			seq_printf(seq, "%d\t%-10s: %5d %7s\n",
				   state->dev->ifindex, state->dev->name, state->in_dev->mc_count, querier);
		}

		seq_printf(seq,
			   "\t\t\t\t%08X %5d %d:%08lX\t\t%d\n",
			   im->multiaddr, im->users,
			   im->tm_running, im->tm_running ?
			   jiffies_to_clock_t(im->timer.expires-jiffies) : 0,
			   im->reporter);
	}
	return 0;
}

static const struct seq_operations igmp_mc_seq_ops = {
	.start	=	igmp_mc_seq_start,
	.next	=	igmp_mc_seq_next,
	.stop	=	igmp_mc_seq_stop,
	.show	=	igmp_mc_seq_show,
};

static int igmp_mc_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &igmp_mc_seq_ops,
			sizeof(struct igmp_mc_iter_state));
}

static const struct file_operations igmp_mc_seq_fops = {
	.owner		=	THIS_MODULE,
	.open		=	igmp_mc_seq_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	seq_release_net,
};

struct igmp_mcf_iter_state {
	struct seq_net_private p;
	struct net_device *dev;
	struct in_device *idev;
	struct ip_mc_list *im;
};

#define igmp_mcf_seq_private(seq)	((struct igmp_mcf_iter_state *)(seq)->private)

static inline struct ip_sf_list *igmp_mcf_get_first(struct seq_file *seq)
{
	struct net *net = seq_file_net(seq);
	struct ip_sf_list *psf = NULL;
	struct ip_mc_list *im = NULL;
	struct igmp_mcf_iter_state *state = igmp_mcf_seq_private(seq);

	state->idev = NULL;
	state->im = NULL;
	for_each_netdev_rcu(net, state->dev) {
		struct in_device *idev;
		idev = __in_dev_get_rcu(state->dev);
		if (unlikely(idev == NULL))
			continue;
		read_lock(&idev->mc_list_lock);
		im = idev->mc_list;
		if (likely(im != NULL)) {
			spin_lock_bh(&im->lock);
			psf = im->sources;
			if (likely(psf != NULL)) {
				state->im = im;
				state->idev = idev;
				break;
			}
			spin_unlock_bh(&im->lock);
		}
		read_unlock(&idev->mc_list_lock);
	}
	return psf;
}

static struct ip_sf_list *igmp_mcf_get_next(struct seq_file *seq, struct ip_sf_list *psf)
{
	struct igmp_mcf_iter_state *state = igmp_mcf_seq_private(seq);

	psf = psf->sf_next;
	while (!psf) {
		spin_unlock_bh(&state->im->lock);
		state->im = state->im->next;
		while (!state->im) {
			if (likely(state->idev != NULL))
				read_unlock(&state->idev->mc_list_lock);

			state->dev = next_net_device_rcu(state->dev);
			if (!state->dev) {
				state->idev = NULL;
				goto out;
			}
			state->idev = __in_dev_get_rcu(state->dev);
			if (!state->idev)
				continue;
			read_lock(&state->idev->mc_list_lock);
			state->im = state->idev->mc_list;
		}
		if (!state->im)
			break;
		spin_lock_bh(&state->im->lock);
		psf = state->im->sources;
	}
out:
	return psf;
}

static struct ip_sf_list *igmp_mcf_get_idx(struct seq_file *seq, loff_t pos)
{
	struct ip_sf_list *psf = igmp_mcf_get_first(seq);
	if (psf)
		while (pos && (psf = igmp_mcf_get_next(seq, psf)) != NULL)
			--pos;
	return pos ? NULL : psf;
}

static void *igmp_mcf_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(rcu)
{
	rcu_read_lock();
	return *pos ? igmp_mcf_get_idx(seq, *pos - 1) : SEQ_START_TOKEN;
}

static void *igmp_mcf_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct ip_sf_list *psf;
	if (v == SEQ_START_TOKEN)
		psf = igmp_mcf_get_first(seq);
	else
		psf = igmp_mcf_get_next(seq, v);
	++*pos;
	return psf;
}

static void igmp_mcf_seq_stop(struct seq_file *seq, void *v)
	__releases(rcu)
{
	struct igmp_mcf_iter_state *state = igmp_mcf_seq_private(seq);
	if (likely(state->im != NULL)) {
		spin_unlock_bh(&state->im->lock);
		state->im = NULL;
	}
	if (likely(state->idev != NULL)) {
		read_unlock(&state->idev->mc_list_lock);
		state->idev = NULL;
	}
	state->dev = NULL;
	rcu_read_unlock();
}

static int igmp_mcf_seq_show(struct seq_file *seq, void *v)
{
	struct ip_sf_list *psf = (struct ip_sf_list *)v;
	struct igmp_mcf_iter_state *state = igmp_mcf_seq_private(seq);

	if (v == SEQ_START_TOKEN) {
		seq_printf(seq,
			   "%3s %6s "
			   "%10s %10s %6s %6s\n", "Idx",
			   "Device", "MCA",
			   "SRC", "INC", "EXC");
	} else {
		seq_printf(seq,
			   "%3d %6.6s 0x%08x "
			   "0x%08x %6lu %6lu\n",
			   state->dev->ifindex, state->dev->name,
			   ntohl(state->im->multiaddr),
			   ntohl(psf->sf_inaddr),
			   psf->sf_count[MCAST_INCLUDE],
			   psf->sf_count[MCAST_EXCLUDE]);
	}
	return 0;
}

static const struct seq_operations igmp_mcf_seq_ops = {
	.start	=	igmp_mcf_seq_start,
	.next	=	igmp_mcf_seq_next,
	.stop	=	igmp_mcf_seq_stop,
	.show	=	igmp_mcf_seq_show,
};

static int igmp_mcf_seq_open(struct inode *inode, struct file *file)
{
	return seq_open_net(inode, file, &igmp_mcf_seq_ops,
			sizeof(struct igmp_mcf_iter_state));
}

static const struct file_operations igmp_mcf_seq_fops = {
	.owner		=	THIS_MODULE,
	.open		=	igmp_mcf_seq_open,
	.read		=	seq_read,
	.llseek		=	seq_lseek,
	.release	=	seq_release_net,
};

static int igmp_net_init(struct net *net)
{
	struct proc_dir_entry *pde;

	pde = proc_net_fops_create(net, "igmp", S_IRUGO, &igmp_mc_seq_fops);
	if (!pde)
		goto out_igmp;
	pde = proc_net_fops_create(net, "mcfilter", S_IRUGO, &igmp_mcf_seq_fops);
	if (!pde)
		goto out_mcfilter;
	return 0;

out_mcfilter:
	proc_net_remove(net, "igmp");
out_igmp:
	return -ENOMEM;
}

static void igmp_net_exit(struct net *net)
{
	proc_net_remove(net, "mcfilter");
	proc_net_remove(net, "igmp");
}

static struct pernet_operations igmp_net_ops = {
	.init = igmp_net_init,
	.exit = igmp_net_exit,
};

int __init igmp_mc_proc_init(void)
{
	return register_pernet_subsys(&igmp_net_ops);
}
#endif

EXPORT_SYMBOL(ip_mc_dec_group);
EXPORT_SYMBOL(ip_mc_inc_group);
EXPORT_SYMBOL(ip_mc_join_group);
EXPORT_SYMBOL(ip_mc_rejoin_group);
