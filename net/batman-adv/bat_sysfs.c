/* Copyright (C) 2010-2012 B.A.T.M.A.N. contributors:
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 */

#include "main.h"
#include "bat_sysfs.h"
#include "translation-table.h"
#include "originator.h"
#include "hard-interface.h"
#include "gateway_common.h"
#include "gateway_client.h"
#include "vis.h"

static struct net_device *batadv_kobj_to_netdev(struct kobject *obj)
{
	struct device *dev = container_of(obj->parent, struct device, kobj);
	return to_net_dev(dev);
}

static struct bat_priv *batadv_kobj_to_batpriv(struct kobject *obj)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(obj);
	return netdev_priv(net_dev);
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

/* Use this, if you have customized show and store functions */
#define BATADV_ATTR(_name, _mode, _show, _store)	\
struct bat_attribute batadv_attr_##_name = {		\
	.attr = {.name = __stringify(_name),		\
		 .mode = _mode },			\
	.show   = _show,				\
	.store  = _store,				\
};

#define BATADV_ATTR_SIF_STORE_BOOL(_name, _post_func)			\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct bat_priv *bat_priv = netdev_priv(net_dev);		\
	return __batadv_store_bool_attr(buff, count, _post_func, attr,	\
					&bat_priv->_name, net_dev);	\
}

#define BATADV_ATTR_SIF_SHOW_BOOL(_name)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);	\
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


#define BATADV_ATTR_SIF_STORE_UINT(_name, _min, _max, _post_func)	\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct bat_priv *bat_priv = netdev_priv(net_dev);		\
	return __batadv_store_uint_attr(buff, count, _min, _max,	\
					_post_func, attr,		\
					&bat_priv->_name, net_dev);	\
}

#define BATADV_ATTR_SIF_SHOW_UINT(_name)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);	\
	return sprintf(buff, "%i\n", atomic_read(&bat_priv->_name));	\
}									\

/* Use this, if you are going to set [name] in the soft-interface
 * (bat_priv) to an unsigned integer value
 */
#define BATADV_ATTR_SIF_UINT(_name, _mode, _min, _max, _post_func)	\
	static BATADV_ATTR_SIF_STORE_UINT(_name, _min, _max, _post_func)\
	static BATADV_ATTR_SIF_SHOW_UINT(_name)				\
	static BATADV_ATTR(_name, _mode, batadv_show_##_name,		\
			   batadv_store_##_name)


#define BATADV_ATTR_HIF_STORE_UINT(_name, _min, _max, _post_func)	\
ssize_t batadv_store_##_name(struct kobject *kobj,			\
			     struct attribute *attr, char *buff,	\
			     size_t count)				\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct hard_iface *hard_iface;					\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = __batadv_store_uint_attr(buff, count, _min, _max,	\
					  _post_func, attr,		\
					  &hard_iface->_name, net_dev);	\
									\
	batadv_hardif_free_ref(hard_iface);				\
	return length;							\
}

#define BATADV_ATTR_HIF_SHOW_UINT(_name)				\
ssize_t batadv_show_##_name(struct kobject *kobj,			\
			    struct attribute *attr, char *buff)		\
{									\
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);	\
	struct hard_iface *hard_iface;					\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = sprintf(buff, "%i\n", atomic_read(&hard_iface->_name));\
									\
	batadv_hardif_free_ref(hard_iface);				\
	return length;							\
}

/* Use this, if you are going to set [name] in hard_iface to an
 * unsigned integer value
 */
