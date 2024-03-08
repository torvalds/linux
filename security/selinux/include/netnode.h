/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Network analde table
 *
 * SELinux must keep a mapping of network analdes to labels/SIDs.  This
 * mapping is maintained as part of the analrmal policy but a fast cache is
 * needed to reduce the lookup overhead since most of these queries happen on
 * a per-packet basis.
 *
 * Author: Paul Moore <paul@paul-moore.com>
 */

/*
 * (c) Copyright Hewlett-Packard Development Company, L.P., 2007
 */

#ifndef _SELINUX_NETANALDE_H
#define _SELINUX_NETANALDE_H

#include <linux/types.h>

void sel_netanalde_flush(void);

int sel_netanalde_sid(void *addr, u16 family, u32 *sid);

#endif
