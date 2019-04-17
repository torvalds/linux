/*
 * net-sysfs.c - network device class and attributes
 *
 * Copyright (c) 2003 Stephen Hemminger <shemminger@osdl.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/nsproxy.h>
#include <net/sock.h>
#include <net/net_namespace.h>
#include <linux/rtnetlink.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <linux/pm_runtime.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/cpu.h>

#include "net-sysfs.h"

#ifdef CONFIG_SYSFS
static const char fmt_hex[] = "%#x\n";
static const char fmt_dec[] = "%d\n";
static const char fmt_ulong[] = "%lu\n";
static const char fmt_u64[] = "%llu\n";

static inline int dev_isalive(const struct net_device *dev)
{
	return dev->reg_state <= NETREG_REGISTERED;
}

/* use same locking rules as GIF* ioctl's */
static ssize_t netdev_show(const struct device *dev,
			   struct device_attribute *attr, char *buf,
			   ssize_t (*format)(const struct net_device *, char *))
{
	struct net_device *ndev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (dev_isalive(ndev))
		ret = (*format)(ndev, buf);
	read_unlock(&dev_base_lock);

	return ret;
}

/* generate a show function for simple field */
#define NETDEVICE_SHOW(field, format_string)				\
static ssize_t format_##field(const struct net_device *dev, char *buf)	\
{									\
	return sprintf(buf, format_string, dev->field);			\
}									\
static ssize_t field##_show(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	return netdev_show(dev, attr, buf, format_##field);		\
}									\

#define NETDEVICE_SHOW_RO(field, format_string)				\
NETDEVICE_SHOW(field, format_string);					\
static DEVICE_ATTR_RO(field)

#define NETDEVICE_SHOW_RW(field, format_string)				\
NETDEVICE_SHOW(field, format_string);					\
static DEVICE_ATTR_RW(field)

/* use same locking and permission rules as SIF* ioctl's */
static ssize_t netdev_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len,
			    int (*set)(struct net_device *, unsigned long))
{
	struct net_device *netdev = to_net_dev(dev);
	struct net *net = dev_net(netdev);
	unsigned long new;
	int ret = -EINVAL;

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	ret = kstrtoul(buf, 0, &new);
	if (ret)
		goto err;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(netdev)) {
		ret = (*set)(netdev, new);
		if (ret == 0)
			ret = len;
	}
	rtnl_unlock();
 err:
	return ret;
}

NETDEVICE_SHOW_RO(dev_id, fmt_hex);
NETDEVICE_SHOW_RO(dev_port, fmt_dec);
NETDEVICE_SHOW_RO(addr_assign_type, fmt_dec);
NETDEVICE_SHOW_RO(addr_len, fmt_dec);
NETDEVICE_SHOW_RO(ifindex, fmt_dec);
NETDEVICE_SHOW_RO(type, fmt_dec);
NETDEVICE_SHOW_RO(link_mode, fmt_dec);

static ssize_t iflink_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct net_device *ndev = to_net_dev(dev);

	return sprintf(buf, fmt_dec, dev_get_iflink(ndev));
}
static DEVICE_ATTR_RO(iflink);

static ssize_t format_name_assign_type(const struct net_device *dev, char *buf)
{
	return sprintf(buf, fmt_dec, dev->name_assign_type);
}

static ssize_t name_assign_type_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	if (ndev->name_assign_type != NET_NAME_UNKNOWN)
		ret = netdev_show(dev, attr, buf, format_name_assign_type);

	return ret;
}
static DEVICE_ATTR_RO(name_assign_type);

/* use same locking rules as GIFHWADDR ioctl's */
static ssize_t address_show(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct net_device *ndev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (dev_isalive(ndev))
		ret = sysfs_format_mac(buf, ndev->dev_addr, ndev->addr_len);
	read_unlock(&dev_base_lock);
	return ret;
}
static DEVICE_ATTR_RO(address);

static ssize_t broadcast_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *ndev = to_net_dev(dev);

	if (dev_isalive(ndev))
		return sysfs_format_mac(buf, ndev->broadcast, ndev->addr_len);
	return -EINVAL;
}
static DEVICE_ATTR_RO(broadcast);

static int change_carrier(struct net_device *dev, unsigned long new_carrier)
{
	if (!netif_running(dev))
		return -EINVAL;
	return dev_change_carrier(dev, (bool)new_carrier);
}

static ssize_t carrier_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_carrier);
}

static ssize_t carrier_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	if (netif_running(netdev))
		return sprintf(buf, fmt_dec, !!netif_carrier_ok(netdev));

	return -EINVAL;
}
static DEVICE_ATTR_RW(carrier);

static ssize_t speed_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	int ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (netif_running(netdev)) {
		struct ethtool_link_ksettings cmd;

		if (!__ethtool_get_link_ksettings(netdev, &cmd))
			ret = sprintf(buf, fmt_dec, cmd.base.speed);
	}
	rtnl_unlock();
	return ret;
}
static DEVICE_ATTR_RO(speed);

static ssize_t duplex_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	int ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (netif_running(netdev)) {
		struct ethtool_link_ksettings cmd;

		if (!__ethtool_get_link_ksettings(netdev, &cmd)) {
			const char *duplex;

			switch (cmd.base.duplex) {
			case DUPLEX_HALF:
				duplex = "half";
				break;
			case DUPLEX_FULL:
				duplex = "full";
				break;
			default:
				duplex = "unknown";
				break;
			}
			ret = sprintf(buf, "%s\n", duplex);
		}
	}
	rtnl_unlock();
	return ret;
}
static DEVICE_ATTR_RO(duplex);

static ssize_t dormant_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	if (netif_running(netdev))
		return sprintf(buf, fmt_dec, !!netif_dormant(netdev));

	return -EINVAL;
}
static DEVICE_ATTR_RO(dormant);

static const char *const operstates[] = {
	"unknown",
	"notpresent", /* currently unused */
	"down",
	"lowerlayerdown",
	"testing", /* currently unused */
	"dormant",
	"up"
};

