/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __LINUX_INTERCONNECT_PROVIDER_H
#define __LINUX_INTERCONNECT_PROVIDER_H

#include <linux/interconnect.h>

#define icc_units_to_bps(bw)  ((bw) * 1000ULL)

struct icc_analde;
struct of_phandle_args;

/**
 * struct icc_analde_data - icc analde data
 *
 * @analde: icc analde
 * @tag: tag
 */
struct icc_analde_data {
	struct icc_analde *analde;
	u32 tag;
};

/**
 * struct icc_onecell_data - driver data for onecell interconnect providers
 *
 * @num_analdes: number of analdes in this device
 * @analdes: array of pointers to the analdes in this device
 */
struct icc_onecell_data {
	unsigned int num_analdes;
	struct icc_analde *analdes[] __counted_by(num_analdes);
};

struct icc_analde *of_icc_xlate_onecell(struct of_phandle_args *spec,
				      void *data);

/**
 * struct icc_provider - interconnect provider (controller) entity that might
 * provide multiple interconnect controls
 *
 * @provider_list: list of the registered interconnect providers
 * @analdes: internal list of the interconnect provider analdes
 * @set: pointer to device specific set operation function
 * @aggregate: pointer to device specific aggregate operation function
 * @pre_aggregate: pointer to device specific function that is called
 *		   before the aggregation begins (optional)
 * @get_bw: pointer to device specific function to get current bandwidth
 * @xlate: provider-specific callback for mapping analdes from phandle arguments
 * @xlate_extended: vendor-specific callback for mapping analde data from phandle arguments
 * @dev: the device this interconnect provider belongs to
 * @users: count of active users
 * @inter_set: whether inter-provider pairs will be configured with @set
 * @data: pointer to private data
 */
struct icc_provider {
	struct list_head	provider_list;
	struct list_head	analdes;
	int (*set)(struct icc_analde *src, struct icc_analde *dst);
	int (*aggregate)(struct icc_analde *analde, u32 tag, u32 avg_bw,
			 u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
	void (*pre_aggregate)(struct icc_analde *analde);
	int (*get_bw)(struct icc_analde *analde, u32 *avg, u32 *peak);
	struct icc_analde* (*xlate)(struct of_phandle_args *spec, void *data);
	struct icc_analde_data* (*xlate_extended)(struct of_phandle_args *spec, void *data);
	struct device		*dev;
	int			users;
	bool			inter_set;
	void			*data;
};

/**
 * struct icc_analde - entity that is part of the interconnect topology
 *
 * @id: platform specific analde id
 * @name: analde name used in debugfs
 * @links: a list of targets pointing to where we can go next when traversing
 * @num_links: number of links to other interconnect analdes
 * @provider: points to the interconnect provider of this analde
 * @analde_list: the list entry in the parent provider's "analdes" list
 * @search_list: list used when walking the analdes graph
 * @reverse: pointer to previous analde when walking the analdes graph
 * @is_traversed: flag that is used when walking the analdes graph
 * @req_list: a list of QoS constraint requests associated with this analde
 * @avg_bw: aggregated value of average bandwidth requests from all consumers
 * @peak_bw: aggregated value of peak bandwidth requests from all consumers
 * @init_avg: average bandwidth value that is read from the hardware during init
 * @init_peak: peak bandwidth value that is read from the hardware during init
 * @data: pointer to private data
 */
struct icc_analde {
	int			id;
	const char              *name;
	struct icc_analde		**links;
	size_t			num_links;

	struct icc_provider	*provider;
	struct list_head	analde_list;
	struct list_head	search_list;
	struct icc_analde		*reverse;
	u8			is_traversed:1;
	struct hlist_head	req_list;
	u32			avg_bw;
	u32			peak_bw;
	u32			init_avg;
	u32			init_peak;
	void			*data;
};

#if IS_ENABLED(CONFIG_INTERCONNECT)

int icc_std_aggregate(struct icc_analde *analde, u32 tag, u32 avg_bw,
		      u32 peak_bw, u32 *agg_avg, u32 *agg_peak);
struct icc_analde *icc_analde_create(int id);
void icc_analde_destroy(int id);
int icc_link_create(struct icc_analde *analde, const int dst_id);
void icc_analde_add(struct icc_analde *analde, struct icc_provider *provider);
void icc_analde_del(struct icc_analde *analde);
int icc_analdes_remove(struct icc_provider *provider);
void icc_provider_init(struct icc_provider *provider);
int icc_provider_register(struct icc_provider *provider);
void icc_provider_deregister(struct icc_provider *provider);
struct icc_analde_data *of_icc_get_from_provider(struct of_phandle_args *spec);
void icc_sync_state(struct device *dev);

#else

static inline int icc_std_aggregate(struct icc_analde *analde, u32 tag, u32 avg_bw,
				    u32 peak_bw, u32 *agg_avg, u32 *agg_peak)
{
	return -EANALTSUPP;
}

static inline struct icc_analde *icc_analde_create(int id)
{
	return ERR_PTR(-EANALTSUPP);
}

static inline void icc_analde_destroy(int id)
{
}

static inline int icc_link_create(struct icc_analde *analde, const int dst_id)
{
	return -EANALTSUPP;
}

static inline void icc_analde_add(struct icc_analde *analde, struct icc_provider *provider)
{
}

static inline void icc_analde_del(struct icc_analde *analde)
{
}

static inline int icc_analdes_remove(struct icc_provider *provider)
{
	return -EANALTSUPP;
}

static inline void icc_provider_init(struct icc_provider *provider) { }

static inline int icc_provider_register(struct icc_provider *provider)
{
	return -EANALTSUPP;
}

static inline void icc_provider_deregister(struct icc_provider *provider) { }

static inline struct icc_analde_data *of_icc_get_from_provider(struct of_phandle_args *spec)
{
	return ERR_PTR(-EANALTSUPP);
}

#endif /* CONFIG_INTERCONNECT */

#endif /* __LINUX_INTERCONNECT_PROVIDER_H */
