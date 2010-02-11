/*
 *	Sysfs attributes of bridge ports
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/times.h>

#include "br_private.h"

#define to_dev(obj)	container_of(obj, struct device, kobj)
#define to_bridge(cd)	((struct net_bridge *)netdev_priv(to_net_dev(cd)))

/*
 * Common code for storing bridge parameters.
 */
static ssize_t store_bridge_parm(struct device *d,
				 const char *buf, size_t len,
				 int (*set)(struct net_bridge *, unsigned long))
{
	struct net_bridge *br = to_bridge(d);
	char *endp;
	unsigned long val;
	int err;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;

	spin_lock_bh(&br->lock);
	err = (*set)(br, val);
	spin_unlock_bh(&br->lock);
	return err ? err : len;
}


static ssize_t show_forward_delay(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->forward_delay));
}

static int set_forward_delay(struct net_bridge *br, unsigned long val)
{
	unsigned long delay = clock_t_to_jiffies(val);
	br->forward_delay = delay;
	if (br_is_root_bridge(br))
		br->bridge_forward_delay = delay;
	return 0;
}

static ssize_t store_forward_delay(struct device *d,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_forward_delay);
}
static DEVICE_ATTR(forward_delay, S_IRUGO | S_IWUSR,
		   show_forward_delay, store_forward_delay);

static ssize_t show_hello_time(struct device *d, struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(d)->hello_time));
}

static int set_hello_time(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);

	if (t < HZ)
		return -EINVAL;

	br->hello_time = t;
	if (br_is_root_bridge(br))
		br->bridge_hello_time = t;
	return 0;
}

static ssize_t store_hello_time(struct device *d,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	return store_bridge_parm(d, buf, len, set_hello_time);
}
static DEVICE_ATTR(hello_time, S_IRUGO | S_IWUSR, show_hello_time,
		   store_hello_time);

static ssize_t show_max_age(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(d)->max_age));
}

static int set_max_age(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);
	br->max_age = t;
	if (br_is_root_bridge(br))
		br->bridge_max_age = t;
	return 0;
}

static ssize_t store_max_age(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_max_age);
}
static DEVICE_ATTR(max_age, S_IRUGO | S_IWUSR, show_max_age, store_max_age);

static ssize_t show_ageing_time(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->ageing_time));
}

static int set_ageing_time(struct net_bridge *br, unsigned long val)
{
	br->ageing_time = clock_t_to_jiffies(val);
	return 0;
}

static ssize_t store_ageing_time(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_ageing_time);
}
static DEVICE_ATTR(ageing_time, S_IRUGO | S_IWUSR, show_ageing_time,
		   store_ageing_time);

static ssize_t show_stp_state(struct device *d,
			      struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->stp_enabled);
}


static ssize_t store_stp_state(struct device *d,
			       struct device_attribute *attr, const char *buf,
			       size_t len)
{
	struct net_bridge *br = to_bridge(d);
	char *endp;
	unsigned long val;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();
	br_stp_set_enabled(br, val);
	rtnl_unlock();

	return len;
}
static DEVICE_ATTR(stp_state, S_IRUGO | S_IWUSR, show_stp_state,
		   store_stp_state);

static ssize_t show_priority(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n",
		       (br->bridge_id.prio[0] << 8) | br->bridge_id.prio[1]);
}

static int set_priority(struct net_bridge *br, unsigned long val)
{
	br_stp_set_bridge_priority(br, (u16) val);
	return 0;
}

static ssize_t store_priority(struct device *d, struct device_attribute *attr,
			       const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_priority);
}
static DEVICE_ATTR(priority, S_IRUGO | S_IWUSR, show_priority, store_priority);

static ssize_t show_root_id(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(d)->designated_root);
}
static DEVICE_ATTR(root_id, S_IRUGO, show_root_id, NULL);

static ssize_t show_bridge_id(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(d)->bridge_id);
}
static DEVICE_ATTR(bridge_id, S_IRUGO, show_bridge_id, NULL);

static ssize_t show_root_port(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->root_port);
}
static DEVICE_ATTR(root_port, S_IRUGO, show_root_port, NULL);

static ssize_t show_root_path_cost(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->root_path_cost);
}
static DEVICE_ATTR(root_path_cost, S_IRUGO, show_root_path_cost, NULL);

static ssize_t show_topology_change(struct device *d,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->topology_change);
}
static DEVICE_ATTR(topology_change, S_IRUGO, show_topology_change, NULL);

static ssize_t show_topology_change_detected(struct device *d,
					     struct device_attribute *attr,
					     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->topology_change_detected);
}
static DEVICE_ATTR(topology_change_detected, S_IRUGO,
		   show_topology_change_detected, NULL);

static ssize_t show_hello_timer(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->hello_timer));
}
static DEVICE_ATTR(hello_timer, S_IRUGO, show_hello_timer, NULL);

static ssize_t show_tcn_timer(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->tcn_timer));
}
static DEVICE_ATTR(tcn_timer, S_IRUGO, show_tcn_timer, NULL);

static ssize_t show_topology_change_timer(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->topology_change_timer));
}
static DEVICE_ATTR(topology_change_timer, S_IRUGO, show_topology_change_timer,
		   NULL);

static ssize_t show_gc_timer(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->gc_timer));
}
static DEVICE_ATTR(gc_timer, S_IRUGO, show_gc_timer, NULL);

