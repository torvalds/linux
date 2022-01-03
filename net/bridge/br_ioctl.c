// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Ioctl handler
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/if_bridge.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/times.h>
#include <net/net_namespace.h>
#include <linux/uaccess.h>
#include "br_private.h"

static int get_bridge_ifindices(struct net *net, int *indices, int num)
{
	struct net_device *dev;
	int i = 0;

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		if (i >= num)
			break;
		if (netif_is_bridge_master(dev))
			indices[i++] = dev->ifindex;
	}
	rcu_read_unlock();

	return i;
}

/* called with RTNL */
static void get_port_ifindices(struct net_bridge *br, int *ifindices, int num)
{
	struct net_bridge_port *p;

	list_for_each_entry(p, &br->port_list, list) {
		if (p->port_no < num)
			ifindices[p->port_no] = p->dev->ifindex;
	}
}

/*
 * Format up to a page worth of forwarding table entries
 * userbuf -- where to copy result
 * maxnum  -- maximum number of entries desired
 *            (limited to a page for sanity)
 * offset  -- number of records to skip
 */
static int get_fdb_entries(struct net_bridge *br, void __user *userbuf,
			   unsigned long maxnum, unsigned long offset)
{
	int num;
	void *buf;
	size_t size;

	/* Clamp size to PAGE_SIZE, test maxnum to avoid overflow */
	if (maxnum > PAGE_SIZE/sizeof(struct __fdb_entry))
		maxnum = PAGE_SIZE/sizeof(struct __fdb_entry);

	size = maxnum * sizeof(struct __fdb_entry);

	buf = kmalloc(size, GFP_USER);
	if (!buf)
		return -ENOMEM;

	num = br_fdb_fillbuf(br, buf, maxnum, offset);
	if (num > 0) {
		if (copy_to_user(userbuf, buf,
				 array_size(num, sizeof(struct __fdb_entry))))
			num = -EFAULT;
	}
	kfree(buf);

	return num;
}

/* called with RTNL */
static int add_del_if(struct net_bridge *br, int ifindex, int isadd)
{
	struct net *net = dev_net(br->dev);
	struct net_device *dev;
	int ret;

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	dev = __dev_get_by_index(net, ifindex);
	if (dev == NULL)
		return -EINVAL;

	if (isadd)
		ret = br_add_if(br, dev, NULL);
	else
		ret = br_del_if(br, dev);

	return ret;
}

/*
 * Legacy ioctl's through SIOCDEVPRIVATE
 * This interface is deprecated because it was too difficult
 * to do the translation for 32/64bit ioctl compatibility.
 */
