/* Copyright (C) 2010-2016  B.A.T.M.A.N. contributors:
 *
 * Marek Lindner
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "sysfs.h"
#include "main.h"

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/if.h>
#include <linux/if_vlan.h>
#include <linux/kref.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/printk.h>
#include <linux/rculist.h>
#include <linux/rcupdate.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/stringify.h>

#include "distributed-arp-table.h"
#include "gateway_client.h"
#include "gateway_common.h"
#include "bridge_loop_avoidance.h"
#include "hard-interface.h"
#include "network-coding.h"
#include "packet.h"
#include "soft-interface.h"

static struct net_device *batadv_kobj_to_netdev(struct kobject *obj)
{
	struct device *dev = container_of(obj->parent, struct device, kobj);

	return to_net_dev(dev);
}

static struct batadv_priv *batadv_kobj_to_batpriv(struct kobject *obj)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(obj);

	return netdev_priv(net_dev);
}

/**
 * batadv_vlan_kobj_to_batpriv - convert a vlan kobj in the associated batpriv
 * @obj: kobject to covert
 *
 * Return: the associated batadv_priv struct.
 */
static struct batadv_priv *batadv_vlan_kobj_to_batpriv(struct kobject *obj)
{
	/* VLAN specific attributes are located in the root sysfs folder if they
	 * refer to the untagged VLAN..
	 */
	if (!strcmp(BATADV_SYSFS_IF_MESH_SUBDIR, obj->name))
		return batadv_kobj_to_batpriv(obj);

	/* ..while the attributes for the tagged vlans are located in
	 * the in the corresponding "vlan%VID" subfolder
	 */
	return batadv_kobj_to_batpriv(obj->parent);
}

/**
 * batadv_kobj_to_vlan - convert a kobj in the associated softif_vlan struct
 * @bat_priv: the bat priv with all the soft interface information
 * @obj: kobject to covert
 *
 * Return: the associated softif_vlan struct if found, NULL otherwise.
 */
static struct batadv_softif_vlan *
batadv_kobj_to_vlan(struct batadv_priv *bat_priv, struct kobject *obj)
{
	struct batadv_softif_vlan *vlan_tmp, *vlan = NULL;

	rcu_read_lock();
	hlist_for_each_entry_rcu(vlan_tmp, &bat_priv->softif_vlan_list, list) {
		if (vlan_tmp->kobj != obj)
			continue;

		if (!kref_get_unless_zero(&vlan_tmp->refcount))
			continue;

		vlan = vlan_tmp;
		break;
	}
	rcu_read_unlock();

	return vlan;
}

#define BATADV_UEV_TYPE_VAR	"BATTYPE="
#define BATADV_UEV_ACTION_VAR	"BATACTION="
#define BATADV_UEV_DATA_VAR	"BATDATA="

static char *batadv_uev_action_str[] = {
	"add",
	"del",
	"change"
};

static char *batadv_uev_type_str[] = {
	"gw"
};

/* Use this, if you have customized show and store functions for vlan attrs */
#define BATADV_ATTR_VLAN(_name, _mode, _show, _store)	\
struct batadv_attribute batadv_attr_vlan_##_name = {	\
	.attr = {.name = __stringify(_name),		\
		 .mode = _mode },			\
	.show   = _show,				\
	.store  = _store,				\
}

/* Use this, if you have customized show and store functions */
#define BATADV_ATTR(_name, _mode, _show, _store)	\
struct batadv_attribute batadv_attr_##_name = {		\
	.attr = {.name = __stringify(_name),		\
		 .mode = _mode },			\
	.show   = _show,				\
	.store  = _store,				\
}

#define BATADV_ATTR_SIF_STORE_BOOL(_name, _post_func)			\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct batadv_priv *bat_priv = netdev_priv(net_dev);		\
									\
	return __batadv_store_bool_attr(buff, count, _post_func, attr,	\
					&bat_priv->_name, net_dev);	\
}

#define BATADV_ATTR_SIF_SHOW_BOOL(_name)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);	\
									\
	return sprintf(buff, "%s\n",					\
		       atomic_read(&bat_priv->_name) == 0 ?		\
		       "disabled" : "enabled");				\
}									\

/* Use this, if you are going to turn a [name] in the soft-interface
 * (bat_priv) on or off
 */
