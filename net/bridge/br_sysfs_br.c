// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Sysfs attributes of bridge
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Stephen Hemminger		<shemminger@osdl.org>
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <linux/rtnetlink.h>
#include <linux/spinlock.h>
#include <linux/times.h>
#include <linux/sched/signal.h>

#include "br_private.h"

/* IMPORTANT: new bridge options must be added with netlink support only
 *            please do not add new sysfs entries
 */

#define to_bridge(cd)	((struct net_bridge *)netdev_priv(to_net_dev(cd)))

/*
 * Common code for storing bridge parameters.
 */
static ssize_t store_bridge_parm(struct device *d,
				 const char *buf, size_t len,
				 int (*set)(struct net_bridge *br, unsigned long val,
					    struct netlink_ext_ack *extack))
{
	struct net_bridge *br = to_bridge(d);
	struct netlink_ext_ack extack = {0};
	unsigned long val;
	int err;

	if (!ns_capable(dev_net(br->dev)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	err = kstrtoul(buf, 0, &val);
	if (err != 0)
		return err;

	if (!rtnl_trylock())
		return restart_syscall();

	err = (*set)(br, val, &extack);
	if (!err)
		netdev_state_change(br->dev);
	if (extack._msg) {
		if (err)
			br_err(br, "%s\n", extack._msg);
		else
			br_warn(br, "%s\n", extack._msg);
	}
	rtnl_unlock();

	return err ? err : len;
}


static ssize_t forward_delay_show(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->forward_delay));
}

static int set_forward_delay(struct net_bridge *br, unsigned long val,
			     struct netlink_ext_ack *extack)
{
	return br_set_forward_delay(br, val);
}

static ssize_t forward_delay_store(struct device *d,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_forward_delay);
}
static DEVICE_ATTR_RW(forward_delay);

static ssize_t hello_time_show(struct device *d, struct device_attribute *attr,
			       char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(d)->hello_time));
}

static int set_hello_time(struct net_bridge *br, unsigned long val,
			  struct netlink_ext_ack *extack)
{
	return br_set_hello_time(br, val);
}

static ssize_t hello_time_store(struct device *d,
				struct device_attribute *attr, const char *buf,
				size_t len)
{
	return store_bridge_parm(d, buf, len, set_hello_time);
}
static DEVICE_ATTR_RW(hello_time);

static ssize_t max_age_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(to_bridge(d)->max_age));
}

static int set_max_age(struct net_bridge *br, unsigned long val,
		       struct netlink_ext_ack *extack)
{
	return br_set_max_age(br, val);
}

static ssize_t max_age_store(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_max_age);
}
static DEVICE_ATTR_RW(max_age);

static ssize_t ageing_time_show(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n", jiffies_to_clock_t(br->ageing_time));
}

static int set_ageing_time(struct net_bridge *br, unsigned long val,
			   struct netlink_ext_ack *extack)
{
	return br_set_ageing_time(br, val);
}

static ssize_t ageing_time_store(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_ageing_time);
}
static DEVICE_ATTR_RW(ageing_time);

static ssize_t stp_state_show(struct device *d,
			      struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->stp_enabled);
}


static int set_stp_state(struct net_bridge *br, unsigned long val,
			 struct netlink_ext_ack *extack)
{
	return br_stp_set_enabled(br, val, extack);
}

static ssize_t stp_state_store(struct device *d,
			       struct device_attribute *attr, const char *buf,
			       size_t len)
{
	return store_bridge_parm(d, buf, len, set_stp_state);
}
static DEVICE_ATTR_RW(stp_state);

static ssize_t group_fwd_mask_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%#x\n", br->group_fwd_mask);
}

static int set_group_fwd_mask(struct net_bridge *br, unsigned long val,
			      struct netlink_ext_ack *extack)
{
	if (val & BR_GROUPFWD_RESTRICTED)
		return -EINVAL;

	br->group_fwd_mask = val;

	return 0;
}

