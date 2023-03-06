/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __LINUX_INTERCONNECT_PROVIDER_H
#define __LINUX_INTERCONNECT_PROVIDER_H

#include <linux/interconnect.h>

#define icc_units_to_bps(bw)  ((bw) * 1000ULL)

struct icc_node;
struct of_phandle_args;

/**
 * struct icc_node_data - icc node data
 *
 * @node: icc node
 * @tag: tag
 */
struct icc_node_data {
	struct icc_node *node;
	u32 tag;
};

/**
 * struct icc_onecell_data - driver data for onecell interconnect providers
 *
 * @num_nodes: number of nodes in this device
 * @nodes: array of pointers to the nodes in this device
 */
struct icc_onecell_data {
	unsigned int num_nodes;
	struct icc_node *nodes[];
};

struct icc_node *of_icc_xlate_onecell(struct of_phandle_args *spec,
				      void *data);

/**
 * struct icc_provider - interconnect provider (controller) entity that might
 * provide multiple interconnect controls
 *
 * @provider_list: list of the registered interconnect providers
 * @nodes: internal list of the interconnect provider nodes
 * @set: pointer to device specific set operation function
 * @aggregate: pointer to device specific aggregate operation function
 * @pre_aggregate: pointer to device specific function that is called
 *		   before the aggregation begins (optional)
 * @get_bw: pointer to device specific function to get current bandwidth
 * @xlate: provider-specific callback for mapping nodes from phandle arguments
 * @xlate_extended: vendor-specific callback for mapping node data from phandle arguments
 * @dev: the device this interconnect provider belongs to
 * @users: count of active users
 * @inter_set: whether inter-provider pairs will be configured with @set
 * @data: pointer to private data
 */
struct icc_provider {
	struct list_head	provider_list;
	struct list_head	nodes;
	int (*set)(struct icc_node *src, struct icc_node *dst);
	int (*aggregate)(struct icc_node *node, u32 tag, u32 avg_bw,
			 u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
	void (*pre_aggregate)(struct icc_node *node);
	int (*get_bw)(struct icc_node *node, u32 *avg, u32 *peak);
	struct icc_node* (*xlate)(struct of_phandle_args *spec, void *data);
	struct icc_node_data* (*xlate_extended)(struct of_phandle_args *spec, void *data);
	struct device		*dev;
	int			users;
	bool			inter_set;
	void			*data;
};

/**
 * struct icc_node - entity that is part of the interconnect topology
 *
 * @id: platform specific node id
 * @name: node name used in debugfs
 * @links: a list of targets pointing to where we can go next when traversing
 * @num_links: number of links to other interconnect nodes
 * @provider: points to the interconnect provider of this node
 * @node_list: the list entry in the parent provider's "nodes" list
 * @search_list: list used when walking the nodes graph
 * @reverse: pointer to previous node when walking the nodes graph
 * @is_traversed: flag that is used when walking the nodes graph
 * @req_list: a list of QoS constraint requests associated with this node
 * @avg_bw: aggregated value of average bandwidth requests from all consumers
 * @peak_bw: aggregated value of peak bandwidth requests from all consumers
 * @init_avg: average bandwidth value that is read from the hardware during init
 * @init_peak: peak bandwidth value that is read from the hardware during init
 * @data: pointer to private data
 */
struct icc_node {
	int			id;
	const char              *name;
	struct icc_node		**links;
	size_t			num_links;

	struct icc_provider	*provider;
	struct list_head	node_list;
	struct list_head	search_list;
	struct icc_node		*reverse;
	u8			is_traversed:1;
	struct hlist_head	req_list;
	u32			avg_bw;
	u32			peak_bw;
	u32			init_avg;
	u32			init_peak;
	void			*data;
};

#if IS_ENABLED(CONFIG_INTERCONNECT)

int icc_std_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
		      u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
struct icc_node *icc_node_create(int id);
void icc_node_destroy(int id);
int icc_link_create(struct icc_node *node, const int dst_id);
int icc_link_destroy(struct icc_node *src, struct icc_node *dst);
void icc_node_add(struct icc_node *node, struct icc_provider *provider);
void icc_node_del(struct icc_node *node);
int icc_nodes_remove(struct icc_provider *provider);
void icc_provider_init(struct icc_provider *provider);
int icc_provider_register(struct icc_provider *provider);
void icc_provider_deregister(struct icc_provider *provider);
struct icc_node_data *of_icc_get_from_provider(struct of_phandle_args *spec);
void icc_sync_state(struct device *dev);

#else

static inline int icc_std_aggregate(struct icc_node *node, u32 tag, u32 avg_bw,
				    u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	return -ENOTSUPP;
}

static inline struct icc_node *icc_node_create(int id)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline void icc_node_destroy(int id)
{
}

static inline int icc_link_create(struct icc_node *node, const int dst_id)
{
	return -ENOTSUPP;
}

static inline int icc_link_destroy(struct icc_node *src, struct icc_node *dst)
{
	return -ENOTSUPP;
}

static inline void icc_node_add(struct icc_node *node, struct icc_provider *provider)
{
}

static inline void icc_node_del(struct icc_node *node)
{
}

static inline int icc_nodes_remove(struct icc_provider *provider)
{
	return -ENOTSUPP;
}

static inline void icc_provider_init(struct icc_provider *provider) { }

static inline int icc_provider_register(struct icc_provider *provider)
{
	return -ENOTSUPP;
}

static inline void icc_provider_deregister(struct icc_provider *provider) { }

static inline struct icc_node_data *of_icc_get_from_provider(struct of_phandle_args *spec)
{
	return ERR_PTR(-ENOTSUPP);
}

#endif /* CONFIG_INTERCONNECT */

#endif /* __LINUX_INTERCONNECT_PROVIDER_H */
