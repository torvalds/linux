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
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#define to_class_dev(obj) container_of(obj,struct class_device,kobj)
#define to_net_dev(class) container_of(class, struct net_device, class_dev)

static const char fmt_hex[] = "%#x\n";
static const char fmt_long_hex[] = "%#lx\n";
static const char fmt_dec[] = "%d\n";
static const char fmt_ulong[] = "%lu\n";

static inline int dev_isalive(const struct net_device *dev) 
{
	return dev->reg_state <= NETREG_REGISTERED;
}

/* use same locking rules as GIF* ioctl's */
static ssize_t netdev_show(const struct class_device *cd, char *buf,
			   ssize_t (*format)(const struct net_device *, char *))
{
	struct net_device *net = to_net_dev(cd);
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
static ssize_t show_##field(struct class_device *cd, char *buf)		\
{									\
	return netdev_show(cd, buf, format_##field);			\
}


/* use same locking and permission rules as SIF* ioctl's */
static ssize_t netdev_store(struct class_device *dev,
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

	rtnl_lock();
	if (dev_isalive(net)) {
		if ((ret = (*set)(net, new)) == 0)
			ret = len;
	}
	rtnl_unlock();
 err:
	return ret;
}

NETDEVICE_SHOW(addr_len, fmt_dec);
NETDEVICE_SHOW(iflink, fmt_dec);
NETDEVICE_SHOW(ifindex, fmt_dec);
NETDEVICE_SHOW(features, fmt_long_hex);
NETDEVICE_SHOW(type, fmt_dec);
NETDEVICE_SHOW(link_mode, fmt_dec);

/* use same locking rules as GIFHWADDR ioctl's */
static ssize_t format_addr(char *buf, const unsigned char *addr, int len)
{
	int i;
	char *cp = buf;

	for (i = 0; i < len; i++)
		cp += sprintf(cp, "%02x%c", addr[i],
			      i == (len - 1) ? '\n' : ':');
	return cp - buf;
}

static ssize_t show_address(struct class_device *dev, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	ssize_t ret = -EINVAL;

	read_lock(&dev_base_lock);
	if (dev_isalive(net))
	    ret = format_addr(buf, net->dev_addr, net->addr_len);
	read_unlock(&dev_base_lock);
	return ret;
}

static ssize_t show_broadcast(struct class_device *dev, char *buf)
{
	struct net_device *net = to_net_dev(dev);
	if (dev_isalive(net))
		return format_addr(buf, net->broadcast, net->addr_len);
	return -EINVAL;
}

static ssize_t show_carrier(struct class_device *dev, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	if (netif_running(netdev)) {
		return sprintf(buf, fmt_dec, !!netif_carrier_ok(netdev));
	}
	return -EINVAL;
}

static ssize_t show_dormant(struct class_device *dev, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);

	if (netif_running(netdev))
		return sprintf(buf, fmt_dec, !!netif_dormant(netdev));

	return -EINVAL;
}

static const char *operstates[] = {
	"unknown",
	"notpresent", /* currently unused */
	"down",
	"lowerlayerdown",
	"testing", /* currently unused */
	"dormant",
	"up"
};

static ssize_t show_operstate(struct class_device *dev, char *buf)
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

static ssize_t store_mtu(struct class_device *dev, const char *buf, size_t len)
{
	return netdev_store(dev, buf, len, change_mtu);
}

NETDEVICE_SHOW(flags, fmt_hex);

static int change_flags(struct net_device *net, unsigned long new_flags)
{
	return dev_change_flags(net, (unsigned) new_flags);
}

static ssize_t store_flags(struct class_device *dev, const char *buf, size_t len)
{
	return netdev_store(dev, buf, len, change_flags);
}

NETDEVICE_SHOW(tx_queue_len, fmt_ulong);

static int change_tx_queue_len(struct net_device *net, unsigned long new_len)
{
	net->tx_queue_len = new_len;
	return 0;
}

static ssize_t store_tx_queue_len(struct class_device *dev, const char *buf, size_t len)
{
	return netdev_store(dev, buf, len, change_tx_queue_len);
}

NETDEVICE_SHOW(weight, fmt_dec);

static int change_weight(struct net_device *net, unsigned long new_weight)
{
	net->weight = new_weight;
	return 0;
}

static ssize_t store_weight(struct class_device *dev, const char *buf, size_t len)
{
	return netdev_store(dev, buf, len, change_weight);
}

static struct class_device_attribute net_class_attributes[] = {
	__ATTR(addr_len, S_IRUGO, show_addr_len, NULL),
	__ATTR(iflink, S_IRUGO, show_iflink, NULL),
	__ATTR(ifindex, S_IRUGO, show_ifindex, NULL),
	__ATTR(features, S_IRUGO, show_features, NULL),
	__ATTR(type, S_IRUGO, show_type, NULL),
	__ATTR(link_mode, S_IRUGO, show_link_mode, NULL),
	__ATTR(address, S_IRUGO, show_address, NULL),
	__ATTR(broadcast, S_IRUGO, show_broadcast, NULL),
	__ATTR(carrier, S_IRUGO, show_carrier, NULL),
	__ATTR(dormant, S_IRUGO, show_dormant, NULL),
	__ATTR(operstate, S_IRUGO, show_operstate, NULL),
	__ATTR(mtu, S_IRUGO | S_IWUSR, show_mtu, store_mtu),
	__ATTR(flags, S_IRUGO | S_IWUSR, show_flags, store_flags),
	__ATTR(tx_queue_len, S_IRUGO | S_IWUSR, show_tx_queue_len,
	       store_tx_queue_len),
	__ATTR(weight, S_IRUGO | S_IWUSR, show_weight, store_weight),
	{}
};

/* Show a given an attribute in the statistics group */
static ssize_t netstat_show(const struct class_device *cd, char *buf, 
			    unsigned long offset)
{
	struct net_device *dev = to_net_dev(cd);
	struct net_device_stats *stats;
	ssize_t ret = -EINVAL;

	if (offset > sizeof(struct net_device_stats) ||
	    offset % sizeof(unsigned long) != 0)
		WARN_ON(1);

	read_lock(&dev_base_lock);
	if (dev_isalive(dev) && dev->get_stats &&
	    (stats = (*dev->get_stats)(dev))) 
		ret = sprintf(buf, fmt_ulong,
			      *(unsigned long *)(((u8 *) stats) + offset));

	read_unlock(&dev_base_lock);
	return ret;
}

/* generate a read-only statistics attribute */
#define NETSTAT_ENTRY(name)						\
static ssize_t show_##name(struct class_device *cd, char *buf) 		\
{									\
	return netstat_show(cd, buf, 					\
			    offsetof(struct net_device_stats, name));	\
}									\
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_##name, NULL)

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
	&class_device_attr_rx_packets.attr,
	&class_device_attr_tx_packets.attr,
	&class_device_attr_rx_bytes.attr,
	&class_device_attr_tx_bytes.attr,
	&class_device_attr_rx_errors.attr,
	&class_device_attr_tx_errors.attr,
	&class_device_attr_rx_dropped.attr,
	&class_device_attr_tx_dropped.attr,
	&class_device_attr_multicast.attr,
	&class_device_attr_collisions.attr,
	&class_device_attr_rx_length_errors.attr,
	&class_device_attr_rx_over_errors.attr,
	&class_device_attr_rx_crc_errors.attr,
	&class_device_attr_rx_frame_errors.attr,
	&class_device_attr_rx_fifo_errors.attr,
	&class_device_attr_rx_missed_errors.attr,
	&class_device_attr_tx_aborted_errors.attr,
	&class_device_attr_tx_carrier_errors.attr,
	&class_device_attr_tx_fifo_errors.attr,
	&class_device_attr_tx_heartbeat_errors.attr,
	&class_device_attr_tx_window_errors.attr,
	&class_device_attr_rx_compressed.attr,
	&class_device_attr_tx_compressed.attr,
	NULL
};


