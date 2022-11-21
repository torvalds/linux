// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2004, 2005 Oracle.  All rights reserved.
 */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/configfs.h>

#include "tcp.h"
#include "nodemanager.h"
#include "heartbeat.h"
#include "masklog.h"
#include "sys.h"

/* for now we operate under the assertion that there can be only one
 * cluster active at a time.  Changing this will require trickling
 * cluster references throughout where nodes are looked up */
struct o2nm_cluster *o2nm_single_cluster = NULL;

static const char *o2nm_fence_method_desc[O2NM_FENCE_METHODS] = {
	"reset",	/* O2NM_FENCE_RESET */
	"panic",	/* O2NM_FENCE_PANIC */
};

static inline void o2nm_lock_subsystem(void);
static inline void o2nm_unlock_subsystem(void);

struct o2nm_node *o2nm_get_node_by_num(u8 node_num)
{
	struct o2nm_node *node = NULL;

	if (node_num >= O2NM_MAX_NODES || o2nm_single_cluster == NULL)
		goto out;

	read_lock(&o2nm_single_cluster->cl_nodes_lock);
	node = o2nm_single_cluster->cl_nodes[node_num];
	if (node)
		config_item_get(&node->nd_item);
	read_unlock(&o2nm_single_cluster->cl_nodes_lock);
out:
	return node;
}
EXPORT_SYMBOL_GPL(o2nm_get_node_by_num);

int o2nm_configured_node_map(unsigned long *map, unsigned bytes)
{
	struct o2nm_cluster *cluster = o2nm_single_cluster;

	BUG_ON(bytes < (sizeof(cluster->cl_nodes_bitmap)));

	if (cluster == NULL)
		return -EINVAL;

	read_lock(&cluster->cl_nodes_lock);
	memcpy(map, cluster->cl_nodes_bitmap, sizeof(cluster->cl_nodes_bitmap));
	read_unlock(&cluster->cl_nodes_lock);

	return 0;
}
EXPORT_SYMBOL_GPL(o2nm_configured_node_map);

static struct o2nm_node *o2nm_node_ip_tree_lookup(struct o2nm_cluster *cluster,
						  __be32 ip_needle,
						  struct rb_node ***ret_p,
						  struct rb_node **ret_parent)
{
	struct rb_node **p = &cluster->cl_node_ip_tree.rb_node;
	struct rb_node *parent = NULL;
	struct o2nm_node *node, *ret = NULL;

	while (*p) {
		int cmp;

		parent = *p;
		node = rb_entry(parent, struct o2nm_node, nd_ip_node);

		cmp = memcmp(&ip_needle, &node->nd_ipv4_address,
				sizeof(ip_needle));
		if (cmp < 0)
			p = &(*p)->rb_left;
		else if (cmp > 0)
			p = &(*p)->rb_right;
		else {
			ret = node;
			break;
		}
	}

	if (ret_p != NULL)
		*ret_p = p;
	if (ret_parent != NULL)
		*ret_parent = parent;

	return ret;
}

struct o2nm_node *o2nm_get_node_by_ip(__be32 addr)
{
	struct o2nm_node *node = NULL;
	struct o2nm_cluster *cluster = o2nm_single_cluster;

	if (cluster == NULL)
		goto out;

	read_lock(&cluster->cl_nodes_lock);
	node = o2nm_node_ip_tree_lookup(cluster, addr, NULL, NULL);
	if (node)
		config_item_get(&node->nd_item);
	read_unlock(&cluster->cl_nodes_lock);

out:
	return node;
}
EXPORT_SYMBOL_GPL(o2nm_get_node_by_ip);

void o2nm_node_put(struct o2nm_node *node)
{
	config_item_put(&node->nd_item);
}
EXPORT_SYMBOL_GPL(o2nm_node_put);

void o2nm_node_get(struct o2nm_node *node)
{
	config_item_get(&node->nd_item);
}
EXPORT_SYMBOL_GPL(o2nm_node_get);

