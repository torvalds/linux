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

#include "br_private.h"

struct brport_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct net_bridge_port *, char *);
	int (*store)(struct net_bridge_port *, unsigned long);
};

#define BRPORT_ATTR(_name, _mode, _show, _store)		\
const struct brport_attribute brport_attr_##_name = { 	        \
	.attr = {.name = __stringify(_name), 			\
		 .mode = _mode },				\
	.show	= _show,					\
	.store	= _store,					\
};

#define BRPORT_ATTR_FLAG(_name, _mask)				\
static ssize_t show_##_name(struct net_bridge_port *p, char *buf) \
{								\
	return sprintf(buf, "%d\n", !!(p->flags & _mask));	\
}								\
static int store_##_name(struct net_bridge_port *p, unsigned long v) \
{								\
	return store_flag(p, v, _mask);				\
}								\
static BRPORT_ATTR(_name, S_IRUGO | S_IWUSR,			\
		   show_##_name, store_##_name)

static int store_flag(struct net_bridge_port *p, unsigned long v,
		      unsigned long mask)
{
	unsigned long flags;

	flags = p->flags;

	if (v)
		flags |= mask;
	else
		flags &= ~mask;

	if (flags != p->flags) {
		p->flags = flags;
		br_port_flags_change(p, mask);
	}
	return 0;
}

static ssize_t show_path_cost(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->path_cost);
}

static BRPORT_ATTR(path_cost, S_IRUGO | S_IWUSR,
		   show_path_cost, br_stp_set_path_cost);

static ssize_t show_priority(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->priority);
}

static BRPORT_ATTR(priority, S_IRUGO | S_IWUSR,
			 show_priority, br_stp_set_port_priority);

static ssize_t show_designated_root(struct net_bridge_port *p, char *buf)
{
	return br_show_bridge_id(buf, &p->designated_root);
}
static BRPORT_ATTR(designated_root, S_IRUGO, show_designated_root, NULL);

static ssize_t show_designated_bridge(struct net_bridge_port *p, char *buf)
{
	return br_show_bridge_id(buf, &p->designated_bridge);
}
static BRPORT_ATTR(designated_bridge, S_IRUGO, show_designated_bridge, NULL);

static ssize_t show_designated_port(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->designated_port);
}
static BRPORT_ATTR(designated_port, S_IRUGO, show_designated_port, NULL);

static ssize_t show_designated_cost(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->designated_cost);
}
static BRPORT_ATTR(designated_cost, S_IRUGO, show_designated_cost, NULL);

static ssize_t show_port_id(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "0x%x\n", p->port_id);
}
static BRPORT_ATTR(port_id, S_IRUGO, show_port_id, NULL);

static ssize_t show_port_no(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "0x%x\n", p->port_no);
}

static BRPORT_ATTR(port_no, S_IRUGO, show_port_no, NULL);

static ssize_t show_change_ack(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->topology_change_ack);
}
static BRPORT_ATTR(change_ack, S_IRUGO, show_change_ack, NULL);

static ssize_t show_config_pending(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->config_pending);
}
static BRPORT_ATTR(config_pending, S_IRUGO, show_config_pending, NULL);

static ssize_t show_port_state(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->state);
}
static BRPORT_ATTR(state, S_IRUGO, show_port_state, NULL);

static ssize_t show_message_age_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->message_age_timer));
}
static BRPORT_ATTR(message_age_timer, S_IRUGO, show_message_age_timer, NULL);

static ssize_t show_forward_delay_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->forward_delay_timer));
}
static BRPORT_ATTR(forward_delay_timer, S_IRUGO, show_forward_delay_timer, NULL);

static ssize_t show_hold_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->hold_timer));
}
static BRPORT_ATTR(hold_timer, S_IRUGO, show_hold_timer, NULL);

static int store_flush(struct net_bridge_port *p, unsigned long v)
{
	br_fdb_delete_by_port(p->br, p, 0, 0); // Don't delete local entry
	return 0;
}
static BRPORT_ATTR(flush, S_IWUSR, NULL, store_flush);

BRPORT_ATTR_FLAG(hairpin_mode, BR_HAIRPIN_MODE);
BRPORT_ATTR_FLAG(bpdu_guard, BR_BPDU_GUARD);
BRPORT_ATTR_FLAG(root_block, BR_ROOT_BLOCK);
BRPORT_ATTR_FLAG(learning, BR_LEARNING);
BRPORT_ATTR_FLAG(unicast_flood, BR_FLOOD);
BRPORT_ATTR_FLAG(proxyarp, BR_PROXYARP);
BRPORT_ATTR_FLAG(proxyarp_wifi, BR_PROXYARP_WIFI);
BRPORT_ATTR_FLAG(multicast_flood, BR_MCAST_FLOOD);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
static ssize_t show_multicast_router(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->multicast_router);
}