#define BATADV_ATTR_SIF_BOOL(_name, _mode, _post_func)			\
	static BATADV_ATTR_SIF_STORE_BOOL(_name, _post_func)		\
	static BATADV_ATTR_SIF_SHOW_BOOL(_name)				\
	static BATADV_ATTR(_name, _mode, batadv_show_##_name,		\
			   batadv_store_##_name)

#define BATADV_ATTR_SIF_STORE_UINT(_name, _var, _min, _max, _post_func)	\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct batadv_priv *bat_priv = netdev_priv(net_dev);		\
									\
	return __batadv_store_uint_attr(buff, count, _min, _max,	\
					_post_func, attr,		\
					&bat_priv->_var, net_dev);	\
}

#define BATADV_ATTR_SIF_SHOW_UINT(_name, _var)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);	\
									\
	return sprintf(buff, "%i\n", atomic_read(&bat_priv->_var));	\
}									\

/* Use this, if you are going to set [name] in the soft-interface
 * (bat_priv) to an unsigned integer value
 */
#define BATADV_ATTR_SIF_UINT(_name, _var, _mode, _min, _max, _post_func)\
	static BATADV_ATTR_SIF_STORE_UINT(_name, _var, _min, _max, _post_func)\
	static BATADV_ATTR_SIF_SHOW_UINT(_name, _var)			\
	static BATADV_ATTR(_name, _mode, batadv_show_##_name,		\
			   batadv_store_##_name)

#define BATADV_ATTR_VLAN_STORE_BOOL(_name, _post_func)			\
ssize_t batadv_store_vlan_##_name(struct kobject *kobj,			\
				  struct attribute *attr, char *buff,	\
				  size_t count)				\
{									\
	struct batadv_priv *bat_priv = batadv_vlan_kobj_to_batpriv(kobj);\
	struct batadv_softif_vlan *vlan = batadv_kobj_to_vlan(bat_priv,	\
							      kobj);	\
	size_t res = __batadv_store_bool_attr(buff, count, _post_func,	\
					      attr, &vlan->_name,	\
					      bat_priv->soft_iface);	\
									\
	batadv_softif_vlan_put(vlan);					\
	return res;							\
}

#define BATADV_ATTR_VLAN_SHOW_BOOL(_name)				\
ssize_t batadv_show_vlan_##_name(struct kobject *kobj,			\
				 struct attribute *attr, char *buff)	\
{									\
	struct batadv_priv *bat_priv = batadv_vlan_kobj_to_batpriv(kobj);\
	struct batadv_softif_vlan *vlan = batadv_kobj_to_vlan(bat_priv,	\
							      kobj);	\
	size_t res = sprintf(buff, "%s\n",				\
			     atomic_read(&vlan->_name) == 0 ?		\
			     "disabled" : "enabled");			\
									\
	batadv_softif_vlan_put(vlan);					\
	return res;							\
}

/* Use this, if you are going to turn a [name] in the vlan struct on or off */
#define BATADV_ATTR_VLAN_BOOL(_name, _mode, _post_func)			\
	static BATADV_ATTR_VLAN_STORE_BOOL(_name, _post_func)		\
	static BATADV_ATTR_VLAN_SHOW_BOOL(_name)			\
	static BATADV_ATTR_VLAN(_name, _mode, batadv_show_vlan_##_name,	\
				batadv_store_vlan_##_name)

#define BATADV_ATTR_HIF_STORE_UINT(_name, _var, _min, _max, _post_func)	\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct batadv_hard_iface *hard_iface;				\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = __batadv_store_uint_attr(buff, count, _min, _max,	\
					  _post_func, attr,		\
					  &hard_iface->_var, net_dev);	\
									\
	batadv_hardif_put(hard_iface);				\
	return length;							\
}

#define BATADV_ATTR_HIF_SHOW_UINT(_name, _var)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct batadv_hard_iface *hard_iface;				\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = sprintf(buff, "%i\n", atomic_read(&hard_iface->_var));	\
									\
	batadv_hardif_put(hard_iface);				\
	return length;							\
}

/* Use this, if you are going to set [name] in hard_iface to an
 * unsigned integer value
 */