static ssize_t group_fwd_mask_store(struct device *d,
				    struct device_attribute *attr,
				    const char *buf,
				    size_t len)
{
	return store_bridge_parm(d, buf, len, set_group_fwd_mask);
}
static DEVICE_ATTR_RW(group_fwd_mask);

static ssize_t priority_show(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n",
		       (br->bridge_id.prio[0] << 8) | br->bridge_id.prio[1]);
}

static int set_priority(struct net_bridge *br, unsigned long val,
			struct netlink_ext_ack *extack)
{
	br_stp_set_bridge_priority(br, (u16) val);
	return 0;
}

static ssize_t priority_store(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_priority);
}
static DEVICE_ATTR_RW(priority);

static ssize_t root_id_show(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(d)->designated_root);
}
static DEVICE_ATTR_RO(root_id);

static ssize_t bridge_id_show(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	return br_show_bridge_id(buf, &to_bridge(d)->bridge_id);
}
static DEVICE_ATTR_RO(bridge_id);

static ssize_t root_port_show(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->root_port);
}
static DEVICE_ATTR_RO(root_port);

static ssize_t root_path_cost_show(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->root_path_cost);
}
static DEVICE_ATTR_RO(root_path_cost);

static ssize_t topology_change_show(struct device *d,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", to_bridge(d)->topology_change);
}
static DEVICE_ATTR_RO(topology_change);

static ssize_t topology_change_detected_show(struct device *d,
					     struct device_attribute *attr,
					     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->topology_change_detected);
}
static DEVICE_ATTR_RO(topology_change_detected);

static ssize_t hello_timer_show(struct device *d,
				struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->hello_timer));
}
static DEVICE_ATTR_RO(hello_timer);

static ssize_t tcn_timer_show(struct device *d, struct device_attribute *attr,
			      char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->tcn_timer));
}
static DEVICE_ATTR_RO(tcn_timer);

static ssize_t topology_change_timer_show(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->topology_change_timer));
}
static DEVICE_ATTR_RO(topology_change_timer);

static ssize_t gc_timer_show(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%ld\n", br_timer_value(&br->gc_work.timer));
}
static DEVICE_ATTR_RO(gc_timer);

static ssize_t group_addr_show(struct device *d,
			       struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%pM\n", br->group_addr);
}

static ssize_t group_addr_store(struct device *d,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct net_bridge *br = to_bridge(d);
	u8 new_addr[6];

	if (!ns_capable(dev_net(br->dev)->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	if (!mac_pton(buf, new_addr))
		return -EINVAL;

	if (!is_link_local_ether_addr(new_addr))
		return -EINVAL;

	if (new_addr[5] == 1 ||		/* 802.3x Pause address */
	    new_addr[5] == 2 ||		/* 802.3ad Slow protocols */
	    new_addr[5] == 3)		/* 802.1X PAE address */
		return -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	spin_lock_bh(&br->lock);
	ether_addr_copy(br->group_addr, new_addr);
	spin_unlock_bh(&br->lock);

	br_opt_toggle(br, BROPT_GROUP_ADDR_SET, true);
	br_recalculate_fwd_mask(br);
	netdev_state_change(br->dev);

	rtnl_unlock();

	return len;
}

static DEVICE_ATTR_RW(group_addr);

static int set_flush(struct net_bridge *br, unsigned long val,
		     struct netlink_ext_ack *extack)
{
	struct net_bridge_fdb_flush_desc desc = {
		.flags_mask = BIT(BR_FDB_STATIC)
	};

	br_fdb_flush(br, &desc);
	return 0;
}

static ssize_t flush_store(struct device *d,
			   struct device_attribute *attr,
			   const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_flush);
}
static DEVICE_ATTR_WO(flush);

static ssize_t no_linklocal_learn_show(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br_boolopt_get(br, BR_BOOLOPT_NO_LL_LEARN));
}

static int set_no_linklocal_learn(struct net_bridge *br, unsigned long val,
				  struct netlink_ext_ack *extack)
{
	return br_boolopt_toggle(br, BR_BOOLOPT_NO_LL_LEARN, !!val, extack);
}

