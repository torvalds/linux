/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2011-2013 Autronica Fire and Security AS
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * Author(s):
 *	2011-2013 Arvid Brodin, arvid.brodin@xdin.com
 */

#ifndef __UAPI_HSR_NETLINK_H
#define __UAPI_HSR_NETLINK_H

/* Generic Netlink HSR family definition
 */

/* attributes */
enum {
	HSR_A_UNSPEC,
	HSR_A_NODE_ADDR,
	HSR_A_IFINDEX,
	HSR_A_IF1_AGE,
	HSR_A_IF2_AGE,
	HSR_A_NODE_ADDR_B,
	HSR_A_IF1_SEQ,
	HSR_A_IF2_SEQ,
	HSR_A_IF1_IFINDEX,
	HSR_A_IF2_IFINDEX,
	HSR_A_ADDR_B_IFINDEX,
	__HSR_A_MAX,
};
#define HSR_A_MAX (__HSR_A_MAX - 1)


/* commands */
enum {
	HSR_C_UNSPEC,
	HSR_C_RING_ERROR,
	HSR_C_NODE_DOWN,
	HSR_C_GET_NODE_STATUS,
	HSR_C_SET_NODE_STATUS,
	HSR_C_GET_NODE_LIST,
	HSR_C_SET_NODE_LIST,
	__HSR_C_MAX,
};
#define HSR_C_MAX (__HSR_C_MAX - 1)

#endif /* __UAPI_HSR_NETLINK_H */
