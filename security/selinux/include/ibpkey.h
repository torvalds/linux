/*
 * pkey table
 *
 * SELinux must keep a mapping of pkeys to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead.
 *
 */

/*
 * (c) Mellanox Technologies, 2016
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _SELINUX_IB_PKEY_H
#define _SELINUX_IB_PKEY_H

void sel_ib_pkey_flush(void);

int sel_ib_pkey_sid(u64 subnet_prefix, u16 pkey, u32 *sid);

#endif