static ssize_t no_linklocal_learn_store(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_no_linklocal_learn);
}
static DEVICE_ATTR_RW(no_linklocal_learn);

#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
static ssize_t multicast_router_show(struct device *d,
				     struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->multicast_ctx.multicast_router);
}

static int set_multicast_router(struct net_bridge *br, unsigned long val,
				struct netlink_ext_ack *extack)
{
	return br_multicast_set_router(&br->multicast_ctx, val);
}

static ssize_t multicast_router_store(struct device *d,
				      struct device_attribute *attr,
				      const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_multicast_router);
}
static DEVICE_ATTR_RW(multicast_router);

static ssize_t multicast_snooping_show(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br_opt_get(br, BROPT_MULTICAST_ENABLED));
}

static ssize_t multicast_snooping_store(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, br_multicast_toggle);
}
static DEVICE_ATTR_RW(multicast_snooping);

static ssize_t multicast_query_use_ifaddr_show(struct device *d,
					       struct device_attribute *attr,
					       char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n",
		       br_opt_get(br, BROPT_MULTICAST_QUERY_USE_IFADDR));
}

static int set_query_use_ifaddr(struct net_bridge *br, unsigned long val,
				struct netlink_ext_ack *extack)
{
	br_opt_toggle(br, BROPT_MULTICAST_QUERY_USE_IFADDR, !!val);
	return 0;
}

static ssize_t
multicast_query_use_ifaddr_store(struct device *d,
				 struct device_attribute *attr,
				 const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_query_use_ifaddr);
}
static DEVICE_ATTR_RW(multicast_query_use_ifaddr);

static ssize_t multicast_querier_show(struct device *d,
				      struct device_attribute *attr,
				      char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->multicast_ctx.multicast_querier);
}

static int set_multicast_querier(struct net_bridge *br, unsigned long val,
				 struct netlink_ext_ack *extack)
{
	return br_multicast_set_querier(&br->multicast_ctx, val);
}

static ssize_t multicast_querier_store(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_multicast_querier);
}
static DEVICE_ATTR_RW(multicast_querier);

static ssize_t hash_elasticity_show(struct device *d,
				    struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%u\n", RHT_ELASTICITY);
}

static int set_elasticity(struct net_bridge *br, unsigned long val,
			  struct netlink_ext_ack *extack)
{
	/* 16 is RHT_ELASTICITY */
	NL_SET_ERR_MSG_MOD(extack,
			   "the hash_elasticity option has been deprecated and is always 16");
	return 0;
}

static ssize_t hash_elasticity_store(struct device *d,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_elasticity);
}
static DEVICE_ATTR_RW(hash_elasticity);

static ssize_t hash_max_show(struct device *d, struct device_attribute *attr,
			     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br->hash_max);
}

static int set_hash_max(struct net_bridge *br, unsigned long val,
			struct netlink_ext_ack *extack)
{
	br->hash_max = val;
	return 0;
}

static ssize_t hash_max_store(struct device *d, struct device_attribute *attr,
			      const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_hash_max);
}
static DEVICE_ATTR_RW(hash_max);

static ssize_t multicast_igmp_version_show(struct device *d,
					   struct device_attribute *attr,
					   char *buf)
{
	struct net_bridge *br = to_bridge(d);

	return sprintf(buf, "%u\n", br->multicast_ctx.multicast_igmp_version);
}

static int set_multicast_igmp_version(struct net_bridge *br, unsigned long val,
				      struct netlink_ext_ack *extack)
{
	return br_multicast_set_igmp_version(&br->multicast_ctx, val);
}

static ssize_t multicast_igmp_version_store(struct device *d,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_multicast_igmp_version);
}
static DEVICE_ATTR_RW(multicast_igmp_version);

static ssize_t multicast_last_member_count_show(struct device *d,
						struct device_attribute *attr,
						char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br->multicast_ctx.multicast_last_member_count);
}

