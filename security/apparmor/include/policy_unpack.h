/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor policy loading interface function definitions.
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#ifndef __POLICY_INTERFACE_H
#define __POLICY_INTERFACE_H

#include <linux/list.h>
#include <linux/kref.h>
#include <linux/dcache.h>
#include <linux/workqueue.h>

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

struct aa_ns;

enum {
	AAFS_LOADDATA_ABI = 0,
	AAFS_LOADDATA_REVISION,
	AAFS_LOADDATA_HASH,
	AAFS_LOADDATA_DATA,
	AAFS_LOADDATA_COMPRESSED_SIZE,
	AAFS_LOADDATA_DIR,		/* must be last actual entry */
	AAFS_LOADDATA_NDENTS		/* count of entries */
};

/*
 * struct aa_loaddata - buffer of policy raw_data set
 *
 * there is no loaddata ref for being on ns list, nor a ref from
 * d_inode(@dentry) when grab a ref from these, @ns->lock must be held
 * && __aa_get_loaddata() needs to be used, and the return value
 * checked, if NULL the loaddata is already being reaped and should be
 * considered dead.
 */
struct aa_loaddata {
	struct kref count;
	struct list_head list;
	struct work_struct work;
	struct dentry *dents[AAFS_LOADDATA_NDENTS];
	struct aa_ns *ns;
	char *name;
	size_t size;			/* the original size of the payload */
	size_t compressed_size;		/* the compressed size of the payload */
	long revision;			/* the ns policy revision this caused */
	int abi;
	unsigned char *hash;

	/* Pointer to payload. If @compressed_size > 0, then this is the
	 * compressed version of the payload, else it is the uncompressed
	 * version (with the size indicated by @size).
	 */
	char *data;
};

int aa_unpack(struct aa_loaddata *udata, struct list_head *lh, const char **ns);

/**
 * __aa_get_loaddata - get a reference count to uncounted data reference
 * @data: reference to get a count on
 *
 * Returns: pointer to reference OR NULL if race is lost and reference is
 *          being repeated.
 * Requires: @data->ns->lock held, and the return code MUST be checked
 *
 * Use only from inode->i_private and @data->list found references
 */
static inline struct aa_loaddata *
__aa_get_loaddata(struct aa_loaddata *data)
{
	if (data && kref_get_unless_zero(&(data->count)))
		return data;

	return NULL;
}

/**
 * aa_get_loaddata - get a reference count from a counted data reference
 * @data: reference to get a count on
 *
 * Returns: point to reference
 * Requires: @data to have a valid reference count on it. It is a bug
 *           if the race to reap can be encountered when it is used.
 */
static inline struct aa_loaddata *
aa_get_loaddata(struct aa_loaddata *data)
{
	struct aa_loaddata *tmp = __aa_get_loaddata(data);

	AA_BUG(data && !tmp);

	return tmp;
}

void __aa_loaddata_update(struct aa_loaddata *data, long revision);
bool aa_rawdata_eq(struct aa_loaddata *l, struct aa_loaddata *r);
void aa_loaddata_kref(struct kref *kref);
struct aa_loaddata *aa_loaddata_alloc(size_t size);
static inline void aa_put_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_put(&data->count, aa_loaddata_kref);
}

#endif /* __POLICY_INTERFACE_H */
