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

#define to_class_dev(obj) container_of(obj,struct class_device,kobj)
#define to_net_dev(class) container_of(class, struct net_device, class_dev)
#define to_bridge(cd)	((struct net_bridge *)(to_net_dev(cd)->priv))

/*
 * Common code for storing bridge parameters.
 */
static ssize_t store_bridge_parm(struct class_device *cd,
				 const char *buf, size_t len,
				 void (*set)(struct net_bridge *, unsigned long))
{
	struct net_bridge *br = to_bridge(cd);
	char *endp;
	unsigned long val;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	val = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EINVAL;

	spin_lock_bh(&br->lock);
	(*set)(br, val);
	spin_unlock_bh(&br->lock);
	return len;
}


static ssize_t show_forward_delay(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->forward_delay));
}

static void set_forward_delay(struct net_bridge *br, unsigned long val)
{
	unsigned long delay = clock_t_to_jiffies(val);
	br->forward_delay = delay;
	if (br_is_root_bridge(br))
		br->bridge_forward_delay = delay;
}

static ssize_t store_forward_delay(struct class_device *cd, const char *buf,
				   size_t len)
{
	return store_bridge_parm(cd, buf, len, set_forward_delay);
}
static CLASS_DEVICE_ATTR(forward_delay, S_IRUGO | S_IWUSR,
			 show_forward_delay, store_forward_delay);

static ssize_t show_hello_time(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(cd)->hello_time));
}

static void set_hello_time(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);
	br->hello_time = t;
	if (br_is_root_bridge(br))
		br->bridge_hello_time = t;
}

static ssize_t store_hello_time(struct class_device *cd, const char *buf,
				size_t len)
{
	return store_bridge_parm(cd, buf, len, set_hello_time);
}

static CLASS_DEVICE_ATTR(hello_time, S_IRUGO | S_IWUSR, show_hello_time,
			 store_hello_time);

static ssize_t show_max_age(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(cd)->max_age));
}

static void set_max_age(struct net_bridge *br, unsigned long val)
{
	unsigned long t = clock_t_to_jiffies(val);
	br->max_age = t;
	if (br_is_root_bridge(br))
		br->bridge_max_age = t;
}

static ssize_t store_max_age(struct class_device *cd, const char *buf,
				size_t len)
{
	return store_bridge_parm(cd, buf, len, set_max_age);
}

static CLASS_DEVICE_ATTR(max_age, S_IRUGO | S_IWUSR, show_max_age,
			 store_max_age);

static ssize_t show_ageing_time(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->ageing_time));
}

static void set_ageing_time(struct net_bridge *br, unsigned long val)
{
	br->ageing_time = clock_t_to_jiffies(val);
}

static ssize_t store_ageing_time(struct class_device *cd, const char *buf,
				 size_t len)
{
	return store_bridge_parm(cd, buf, len, set_ageing_time);
}

static CLASS_DEVICE_ATTR(ageing_time, S_IRUGO | S_IWUSR, show_ageing_time,
			 store_ageing_time);
static ssize_t show_stp_state(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%d\n", br->stp_enabled);
}

static void set_stp_state(struct net_bridge *br, unsigned long val)
{
	br->stp_enabled = val;
}

static ssize_t store_stp_state(struct class_device *cd,
			       const char *buf, size_t len)
{
	return store_bridge_parm(cd, buf, len, set_stp_state);
}

static CLASS_DEVICE_ATTR(stp_state, S_IRUGO | S_IWUSR, show_stp_state,
			 store_stp_state);

static ssize_t show_priority(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%d\n",
		       (br->bridge_id.prio[0] << 8) | br->bridge_id.prio[1]);
}

static void set_priority(struct net_bridge *br, unsigned long val)
{
	br_stp_set_bridge_priority(br, (u16) val);
}

static ssize_t store_priority(struct class_device *cd,
			       const char *buf, size_t len)
{
	return store_bridge_parm(cd, buf, len, set_priority);
}
static CLASS_DEVICE_ATTR(priority, S_IRUGO | S_IWUSR, show_priority,
			 store_priority);

static ssize_t show_root_id(struct class_device *cd, char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(cd)->designated_root);
}
static CLASS_DEVICE_ATTR(root_id, S_IRUGO, show_root_id, NULL);

static ssize_t show_bridge_id(struct class_device *cd, char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(cd)->bridge_id);
}
static CLASS_DEVICE_ATTR(bridge_id, S_IRUGO, show_bridge_id, NULL);