int br_dev_siocdevprivate(struct net_device *dev, struct ifreq *rq, void __user *data, int cmd)
{
	struct net_bridge *br = netdev_priv(dev);
	struct net_bridge_port *p = NULL;
	unsigned long args[4];
	void __user *argp;
	int ret = -EOPNOTSUPP;

	if (in_compat_syscall()) {
		unsigned int cargs[4];

		if (copy_from_user(cargs, data, sizeof(cargs)))
			return -EFAULT;

		args[0] = cargs[0];
		args[1] = cargs[1];
		args[2] = cargs[2];
		args[3] = cargs[3];

		argp = compat_ptr(args[1]);
	} else {
		if (copy_from_user(args, data, sizeof(args)))
			return -EFAULT;

		argp = (void __user *)args[1];
	}

	switch (args[0]) {
	case BRCTL_ADD_IF:
	case BRCTL_DEL_IF:
		return add_del_if(br, args[1], args[0] == BRCTL_ADD_IF);

	case BRCTL_GET_BRIDGE_INFO:
	{
		struct __bridge_info b;

		memset(&b, 0, sizeof(struct __bridge_info));
		rcu_read_lock();
		memcpy(&b.designated_root, &br->designated_root, 8);
		memcpy(&b.bridge_id, &br->bridge_id, 8);
		b.root_path_cost = br->root_path_cost;
		b.max_age = jiffies_to_clock_t(br->max_age);
		b.hello_time = jiffies_to_clock_t(br->hello_time);
		b.forward_delay = br->forward_delay;
		b.bridge_max_age = br->bridge_max_age;
		b.bridge_hello_time = br->bridge_hello_time;
		b.bridge_forward_delay = jiffies_to_clock_t(br->bridge_forward_delay);
		b.topology_change = br->topology_change;
		b.topology_change_detected = br->topology_change_detected;
		b.root_port = br->root_port;

		b.stp_enabled = (br->stp_enabled != BR_NO_STP);
		b.ageing_time = jiffies_to_clock_t(br->ageing_time);
		b.hello_timer_value = br_timer_value(&br->hello_timer);
		b.tcn_timer_value = br_timer_value(&br->tcn_timer);
		b.topology_change_timer_value = br_timer_value(&br->topology_change_timer);
		b.gc_timer_value = br_timer_value(&br->gc_work.timer);
		rcu_read_unlock();

		if (copy_to_user((void __user *)args[1], &b, sizeof(b)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_GET_PORT_LIST:
	{
		int num, *indices;

		num = args[2];
		if (num < 0)
			return -EINVAL;
		if (num == 0)
			num = 256;
		if (num > BR_MAX_PORTS)
			num = BR_MAX_PORTS;

		indices = kcalloc(num, sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		get_port_ifindices(br, indices, num);
		if (copy_to_user(argp, indices, array_size(num, sizeof(int))))
			num =  -EFAULT;
		kfree(indices);
		return num;
	}

	case BRCTL_SET_BRIDGE_FORWARD_DELAY:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		ret = br_set_forward_delay(br, args[1]);
		break;

	case BRCTL_SET_BRIDGE_HELLO_TIME:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		ret = br_set_hello_time(br, args[1]);
		break;

	case BRCTL_SET_BRIDGE_MAX_AGE:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		ret = br_set_max_age(br, args[1]);
		break;

	case BRCTL_SET_AGEING_TIME:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		ret = br_set_ageing_time(br, args[1]);
		break;

	case BRCTL_GET_PORT_INFO:
	{
		struct __port_info p;
		struct net_bridge_port *pt;

		rcu_read_lock();
		if ((pt = br_get_port(br, args[2])) == NULL) {
			rcu_read_unlock();
			return -EINVAL;
		}

		memset(&p, 0, sizeof(struct __port_info));
		memcpy(&p.designated_root, &pt->designated_root, 8);
		memcpy(&p.designated_bridge, &pt->designated_bridge, 8);
		p.port_id = pt->port_id;
		p.designated_port = pt->designated_port;
		p.path_cost = pt->path_cost;
		p.designated_cost = pt->designated_cost;
		p.state = pt->state;
		p.top_change_ack = pt->topology_change_ack;
		p.config_pending = pt->config_pending;
		p.message_age_timer_value = br_timer_value(&pt->message_age_timer);
		p.forward_delay_timer_value = br_timer_value(&pt->forward_delay_timer);
		p.hold_timer_value = br_timer_value(&pt->hold_timer);

		rcu_read_unlock();

		if (copy_to_user(argp, &p, sizeof(p)))
			return -EFAULT;

		return 0;
	}

	case BRCTL_SET_BRIDGE_STP_STATE:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		ret = br_stp_set_enabled(br, args[1], NULL);
		break;

	case BRCTL_SET_BRIDGE_PRIORITY:
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		br_stp_set_bridge_priority(br, args[1]);
		ret = 0;
		break;

	case BRCTL_SET_PORT_PRIORITY:
	{
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&br->lock);
		if ((p = br_get_port(br, args[1])) == NULL)
			ret = -EINVAL;
		else
			ret = br_stp_set_port_priority(p, args[2]);
		spin_unlock_bh(&br->lock);
		break;
	}

	case BRCTL_SET_PATH_COST:
	{
		if (!ns_capable(dev_net(dev)->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		spin_lock_bh(&br->lock);
		if ((p = br_get_port(br, args[1])) == NULL)
			ret = -EINVAL;
		else
			ret = br_stp_set_path_cost(p, args[2]);
		spin_unlock_bh(&br->lock);
		break;
	}

	case BRCTL_GET_FDB_ENTRIES:
		return get_fdb_entries(br, argp, args[2], args[3]);
	}

	if (!ret) {
		if (p)
			br_ifinfo_notify(RTM_NEWLINK, NULL, p);
		else
			netdev_state_change(br->dev);
	}

	return ret;
}

static int old_deviceless(struct net *net, void __user *uarg)
{
	unsigned long args[3];

	if (copy_from_user(args, uarg, sizeof(args)))
		return -EFAULT;

	switch (args[0]) {
	case BRCTL_GET_VERSION:
		return BRCTL_VERSION;

	case BRCTL_GET_BRIDGES:
	{
		int *indices;
		int ret = 0;

		if (args[2] >= 2048)
			return -ENOMEM;
		indices = kcalloc(args[2], sizeof(int), GFP_KERNEL);
		if (indices == NULL)
			return -ENOMEM;

		args[2] = get_bridge_ifindices(net, indices, args[2]);

		ret = copy_to_user((void __user *)args[1], indices,
				   array_size(args[2], sizeof(int)))
			? -EFAULT : args[2];

		kfree(indices);
		return ret;
	}

	case BRCTL_ADD_BRIDGE:
	case BRCTL_DEL_BRIDGE:
	{
		char buf[IFNAMSIZ];

		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;

		if (copy_from_user(buf, (void __user *)args[1], IFNAMSIZ))
			return -EFAULT;

		buf[IFNAMSIZ-1] = 0;

		if (args[0] == BRCTL_ADD_BRIDGE)
			return br_add_bridge(net, buf);

		return br_del_bridge(net, buf);
	}
	}

	return -EOPNOTSUPP;
}

int br_ioctl_stub(struct net *net, struct net_bridge *br, unsigned int cmd,
		  struct ifreq *ifr, void __user *uarg)
{
	int ret = -EOPNOTSUPP;

	rtnl_lock();

	switch (cmd) {
	case SIOCGIFBR:
	case SIOCSIFBR:
		ret = old_deviceless(net, uarg);
		break;
	case SIOCBRADDBR:
	case SIOCBRDELBR:
	{
		char buf[IFNAMSIZ];

		if (!ns_capable(net->user_ns, CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		if (copy_from_user(buf, uarg, IFNAMSIZ)) {
			ret = -EFAULT;
			break;
		}

		buf[IFNAMSIZ-1] = 0;
		if (cmd == SIOCBRADDBR)
			ret = br_add_bridge(net, buf);
		else
			ret = br_del_bridge(net, buf);
	}
		break;
	case SIOCBRADDIF:
	case SIOCBRDELIF:
		ret = add_del_if(br, ifr->ifr_ifindex, cmd == SIOCBRADDIF);
		break;
	}

	rtnl_unlock();

	return ret;
}
