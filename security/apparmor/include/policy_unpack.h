/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#ifndef __POLICY_INTERFACE_H
#define __POLICY_INTERFACE_H

#include <linux/list.h>
#include <linux/kref.h>

struct aa_load_ent {
	struct list_head list;
	struct aa_profile *new;
	struct aa_profile *old;
	struct aa_profile *rename;
	const char *ns_name;
};

void aa_load_ent_free(struct aa_load_ent *ent);
struct aa_load_ent *aa_load_ent_alloc(void);

#define PACKED_FLAG_HAT		1

#define PACKED_MODE_ENFORCE	0
#define PACKED_MODE_COMPLAIN	1
#define PACKED_MODE_KILL	2
#define PACKED_MODE_UNCONFINED	3

/* struct aa_loaddata - buffer of policy load data set */
struct aa_loaddata {
	struct kref count;
	size_t size;
	int abi;
	unsigned char *hash;
	char data[];
};

int aa_unpack(struct aa_loaddata *udata, struct list_head *lh, const char **ns);

static inline struct aa_loaddata *
aa_get_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_get(&(data->count));
	return data;
}

void aa_loaddata_kref(struct kref *kref);
static inline void aa_put_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_put(&data->count, aa_loaddata_kref);
}

#endif /* __POLICY_INTERFACE_H */
