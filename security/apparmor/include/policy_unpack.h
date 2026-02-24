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
#define PACKED_MODE_USER	4

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

/* struct aa_loaddata - buffer of policy raw_data set
 * @count: inode/filesystem refcount - use aa_get_i_loaddata()
 * @pcount: profile refcount - use aa_get_profile_loaddata()
 * @list: list the loaddata is on
 * @work: used to do a delayed cleanup
 * @dents: refs to dents created in aafs
 * @ns: the namespace this loaddata was loaded into
 * @name:
 * @size: the size of the data that was loaded
 * @compressed_size: the size of the data when it is compressed
 * @revision: unique revision count that this data was loaded as
 * @abi: the abi number the loaddata uses
 * @hash: a hash of the loaddata, used to help dedup data
 *
 * There is no loaddata ref for being on ns->rawdata_list, so
 * @ns->lock must be held when walking the list. Dentries and
 * inode opens hold refs on @count; profiles hold refs on @pcount.
 * When the last @pcount drops, do_ploaddata_rmfs() removes the
 * fs entries and drops the associated @count ref.
 */
struct aa_loaddata {
	struct kref count;
	struct kref pcount;
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
 * aa_get_loaddata - get a reference count from a counted data reference
 * @data: reference to get a count on
 *
 * Returns: pointer to reference
 * Requires: @data to have a valid reference count on it. It is a bug
 *           if the race to reap can be encountered when it is used.
 */
static inline struct aa_loaddata *
aa_get_i_loaddata(struct aa_loaddata *data)
{

	if (data)
		kref_get(&(data->count));
	return data;
}


/**
 * aa_get_profile_loaddata - get a profile reference count on loaddata
 * @data: reference to get a count on
 *
 * Returns: pointer to reference
 * Requires: @data to have a valid reference count on it.
 */
static inline struct aa_loaddata *
aa_get_profile_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_get(&(data->pcount));
	return data;
}

void __aa_loaddata_update(struct aa_loaddata *data, long revision);
bool aa_rawdata_eq(struct aa_loaddata *l, struct aa_loaddata *r);
void aa_loaddata_kref(struct kref *kref);
void aa_ploaddata_kref(struct kref *kref);
struct aa_loaddata *aa_loaddata_alloc(size_t size);
static inline void aa_put_i_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_put(&data->count, aa_loaddata_kref);
}

static inline void aa_put_profile_loaddata(struct aa_loaddata *data)
{
	if (data)
		kref_put(&data->pcount, aa_ploaddata_kref);
}

#if IS_ENABLED(CONFIG_KUNIT)
bool aa_inbounds(struct aa_ext *e, size_t size);
size_t aa_unpack_u16_chunk(struct aa_ext *e, char **chunk);
bool aa_unpack_X(struct aa_ext *e, enum aa_code code);
bool aa_unpack_nameX(struct aa_ext *e, enum aa_code code, const char *name);
bool aa_unpack_u32(struct aa_ext *e, u32 *data, const char *name);
bool aa_unpack_u64(struct aa_ext *e, u64 *data, const char *name);
bool aa_unpack_array(struct aa_ext *e, const char *name, u16 *size);
size_t aa_unpack_blob(struct aa_ext *e, char **blob, const char *name);
int aa_unpack_str(struct aa_ext *e, const char **string, const char *name);
int aa_unpack_strdup(struct aa_ext *e, char **string, const char *name);
#endif

#endif /* __POLICY_INTERFACE_H */
