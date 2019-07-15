/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Network node table
 *
 * SELinux must keep a mapping of network nodes to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead since most of these queries happen on
 * a per-packet basis.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007
 */

#ifndef _SELINUX_NETNODE_H
#define _SELINUX_NETNODE_H

void sel_netnode_flush(void);

int sel_netnode_sid(void *addr, u16 family, u32 *sid);

#endif
