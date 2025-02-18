// SPDX-License-Identifier: GPL-2.0-only
/*
 * SELinux initcalls
 */

#ifndef _SELINUX_INITCALLS_H
#define _SELINUX_INITCALLS_H

int init_sel_fs(void);
int sel_netport_init(void);
int sel_netnode_init(void);
int sel_netif_init(void);
int sel_netlink_init(void);
int sel_ib_pkey_init(void);
int selinux_nf_ip_init(void);

int selinux_initcall(void);

#endif
