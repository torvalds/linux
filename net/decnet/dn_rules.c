
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Routing Forwarding Information Base (Rules)
 *
 * Author:      Steve Whitehouse <SteveW@ACM.org>
 *              Mostly copied from Alexey Kuznetsov's ipv4/fib_rules.c
 *
 *
 * Changes:
 *
 */
#include <linux/config.h>
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
#include <linux/in_route.h>
#include <asm/atomic.h>
#include <asm/uaccess.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/dn.h>
#include <net/dn_fib.h>
#include <net/dn_neigh.h>
#include <net/dn_dev.h>

struct dn_fib_rule
{
	struct dn_fib_rule	*r_next;
	atomic_t		r_clntref;
	u32			r_preference;
	unsigned char		r_table;
	unsigned char		r_action;
	unsigned char		r_dst_len;
	unsigned char		r_src_len;
	dn_address		r_src;
	dn_address		r_srcmask;
	dn_address		r_dst;
	dn_address		r_dstmask;
	dn_address		r_srcmap;
	u8			r_flags;
#ifdef CONFIG_DECNET_ROUTE_FWMARK
	u32			r_fwmark;
#endif
	int			r_ifindex;
	char			r_ifname[IFNAMSIZ];
	int			r_dead;
};

static struct dn_fib_rule default_rule = {
	.r_clntref =		ATOMIC_INIT(2),
	.r_preference =		0x7fff,
	.r_table =		RT_TABLE_MAIN,
	.r_action =		RTN_UNICAST
};

static struct dn_fib_rule *dn_fib_rules = &default_rule;
static DEFINE_RWLOCK(dn_fib_rules_lock);


int dn_fib_rtm_delrule(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct dn_fib_rule *r, **rp;
	int err = -ESRCH;

	for(rp=&dn_fib_rules; (r=*rp) != NULL; rp = &r->r_next) {
		if ((!rta[RTA_SRC-1] || memcmp(RTA_DATA(rta[RTA_SRC-1]), &r->r_src, 2) == 0) &&
			rtm->rtm_src_len == r->r_src_len &&
			rtm->rtm_dst_len == r->r_dst_len &&
			(!rta[RTA_DST-1] || memcmp(RTA_DATA(rta[RTA_DST-1]), &r->r_dst, 2) == 0) &&
#ifdef CONFIG_DECNET_ROUTE_FWMARK
			(!rta[RTA_PROTOINFO-1] || memcmp(RTA_DATA(rta[RTA_PROTOINFO-1]), &r->r_fwmark, 4) == 0) &&
#endif
			(!rtm->rtm_type || rtm->rtm_type == r->r_action) &&
			(!rta[RTA_PRIORITY-1] || memcmp(RTA_DATA(rta[RTA_PRIORITY-1]), &r->r_preference, 4) == 0) &&
			(!rta[RTA_IIF-1] || rtattr_strcmp(rta[RTA_IIF-1], r->r_ifname) == 0) &&
			(!rtm->rtm_table || (r && rtm->rtm_table == r->r_table))) {

			err = -EPERM;
			if (r == &default_rule)
				break;

			write_lock_bh(&dn_fib_rules_lock);
			*rp = r->r_next;
			r->r_dead = 1;
			write_unlock_bh(&dn_fib_rules_lock);
			dn_fib_rule_put(r);
			err = 0;
			break;
		}
	}

	return err;
}

void dn_fib_rule_put(struct dn_fib_rule *r)
{
	if (atomic_dec_and_test(&r->r_clntref)) {
		if (r->r_dead)
			kfree(r);
		else
			printk(KERN_DEBUG "Attempt to free alive dn_fib_rule\n");
	}
}


