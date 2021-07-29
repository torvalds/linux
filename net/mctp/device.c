// SPDX-License-Identifier: GPL-2.0
/*
 * Management Component Transport Protocol (MCTP) - device implementation.
 *
 * Copyright (c) 2021 Code Construct
 * Copyright (c) 2021 Google
 */

#include <linux/if_link.h>
#include <linux/mctp.h>
#include <linux/netdevice.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>

#include <net/addrconf.h>
#include <net/netlink.h>
#include <net/mctp.h>
#include <net/mctpdevice.h>
#include <net/sock.h>

struct mctp_dump_cb {
	int h;
	int idx;
	size_t a_idx;
};

/* unlocked: caller must hold rcu_read_lock */
struct mctp_dev *__mctp_dev_get(const struct net_device *dev)
{
	return rcu_dereference(dev->mctp_ptr);
}

struct mctp_dev *mctp_dev_get_rtnl(const struct net_device *dev)
{
	return rtnl_dereference(dev->mctp_ptr);
}

static void mctp_dev_destroy(struct mctp_dev *mdev)
{
	struct net_device *dev = mdev->dev;

	dev_put(dev);
	kfree_rcu(mdev, rcu);
}

static int mctp_fill_addrinfo(struct sk_buff *skb, struct netlink_callback *cb,
			      struct mctp_dev *mdev, mctp_eid_t eid)
{
	struct ifaddrmsg *hdr;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RTM_NEWADDR, sizeof(*hdr), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;

	hdr = nlmsg_data(nlh);
	hdr->ifa_family = AF_MCTP;
	hdr->ifa_prefixlen = 0;
	hdr->ifa_flags = 0;
	hdr->ifa_scope = 0;
	hdr->ifa_index = mdev->dev->ifindex;

	if (nla_put_u8(skb, IFA_LOCAL, eid))
		goto cancel;

	if (nla_put_u8(skb, IFA_ADDRESS, eid))
		goto cancel;

	nlmsg_end(skb, nlh);

	return 0;

cancel:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int mctp_dump_dev_addrinfo(struct mctp_dev *mdev, struct sk_buff *skb,
				  struct netlink_callback *cb)
{
	struct mctp_dump_cb *mcb = (void *)cb->ctx;
	int rc = 0;

	for (; mcb->a_idx < mdev->num_addrs; mcb->a_idx++) {
		rc = mctp_fill_addrinfo(skb, cb, mdev, mdev->addrs[mcb->a_idx]);
		if (rc < 0)
			break;
	}

	return rc;
}

static int mctp_dump_addrinfo(struct sk_buff *skb, struct netlink_callback *cb)
{
	struct mctp_dump_cb *mcb = (void *)cb->ctx;
	struct net *net = sock_net(skb->sk);
	struct hlist_head *head;
	struct net_device *dev;
	struct ifaddrmsg *hdr;
	struct mctp_dev *mdev;
	int ifindex;
	int idx, rc;

	hdr = nlmsg_data(cb->nlh);
	// filter by ifindex if requested
	ifindex = hdr->ifa_index;

	rcu_read_lock();
	for (; mcb->h < NETDEV_HASHENTRIES; mcb->h++, mcb->idx = 0) {
		idx = 0;
		head = &net->dev_index_head[mcb->h];
		hlist_for_each_entry_rcu(dev, head, index_hlist) {
			if (idx >= mcb->idx &&
			    (ifindex == 0 || ifindex == dev->ifindex)) {
				mdev = __mctp_dev_get(dev);
				if (mdev) {
					rc = mctp_dump_dev_addrinfo(mdev,
								    skb, cb);
					// Error indicates full buffer, this
					// callback will get retried.
					if (rc < 0)
						goto out;
				}
			}
			idx++;
			// reset for next iteration
			mcb->a_idx = 0;
		}
	}
out:
	rcu_read_unlock();
	mcb->idx = idx;

	return skb->len;
}

static const struct nla_policy ifa_mctp_policy[IFA_MAX + 1] = {
	[IFA_ADDRESS]		= { .type = NLA_U8 },
	[IFA_LOCAL]		= { .type = NLA_U8 },
};

static int mctp_rtm_newaddr(struct sk_buff *skb, struct nlmsghdr *nlh,
			    struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[IFA_MAX + 1];
	struct net_device *dev;
	struct mctp_addr *addr;
	struct mctp_dev *mdev;
	struct ifaddrmsg *ifm;
	unsigned long flags;
	u8 *tmp_addrs;
	int rc;

	rc = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_mctp_policy,
			 extack);
	if (rc < 0)
		return rc;

