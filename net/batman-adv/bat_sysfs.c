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

static struct net_device *kobj_to_netdev(struct kobject *obj)
{
	struct device *dev = container_of(obj->parent, struct device, kobj);
	return to_net_dev(dev);
}

static struct bat_priv *kobj_to_batpriv(struct kobject *obj)
{
	struct net_device *net_dev = kobj_to_netdev(obj);
	return netdev_priv(net_dev);
}

#define UEV_TYPE_VAR	"BATTYPE="
#define UEV_ACTION_VAR	"BATACTION="
#define UEV_DATA_VAR	"BATDATA="

static char *uev_action_str[] = {
	"add",
	"del",
	"change"
};

static char *uev_type_str[] = {
	"gw"
};

/* Use this, if you have customized show and store functions */
#define BAT_ATTR(_name, _mode, _show, _store)	\
struct bat_attribute bat_attr_##_name = {	\
	.attr = {.name = __stringify(_name),	\
		 .mode = _mode },		\
	.show   = _show,			\
	.store  = _store,			\
};

#define BAT_ATTR_SIF_STORE_BOOL(_name, _post_func)			\
ssize_t store_##_name(struct kobject *kobj, struct attribute *attr,	\
		      char *buff, size_t count)				\
{									\
	struct net_device *net_dev = kobj_to_netdev(kobj);		\
	struct bat_priv *bat_priv = netdev_priv(net_dev);		\
	return __store_bool_attr(buff, count, _post_func, attr,		\
				 &bat_priv->_name, net_dev);		\
}

#define BAT_ATTR_SIF_SHOW_BOOL(_name)					\
ssize_t show_##_name(struct kobject *kobj,				\
		     struct attribute *attr, char *buff)		\
{									\
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);		\
	return sprintf(buff, "%s\n",					\
		       atomic_read(&bat_priv->_name) == 0 ?		\
		       "disabled" : "enabled");				\
}									\

/* Use this, if you are going to turn a [name] in the soft-interface
 * (bat_priv) on or off
 */
#define BAT_ATTR_SIF_BOOL(_name, _mode, _post_func)			\
	static BAT_ATTR_SIF_STORE_BOOL(_name, _post_func)		\
	static BAT_ATTR_SIF_SHOW_BOOL(_name)				\
	static BAT_ATTR(_name, _mode, show_##_name, store_##_name)


#define BAT_ATTR_SIF_STORE_UINT(_name, _min, _max, _post_func)		\
ssize_t store_##_name(struct kobject *kobj, struct attribute *attr,	\
		      char *buff, size_t count)				\
{									\
	struct net_device *net_dev = kobj_to_netdev(kobj);		\
	struct bat_priv *bat_priv = netdev_priv(net_dev);		\
	return __store_uint_attr(buff, count, _min, _max, _post_func,	\
				 attr, &bat_priv->_name, net_dev);	\
}

#define BAT_ATTR_SIF_SHOW_UINT(_name)					\
ssize_t show_##_name(struct kobject *kobj,				\
		     struct attribute *attr, char *buff)		\
{									\
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);		\
	return sprintf(buff, "%i\n", atomic_read(&bat_priv->_name));	\
}									\

/* Use this, if you are going to set [name] in the soft-interface
 * (bat_priv) to an unsigned integer value
 */
#define BAT_ATTR_SIF_UINT(_name, _mode, _min, _max, _post_func)		\
	static BAT_ATTR_SIF_STORE_UINT(_name, _min, _max, _post_func)	\
	static BAT_ATTR_SIF_SHOW_UINT(_name)				\
	static BAT_ATTR(_name, _mode, show_##_name, store_##_name)


#define BAT_ATTR_HIF_STORE_UINT(_name, _min, _max, _post_func)		\
ssize_t store_##_name(struct kobject *kobj, struct attribute *attr,	\
		      char *buff, size_t count)				\
{									\
	struct net_device *net_dev = kobj_to_netdev(kobj);		\
	struct hard_iface *hard_iface;					\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = __store_uint_attr(buff, count, _min, _max, _post_func,	\
				   attr, &hard_iface->_name, net_dev);	\
									\
	hardif_free_ref(hard_iface);					\
	return length;							\
}