static ssize_t operstate_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	const struct net_device *netdev = to_net_dev(dev);
	unsigned char operstate;

	read_lock(&dev_base_lock);
	operstate = netdev->operstate;
	if (!netif_running(netdev))
		operstate = IF_OPER_DOWN;
	read_unlock(&dev_base_lock);

	if (operstate >= ARRAY_SIZE(operstates))
		return -EINVAL; /* should not happen */

	return sprintf(buf, "%s\n", operstates[operstate]);
}
static DEVICE_ATTR_RO(operstate);

static ssize_t carrier_changes_show(struct device *dev,
				    struct device_attribute *attr,
				    char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	return sprintf(buf, fmt_dec,
		       atomic_read(&netdev->carrier_up_count) +
		       atomic_read(&netdev->carrier_down_count));
}
static DEVICE_ATTR_RO(carrier_changes);

static ssize_t carrier_up_count_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	return sprintf(buf, fmt_dec, atomic_read(&netdev->carrier_up_count));
}
static DEVICE_ATTR_RO(carrier_up_count);

static ssize_t carrier_down_count_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	return sprintf(buf, fmt_dec, atomic_read(&netdev->carrier_down_count));
}
static DEVICE_ATTR_RO(carrier_down_count);

/* read-write attributes */

static int change_mtu(struct net_device *dev, unsigned long new_mtu)
{
	return dev_set_mtu(dev, (int)new_mtu);
}

static ssize_t mtu_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_mtu);
}
NETDEVICE_SHOW_RW(mtu, fmt_dec);

static int change_flags(struct net_device *dev, unsigned long new_flags)
{
	return dev_change_flags(dev, (unsigned int)new_flags, NULL);
}

static ssize_t flags_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_flags);
}
NETDEVICE_SHOW_RW(flags, fmt_hex);

static ssize_t tx_queue_len_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	return netdev_store(dev, attr, buf, len, dev_change_tx_queue_len);
}
NETDEVICE_SHOW_RW(tx_queue_len, fmt_dec);

static int change_gro_flush_timeout(struct net_device *dev, unsigned long val)
{
	dev->gro_flush_timeout = val;
	return 0;
}

static ssize_t gro_flush_timeout_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t len)
{
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	return netdev_store(dev, attr, buf, len, change_gro_flush_timeout);
}
NETDEVICE_SHOW_RW(gro_flush_timeout, fmt_ulong);

static ssize_t ifalias_store(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	struct net *net = dev_net(netdev);
	size_t count = len;
	ssize_t ret = 0;

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	/* ignore trailing newline */
	if (len >  0 && buf[len - 1] == '\n')
		--count;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(netdev)) {
		ret = dev_set_alias(netdev, buf, count);
		if (ret < 0)
			goto err;
		ret = len;
		netdev_state_change(netdev);
	}
err:
	rtnl_unlock();

	return ret;
}

static ssize_t ifalias_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	const struct net_device *netdev = to_net_dev(dev);
	char tmp[IFALIASZ];
	ssize_t ret = 0;

	ret = dev_get_alias(netdev, tmp, sizeof(tmp));
	if (ret > 0)
		ret = sprintf(buf, "%s\n", tmp);
	return ret;
}
static DEVICE_ATTR_RW(ifalias);

static int change_group(struct net_device *dev, unsigned long new_group)
{
	dev_set_group(dev, (int)new_group);
	return 0;
}

static ssize_t group_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_group);
}
NETDEVICE_SHOW(group, fmt_dec);
static DEVICE_ATTR(netdev_group, 0644, group_show, group_store);

static int change_proto_down(struct net_device *dev, unsigned long proto_down)
{
	return dev_change_proto_down(dev, (bool)proto_down);
}

static ssize_t proto_down_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_proto_down);
}
NETDEVICE_SHOW_RW(proto_down, fmt_dec);

static ssize_t phys_port_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(netdev)) {
		struct netdev_phys_item_id ppid;

		ret = dev_get_phys_port_id(netdev, &ppid);
		if (!ret)
			ret = sprintf(buf, "%*phN\n", ppid.id_len, ppid.id);
	}
	rtnl_unlock();

	return ret;
}
static DEVICE_ATTR_RO(phys_port_id);

static ssize_t phys_port_name_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(netdev)) {
		char name[IFNAMSIZ];

		ret = dev_get_phys_port_name(netdev, name, sizeof(name));
		if (!ret)
			ret = sprintf(buf, "%s\n", name);
	}
	rtnl_unlock();

	return ret;
}
static DEVICE_ATTR_RO(phys_port_name);

static ssize_t phys_switch_id_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(netdev)) {
		struct netdev_phys_item_id ppid = { };

		ret = dev_get_port_parent_id(netdev, &ppid, false);
		if (!ret)
			ret = sprintf(buf, "%*phN\n", ppid.id_len, ppid.id);
	}
	rtnl_unlock();

	return ret;
}
static DEVICE_ATTR_RO(phys_switch_id);

static struct attribute *net_class_attrs[] __ro_after_init = {
	&dev_attr_netdev_group.attr,
	&dev_attr_type.attr,
	&dev_attr_dev_id.attr,
	&dev_attr_dev_port.attr,
	&dev_attr_iflink.attr,
	&dev_attr_ifindex.attr,
	&dev_attr_name_assign_type.attr,
	&dev_attr_addr_assign_type.attr,
	&dev_attr_addr_len.attr,
	&dev_attr_link_mode.attr,
	&dev_attr_address.attr,
	&dev_attr_broadcast.attr,
	&dev_attr_speed.attr,
	&dev_attr_duplex.attr,
	&dev_attr_dormant.attr,
	&dev_attr_operstate.attr,
	&dev_attr_carrier_changes.attr,
	&dev_attr_ifalias.attr,
	&dev_attr_carrier.attr,
	&dev_attr_mtu.attr,
	&dev_attr_flags.attr,
	&dev_attr_tx_queue_len.attr,
	&dev_attr_gro_flush_timeout.attr,
	&dev_attr_phys_port_id.attr,
	&dev_attr_phys_port_name.attr,
	&dev_attr_phys_switch_id.attr,
	&dev_attr_proto_down.attr,
	&dev_attr_carrier_up_count.attr,
	&dev_attr_carrier_down_count.attr,
	NULL,
};
ATTRIBUTE_GROUPS(net_class);