#define BATADV_ATTR_HIF_UINT(_name, _var, _mode, _min, _max, _post_func)\
	static BATADV_ATTR_HIF_STORE_UINT(_name, _var, _min,		\
					  _max, _post_func)		\
	static BATADV_ATTR_HIF_SHOW_UINT(_name, _var)			\
	static BATADV_ATTR(_name, _mode, batadv_show_##_name,		\
			   batadv_store_##_name)

static int batadv_store_bool_attr(char *buff, size_t count,
				  struct net_device *net_dev,
				  const char *attr_name, atomic_t *attr,
				  bool *changed)
{
	int enabled = -1;

	*changed = false;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if ((strncmp(buff, "1", 2) == 0) ||
	    (strncmp(buff, "enable", 7) == 0) ||
	    (strncmp(buff, "enabled", 8) == 0))
		enabled = 1;

	if ((strncmp(buff, "0", 2) == 0) ||
	    (strncmp(buff, "disable", 8) == 0) ||
	    (strncmp(buff, "disabled", 9) == 0))
		enabled = 0;

	if (enabled < 0) {
		batadv_info(net_dev, "%s: Invalid parameter received: %s\n",
			    attr_name, buff);
		return -EINVAL;
	}

	if (atomic_read(attr) == enabled)
		return count;

	batadv_info(net_dev, "%s: Changing from: %s to: %s\n", attr_name,
		    atomic_read(attr) == 1 ? "enabled" : "disabled",
		    enabled == 1 ? "enabled" : "disabled");

	*changed = true;

	atomic_set(attr, (unsigned int)enabled);
	return count;
}

static inline ssize_t
__batadv_store_bool_attr(char *buff, size_t count,
			 void (*post_func)(struct net_device *),
			 struct attribute *attr,
			 atomic_t *attr_store, struct net_device *net_dev)
{
	bool changed;
	int ret;

	ret = batadv_store_bool_attr(buff, count, net_dev, attr->name,
				     attr_store, &changed);
	if (post_func && changed)
		post_func(net_dev);

	return ret;
}

static int batadv_store_uint_attr(const char *buff, size_t count,
				  struct net_device *net_dev,
				  const char *attr_name,
				  unsigned int min, unsigned int max,
				  atomic_t *attr)
{
	unsigned long uint_val;
	int ret;

	ret = kstrtoul(buff, 10, &uint_val);
	if (ret) {
		batadv_info(net_dev, "%s: Invalid parameter received: %s\n",
			    attr_name, buff);
		return -EINVAL;
	}

	if (uint_val < min) {
		batadv_info(net_dev, "%s: Value is too small: %lu min: %u\n",
			    attr_name, uint_val, min);
		return -EINVAL;
	}

	if (uint_val > max) {
		batadv_info(net_dev, "%s: Value is too big: %lu max: %u\n",
			    attr_name, uint_val, max);
		return -EINVAL;
	}

	if (atomic_read(attr) == uint_val)
		return count;

	batadv_info(net_dev, "%s: Changing from: %i to: %lu\n",
		    attr_name, atomic_read(attr), uint_val);

	atomic_set(attr, uint_val);
	return count;
}

static inline ssize_t
__batadv_store_uint_attr(const char *buff, size_t count,
			 int min, int max,
			 void (*post_func)(struct net_device *),
			 const struct attribute *attr,
			 atomic_t *attr_store, struct net_device *net_dev)
{
	int ret;

	ret = batadv_store_uint_attr(buff, count, net_dev, attr->name, min, max,
				     attr_store);
	if (post_func && ret)
		post_func(net_dev);

	return ret;
}

static ssize_t batadv_show_bat_algo(struct kobject *kobj,
				    struct attribute *attr, char *buff)
{
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);

	return sprintf(buff, "%s\n", bat_priv->bat_algo_ops->name);
}

static void batadv_post_gw_reselect(struct net_device *net_dev)
{
	struct batadv_priv *bat_priv = netdev_priv(net_dev);

	batadv_gw_reselect(bat_priv);
}

static ssize_t batadv_show_gw_mode(struct kobject *kobj, struct attribute *attr,
				   char *buff)
{
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
	int bytes_written;

	switch (atomic_read(&bat_priv->gw_mode)) {
	case BATADV_GW_MODE_CLIENT:
		bytes_written = sprintf(buff, "%s\n",
					BATADV_GW_MODE_CLIENT_NAME);
		break;
	case BATADV_GW_MODE_SERVER:
		bytes_written = sprintf(buff, "%s\n",
					BATADV_GW_MODE_SERVER_NAME);
		break;
	default:
		bytes_written = sprintf(buff, "%s\n",
					BATADV_GW_MODE_OFF_NAME);
		break;
	}

	return bytes_written;
}