#define BAT_ATTR_HIF_SHOW_UINT(_name)					\
ssize_t show_##_name(struct kobject *kobj,				\
		     struct attribute *attr, char *buff)		\
{									\
	struct net_device *net_dev = kobj_to_netdev(kobj);		\
	struct hard_iface *hard_iface;					\
	ssize_t length;							\
									\
	hard_iface = batadv_hardif_get_by_netdev(net_dev);		\
	if (!hard_iface)						\
		return 0;						\
									\
	length = sprintf(buff, "%i\n", atomic_read(&hard_iface->_name));\
									\
	hardif_free_ref(hard_iface);					\
	return length;							\
}

/* Use this, if you are going to set [name] in hard_iface to an
 * unsigned integer value
 */
#define BAT_ATTR_HIF_UINT(_name, _mode, _min, _max, _post_func)		\
	static BAT_ATTR_HIF_STORE_UINT(_name, _min, _max, _post_func)	\
	static BAT_ATTR_HIF_SHOW_UINT(_name)				\
	static BAT_ATTR(_name, _mode, show_##_name, store_##_name)


static int store_bool_attr(char *buff, size_t count,
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
		bat_info(net_dev,
			 "%s: Invalid parameter received: %s\n",
			 attr_name, buff);
		return -EINVAL;
	}

	if (atomic_read(attr) == enabled)
		return count;

	bat_info(net_dev, "%s: Changing from: %s to: %s\n", attr_name,
		 atomic_read(attr) == 1 ? "enabled" : "disabled",
		 enabled == 1 ? "enabled" : "disabled");

	atomic_set(attr, (unsigned int)enabled);
	return count;
}

static inline ssize_t __store_bool_attr(char *buff, size_t count,
			void (*post_func)(struct net_device *),
			struct attribute *attr,
			atomic_t *attr_store, struct net_device *net_dev)
{
	int ret;

	ret = store_bool_attr(buff, count, net_dev, attr->name, attr_store);
	if (post_func && ret)
		post_func(net_dev);

	return ret;
}

static int store_uint_attr(const char *buff, size_t count,
			   struct net_device *net_dev, const char *attr_name,
			   unsigned int min, unsigned int max, atomic_t *attr)
{
	unsigned long uint_val;
	int ret;

	ret = kstrtoul(buff, 10, &uint_val);
	if (ret) {
		bat_info(net_dev,
			 "%s: Invalid parameter received: %s\n",
			 attr_name, buff);
		return -EINVAL;
	}

	if (uint_val < min) {
		bat_info(net_dev, "%s: Value is too small: %lu min: %u\n",
			 attr_name, uint_val, min);
		return -EINVAL;
	}

	if (uint_val > max) {
		bat_info(net_dev, "%s: Value is too big: %lu max: %u\n",
			 attr_name, uint_val, max);
		return -EINVAL;
	}

	if (atomic_read(attr) == uint_val)
		return count;

	bat_info(net_dev, "%s: Changing from: %i to: %lu\n",
		 attr_name, atomic_read(attr), uint_val);

	atomic_set(attr, uint_val);
	return count;
}

static inline ssize_t __store_uint_attr(const char *buff, size_t count,
			int min, int max,
			void (*post_func)(struct net_device *),
			const struct attribute *attr,
			atomic_t *attr_store, struct net_device *net_dev)
{
	int ret;

	ret = store_uint_attr(buff, count, net_dev, attr->name,
			      min, max, attr_store);
	if (post_func && ret)
		post_func(net_dev);

	return ret;
}

