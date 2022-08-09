// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Sysfs attributes of bridge ports
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_bridge.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/sched/signal.h>

#include "br_private.h"

/* IMPORTANT: new bridge port options must be added with netlink support only
 *            please do not add new sysfs entries
 */

struct brport_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct net_bridge_port *, char *);
	int (*store)(struct net_bridge_port *, unsigned long);
	int (*store_raw)(struct net_bridge_port *, char *);
};

#define BRPORT_ATTR_RAW(_name, _mode, _show, _store)			\
const struct brport_attribute brport_attr_##_name = {			\
	.attr		= {.name = __stringify(_name),			\
			   .mode = _mode },				\
	.show		= _show,					\
	.store_raw	= _store,					\
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
static BRPORT_ATTR(_name, 0644,					\
		   show_##_name, store_##_name)

static int store_flag(struct net_bridge_port *p, unsigned long v,
		      unsigned long mask)
{
	struct netlink_ext_ack extack = {0};
	unsigned long flags = p->flags;
	int err;

	if (v)
		flags |= mask;
	else
		flags &= ~mask;

	if (flags != p->flags) {
		err = br_switchdev_set_port_flag(p, flags, mask, &extack);
		if (err) {
			netdev_err(p->dev, "%s\n", extack._msg);
			return err;
		}

		p->flags = flags;
		br_port_flags_change(p, mask);
	}
	return 0;
}

static ssize_t show_path_cost(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->path_cost);
}

static BRPORT_ATTR(path_cost, 0644,
		   show_path_cost, br_stp_set_path_cost);

static ssize_t show_priority(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->priority);
}

static BRPORT_ATTR(priority, 0644,
			 show_priority, br_stp_set_port_priority);

static ssize_t show_designated_root(struct net_bridge_port *p, char *buf)
{
	return br_show_bridge_id(buf, &p->designated_root);
}
static BRPORT_ATTR(designated_root, 0444, show_designated_root, NULL);

static ssize_t show_designated_bridge(struct net_bridge_port *p, char *buf)
{
	return br_show_bridge_id(buf, &p->designated_bridge);
}
static BRPORT_ATTR(designated_bridge, 0444, show_designated_bridge, NULL);

static ssize_t show_designated_port(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->designated_port);
}
static BRPORT_ATTR(designated_port, 0444, show_designated_port, NULL);

static ssize_t show_designated_cost(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->designated_cost);
}
static BRPORT_ATTR(designated_cost, 0444, show_designated_cost, NULL);

static ssize_t show_port_id(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "0x%x\n", p->port_id);
}
static BRPORT_ATTR(port_id, 0444, show_port_id, NULL);

static ssize_t show_port_no(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "0x%x\n", p->port_no);
}

static BRPORT_ATTR(port_no, 0444, show_port_no, NULL);

static ssize_t show_change_ack(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->topology_change_ack);
}
static BRPORT_ATTR(change_ack, 0444, show_change_ack, NULL);

static ssize_t show_config_pending(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->config_pending);
}
static BRPORT_ATTR(config_pending, 0444, show_config_pending, NULL);

static ssize_t show_port_state(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->state);
}
static BRPORT_ATTR(state, 0444, show_port_state, NULL);

static ssize_t show_message_age_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->message_age_timer));
}
static BRPORT_ATTR(message_age_timer, 0444, show_message_age_timer, NULL);

static ssize_t show_forward_delay_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->forward_delay_timer));
}
static BRPORT_ATTR(forward_delay_timer, 0444, show_forward_delay_timer, NULL);

static ssize_t show_hold_timer(struct net_bridge_port *p,
					    char *buf)
{
	return sprintf(buf, "%ld\n", br_timer_value(&p->hold_timer));
}
static BRPORT_ATTR(hold_timer, 0444, show_hold_timer, NULL);

static int store_flush(struct net_bridge_port *p, unsigned long v)
{
	br_fdb_delete_by_port(p->br, p, 0, 0); // Don't delete local entry
	return 0;
}
static BRPORT_ATTR(flush, 0200, NULL, store_flush);

static ssize_t show_group_fwd_mask(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%#x\n", p->group_fwd_mask);
}

static int store_group_fwd_mask(struct net_bridge_port *p,
				unsigned long v)
{
	if (v & BR_GROUPFWD_MACPAUSE)
		return -EINVAL;
	p->group_fwd_mask = v;

	return 0;
}
static BRPORT_ATTR(group_fwd_mask, 0644, show_group_fwd_mask,
		   store_group_fwd_mask);