static ssize_t batadv_store_gw_mode(struct kobject *kobj,
				    struct attribute *attr, char *buff,
				    size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	char *curr_gw_mode_str;
	int gw_mode_tmp = -1;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if (strncmp(buff, BATADV_GW_MODE_OFF_NAME,
		    strlen(BATADV_GW_MODE_OFF_NAME)) == 0)
		gw_mode_tmp = BATADV_GW_MODE_OFF;

	if (strncmp(buff, BATADV_GW_MODE_CLIENT_NAME,
		    strlen(BATADV_GW_MODE_CLIENT_NAME)) == 0)
		gw_mode_tmp = BATADV_GW_MODE_CLIENT;

	if (strncmp(buff, BATADV_GW_MODE_SERVER_NAME,
		    strlen(BATADV_GW_MODE_SERVER_NAME)) == 0)
		gw_mode_tmp = BATADV_GW_MODE_SERVER;

	if (gw_mode_tmp < 0) {
		batadv_info(net_dev,
			    "Invalid parameter for 'gw mode' setting received: %s\n",
			    buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->gw_mode) == gw_mode_tmp)
		return count;

	switch (atomic_read(&bat_priv->gw_mode)) {
	case BATADV_GW_MODE_CLIENT:
		curr_gw_mode_str = BATADV_GW_MODE_CLIENT_NAME;
		break;
	case BATADV_GW_MODE_SERVER:
		curr_gw_mode_str = BATADV_GW_MODE_SERVER_NAME;
		break;
	default:
		curr_gw_mode_str = BATADV_GW_MODE_OFF_NAME;
		break;
	}

	batadv_info(net_dev, "Changing gw mode from: %s to: %s\n",
		    curr_gw_mode_str, buff);

	/* Invoking batadv_gw_reselect() is not enough to really de-select the
	 * current GW. It will only instruct the gateway client code to perform
	 * a re-election the next time that this is needed.
	 *
	 * When gw client mode is being switched off the current GW must be
	 * de-selected explicitly otherwise no GW_ADD uevent is thrown on
	 * client mode re-activation. This is operation is performed in
	 * batadv_gw_check_client_stop().
	 */
	batadv_gw_reselect(bat_priv);
	/* always call batadv_gw_check_client_stop() before changing the gateway
	 * state
	 */
	batadv_gw_check_client_stop(bat_priv);
	atomic_set(&bat_priv->gw_mode, (unsigned int)gw_mode_tmp);
	batadv_gw_tvlv_container_update(bat_priv);
	return count;
}

static ssize_t batadv_show_gw_bwidth(struct kobject *kobj,
				     struct attribute *attr, char *buff)
{
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
	u32 down, up;

	down = atomic_read(&bat_priv->gw.bandwidth_down);
	up = atomic_read(&bat_priv->gw.bandwidth_up);

	return sprintf(buff, "%u.%u/%u.%u MBit\n", down / 10,
		       down % 10, up / 10, up % 10);
}

static ssize_t batadv_store_gw_bwidth(struct kobject *kobj,
				      struct attribute *attr, char *buff,
				      size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	return batadv_gw_bandwidth_set(net_dev, buff, count);
}

/**
 * batadv_show_isolation_mark - print the current isolation mark/mask
 * @kobj: kobject representing the private mesh sysfs directory
 * @attr: the batman-adv attribute the user is interacting with
 * @buff: the buffer that will contain the data to send back to the user
 *
 * Return: the number of bytes written into 'buff' on success or a negative
 * error code in case of failure
 */
static ssize_t batadv_show_isolation_mark(struct kobject *kobj,
					  struct attribute *attr, char *buff)
{
	struct batadv_priv *bat_priv = batadv_kobj_to_batpriv(kobj);

	return sprintf(buff, "%#.8x/%#.8x\n", bat_priv->isolation_mark,
		       bat_priv->isolation_mark_mask);
}

