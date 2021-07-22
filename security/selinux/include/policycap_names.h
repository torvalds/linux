/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SELINUX_POLICYCAP_NAMES_H_
#define _SELINUX_POLICYCAP_NAMES_H_

#include "policycap.h"

/* Policy capability names */
const char *selinux_policycap_names[__POLICYDB_CAPABILITY_MAX] = {
	"network_peer_controls",
	"open_perms",
	"extended_socket_class",
	"always_check_network",
	"cgroup_seclabel",
	"nnp_nosuid_transition",
	"genfs_seclabel_symlinks"
};

#endif /* _SELINUX_POLICYCAP_NAMES_H_ */
