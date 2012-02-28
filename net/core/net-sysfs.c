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
#include <linux/nsproxy.h>
#include <net/sock.h>
#include <net/net_namespace.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <linux/jiffies.h>
#include <net/wext.h>

#include "net-sysfs.h"

#ifdef CONFIG_SYSFS
static const char fmt_hex[] = "%#x\n";
static const char fmt_long_hex[] = "%#lx\n";
static const char fmt_dec[] = "%d\n";
static const char fmt_udec[] = "%u\n";
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
	struct net_device *net = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (dev_isalive(net))
		ret = (*format)(net, buf);
	read_unlock(&dev_base_lock);

	return ret;
}

/* generate a show function for simple field */
#define NETDEVICE_SHOW(field, format_string)				\
static ssize_t format_##field(const struct net_device *net, char *buf)	\
{									\
	return sprintf(buf, format_string, net->field);			\
}									\
static ssize_t show_##field(struct device *dev,				\
			    struct device_attribute *attr, char *buf)	\
{									\
	return netdev_show(dev, attr, buf, format_##field);		\
}


/* use same locking and permission rules as SIF* ioctl's */
static ssize_t netdev_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len,
			    int (*set)(struct net_device *, unsigned long))
{
	struct net_device *net = to_net_dev(dev);
	char *endp;
	unsigned long new;
	int ret = -EINVAL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	new = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		goto err;

	if (!rtnl_trylock())
		return restart_syscall();

	if (dev_isalive(net)) {
		if ((ret = (*set)(net, new)) == 0)
			ret = len;
	}
	rtnl_unlock();
 err:
	return ret;
}

NETDEVICE_SHOW(dev_id, fmt_hex);
NETDEVICE_SHOW(addr_assign_type, fmt_dec);
NETDEVICE_SHOW(addr_len, fmt_dec);
NETDEVICE_SHOW(iflink, fmt_dec);
NETDEVICE_SHOW(ifindex, fmt_dec);
NETDEVICE_SHOW(type, fmt_dec);
NETDEVICE_SHOW(link_mode, fmt_dec);

/* use same locking rules as GIFHWADDR ioctl's */
static ssize_t show_address(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct net_device *net = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (dev_isalive(net))
		ret = sysfs_format_mac(buf, net->dev_addr, net->addr_len);
	read_unlock(&dev_base_lock);
	return ret;
}

static ssize_t show_broadcast(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	if (dev_isalive(net))
		return sysfs_format_mac(buf, net->broadcast, net->addr_len);
	return -EINVAL;
}

static ssize_t show_carrier(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	if (netif_running(netdev)) {
		return sprintf(buf, fmt_dec, !!netif_carrier_ok(netdev));
	}
	return -EINVAL;
}

static ssize_t show_speed(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	int ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (netif_running(netdev)) {
		struct ethtool_cmd cmd;
		if (!__ethtool_get_settings(netdev, &cmd))
			ret = sprintf(buf, fmt_udec, ethtool_cmd_speed(&cmd));
	}
	rtnl_unlock();
	return ret;
}

static ssize_t show_duplex(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	int ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();

	if (netif_running(netdev)) {
		struct ethtool_cmd cmd;
		if (!__ethtool_get_settings(netdev, &cmd))
			ret = sprintf(buf, "%s\n",
				      cmd.duplex ? "full" : "half");
	}
	rtnl_unlock();
	return ret;
}

static ssize_t show_dormant(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	if (netif_running(netdev))
		return sprintf(buf, fmt_dec, !!netif_dormant(netdev));

	return -EINVAL;
}

static const char *const operstates[] = {
	"unknown",
	"notpresent", /* currently unused */
	"down",
	"lowerlayerdown",
	"testing", /* currently unused */
	"dormant",
	"up"
};

static ssize_t show_operstate(struct device *dev,
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

/* read-write attributes */
NETDEVICE_SHOW(mtu, fmt_dec);

static int change_mtu(struct net_device *net, unsigned long new_mtu)
{
	return dev_set_mtu(net, (int) new_mtu);
}

static ssize_t store_mtu(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_mtu);
}

NETDEVICE_SHOW(flags, fmt_hex);

static int change_flags(struct net_device *net, unsigned long new_flags)
{
	return dev_change_flags(net, (unsigned) new_flags);
}

static ssize_t store_flags(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_flags);
}

NETDEVICE_SHOW(tx_queue_len, fmt_ulong);

static int change_tx_queue_len(struct net_device *net, unsigned long new_len)
{
	net->tx_queue_len = new_len;
	return 0;
}

