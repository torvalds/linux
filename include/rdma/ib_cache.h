/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 */

#ifndef _IB_CACHE_H
#define _IB_CACHE_H

#include <rdma/ib_verbs.h>

int rdma_query_gid(struct ib_device *device, u8 port_num, int index,
		   union ib_gid *gid);
void *rdma_read_gid_hw_context(const struct ib_gid_attr *attr);
const struct ib_gid_attr *rdma_find_gid(struct ib_device *device,
					const union ib_gid *gid,
					enum ib_gid_type gid_type,
					struct net_device *ndev);
const struct ib_gid_attr *rdma_find_gid_by_port(struct ib_device *ib_dev,
						const union ib_gid *gid,
						enum ib_gid_type gid_type,
						u8 port,
						struct net_device *ndev);
const struct ib_gid_attr *rdma_find_gid_by_filter(
	struct ib_device *device, const union ib_gid *gid, u8 port_num,
	bool (*filter)(const union ib_gid *gid, const struct ib_gid_attr *,
		       void *),
	void *context);

int rdma_read_gid_l2_fields(const struct ib_gid_attr *attr,
			    u16 *vlan_id, u8 *smac);
struct net_device *rdma_read_gid_attr_ndev_rcu(const struct ib_gid_attr *attr);

/**
 * ib_get_cached_pkey - Returns a cached PKey table entry
 * @device: The device to query.
 * @port_num: The port number of the device to query.
 * @index: The index into the cached PKey table to query.
 * @pkey: The PKey value found at the specified index.
 *
 * ib_get_cached_pkey() fetches the specified PKey table entry stored in
 * the local software cache.
 */
int ib_get_cached_pkey(struct ib_device    *device_handle,
		       u8                   port_num,
		       int                  index,
		       u16                 *pkey);

/**
 * ib_find_cached_pkey - Returns the PKey table index where a specified
 *   PKey value occurs.
 * @device: The device to query.
 * @port_num: The port number of the device to search for the PKey.
 * @pkey: The PKey value to search for.
 * @index: The index into the cached PKey table where the PKey was found.
 *
 * ib_find_cached_pkey() searches the specified PKey table in
 * the local software cache.
 */
int ib_find_cached_pkey(struct ib_device    *device,
			u8                   port_num,
			u16                  pkey,
			u16                 *index);

/**
 * ib_find_exact_cached_pkey - Returns the PKey table index where a specified
 *   PKey value occurs. Comparison uses the FULL 16 bits (incl membership bit)
 * @device: The device to query.
 * @port_num: The port number of the device to search for the PKey.
 * @pkey: The PKey value to search for.
 * @index: The index into the cached PKey table where the PKey was found.
 *
 * ib_find_exact_cached_pkey() searches the specified PKey table in
 * the local software cache.
 */
int ib_find_exact_cached_pkey(struct ib_device    *device,
			      u8                   port_num,
			      u16                  pkey,
			      u16                 *index);

/**
 * ib_get_cached_lmc - Returns a cached lmc table entry
 * @device: The device to query.
 * @port_num: The port number of the device to query.
 * @lmc: The lmc value for the specified port for that device.
 *
 * ib_get_cached_lmc() fetches the specified lmc table entry stored in
 * the local software cache.
 */
int ib_get_cached_lmc(struct ib_device *device,
		      u8                port_num,
		      u8                *lmc);

/**
 * ib_get_cached_port_state - Returns a cached port state table entry
 * @device: The device to query.
 * @port_num: The port number of the device to query.
 * @port_state: port_state for the specified port for that device.
 *
 * ib_get_cached_port_state() fetches the specified port_state table entry stored in
 * the local software cache.
 */
int ib_get_cached_port_state(struct ib_device *device,
			      u8                port_num,
			      enum ib_port_state *port_active);

bool rdma_is_zero_gid(const union ib_gid *gid);
const struct ib_gid_attr *rdma_get_gid_attr(struct ib_device *device,
					    u8 port_num, int index);
void rdma_put_gid_attr(const struct ib_gid_attr *attr);
void rdma_hold_gid_attr(const struct ib_gid_attr *attr);
ssize_t rdma_query_gid_table(struct ib_device *device,
			     struct ib_uverbs_gid_entry *entries,
			     size_t max_entries);

#endif /* _IB_CACHE_H */
