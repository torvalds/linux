/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * Netlink routines for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */


#ifndef _UAPILINUX_CIFS_NETLINK_H
#define _UAPILINUX_CIFS_NETLINK_H

#define CIFS_GENL_NAME			"cifs"
#define CIFS_GENL_VERSION		0x1

#define CIFS_GENL_MCGRP_SWN_NAME	"cifs_mcgrp_swn"

enum cifs_genl_multicast_groups {
	CIFS_GENL_MCGRP_SWN,
};

enum cifs_genl_attributes {
	__CIFS_GENL_ATTR_MAX,
};
#define CIFS_GENL_ATTR_MAX (__CIFS_GENL_ATTR_MAX - 1)

enum cifs_genl_commands {
	__CIFS_GENL_CMD_MAX
};
#define CIFS_GENL_CMD_MAX (__CIFS_GENL_CMD_MAX - 1)

#endif /* _UAPILINUX_CIFS_NETLINK_H */