	ifm = nlmsg_data(nlh);

	if (tb[IFA_LOCAL])
		addr = nla_data(tb[IFA_LOCAL]);
	else if (tb[IFA_ADDRESS])
		addr = nla_data(tb[IFA_ADDRESS]);
	else
		return -EINVAL;

	/* find device */
	dev = __dev_get_by_index(net, ifm->ifa_index);
	if (!dev)
		return -ENODEV;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODEV;

	if (!mctp_address_ok(addr->s_addr))
		return -EINVAL;

	/* Prevent duplicates. Under RTNL so don't need to lock for reading */
	if (memchr(mdev->addrs, addr->s_addr, mdev->num_addrs))
		return -EEXIST;

	tmp_addrs = kmalloc(mdev->num_addrs + 1, GFP_KERNEL);
	if (!tmp_addrs)
		return -ENOMEM;
	memcpy(tmp_addrs, mdev->addrs, mdev->num_addrs);
	tmp_addrs[mdev->num_addrs] = addr->s_addr;

	/* Lock to write */
	spin_lock_irqsave(&mdev->addrs_lock, flags);
	mdev->num_addrs++;
	swap(mdev->addrs, tmp_addrs);
	spin_unlock_irqrestore(&mdev->addrs_lock, flags);

	kfree(tmp_addrs);

	mctp_route_add_local(mdev, addr->s_addr);

	return 0;
}

static int mctp_rtm_deladdr(struct sk_buff *skb, struct nlmsghdr *nlh,
			    struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct nlattr *tb[IFA_MAX + 1];
	struct net_device *dev;
	struct mctp_addr *addr;
	struct mctp_dev *mdev;
	struct ifaddrmsg *ifm;
	unsigned long flags;
	u8 *pos;
	int rc;

	rc = nlmsg_parse(nlh, sizeof(*ifm), tb, IFA_MAX, ifa_mctp_policy,
			 extack);
	if (rc < 0)
		return rc;

	ifm = nlmsg_data(nlh);

	if (tb[IFA_LOCAL])
		addr = nla_data(tb[IFA_LOCAL]);
	else if (tb[IFA_ADDRESS])
		addr = nla_data(tb[IFA_ADDRESS]);
	else
		return -EINVAL;

	/* find device */
	dev = __dev_get_by_index(net, ifm->ifa_index);
	if (!dev)
		return -ENODEV;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODEV;

	pos = memchr(mdev->addrs, addr->s_addr, mdev->num_addrs);
	if (!pos)
		return -ENOENT;

	rc = mctp_route_remove_local(mdev, addr->s_addr);
	// we can ignore -ENOENT in the case a route was already removed
	if (rc < 0 && rc != -ENOENT)
		return rc;

	spin_lock_irqsave(&mdev->addrs_lock, flags);
	memmove(pos, pos + 1, mdev->num_addrs - 1 - (pos - mdev->addrs));
	mdev->num_addrs--;
	spin_unlock_irqrestore(&mdev->addrs_lock, flags);

	return 0;
}

static struct mctp_dev *mctp_add_dev(struct net_device *dev)
{
	struct mctp_dev *mdev;

	ASSERT_RTNL();

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return ERR_PTR(-ENOMEM);

	spin_lock_init(&mdev->addrs_lock);

	mdev->net = mctp_default_net(dev_net(dev));

	/* associate to net_device */
	rcu_assign_pointer(dev->mctp_ptr, mdev);
	dev_hold(dev);
	mdev->dev = dev;

	return mdev;
}