static int set_last_member_count(struct net_bridge *br, unsigned long val,
				 struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_last_member_count = val;
	return 0;
}

static ssize_t multicast_last_member_count_store(struct device *d,
						 struct device_attribute *attr,
						 const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_last_member_count);
}
static DEVICE_ATTR_RW(multicast_last_member_count);

static ssize_t multicast_startup_query_count_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br->multicast_ctx.multicast_startup_query_count);
}

static int set_startup_query_count(struct net_bridge *br, unsigned long val,
				   struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_startup_query_count = val;
	return 0;
}

static ssize_t multicast_startup_query_count_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_startup_query_count);
}
static DEVICE_ATTR_RW(multicast_startup_query_count);

static ssize_t multicast_last_member_interval_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(br->multicast_ctx.multicast_last_member_interval));
}

static int set_last_member_interval(struct net_bridge *br, unsigned long val,
				    struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_last_member_interval = clock_t_to_jiffies(val);
	return 0;
}

static ssize_t multicast_last_member_interval_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_last_member_interval);
}
static DEVICE_ATTR_RW(multicast_last_member_interval);

static ssize_t multicast_membership_interval_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(br->multicast_ctx.multicast_membership_interval));
}

static int set_membership_interval(struct net_bridge *br, unsigned long val,
				   struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_membership_interval = clock_t_to_jiffies(val);
	return 0;
}

static ssize_t multicast_membership_interval_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_membership_interval);
}
static DEVICE_ATTR_RW(multicast_membership_interval);

static ssize_t multicast_querier_interval_show(struct device *d,
					       struct device_attribute *attr,
					       char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(br->multicast_ctx.multicast_querier_interval));
}

static int set_querier_interval(struct net_bridge *br, unsigned long val,
				struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_querier_interval = clock_t_to_jiffies(val);
	return 0;
}

static ssize_t multicast_querier_interval_store(struct device *d,
						struct device_attribute *attr,
						const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_querier_interval);
}
static DEVICE_ATTR_RW(multicast_querier_interval);

static ssize_t multicast_query_interval_show(struct device *d,
					     struct device_attribute *attr,
					     char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%lu\n",
		       jiffies_to_clock_t(br->multicast_ctx.multicast_query_interval));
}

static int set_query_interval(struct net_bridge *br, unsigned long val,
			      struct netlink_ext_ack *extack)
{
	br_multicast_set_query_intvl(&br->multicast_ctx, val);
	return 0;
}

static ssize_t multicast_query_interval_store(struct device *d,
					      struct device_attribute *attr,
					      const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_query_interval);
}
static DEVICE_ATTR_RW(multicast_query_interval);

static ssize_t multicast_query_response_interval_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(
		buf, "%lu\n",
		jiffies_to_clock_t(br->multicast_ctx.multicast_query_response_interval));
}

static int set_query_response_interval(struct net_bridge *br, unsigned long val,
				       struct netlink_ext_ack *extack)
{
	br->multicast_ctx.multicast_query_response_interval = clock_t_to_jiffies(val);
	return 0;
}

static ssize_t multicast_query_response_interval_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_query_response_interval);
}
static DEVICE_ATTR_RW(multicast_query_response_interval);

static ssize_t multicast_startup_query_interval_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(
		buf, "%lu\n",
		jiffies_to_clock_t(br->multicast_ctx.multicast_startup_query_interval));
}

static int set_startup_query_interval(struct net_bridge *br, unsigned long val,
				      struct netlink_ext_ack *extack)
{
	br_multicast_set_startup_query_intvl(&br->multicast_ctx, val);
	return 0;
}

static ssize_t multicast_startup_query_interval_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_startup_query_interval);
}
static DEVICE_ATTR_RW(multicast_startup_query_interval);

static ssize_t multicast_stats_enabled_show(struct device *d,
					    struct device_attribute *attr,
					    char *buf)
{
	struct net_bridge *br = to_bridge(d);

	return sprintf(buf, "%d\n",
		       br_opt_get(br, BROPT_MULTICAST_STATS_ENABLED));
}

