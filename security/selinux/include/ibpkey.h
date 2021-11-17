/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * pkey table
 *
 * SELinux must keep a mapping of pkeys to labels/SIDs.  This
 * mapping is maintained as part of the normal policy but a fast cache is
 * needed to reduce the lookup overhead.
 */

/*
 * (c) Mellanox Technologies, 2016
 */

#ifndef _SELINUX_IB_PKEY_H
#define _SELINUX_IB_PKEY_H

#ifdef CONFIG_SECURITY_INFINIBAND
void sel_ib_pkey_flush(void);
int sel_ib_pkey_sid(u64 subnet_prefix, u16 pkey, u32 *sid);
#else
static inline void sel_ib_pkey_flush(void)
{
	return;
}
static inline int sel_ib_pkey_sid(u64 subnet_prefix, u16 pkey, u32 *sid)
{
	*sid = SECINITSID_UNLABELED;
	return 0;
}
#endif

#endif
