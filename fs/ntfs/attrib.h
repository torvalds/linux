/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for attribute handling in NTFS Linux kernel driver.
 *
 * Copyright (c) 2001-2005 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#ifndef _LINUX_NTFS_ATTRIB_H
#define _LINUX_NTFS_ATTRIB_H

#include "ntfs.h"
#include "dir.h"

extern __le16 AT_UNNAMED[];

/*
 * ntfs_attr_search_ctx - used in attribute search functions
 * @mrec: buffer containing mft record to search
 * @mapped_mrec: true if @mrec was mapped by the search functions
 * @attr: attribute record in @mrec where to begin/continue search
 * @is_first: if true ntfs_attr_lookup() begins search with @attr, else after
 * @ntfs_ino: Inode owning this attribute search
 * @al_entry: Current attribute list entry
 * @base_ntfs_ino: Base inode
 * @mapped_base_mrec: true if @base_mrec was mapped by the search
 * @base_attr: Base attribute record pointer
 *
 * Structure must be initialized to zero before the first call to one of the
 * attribute search functions. Initialize @mrec to point to the mft record to
 * search, and @attr to point to the first attribute within @mrec (not necessary
 * if calling the _first() functions), and set @is_first to 'true' (not necessary
 * if calling the _first() functions).
 *
 * If @is_first is 'true', the search begins with @attr. If @is_first is 'false',
 * the search begins after @attr. This is so that, after the first call to one
 * of the search attribute functions, we can call the function again, without
 * any modification of the search context, to automagically get the next
 * matching attribute.
 */
struct ntfs_attr_search_ctx {
	struct mft_record *mrec;
	bool mapped_mrec;
	struct attr_record *attr;
	bool is_first;
	struct ntfs_inode *ntfs_ino;
	struct attr_list_entry *al_entry;
	struct ntfs_inode *base_ntfs_ino;
	struct mft_record *base_mrec;
	bool mapped_base_mrec;
	struct attr_record *base_attr;
};

enum {                  /* ways of processing holes when expanding */
	HOLES_NO,
	HOLES_OK,
};

int ntfs_map_runlist_nolock(struct ntfs_inode *ni, s64 vcn,
		struct ntfs_attr_search_ctx *ctx);
int ntfs_map_runlist(struct ntfs_inode *ni, s64 vcn);
s64 ntfs_attr_vcn_to_lcn_nolock(struct ntfs_inode *ni, const s64 vcn,
		const bool write_locked);
struct runlist_element *ntfs_attr_find_vcn_nolock(struct ntfs_inode *ni,
		const s64 vcn, struct ntfs_attr_search_ctx *ctx);
struct runlist_element *__ntfs_attr_find_vcn_nolock(struct runlist *runlist,
		const s64 vcn);
int ntfs_attr_map_whole_runlist(struct ntfs_inode *ni);
int ntfs_attr_lookup(const __le32 type, const __le16 *name,
		const u32 name_len, const u32 ic,
		const s64 lowest_vcn, const u8 *val, const u32 val_len,
		struct ntfs_attr_search_ctx *ctx);
int load_attribute_list(struct ntfs_inode *base_ni,
			       u8 *al_start, const s64 size);

static inline s64 ntfs_attr_size(const struct attr_record *a)
{
	if (!a->non_resident)
		return (s64)le32_to_cpu(a->data.resident.value_length);
	return le64_to_cpu(a->data.non_resident.data_size);
}

void ntfs_attr_reinit_search_ctx(struct ntfs_attr_search_ctx *ctx);
struct ntfs_attr_search_ctx *ntfs_attr_get_search_ctx(struct ntfs_inode *ni,
		struct mft_record *mrec);
void ntfs_attr_put_search_ctx(struct ntfs_attr_search_ctx *ctx);
int ntfs_attr_size_bounds_check(const struct ntfs_volume *vol,
		const __le32 type, const s64 size);
int ntfs_attr_can_be_resident(const struct ntfs_volume *vol,
		const __le32 type);
int ntfs_attr_map_cluster(struct ntfs_inode *ni, s64 vcn_start, s64 *lcn_start,
		s64 *lcn_count, s64 max_clu_count, bool *balloc, bool update_mp, bool skip_holes);
int ntfs_attr_record_resize(struct mft_record *m, struct attr_record *a, u32 new_size);
int ntfs_resident_attr_value_resize(struct mft_record *m, struct attr_record *a,
		const u32 new_size);