static int set_stats_enabled(struct net_bridge *br, unsigned long val,
			     struct netlink_ext_ack *extack)
{
	br_opt_toggle(br, BROPT_MULTICAST_STATS_ENABLED, !!val);
	return 0;
}

static ssize_t multicast_stats_enabled_store(struct device *d,
					     struct device_attribute *attr,
					     const char *buf,
					     size_t len)
{
	return store_bridge_parm(d, buf, len, set_stats_enabled);
}
static DEVICE_ATTR_RW(multicast_stats_enabled);

#if IS_ENABLED(CONFIG_IPV6)
static ssize_t multicast_mld_version_show(struct device *d,
					  struct device_attribute *attr,
					  char *buf)
{
	struct net_bridge *br = to_bridge(d);

	return sprintf(buf, "%u\n", br->multicast_ctx.multicast_mld_version);
}

static int set_multicast_mld_version(struct net_bridge *br, unsigned long val,
				     struct netlink_ext_ack *extack)
{
	return br_multicast_set_mld_version(&br->multicast_ctx, val);
}

static ssize_t multicast_mld_version_store(struct device *d,
					   struct device_attribute *attr,
					   const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_multicast_mld_version);
}
static DEVICE_ATTR_RW(multicast_mld_version);
#endif
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
static ssize_t nf_call_iptables_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br_opt_get(br, BROPT_NF_CALL_IPTABLES));
}

static int set_nf_call_iptables(struct net_bridge *br, unsigned long val,
				struct netlink_ext_ack *extack)
{
	br_opt_toggle(br, BROPT_NF_CALL_IPTABLES, !!val);
	return 0;
}

static ssize_t nf_call_iptables_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_nf_call_iptables);
}
static DEVICE_ATTR_RW(nf_call_iptables);

static ssize_t nf_call_ip6tables_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br_opt_get(br, BROPT_NF_CALL_IP6TABLES));
}

static int set_nf_call_ip6tables(struct net_bridge *br, unsigned long val,
				 struct netlink_ext_ack *extack)
{
	br_opt_toggle(br, BROPT_NF_CALL_IP6TABLES, !!val);
	return 0;
}

static ssize_t nf_call_ip6tables_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_nf_call_ip6tables);
}
static DEVICE_ATTR_RW(nf_call_ip6tables);

static ssize_t nf_call_arptables_show(
	struct device *d, struct device_attribute *attr, char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br_opt_get(br, BROPT_NF_CALL_ARPTABLES));
}

static int set_nf_call_arptables(struct net_bridge *br, unsigned long val,
				 struct netlink_ext_ack *extack)
{
	br_opt_toggle(br, BROPT_NF_CALL_ARPTABLES, !!val);
	return 0;
}

static ssize_t nf_call_arptables_store(
	struct device *d, struct device_attribute *attr, const char *buf,
	size_t len)
{
	return store_bridge_parm(d, buf, len, set_nf_call_arptables);
}
static DEVICE_ATTR_RW(nf_call_arptables);
#endif
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
static ssize_t vlan_filtering_show(struct device *d,
				   struct device_attribute *attr,
				   char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br_opt_get(br, BROPT_VLAN_ENABLED));
}

static ssize_t vlan_filtering_store(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, br_vlan_filter_toggle);
}
static DEVICE_ATTR_RW(vlan_filtering);

static ssize_t vlan_protocol_show(struct device *d,
				  struct device_attribute *attr,
				  char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%#06x\n", ntohs(br->vlan_proto));
}

static ssize_t vlan_protocol_store(struct device *d,
				   struct device_attribute *attr,
				   const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, br_vlan_set_proto);
}
static DEVICE_ATTR_RW(vlan_protocol);

static ssize_t default_pvid_show(struct device *d,
				 struct device_attribute *attr,
				 char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%d\n", br->default_pvid);
}

static ssize_t default_pvid_store(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, br_vlan_set_default_pvid);
}
static DEVICE_ATTR_RW(default_pvid);