/**
 * batadv_store_isolation_mark - parse and store the isolation mark/mask entered
 *  by the user
 * @kobj: kobject representing the private mesh sysfs directory
 * @attr: the batman-adv attribute the user is interacting with
 * @buff: the buffer containing the user data
 * @count: number of bytes in the buffer
 *
 * Return: 'count' on success or a negative error code in case of failure
 */
static ssize_t batadv_store_isolation_mark(struct kobject *kobj,
					   struct attribute *attr, char *buff,
					   size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_priv *bat_priv = netdev_priv(net_dev);
	u32 mark, mask;
	char *mask_ptr;

	/* parse the mask if it has been specified, otherwise assume the mask is
	 * the biggest possible
	 */
	mask = 0xFFFFFFFF;
	mask_ptr = strchr(buff, '/');
	if (mask_ptr) {
		*mask_ptr = '\0';
		mask_ptr++;

		/* the mask must be entered in hex base as it is going to be a
		 * bitmask and not a prefix length
		 */
		if (kstrtou32(mask_ptr, 16, &mask) < 0)
			return -EINVAL;
	}

	/* the mark can be entered in any base */
	if (kstrtou32(buff, 0, &mark) < 0)
		return -EINVAL;

	bat_priv->isolation_mark_mask = mask;
	/* erase bits not covered by the mask */
	bat_priv->isolation_mark = mark & bat_priv->isolation_mark_mask;

	batadv_info(net_dev,
		    "New skb mark for extended isolation: %#.8x/%#.8x\n",
		    bat_priv->isolation_mark, bat_priv->isolation_mark_mask);

	return count;
}

BATADV_ATTR_SIF_BOOL(aggregated_ogms, S_IRUGO | S_IWUSR, NULL);
BATADV_ATTR_SIF_BOOL(bonding, S_IRUGO | S_IWUSR, NULL);
#ifdef CONFIG_BATMAN_ADV_BLA
BATADV_ATTR_SIF_BOOL(bridge_loop_avoidance, S_IRUGO | S_IWUSR,
		     batadv_bla_status_update);
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
BATADV_ATTR_SIF_BOOL(distributed_arp_table, S_IRUGO | S_IWUSR,
		     batadv_dat_status_update);
#endif
BATADV_ATTR_SIF_BOOL(fragmentation, S_IRUGO | S_IWUSR, batadv_update_min_mtu);
static BATADV_ATTR(routing_algo, S_IRUGO, batadv_show_bat_algo, NULL);
static BATADV_ATTR(gw_mode, S_IRUGO | S_IWUSR, batadv_show_gw_mode,
		   batadv_store_gw_mode);
BATADV_ATTR_SIF_UINT(orig_interval, orig_interval, S_IRUGO | S_IWUSR,
		     2 * BATADV_JITTER, INT_MAX, NULL);
BATADV_ATTR_SIF_UINT(hop_penalty, hop_penalty, S_IRUGO | S_IWUSR, 0,
		     BATADV_TQ_MAX_VALUE, NULL);
BATADV_ATTR_SIF_UINT(gw_sel_class, gw_sel_class, S_IRUGO | S_IWUSR, 1,
		     BATADV_TQ_MAX_VALUE, batadv_post_gw_reselect);
static BATADV_ATTR(gw_bandwidth, S_IRUGO | S_IWUSR, batadv_show_gw_bwidth,
		   batadv_store_gw_bwidth);
#ifdef CONFIG_BATMAN_ADV_MCAST
BATADV_ATTR_SIF_BOOL(multicast_mode, S_IRUGO | S_IWUSR, NULL);
#endif
#ifdef CONFIG_BATMAN_ADV_DEBUG
BATADV_ATTR_SIF_UINT(log_level, log_level, S_IRUGO | S_IWUSR, 0,
		     BATADV_DBG_ALL, NULL);
#endif
#ifdef CONFIG_BATMAN_ADV_NC
BATADV_ATTR_SIF_BOOL(network_coding, S_IRUGO | S_IWUSR,
		     batadv_nc_status_update);
#endif
static BATADV_ATTR(isolation_mark, S_IRUGO | S_IWUSR,
		   batadv_show_isolation_mark, batadv_store_isolation_mark);