u8 o2nm_this_node(void)
{
	u8 node_num = O2NM_MAX_NODES;

	if (o2nm_single_cluster && o2nm_single_cluster->cl_has_local)
		node_num = o2nm_single_cluster->cl_local_node;

	return node_num;
}
EXPORT_SYMBOL_GPL(o2nm_this_node);

/* node configfs bits */

static struct o2nm_cluster *to_o2nm_cluster(struct config_item *item)
{
	return item ?
		container_of(to_config_group(item), struct o2nm_cluster,
			     cl_group)
		: NULL;
}

static struct o2nm_node *to_o2nm_node(struct config_item *item)
{
	return item ? container_of(item, struct o2nm_node, nd_item) : NULL;
}

static void o2nm_node_release(struct config_item *item)
{
	struct o2nm_node *node = to_o2nm_node(item);
	kfree(node);
}

static ssize_t o2nm_node_num_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", to_o2nm_node(item)->nd_num);
}

static struct o2nm_cluster *to_o2nm_cluster_from_node(struct o2nm_node *node)
{
	/* through the first node_set .parent
	 * mycluster/nodes/mynode == o2nm_cluster->o2nm_node_group->o2nm_node */
	if (node->nd_item.ci_parent)
		return to_o2nm_cluster(node->nd_item.ci_parent->ci_parent);
	else
		return NULL;
}

enum {
	O2NM_NODE_ATTR_NUM = 0,
	O2NM_NODE_ATTR_PORT,
	O2NM_NODE_ATTR_ADDRESS,
};

static ssize_t o2nm_node_num_store(struct config_item *item, const char *page,
				   size_t count)
{
	struct o2nm_node *node = to_o2nm_node(item);
	struct o2nm_cluster *cluster;
	unsigned long tmp;
	char *p = (char *)page;
	int ret = 0;

	tmp = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	if (tmp >= O2NM_MAX_NODES)
		return -ERANGE;

	/* once we're in the cl_nodes tree networking can look us up by
	 * node number and try to use our address and port attributes
	 * to connect to this node.. make sure that they've been set
	 * before writing the node attribute? */
	if (!test_bit(O2NM_NODE_ATTR_ADDRESS, &node->nd_set_attributes) ||
	    !test_bit(O2NM_NODE_ATTR_PORT, &node->nd_set_attributes))
		return -EINVAL; /* XXX */

	o2nm_lock_subsystem();
	cluster = to_o2nm_cluster_from_node(node);
	if (!cluster) {
		o2nm_unlock_subsystem();
		return -EINVAL;
	}

	write_lock(&cluster->cl_nodes_lock);
	if (cluster->cl_nodes[tmp])
		ret = -EEXIST;
	else if (test_and_set_bit(O2NM_NODE_ATTR_NUM,
			&node->nd_set_attributes))
		ret = -EBUSY;
	else  {
		cluster->cl_nodes[tmp] = node;
		node->nd_num = tmp;
		set_bit(tmp, cluster->cl_nodes_bitmap);
	}
	write_unlock(&cluster->cl_nodes_lock);
	o2nm_unlock_subsystem();

	if (ret)
		return ret;

	return count;
}
static ssize_t o2nm_node_ipv4_port_show(struct config_item *item, char *page)
{
	return sprintf(page, "%u\n", ntohs(to_o2nm_node(item)->nd_ipv4_port));
}

static ssize_t o2nm_node_ipv4_port_store(struct config_item *item,
					 const char *page, size_t count)
{
	struct o2nm_node *node = to_o2nm_node(item);
	unsigned long tmp;
	char *p = (char *)page;

	tmp = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	if (tmp == 0)
		return -EINVAL;
	if (tmp >= (u16)-1)
		return -ERANGE;

	if (test_and_set_bit(O2NM_NODE_ATTR_PORT, &node->nd_set_attributes))
		return -EBUSY;
	node->nd_ipv4_port = htons(tmp);

	return count;
}

