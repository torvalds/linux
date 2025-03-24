/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux Security Module interface to other subsystems.
 * AppArmor presents single pointer to an aa_label structure.
 */
#ifndef __LINUX_LSM_APPARMOR_H
#define __LINUX_LSM_APPARMOR_H

struct aa_label;

struct lsm_prop_apparmor {
#ifdef CONFIG_SECURITY_APPARMOR
	struct aa_label *label;
#endif
};

#endif /* ! __LINUX_LSM_APPARMOR_H */
