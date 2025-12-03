// SPDX-License-Identifier: GPL-2.0-only
/*
 * SELinux initcalls
 */

#include <linux/init.h>

#include "initcalls.h"

/**
 * selinux_initcall - Perform the SELinux initcalls
 *
 * Used as a device initcall in the SELinux LSM definition.
 */
int __init selinux_initcall(void)
{
	int rc = 0, rc_tmp = 0;

	rc_tmp = init_sel_fs();
	if (!rc && rc_tmp)
		rc = rc_tmp;

	rc_tmp = sel_netport_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;

	rc_tmp = sel_netnode_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;

	rc_tmp = sel_netif_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;

	rc_tmp = sel_netlink_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;

#if defined(CONFIG_SECURITY_INFINIBAND)
	rc_tmp = sel_ib_pkey_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;
#endif

#if defined(CONFIG_NETFILTER)
	rc_tmp = selinux_nf_ip_init();
	if (!rc && rc_tmp)
		rc = rc_tmp;
#endif

	return rc;
}