static ssize_t store_tx_queue_len(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_tx_queue_len);
}

static ssize_t store_ifalias(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	size_t count = len;
	ssize_t ret;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	/* ignore trailing newline */
	if (len >  0 && buf[len - 1] == '\n')
		--count;

	if (!rtnl_trylock())
		return restart_syscall();
	ret = dev_set_alias(netdev, buf, count);
	rtnl_unlock();

	return ret < 0 ? ret : len;
}

static ssize_t show_ifalias(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	const struct net_device *netdev = to_net_dev(dev);
	ssize_t ret = 0;

	if (!rtnl_trylock())
		return restart_syscall();
	if (netdev->ifalias)
		ret = sprintf(buf, "%s\n", netdev->ifalias);
	rtnl_unlock();
	return ret;
}

NETDEVICE_SHOW(group, fmt_dec);

static int change_group(struct net_device *net, unsigned long new_group)
{
	dev_set_group(net, (int) new_group);
	return 0;
}

static ssize_t store_group(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t len)
{
	return netdev_store(dev, attr, buf, len, change_group);
}

static struct device_attribute net_class_attributes[] = {
	__ATTR(addr_assign_type, S_IRUGO, show_addr_assign_type, NULL),
	__ATTR(addr_len, S_IRUGO, show_addr_len, NULL),
	__ATTR(dev_id, S_IRUGO, show_dev_id, NULL),
	__ATTR(ifalias, S_IRUGO | S_IWUSR, show_ifalias, store_ifalias),
	__ATTR(iflink, S_IRUGO, show_iflink, NULL),
	__ATTR(ifindex, S_IRUGO, show_ifindex, NULL),
	__ATTR(type, S_IRUGO, show_type, NULL),
	__ATTR(link_mode, S_IRUGO, show_link_mode, NULL),
	__ATTR(address, S_IRUGO, show_address, NULL),
	__ATTR(broadcast, S_IRUGO, show_broadcast, NULL),
	__ATTR(carrier, S_IRUGO, show_carrier, NULL),
	__ATTR(speed, S_IRUGO, show_speed, NULL),
	__ATTR(duplex, S_IRUGO, show_duplex, NULL),
	__ATTR(dormant, S_IRUGO, show_dormant, NULL),
	__ATTR(operstate, S_IRUGO, show_operstate, NULL),
	__ATTR(mtu, S_IRUGO | S_IWUSR, show_mtu, store_mtu),
	__ATTR(flags, S_IRUGO | S_IWUSR, show_flags, store_flags),
	__ATTR(tx_queue_len, S_IRUGO | S_IWUSR, show_tx_queue_len,
	       store_tx_queue_len),
	__ATTR(netdev_group, S_IRUGO | S_IWUSR, show_group, store_group),
	{}
};

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

		ret = sprintf(buf, fmt_u64, *(u64 *)(((u8 *) stats) + offset));
	}
	read_unlock(&dev_base_lock);
	return ret;
}