#define BATADV_ATTR_HIF_UINT(_name, _mode, _min, _max, _post_func)	\
	static BATADV_ATTR_HIF_STORE_UINT(_name, _min, _max, _post_func)\
	static BATADV_ATTR_HIF_SHOW_UINT(_name)				\
	static BATADV_ATTR(_name, _mode, batadv_show_##_name,		\
			   batadv_store_##_name)


static int batadv_store_bool_attr(char *buff, size_t count,
				  struct net_device *net_dev,
				  const char *attr_name, atomic_t *attr)
{
	int enabled = -1;

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

	atomic_set(attr, (unsigned int)enabled);
	return count;
}

static inline ssize_t
__batadv_store_bool_attr(char *buff, size_t count,
			 void (*post_func)(struct net_device *),
			 struct attribute *attr,
			 atomic_t *attr_store, struct net_device *net_dev)
{
	int ret;

	ret = batadv_store_bool_attr(buff, count, net_dev, attr->name,
				     attr_store);
	if (post_func && ret)
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

static ssize_t batadv_show_vis_mode(struct kobject *kobj,
				    struct attribute *attr, char *buff)
{
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
	int vis_mode = atomic_read(&bat_priv->vis_mode);
	const char *mode;

	if (vis_mode == BATADV_VIS_TYPE_CLIENT_UPDATE)
		mode = "client";
	else
		mode = "server";

	return sprintf(buff, "%s\n", mode);
}

static ssize_t batadv_store_vis_mode(struct kobject *kobj,
				     struct attribute *attr, char *buff,
				     size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	unsigned long val;
	int ret, vis_mode_tmp = -1;
	const char *old_mode, *new_mode;

	ret = kstrtoul(buff, 10, &val);

	if (((count == 2) && (!ret) &&
	     (val == BATADV_VIS_TYPE_CLIENT_UPDATE)) ||
	    (strncmp(buff, "client", 6) == 0) ||
	    (strncmp(buff, "off", 3) == 0))
		vis_mode_tmp = BATADV_VIS_TYPE_CLIENT_UPDATE;

	if (((count == 2) && (!ret) &&
	     (val == BATADV_VIS_TYPE_SERVER_SYNC)) ||
	    (strncmp(buff, "server", 6) == 0))
		vis_mode_tmp = BATADV_VIS_TYPE_SERVER_SYNC;

	if (vis_mode_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		batadv_info(net_dev,
			    "Invalid parameter for 'vis mode' setting received: %s\n",
			    buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->vis_mode) == vis_mode_tmp)
		return count;

	if (atomic_read(&bat_priv->vis_mode) == BATADV_VIS_TYPE_CLIENT_UPDATE)
		old_mode =  "client";
	else
		old_mode = "server";

	if (vis_mode_tmp == BATADV_VIS_TYPE_CLIENT_UPDATE)
		new_mode =  "client";
	else
		new_mode = "server";

	batadv_info(net_dev, "Changing vis mode from: %s to: %s\n", old_mode,
		    new_mode);

	atomic_set(&bat_priv->vis_mode, (unsigned int)vis_mode_tmp);
	return count;
}

static ssize_t batadv_show_bat_algo(struct kobject *kobj,
				    struct attribute *attr, char *buff)
{
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
	return sprintf(buff, "%s\n", bat_priv->bat_algo_ops->name);
}

static void batadv_post_gw_deselect(struct net_device *net_dev)
{
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	batadv_gw_deselect(bat_priv);
}

static ssize_t batadv_show_gw_mode(struct kobject *kobj, struct attribute *attr,
				   char *buff)
{
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
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
	struct bat_priv *bat_priv = netdev_priv(net_dev);
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

	batadv_gw_deselect(bat_priv);
	atomic_set(&bat_priv->gw_mode, (unsigned int)gw_mode_tmp);
	return count;
}

static ssize_t batadv_show_gw_bwidth(struct kobject *kobj,
				     struct attribute *attr, char *buff)
{
	struct bat_priv *bat_priv = batadv_kobj_to_batpriv(kobj);
	int down, up;
	int gw_bandwidth = atomic_read(&bat_priv->gw_bandwidth);

	batadv_gw_bandwidth_to_kbit(gw_bandwidth, &down, &up);
	return sprintf(buff, "%i%s/%i%s\n",
		       (down > 2048 ? down / 1024 : down),
		       (down > 2048 ? "MBit" : "KBit"),
		       (up > 2048 ? up / 1024 : up),
		       (up > 2048 ? "MBit" : "KBit"));
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

BATADV_ATTR_SIF_BOOL(aggregated_ogms, S_IRUGO | S_IWUSR, NULL);
BATADV_ATTR_SIF_BOOL(bonding, S_IRUGO | S_IWUSR, NULL);
#ifdef CONFIG_BATMAN_ADV_BLA
BATADV_ATTR_SIF_BOOL(bridge_loop_avoidance, S_IRUGO | S_IWUSR, NULL);
#endif
BATADV_ATTR_SIF_BOOL(fragmentation, S_IRUGO | S_IWUSR, batadv_update_min_mtu);
BATADV_ATTR_SIF_BOOL(ap_isolation, S_IRUGO | S_IWUSR, NULL);
static BATADV_ATTR(vis_mode, S_IRUGO | S_IWUSR, batadv_show_vis_mode,
		   batadv_store_vis_mode);
static BATADV_ATTR(routing_algo, S_IRUGO, batadv_show_bat_algo, NULL);
static BATADV_ATTR(gw_mode, S_IRUGO | S_IWUSR, batadv_show_gw_mode,
		   batadv_store_gw_mode);
BATADV_ATTR_SIF_UINT(orig_interval, S_IRUGO | S_IWUSR, 2 * BATADV_JITTER,
		     INT_MAX, NULL);
BATADV_ATTR_SIF_UINT(hop_penalty, S_IRUGO | S_IWUSR, 0, BATADV_TQ_MAX_VALUE,
		     NULL);
BATADV_ATTR_SIF_UINT(gw_sel_class, S_IRUGO | S_IWUSR, 1, BATADV_TQ_MAX_VALUE,
		     batadv_post_gw_deselect);
static BATADV_ATTR(gw_bandwidth, S_IRUGO | S_IWUSR, batadv_show_gw_bwidth,
		   batadv_store_gw_bwidth);
#ifdef CONFIG_BATMAN_ADV_DEBUG
BATADV_ATTR_SIF_UINT(log_level, S_IRUGO | S_IWUSR, 0, BATADV_DBG_ALL, NULL);
#endif

static struct bat_attribute *batadv_mesh_attrs[] = {
	&batadv_attr_aggregated_ogms,
	&batadv_attr_bonding,
#ifdef CONFIG_BATMAN_ADV_BLA
	&batadv_attr_bridge_loop_avoidance,
#endif
	&batadv_attr_fragmentation,
	&batadv_attr_ap_isolation,
	&batadv_attr_vis_mode,
	&batadv_attr_routing_algo,
	&batadv_attr_gw_mode,
	&batadv_attr_orig_interval,
	&batadv_attr_hop_penalty,
	&batadv_attr_gw_sel_class,
	&batadv_attr_gw_bandwidth,
#ifdef CONFIG_BATMAN_ADV_DEBUG
	&batadv_attr_log_level,
#endif
	NULL,
};

int batadv_sysfs_add_meshif(struct net_device *dev)
{
	struct kobject *batif_kobject = &dev->dev.kobj;
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;
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
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;

	for (bat_attr = batadv_mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
}

static ssize_t batadv_show_mesh_iface(struct kobject *kobj,
				      struct attribute *attr, char *buff)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct hard_iface *hard_iface = batadv_hardif_get_by_netdev(net_dev);
	ssize_t length;
	const char *ifname;

	if (!hard_iface)
		return 0;

	if (hard_iface->if_status == BATADV_IF_NOT_IN_USE)
		ifname =  "none";
	else
		ifname = hard_iface->soft_iface->name;

	length = sprintf(buff, "%s\n", ifname);

	batadv_hardif_free_ref(hard_iface);

	return length;
}

static ssize_t batadv_store_mesh_iface(struct kobject *kobj,
				       struct attribute *attr, char *buff,
				       size_t count)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct hard_iface *hard_iface = batadv_hardif_get_by_netdev(net_dev);
	int status_tmp = -1;
	int ret = count;

	if (!hard_iface)
		return count;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if (strlen(buff) >= IFNAMSIZ) {
		pr_err("Invalid parameter for 'mesh_iface' setting received: interface name too long '%s'\n",
		       buff);
		batadv_hardif_free_ref(hard_iface);
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

	if (!rtnl_trylock()) {
		ret = -ERESTARTSYS;
		goto out;
	}

	if (status_tmp == BATADV_IF_NOT_IN_USE) {
		batadv_hardif_disable_interface(hard_iface);
		goto unlock;
	}

	/* if the interface already is in use */
	if (hard_iface->if_status != BATADV_IF_NOT_IN_USE)
		batadv_hardif_disable_interface(hard_iface);

	ret = batadv_hardif_enable_interface(hard_iface, buff);

unlock:
	rtnl_unlock();
out:
	batadv_hardif_free_ref(hard_iface);
	return ret;
}

static ssize_t batadv_show_iface_status(struct kobject *kobj,
					struct attribute *attr, char *buff)
{
	struct net_device *net_dev = batadv_kobj_to_netdev(kobj);
	struct hard_iface *hard_iface = batadv_hardif_get_by_netdev(net_dev);
	ssize_t length;

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

	batadv_hardif_free_ref(hard_iface);

	return length;
}

static BATADV_ATTR(mesh_iface, S_IRUGO | S_IWUSR, batadv_show_mesh_iface,
		   batadv_store_mesh_iface);
static BATADV_ATTR(iface_status, S_IRUGO, batadv_show_iface_status, NULL);

static struct bat_attribute *batadv_batman_attrs[] = {
	&batadv_attr_mesh_iface,
	&batadv_attr_iface_status,
	NULL,
};

int batadv_sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev)
{
	struct kobject *hardif_kobject = &dev->dev.kobj;
	struct bat_attribute **bat_attr;
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

int batadv_throw_uevent(struct bat_priv *bat_priv, enum batadv_uev_type type,
			enum batadv_uev_action action, const char *data)
{
	int ret = -ENOMEM;
	struct hard_iface *primary_if = NULL;
	struct kobject *bat_kobj;
	char *uevent_env[4] = { NULL, NULL, NULL, NULL };

	primary_if = batadv_primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	bat_kobj = &primary_if->soft_iface->dev.kobj;

	uevent_env[0] = kmalloc(strlen(BATADV_UEV_TYPE_VAR) +
				strlen(batadv_uev_type_str[type]) + 1,
				GFP_ATOMIC);
	if (!uevent_env[0])
		goto out;

	sprintf(uevent_env[0], "%s%s", BATADV_UEV_TYPE_VAR,
		batadv_uev_type_str[type]);

	uevent_env[1] = kmalloc(strlen(BATADV_UEV_ACTION_VAR) +
				strlen(batadv_uev_action_str[action]) + 1,
				GFP_ATOMIC);
	if (!uevent_env[1])
		goto out;

	sprintf(uevent_env[1], "%s%s", BATADV_UEV_ACTION_VAR,
		batadv_uev_action_str[action]);

	/* If the event is DEL, ignore the data field */
	if (action != BATADV_UEV_DEL) {
		uevent_env[2] = kmalloc(strlen(BATADV_UEV_DATA_VAR) +
					strlen(data) + 1, GFP_ATOMIC);
		if (!uevent_env[2])
			goto out;

		sprintf(uevent_env[2], "%s%s", BATADV_UEV_DATA_VAR, data);
	}

	ret = kobject_uevent_env(bat_kobj, KOBJ_CHANGE, uevent_env);
out:
	kfree(uevent_env[0]);
	kfree(uevent_env[1]);
	kfree(uevent_env[2]);

	if (primary_if)
		batadv_hardif_free_ref(primary_if);

	if (ret)
		batadv_dbg(BATADV_DBG_BATMAN, bat_priv,
			   "Impossible to send uevent for (%s,%s,%s) event (err: %d)\n",
			   batadv_uev_type_str[type],
			   batadv_uev_action_str[action],
			   (action == BATADV_UEV_DEL ? "NULL" : data), ret);
	return ret;
}