static ssize_t show_vis_mode(struct kobject *kobj, struct attribute *attr,
			     char *buff)
{
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);
	int vis_mode = atomic_read(&bat_priv->vis_mode);

	return sprintf(buff, "%s\n",
		       vis_mode == VIS_TYPE_CLIENT_UPDATE ?
							"client" : "server");
}

static ssize_t store_vis_mode(struct kobject *kobj, struct attribute *attr,
			      char *buff, size_t count)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	unsigned long val;
	int ret, vis_mode_tmp = -1;

	ret = kstrtoul(buff, 10, &val);

	if (((count == 2) && (!ret) && (val == VIS_TYPE_CLIENT_UPDATE)) ||
	    (strncmp(buff, "client", 6) == 0) ||
	    (strncmp(buff, "off", 3) == 0))
		vis_mode_tmp = VIS_TYPE_CLIENT_UPDATE;

	if (((count == 2) && (!ret) && (val == VIS_TYPE_SERVER_SYNC)) ||
	    (strncmp(buff, "server", 6) == 0))
		vis_mode_tmp = VIS_TYPE_SERVER_SYNC;

	if (vis_mode_tmp < 0) {
		if (buff[count - 1] == '\n')
			buff[count - 1] = '\0';

		bat_info(net_dev,
			 "Invalid parameter for 'vis mode' setting received: %s\n",
			 buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->vis_mode) == vis_mode_tmp)
		return count;

	bat_info(net_dev, "Changing vis mode from: %s to: %s\n",
		 atomic_read(&bat_priv->vis_mode) == VIS_TYPE_CLIENT_UPDATE ?
		 "client" : "server", vis_mode_tmp == VIS_TYPE_CLIENT_UPDATE ?
		 "client" : "server");

	atomic_set(&bat_priv->vis_mode, (unsigned int)vis_mode_tmp);
	return count;
}

static ssize_t show_bat_algo(struct kobject *kobj, struct attribute *attr,
			    char *buff)
{
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);
	return sprintf(buff, "%s\n", bat_priv->bat_algo_ops->name);
}

static void post_gw_deselect(struct net_device *net_dev)
{
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	batadv_gw_deselect(bat_priv);
}

static ssize_t show_gw_mode(struct kobject *kobj, struct attribute *attr,
			    char *buff)
{
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);
	int bytes_written;

	switch (atomic_read(&bat_priv->gw_mode)) {
	case GW_MODE_CLIENT:
		bytes_written = sprintf(buff, "%s\n", GW_MODE_CLIENT_NAME);
		break;
	case GW_MODE_SERVER:
		bytes_written = sprintf(buff, "%s\n", GW_MODE_SERVER_NAME);
		break;
	default:
		bytes_written = sprintf(buff, "%s\n", GW_MODE_OFF_NAME);
		break;
	}

	return bytes_written;
}

static ssize_t store_gw_mode(struct kobject *kobj, struct attribute *attr,
			     char *buff, size_t count)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);
	struct bat_priv *bat_priv = netdev_priv(net_dev);
	char *curr_gw_mode_str;
	int gw_mode_tmp = -1;

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	if (strncmp(buff, GW_MODE_OFF_NAME, strlen(GW_MODE_OFF_NAME)) == 0)
		gw_mode_tmp = GW_MODE_OFF;

	if (strncmp(buff, GW_MODE_CLIENT_NAME,
		    strlen(GW_MODE_CLIENT_NAME)) == 0)
		gw_mode_tmp = GW_MODE_CLIENT;

	if (strncmp(buff, GW_MODE_SERVER_NAME,
		    strlen(GW_MODE_SERVER_NAME)) == 0)
		gw_mode_tmp = GW_MODE_SERVER;

	if (gw_mode_tmp < 0) {
		bat_info(net_dev,
			 "Invalid parameter for 'gw mode' setting received: %s\n",
			 buff);
		return -EINVAL;
	}

	if (atomic_read(&bat_priv->gw_mode) == gw_mode_tmp)
		return count;

	switch (atomic_read(&bat_priv->gw_mode)) {
	case GW_MODE_CLIENT:
		curr_gw_mode_str = GW_MODE_CLIENT_NAME;
		break;
	case GW_MODE_SERVER:
		curr_gw_mode_str = GW_MODE_SERVER_NAME;
		break;
	default:
		curr_gw_mode_str = GW_MODE_OFF_NAME;
		break;
	}

	bat_info(net_dev, "Changing gw mode from: %s to: %s\n",
		 curr_gw_mode_str, buff);

	batadv_gw_deselect(bat_priv);
	atomic_set(&bat_priv->gw_mode, (unsigned int)gw_mode_tmp);
	return count;
}

