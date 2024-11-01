/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Access vector cache interface for the security server.
 *
 * Author : Stephen Smalley, <sds@tycho.nsa.gov>
 */
#ifndef _SELINUX_AVC_SS_H_
#define _SELINUX_AVC_SS_H_

#include <linux/types.h>

struct selinux_avc;
int avc_ss_reset(struct selinux_avc *avc, u32 seqno);

/* Class/perm mapping support */
struct security_class_mapping {
	const char *name;
	const char *perms[sizeof(u32) * 8 + 1];
};

extern const struct security_class_mapping secclass_map[];

#endif /* _SELINUX_AVC_SS_H_ */

