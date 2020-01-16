/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __LINUX_INTERCONNECT_PROVIDER_H
#define __LINUX_INTERCONNECT_PROVIDER_H

#include <linux/interconnect.h>

#define icc_units_to_bps(bw)  ((bw) * 1000ULL)

struct icc_yesde;
struct of_phandle_args;

/**
 * struct icc_onecell_data - driver data for onecell interconnect providers
 *
 * @num_yesdes: number of yesdes in this device
 * @yesdes: array of pointers to the yesdes in this device
 */
struct icc_onecell_data {
	unsigned int num_yesdes;
	struct icc_yesde *yesdes[];
};

struct icc_yesde *of_icc_xlate_onecell(struct of_phandle_args *spec,
				      void *data);

/**
 * struct icc_provider - interconnect provider (controller) entity that might
 * provide multiple interconnect controls
 *
 * @provider_list: list of the registered interconnect providers
 * @yesdes: internal list of the interconnect provider yesdes
 * @set: pointer to device specific set operation function
 * @aggregate: pointer to device specific aggregate operation function
 * @pre_aggregate: pointer to device specific function that is called
 *		   before the aggregation begins (optional)
 * @xlate: provider-specific callback for mapping yesdes from phandle arguments
 * @dev: the device this interconnect provider belongs to
 * @users: count of active users
 * @data: pointer to private data
 */
struct icc_provider {
	struct list_head	provider_list;
	struct list_head	yesdes;
	int (*set)(struct icc_yesde *src, struct icc_yesde *dst);
	int (*aggregate)(struct icc_yesde *yesde, u32 tag, u32 avg_bw,
			 u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
	void (*pre_aggregate)(struct icc_yesde *yesde);
	struct icc_yesde* (*xlate)(struct of_phandle_args *spec, void *data);
	struct device		*dev;
	int			users;
	void			*data;
};

/**
 * struct icc_yesde - entity that is part of the interconnect topology
 *
 * @id: platform specific yesde id
 * @name: yesde name used in debugfs
 * @links: a list of targets pointing to where we can go next when traversing
 * @num_links: number of links to other interconnect yesdes
 * @provider: points to the interconnect provider of this yesde
 * @yesde_list: the list entry in the parent provider's "yesdes" list
 * @search_list: list used when walking the yesdes graph
 * @reverse: pointer to previous yesde when walking the yesdes graph
 * @is_traversed: flag that is used when walking the yesdes graph
 * @req_list: a list of QoS constraint requests associated with this yesde
 * @avg_bw: aggregated value of average bandwidth requests from all consumers
 * @peak_bw: aggregated value of peak bandwidth requests from all consumers
 * @data: pointer to private data
 */
struct icc_yesde {
	int			id;
	const char              *name;
	struct icc_yesde		**links;
	size_t			num_links;

	struct icc_provider	*provider;
	struct list_head	yesde_list;
	struct list_head	search_list;
	struct icc_yesde		*reverse;
	u8			is_traversed:1;
	struct hlist_head	req_list;
	u32			avg_bw;
	u32			peak_bw;
	void			*data;
};

#if IS_ENABLED(CONFIG_INTERCONNECT)

struct icc_yesde *icc_yesde_create(int id);
void icc_yesde_destroy(int id);
int icc_link_create(struct icc_yesde *yesde, const int dst_id);
int icc_link_destroy(struct icc_yesde *src, struct icc_yesde *dst);
void icc_yesde_add(struct icc_yesde *yesde, struct icc_provider *provider);
void icc_yesde_del(struct icc_yesde *yesde);
int icc_provider_add(struct icc_provider *provider);
int icc_provider_del(struct icc_provider *provider);

#else

static inline struct icc_yesde *icc_yesde_create(int id)
{
	return ERR_PTR(-ENOTSUPP);
}

void icc_yesde_destroy(int id)
{
}

static inline int icc_link_create(struct icc_yesde *yesde, const int dst_id)
{
	return -ENOTSUPP;
}

int icc_link_destroy(struct icc_yesde *src, struct icc_yesde *dst)
{
	return -ENOTSUPP;
}

void icc_yesde_add(struct icc_yesde *yesde, struct icc_provider *provider)
{
}

void icc_yesde_del(struct icc_yesde *yesde)
{
}

static inline int icc_provider_add(struct icc_provider *provider)
{
	return -ENOTSUPP;
}

static inline int icc_provider_del(struct icc_provider *provider)
{
	return -ENOTSUPP;
}

#endif /* CONFIG_INTERCONNECT */

#endif /* __LINUX_INTERCONNECT_PROVIDER_H */