static ssize_t show_gw_bwidth(struct kobject *kobj, struct attribute *attr,
			      char *buff)
{
	struct bat_priv *bat_priv = kobj_to_batpriv(kobj);
	int down, up;
	int gw_bandwidth = atomic_read(&bat_priv->gw_bandwidth);

	batadv_gw_bandwidth_to_kbit(gw_bandwidth, &down, &up);
	return sprintf(buff, "%i%s/%i%s\n",
		       (down > 2048 ? down / 1024 : down),
		       (down > 2048 ? "MBit" : "KBit"),
		       (up > 2048 ? up / 1024 : up),
		       (up > 2048 ? "MBit" : "KBit"));
}

static ssize_t store_gw_bwidth(struct kobject *kobj, struct attribute *attr,
			       char *buff, size_t count)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);

	if (buff[count - 1] == '\n')
		buff[count - 1] = '\0';

	return batadv_gw_bandwidth_set(net_dev, buff, count);
}

BAT_ATTR_SIF_BOOL(aggregated_ogms, S_IRUGO | S_IWUSR, NULL);
BAT_ATTR_SIF_BOOL(bonding, S_IRUGO | S_IWUSR, NULL);
#ifdef CONFIG_BATMAN_ADV_BLA
BAT_ATTR_SIF_BOOL(bridge_loop_avoidance, S_IRUGO | S_IWUSR, NULL);
#endif
BAT_ATTR_SIF_BOOL(fragmentation, S_IRUGO | S_IWUSR, batadv_update_min_mtu);
BAT_ATTR_SIF_BOOL(ap_isolation, S_IRUGO | S_IWUSR, NULL);
static BAT_ATTR(vis_mode, S_IRUGO | S_IWUSR, show_vis_mode, store_vis_mode);
static BAT_ATTR(routing_algo, S_IRUGO, show_bat_algo, NULL);
static BAT_ATTR(gw_mode, S_IRUGO | S_IWUSR, show_gw_mode, store_gw_mode);
BAT_ATTR_SIF_UINT(orig_interval, S_IRUGO | S_IWUSR, 2 * JITTER, INT_MAX, NULL);
BAT_ATTR_SIF_UINT(hop_penalty, S_IRUGO | S_IWUSR, 0, TQ_MAX_VALUE, NULL);
BAT_ATTR_SIF_UINT(gw_sel_class, S_IRUGO | S_IWUSR, 1, TQ_MAX_VALUE,
		  post_gw_deselect);
static BAT_ATTR(gw_bandwidth, S_IRUGO | S_IWUSR, show_gw_bwidth,
		store_gw_bwidth);
#ifdef CONFIG_BATMAN_ADV_DEBUG
BAT_ATTR_SIF_UINT(log_level, S_IRUGO | S_IWUSR, 0, DBG_ALL, NULL);
#endif