int dn_fib_rtm_newrule(struct sk_buff *skb, struct nlmsghdr *nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct dn_fib_rule *r, *new_r, **rp;
	unsigned char table_id;

	if (rtm->rtm_src_len > 16 || rtm->rtm_dst_len > 16)
		return -EINVAL;

	if (rta[RTA_IIF-1] && RTA_PAYLOAD(rta[RTA_IIF-1]) > IFNAMSIZ)
		return -EINVAL;

	if (rtm->rtm_type == RTN_NAT)
		return -EINVAL;

	table_id = rtm->rtm_table;
	if (table_id == RT_TABLE_UNSPEC) {
		struct dn_fib_table *tb;
		if (rtm->rtm_type == RTN_UNICAST) {
			if ((tb = dn_fib_empty_table()) == NULL)
				return -ENOBUFS;
			table_id = tb->n;
		}
	}

	new_r = kmalloc(sizeof(*new_r), GFP_KERNEL);
	if (!new_r)
		return -ENOMEM;
	memset(new_r, 0, sizeof(*new_r));
	if (rta[RTA_SRC-1])
		memcpy(&new_r->r_src, RTA_DATA(rta[RTA_SRC-1]), 2);
	if (rta[RTA_DST-1])
		memcpy(&new_r->r_dst, RTA_DATA(rta[RTA_DST-1]), 2);
	if (rta[RTA_GATEWAY-1])
		memcpy(&new_r->r_srcmap, RTA_DATA(rta[RTA_GATEWAY-1]), 2);
	new_r->r_src_len = rtm->rtm_src_len;
	new_r->r_dst_len = rtm->rtm_dst_len;
	new_r->r_srcmask = dnet_make_mask(rtm->rtm_src_len);
	new_r->r_dstmask = dnet_make_mask(rtm->rtm_dst_len);
#ifdef CONFIG_DECNET_ROUTE_FWMARK
	if (rta[RTA_PROTOINFO-1])
		memcpy(&new_r->r_fwmark, RTA_DATA(rta[RTA_PROTOINFO-1]), 4);
#endif
	new_r->r_action = rtm->rtm_type;
	new_r->r_flags = rtm->rtm_flags;
	if (rta[RTA_PRIORITY-1])
		memcpy(&new_r->r_preference, RTA_DATA(rta[RTA_PRIORITY-1]), 4);
	new_r->r_table = table_id;
	if (rta[RTA_IIF-1]) {
		struct net_device *dev;
		rtattr_strlcpy(new_r->r_ifname, rta[RTA_IIF-1], IFNAMSIZ);
		new_r->r_ifindex = -1;
		dev = dev_get_by_name(new_r->r_ifname);
		if (dev) {
			new_r->r_ifindex = dev->ifindex;
			dev_put(dev);
		}
	}

	rp = &dn_fib_rules;
	if (!new_r->r_preference) {
		r = dn_fib_rules;
		if (r && (r = r->r_next) != NULL) {
			rp = &dn_fib_rules->r_next;
			if (r->r_preference)
				new_r->r_preference = r->r_preference - 1;
		}
	}

	while((r=*rp) != NULL) {
		if (r->r_preference > new_r->r_preference)
			break;
		rp = &r->r_next;
	}

	new_r->r_next = r;
	atomic_inc(&new_r->r_clntref);
	write_lock_bh(&dn_fib_rules_lock);
	*rp = new_r;
	write_unlock_bh(&dn_fib_rules_lock);
	return 0;
}


int dn_fib_lookup(const struct flowi *flp, struct dn_fib_res *res)
{
	struct dn_fib_rule *r, *policy;
	struct dn_fib_table *tb;
	dn_address saddr = flp->fld_src;
	dn_address daddr = flp->fld_dst;
	int err;

	read_lock(&dn_fib_rules_lock);
	for(r = dn_fib_rules; r; r = r->r_next) {
		if (((saddr^r->r_src) & r->r_srcmask) ||
		    ((daddr^r->r_dst) & r->r_dstmask) ||
#ifdef CONFIG_DECNET_ROUTE_FWMARK
		    (r->r_fwmark && r->r_fwmark != flp->fld_fwmark) ||
#endif
		    (r->r_ifindex && r->r_ifindex != flp->iif))
			continue;

		switch(r->r_action) {
			case RTN_UNICAST:
			case RTN_NAT:
				policy = r;
				break;
			case RTN_UNREACHABLE:
				read_unlock(&dn_fib_rules_lock);
				return -ENETUNREACH;
			default:
			case RTN_BLACKHOLE:
				read_unlock(&dn_fib_rules_lock);
				return -EINVAL;
			case RTN_PROHIBIT:
				read_unlock(&dn_fib_rules_lock);
				return -EACCES;
		}

		if ((tb = dn_fib_get_table(r->r_table, 0)) == NULL)
			continue;
		err = tb->lookup(tb, flp, res);
		if (err == 0) {
			res->r = policy;
			if (policy)
				atomic_inc(&policy->r_clntref);
			read_unlock(&dn_fib_rules_lock);
			return 0;
		}
		if (err < 0 && err != -EAGAIN) {
			read_unlock(&dn_fib_rules_lock);
			return err;
		}
	}

	read_unlock(&dn_fib_rules_lock);
	return -ESRCH;
}

unsigned dnet_addr_type(__u16 addr)
{
	struct flowi fl = { .nl_u = { .dn_u = { .daddr = addr } } };
	struct dn_fib_res res;
	unsigned ret = RTN_UNICAST;
	struct dn_fib_table *tb = dn_fib_tables[RT_TABLE_LOCAL];

	res.r = NULL;

	if (tb) {
		if (!tb->lookup(tb, &fl, &res)) {
			ret = res.type;
			dn_fib_res_put(&res);
		}
	}
	return ret;
}

