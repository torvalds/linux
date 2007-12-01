/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Forwarding Information Base (Glue/Info List)
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *
 *
 * Changes:
 *              Alexey Kuznetsov : SMP locking changes
 *              Steve Whitehouse : Rewrote it... Well to be more correct, I
 *                                 copied most of it from the ipv4 fib code.
 *              Steve Whitehouse : Updated it in style and fixed a few bugs
 *                                 which were fixed in the ipv4 code since
 *                                 this code was copied from it.
 *
 */
#include <linux/string.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/proc_fs.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/fib_rules.h>
#include <net/dn.h>
#include <net/dn_route.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>
#include <net/dn_dev.h>

#define RT_MIN_TABLE 1

#define for_fib_info() { struct dn_fib_info *fi;\
	for(fi = dn_fib_info_list; fi; fi = fi->fib_next)
#define endfor_fib_info() }

#define for_nexthops(fi) { int nhsel; const struct dn_fib_nh *nh;\
	for(nhsel = 0, nh = (fi)->fib_nh; nhsel < (fi)->fib_nhs; nh++, nhsel++)

#define change_nexthops(fi) { int nhsel; struct dn_fib_nh *nh;\
	for(nhsel = 0, nh = (struct dn_fib_nh *)((fi)->fib_nh); nhsel < (fi)->fib_nhs; nh++, nhsel++)

#define endfor_nexthops(fi) }

static DEFINE_SPINLOCK(dn_fib_multipath_lock);
static struct dn_fib_info *dn_fib_info_list;
static DEFINE_SPINLOCK(dn_fib_info_lock);

static struct
{
	int error;
	u8 scope;
} dn_fib_props[RTN_MAX+1] = {
	[RTN_UNSPEC] =      { .error = 0,       .scope = RT_SCOPE_NOWHERE },
	[RTN_UNICAST] =     { .error = 0,       .scope = RT_SCOPE_UNIVERSE },
	[RTN_LOCAL] =       { .error = 0,       .scope = RT_SCOPE_HOST },
	[RTN_BROADCAST] =   { .error = -EINVAL, .scope = RT_SCOPE_NOWHERE },
	[RTN_ANYCAST] =     { .error = -EINVAL, .scope = RT_SCOPE_NOWHERE },
	[RTN_MULTICAST] =   { .error = -EINVAL, .scope = RT_SCOPE_NOWHERE },
	[RTN_BLACKHOLE] =   { .error = -EINVAL, .scope = RT_SCOPE_UNIVERSE },
	[RTN_UNREACHABLE] = { .error = -EHOSTUNREACH, .scope = RT_SCOPE_UNIVERSE },
	[RTN_PROHIBIT] =    { .error = -EACCES, .scope = RT_SCOPE_UNIVERSE },
	[RTN_THROW] =       { .error = -EAGAIN, .scope = RT_SCOPE_UNIVERSE },
	[RTN_NAT] =         { .error = 0,       .scope = RT_SCOPE_NOWHERE },
	[RTN_XRESOLVE] =    { .error = -EINVAL, .scope = RT_SCOPE_NOWHERE },
};

static int dn_fib_sync_down(__le16 local, struct net_device *dev, int force);
static int dn_fib_sync_up(struct net_device *dev);

void dn_fib_free_info(struct dn_fib_info *fi)
{
	if (fi->fib_dead == 0) {
		printk(KERN_DEBUG "DECnet: BUG! Attempt to free alive dn_fib_info\n");
		return;
	}

	change_nexthops(fi) {
		if (nh->nh_dev)
			dev_put(nh->nh_dev);
		nh->nh_dev = NULL;
	} endfor_nexthops(fi);
	kfree(fi);
}

void dn_fib_release_info(struct dn_fib_info *fi)
{
	spin_lock(&dn_fib_info_lock);
	if (fi && --fi->fib_treeref == 0) {
		if (fi->fib_next)
			fi->fib_next->fib_prev = fi->fib_prev;
		if (fi->fib_prev)
			fi->fib_prev->fib_next = fi->fib_next;
		if (fi == dn_fib_info_list)
			dn_fib_info_list = fi->fib_next;
		fi->fib_dead = 1;
		dn_fib_info_put(fi);
	}
	spin_unlock(&dn_fib_info_lock);
}