static ssize_t show_group_addr(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%x:%x:%x:%x:%x:%x\n",
		       br->group_addr[0], br->group_addr[1],
		       br->group_addr[2], br->group_addr[3],
		       br->group_addr[4], br->group_addr[5]);
}

static ssize_t store_group_addr(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct net_bridge *br = to_bridge(d);
	unsigned new_addr[6];
	int i;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (sscanf(buf, "%x:%x:%x:%x:%x:%x",
		   &new_addr[0], &new_addr[1], &new_addr[2],
		   &new_addr[3], &new_addr[4], &new_addr[5]) != 6)
		return -EINVAL;

	/* Must be 01:80:c2:00:00:0X */
	for (i = 0; i < 5; i++)
		if (new_addr[i] != br_group_address[i])
			return -EINVAL;

	if (new_addr[5] & ~0xf)
		return -EINVAL;

	if (new_addr[5] == 1 ||		/* 802.3x Pause address */
	    new_addr[5] == 2 ||		/* 802.3ad Slow protocols */
	    new_addr[5] == 3)		/* 802.1X PAE address */
		return -EINVAL;

	spin_lock_bh(&br->lock);
	for (i = 0; i < 6; i++)
		br->group_addr[i] = new_addr[i];
	spin_unlock_bh(&br->lock);
	return len;
}

static DEVICE_ATTR(group_addr, S_IRUGO | S_IWUSR,
		   show_group_addr, store_group_addr);

static ssize_t store_flush(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t len)
{
	struct net_bridge *br = to_bridge(d);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	br_fdb_flush(br);
	return len;
}
static DEVICE_ATTR(flush, S_IWUSR, NULL, store_flush);

static struct attribute *bridge_attrs[] = {
	&dev_attr_forward_delay.attr,
	&dev_attr_hello_time.attr,
	&dev_attr_max_age.attr,
	&dev_attr_ageing_time.attr,
	&dev_attr_stp_state.attr,
	&dev_attr_priority.attr,
	&dev_attr_bridge_id.attr,
	&dev_attr_root_id.attr,
	&dev_attr_root_path_cost.attr,
	&dev_attr_root_port.attr,
	&dev_attr_topology_change.attr,
	&dev_attr_topology_change_detected.attr,
	&dev_attr_hello_timer.attr,
	&dev_attr_tcn_timer.attr,
	&dev_attr_topology_change_timer.attr,
	&dev_attr_gc_timer.attr,
	&dev_attr_group_addr.attr,
	&dev_attr_flush.attr,
	NULL
};

static struct attribute_group bridge_group = {
	.name = SYSFS_BRIDGE_ATTR,
	.attrs = bridge_attrs,
};

/*
 * Export the forwarding information table as a binary file
 * The records are struct __fdb_entry.
 *
 * Returns the number of bytes read.
 */
static ssize_t brforward_read(struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      char *buf, loff_t off, size_t count)
{
	struct device *dev = to_dev(kobj);
	struct net_bridge *br = to_bridge(dev);
	int n;

	/* must read whole records */
	if (off % sizeof(struct __fdb_entry) != 0)
		return -EINVAL;

	n =  br_fdb_fillbuf(br, buf,
			    count / sizeof(struct __fdb_entry),
			    off / sizeof(struct __fdb_entry));

	if (n > 0)
		n *= sizeof(struct __fdb_entry);

	return n;
}

static struct bin_attribute bridge_forward = {
	.attr = { .name = SYSFS_BRIDGE_FDB,
		  .mode = S_IRUGO, },
	.read = brforward_read,
};

/*
 * Add entries in sysfs onto the existing network class device
 * for the bridge.
 *   Adds a attribute group "bridge" containing tuning parameters.
 *   Binary attribute containing the forward table
 *   Sub directory to hold links to interfaces.
 *
 * Note: the ifobj exists only to be a subdirectory
 *   to hold links.  The ifobj exists in same data structure
 *   as it's parent the bridge so reference counting works.
 */
int br_sysfs_addbr(struct net_device *dev)
{
	struct kobject *brobj = &dev->dev.kobj;
	struct net_bridge *br = netdev_priv(dev);
	int err;

	err = sysfs_create_group(brobj, &bridge_group);
	if (err) {
		pr_info("%s: can't create group %s/%s\n",
			__func__, dev->name, bridge_group.name);
		goto out1;
	}

	err = sysfs_create_bin_file(brobj, &bridge_forward);
	if (err) {
		pr_info("%s: can't create attribute file %s/%s\n",
			__func__, dev->name, bridge_forward.attr.name);
		goto out2;
	}

	br->ifobj = kobject_create_and_add(SYSFS_BRIDGE_PORT_SUBDIR, brobj);
	if (!br->ifobj) {
		pr_info("%s: can't add kobject (directory) %s/%s\n",
			__func__, dev->name, SYSFS_BRIDGE_PORT_SUBDIR);
		goto out3;
	}
	return 0;
 out3:
	sysfs_remove_bin_file(&dev->dev.kobj, &bridge_forward);
 out2:
	sysfs_remove_group(&dev->dev.kobj, &bridge_group);
 out1:
	return err;

}

void br_sysfs_delbr(struct net_device *dev)
{
	struct kobject *kobj = &dev->dev.kobj;
	struct net_bridge *br = netdev_priv(dev);

	kobject_put(br->ifobj);
	sysfs_remove_bin_file(kobj, &bridge_forward);
	sysfs_remove_group(kobj, &bridge_group);
}