static int mctp_fill_link_af(struct sk_buff *skb,
			     const struct net_device *dev, u32 ext_filter_mask)
{
	struct mctp_dev *mdev;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return -ENODATA;
	if (nla_put_u32(skb, IFLA_MCTP_NET, mdev->net))
		return -EMSGSIZE;
	return 0;
}

static size_t mctp_get_link_af_size(const struct net_device *dev,
				    u32 ext_filter_mask)
{
	struct mctp_dev *mdev;
	unsigned int ret;

	/* caller holds RCU */
	mdev = __mctp_dev_get(dev);
	if (!mdev)
		return 0;
	ret = nla_total_size(4); /* IFLA_MCTP_NET */
	return ret;
}

static const struct nla_policy ifla_af_mctp_policy[IFLA_MCTP_MAX + 1] = {
	[IFLA_MCTP_NET]		= { .type = NLA_U32 },
};

static int mctp_set_link_af(struct net_device *dev, const struct nlattr *attr,
			    struct netlink_ext_ack *extack)
{
	struct nlattr *tb[IFLA_MCTP_MAX + 1];
	struct mctp_dev *mdev;
	int rc;

	rc = nla_parse_nested(tb, IFLA_MCTP_MAX, attr, ifla_af_mctp_policy,
			      NULL);
	if (rc)
		return rc;

	mdev = mctp_dev_get_rtnl(dev);
	if (!mdev)
		return 0;

	if (tb[IFLA_MCTP_NET])
		WRITE_ONCE(mdev->net, nla_get_u32(tb[IFLA_MCTP_NET]));

	return 0;
}

static void mctp_unregister(struct net_device *dev)
{
	struct mctp_dev *mdev;

	mdev = mctp_dev_get_rtnl(dev);

	if (!mdev)
		return;

	RCU_INIT_POINTER(mdev->dev->mctp_ptr, NULL);

	mctp_route_remove_dev(mdev);
	mctp_neigh_remove_dev(mdev);
	kfree(mdev->addrs);

	mctp_dev_destroy(mdev);
}

static int mctp_register(struct net_device *dev)
{
	struct mctp_dev *mdev;

	/* Already registered? */
	if (rtnl_dereference(dev->mctp_ptr))
		return 0;

	/* only register specific types; MCTP-specific and loopback for now */
	if (dev->type != ARPHRD_MCTP && dev->type != ARPHRD_LOOPBACK)
		return 0;

	mdev = mctp_add_dev(dev);
	if (IS_ERR(mdev))
		return PTR_ERR(mdev);

	return 0;
}

static int mctp_dev_notify(struct notifier_block *this, unsigned long event,
			   void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	int rc;

	switch (event) {
	case NETDEV_REGISTER:
		rc = mctp_register(dev);
		if (rc)
			return notifier_from_errno(rc);
		break;
	case NETDEV_UNREGISTER:
		mctp_unregister(dev);
		break;
	}

	return NOTIFY_OK;
}

static struct rtnl_af_ops mctp_af_ops = {
	.family = AF_MCTP,
	.fill_link_af = mctp_fill_link_af,
	.get_link_af_size = mctp_get_link_af_size,
	.set_link_af = mctp_set_link_af,
};

static struct notifier_block mctp_dev_nb = {
	.notifier_call = mctp_dev_notify,
	.priority = ADDRCONF_NOTIFY_PRIORITY,
};

void __init mctp_device_init(void)
{
	register_netdevice_notifier(&mctp_dev_nb);

	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_GETADDR,
			     NULL, mctp_dump_addrinfo, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_NEWADDR,
			     mctp_rtm_newaddr, NULL, 0);
	rtnl_register_module(THIS_MODULE, PF_MCTP, RTM_DELADDR,
			     mctp_rtm_deladdr, NULL, 0);
	rtnl_af_register(&mctp_af_ops);
}

void __exit mctp_device_exit(void)
{
	rtnl_af_unregister(&mctp_af_ops);
	rtnl_unregister(PF_MCTP, RTM_DELADDR);
	rtnl_unregister(PF_MCTP, RTM_NEWADDR);
	rtnl_unregister(PF_MCTP, RTM_GETADDR);

	unregister_netdevice_notifier(&mctp_dev_nb);
}
