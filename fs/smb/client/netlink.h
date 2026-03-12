/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Netlink routines for CIFS
 *
 * Copyright (c) 2020 Samuel Cabrero <scabrero@suse.de>
 */

#ifndef _CIFS_NETLINK_H
#define _CIFS_NETLINK_H

extern struct genl_family cifs_genl_family;

int cifs_genl_init(void);
void cifs_genl_exit(void);

#endif /* _CIFS_NETLINK_H */
