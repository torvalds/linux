/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
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
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *	  copyright notice, this list of conditions and the following
 *	  disclaimer in the documentation and/or other materials
 *	  provided with the distribution.
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

#ifndef _IW_PORTMAP_H
#define _IW_PORTMAP_H

#define IWPM_ULIBNAME_SIZE	32
#define IWPM_DEVNAME_SIZE	32
#define IWPM_IFNAME_SIZE	16
#define IWPM_IPADDR_SIZE	16

enum {
	IWPM_INVALID_NLMSG_ERR = 10,
	IWPM_CREATE_MAPPING_ERR,
	IWPM_DUPLICATE_MAPPING_ERR,
	IWPM_UNKNOWN_MAPPING_ERR,
	IWPM_CLIENT_DEV_INFO_ERR,
	IWPM_USER_LIB_INFO_ERR,
	IWPM_REMOTE_QUERY_REJECT
};

struct iwpm_dev_data {
	char dev_name[IWPM_DEVNAME_SIZE];
	char if_name[IWPM_IFNAME_SIZE];
};

struct iwpm_sa_data {
	struct sockaddr_storage loc_addr;
	struct sockaddr_storage mapped_loc_addr;
	struct sockaddr_storage rem_addr;
	struct sockaddr_storage mapped_rem_addr;
};

/**
 * iwpm_valid_pid - Check if the userspace iwarp port mapper pid is valid
 *
 * Returns true if the pid is greater than zero, otherwise returns false
 */
int iwpm_valid_pid(void);

#endif /* _IW_PORTMAP_H */