__u16 dn_fib_rules_policy(__u16 saddr, struct dn_fib_res *res, unsigned *flags)
{
	struct dn_fib_rule *r = res->r;

	if (r->r_action == RTN_NAT) {
		int addrtype = dnet_addr_type(r->r_srcmap);

		if (addrtype == RTN_NAT) {
			saddr = (saddr&~r->r_srcmask)|r->r_srcmap;
			*flags |= RTCF_SNAT;
		} else if (addrtype == RTN_LOCAL || r->r_srcmap == 0) {
			saddr = r->r_srcmap;
			*flags |= RTCF_MASQ;
		}
	}
	return saddr;
}

static void dn_fib_rules_detach(struct net_device *dev)
{
	struct dn_fib_rule *r;

	for(r = dn_fib_rules; r; r = r->r_next) {
		if (r->r_ifindex == dev->ifindex) {
			write_lock_bh(&dn_fib_rules_lock);
			r->r_ifindex = -1;
			write_unlock_bh(&dn_fib_rules_lock);
		}
	}
}

static void dn_fib_rules_attach(struct net_device *dev)
{
	struct dn_fib_rule *r;

	for(r = dn_fib_rules; r; r = r->r_next) {
		if (r->r_ifindex == -1 && strcmp(dev->name, r->r_ifname) == 0) {
			write_lock_bh(&dn_fib_rules_lock);
			r->r_ifindex = dev->ifindex;
			write_unlock_bh(&dn_fib_rules_lock);
		}
	}
}

static int dn_fib_rules_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct net_device *dev = ptr;

	switch(event) {
		case NETDEV_UNREGISTER:
			dn_fib_rules_detach(dev);
			dn_fib_sync_down(0, dev, 1);
		case NETDEV_REGISTER:
			dn_fib_rules_attach(dev);
			dn_fib_sync_up(dev);
	}

	return NOTIFY_DONE;
}


static struct notifier_block dn_fib_rules_notifier = {
	.notifier_call =	dn_fib_rules_event,
};

static int dn_fib_fill_rule(struct sk_buff *skb, struct dn_fib_rule *r, struct netlink_callback *cb)
{
	struct rtmsg *rtm;
	struct nlmsghdr *nlh;
	unsigned char *b = skb->tail;


	nlh = NLMSG_PUT(skb, NETLINK_CREDS(cb->skb)->pid, cb->nlh->nlmsg_seq, RTM_NEWRULE, sizeof(*rtm));
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_DECnet;
	rtm->rtm_dst_len = r->r_dst_len;
	rtm->rtm_src_len = r->r_src_len;
	rtm->rtm_tos = 0;
#ifdef CONFIG_DECNET_ROUTE_FWMARK
	if (r->r_fwmark)
		RTA_PUT(skb, RTA_PROTOINFO, 4, &r->r_fwmark);
#endif
	rtm->rtm_table = r->r_table;
	rtm->rtm_protocol = 0;
	rtm->rtm_scope = 0;
	rtm->rtm_type = r->r_action;
	rtm->rtm_flags = r->r_flags;

	if (r->r_dst_len)
		RTA_PUT(skb, RTA_DST, 2, &r->r_dst);
	if (r->r_src_len)
		RTA_PUT(skb, RTA_SRC, 2, &r->r_src);
	if (r->r_ifname[0])
		RTA_PUT(skb, RTA_IIF, IFNAMSIZ, &r->r_ifname);
	if (r->r_preference)
		RTA_PUT(skb, RTA_PRIORITY, 4, &r->r_preference);
	if (r->r_srcmap)
		RTA_PUT(skb, RTA_GATEWAY, 2, &r->r_srcmap);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_trim(skb, b - skb->data);
	return -1;
}

int dn_fib_dump_rules(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->args[0];
	struct dn_fib_rule *r;

	read_lock(&dn_fib_rules_lock);
	for(r = dn_fib_rules, idx = 0; r; r = r->r_next, idx++) {
		if (idx < s_idx)
			continue;
		if (dn_fib_fill_rule(skb, r, cb) < 0)
			break;
	}
	read_unlock(&dn_fib_rules_lock);
	cb->args[0] = idx;

	return skb->len;
}

void __init dn_fib_rules_init(void)
{
	register_netdevice_notifier(&dn_fib_rules_notifier);
}

void __exit dn_fib_rules_cleanup(void)
{
	unregister_netdevice_notifier(&dn_fib_rules_notifier);
}


