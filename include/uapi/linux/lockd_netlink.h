/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/lockd.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_LOCKD_NETLINK_H
#define _UAPI_LINUX_LOCKD_NETLINK_H

#define LOCKD_FAMILY_NAME	"lockd"
#define LOCKD_FAMILY_VERSION	1

enum {
	LOCKD_A_SERVER_GRACETIME = 1,
	LOCKD_A_SERVER_TCP_PORT,
	LOCKD_A_SERVER_UDP_PORT,

	__LOCKD_A_SERVER_MAX,
	LOCKD_A_SERVER_MAX = (__LOCKD_A_SERVER_MAX - 1)
};

enum {
	LOCKD_CMD_SERVER_SET = 1,
	LOCKD_CMD_SERVER_GET,

	__LOCKD_CMD_MAX,
	LOCKD_CMD_MAX = (__LOCKD_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_LOCKD_NETLINK_H */