static int store_multicast_router(struct net_bridge_port *p,
				      unsigned long v)
{
	return br_multicast_set_port_router(p, v);
}
static BRPORT_ATTR(multicast_router, S_IRUGO | S_IWUSR, show_multicast_router,
		   store_multicast_router);

BRPORT_ATTR_FLAG(multicast_fast_leave, BR_MULTICAST_FAST_LEAVE);
#endif

static const struct brport_attribute *brport_attrs[] = {
	&brport_attr_path_cost,
	&brport_attr_priority,
	&brport_attr_port_id,
	&brport_attr_port_no,
	&brport_attr_designated_root,
	&brport_attr_designated_bridge,
	&brport_attr_designated_port,
	&brport_attr_designated_cost,
	&brport_attr_state,
	&brport_attr_change_ack,
	&brport_attr_config_pending,
	&brport_attr_message_age_timer,
	&brport_attr_forward_delay_timer,
	&brport_attr_hold_timer,
	&brport_attr_flush,
	&brport_attr_hairpin_mode,
	&brport_attr_bpdu_guard,
	&brport_attr_root_block,
	&brport_attr_learning,
	&brport_attr_unicast_flood,
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	&brport_attr_multicast_router,
	&brport_attr_multicast_fast_leave,
#endif
	&brport_attr_proxyarp,
	&brport_attr_proxyarp_wifi,
	&brport_attr_multicast_flood,
	NULL
};

#define to_brport_attr(_at) container_of(_at, struct brport_attribute, attr)
#define to_brport(obj)	container_of(obj, struct net_bridge_port, kobj)

static ssize_t brport_show(struct kobject *kobj,
			   struct attribute *attr, char *buf)
{
	struct brport_attribute *brport_attr = to_brport_attr(attr);
	struct net_bridge_port *p = to_brport(kobj);

	return brport_attr->show(p, buf);
}

static ssize_t brport_store(struct kobject *kobj,
			    struct attribute *attr,
			    const char *buf, size_t count)
{
	struct brport_attribute *brport_attr = to_brport_attr(attr);
	struct net_bridge_port *p = to_brport(kobj);
	ssize_t ret = -EINVAL;
	char *endp;
	unsigned long val;

	if (!ns_capable(dev_net(p->dev)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	val = simple_strtoul(buf, &endp, 0);
	if (endp != buf) {
		if (!rtnl_trylock())
			return restart_syscall();
		if (p->dev && p->br && brport_attr->store) {
			spin_lock_bh(&p->br->lock);
			ret = brport_attr->store(p, val);
			spin_unlock_bh(&p->br->lock);
			if (!ret) {
				br_ifinfo_notify(RTM_NEWLINK, p);
				ret = count;
			}
		}
		rtnl_unlock();
	}
	return ret;
}

const struct sysfs_ops brport_sysfs_ops = {
	.show = brport_show,
	.store = brport_store,
};

/*
 * Add sysfs entries to ethernet device added to a bridge.
 * Creates a brport subdirectory with bridge attributes.
 * Puts symlink in bridge's brif subdirectory
 */
int br_sysfs_addif(struct net_bridge_port *p)
{
	struct net_bridge *br = p->br;
	const struct brport_attribute **a;
	int err;

	err = sysfs_create_link(&p->kobj, &br->dev->dev.kobj,
				SYSFS_BRIDGE_PORT_LINK);
	if (err)
		return err;

	for (a = brport_attrs; *a; ++a) {
		err = sysfs_create_file(&p->kobj, &((*a)->attr));
		if (err)
			return err;
	}

	strlcpy(p->sysfs_name, p->dev->name, IFNAMSIZ);
	return sysfs_create_link(br->ifobj, &p->kobj, p->sysfs_name);
}

/* Rename bridge's brif symlink */
int br_sysfs_renameif(struct net_bridge_port *p)
{
	struct net_bridge *br = p->br;
	int err;

	/* If a rename fails, the rollback will cause another
	 * rename call with the existing name.
	 */
	if (!strncmp(p->sysfs_name, p->dev->name, IFNAMSIZ))
		return 0;

	err = sysfs_rename_link(br->ifobj, &p->kobj,
				p->sysfs_name, p->dev->name);
	if (err)
		netdev_notice(br->dev, "unable to rename link %s to %s",
			      p->sysfs_name, p->dev->name);
	else
		strlcpy(p->sysfs_name, p->dev->name, IFNAMSIZ);

	return err;
}