static ssize_t o2nm_node_ipv4_address_show(struct config_item *item, char *page)
{
	return sprintf(page, "%pI4\n", &to_o2nm_node(item)->nd_ipv4_address);
}

static ssize_t o2nm_node_ipv4_address_store(struct config_item *item,
					    const char *page,
					    size_t count)
{
	struct o2nm_node *node = to_o2nm_node(item);
	struct o2nm_cluster *cluster;
	int ret, i;
	struct rb_node **p, *parent;
	unsigned int octets[4];
	__be32 ipv4_addr = 0;

	ret = sscanf(page, "%3u.%3u.%3u.%3u", &octets[3], &octets[2],
		     &octets[1], &octets[0]);
	if (ret != 4)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(octets); i++) {
		if (octets[i] > 255)
			return -ERANGE;
		be32_add_cpu(&ipv4_addr, octets[i] << (i * 8));
	}

	o2nm_lock_subsystem();
	cluster = to_o2nm_cluster_from_node(node);
	if (!cluster) {
		o2nm_unlock_subsystem();
		return -EINVAL;
	}

	ret = 0;
	write_lock(&cluster->cl_nodes_lock);
	if (o2nm_node_ip_tree_lookup(cluster, ipv4_addr, &p, &parent))
		ret = -EEXIST;
	else if (test_and_set_bit(O2NM_NODE_ATTR_ADDRESS,
			&node->nd_set_attributes))
		ret = -EBUSY;
	else {
		rb_link_node(&node->nd_ip_node, parent, p);
		rb_insert_color(&node->nd_ip_node, &cluster->cl_node_ip_tree);
	}
	write_unlock(&cluster->cl_nodes_lock);
	o2nm_unlock_subsystem();

	if (ret)
		return ret;

	memcpy(&node->nd_ipv4_address, &ipv4_addr, sizeof(ipv4_addr));

	return count;
}

static ssize_t o2nm_node_local_show(struct config_item *item, char *page)
{
	return sprintf(page, "%d\n", to_o2nm_node(item)->nd_local);
}

static ssize_t o2nm_node_local_store(struct config_item *item, const char *page,
				     size_t count)
{
	struct o2nm_node *node = to_o2nm_node(item);
	struct o2nm_cluster *cluster;
	unsigned long tmp;
	char *p = (char *)page;
	ssize_t ret;

	tmp = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	tmp = !!tmp; /* boolean of whether this node wants to be local */

	/* setting local turns on networking rx for now so we require having
	 * set everything else first */
	if (!test_bit(O2NM_NODE_ATTR_ADDRESS, &node->nd_set_attributes) ||
	    !test_bit(O2NM_NODE_ATTR_NUM, &node->nd_set_attributes) ||
	    !test_bit(O2NM_NODE_ATTR_PORT, &node->nd_set_attributes))
		return -EINVAL; /* XXX */

	o2nm_lock_subsystem();
	cluster = to_o2nm_cluster_from_node(node);
	if (!cluster) {
		ret = -EINVAL;
		goto out;
	}

	/* the only failure case is trying to set a new local node
	 * when a different one is already set */
	if (tmp && tmp == cluster->cl_has_local &&
	    cluster->cl_local_node != node->nd_num) {
		ret = -EBUSY;
		goto out;
	}

	/* bring up the rx thread if we're setting the new local node. */
	if (tmp && !cluster->cl_has_local) {
		ret = o2net_start_listening(node);
		if (ret)
			goto out;
	}

	if (!tmp && cluster->cl_has_local &&
	    cluster->cl_local_node == node->nd_num) {
		o2net_stop_listening(node);
		cluster->cl_local_node = O2NM_INVALID_NODE_NUM;
	}

	node->nd_local = tmp;
	if (node->nd_local) {
		cluster->cl_has_local = tmp;
		cluster->cl_local_node = node->nd_num;
	}

	ret = count;

out:
	o2nm_unlock_subsystem();
	return ret;
}