static struct batadv_attribute *batadv_mesh_attrs[] = {
	&batadv_attr_aggregated_ogms,
	&batadv_attr_bonding,
#ifdef CONFIG_BATMAN_ADV_BLA
	&batadv_attr_bridge_loop_avoidance,
#endif
#ifdef CONFIG_BATMAN_ADV_DAT
	&batadv_attr_distributed_arp_table,
#endif
#ifdef CONFIG_BATMAN_ADV_MCAST
	&batadv_attr_multicast_mode,
#endif
	&batadv_attr_fragmentation,
	&batadv_attr_routing_algo,
	&batadv_attr_gw_mode,
	&batadv_attr_orig_interval,
	&batadv_attr_hop_penalty,
	&batadv_attr_gw_sel_class,
	&batadv_attr_gw_bandwidth,
#ifdef CONFIG_BATMAN_ADV_DEBUG
	&batadv_attr_log_level,
#endif
#ifdef CONFIG_BATMAN_ADV_NC
	&batadv_attr_network_coding,
#endif
	&batadv_attr_isolation_mark,
	NULL,
};

BATADV_ATTR_VLAN_BOOL(ap_isolation, S_IRUGO | S_IWUSR, NULL);

/* array of vlan specific sysfs attributes */
static struct batadv_attribute *batadv_vlan_attrs[] = {
	&batadv_attr_vlan_ap_isolation,
	NULL,
};