static inline int dn_fib_nh_comp(const struct dn_fib_info *fi, const struct dn_fib_info *ofi)
{
	const struct dn_fib_nh *onh = ofi->fib_nh;

	for_nexthops(fi) {
		if (nh->nh_oif != onh->nh_oif ||
			nh->nh_gw != onh->nh_gw ||
			nh->nh_scope != onh->nh_scope ||
			nh->nh_weight != onh->nh_weight ||
			((nh->nh_flags^onh->nh_flags)&~RTNH_F_DEAD))
				return -1;
		onh++;
	} endfor_nexthops(fi);
	return 0;
}

static inline struct dn_fib_info *dn_fib_find_info(const struct dn_fib_info *nfi)
{
	for_fib_info() {
		if (fi->fib_nhs != nfi->fib_nhs)
			continue;
		if (nfi->fib_protocol == fi->fib_protocol &&
			nfi->fib_prefsrc == fi->fib_prefsrc &&
			nfi->fib_priority == fi->fib_priority &&
			memcmp(nfi->fib_metrics, fi->fib_metrics, sizeof(fi->fib_metrics)) == 0 &&
			((nfi->fib_flags^fi->fib_flags)&~RTNH_F_DEAD) == 0 &&
			(nfi->fib_nhs == 0 || dn_fib_nh_comp(fi, nfi) == 0))
				return fi;
	} endfor_fib_info();
	return NULL;
}

__le16 dn_fib_get_attr16(struct rtattr *attr, int attrlen, int type)
{
	while(RTA_OK(attr,attrlen)) {
		if (attr->rta_type == type)
			return *(__le16*)RTA_DATA(attr);
		attr = RTA_NEXT(attr, attrlen);
	}

	return 0;
}

static int dn_fib_count_nhs(struct rtattr *rta)
{
	int nhs = 0;
	struct rtnexthop *nhp = RTA_DATA(rta);
	int nhlen = RTA_PAYLOAD(rta);

	while(nhlen >= (int)sizeof(struct rtnexthop)) {
		if ((nhlen -= nhp->rtnh_len) < 0)
			return 0;
		nhs++;
		nhp = RTNH_NEXT(nhp);
	}

	return nhs;
}

static int dn_fib_get_nhs(struct dn_fib_info *fi, const struct rtattr *rta, const struct rtmsg *r)
{
	struct rtnexthop *nhp = RTA_DATA(rta);
	int nhlen = RTA_PAYLOAD(rta);

	change_nexthops(fi) {
		int attrlen = nhlen - sizeof(struct rtnexthop);
		if (attrlen < 0 || (nhlen -= nhp->rtnh_len) < 0)
			return -EINVAL;

		nh->nh_flags  = (r->rtm_flags&~0xFF) | nhp->rtnh_flags;
		nh->nh_oif    = nhp->rtnh_ifindex;
		nh->nh_weight = nhp->rtnh_hops + 1;

		if (attrlen) {
			nh->nh_gw = dn_fib_get_attr16(RTNH_DATA(nhp), attrlen, RTA_GATEWAY);
		}
		nhp = RTNH_NEXT(nhp);
	} endfor_nexthops(fi);

	return 0;
}


static int dn_fib_check_nh(const struct rtmsg *r, struct dn_fib_info *fi, struct dn_fib_nh *nh)
{
	int err;

	if (nh->nh_gw) {
		struct flowi fl;
		struct dn_fib_res res;

		if (nh->nh_flags&RTNH_F_ONLINK) {
			struct net_device *dev;

			if (r->rtm_scope >= RT_SCOPE_LINK)
				return -EINVAL;
			if (dnet_addr_type(nh->nh_gw) != RTN_UNICAST)
				return -EINVAL;
			if ((dev = __dev_get_by_index(&init_net, nh->nh_oif)) == NULL)
				return -ENODEV;
			if (!(dev->flags&IFF_UP))
				return -ENETDOWN;
			nh->nh_dev = dev;
			dev_hold(dev);
			nh->nh_scope = RT_SCOPE_LINK;
			return 0;
		}

		memset(&fl, 0, sizeof(fl));
		fl.fld_dst = nh->nh_gw;
		fl.oif = nh->nh_oif;
		fl.fld_scope = r->rtm_scope + 1;

		if (fl.fld_scope < RT_SCOPE_LINK)
			fl.fld_scope = RT_SCOPE_LINK;

		if ((err = dn_fib_lookup(&fl, &res)) != 0)
			return err;

		err = -EINVAL;
		if (res.type != RTN_UNICAST && res.type != RTN_LOCAL)
			goto out;
		nh->nh_scope = res.scope;
		nh->nh_oif = DN_FIB_RES_OIF(res);
		nh->nh_dev = DN_FIB_RES_DEV(res);
		if (nh->nh_dev == NULL)
			goto out;
		dev_hold(nh->nh_dev);
		err = -ENETDOWN;
		if (!(nh->nh_dev->flags & IFF_UP))
			goto out;
		err = 0;
out:
		dn_fib_res_put(&res);
		return err;
	} else {
		struct net_device *dev;

		if (nh->nh_flags&(RTNH_F_PERVASIVE|RTNH_F_ONLINK))
			return -EINVAL;

		dev = __dev_get_by_index(&init_net, nh->nh_oif);
		if (dev == NULL || dev->dn_ptr == NULL)
			return -ENODEV;
		if (!(dev->flags&IFF_UP))
			return -ENETDOWN;
		nh->nh_dev = dev;
		dev_hold(nh->nh_dev);
		nh->nh_scope = RT_SCOPE_HOST;
	}

	return 0;
}