/* Show a given an attribute in the statistics group */
static ssize_t netstat_show(const struct device *d,
			    struct device_attribute *attr, char *buf,
			    unsigned long offset)
{
	struct net_device *dev = to_net_dev(d);
	ssize_t ret = -EINVAL;

	WARN_ON(offset > sizeof(struct rtnl_link_stats64) ||
		offset % sizeof(u64) != 0);

	read_lock(&dev_base_lock);
	if (dev_isalive(dev)) {
		struct rtnl_link_stats64 temp;
		const struct rtnl_link_stats64 *stats = dev_get_stats(dev, &temp);

		ret = sprintf(buf, fmt_u64, *(u64 *)(((u8 *)stats) + offset));
	}
	read_unlock(&dev_base_lock);
	return ret;
}

/* generate a read-only statistics attribute */
#define NETSTAT_ENTRY(name)						\
static ssize_t name##_show(struct device *d,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	return netstat_show(d, attr, buf,				\
			    offsetof(struct rtnl_link_stats64, name));	\
}									\
static DEVICE_ATTR_RO(name)

NETSTAT_ENTRY(rx_packets);
NETSTAT_ENTRY(tx_packets);
NETSTAT_ENTRY(rx_bytes);
NETSTAT_ENTRY(tx_bytes);
NETSTAT_ENTRY(rx_errors);
NETSTAT_ENTRY(tx_errors);
NETSTAT_ENTRY(rx_dropped);
NETSTAT_ENTRY(tx_dropped);
NETSTAT_ENTRY(multicast);
NETSTAT_ENTRY(collisions);
NETSTAT_ENTRY(rx_length_errors);
NETSTAT_ENTRY(rx_over_errors);
NETSTAT_ENTRY(rx_crc_errors);
NETSTAT_ENTRY(rx_frame_errors);
NETSTAT_ENTRY(rx_fifo_errors);
NETSTAT_ENTRY(rx_missed_errors);
NETSTAT_ENTRY(tx_aborted_errors);
NETSTAT_ENTRY(tx_carrier_errors);
NETSTAT_ENTRY(tx_fifo_errors);
NETSTAT_ENTRY(tx_heartbeat_errors);
NETSTAT_ENTRY(tx_window_errors);
NETSTAT_ENTRY(rx_compressed);
NETSTAT_ENTRY(tx_compressed);
NETSTAT_ENTRY(rx_nohandler);

static struct attribute *netstat_attrs[] __ro_after_init = {
	&dev_attr_rx_packets.attr,
	&dev_attr_tx_packets.attr,
	&dev_attr_rx_bytes.attr,
	&dev_attr_tx_bytes.attr,
	&dev_attr_rx_errors.attr,
	&dev_attr_tx_errors.attr,
	&dev_attr_rx_dropped.attr,
	&dev_attr_tx_dropped.attr,
	&dev_attr_multicast.attr,
	&dev_attr_collisions.attr,
	&dev_attr_rx_length_errors.attr,
	&dev_attr_rx_over_errors.attr,
	&dev_attr_rx_crc_errors.attr,
	&dev_attr_rx_frame_errors.attr,
	&dev_attr_rx_fifo_errors.attr,
	&dev_attr_rx_missed_errors.attr,
	&dev_attr_tx_aborted_errors.attr,
	&dev_attr_tx_carrier_errors.attr,
	&dev_attr_tx_fifo_errors.attr,
	&dev_attr_tx_heartbeat_errors.attr,
	&dev_attr_tx_window_errors.attr,
	&dev_attr_rx_compressed.attr,
	&dev_attr_tx_compressed.attr,
	&dev_attr_rx_nohandler.attr,
	NULL
};

static const struct attribute_group netstat_group = {
	.name  = "statistics",
	.attrs  = netstat_attrs,
};

#if IS_ENABLED(CONFIG_WIRELESS_EXT) || IS_ENABLED(CONFIG_CFG80211)
static struct attribute *wireless_attrs[] = {
	NULL
};

static const struct attribute_group wireless_group = {
	.name = "wireless",
	.attrs = wireless_attrs,
};
#endif

#else /* CONFIG_SYSFS */
#define net_class_groups	NULL
#endif /* CONFIG_SYSFS */

#ifdef CONFIG_SYSFS
#define to_rx_queue_attr(_attr) \
	container_of(_attr, struct rx_queue_attribute, attr)

#define to_rx_queue(obj) container_of(obj, struct netdev_rx_queue, kobj)

