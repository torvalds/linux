/* include/uapi/linux/if_pppopns.h
 *
 * Header for PPP on PPTP Network Server / PPPoPNS Socket (RFC 2637)
 *
 * Copyright (C) 2009 Google, Inc.
 * Author: Chia-chi Yeh <chiachi@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _UAPI_LINUX_IF_PPPOPNS_H
#define _UAPI_LINUX_IF_PPPOPNS_H

#include <linux/socket.h>
#include <linux/types.h>

struct sockaddr_pppopns {
	sa_family_t	sa_family;	/* AF_PPPOX */
	unsigned int	sa_protocol;	/* PX_PROTO_OPNS */
	int		tcp_socket;
	__u16		local;
	__u16		remote;
} __attribute__((packed));

#endif /* _UAPI_LINUX_IF_PPPOPNS_H */