static struct bat_attribute *mesh_attrs[] = {
	&bat_attr_aggregated_ogms,
	&bat_attr_bonding,
#ifdef CONFIG_BATMAN_ADV_BLA
	&bat_attr_bridge_loop_avoidance,
#endif
	&bat_attr_fragmentation,
	&bat_attr_ap_isolation,
	&bat_attr_vis_mode,
	&bat_attr_routing_algo,
	&bat_attr_gw_mode,
	&bat_attr_orig_interval,
	&bat_attr_hop_penalty,
	&bat_attr_gw_sel_class,
	&bat_attr_gw_bandwidth,
#ifdef CONFIG_BATMAN_ADV_DEBUG
	&bat_attr_log_level,
#endif
	NULL,
};

int batadv_sysfs_add_meshif(struct net_device *dev)
{
	struct kobject *batif_kobject = &dev->dev.kobj;
	struct bat_priv *bat_priv = netdev_priv(dev);
	struct bat_attribute **bat_attr;
	int err;

	bat_priv->mesh_obj = kobject_create_and_add(SYSFS_IF_MESH_SUBDIR,
						    batif_kobject);
	if (!bat_priv->mesh_obj) {
		bat_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			SYSFS_IF_MESH_SUBDIR);
		goto out;
	}

	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(bat_priv->mesh_obj,
					&((*bat_attr)->attr));
		if (err) {
			bat_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				dev->name, SYSFS_IF_MESH_SUBDIR,
				((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr)
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

	for (bat_attr = mesh_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(bat_priv->mesh_obj, &((*bat_attr)->attr));

	kobject_put(bat_priv->mesh_obj);
	bat_priv->mesh_obj = NULL;
}

static ssize_t show_mesh_iface(struct kobject *kobj, struct attribute *attr,
			       char *buff)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);
	struct hard_iface *hard_iface = batadv_hardif_get_by_netdev(net_dev);
	ssize_t length;

	if (!hard_iface)
		return 0;

	length = sprintf(buff, "%s\n", hard_iface->if_status == IF_NOT_IN_USE ?
			 "none" : hard_iface->soft_iface->name);

	hardif_free_ref(hard_iface);

	return length;
}

static ssize_t store_mesh_iface(struct kobject *kobj, struct attribute *attr,
				char *buff, size_t count)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);
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
		hardif_free_ref(hard_iface);
		return -EINVAL;
	}

	if (strncmp(buff, "none", 4) == 0)
		status_tmp = IF_NOT_IN_USE;
	else
		status_tmp = IF_I_WANT_YOU;

	if (hard_iface->if_status == status_tmp)
		goto out;

	if ((hard_iface->soft_iface) &&
	    (strncmp(hard_iface->soft_iface->name, buff, IFNAMSIZ) == 0))
		goto out;

	if (!rtnl_trylock()) {
		ret = -ERESTARTSYS;
		goto out;
	}

	if (status_tmp == IF_NOT_IN_USE) {
		batadv_hardif_disable_interface(hard_iface);
		goto unlock;
	}

	/* if the interface already is in use */
	if (hard_iface->if_status != IF_NOT_IN_USE)
		batadv_hardif_disable_interface(hard_iface);

	ret = batadv_hardif_enable_interface(hard_iface, buff);

unlock:
	rtnl_unlock();
out:
	hardif_free_ref(hard_iface);
	return ret;
}

static ssize_t show_iface_status(struct kobject *kobj, struct attribute *attr,
				 char *buff)
{
	struct net_device *net_dev = kobj_to_netdev(kobj);
	struct hard_iface *hard_iface = batadv_hardif_get_by_netdev(net_dev);
	ssize_t length;

	if (!hard_iface)
		return 0;

	switch (hard_iface->if_status) {
	case IF_TO_BE_REMOVED:
		length = sprintf(buff, "disabling\n");
		break;
	case IF_INACTIVE:
		length = sprintf(buff, "inactive\n");
		break;
	case IF_ACTIVE:
		length = sprintf(buff, "active\n");
		break;
	case IF_TO_BE_ACTIVATED:
		length = sprintf(buff, "enabling\n");
		break;
	case IF_NOT_IN_USE:
	default:
		length = sprintf(buff, "not in use\n");
		break;
	}

	hardif_free_ref(hard_iface);

	return length;
}

