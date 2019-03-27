/*-
 * SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0
 *
 * Copyright (c) 2014 Chelsio, Inc. All rights reserved.
 * Copyright (c) 2014 Intel Corporation. All rights reserved.
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "iwpm_util.h"

#define IWPM_MAPINFO_HASH_SIZE	512
#define IWPM_MAPINFO_HASH_MASK	(IWPM_MAPINFO_HASH_SIZE - 1)
#define IWPM_REMINFO_HASH_SIZE	64
#define IWPM_REMINFO_HASH_MASK	(IWPM_REMINFO_HASH_SIZE - 1)
#define IWPM_MSG_SIZE		512

int iwpm_compare_sockaddr(struct sockaddr_storage *a_sockaddr,
				struct sockaddr_storage *b_sockaddr)
{
	if (a_sockaddr->ss_family != b_sockaddr->ss_family)
		return 1;
	if (a_sockaddr->ss_family == AF_INET) {
		struct sockaddr_in *a4_sockaddr =
			(struct sockaddr_in *)a_sockaddr;
		struct sockaddr_in *b4_sockaddr =
			(struct sockaddr_in *)b_sockaddr;
		if (!memcmp(&a4_sockaddr->sin_addr,
			&b4_sockaddr->sin_addr, sizeof(struct in_addr))
			&& a4_sockaddr->sin_port == b4_sockaddr->sin_port)
				return 0;

	} else if (a_sockaddr->ss_family == AF_INET6) {
		struct sockaddr_in6 *a6_sockaddr =
			(struct sockaddr_in6 *)a_sockaddr;
		struct sockaddr_in6 *b6_sockaddr =
			(struct sockaddr_in6 *)b_sockaddr;
		if (!memcmp(&a6_sockaddr->sin6_addr,
			&b6_sockaddr->sin6_addr, sizeof(struct in6_addr))
			&& a6_sockaddr->sin6_port == b6_sockaddr->sin6_port)
				return 0;

	} else {
		pr_err("%s: Invalid sockaddr family\n", __func__);
	}
	return 1;
}

void iwpm_print_sockaddr(struct sockaddr_storage *sockaddr, char *msg)
{
	struct sockaddr_in6 *sockaddr_v6;
	struct sockaddr_in *sockaddr_v4;

	switch (sockaddr->ss_family) {
	case AF_INET:
		sockaddr_v4 = (struct sockaddr_in *)sockaddr;
		pr_debug("%s IPV4 %pI4: %u(0x%04X)\n",
			msg, &sockaddr_v4->sin_addr,
			ntohs(sockaddr_v4->sin_port),
			ntohs(sockaddr_v4->sin_port));
		break;
	case AF_INET6:
		sockaddr_v6 = (struct sockaddr_in6 *)sockaddr;
		pr_debug("%s IPV6 %pI6: %u(0x%04X)\n",
			msg, &sockaddr_v6->sin6_addr,
			ntohs(sockaddr_v6->sin6_port),
			ntohs(sockaddr_v6->sin6_port));
		break;
	default:
		break;
	}
}