CONFIGFS_ATTR(o2nm_node_, num);
CONFIGFS_ATTR(o2nm_node_, ipv4_port);
CONFIGFS_ATTR(o2nm_node_, ipv4_address);
CONFIGFS_ATTR(o2nm_node_, local);

static struct configfs_attribute *o2nm_node_attrs[] = {
	&o2nm_node_attr_num,
	&o2nm_node_attr_ipv4_port,
	&o2nm_node_attr_ipv4_address,
	&o2nm_node_attr_local,
	NULL,
};

static struct configfs_item_operations o2nm_node_item_ops = {
	.release		= o2nm_node_release,
};

static const struct config_item_type o2nm_node_type = {
	.ct_item_ops	= &o2nm_node_item_ops,
	.ct_attrs	= o2nm_node_attrs,
	.ct_owner	= THIS_MODULE,
};

/* node set */

struct o2nm_node_group {
	struct config_group ns_group;
	/* some stuff? */
};

#if 0
static struct o2nm_node_group *to_o2nm_node_group(struct config_group *group)
{
	return group ?
		container_of(group, struct o2nm_node_group, ns_group)
		: NULL;
}
#endif

static ssize_t o2nm_cluster_attr_write(const char *page, ssize_t count,
                                       unsigned int *val)
{
	unsigned long tmp;
	char *p = (char *)page;

	tmp = simple_strtoul(p, &p, 0);
	if (!p || (*p && (*p != '\n')))
		return -EINVAL;

	if (tmp == 0)
		return -EINVAL;
	if (tmp >= (u32)-1)
		return -ERANGE;

	*val = tmp;

	return count;
}

static ssize_t o2nm_cluster_idle_timeout_ms_show(struct config_item *item,
	char *page)
{
	return sprintf(page, "%u\n", to_o2nm_cluster(item)->cl_idle_timeout_ms);
}

static ssize_t o2nm_cluster_idle_timeout_ms_store(struct config_item *item,
	const char *page, size_t count)
{
	struct o2nm_cluster *cluster = to_o2nm_cluster(item);
	ssize_t ret;
	unsigned int val;

	ret =  o2nm_cluster_attr_write(page, count, &val);

	if (ret > 0) {
		if (cluster->cl_idle_timeout_ms != val
			&& o2net_num_connected_peers()) {
			mlog(ML_NOTICE,
			     "o2net: cannot change idle timeout after "
			     "the first peer has agreed to it."
			     "  %d connected peers\n",
			     o2net_num_connected_peers());
			ret = -EINVAL;
		} else if (val <= cluster->cl_keepalive_delay_ms) {
			mlog(ML_NOTICE, "o2net: idle timeout must be larger "
			     "than keepalive delay\n");
			ret = -EINVAL;
		} else {
			cluster->cl_idle_timeout_ms = val;
		}
	}

	return ret;
}

static ssize_t o2nm_cluster_keepalive_delay_ms_show(
	struct config_item *item, char *page)
{
	return sprintf(page, "%u\n",
			to_o2nm_cluster(item)->cl_keepalive_delay_ms);
}

static ssize_t o2nm_cluster_keepalive_delay_ms_store(
	struct config_item *item, const char *page, size_t count)
{
	struct o2nm_cluster *cluster = to_o2nm_cluster(item);
	ssize_t ret;
	unsigned int val;

	ret =  o2nm_cluster_attr_write(page, count, &val);