static ssize_t vlan_stats_enabled_show(struct device *d,
				       struct device_attribute *attr,
				       char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br_opt_get(br, BROPT_VLAN_STATS_ENABLED));
}

static int set_vlan_stats_enabled(struct net_bridge *br, unsigned long val,
				  struct netlink_ext_ack *extack)
{
	return br_vlan_set_stats(br, val);
}

static ssize_t vlan_stats_enabled_store(struct device *d,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_vlan_stats_enabled);
}
static DEVICE_ATTR_RW(vlan_stats_enabled);

static ssize_t vlan_stats_per_port_show(struct device *d,
					struct device_attribute *attr,
					char *buf)
{
	struct net_bridge *br = to_bridge(d);
	return sprintf(buf, "%u\n", br_opt_get(br, BROPT_VLAN_STATS_PER_PORT));
}

static int set_vlan_stats_per_port(struct net_bridge *br, unsigned long val,
				   struct netlink_ext_ack *extack)
{
	return br_vlan_set_stats_per_port(br, val);
}

static ssize_t vlan_stats_per_port_store(struct device *d,
					 struct device_attribute *attr,
					 const char *buf, size_t len)
{
	return store_bridge_parm(d, buf, len, set_vlan_stats_per_port);
}
static DEVICE_ATTR_RW(vlan_stats_per_port);
#endif

static struct attribute *bridge_attrs[] = {
	&dev_attr_forward_delay.attr,
	&dev_attr_hello_time.attr,
	&dev_attr_max_age.attr,
	&dev_attr_ageing_time.attr,
	&dev_attr_stp_state.attr,
	&dev_attr_group_fwd_mask.attr,
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
	&dev_attr_no_linklocal_learn.attr,
#ifdef CONFIG_BRIDGE_IGMP_SNOOPING
	&dev_attr_multicast_router.attr,
	&dev_attr_multicast_snooping.attr,
	&dev_attr_multicast_querier.attr,
	&dev_attr_multicast_query_use_ifaddr.attr,
	&dev_attr_hash_elasticity.attr,
	&dev_attr_hash_max.attr,
	&dev_attr_multicast_last_member_count.attr,
	&dev_attr_multicast_startup_query_count.attr,
	&dev_attr_multicast_last_member_interval.attr,
	&dev_attr_multicast_membership_interval.attr,
	&dev_attr_multicast_querier_interval.attr,
	&dev_attr_multicast_query_interval.attr,
	&dev_attr_multicast_query_response_interval.attr,
	&dev_attr_multicast_startup_query_interval.attr,
	&dev_attr_multicast_stats_enabled.attr,
	&dev_attr_multicast_igmp_version.attr,
#if IS_ENABLED(CONFIG_IPV6)
	&dev_attr_multicast_mld_version.attr,
#endif
#endif
#if IS_ENABLED(CONFIG_BRIDGE_NETFILTER)
	&dev_attr_nf_call_iptables.attr,
	&dev_attr_nf_call_ip6tables.attr,
	&dev_attr_nf_call_arptables.attr,
#endif
#ifdef CONFIG_BRIDGE_VLAN_FILTERING
	&dev_attr_vlan_filtering.attr,
	&dev_attr_vlan_protocol.attr,
	&dev_attr_default_pvid.attr,
	&dev_attr_vlan_stats_enabled.attr,
	&dev_attr_vlan_stats_per_port.attr,
#endif
	NULL
};

static const struct attribute_group bridge_group = {
	.name = SYSFS_BRIDGE_ATTR,
	.attrs = bridge_attrs,
};

/*
 * Export the forwarding information table as a binary file
 * The records are struct __fdb_entry.
 *
 * Returns the number of bytes read.
 */
static ssize_t brforward_read(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *bin_attr,
			      char *buf, loff_t off, size_t count)
{
	struct device *dev = kobj_to_dev(kobj);
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
		  .mode = 0444, },
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
		err = -ENOMEM;
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