/* generate a read-only statistics attribute */
#define NETSTAT_ENTRY(name)						\
static ssize_t show_##name(struct device *d,				\
			   struct device_attribute *attr, char *buf) 	\
{									\
	return netstat_show(d, attr, buf,				\
			    offsetof(struct rtnl_link_stats64, name));	\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

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

static struct attribute *netstat_attrs[] = {
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
	NULL
};


static struct attribute_group netstat_group = {
	.name  = "statistics",
	.attrs  = netstat_attrs,
};

#ifdef CONFIG_WIRELESS_EXT_SYSFS
/* helper function that does all the locking etc for wireless stats */
static ssize_t wireless_show(struct device *d, char *buf,
			     ssize_t (*format)(const struct iw_statistics *,
					       char *))
{
	struct net_device *dev = to_net_dev(d);
	const struct iw_statistics *iw;
	ssize_t ret = -EINVAL;

	if (!rtnl_trylock())
		return restart_syscall();
	if (dev_isalive(dev)) {
		iw = get_wireless_stats(dev);
		if (iw)
			ret = (*format)(iw, buf);
	}
	rtnl_unlock();

	return ret;
}

/* show function template for wireless fields */
#define WIRELESS_SHOW(name, field, format_string)			\
static ssize_t format_iw_##name(const struct iw_statistics *iw, char *buf) \
{									\
	return sprintf(buf, format_string, iw->field);			\
}									\
static ssize_t show_iw_##name(struct device *d,				\
			      struct device_attribute *attr, char *buf)	\
{									\
	return wireless_show(d, buf, format_iw_##name);			\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_iw_##name, NULL)

WIRELESS_SHOW(status, status, fmt_hex);
WIRELESS_SHOW(link, qual.qual, fmt_dec);
WIRELESS_SHOW(level, qual.level, fmt_dec);
WIRELESS_SHOW(noise, qual.noise, fmt_dec);
WIRELESS_SHOW(nwid, discard.nwid, fmt_dec);
WIRELESS_SHOW(crypt, discard.code, fmt_dec);
WIRELESS_SHOW(fragment, discard.fragment, fmt_dec);
WIRELESS_SHOW(misc, discard.misc, fmt_dec);
WIRELESS_SHOW(retries, discard.retries, fmt_dec);
WIRELESS_SHOW(beacon, miss.beacon, fmt_dec);

static struct attribute *wireless_attrs[] = {
	&dev_attr_status.attr,
	&dev_attr_link.attr,
	&dev_attr_level.attr,
	&dev_attr_noise.attr,
	&dev_attr_nwid.attr,
	&dev_attr_crypt.attr,
	&dev_attr_fragment.attr,
	&dev_attr_retries.attr,
	&dev_attr_misc.attr,
	&dev_attr_beacon.attr,
	NULL
};

static struct attribute_group wireless_group = {
	.name = "wireless",
	.attrs = wireless_attrs,
};
#endif
#endif /* CONFIG_SYSFS */

#ifdef CONFIG_RPS
/*
 * RX queue sysfs structures and functions.
 */
struct rx_queue_attribute {
	struct attribute attr;
	ssize_t (*show)(struct netdev_rx_queue *queue,
	    struct rx_queue_attribute *attr, char *buf);
	ssize_t (*store)(struct netdev_rx_queue *queue,
	    struct rx_queue_attribute *attr, const char *buf, size_t len);
};
#define to_rx_queue_attr(_attr) container_of(_attr,		\
    struct rx_queue_attribute, attr)

#define to_rx_queue(obj) container_of(obj, struct netdev_rx_queue, kobj)

static ssize_t rx_queue_attr_show(struct kobject *kobj, struct attribute *attr,
				  char *buf)
{
	struct rx_queue_attribute *attribute = to_rx_queue_attr(attr);
	struct netdev_rx_queue *queue = to_rx_queue(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(queue, attribute, buf);
}

static ssize_t rx_queue_attr_store(struct kobject *kobj, struct attribute *attr,
				   const char *buf, size_t count)
{
	struct rx_queue_attribute *attribute = to_rx_queue_attr(attr);
	struct netdev_rx_queue *queue = to_rx_queue(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(queue, attribute, buf, count);
}

static const struct sysfs_ops rx_queue_sysfs_ops = {
	.show = rx_queue_attr_show,
	.store = rx_queue_attr_store,
};

static ssize_t show_rps_map(struct netdev_rx_queue *queue,
			    struct rx_queue_attribute *attribute, char *buf)
{
	struct rps_map *map;
	cpumask_var_t mask;
	size_t len = 0;
	int i;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	rcu_read_lock();
	map = rcu_dereference(queue->rps_map);
	if (map)
		for (i = 0; i < map->len; i++)
			cpumask_set_cpu(map->cpus[i], mask);

	len += cpumask_scnprintf(buf + len, PAGE_SIZE, mask);
	if (PAGE_SIZE - len < 3) {
		rcu_read_unlock();
		free_cpumask_var(mask);
		return -EINVAL;
	}
	rcu_read_unlock();

	free_cpumask_var(mask);
	len += sprintf(buf + len, "\n");
	return len;
}

static ssize_t store_rps_map(struct netdev_rx_queue *queue,
		      struct rx_queue_attribute *attribute,
		      const char *buf, size_t len)
{
	struct rps_map *old_map, *map;
	cpumask_var_t mask;
	int err, cpu, i;
	static DEFINE_SPINLOCK(rps_map_lock);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!alloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	err = bitmap_parse(buf, len, cpumask_bits(mask), nr_cpumask_bits);
	if (err) {
		free_cpumask_var(mask);
		return err;
	}

	map = kzalloc(max_t(unsigned,
	    RPS_MAP_SIZE(cpumask_weight(mask)), L1_CACHE_BYTES),
	    GFP_KERNEL);
	if (!map) {
		free_cpumask_var(mask);
		return -ENOMEM;
	}

	i = 0;
	for_each_cpu_and(cpu, mask, cpu_online_mask)
		map->cpus[i++] = cpu;

	if (i)
		map->len = i;
	else {
		kfree(map);
		map = NULL;
	}

	spin_lock(&rps_map_lock);
	old_map = rcu_dereference_protected(queue->rps_map,
					    lockdep_is_held(&rps_map_lock));
	rcu_assign_pointer(queue->rps_map, map);
	spin_unlock(&rps_map_lock);

	if (map)
		jump_label_inc(&rps_needed);
	if (old_map) {
		kfree_rcu(old_map, rcu);
		jump_label_dec(&rps_needed);
	}
	free_cpumask_var(mask);
	return len;
}

static ssize_t show_rps_dev_flow_table_cnt(struct netdev_rx_queue *queue,
					   struct rx_queue_attribute *attr,
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

static void rps_dev_flow_table_release_work(struct work_struct *work)
{
	struct rps_dev_flow_table *table = container_of(work,
	    struct rps_dev_flow_table, free_work);

	vfree(table);
}

static void rps_dev_flow_table_release(struct rcu_head *rcu)
{
	struct rps_dev_flow_table *table = container_of(rcu,
	    struct rps_dev_flow_table, rcu);

	INIT_WORK(&table->free_work, rps_dev_flow_table_release_work);
	schedule_work(&table->free_work);
}

static ssize_t store_rps_dev_flow_table_cnt(struct netdev_rx_queue *queue,
				     struct rx_queue_attribute *attr,
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
		 * and on 32bit arches, must check RPS_DEV_FLOW_TABLE_SIZE(mask + 1)
		 * doesnt overflow.
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
	} else
		table = NULL;

	spin_lock(&rps_dev_flow_lock);
	old_table = rcu_dereference_protected(queue->rps_flow_table,
					      lockdep_is_held(&rps_dev_flow_lock));
	rcu_assign_pointer(queue->rps_flow_table, table);
	spin_unlock(&rps_dev_flow_lock);

	if (old_table)
		call_rcu(&old_table->rcu, rps_dev_flow_table_release);

	return len;
}

static struct rx_queue_attribute rps_cpus_attribute =
	__ATTR(rps_cpus, S_IRUGO | S_IWUSR, show_rps_map, store_rps_map);


static struct rx_queue_attribute rps_dev_flow_table_cnt_attribute =
	__ATTR(rps_flow_cnt, S_IRUGO | S_IWUSR,
	    show_rps_dev_flow_table_cnt, store_rps_dev_flow_table_cnt);

static struct attribute *rx_queue_default_attrs[] = {
	&rps_cpus_attribute.attr,
	&rps_dev_flow_table_cnt_attribute.attr,
	NULL
};

static void rx_queue_release(struct kobject *kobj)
{
	struct netdev_rx_queue *queue = to_rx_queue(kobj);
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

	memset(kobj, 0, sizeof(*kobj));
	dev_put(queue->dev);
}

static struct kobj_type rx_queue_ktype = {
	.sysfs_ops = &rx_queue_sysfs_ops,
	.release = rx_queue_release,
	.default_attrs = rx_queue_default_attrs,
};

static int rx_queue_add_kobject(struct net_device *net, int index)
{
	struct netdev_rx_queue *queue = net->_rx + index;
	struct kobject *kobj = &queue->kobj;
	int error = 0;

	kobj->kset = net->queues_kset;
	error = kobject_init_and_add(kobj, &rx_queue_ktype, NULL,
	    "rx-%u", index);
	if (error) {
		kobject_put(kobj);
		return error;
	}

	kobject_uevent(kobj, KOBJ_ADD);
	dev_hold(queue->dev);

	return error;
}
#endif /* CONFIG_RPS */

int
net_rx_queue_update_kobjects(struct net_device *net, int old_num, int new_num)
{
#ifdef CONFIG_RPS
	int i;
	int error = 0;

	for (i = old_num; i < new_num; i++) {
		error = rx_queue_add_kobject(net, i);
		if (error) {
			new_num = old_num;
			break;
		}
	}

	while (--i >= new_num)
		kobject_put(&net->_rx[i].kobj);

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
	ssize_t (*show)(struct netdev_queue *queue,
	    struct netdev_queue_attribute *attr, char *buf);
	ssize_t (*store)(struct netdev_queue *queue,
	    struct netdev_queue_attribute *attr, const char *buf, size_t len);
};
#define to_netdev_queue_attr(_attr) container_of(_attr,		\
    struct netdev_queue_attribute, attr)

#define to_netdev_queue(obj) container_of(obj, struct netdev_queue, kobj)

static ssize_t netdev_queue_attr_show(struct kobject *kobj,
				      struct attribute *attr, char *buf)
{
	struct netdev_queue_attribute *attribute = to_netdev_queue_attr(attr);
	struct netdev_queue *queue = to_netdev_queue(kobj);

	if (!attribute->show)
		return -EIO;

	return attribute->show(queue, attribute, buf);
}

static ssize_t netdev_queue_attr_store(struct kobject *kobj,
				       struct attribute *attr,
				       const char *buf, size_t count)
{
	struct netdev_queue_attribute *attribute = to_netdev_queue_attr(attr);
	struct netdev_queue *queue = to_netdev_queue(kobj);

	if (!attribute->store)
		return -EIO;

	return attribute->store(queue, attribute, buf, count);
}

static const struct sysfs_ops netdev_queue_sysfs_ops = {
	.show = netdev_queue_attr_show,
	.store = netdev_queue_attr_store,
};

static ssize_t show_trans_timeout(struct netdev_queue *queue,
				  struct netdev_queue_attribute *attribute,
				  char *buf)
{
	unsigned long trans_timeout;

	spin_lock_irq(&queue->_xmit_lock);
	trans_timeout = queue->trans_timeout;
	spin_unlock_irq(&queue->_xmit_lock);

	return sprintf(buf, "%lu", trans_timeout);
}

static struct netdev_queue_attribute queue_trans_timeout =
	__ATTR(tx_timeout, S_IRUGO, show_trans_timeout, NULL);

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

	if (!strcmp(buf, "max") || !strcmp(buf, "max\n"))
		value = DQL_MAX_LIMIT;
	else {
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
				  struct netdev_queue_attribute *attr,
				  char *buf)
{
	struct dql *dql = &queue->dql;

	return sprintf(buf, "%u\n", jiffies_to_msecs(dql->slack_hold_time));
}

static ssize_t bql_set_hold_time(struct netdev_queue *queue,
				 struct netdev_queue_attribute *attribute,
				 const char *buf, size_t len)
{
	struct dql *dql = &queue->dql;
	unsigned value;
	int err;

	err = kstrtouint(buf, 10, &value);
	if (err < 0)
		return err;

	dql->slack_hold_time = msecs_to_jiffies(value);

	return len;
}

static struct netdev_queue_attribute bql_hold_time_attribute =
	__ATTR(hold_time, S_IRUGO | S_IWUSR, bql_show_hold_time,
	    bql_set_hold_time);

static ssize_t bql_show_inflight(struct netdev_queue *queue,
				 struct netdev_queue_attribute *attr,
				 char *buf)
{
	struct dql *dql = &queue->dql;

	return sprintf(buf, "%u\n", dql->num_queued - dql->num_completed);
}

static struct netdev_queue_attribute bql_inflight_attribute =
	__ATTR(inflight, S_IRUGO, bql_show_inflight, NULL);

#define BQL_ATTR(NAME, FIELD)						\
static ssize_t bql_show_ ## NAME(struct netdev_queue *queue,		\
				 struct netdev_queue_attribute *attr,	\
				 char *buf)				\
{									\
	return bql_show(buf, queue->dql.FIELD);				\
}									\
									\
static ssize_t bql_set_ ## NAME(struct netdev_queue *queue,		\
				struct netdev_queue_attribute *attr,	\
				const char *buf, size_t len)		\
{									\
	return bql_set(buf, len, &queue->dql.FIELD);			\
}									\
									\
