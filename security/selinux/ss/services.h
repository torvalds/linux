/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Implementation of the security services.
 *
 * Author : Stephen Smalley, <sds@tycho.nsa.gov>
 */
#ifndef _SS_SERVICES_H_
#define _SS_SERVICES_H_

#include "policydb.h"
#include "context.h"

/* Mapping for a single class */
struct selinux_mapping {
	u16 value; /* policy value for class */
	unsigned int num_perms; /* number of permissions in class */
	u32 perms[sizeof(u32) * 8]; /* policy values for permissions */
};

/* Map for all of the classes, with array size */
struct selinux_map {
	struct selinux_mapping *mapping; /* indexed by class */
	u16 size; /* array size of mapping */
};

struct selinux_ss {
	struct sidtab *sidtab;
	struct policydb policydb;
	rwlock_t policy_rwlock;
	u32 latest_granting;
	struct selinux_map map;
	struct page *status_page;
	struct mutex status_lock;
};

void services_compute_xperms_drivers(struct extended_perms *xperms,
				struct avtab_node *node);

void services_compute_xperms_decision(struct extended_perms_decision *xpermd,
					struct avtab_node *node);

int context_add_hash(struct policydb *policydb, struct context *context);

#endif	/* _SS_SERVICES_H_ */
