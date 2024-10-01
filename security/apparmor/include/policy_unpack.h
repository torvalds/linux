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
#define PACKED_FLAG_DEBUG1	2
#define PACKED_FLAG_DEBUG2	4

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
 * The AppArmor interface treats data as a type byte followed by the
 * actual data.  The interface has the notion of a named entry
 * which has a name (AA_NAME typecode followed by name string) followed by
 * the entries typecode and data.  Named types allow for optional
 * elements and extensions to be added and tested for without breaking
 * backwards compatibility.
 */

enum aa_code {
	AA_U8,
	AA_U16,
	AA_U32,
	AA_U64,
	AA_NAME,		/* same as string except it is items name */
	AA_STRING,
	AA_BLOB,
	AA_STRUCT,
	AA_STRUCTEND,
	AA_LIST,
	AA_LISTEND,
	AA_ARRAY,
	AA_ARRAYEND,
};

/*
 * aa_ext is the read of the buffer containing the serialized profile.  The
 * data is copied into a kernel buffer in apparmorfs and then handed off to
 * the unpack routines.
 */
struct aa_ext {
	void *start;
	void *end;
	void *pos;		/* pointer to current position in the buffer */
	u32 version;
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

#if IS_ENABLED(CONFIG_KUNIT)
bool aa_inbounds(struct aa_ext *e, size_t size);
size_t aa_unpack_u16_chunk(struct aa_ext *e, char **chunk);
bool aa_unpack_X(struct aa_ext *e, enum aa_code code);
bool aa_unpack_nameX(struct aa_ext *e, enum aa_code code, const char *name);
bool aa_unpack_u32(struct aa_ext *e, u32 *data, const char *name);
bool aa_unpack_u64(struct aa_ext *e, u64 *data, const char *name);
size_t aa_unpack_array(struct aa_ext *e, const char *name);
size_t aa_unpack_blob(struct aa_ext *e, char **blob, const char *name);
int aa_unpack_str(struct aa_ext *e, const char **string, const char *name);
int aa_unpack_strdup(struct aa_ext *e, char **string, const char *name);
#endif

#endif /* __POLICY_INTERFACE_H */