static struct netdev_queue_attribute bql_ ## NAME ## _attribute =	\
	__ATTR(NAME, S_IRUGO | S_IWUSR, bql_show_ ## NAME,		\
	    bql_set_ ## NAME);

BQL_ATTR(limit, limit)
BQL_ATTR(limit_max, max_limit)
BQL_ATTR(limit_min, min_limit)

static struct attribute *dql_attrs[] = {
	&bql_limit_attribute.attr,
	&bql_limit_max_attribute.attr,
	&bql_limit_min_attribute.attr,
	&bql_hold_time_attribute.attr,
	&bql_inflight_attribute.attr,
	NULL
};

static struct attribute_group dql_group = {
	.name  = "byte_queue_limits",
	.attrs  = dql_attrs,
};
#endif /* CONFIG_BQL */

#ifdef CONFIG_XPS
static inline unsigned int get_netdev_queue_index(struct netdev_queue *queue)
{
	struct net_device *dev = queue->dev;
	int i;

	for (i = 0; i < dev->num_tx_queues; i++)
		if (queue == &dev->_tx[i])
			break;

	BUG_ON(i >= dev->num_tx_queues);

	return i;
}


static ssize_t show_xps_map(struct netdev_queue *queue,
			    struct netdev_queue_attribute *attribute, char *buf)
{
	struct net_device *dev = queue->dev;
	struct xps_dev_maps *dev_maps;
	cpumask_var_t mask;
	unsigned long index;
	size_t len = 0;
	int i;

	if (!zalloc_cpumask_var(&mask, GFP_KERNEL))
		return -ENOMEM;

	index = get_netdev_queue_index(queue);

	rcu_read_lock();
	dev_maps = rcu_dereference(dev->xps_maps);
	if (dev_maps) {
		for_each_possible_cpu(i) {
			struct xps_map *map =
			    rcu_dereference(dev_maps->cpu_map[i]);
			if (map) {
				int j;
				for (j = 0; j < map->len; j++) {
					if (map->queues[j] == index) {
						cpumask_set_cpu(i, mask);
						break;
					}
				}
			}
		}
	}
	rcu_read_unlock();

	len += cpumask_scnprintf(buf + len, PAGE_SIZE, mask);
	if (PAGE_SIZE - len < 3) {
		free_cpumask_var(mask);
		return -EINVAL;
	}

	free_cpumask_var(mask);
	len += sprintf(buf + len, "\n");
	return len;
}

static DEFINE_MUTEX(xps_map_mutex);
#define xmap_dereference(P)		\
	rcu_dereference_protected((P), lockdep_is_held(&xps_map_mutex))

static void xps_queue_release(struct netdev_queue *queue)
{
	struct net_device *dev = queue->dev;
	struct xps_dev_maps *dev_maps;
	struct xps_map *map;
	unsigned long index;
	int i, pos, nonempty = 0;

	index = get_netdev_queue_index(queue);

	mutex_lock(&xps_map_mutex);
	dev_maps = xmap_dereference(dev->xps_maps);

	if (dev_maps) {
		for_each_possible_cpu(i) {
			map = xmap_dereference(dev_maps->cpu_map[i]);
			if (!map)
				continue;

			for (pos = 0; pos < map->len; pos++)
				if (map->queues[pos] == index)
					break;

			if (pos < map->len) {
				if (map->len > 1)
					map->queues[pos] =
					    map->queues[--map->len];
				else {
					RCU_INIT_POINTER(dev_maps->cpu_map[i],
					    NULL);
					kfree_rcu(map, rcu);
					map = NULL;
				}
			}
			if (map)
				nonempty = 1;
		}

		if (!nonempty) {
			RCU_INIT_POINTER(dev->xps_maps, NULL);
			kfree_rcu(dev_maps, rcu);
		}
	}
	mutex_unlock(&xps_map_mutex);
}

static ssize_t store_xps_map(struct netdev_queue *queue,
		      struct netdev_queue_attribute *attribute,
		      const char *buf, size_t len)
{
	struct net_device *dev = queue->dev;
	cpumask_var_t mask;
	int err, i, cpu, pos, map_len, alloc_len, need_set;
	unsigned long index;
	struct xps_map *map, *new_map;
	struct xps_dev_maps *dev_maps, *new_dev_maps;
	int nonempty = 0;
	int numa_node_id = -2;

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

	new_dev_maps = kzalloc(max_t(unsigned,
	    XPS_DEV_MAPS_SIZE, L1_CACHE_BYTES), GFP_KERNEL);
	if (!new_dev_maps) {
		free_cpumask_var(mask);
		return -ENOMEM;
	}

	mutex_lock(&xps_map_mutex);

	dev_maps = xmap_dereference(dev->xps_maps);

	for_each_possible_cpu(cpu) {
		map = dev_maps ?
			xmap_dereference(dev_maps->cpu_map[cpu]) : NULL;
		new_map = map;
		if (map) {
			for (pos = 0; pos < map->len; pos++)
				if (map->queues[pos] == index)
					break;
			map_len = map->len;
			alloc_len = map->alloc_len;
		} else
			pos = map_len = alloc_len = 0;

		need_set = cpumask_test_cpu(cpu, mask) && cpu_online(cpu);
#ifdef CONFIG_NUMA
		if (need_set) {
			if (numa_node_id == -2)
				numa_node_id = cpu_to_node(cpu);
			else if (numa_node_id != cpu_to_node(cpu))
				numa_node_id = -1;
		}
#endif
		if (need_set && pos >= map_len) {
			/* Need to add queue to this CPU's map */
			if (map_len >= alloc_len) {
				alloc_len = alloc_len ?
				    2 * alloc_len : XPS_MIN_MAP_ALLOC;
				new_map = kzalloc_node(XPS_MAP_SIZE(alloc_len),
						       GFP_KERNEL,
						       cpu_to_node(cpu));
				if (!new_map)
					goto error;
				new_map->alloc_len = alloc_len;
				for (i = 0; i < map_len; i++)
					new_map->queues[i] = map->queues[i];
				new_map->len = map_len;
			}
			new_map->queues[new_map->len++] = index;
		} else if (!need_set && pos < map_len) {
			/* Need to remove queue from this CPU's map */
			if (map_len > 1)
				new_map->queues[pos] =
				    new_map->queues[--new_map->len];
			else
				new_map = NULL;
		}
		RCU_INIT_POINTER(new_dev_maps->cpu_map[cpu], new_map);
	}

	/* Cleanup old maps */
	for_each_possible_cpu(cpu) {
		map = dev_maps ?
			xmap_dereference(dev_maps->cpu_map[cpu]) : NULL;
		if (map && xmap_dereference(new_dev_maps->cpu_map[cpu]) != map)
			kfree_rcu(map, rcu);
		if (new_dev_maps->cpu_map[cpu])
			nonempty = 1;
	}

	if (nonempty) {
		rcu_assign_pointer(dev->xps_maps, new_dev_maps);
	} else {
		kfree(new_dev_maps);
		RCU_INIT_POINTER(dev->xps_maps, NULL);
	}

	if (dev_maps)
		kfree_rcu(dev_maps, rcu);

	netdev_queue_numa_node_write(queue, (numa_node_id >= 0) ? numa_node_id :
					    NUMA_NO_NODE);

	mutex_unlock(&xps_map_mutex);

	free_cpumask_var(mask);
	return len;

error:
	mutex_unlock(&xps_map_mutex);

	if (new_dev_maps)
		for_each_possible_cpu(i)
			kfree(rcu_dereference_protected(
				new_dev_maps->cpu_map[i],
				1));
	kfree(new_dev_maps);
	free_cpumask_var(mask);
	return -ENOMEM;
}

static struct netdev_queue_attribute xps_cpus_attribute =
    __ATTR(xps_cpus, S_IRUGO | S_IWUSR, show_xps_map, store_xps_map);
#endif /* CONFIG_XPS */

static struct attribute *netdev_queue_default_attrs[] = {
	&queue_trans_timeout.attr,
#ifdef CONFIG_XPS
	&xps_cpus_attribute.attr,
#endif
	NULL
};

static void netdev_queue_release(struct kobject *kobj)
{
	struct netdev_queue *queue = to_netdev_queue(kobj);

#ifdef CONFIG_XPS
	xps_queue_release(queue);
#endif

	memset(kobj, 0, sizeof(*kobj));
	dev_put(queue->dev);
}

static struct kobj_type netdev_queue_ktype = {
	.sysfs_ops = &netdev_queue_sysfs_ops,
	.release = netdev_queue_release,
	.default_attrs = netdev_queue_default_attrs,
};

static int netdev_queue_add_kobject(struct net_device *net, int index)
{
	struct netdev_queue *queue = net->_tx + index;
	struct kobject *kobj = &queue->kobj;
	int error = 0;

	kobj->kset = net->queues_kset;
	error = kobject_init_and_add(kobj, &netdev_queue_ktype, NULL,
	    "tx-%u", index);
	if (error)
		goto exit;

#ifdef CONFIG_BQL
	error = sysfs_create_group(kobj, &dql_group);
	if (error)
		goto exit;
#endif

	kobject_uevent(kobj, KOBJ_ADD);
	dev_hold(queue->dev);

	return 0;
exit:
	kobject_put(kobj);
	return error;
}
#endif /* CONFIG_SYSFS */

int
netdev_queue_update_kobjects(struct net_device *net, int old_num, int new_num)
{
#ifdef CONFIG_SYSFS
	int i;
	int error = 0;

	for (i = old_num; i < new_num; i++) {
		error = netdev_queue_add_kobject(net, i);
		if (error) {
			new_num = old_num;
			break;
		}
	}

	while (--i >= new_num) {
		struct netdev_queue *queue = net->_tx + i;

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

static int register_queue_kobjects(struct net_device *net)
{
	int error = 0, txq = 0, rxq = 0, real_rx = 0, real_tx = 0;

#ifdef CONFIG_SYSFS
	net->queues_kset = kset_create_and_add("queues",
	    NULL, &net->dev.kobj);
	if (!net->queues_kset)
		return -ENOMEM;
#endif

#ifdef CONFIG_RPS
	real_rx = net->real_num_rx_queues;
#endif
	real_tx = net->real_num_tx_queues;

	error = net_rx_queue_update_kobjects(net, 0, real_rx);
	if (error)
		goto error;
	rxq = real_rx;

	error = netdev_queue_update_kobjects(net, 0, real_tx);
	if (error)
		goto error;
	txq = real_tx;

	return 0;

error:
	netdev_queue_update_kobjects(net, txq, 0);
	net_rx_queue_update_kobjects(net, rxq, 0);
	return error;
}

static void remove_queue_kobjects(struct net_device *net)
{
	int real_rx = 0, real_tx = 0;

#ifdef CONFIG_RPS
	real_rx = net->real_num_rx_queues;
#endif
	real_tx = net->real_num_tx_queues;

	net_rx_queue_update_kobjects(net, real_rx, 0);
	netdev_queue_update_kobjects(net, real_tx, 0);
#ifdef CONFIG_SYSFS
	kset_unregister(net->queues_kset);
#endif
}

static void *net_grab_current_ns(void)
{
	struct net *ns = current->nsproxy->net_ns;
#ifdef CONFIG_NET_NS
	if (ns)
		atomic_inc(&ns->passive);
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

struct kobj_ns_type_operations net_ns_type_operations = {
	.type = KOBJ_NS_TYPE_NET,
	.grab_current_ns = net_grab_current_ns,
	.netlink_ns = net_netlink_ns,
	.initial_ns = net_initial_ns,
	.drop_ns = net_drop_ns,
};
EXPORT_SYMBOL_GPL(net_ns_type_operations);

#ifdef CONFIG_HOTPLUG
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
	 * and is what RtNetlink uses natively. */
	retval = add_uevent_var(env, "IFINDEX=%d", dev->ifindex);

exit:
	return retval;
}
#endif

/*
 *	netdev_release -- destroy and free a dead device.
 *	Called when last reference to device kobject is gone.
 */
static void netdev_release(struct device *d)
{
	struct net_device *dev = to_net_dev(d);

	BUG_ON(dev->reg_state != NETREG_RELEASED);

	kfree(dev->ifalias);
	kfree((char *)dev - dev->padded);
}

static const void *net_namespace(struct device *d)
{
	struct net_device *dev;
	dev = container_of(d, struct net_device, dev);
	return dev_net(dev);
}

static struct class net_class = {
	.name = "net",
	.dev_release = netdev_release,
#ifdef CONFIG_SYSFS
	.dev_attrs = net_class_attributes,
#endif /* CONFIG_SYSFS */
#ifdef CONFIG_HOTPLUG
	.dev_uevent = netdev_uevent,
#endif
	.ns_type = &net_ns_type_operations,
	.namespace = net_namespace,
};

/* Delete sysfs entries but hold kobject reference until after all
 * netdev references are gone.
 */
void netdev_unregister_kobject(struct net_device * net)
{
	struct device *dev = &(net->dev);

	kobject_get(&dev->kobj);

	remove_queue_kobjects(net);

	device_del(dev);
}

/* Create sysfs entries for network device. */
int netdev_register_kobject(struct net_device *net)
{
	struct device *dev = &(net->dev);
	const struct attribute_group **groups = net->sysfs_groups;
	int error = 0;

	device_initialize(dev);
	dev->class = &net_class;
	dev->platform_data = net;
	dev->groups = groups;

	dev_set_name(dev, "%s", net->name);

#ifdef CONFIG_SYSFS
	/* Allow for a device specific group */
	if (*groups)
		groups++;

	*groups++ = &netstat_group;
#ifdef CONFIG_WIRELESS_EXT_SYSFS
	if (net->ieee80211_ptr)
		*groups++ = &wireless_group;
#ifdef CONFIG_WIRELESS_EXT
	else if (net->wireless_handlers)
		*groups++ = &wireless_group;
#endif
#endif
#endif /* CONFIG_SYSFS */

	error = device_add(dev);
	if (error)
		return error;

	error = register_queue_kobjects(net);
	if (error) {
		device_del(dev);
		return error;
	}

	return error;
}

int netdev_class_create_file(struct class_attribute *class_attr)
{
	return class_create_file(&net_class, class_attr);
}
EXPORT_SYMBOL(netdev_class_create_file);

void netdev_class_remove_file(struct class_attribute *class_attr)
{
	class_remove_file(&net_class, class_attr);
}
EXPORT_SYMBOL(netdev_class_remove_file);

int netdev_kobject_init(void)
{
	kobj_ns_type_register(&net_ns_type_operations);
	return class_register(&net_class);
}
