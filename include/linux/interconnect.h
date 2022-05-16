/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2019, Linaro Ltd.
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#ifndef __LINUX_INTERCONNECT_H
#define __LINUX_INTERCONNECT_H

#include <linux/mutex.h>
#include <linux/types.h>

/* macros for converting to icc units */
#define Bps_to_icc(x)	((x) / 1000)
#define kBps_to_icc(x)	(x)
#define MBps_to_icc(x)	((x) * 1000)
#define GBps_to_icc(x)	((x) * 1000 * 1000)
#define bps_to_icc(x)	(1)
#define kbps_to_icc(x)	((x) / 8 + ((x) % 8 ? 1 : 0))
#define Mbps_to_icc(x)	((x) * 1000 / 8)
#define Gbps_to_icc(x)	((x) * 1000 * 1000 / 8)

struct icc_path;
struct device;

/**
 * struct icc_bulk_data - Data used for bulk icc operations.
 *
 * @path: reference to the interconnect path (internal use)
 * @name: the name from the "interconnect-names" DT property
 * @avg_bw: average bandwidth in icc units
 * @peak_bw: peak bandwidth in icc units
 */
struct icc_bulk_data {
	struct icc_path	*path;
	const char *name;
	u32 avg_bw;
	u32 peak_bw;
};

int __must_check of_icc_bulk_get(struct device *dev, int num_paths,
				 struct icc_bulk_data *paths);
void icc_bulk_put(int num_paths, struct icc_bulk_data *paths);
int icc_bulk_set_bw(int num_paths, const struct icc_bulk_data *paths);
int icc_bulk_enable(int num_paths, const struct icc_bulk_data *paths);
void icc_bulk_disable(int num_paths, const struct icc_bulk_data *paths);

#if IS_ENABLED(CONFIG_INTERCONNECT)

struct icc_path *icc_get(struct device *dev, const int src_id,
			 const int dst_id);
struct icc_path *of_icc_get(struct device *dev, const char *name);
struct icc_path *devm_of_icc_get(struct device *dev, const char *name);
struct icc_path *of_icc_get_by_index(struct device *dev, int idx);
void icc_put(struct icc_path *path);
int icc_enable(struct icc_path *path);
int icc_disable(struct icc_path *path);
int icc_set_bw(struct icc_path *path, u32 avg_bw, u32 peak_bw);
void icc_set_tag(struct icc_path *path, u32 tag);
const char *icc_get_name(struct icc_path *path);

#else

static inline struct icc_path *icc_get(struct device *dev, const int src_id,
				       const int dst_id)
{
	return NULL;
}

static inline struct icc_path *of_icc_get(struct device *dev,
					  const char *name)
{
	return NULL;
}

static inline struct icc_path *devm_of_icc_get(struct device *dev,
						const char *name)
{
	return NULL;
}

static inline struct icc_path *of_icc_get_by_index(struct device *dev, int idx)
{
	return NULL;
}

static inline void icc_put(struct icc_path *path)
{
}

static inline int icc_enable(struct icc_path *path)
{
	return 0;
}

static inline int icc_disable(struct icc_path *path)
{
	return 0;
}

static inline int icc_set_bw(struct icc_path *path, u32 avg_bw, u32 peak_bw)
{
	return 0;
}

static inline void icc_set_tag(struct icc_path *path, u32 tag)
{
}

static inline const char *icc_get_name(struct icc_path *path)
{
	return NULL;
}

#endif /* CONFIG_INTERCONNECT */

#endif /* __LINUX_INTERCONNECT_H */