	if (ret > 0) {
		if (cluster->cl_keepalive_delay_ms != val
		    && o2net_num_connected_peers()) {
			mlog(ML_NOTICE,
			     "o2net: cannot change keepalive delay after"
			     " the first peer has agreed to it."
			     "  %d connected peers\n",
			     o2net_num_connected_peers());
			ret = -EINVAL;
		} else if (val >= cluster->cl_idle_timeout_ms) {
			mlog(ML_NOTICE, "o2net: keepalive delay must be "
			     "smaller than idle timeout\n");
			ret = -EINVAL;
		} else {
			cluster->cl_keepalive_delay_ms = val;
		}
	}

	return ret;
}

static ssize_t o2nm_cluster_reconnect_delay_ms_show(
	struct config_item *item, char *page)
{
	return sprintf(page, "%u\n",
			to_o2nm_cluster(item)->cl_reconnect_delay_ms);
}

static ssize_t o2nm_cluster_reconnect_delay_ms_store(
	struct config_item *item, const char *page, size_t count)
{
	return o2nm_cluster_attr_write(page, count,
                               &to_o2nm_cluster(item)->cl_reconnect_delay_ms);
}

static ssize_t o2nm_cluster_fence_method_show(
	struct config_item *item, char *page)
{
	struct o2nm_cluster *cluster = to_o2nm_cluster(item);
	ssize_t ret = 0;

	if (cluster)
		ret = sprintf(page, "%s\n",
			      o2nm_fence_method_desc[cluster->cl_fence_method]);
	return ret;
}

static ssize_t o2nm_cluster_fence_method_store(
	struct config_item *item, const char *page, size_t count)
{
	unsigned int i;

	if (page[count - 1] != '\n')
		goto bail;

	for (i = 0; i < O2NM_FENCE_METHODS; ++i) {
		if (count != strlen(o2nm_fence_method_desc[i]) + 1)
			continue;
		if (strncasecmp(page, o2nm_fence_method_desc[i], count - 1))
			continue;
		if (to_o2nm_cluster(item)->cl_fence_method != i) {
			printk(KERN_INFO "ocfs2: Changing fence method to %s\n",
			       o2nm_fence_method_desc[i]);
			to_o2nm_cluster(item)->cl_fence_method = i;
		}
		return count;
	}

bail:
	return -EINVAL;
}

CONFIGFS_ATTR(o2nm_cluster_, idle_timeout_ms);
CONFIGFS_ATTR(o2nm_cluster_, keepalive_delay_ms);
CONFIGFS_ATTR(o2nm_cluster_, reconnect_delay_ms);
CONFIGFS_ATTR(o2nm_cluster_, fence_method);

static struct configfs_attribute *o2nm_cluster_attrs[] = {
	&o2nm_cluster_attr_idle_timeout_ms,
	&o2nm_cluster_attr_keepalive_delay_ms,
	&o2nm_cluster_attr_reconnect_delay_ms,
	&o2nm_cluster_attr_fence_method,
	NULL,
};