static BAT_ATTR(mesh_iface, S_IRUGO | S_IWUSR,
		show_mesh_iface, store_mesh_iface);
static BAT_ATTR(iface_status, S_IRUGO, show_iface_status, NULL);

static struct bat_attribute *batman_attrs[] = {
	&bat_attr_mesh_iface,
	&bat_attr_iface_status,
	NULL,
};

int batadv_sysfs_add_hardif(struct kobject **hardif_obj, struct net_device *dev)
{
	struct kobject *hardif_kobject = &dev->dev.kobj;
	struct bat_attribute **bat_attr;
	int err;

	*hardif_obj = kobject_create_and_add(SYSFS_IF_BAT_SUBDIR,
						    hardif_kobject);

	if (!*hardif_obj) {
		bat_err(dev, "Can't add sysfs directory: %s/%s\n", dev->name,
			SYSFS_IF_BAT_SUBDIR);
		goto out;
	}

	for (bat_attr = batman_attrs; *bat_attr; ++bat_attr) {
		err = sysfs_create_file(*hardif_obj, &((*bat_attr)->attr));
		if (err) {
			bat_err(dev, "Can't add sysfs file: %s/%s/%s\n",
				dev->name, SYSFS_IF_BAT_SUBDIR,
				((*bat_attr)->attr).name);
			goto rem_attr;
		}
	}

	return 0;

rem_attr:
	for (bat_attr = batman_attrs; *bat_attr; ++bat_attr)
		sysfs_remove_file(*hardif_obj, &((*bat_attr)->attr));
out:
	return -ENOMEM;
}

void batadv_sysfs_del_hardif(struct kobject **hardif_obj)
{
	kobject_put(*hardif_obj);
	*hardif_obj = NULL;
}

int batadv_throw_uevent(struct bat_priv *bat_priv, enum uev_type type,
			enum uev_action action, const char *data)
{
	int ret = -ENOMEM;
	struct hard_iface *primary_if = NULL;
	struct kobject *bat_kobj;
	char *uevent_env[4] = { NULL, NULL, NULL, NULL };

	primary_if = primary_if_get_selected(bat_priv);
	if (!primary_if)
		goto out;

	bat_kobj = &primary_if->soft_iface->dev.kobj;

	uevent_env[0] = kmalloc(strlen(UEV_TYPE_VAR) +
				strlen(uev_type_str[type]) + 1,
				GFP_ATOMIC);
	if (!uevent_env[0])
		goto out;

	sprintf(uevent_env[0], "%s%s", UEV_TYPE_VAR, uev_type_str[type]);

	uevent_env[1] = kmalloc(strlen(UEV_ACTION_VAR) +
				strlen(uev_action_str[action]) + 1,
				GFP_ATOMIC);
	if (!uevent_env[1])
		goto out;

	sprintf(uevent_env[1], "%s%s", UEV_ACTION_VAR, uev_action_str[action]);

	/* If the event is DEL, ignore the data field */
	if (action != UEV_DEL) {
		uevent_env[2] = kmalloc(strlen(UEV_DATA_VAR) +
					strlen(data) + 1, GFP_ATOMIC);
		if (!uevent_env[2])
			goto out;

		sprintf(uevent_env[2], "%s%s", UEV_DATA_VAR, data);
	}

	ret = kobject_uevent_env(bat_kobj, KOBJ_CHANGE, uevent_env);
out:
	kfree(uevent_env[0]);
	kfree(uevent_env[1]);
	kfree(uevent_env[2]);

	if (primary_if)
		hardif_free_ref(primary_if);

	if (ret)
		bat_dbg(DBG_BATMAN, bat_priv,
			"Impossible to send uevent for (%s,%s,%s) event (err: %d)\n",
			uev_type_str[type], uev_action_str[action],
			(action == UEV_DEL ? "NULL" : data), ret);
	return ret;
}
