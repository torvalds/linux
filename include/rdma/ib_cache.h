/*
 * Copyright (c) 2004 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Intel Corporation. All rights reserved.
 * Copyright (c) 2005 Sun Microsystems, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _IB_CACHE_H
#define _IB_CACHE_H

#include <rdma/ib_verbs.h>

int rdma_query_gid(struct ib_device *device, u8 port_num, int index,
		   union ib_gid *gid);
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

#endif /* _IB_CACHE_H */
