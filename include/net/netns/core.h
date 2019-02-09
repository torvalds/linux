/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NETNS_CORE_H__
#define __NETNS_CORE_H__

struct ctl_table_header;
struct prot_inuse;

struct netns_core {
	/* core sysctls */
	struct ctl_table_header	*sysctl_hdr;

	int	sysctl_somaxconn;

#ifdef CONFIG_PROC_FS
	int __percpu *sock_inuse;
	struct prot_inuse __percpu *prot_inuse;
#endif
};

#endif