static struct config_item *o2nm_node_group_make_item(struct config_group *group,
						     const char *name)
{
	struct o2nm_node *node = NULL;

	if (strlen(name) > O2NM_MAX_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	node = kzalloc(sizeof(struct o2nm_node), GFP_KERNEL);
	if (node == NULL)
		return ERR_PTR(-ENOMEM);

	strcpy(node->nd_name, name); /* use item.ci_namebuf instead? */
	config_item_init_type_name(&node->nd_item, name, &o2nm_node_type);
	spin_lock_init(&node->nd_lock);

	mlog(ML_CLUSTER, "o2nm: Registering node %s\n", name);

	return &node->nd_item;
}

static void o2nm_node_group_drop_item(struct config_group *group,
				      struct config_item *item)
{
	struct o2nm_node *node = to_o2nm_node(item);
	struct o2nm_cluster *cluster = to_o2nm_cluster(group->cg_item.ci_parent);

	if (cluster->cl_nodes[node->nd_num] == node) {
		o2net_disconnect_node(node);

		if (cluster->cl_has_local &&
		    (cluster->cl_local_node == node->nd_num)) {
			cluster->cl_has_local = 0;
			cluster->cl_local_node = O2NM_INVALID_NODE_NUM;
			o2net_stop_listening(node);
		}
	}

	/* XXX call into net to stop this node from trading messages */

	write_lock(&cluster->cl_nodes_lock);

	/* XXX sloppy */
	if (node->nd_ipv4_address)
		rb_erase(&node->nd_ip_node, &cluster->cl_node_ip_tree);

	/* nd_num might be 0 if the node number hasn't been set.. */
	if (cluster->cl_nodes[node->nd_num] == node) {
		cluster->cl_nodes[node->nd_num] = NULL;
		clear_bit(node->nd_num, cluster->cl_nodes_bitmap);
	}
	write_unlock(&cluster->cl_nodes_lock);

	mlog(ML_CLUSTER, "o2nm: Unregistered node %s\n",
	     config_item_name(&node->nd_item));

	config_item_put(item);
}

static struct configfs_group_operations o2nm_node_group_group_ops = {
	.make_item	= o2nm_node_group_make_item,
	.drop_item	= o2nm_node_group_drop_item,
};

static const struct config_item_type o2nm_node_group_type = {
	.ct_group_ops	= &o2nm_node_group_group_ops,
	.ct_owner	= THIS_MODULE,
};

/* cluster */

static void o2nm_cluster_release(struct config_item *item)
{
	struct o2nm_cluster *cluster = to_o2nm_cluster(item);

	kfree(cluster);
}

static struct configfs_item_operations o2nm_cluster_item_ops = {
	.release	= o2nm_cluster_release,
};

static const struct config_item_type o2nm_cluster_type = {
	.ct_item_ops	= &o2nm_cluster_item_ops,
	.ct_attrs	= o2nm_cluster_attrs,
	.ct_owner	= THIS_MODULE,
};

/* cluster set */

struct o2nm_cluster_group {
	struct configfs_subsystem cs_subsys;
	/* some stuff? */
};

#if 0
static struct o2nm_cluster_group *to_o2nm_cluster_group(struct config_group *group)
{
	return group ?
		container_of(to_configfs_subsystem(group), struct o2nm_cluster_group, cs_subsys)
	       : NULL;
}
#endif

static struct config_group *o2nm_cluster_group_make_group(struct config_group *group,
							  const char *name)
{
	struct o2nm_cluster *cluster = NULL;
	struct o2nm_node_group *ns = NULL;
	struct config_group *o2hb_group = NULL, *ret = NULL;

	/* this runs under the parent dir's i_rwsem; there can be only
	 * one caller in here at a time */
	if (o2nm_single_cluster)
		return ERR_PTR(-ENOSPC);

	cluster = kzalloc(sizeof(struct o2nm_cluster), GFP_KERNEL);
	ns = kzalloc(sizeof(struct o2nm_node_group), GFP_KERNEL);
	o2hb_group = o2hb_alloc_hb_set();
	if (cluster == NULL || ns == NULL || o2hb_group == NULL)
		goto out;

	config_group_init_type_name(&cluster->cl_group, name,
				    &o2nm_cluster_type);
	configfs_add_default_group(&ns->ns_group, &cluster->cl_group);

	config_group_init_type_name(&ns->ns_group, "node",
				    &o2nm_node_group_type);
	configfs_add_default_group(o2hb_group, &cluster->cl_group);

	rwlock_init(&cluster->cl_nodes_lock);
	cluster->cl_node_ip_tree = RB_ROOT;
	cluster->cl_reconnect_delay_ms = O2NET_RECONNECT_DELAY_MS_DEFAULT;
	cluster->cl_idle_timeout_ms    = O2NET_IDLE_TIMEOUT_MS_DEFAULT;
	cluster->cl_keepalive_delay_ms = O2NET_KEEPALIVE_DELAY_MS_DEFAULT;
	cluster->cl_fence_method       = O2NM_FENCE_RESET;

	ret = &cluster->cl_group;
	o2nm_single_cluster = cluster;

out:
	if (ret == NULL) {
		kfree(cluster);
		kfree(ns);
		o2hb_free_hb_set(o2hb_group);
		ret = ERR_PTR(-ENOMEM);
	}

	return ret;
}

static void o2nm_cluster_group_drop_item(struct config_group *group, struct config_item *item)
{
	struct o2nm_cluster *cluster = to_o2nm_cluster(item);

	BUG_ON(o2nm_single_cluster != cluster);
	o2nm_single_cluster = NULL;

	configfs_remove_default_groups(&cluster->cl_group);
	config_item_put(item);
}

static struct configfs_group_operations o2nm_cluster_group_group_ops = {
	.make_group	= o2nm_cluster_group_make_group,
	.drop_item	= o2nm_cluster_group_drop_item,
};

static const struct config_item_type o2nm_cluster_group_type = {
	.ct_group_ops	= &o2nm_cluster_group_group_ops,
	.ct_owner	= THIS_MODULE,
};

static struct o2nm_cluster_group o2nm_cluster_group = {
	.cs_subsys = {
		.su_group = {
			.cg_item = {
				.ci_namebuf = "cluster",
				.ci_type = &o2nm_cluster_group_type,
			},
		},
	},
};

static inline void o2nm_lock_subsystem(void)
{
	mutex_lock(&o2nm_cluster_group.cs_subsys.su_mutex);
}

static inline void o2nm_unlock_subsystem(void)
{
	mutex_unlock(&o2nm_cluster_group.cs_subsys.su_mutex);
}

int o2nm_depend_item(struct config_item *item)
{
	return configfs_depend_item(&o2nm_cluster_group.cs_subsys, item);
}

void o2nm_undepend_item(struct config_item *item)
{
	configfs_undepend_item(item);
}

int o2nm_depend_this_node(void)
{
	int ret = 0;
	struct o2nm_node *local_node;

	local_node = o2nm_get_node_by_num(o2nm_this_node());
	if (!local_node) {
		ret = -EINVAL;
		goto out;
	}

	ret = o2nm_depend_item(&local_node->nd_item);
	o2nm_node_put(local_node);

out:
	return ret;
}

void o2nm_undepend_this_node(void)
{
	struct o2nm_node *local_node;

	local_node = o2nm_get_node_by_num(o2nm_this_node());
	BUG_ON(!local_node);

	o2nm_undepend_item(&local_node->nd_item);
	o2nm_node_put(local_node);
}


static void __exit exit_o2nm(void)
{
	/* XXX sync with hb callbacks and shut down hb? */
	o2net_unregister_hb_callbacks();
	configfs_unregister_subsystem(&o2nm_cluster_group.cs_subsys);
	o2cb_sys_shutdown();

	o2net_exit();
	o2hb_exit();
}

static int __init init_o2nm(void)
{
	int ret;

	o2hb_init();

	ret = o2net_init();
	if (ret)
		goto out_o2hb;

	ret = o2net_register_hb_callbacks();
	if (ret)
		goto out_o2net;

	config_group_init(&o2nm_cluster_group.cs_subsys.su_group);
	mutex_init(&o2nm_cluster_group.cs_subsys.su_mutex);
	ret = configfs_register_subsystem(&o2nm_cluster_group.cs_subsys);
	if (ret) {
		printk(KERN_ERR "nodemanager: Registration returned %d\n", ret);
		goto out_callbacks;
	}

	ret = o2cb_sys_init();
	if (!ret)
		goto out;

	configfs_unregister_subsystem(&o2nm_cluster_group.cs_subsys);
out_callbacks:
	o2net_unregister_hb_callbacks();
out_o2net:
	o2net_exit();
out_o2hb:
	o2hb_exit();
out:
	return ret;
}

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("OCFS2 cluster management");

module_init(init_o2nm)
module_exit(exit_o2nm)