static ssize_t show_root_port(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(cd)->root_port);
}
static CLASS_DEVICE_ATTR(root_port, S_IRUGO, show_root_port, NULL);

static ssize_t show_root_path_cost(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(cd)->root_path_cost);
}
static CLASS_DEVICE_ATTR(root_path_cost, S_IRUGO, show_root_path_cost, NULL);

static ssize_t show_topology_change(struct class_device *cd, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(cd)->topology_change);
}
static CLASS_DEVICE_ATTR(topology_change, S_IRUGO, show_topology_change, NULL);

static ssize_t show_topology_change_detected(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%d\n", br->topology_change_detected);
}
static CLASS_DEVICE_ATTR(topology_change_detected, S_IRUGO, show_topology_change_detected, NULL);

static ssize_t show_hello_timer(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%ld\n", br_timer_value(&br->hello_timer));
}
static CLASS_DEVICE_ATTR(hello_timer, S_IRUGO, show_hello_timer, NULL);

static ssize_t show_tcn_timer(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%ld\n", br_timer_value(&br->tcn_timer));
}
static CLASS_DEVICE_ATTR(tcn_timer, S_IRUGO, show_tcn_timer, NULL);

static ssize_t show_topology_change_timer(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%ld\n", br_timer_value(&br->topology_change_timer));
}
static CLASS_DEVICE_ATTR(topology_change_timer, S_IRUGO, show_topology_change_timer, NULL);

static ssize_t show_gc_timer(struct class_device *cd, char *buf)
{
	struct net_bridge *br = to_bridge(cd);
	return sprintf(buf, "%ld\n", br_timer_value(&br->gc_timer));
}
static CLASS_DEVICE_ATTR(gc_timer, S_IRUGO, show_gc_timer, NULL);

static struct attribute *bridge_attrs[] = {
	&class_device_attr_forward_delay.attr,
	&class_device_attr_hello_time.attr,
	&class_device_attr_max_age.attr,
	&class_device_attr_ageing_time.attr,
	&class_device_attr_stp_state.attr,
	&class_device_attr_priority.attr,
	&class_device_attr_bridge_id.attr,
	&class_device_attr_root_id.attr,
	&class_device_attr_root_path_cost.attr,
	&class_device_attr_root_port.attr,
	&class_device_attr_topology_change.attr,
	&class_device_attr_topology_change_detected.attr,
	&class_device_attr_hello_timer.attr,
	&class_device_attr_tcn_timer.attr,
	&class_device_attr_topology_change_timer.attr,
	&class_device_attr_gc_timer.attr,
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
static ssize_t brforward_read(struct kobject *kobj, char *buf,
			   loff_t off, size_t count)
{
	struct class_device *cdev = to_class_dev(kobj);
	struct net_bridge *br = to_bridge(cdev);
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
		  .mode = S_IRUGO, 
		  .owner = THIS_MODULE, },
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
	struct kobject *brobj = &dev->class_dev.kobj;
	struct net_bridge *br = netdev_priv(dev);
	int err;

	err = sysfs_create_group(brobj, &bridge_group);
	if (err) {
		pr_info("%s: can't create group %s/%s\n",
			__FUNCTION__, dev->name, bridge_group.name);
		goto out1;
	}

	err = sysfs_create_bin_file(brobj, &bridge_forward);
	if (err) {
		pr_info("%s: can't create attribue file %s/%s\n",
			__FUNCTION__, dev->name, bridge_forward.attr.name);
		goto out2;
	}

	
	kobject_set_name(&br->ifobj, SYSFS_BRIDGE_PORT_SUBDIR);
	br->ifobj.ktype = NULL;
	br->ifobj.kset = NULL;
	br->ifobj.parent = brobj;

	err = kobject_register(&br->ifobj);
	if (err) {
		pr_info("%s: can't add kobject (directory) %s/%s\n",
			__FUNCTION__, dev->name, br->ifobj.name);
		goto out3;
	}
	return 0;
 out3:
	sysfs_remove_bin_file(&dev->class_dev.kobj, &bridge_forward);
 out2:
	sysfs_remove_group(&dev->class_dev.kobj, &bridge_group);
 out1:
	return err;

}

void br_sysfs_delbr(struct net_device *dev)
{
	struct kobject *kobj = &dev->class_dev.kobj;
	struct net_bridge *br = netdev_priv(dev);

	kobject_unregister(&br->ifobj);
	sysfs_remove_bin_file(kobj, &bridge_forward);
	sysfs_remove_group(kobj, &bridge_group);
}