static ssize_t rx_queue_attr_show(struct kobject *kobj, struct attribute *attr,
				  char *buf)
{
	const struct rx_queue_attribute *attribute = to_rx_queue_attr(attr);
	struct netdev_rx_queue *queue = to_rx_queue(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(queue, buf);
}

static ssize_t rx_queue_attr_store(struct kobject *kobj, struct attribute *attr,
				   const char *buf, size_t count)
{
	const struct rx_queue_attribute *attribute = to_rx_queue_attr(attr);
	struct netdev_rx_queue *queue = to_rx_queue(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(queue, buf, count);
}

static const struct sysfs_ops rx_queue_sysfs_ops = {
	.show = rx_queue_attr_show,
	.store = rx_queue_attr_store,
};

#ifdef CONFIG_RPS
static ssize_t show_rps_map(struct netdev_rx_queue *queue, char *buf)
{
	struct rps_map *map;
	cpumask_var_t mask;
	int i, len;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	rcu_read_lock();
	map = rcu_dereference(queue->rps_map);
	if (map)
		for (i = 0; i < map->len; i++)
			cpumask_set_cpu(map->cpus[i], mask);

	len = snprintf(buf, PAGE_SIZE, "%*pb\n", cpumask_pr_args(mask));
	rcu_read_unlock();
	free_cpumask_var(mask);

	return len < PAGE_SIZE ? len : -EINVAL;
}

static ssize_t store_rps_map(struct netdev_rx_queue *queue,
			     const char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_MUTEX(rps_map_mutex);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		return err;
	}

	map = kzalloc(max_t(unsigned int,
			    RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
		      GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu_and(cpu, mask, cpu_online_mask)
		map->cpus[i++] = cpu;

	if (i) {
		map->len = i;
	} else {
		kfree(map);
		map = NULL;
	}

	mutex_lock(&rps_map_mutex);
	old_map = rcu_dereference_protected(queue->rps_map,
					    mutex_is_locked(&rps_map_mutex));
	rcu_assign_pointer(queue->rps_map, map);

	if (map)
		static_branch_inc(&rps_needed);
	if (old_map)
		static_branch_dec(&rps_needed);

	mutex_unlock(&rps_map_mutex);

	if (old_map)
		kfree_rcu(old_map, rcu);

	free_cpumask_var(mask);
	return len;
}

static ssize_t show_rps_dev_flow_table_cnt(struct netdev_rx_queue *queue,
					   char *buf)
{
	struct rps_dev_flow_table *flow_table;
	unsigned long val = 0;

	rcu_read_lock();
	flow_table = rcu_dereference(queue->rps_flow_table);
	if (flow_table)
		val = (unsigned long)flow_table->mask + 1;
	rcu_read_unlock();

	return sprintf(buf, "%lu\n", val);
}

static void rps_dev_flow_table_release(struct rcu_head *rcu)
{
	struct rps_dev_flow_table *table = container_of(rcu,
	    struct rps_dev_flow_table, rcu);
	vfree(table);
}

static ssize_t store_rps_dev_flow_table_cnt(struct netdev_rx_queue *queue,
					    const char *buf, size_t len)
{
	unsigned long mask, count;
	struct rps_dev_flow_table *table, *old_table;
	static DEFINE_SPINLOCK(rps_dev_flow_lock);
	int rc;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	rc = kstrtoul(buf, 0, &count);
	if (rc < 0)
		return rc;

	if (count) {
		mask = count - 1;
		/* mask = roundup_pow_of_two(count) - 1;
		 * without overflows...
		 */
		while ((mask | (mask >> 1)) != mask)
			mask |= (mask >> 1);
		/* On 64 bit arches, must check mask fits in table->mask (u32),
		 * and on 32bit arches, must check
		 * RPS_DEV_FLOW_TABLE_SIZE(mask + 1) doesn't overflow.
		 */
#if BITS_PER_LONG > 32
		if (mask > (unsigned long)(u32)mask)
			return -EINVAL;
#else
		if (mask > (ULONG_MAX - RPS_DEV_FLOW_TABLE_SIZE(1))
				/ sizeof(struct rps_dev_flow)) {
			/* Enforce a limit to prevent overflow */
			return -EINVAL;
		}
#endif
		table = vmalloc(RPS_DEV_FLOW_TABLE_SIZE(mask + 1));
		if (!table)
			return -ENOMEM;

		table->mask = mask;
		for (count = 0; count <= mask; count++)
			table->flows[count].cpu = RPS_NO_CPU;
	} else {
		table = NULL;
	}

	spin_lock(&rps_dev_flow_lock);
	old_table = rcu_dereference_protected(queue->rps_flow_table,
					      lockdep_is_held(&rps_dev_flow_lock));
	rcu_assign_pointer(queue->rps_flow_table, table);
	spin_unlock(&rps_dev_flow_lock);

	if (old_table)
		call_rcu(&old_table->rcu, rps_dev_flow_table_release);

	return len;
}

static struct rx_queue_attribute rps_cpus_attribute __ro_after_init
	= __ATTR(rps_cpus, 0644, show_rps_map, store_rps_map);

static struct rx_queue_attribute rps_dev_flow_table_cnt_attribute __ro_after_init
	= __ATTR(rps_flow_cnt, 0644,
		 show_rps_dev_flow_table_cnt, store_rps_dev_flow_table_cnt);
#endif /* CONFIG_RPS */

static struct attribute *rx_queue_default_attrs[] __ro_after_init = {
#ifdef CONFIG_RPS
	&rps_cpus_attribute.attr,
	&rps_dev_flow_table_cnt_attribute.attr,
#endif
	NULL
};

static void rx_queue_release(struct kobject *kobj)
{
	struct netdev_rx_queue *queue = to_rx_queue(kobj);
#ifdef CONFIG_RPS
	struct rps_map *map;
	struct rps_dev_flow_table *flow_table;

	map = rcu_dereference_protected(queue->rps_map, 1);
	if (map) {
		RCU_INIT_POINTER(queue->rps_map, NULL);
		kfree_rcu(map, rcu);
	}

	flow_table = rcu_dereference_protected(queue->rps_flow_table, 1);
	if (flow_table) {
		RCU_INIT_POINTER(queue->rps_flow_table, NULL);
		call_rcu(&flow_table->rcu, rps_dev_flow_table_release);
	}
#endif

	memset(kobj, 0, sizeof(*kobj));
	dev_put(queue->dev);
}

static const void *rx_queue_namespace(struct kobject *kobj)
{
	struct netdev_rx_queue *queue = to_rx_queue(kobj);
	struct device *dev = &queue->dev->dev;
	const void *ns = NULL;

	if (dev->class && dev->class->ns_type)
		ns = dev->class->namespace(dev);

	return ns;
}

static void rx_queue_get_ownership(struct kobject *kobj,
				   kuid_t *uid, kgid_t *gid)
{
	const struct net *net = rx_queue_namespace(kobj);

	net_ns_get_ownership(net, uid, gid);
}

static struct kobj_type rx_queue_ktype __ro_after_init = {
	.sysfs_ops = &rx_queue_sysfs_ops,
	.release = rx_queue_release,
	.default_attrs = rx_queue_default_attrs,
	.namespace = rx_queue_namespace,
	.get_ownership = rx_queue_get_ownership,
};

static int rx_queue_add_kobject(struct net_device *dev, int index)
{
	struct netdev_rx_queue *queue = dev->_rx + index;
	struct kobject *kobj = &queue->kobj;
	int error = 0;

	kobj->kset = dev->queues_kset;
	error = kobject_init_and_add(kobj, &rx_queue_ktype, NULL,
				     "rx-%u", index);
	if (error)
		return error;

	dev_hold(queue->dev);

	if (dev->sysfs_rx_queue_group) {
		error = sysfs_create_group(kobj, dev->sysfs_rx_queue_group);
		if (error) {
			kobject_put(kobj);
			return error;
		}
	}

	kobject_uevent(kobj, KOBJ_ADD);

	return error;
}
#endif /* CONFIG_SYSFS */

int
net_rx_queue_update_kobjects(struct net_device *dev, int old_num, int new_num)
{
#ifdef CONFIG_SYSFS
	int i;
	int error = 0;

#ifndef CONFIG_RPS
	if (!dev->sysfs_rx_queue_group)
		return 0;
#endif
	for (i = old_num; i < new_num; i++) {
		error = rx_queue_add_kobject(dev, i);
		if (error) {
			new_num = old_num;
			break;
		}
	}

	while (--i >= new_num) {
		struct kobject *kobj = &dev->_rx[i].kobj;

		if (!refcount_read(&dev_net(dev)->count))
			kobj->uevent_suppress = 1;
		if (dev->sysfs_rx_queue_group)
			sysfs_remove_group(kobj, dev->sysfs_rx_queue_group);
		kobject_put(kobj);
	}

	return error;
#else
	return 0;
#endif
}

#ifdef CONFIG_SYSFS
/*
 * netdev_queue sysfs structures and functions.
 */
struct netdev_queue_attribute {
	struct attribute attr;
	ssize_t (*show)(struct netdev_queue *queue, char *buf);
	ssize_t (*store)(struct netdev_queue *queue,
			 const char *buf, size_t len);
};
#define to_netdev_queue_attr(_attr) \
	container_of(_attr, struct netdev_queue_attribute, attr)

#define to_netdev_queue(obj) container_of(obj, struct netdev_queue, kobj)

static ssize_t netdev_queue_attr_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	const struct netdev_queue_attribute *attribute
		= to_netdev_queue_attr(attr);
	struct netdev_queue *queue = to_netdev_queue(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(queue, buf);
}

static ssize_t netdev_queue_attr_store(struct kobject *kobj,
				       struct attribute *attr,
				       const char *buf, size_t count)
{
	const struct netdev_queue_attribute *attribute
		= to_netdev_queue_attr(attr);
	struct netdev_queue *queue = to_netdev_queue(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(queue, buf, count);
}

static const struct sysfs_ops netdev_queue_sysfs_ops = {
	.show = netdev_queue_attr_show,
	.store = netdev_queue_attr_store,
};

static ssize_t tx_timeout_show(struct netdev_queue *queue, char *buf)
{
	unsigned long trans_timeout;

	spin_lock_irq(&queue->_xmit_lock);
	trans_timeout = queue->trans_timeout;
	spin_unlock_irq(&queue->_xmit_lock);

	return sprintf(buf, "%lu", trans_timeout);
}

static unsigned int get_netdev_queue_index(struct netdev_queue *queue)
{
	struct net_device *dev = queue->dev;
	unsigned int i;

	i = queue - dev->_tx;
	BUG_ON(i >= dev->num_tx_queues);

	return i;
}

static ssize_t traffic_class_show(struct netdev_queue *queue,
				  char *buf)
{
	struct net_device *dev = queue->dev;
	int index;
	int tc;

	if (!netif_is_multiqueue(dev))
		return -ENOENT;

	index = get_netdev_queue_index(queue);

	/* If queue belongs to subordinate dev use its TC mapping */
	dev = netdev_get_tx_queue(dev, index)->sb_dev ? : dev;

	tc = netdev_txq_to_tc(dev, index);
	if (tc < 0)
		return -EINVAL;

	/* We can report the traffic class one of two ways:
	 * Subordinate device traffic classes are reported with the traffic
	 * class first, and then the subordinate class so for example TC0 on
	 * subordinate device 2 will be reported as "0-2". If the queue
	 * belongs to the root device it will be reported with just the
	 * traffic class, so just "0" for TC 0 for example.
	 */
	return dev->num_tc < 0 ? sprintf(buf, "%u%d\n", tc, dev->num_tc) :
				 sprintf(buf, "%u\n", tc);
}

#ifdef CONFIG_XPS
static ssize_t tx_maxrate_show(struct netdev_queue *queue,
			       char *buf)
{
	return sprintf(buf, "%lu\n", queue->tx_maxrate);
}

static ssize_t tx_maxrate_store(struct netdev_queue *queue,
				const char *buf, size_t len)
{
	struct net_device *dev = queue->dev;
	int err, index = get_netdev_queue_index(queue);
	u32 rate = 0;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	err = kstrtou32(buf, 10, &rate);
	if (err < 0)
		return err;

	if (!rtnl_trylock())
		return restart_syscall();

	err = -EOPNOTSUPP;
	if (dev->netdev_ops->ndo_set_tx_maxrate)
		err = dev->netdev_ops->ndo_set_tx_maxrate(dev, index, rate);

	rtnl_unlock();
	if (!err) {
		queue->tx_maxrate = rate;
		return len;
	}
	return err;
}

static struct netdev_queue_attribute queue_tx_maxrate __ro_after_init
	= __ATTR_RW(tx_maxrate);
#endif

static struct netdev_queue_attribute queue_trans_timeout __ro_after_init
	= __ATTR_RO(tx_timeout);

static struct netdev_queue_attribute queue_traffic_class __ro_after_init
	= __ATTR_RO(traffic_class);

#ifdef CONFIG_BQL
/*
 * Byte queue limits sysfs structures and functions.
 */
static ssize_t bql_show(char *buf, unsigned int value)
{
	return sprintf(buf, "%u\n", value);
}

static ssize_t bql_set(const char *buf, const size_t count,
		       unsigned int *pvalue)
{
	unsigned int value;
	int err;

	if (!strcmp(buf, "max") || !strcmp(buf, "max\n")) {
		value = DQL_MAX_LIMIT;
	} else {
		err = kstrtouint(buf, 10, &value);
		if (err < 0)
			return err;
		if (value > DQL_MAX_LIMIT)
			return -EINVAL;
	}

	*pvalue = value;

	return count;
}

static ssize_t bql_show_hold_time(struct netdev_queue *queue,
				  char *buf)
{
	struct dql *dql = &queue->dql;

	return sprintf(buf, "%u\n", jiffies_to_msecs(dql->slack_hold_time));
}

static ssize_t bql_set_hold_time(struct netdev_queue *queue,
				 const char *buf, size_t len)
{
	struct dql *dql = &queue->dql;
	unsigned int value;
	int err;

	err = kstrtouint(buf, 10, &value);
	if (err < 0)
		return err;

	dql->slack_hold_time = msecs_to_jiffies(value);

	return len;
}

static struct netdev_queue_attribute bql_hold_time_attribute __ro_after_init
	= __ATTR(hold_time, 0644,
		 bql_show_hold_time, bql_set_hold_time);

static ssize_t bql_show_inflight(struct netdev_queue *queue,
				 char *buf)
{
	struct dql *dql = &queue->dql;

	return sprintf(buf, "%u\n", dql->num_queued - dql->num_completed);
}

static struct netdev_queue_attribute bql_inflight_attribute __ro_after_init =
	__ATTR(inflight, 0444, bql_show_inflight, NULL);

#define BQL_ATTR(NAME, FIELD)						\
static ssize_t bql_show_ ## NAME(struct netdev_queue *queue,		\
				 char *buf)				\
{									\
	return bql_show(buf, queue->dql.FIELD);				\
}									\
									\
static ssize_t bql_set_ ## NAME(struct netdev_queue *queue,		\
				const char *buf, size_t len)		\
{									\
	return bql_set(buf, len, &queue->dql.FIELD);			\
}									\
									\
static struct netdev_queue_attribute bql_ ## NAME ## _attribute __ro_after_init \
	= __ATTR(NAME, 0644,				\
		 bql_show_ ## NAME, bql_set_ ## NAME)

BQL_ATTR(limit, limit);
BQL_ATTR(limit_max, max_limit);
BQL_ATTR(limit_min, min_limit);

static struct attribute *dql_attrs[] __ro_after_init = {
	&bql_limit_attribute.attr,
	&bql_limit_max_attribute.attr,
	&bql_limit_min_attribute.attr,
	&bql_hold_time_attribute.attr,
	&bql_inflight_attribute.attr,
	NULL
};

static const struct attribute_group dql_group = {
	.name  = "byte_queue_limits",
	.attrs  = dql_attrs,
};
#endif /* CONFIG_BQL */

#ifdef CONFIG_XPS
static ssize_t xps_cpus_show(struct netdev_queue *queue,
			     char *buf)
{
	struct net_device *dev = queue->dev;
	int cpu, len, num_tc = 1, tc = 0;
	struct xps_dev_maps *dev_maps;
	cpumask_var_t mask;
	unsigned long index;

	if (!netif_is_multiqueue(dev))
		return -ENOENT;

	index = get_netdev_queue_index(queue);

	if (dev->num_tc) {
		/* Do not allow XPS on subordinate device directly */
		num_tc = dev->num_tc;
		if (num_tc < 0)
			return -EINVAL;

		/* If queue belongs to subordinate dev use its map */
		dev = netdev_get_tx_queue(dev, index)->sb_dev ? : dev;

		tc = netdev_txq_to_tc(dev, index);
		if (tc < 0)
			return -EINVAL;
	}

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	rcu_read_lock();
	dev_maps = rcu_dereference(dev->xps_cpus_map);
	if (dev_maps) {
		for_each_possible_cpu(cpu) {
			int i, tci = cpu * num_tc + tc;
			struct xps_map *map;

			map = rcu_dereference(dev_maps->attr_map[tci]);
			if (!map)
				continue;

			for (i = map->len; i--;) {
				if (map->queues[i] == index) {
					cpumask_set_cpu(cpu, mask);
					break;
				}
			}
		}
	}
	rcu_read_unlock();

	len = snprintf(buf, PAGE_SIZE, "%*pb\n", cpumask_pr_args(mask));
	free_cpumask_var(mask);
	return len < PAGE_SIZE ? len : -EINVAL;
}

static ssize_t xps_cpus_store(struct netdev_queue *queue,
			      const char *buf, size_t len)
{
	struct net_device *dev = queue->dev;
	unsigned long index;
	cpumask_var_t mask;
	int err;

	if (!netif_is_multiqueue(dev))
		return -ENOENT;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	index = get_netdev_queue_index(queue);

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		return err;
	}

	err = netif_set_xps_queue(dev, mask, index);

	free_cpumask_var(mask);

	return err ? : len;
}

static struct netdev_queue_attribute xps_cpus_attribute __ro_after_init
	= __ATTR_RW(xps_cpus);

static ssize_t xps_rxqs_show(struct netdev_queue *queue, char *buf)
{
	struct net_device *dev = queue->dev;
	struct xps_dev_maps *dev_maps;
	unsigned long *mask, index;
	int j, len, num_tc = 1, tc = 0;

	index = get_netdev_queue_index(queue);

	if (dev->num_tc) {
		num_tc = dev->num_tc;
		tc = netdev_txq_to_tc(dev, index);
		if (tc < 0)
			return -EINVAL;
	}
	mask = bitmap_zalloc(dev->num_rx_queues, GFP_KERNEL);
	if (!mask)
		return -ENOMEM;

	rcu_read_lock();
	dev_maps = rcu_dereference(dev->xps_rxqs_map);
	if (!dev_maps)
		goto out_no_maps;

	for (j = -1; j = netif_attrmask_next(j, NULL, dev->num_rx_queues),
	     j < dev->num_rx_queues;) {
		int i, tci = j * num_tc + tc;
		struct xps_map *map;

		map = rcu_dereference(dev_maps->attr_map[tci]);
		if (!map)
			continue;

		for (i = map->len; i--;) {
			if (map->queues[i] == index) {
				set_bit(j, mask);
				break;
			}
		}
	}
out_no_maps:
	rcu_read_unlock();

	len = bitmap_print_to_pagebuf(false, buf, mask, dev->num_rx_queues);
	bitmap_free(mask);

	return len < PAGE_SIZE ? len : -EINVAL;
}

static ssize_t xps_rxqs_store(struct netdev_queue *queue, const char *buf,
			      size_t len)
{
	struct net_device *dev = queue->dev;
	struct net *net = dev_net(dev);
	unsigned long *mask, index;
	int err;

	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	mask = bitmap_zalloc(dev->num_rx_queues, GFP_KERNEL);
	if (!mask)
		return -ENOMEM;

	index = get_netdev_queue_index(queue);

	err = bitmap_parse(buf, len, mask, dev->num_rx_queues);
	if (err) {
		bitmap_free(mask);
		return err;
	}

	cpus_read_lock();
	err = __netif_set_xps_queue(dev, mask, index, true);
	cpus_read_unlock();

	bitmap_free(mask);
	return err ? : len;
}

static struct netdev_queue_attribute xps_rxqs_attribute __ro_after_init
	= __ATTR_RW(xps_rxqs);
#endif /* CONFIG_XPS */

static struct attribute *netdev_queue_default_attrs[] __ro_after_init = {
	&queue_trans_timeout.attr,
	&queue_traffic_class.attr,
#ifdef CONFIG_XPS
	&xps_cpus_attribute.attr,
	&xps_rxqs_attribute.attr,
	&queue_tx_maxrate.attr,
#endif
	NULL
};

static void netdev_queue_release(struct kobject *kobj)
{
	struct netdev_queue *queue = to_netdev_queue(kobj);

	memset(kobj, 0, sizeof(*kobj));
	dev_put(queue->dev);
}

static const void *netdev_queue_namespace(struct kobject *kobj)
{
	struct netdev_queue *queue = to_netdev_queue(kobj);
	struct device *dev = &queue->dev->dev;
	const void *ns = NULL;

	if (dev->class && dev->class->ns_type)
		ns = dev->class->namespace(dev);

	return ns;
}

static void netdev_queue_get_ownership(struct kobject *kobj,
				       kuid_t *uid, kgid_t *gid)
{
	const struct net *net = netdev_queue_namespace(kobj);

	net_ns_get_ownership(net, uid, gid);
}

static struct kobj_type netdev_queue_ktype __ro_after_init = {
	.sysfs_ops = &netdev_queue_sysfs_ops,
	.release = netdev_queue_release,
	.default_attrs = netdev_queue_default_attrs,
	.namespace = netdev_queue_namespace,
	.get_ownership = netdev_queue_get_ownership,
};

static int netdev_queue_add_kobject(struct net_device *dev, int index)
{
	struct netdev_queue *queue = dev->_tx + index;
	struct kobject *kobj = &queue->kobj;
	int error = 0;

	kobj->kset = dev->queues_kset;
	error = kobject_init_and_add(kobj, &netdev_queue_ktype, NULL,
				     "tx-%u", index);
	if (error)
		return error;

	dev_hold(queue->dev);

#ifdef CONFIG_BQL
	error = sysfs_create_group(kobj, &dql_group);
	if (error) {
		kobject_put(kobj);
		return error;
	}
#endif

	kobject_uevent(kobj, KOBJ_ADD);

	return 0;
}
#endif /* CONFIG_SYSFS */

int
netdev_queue_update_kobjects(struct net_device *dev, int old_num, int new_num)
{
#ifdef CONFIG_SYSFS
	int i;
	int error = 0;

	for (i = old_num; i < new_num; i++) {
		error = netdev_queue_add_kobject(dev, i);
		if (error) {
			new_num = old_num;
			break;
		}
	}

	while (--i >= new_num) {
		struct netdev_queue *queue = dev->_tx + i;

		if (!refcount_read(&dev_net(dev)->count))
			queue->kobj.uevent_suppress = 1;
#ifdef CONFIG_BQL
		sysfs_remove_group(&queue->kobj, &dql_group);
#endif
		kobject_put(&queue->kobj);
	}

	return error;
#else
	return 0;
#endif /* CONFIG_SYSFS */
}

static int register_queue_kobjects(struct net_device *dev)
{
	int error = 0, txq = 0, rxq = 0, real_rx = 0, real_tx = 0;

#ifdef CONFIG_SYSFS
	dev->queues_kset = kset_create_and_add("queues",
					       NULL, &dev->dev.kobj);
	if (!dev->queues_kset)
		return -ENOMEM;
	real_rx = dev->real_num_rx_queues;
#endif
	real_tx = dev->real_num_tx_queues;

	error = net_rx_queue_update_kobjects(dev, 0, real_rx);
	if (error)
		goto error;
	rxq = real_rx;

	error = netdev_queue_update_kobjects(dev, 0, real_tx);
	if (error)
		goto error;
	txq = real_tx;

	return 0;

error:
	netdev_queue_update_kobjects(dev, txq, 0);
	net_rx_queue_update_kobjects(dev, rxq, 0);
#ifdef CONFIG_SYSFS
	kset_unregister(dev->queues_kset);
#endif
	return error;
}

static void remove_queue_kobjects(struct net_device *dev)
{
	int real_rx = 0, real_tx = 0;

#ifdef CONFIG_SYSFS
	real_rx = dev->real_num_rx_queues;
#endif
	real_tx = dev->real_num_tx_queues;

	net_rx_queue_update_kobjects(dev, real_rx, 0);
	netdev_queue_update_kobjects(dev, real_tx, 0);
#ifdef CONFIG_SYSFS
	kset_unregister(dev->queues_kset);
#endif
}

static bool net_current_may_mount(void)
{
	struct net *net = current->nsproxy->net_ns;

	return ns_capable(net->user_ns, CAP_SYS_ADMIN);
}

static void *net_grab_current_ns(void)
{
	struct net *ns = current->nsproxy->net_ns;
#ifdef CONFIG_NET_NS
	if (ns)
		refcount_inc(&ns->passive);
#endif
	return ns;
}

static const void *net_initial_ns(void)
{
	return &init_net;
}

static const void *net_netlink_ns(struct sock *sk)
{
	return sock_net(sk);
}

const struct kobj_ns_type_operations net_ns_type_operations = {
	.type = KOBJ_NS_TYPE_NET,
	.current_may_mount = net_current_may_mount,
	.grab_current_ns = net_grab_current_ns,
	.netlink_ns = net_netlink_ns,
	.initial_ns = net_initial_ns,
	.drop_ns = net_drop_ns,
};
EXPORT_SYMBOL_GPL(net_ns_type_operations);

static int netdev_uevent(struct device *d, struct kobj_uevent_env *env)
{
	struct net_device *dev = to_net_dev(d);
	int retval;

	/* pass interface to uevent. */
	retval = add_uevent_var(env, "INTERFACE=%s", dev->name);
	if (retval)
		goto exit;

	/* pass ifindex to uevent.
	 * ifindex is useful as it won't change (interface name may change)
	 * and is what RtNetlink uses natively.
	 */
	retval = add_uevent_var(env, "IFINDEX=%d", dev->ifindex);

exit:
	return retval;
}

/*
 *	netdev_release -- destroy and free a dead device.
 *	Called when last reference to device kobject is gone.
 */
static void netdev_release(struct device *d)
{
	struct net_device *dev = to_net_dev(d);

	BUG_ON(dev->reg_state != NETREG_RELEASED);

	/* no need to wait for rcu grace period:
	 * device is dead and about to be freed.
	 */
	kfree(rcu_access_pointer(dev->ifalias));
	netdev_freemem(dev);
}

static const void *net_namespace(struct device *d)
{
	struct net_device *dev = to_net_dev(d);

	return dev_net(dev);
}

static void net_get_ownership(struct device *d, kuid_t *uid, kgid_t *gid)
{
	struct net_device *dev = to_net_dev(d);
	const struct net *net = dev_net(dev);

	net_ns_get_ownership(net, uid, gid);
}

static struct class net_class __ro_after_init = {
	.name = "net",
	.dev_release = netdev_release,
	.dev_groups = net_class_groups,
	.dev_uevent = netdev_uevent,
	.ns_type = &net_ns_type_operations,
	.namespace = net_namespace,
	.get_ownership = net_get_ownership,
};

#ifdef CONFIG_OF_NET
static int of_dev_node_match(struct device *dev, const void *data)
{
	int ret = 0;

	if (dev->parent)
		ret = dev->parent->of_node == data;

	return ret == 0 ? dev->of_node == data : ret;
}

/*
 * of_find_net_device_by_node - lookup the net device for the device node
 * @np: OF device node
 *
 * Looks up the net_device structure corresponding with the device node.
 * If successful, returns a pointer to the net_device with the embedded
 * struct device refcount incremented by one, or NULL on failure. The
 * refcount must be dropped when done with the net_device.
 */
struct net_device *of_find_net_device_by_node(struct device_node *np)
{
	struct device *dev;

	dev = class_find_device(&net_class, NULL, np, of_dev_node_match);
	if (!dev)
		return NULL;

	return to_net_dev(dev);
}
EXPORT_SYMBOL(of_find_net_device_by_node);
#endif

/* Delete sysfs entries but hold kobject reference until after all
 * netdev references are gone.
 */
void netdev_unregister_kobject(struct net_device *ndev)
{
	struct device *dev = &ndev->dev;

	if (!refcount_read(&dev_net(ndev)->count))
		dev_set_uevent_suppress(dev, 1);

	kobject_get(&dev->kobj);

	remove_queue_kobjects(ndev);

	pm_runtime_set_memalloc_noio(dev, false);

	device_del(dev);
}

/* Create sysfs entries for network device. */
int netdev_register_kobject(struct net_device *ndev)
{
	struct device *dev = &ndev->dev;
	const struct attribute_group **groups = ndev->sysfs_groups;
	int error = 0;

	device_initialize(dev);
	dev->class = &net_class;
	dev->platform_data = ndev;
	dev->groups = groups;

	dev_set_name(dev, "%s", ndev->name);

#ifdef CONFIG_SYSFS
	/* Allow for a device specific group */
	if (*groups)
		groups++;

	*groups++ = &netstat_group;

#if IS_ENABLED(CONFIG_WIRELESS_EXT) || IS_ENABLED(CONFIG_CFG80211)
	if (ndev->ieee80211_ptr)
		*groups++ = &wireless_group;
#if IS_ENABLED(CONFIG_WIRELESS_EXT)
	else if (ndev->wireless_handlers)
		*groups++ = &wireless_group;
#endif
#endif
#endif /* CONFIG_SYSFS */

	error = device_add(dev);
	if (error)
		return error;

	error = register_queue_kobjects(ndev);
	if (error) {
		device_del(dev);
		return error;
	}

	pm_runtime_set_memalloc_noio(dev, true);

	return error;
}

int netdev_class_create_file_ns(const struct class_attribute *class_attr,
				const void *ns)
{
	return class_create_file_ns(&net_class, class_attr, ns);
}
EXPORT_SYMBOL(netdev_class_create_file_ns);

void netdev_class_remove_file_ns(const struct class_attribute *class_attr,
				 const void *ns)
{
	class_remove_file_ns(&net_class, class_attr, ns);
}
EXPORT_SYMBOL(netdev_class_remove_file_ns);

int __init netdev_kobject_init(void)
{
	kobj_ns_type_register(&net_ns_type_operations);
	return class_register(&net_class);
}
