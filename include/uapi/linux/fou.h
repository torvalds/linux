/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/fou.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_FOU_H
#define _UAPI_LINUX_FOU_H

#define FOU_GENL_NAME		"fou"
#define FOU_GENL_VERSION	1

enum {
	FOU_ENCAP_UNSPEC,
	FOU_ENCAP_DIRECT,
	FOU_ENCAP_GUE,
};

enum {
	FOU_ATTR_UNSPEC,
	FOU_ATTR_PORT,
	FOU_ATTR_AF,
	FOU_ATTR_IPPROTO,
	FOU_ATTR_TYPE,
	FOU_ATTR_REMCSUM_NOPARTIAL,
	FOU_ATTR_LOCAL_V4,
	FOU_ATTR_LOCAL_V6,
	FOU_ATTR_PEER_V4,
	FOU_ATTR_PEER_V6,
	FOU_ATTR_PEER_PORT,
	FOU_ATTR_IFINDEX,

	__FOU_ATTR_MAX
};
#define FOU_ATTR_MAX (__FOU_ATTR_MAX - 1)

enum {
	FOU_CMD_UNSPEC,
	FOU_CMD_ADD,
	FOU_CMD_DEL,
	FOU_CMD_GET,

	__FOU_CMD_MAX
};
#define FOU_CMD_MAX (__FOU_CMD_MAX - 1)

#endif /* _UAPI_LINUX_FOU_H */