static struct attribute_group netstat_group = {
	.name  = "statistics",
	.attrs  = netstat_attrs,
};

#ifdef WIRELESS_EXT
/* helper function that does all the locking etc for wireless stats */
static ssize_t wireless_show(struct class_device *cd, char *buf,
			     ssize_t (*format)(const struct iw_statistics *,
					       char *))
{
	struct net_device *dev = to_net_dev(cd);
	const struct iw_statistics *iw = NULL;
	ssize_t ret = -EINVAL;
	
	read_lock(&dev_base_lock);
	if (dev_isalive(dev)) {
		if(dev->wireless_handlers &&
		   dev->wireless_handlers->get_wireless_stats)
			iw = dev->wireless_handlers->get_wireless_stats(dev);
		else if (dev->get_wireless_stats)
			iw = dev->get_wireless_stats(dev);
		if (iw != NULL)
			ret = (*format)(iw, buf);
	}
	read_unlock(&dev_base_lock);

	return ret;
}

/* show function template for wireless fields */
#define WIRELESS_SHOW(name, field, format_string)			\
static ssize_t format_iw_##name(const struct iw_statistics *iw, char *buf) \
{									\
	return sprintf(buf, format_string, iw->field);			\
}									\
static ssize_t show_iw_##name(struct class_device *cd, char *buf)	\
{									\
	return wireless_show(cd, buf, format_iw_##name);		\
}									\
static CLASS_DEVICE_ATTR(name, S_IRUGO, show_iw_##name, NULL)

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
	&class_device_attr_status.attr,
	&class_device_attr_link.attr,
	&class_device_attr_level.attr,
	&class_device_attr_noise.attr,
	&class_device_attr_nwid.attr,
	&class_device_attr_crypt.attr,
	&class_device_attr_fragment.attr,
	&class_device_attr_retries.attr,
	&class_device_attr_misc.attr,
	&class_device_attr_beacon.attr,
	NULL
};

static struct attribute_group wireless_group = {
	.name = "wireless",
	.attrs = wireless_attrs,
};
#endif

#ifdef CONFIG_HOTPLUG
static int netdev_uevent(struct class_device *cd, char **envp,
			 int num_envp, char *buf, int size)
{
	struct net_device *dev = to_net_dev(cd);
	int i = 0;
	int n;

	/* pass interface to uevent. */
	envp[i++] = buf;
	n = snprintf(buf, size, "INTERFACE=%s", dev->name) + 1;
	buf += n;
	size -= n;

	if ((size <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = NULL;
	return 0;
}
#endif

/*
 *	netdev_release -- destroy and free a dead device. 
 *	Called when last reference to class_device kobject is gone.
 */
static void netdev_release(struct class_device *cd)
{
	struct net_device *dev 
		= container_of(cd, struct net_device, class_dev);

	BUG_ON(dev->reg_state != NETREG_RELEASED);

	kfree((char *)dev - dev->padded);
}

static struct class net_class = {
	.name = "net",
	.release = netdev_release,
	.class_dev_attrs = net_class_attributes,
#ifdef CONFIG_HOTPLUG
	.uevent = netdev_uevent,
#endif
};

void netdev_unregister_sysfs(struct net_device * net)
{
	class_device_del(&(net->class_dev));
}

/* Create sysfs entries for network device. */
int netdev_register_sysfs(struct net_device *net)
{
	struct class_device *class_dev = &(net->class_dev);
	struct attribute_group **groups = net->sysfs_groups;

	class_device_initialize(class_dev);
	class_dev->class = &net_class;
	class_dev->class_data = net;
	class_dev->groups = groups;

	BUILD_BUG_ON(BUS_ID_SIZE < IFNAMSIZ);
	strlcpy(class_dev->class_id, net->name, BUS_ID_SIZE);

	if (net->get_stats)
		*groups++ = &netstat_group;

#ifdef WIRELESS_EXT
	if (net->get_wireless_stats
	    || (net->wireless_handlers && net->wireless_handlers->get_wireless_stats))
		*groups++ = &wireless_group;
#endif

	return class_device_add(class_dev);
}

int netdev_sysfs_init(void)
{
	return class_register(&net_class);
}
