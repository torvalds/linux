/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2005 Intel Corporation.  All rights reserved.
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
 *
 * $FreeBSD$
 */

#ifndef IB_USER_SA_H
#define IB_USER_SA_H

#ifdef _KERNEL
#include <linux/types.h>
#else
#include <infiniband/types.h>
#endif

enum {
	IB_PATH_GMP		= 1,
	IB_PATH_PRIMARY		= (1<<1),
	IB_PATH_ALTERNATE	= (1<<2),
	IB_PATH_OUTBOUND	= (1<<3),
	IB_PATH_INBOUND		= (1<<4),
	IB_PATH_INBOUND_REVERSE = (1<<5),
	IB_PATH_BIDIRECTIONAL	= IB_PATH_OUTBOUND | IB_PATH_INBOUND_REVERSE
};

struct ib_path_rec_data {
	__u32	flags;
	__u32	reserved;
	__u32	path_rec[16];
};

struct ib_user_path_rec {
	__u8	dgid[16];
	__u8	sgid[16];
	__be16	dlid;
	__be16	slid;
	__u32	raw_traffic;
	__be32	flow_label;
	__u32	reversible;
	__u32	mtu;
	__be16	pkey;
	__u8	hop_limit;
	__u8	traffic_class;
	__u8	numb_path;
	__u8	sl;
	__u8	mtu_selector;
	__u8	rate_selector;
	__u8	rate;
	__u8	packet_life_time_selector;
	__u8	packet_life_time;
	__u8	preference;
};

#endif /* IB_USER_SA_H */