int batadv_sysfs_add_meshif(struct net_device *dev)
{
	struct kobject *batif_kobject = &dev->dev.kobj;
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct batadv_attribute **bat_attr;
	int err;

	bat_priv->mesh_obj = kobject_create_and_add(BATADV_SYSFS_IF_MESH_SUBDIR,
						    batif_kobject);
	if (!bat_priv->mesh_obj) {
		batadv_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			   BATADV_SYSFS_IF_MESH_SUBDIR);
		goto out;
	}

	for (bat_attr = batadv_mesh_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(bat_priv->mesh_obj,
					&((*bat_attr)->attr));
		if (err) {
			batadv_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				   dev->name, BATADV_SYSFS_IF_MESH_SUBDIR,
				   ((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = batadv_mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
out:
	return -ENOMEM;
}

void batadv_sysfs_del_meshif(struct net_device *dev)
{
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct batadv_attribute **bat_attr;

	for (bat_attr = batadv_mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
}

/**
 * batadv_sysfs_add_vlan - add all the needed sysfs objects for the new vlan
 * @dev: netdev of the mesh interface
 * @vlan: private data of the newly added VLAN interface
 *
 * Return: 0 on success and -ENOMEM if any of the structure allocations fails.
 */
int batadv_sysfs_add_vlan(struct net_device *dev,
			  struct batadv_softif_vlan *vlan)
{
	char vlan_subdir[sizeof(BATADV_SYSFS_VLAN_SUBDIR_PREFIX) + 5];
	struct batadv_priv *bat_priv = netdev_priv(dev);
	struct batadv_attribute **bat_attr;
	int err;

	if (vlan->vid & BATADV_VLAN_HAS_TAG) {
		sprintf(vlan_subdir, BATADV_SYSFS_VLAN_SUBDIR_PREFIX "%hu",
			vlan->vid & VLAN_VID_MASK);

		vlan->kobj = kobject_create_and_add(vlan_subdir,
						    bat_priv->mesh_obj);
		if (!vlan->kobj) {
			batadv_err(dev, "Can't add sysfs directory: %s/%s\n",
				   dev->name, vlan_subdir);
			goto out;
		}
	} else {
		/* the untagged LAN uses the root folder to store its "VLAN
		 * specific attributes"
		 */
		vlan->kobj = bat_priv->mesh_obj;
		kobject_get(bat_priv->mesh_obj);
	}

	for (bat_attr = batadv_vlan_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(vlan->kobj,
					&((*bat_attr)->attr));
		if (err) {
			batadv_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				   dev->name, vlan_subdir,
				   ((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = batadv_vlan_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(vlan->kobj, &((*bat_attr)->attr));

	kobject_put(vlan->kobj);
	vlan->kobj = NULL;
out:
	return -ENOMEM;
}

/**
 * batadv_sysfs_del_vlan - remove all the sysfs objects for a given VLAN
 * @bat_priv: the bat priv with all the soft interface information
 * @vlan: the private data of the VLAN to destroy
 */
void batadv_sysfs_del_vlan(struct batadv_priv *bat_priv,
			   struct batadv_softif_vlan *vlan)
{
	struct batadv_attribute **bat_attr;

	for (bat_attr = batadv_vlan_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(vlan->kobj, &((*bat_attr)->attr));

	kobject_put(vlan->kobj);
	vlan->kobj = NULL;
}

static ssize_t batadv_show_mesh_iface(struct kobject *kobj,
				      struct attribute *attr, char *buff)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_hard_iface *hard_iface;
	ssize_t length;
	const char *ifname;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface)
		return 0;

	if (hard_iface->if_status == BATADV_IF_NOT_IN_USE)
		ifname =  "none";
	else
		ifname = hard_iface->soft_iface->name;

	length = sprintf(buff, "%s\n", ifname);

	batadv_hardif_put(hard_iface);

	return length;
}

static ssize_t batadv_store_mesh_iface(struct kobject *kobj,
				       struct attribute *attr, char *buff,
				       size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_hard_iface *hard_iface;
	int status_tmp = -1;
	int ret = count;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface)
		return count;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if (strlen(buff) >= IFNAMSIZ) {
		pr_err("Invalid parameter for 'mesh_iface' setting received: interface name too long '%s'\n",
		       buff);
		batadv_hardif_put(hard_iface);
		return -EINVAL;
	}

	if (strncmp(buff, "none", 4) == 0)
		status_tmp = BATADV_IF_NOT_IN_USE;
	else
		status_tmp = BATADV_IF_I_WANT_YOU;

	if (hard_iface->if_status == status_tmp)
		goto out;

	if ((hard_iface->soft_iface) &&
	    (strncmp(hard_iface->soft_iface->name, buff, IFNAMSIZ) == 0))
		goto out;

	rtnl_lock();

	if (status_tmp == BATADV_IF_NOT_IN_USE) {
		batadv_hardif_disable_interface(hard_iface,
						BATADV_IF_CLEANUP_AUTO);
		goto unlock;
	}

	/* if the interface already is in use */
	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		batadv_hardif_disable_interface(hard_iface,
						BATADV_IF_CLEANUP_AUTO);

	ret = batadv_hardif_enable_interface(hard_iface, buff);

unlock:
	rtnl_unlock();
out:
	batadv_hardif_put(hard_iface);
	return ret;
}

static ssize_t batadv_show_iface_status(struct kobject *kobj,
					struct attribute *attr, char *buff)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_hard_iface *hard_iface;
	ssize_t length;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface)
		return 0;

	switch (hard_iface->if_status) {
	case BATADV_IF_TO_BE_REMOVED:
		length = sprintf(buff, "disabling\n");
		break;
	case BATADV_IF_INACTIVE:
		length = sprintf(buff, "inactive\n");
		break;
	case BATADV_IF_ACTIVE:
		length = sprintf(buff, "active\n");
		break;
	case BATADV_IF_TO_BE_ACTIVATED:
		length = sprintf(buff, "enabling\n");
		break;
	case BATADV_IF_NOT_IN_USE:
	default:
		length = sprintf(buff, "not in use\n");
		break;
	}

	batadv_hardif_put(hard_iface);

	return length;
}

#ifdef CONFIG_BATMAN_ADV_BATMAN_V

/**
 * batadv_store_throughput_override - parse and store throughput override
 *  entered by the user
 * @kobj: kobject representing the private mesh sysfs directory
 * @attr: the batman-adv attribute the user is interacting with
 * @buff: the buffer containing the user data
 * @count: number of bytes in the buffer
 *
 * Return: 'count' on success or a negative error code in case of failure
 */
static ssize_t batadv_store_throughput_override(struct kobject *kobj,
						struct attribute *attr,
						char *buff, size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_hard_iface *hard_iface;
	u32 tp_override;
	u32 old_tp_override;
	bool ret;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface)
		return -EINVAL;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	ret = batadv_parse_throughput(net_dev, buff, "throughput_override",
				      &tp_override);
	if (!ret)
		return count;

	old_tp_override = atomic_read(&hard_iface->bat_v.throughput_override);
	if (old_tp_override == tp_override)
		goto out;

	batadv_info(net_dev, "%s: Changing from: %u.%u MBit to: %u.%u MBit\n",
		    "throughput_override",
		    old_tp_override / 10, old_tp_override % 10,
		    tp_override / 10, tp_override % 10);

	atomic_set(&hard_iface->bat_v.throughput_override, tp_override);

out:
	batadv_hardif_put(hard_iface);
	return count;
}

static ssize_t batadv_show_throughput_override(struct kobject *kobj,
					       struct attribute *attr,
					       char *buff)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct batadv_hard_iface *hard_iface;
	u32 tp_override;

	hard_iface = batadv_hardif_get_by_netdev(net_dev);
	if (!hard_iface)
		return -EINVAL;

	tp_override = atomic_read(&hard_iface->bat_v.throughput_override);

	return sprintf(buff, "%u.%u MBit\n", tp_override / 10,
		       tp_override % 10);
}

#endif

static BATADV_ATTR(mesh_iface, S_IRUGO | S_IWUSR, batadv_show_mesh_iface,
		   batadv_store_mesh_iface);
static BATADV_ATTR(iface_status, S_IRUGO, batadv_show_iface_status, NULL);
#ifdef CONFIG_BATMAN_ADV_BATMAN_V
BATADV_ATTR_HIF_UINT(elp_interval, bat_v.elp_interval, S_IRUGO | S_IWUSR,
		     2 * BATADV_JITTER, INT_MAX, NULL);
static BATADV_ATTR(throughput_override, S_IRUGO | S_IWUSR,
		   batadv_show_throughput_override,
		   batadv_store_throughput_override);
#endif

static struct batadv_attribute *batadv_batman_attrs[] = {
	&batadv_attr_mesh_iface,
	&batadv_attr_iface_status,
#ifdef CONFIG_BATMAN_ADV_BATMAN_V
	&batadv_attr_elp_interval,
	&batadv_attr_throughput_override,
#endif
	NULL,
};

int batadv_sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev)
{
	struct kobject *hardif_kobject = &dev->dev.kobj;
	struct batadv_attribute **bat_attr;
	int err;

	*hardif_obj = kobject_create_and_add(BATADV_SYSFS_IF_BAT_SUBDIR,
					     hardif_kobject);

	if (!*hardif_obj) {
		batadv_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			   BATADV_SYSFS_IF_BAT_SUBDIR);
		goto out;
	}

	for (bat_attr = batadv_batman_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(*hardif_obj, &((*bat_attr)->attr));
		if (err) {
			batadv_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				   dev->name, BATADV_SYSFS_IF_BAT_SUBDIR,
				   ((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = batadv_batman_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(*hardif_obj, &((*bat_attr)->attr));
out:
	return -ENOMEM;
}

void batadv_sysfs_del_hardif(struct kobject **hardif_obj)
{
	kobject_put(*hardif_obj);
	*hardif_obj = NULL;
}

int batadv_throw_uevent(struct batadv_priv *bat_priv, enum batadv_uev_type type,
			enum batadv_uev_action action, const char *data)
{
	int ret = -ENOMEM;
	struct kobject *bat_kobj;
	char *uevent_env[4] = { NULL, NULL, NULL, NULL };

	bat_kobj = &bat_priv->soft_iface->dev.kobj;

	uevent_env[0] = kasprintf(GFP_ATOMIC,
				  "%s%s", BATADV_UEV_TYPE_VAR,
				  batadv_uev_type_str[type]);
	if (!uevent_env[0])
		goto out;

	uevent_env[1] = kasprintf(GFP_ATOMIC,
				  "%s%s", BATADV_UEV_ACTION_VAR,
				  batadv_uev_action_str[action]);
	if (!uevent_env[1])
		goto out;

	/* If the event is DEL, ignore the data field */
	if (action != BATADV_UEV_DEL) {
		uevent_env[2] = kasprintf(GFP_ATOMIC,
					  "%s%s", BATADV_UEV_DATA_VAR, data);
		if (!uevent_env[2])
			goto out;
	}

	ret = kobject_uevent_env(bat_kobj, KOBJ_CHANGE, uevent_env);
out:
	kfree(uevent_env[0]);
	kfree(uevent_env[1]);
	kfree(uevent_env[2]);

	if (ret)
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Impossible to send uevent for (%s,%s,%s) event (err: %d)\n",
			   batadv_uev_type_str[type],
			   batadv_uev_action_str[action],
			   (action == BATADV_UEV_DEL ? "NULL" : data), ret);
	return ret;
}