static ssize_t show_backup_port(struct net_bridge_port *p, char *buf)
{
	struct net_bridge_port *backup_p;
	int ret = 0;

	rcu_read_lock();
	backup_p = rcu_dereference(p->backup_port);
	if (backup_p)
		ret = sprintf(buf, "%s\n", backup_p->dev->name);
	rcu_read_unlock();

	return ret;
}

static int store_backup_port(struct net_bridge_port *p, char *buf)
{
	struct net_device *backup_dev = NULL;
	char *nl = strchr(buf, '\n');

	if (nl)
		*nl = '\0';

	if (strlen(buf) > 0) {
		backup_dev = __dev_get_by_name(dev_net(p->dev), buf);
		if (!backup_dev)
			return -ENOENT;
	}

	return nbp_backup_change(p, backup_dev);
}
static BRPORT_ATTR_RAW(backup_port, 0644, show_backup_port, store_backup_port);

BRPORT_ATTR_FLAG(hairpin_mode, BR_HAIRPIN_MODE);
BRPORT_ATTR_FLAG(bpdu_guard, BR_BPDU_GUARD);
BRPORT_ATTR_FLAG(root_block, BR_ROOT_BLOCK);
BRPORT_ATTR_FLAG(learning, BR_LEARNING);
BRPORT_ATTR_FLAG(unicast_flood, BR_FLOOD);
BRPORT_ATTR_FLAG(proxyarp, BR_PROXYARP);
BRPORT_ATTR_FLAG(proxyarp_wifi, BR_PROXYARP_WIFI);
BRPORT_ATTR_FLAG(multicast_flood, BR_MCAST_FLOOD);
BRPORT_ATTR_FLAG(broadcast_flood, BR_BCAST_FLOOD);
BRPORT_ATTR_FLAG(neigh_suppress, BR_NEIGH_SUPPRESS);
BRPORT_ATTR_FLAG(isolated, BR_ISOLATED);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
static ssize_t show_multicast_router(struct net_bridge_port *p, char *buf)
{
	return sprintf(buf, "%d\n", p->multicast_ctx.multicast_router);
}

static int store_multicast_router(struct net_bridge_port *p,
				      unsigned long v)
{
	return br_multicast_set_port_router(&p->multicast_ctx, v);
}
static BRPORT_ATTR(multicast_router, 0644, show_multicast_router,
		   store_multicast_router);

BRPORT_ATTR_FLAG(multicast_fast_leave, BR_MULTICAST_FAST_LEAVE);
BRPORT_ATTR_FLAG(multicast_to_unicast, BR_MULTICAST_TO_UNICAST);
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
	&brport_attr_multicast_to_unicast,
#endif
	&brport_attr_proxyarp,
	&brport_attr_proxyarp_wifi,
	&brport_attr_multicast_flood,
	&brport_attr_broadcast_flood,
	&brport_attr_group_fwd_mask,
	&brport_attr_neigh_suppress,
	&brport_attr_isolated,
	&brport_attr_backup_port,
	NULL
};

#define to_brport_attr(_at) container_of(_at, struct brport_attribute, attr)

static ssize_t brport_show(struct kobject *kobj,
			   struct attribute *attr, char *buf)
{
	struct brport_attribute *brport_attr = to_brport_attr(attr);
	struct net_bridge_port *p = kobj_to_brport(kobj);

	if (!brport_attr->show)
		return -EINVAL;

	return brport_attr->show(p, buf);
}

static ssize_t brport_store(struct kobject *kobj,
			    struct attribute *attr,
			    const char *buf, size_t count)
{
	struct brport_attribute *brport_attr = to_brport_attr(attr);
	struct net_bridge_port *p = kobj_to_brport(kobj);
	ssize_t ret = -EINVAL;
	unsigned long val;
	char *endp;

	if (!ns_capable(dev_net(p->dev)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (!rtnl_trylock())
		return restart_syscall();

	if (brport_attr->store_raw) {
		char *buf_copy;

		buf_copy = kstrndup(buf, count, GFP_KERNEL);
		if (!buf_copy) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		spin_lock_bh(&p->br->lock);
		ret = brport_attr->store_raw(p, buf_copy);
		spin_unlock_bh(&p->br->lock);
		kfree(buf_copy);
	} else if (brport_attr->store) {
		val = simple_strtoul(buf, &endp, 0);
		if (endp == buf)
			goto out_unlock;
		spin_lock_bh(&p->br->lock);
		ret = brport_attr->store(p, val);
		spin_unlock_bh(&p->br->lock);
	}

	if (!ret) {
		br_ifinfo_notify(RTM_NEWLINK, NULL, p);
		ret = count;
	}
out_unlock:
	rtnl_unlock();

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