struct dn_fib_info *dn_fib_create_info(const struct rtmsg *r, struct dn_kern_rta *rta, const struct nlmsghdr *nlh, int *errp)
{
	int err;
	struct dn_fib_info *fi = NULL;
	struct dn_fib_info *ofi;
	int nhs = 1;

	if (r->rtm_type > RTN_MAX)
		goto err_inval;

	if (dn_fib_props[r->rtm_type].scope > r->rtm_scope)
		goto err_inval;

	if (rta->rta_mp) {
		nhs = dn_fib_count_nhs(rta->rta_mp);
		if (nhs == 0)
			goto err_inval;
	}

	fi = kzalloc(sizeof(*fi)+nhs*sizeof(struct dn_fib_nh), GFP_KERNEL);
	err = -ENOBUFS;
	if (fi == NULL)
		goto failure;

	fi->fib_protocol = r->rtm_protocol;
	fi->fib_nhs = nhs;
	fi->fib_flags = r->rtm_flags;
	if (rta->rta_priority)
		fi->fib_priority = *rta->rta_priority;
	if (rta->rta_mx) {
		int attrlen = RTA_PAYLOAD(rta->rta_mx);
		struct rtattr *attr = RTA_DATA(rta->rta_mx);

		while(RTA_OK(attr, attrlen)) {
			unsigned flavour = attr->rta_type;
			if (flavour) {
				if (flavour > RTAX_MAX)
					goto err_inval;
				fi->fib_metrics[flavour-1] = *(unsigned*)RTA_DATA(attr);
			}
			attr = RTA_NEXT(attr, attrlen);
		}
	}
	if (rta->rta_prefsrc)
		memcpy(&fi->fib_prefsrc, rta->rta_prefsrc, 2);

	if (rta->rta_mp) {
		if ((err = dn_fib_get_nhs(fi, rta->rta_mp, r)) != 0)
			goto failure;
		if (rta->rta_oif && fi->fib_nh->nh_oif != *rta->rta_oif)
			goto err_inval;
		if (rta->rta_gw && memcmp(&fi->fib_nh->nh_gw, rta->rta_gw, 2))
			goto err_inval;
	} else {
		struct dn_fib_nh *nh = fi->fib_nh;
		if (rta->rta_oif)
			nh->nh_oif = *rta->rta_oif;
		if (rta->rta_gw)
			memcpy(&nh->nh_gw, rta->rta_gw, 2);
		nh->nh_flags = r->rtm_flags;
		nh->nh_weight = 1;
	}

	if (r->rtm_type == RTN_NAT) {
		if (rta->rta_gw == NULL || nhs != 1 || rta->rta_oif)
			goto err_inval;
		memcpy(&fi->fib_nh->nh_gw, rta->rta_gw, 2);
		goto link_it;
	}

	if (dn_fib_props[r->rtm_type].error) {
		if (rta->rta_gw || rta->rta_oif || rta->rta_mp)
			goto err_inval;
		goto link_it;
	}

	if (r->rtm_scope > RT_SCOPE_HOST)
		goto err_inval;