int ntfs_attr_make_non_resident(struct ntfs_inode *ni, const u32 data_size);
int ntfs_attr_set(struct ntfs_inode *ni, const s64 ofs, const s64 cnt,
		const u8 val);
int ntfs_attr_set_initialized_size(struct ntfs_inode *ni, loff_t new_size);
int ntfs_attr_open(struct ntfs_inode *ni, const __le32 type,
		__le16 *name, u32 name_len);
void ntfs_attr_close(struct ntfs_inode *n);
int ntfs_attr_fallocate(struct ntfs_inode *ni, loff_t start, loff_t byte_len, bool keep_size);
int ntfs_non_resident_attr_insert_range(struct ntfs_inode *ni, s64 start_vcn, s64 len);
int ntfs_non_resident_attr_collapse_range(struct ntfs_inode *ni, s64 start_vcn, s64 len);
int ntfs_non_resident_attr_punch_hole(struct ntfs_inode *ni, s64 start_vcn, s64 len);
int __ntfs_attr_truncate_vfs(struct ntfs_inode *ni, const s64 newsize,
		const s64 i_size);
int ntfs_attr_expand(struct ntfs_inode *ni, const s64 newsize, const s64 prealloc_size);
int ntfs_attr_truncate_i(struct ntfs_inode *ni, const s64 newsize, unsigned int holes);
int ntfs_attr_truncate(struct ntfs_inode *ni, const s64 newsize);
int ntfs_attr_rm(struct ntfs_inode *ni);
int ntfs_attr_exist(struct ntfs_inode *ni, const __le32 type, __le16 *name,
		u32 name_len);
int ntfs_attr_remove(struct ntfs_inode *ni, const __le32 type, __le16 *name,
		u32 name_len);
int ntfs_attr_record_rm(struct ntfs_attr_search_ctx *ctx);
int ntfs_attr_record_move_to(struct ntfs_attr_search_ctx *ctx, struct ntfs_inode *ni);
int ntfs_attr_add(struct ntfs_inode *ni, __le32 type,
		__le16 *name, u8 name_len, u8 *val, s64 size);
int ntfs_attr_record_move_away(struct ntfs_attr_search_ctx *ctx, int extra);
char *ntfs_attr_name_get(const struct ntfs_volume *vol, const __le16 *uname,
		const int uname_len);
void ntfs_attr_name_free(unsigned char **name);
void *ntfs_attr_readall(struct ntfs_inode *ni, const __le32 type,
		__le16 *name, u32 name_len, s64 *data_size);
int ntfs_resident_attr_record_add(struct ntfs_inode *ni, __le32 type,
		__le16 *name, u8 name_len, u8 *val, u32 size,
		__le16 flags);
int ntfs_attr_update_mapping_pairs(struct ntfs_inode *ni, s64 from_vcn);
struct runlist_element *ntfs_attr_vcn_to_rl(struct ntfs_inode *ni, s64 vcn, s64 *lcn);

/*
 * ntfs_attrs_walk - syntactic sugar for walking all attributes in an inode
 * @ctx:	initialised attribute search context
 *
 * Syntactic sugar for walking attributes in an inode.
 *
 * Return 0 on success and -1 on error with errno set to the error code from
 * ntfs_attr_lookup().
 *
 * Example: When you want to enumerate all attributes in an open ntfs inode
 *	    @ni, you can simply do:
 *
 *	int err;
 *	struct ntfs_attr_search_ctx *ctx = ntfs_attr_get_search_ctx(ni, NULL);
 *	if (!ctx)
 *		// Error code is in errno. Handle this case.
 *	while (!(err = ntfs_attrs_walk(ctx))) {
 *		struct attr_record *attr = ctx->attr;
 *		// attr now contains the next attribute. Do whatever you want
 *		// with it and then just continue with the while loop.
 *	}
 *	if (err && errno != ENOENT)
 *		// Ooops. An error occurred! You should handle this case.
 *	// Now finished with all attributes in the inode.
 */
static inline int ntfs_attrs_walk(struct ntfs_attr_search_ctx *ctx)
{
	return ntfs_attr_lookup(AT_UNUSED, NULL, 0, CASE_SENSITIVE, 0,
			NULL, 0, ctx);
}
#endif /* _LINUX_NTFS_ATTRIB_H */