	if (r->rtm_scope == RT_SCOPE_HOST) {
		struct dn_fib_nh *nh = fi->fib_nh;

		/* Local address is added */
		if (nhs != 1 || nh->nh_gw)
			goto err_inval;
		nh->nh_scope = RT_SCOPE_NOWHERE;
		nh->nh_dev = dev_get_by_index(&init_net, fi->fib_nh->nh_oif);
		err = -ENODEV;
		if (nh->nh_dev == NULL)
			goto failure;
	} else {
		change_nexthops(fi) {
			if ((err = dn_fib_check_nh(r, fi, nh)) != 0)
				goto failure;
		} endfor_nexthops(fi)
	}

	if (fi->fib_prefsrc) {
		if (r->rtm_type != RTN_LOCAL || rta->rta_dst == NULL ||
		    memcmp(&fi->fib_prefsrc, rta->rta_dst, 2))
			if (dnet_addr_type(fi->fib_prefsrc) != RTN_LOCAL)
				goto err_inval;
	}

link_it:
	if ((ofi = dn_fib_find_info(fi)) != NULL) {
		fi->fib_dead = 1;
		dn_fib_free_info(fi);
		ofi->fib_treeref++;
		return ofi;
	}

	fi->fib_treeref++;
	atomic_inc(&fi->fib_clntref);
	spin_lock(&dn_fib_info_lock);
	fi->fib_next = dn_fib_info_list;
	fi->fib_prev = NULL;
	if (dn_fib_info_list)
		dn_fib_info_list->fib_prev = fi;
	dn_fib_info_list = fi;
	spin_unlock(&dn_fib_info_lock);
	return fi;

err_inval:
	err = -EINVAL;

failure:
	*errp = err;
	if (fi) {
		fi->fib_dead = 1;
		dn_fib_free_info(fi);
	}

	return NULL;
}

int dn_fib_semantic_match(int type, struct dn_fib_info *fi, const struct flowi *fl, struct dn_fib_res *res)
{
	int err = dn_fib_props[type].error;

	if (err == 0) {
		if (fi->fib_flags & RTNH_F_DEAD)
			return 1;

		res->fi = fi;

		switch(type) {
			case RTN_NAT:
				DN_FIB_RES_RESET(*res);
				atomic_inc(&fi->fib_clntref);
				return 0;
			case RTN_UNICAST:
			case RTN_LOCAL:
				for_nexthops(fi) {
					if (nh->nh_flags & RTNH_F_DEAD)
						continue;
					if (!fl->oif || fl->oif == nh->nh_oif)
						break;
				}
				if (nhsel < fi->fib_nhs) {
					res->nh_sel = nhsel;
					atomic_inc(&fi->fib_clntref);
					return 0;
				}
				endfor_nexthops(fi);
				res->fi = NULL;
				return 1;
			default:
				if (net_ratelimit())
					 printk("DECnet: impossible routing event : dn_fib_semantic_match type=%d\n", type);
				res->fi = NULL;
				return -EINVAL;
		}
	}
	return err;
}

void dn_fib_select_multipath(const struct flowi *fl, struct dn_fib_res *res)
{
	struct dn_fib_info *fi = res->fi;
	int w;

	spin_lock_bh(&dn_fib_multipath_lock);
	if (fi->fib_power <= 0) {
		int power = 0;
		change_nexthops(fi) {
			if (!(nh->nh_flags&RTNH_F_DEAD)) {
				power += nh->nh_weight;
				nh->nh_power = nh->nh_weight;
			}
		} endfor_nexthops(fi);
		fi->fib_power = power;
		if (power < 0) {
			spin_unlock_bh(&dn_fib_multipath_lock);
			res->nh_sel = 0;
			return;
		}
	}

	w = jiffies % fi->fib_power;

	change_nexthops(fi) {
		if (!(nh->nh_flags&RTNH_F_DEAD) && nh->nh_power) {
			if ((w -= nh->nh_power) <= 0) {
				nh->nh_power--;
				fi->fib_power--;
				res->nh_sel = nhsel;
				spin_unlock_bh(&dn_fib_multipath_lock);
				return;
			}
		}
	} endfor_nexthops(fi);
	res->nh_sel = 0;
	spin_unlock_bh(&dn_fib_multipath_lock);
}


static int dn_fib_check_attr(struct rtmsg *r, struct rtattr **rta)
{
	int i;

	for(i = 1; i <= RTA_MAX; i++) {
		struct rtattr *attr = rta[i-1];
		if (attr) {
			if (RTA_PAYLOAD(attr) < 4 && RTA_PAYLOAD(attr) != 2)
				return -EINVAL;
			if (i != RTA_MULTIPATH && i != RTA_METRICS &&
			    i != RTA_TABLE)
				rta[i-1] = (struct rtattr *)RTA_DATA(attr);
		}
	}

	return 0;
}

static int dn_fib_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = skb->sk->sk_net;
	struct dn_fib_table *tb;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);

	if (net != &init_net)
		return -EINVAL;

	if (dn_fib_check_attr(r, rta))
		return -EINVAL;

	tb = dn_fib_get_table(rtm_get_table(rta, r->rtm_table), 0);
	if (tb)
		return tb->delete(tb, r, (struct dn_kern_rta *)rta, nlh, &NETLINK_CB(skb));

	return -ESRCH;
}

static int dn_fib_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct net *net = skb->sk->sk_net;
	struct dn_fib_table *tb;
	struct rtattr **rta = arg;
	struct rtmsg *r = NLMSG_DATA(nlh);

	if (net != &init_net)
		return -EINVAL;

	if (dn_fib_check_attr(r, rta))
		return -EINVAL;

	tb = dn_fib_get_table(rtm_get_table(rta, r->rtm_table), 1);
	if (tb)
		return tb->insert(tb, r, (struct dn_kern_rta *)rta, nlh, &NETLINK_CB(skb));

	return -ENOBUFS;
}

static void fib_magic(int cmd, int type, __le16 dst, int dst_len, struct dn_ifaddr *ifa)
{
	struct dn_fib_table *tb;
	struct {
		struct nlmsghdr nlh;
		struct rtmsg rtm;
	} req;
	struct dn_kern_rta rta;

	memset(&req.rtm, 0, sizeof(req.rtm));
	memset(&rta, 0, sizeof(rta));

	if (type == RTN_UNICAST)
		tb = dn_fib_get_table(RT_MIN_TABLE, 1);
	else
		tb = dn_fib_get_table(RT_TABLE_LOCAL, 1);

	if (tb == NULL)
		return;

	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = cmd;
	req.nlh.nlmsg_flags = NLM_F_REQUEST|NLM_F_CREATE|NLM_F_APPEND;
	req.nlh.nlmsg_pid = 0;
	req.nlh.nlmsg_seq = 0;

	req.rtm.rtm_dst_len = dst_len;
	req.rtm.rtm_table = tb->n;
	req.rtm.rtm_protocol = RTPROT_KERNEL;
	req.rtm.rtm_scope = (type != RTN_LOCAL ? RT_SCOPE_LINK : RT_SCOPE_HOST);
	req.rtm.rtm_type = type;

	rta.rta_dst = &dst;
	rta.rta_prefsrc = &ifa->ifa_local;
	rta.rta_oif = &ifa->ifa_dev->dev->ifindex;

	if (cmd == RTM_NEWROUTE)
		tb->insert(tb, &req.rtm, &rta, &req.nlh, NULL);
	else
		tb->delete(tb, &req.rtm, &rta, &req.nlh, NULL);
}

static void dn_fib_add_ifaddr(struct dn_ifaddr *ifa)
{

	fib_magic(RTM_NEWROUTE, RTN_LOCAL, ifa->ifa_local, 16, ifa);

#if 0
	if (!(dev->flags&IFF_UP))
		return;
	/* In the future, we will want to add default routes here */

#endif
}

static void dn_fib_del_ifaddr(struct dn_ifaddr *ifa)
{
	int found_it = 0;
	struct net_device *dev;
	struct dn_dev *dn_db;
	struct dn_ifaddr *ifa2;

	ASSERT_RTNL();

	/* Scan device list */
	read_lock(&dev_base_lock);
	for_each_netdev(&init_net, dev) {
		dn_db = dev->dn_ptr;
		if (dn_db == NULL)
			continue;
		for(ifa2 = dn_db->ifa_list; ifa2; ifa2 = ifa2->ifa_next) {
			if (ifa2->ifa_local == ifa->ifa_local) {
				found_it = 1;
				break;
			}
		}
	}
	read_unlock(&dev_base_lock);

	if (found_it == 0) {
		fib_magic(RTM_DELROUTE, RTN_LOCAL, ifa->ifa_local, 16, ifa);

		if (dnet_addr_type(ifa->ifa_local) != RTN_LOCAL) {
			if (dn_fib_sync_down(ifa->ifa_local, NULL, 0))
				dn_fib_flush();
		}
	}
}

static void dn_fib_disable_addr(struct net_device *dev, int force)
{
	if (dn_fib_sync_down(0, dev, force))
		dn_fib_flush();
	dn_rt_cache_flush(0);
	neigh_ifdown(&dn_neigh_table, dev);
}

static int dn_fib_dnaddr_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct dn_ifaddr *ifa = (struct dn_ifaddr *)ptr;

	switch(event) {
		case NETDEV_UP:
			dn_fib_add_ifaddr(ifa);
			dn_fib_sync_up(ifa->ifa_dev->dev);
			dn_rt_cache_flush(-1);
			break;
		case NETDEV_DOWN:
			dn_fib_del_ifaddr(ifa);
			if (ifa->ifa_dev && ifa->ifa_dev->ifa_list == NULL) {
				dn_fib_disable_addr(ifa->ifa_dev->dev, 1);
			} else {
				dn_rt_cache_flush(-1);
			}
			break;
	}
	return NOTIFY_DONE;
}

static int dn_fib_sync_down(__le16 local, struct net_device *dev, int force)
{
	int ret = 0;
	int scope = RT_SCOPE_NOWHERE;

	if (force)
		scope = -1;

	for_fib_info() {
		/*
		 * This makes no sense for DECnet.... we will almost
		 * certainly have more than one local address the same
		 * over all our interfaces. It needs thinking about
		 * some more.
		 */
		if (local && fi->fib_prefsrc == local) {
			fi->fib_flags |= RTNH_F_DEAD;
			ret++;
		} else if (dev && fi->fib_nhs) {
			int dead = 0;

			change_nexthops(fi) {
				if (nh->nh_flags&RTNH_F_DEAD)
					dead++;
				else if (nh->nh_dev == dev &&
						nh->nh_scope != scope) {
					spin_lock_bh(&dn_fib_multipath_lock);
					nh->nh_flags |= RTNH_F_DEAD;
					fi->fib_power -= nh->nh_power;
					nh->nh_power = 0;
					spin_unlock_bh(&dn_fib_multipath_lock);
					dead++;
				}
			} endfor_nexthops(fi)
			if (dead == fi->fib_nhs) {
				fi->fib_flags |= RTNH_F_DEAD;
				ret++;
			}
		}
	} endfor_fib_info();
	return ret;
}


static int dn_fib_sync_up(struct net_device *dev)
{
	int ret = 0;

	if (!(dev->flags&IFF_UP))
		return 0;

	for_fib_info() {
		int alive = 0;

		change_nexthops(fi) {
			if (!(nh->nh_flags&RTNH_F_DEAD)) {
				alive++;
				continue;
			}
			if (nh->nh_dev == NULL || !(nh->nh_dev->flags&IFF_UP))
				continue;
			if (nh->nh_dev != dev || dev->dn_ptr == NULL)
				continue;
			alive++;
			spin_lock_bh(&dn_fib_multipath_lock);
			nh->nh_power = 0;
			nh->nh_flags &= ~RTNH_F_DEAD;
			spin_unlock_bh(&dn_fib_multipath_lock);
		} endfor_nexthops(fi);

		if (alive > 0) {
			fi->fib_flags &= ~RTNH_F_DEAD;
			ret++;
		}
	} endfor_fib_info();
	return ret;
}

static struct notifier_block dn_fib_dnaddr_notifier = {
	.notifier_call = dn_fib_dnaddr_event,
};

void __exit dn_fib_cleanup(void)
{
	dn_fib_table_cleanup();
	dn_fib_rules_cleanup();

	unregister_dnaddr_notifier(&dn_fib_dnaddr_notifier);
}


void __init dn_fib_init(void)
{
	dn_fib_table_init();
	dn_fib_rules_init();

	register_dnaddr_notifier(&dn_fib_dnaddr_notifier);

	rtnl_register(PF_DECnet, RTM_NEWROUTE, dn_fib_rtm_newroute, NULL);
	rtnl_register(PF_DECnet, RTM_DELROUTE, dn_fib_rtm_delroute, NULL);
}


