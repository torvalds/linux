/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for NTFS kernel index handling.
 *
 * Copyright (c) 2004 Anton Altaparmakov
 */

#ifndef _LINUX_NTFS_INDEX_H
#define _LINUX_NTFS_INDEX_H

#include <linux/fs.h>

#include "attrib.h"
#include "mft.h"

#define  VCN_INDEX_ROOT_PARENT  ((s64)-2)

#define MAX_PARENT_VCN	32

/*
 * @idx_ni:	index inode containing the @entry described by this context
 * @name:	Unicode name of the indexed attribute
 *		(usually $I30 for directories)
 * @name_len:	length of @name in Unicode characters
 * @entry:	index entry (points into @ir or @ia)
 * @cr:		creation time of the entry (for sorting/validation)
 * @data:	index entry data (points into @entry)
 * @data_len:	length in bytes of @data
 * @is_in_root:	'true' if @entry is in @ir and 'false' if it is in @ia
 * @ir:		index root if @is_in_root and NULL otherwise
 * @actx:	attribute search context if @is_in_root and NULL otherwise
 * @ib:		index block header (valid when @is_in_root is 'false')
 * @ia_ni:	index allocation inode (extent inode) for @ia
 * @parent_pos:	array of parent entry positions in the B-tree nodes
 * @parent_vcn:	VCNs of parent index blocks in the B-tree traversal
 * @pindex:	current depth (number of parent nodes) in the traversal
 *		(maximum is MAX_PARENT_VCN)
 * @ib_dirty:	true if the current index block (@ia/@ib) was modified
 * @block_size:	size of index blocks in bytes (from $INDEX_ROOT or $Boot)
 * @vcn_size_bits: log2(cluster size)
 * @sync_write:	true if synchronous writeback is requested for this context
 *
 * @idx_ni is the index inode this context belongs to.
 *
 * @entry is the index entry described by this context.  @data and @data_len
 * are the index entry data and its length in bytes, respectively.  @data
 * simply points into @entry.  This is probably what the user is interested in.
 *
 * If @is_in_root is 'true', @entry is in the index root attribute @ir described
 * by the attribute search context @actx and the base inode @base_ni.  @ia and
 * @page are NULL in this case.
 *
 * If @is_in_root is 'false', @entry is in the index allocation attribute and @ia
 * and @page point to the index allocation block and the mapped, locked page it
 * is in, respectively.  @ir, @actx and @base_ni are NULL in this case.
 *
 * To obtain a context call ntfs_index_ctx_get().
 *
 * We use this context to allow ntfs_index_lookup() to return the found index
 * @entry and its @data without having to allocate a buffer and copy the @entry
 * and/or its @data into it.
 *
 * When finished with the @entry and its @data, call ntfs_index_ctx_put() to
 * free the context and other associated resources.
 *
 * If the index entry was modified, ntfs_index_entry_mark_dirty()
 * or ntfs_index_entry_write() before the call to ntfs_index_ctx_put() to
 * ensure that the changes are written to disk.
 */
struct ntfs_index_context {
	struct ntfs_inode *idx_ni;
	__le16 *name;
	u32 name_len;
	struct index_entry *entry;
	__le32 cr;
	void *data;
	u16 data_len;
	bool is_in_root;
	struct index_root *ir;
	struct ntfs_attr_search_ctx *actx;
	struct index_block *ib;
	struct ntfs_inode *ia_ni;
	int parent_pos[MAX_PARENT_VCN];
	s64 parent_vcn[MAX_PARENT_VCN];
	int pindex;
	bool ib_dirty;
	u32 block_size;
	u8 vcn_size_bits;
	bool sync_write;
};

int ntfs_index_entry_inconsistent(struct ntfs_index_context *icx, struct ntfs_volume *vol,
		const struct index_entry *ie, __le32 collation_rule, u64 inum);
struct ntfs_index_context *ntfs_index_ctx_get(struct ntfs_inode *ni, __le16 *name,
		u32 name_len);
void ntfs_index_ctx_put(struct ntfs_index_context *ictx);
int ntfs_index_lookup(const void *key, const u32 key_len,
		struct ntfs_index_context *ictx);

void ntfs_index_entry_mark_dirty(struct ntfs_index_context *ictx);
int ntfs_index_add_filename(struct ntfs_inode *ni, struct file_name_attr *fn, u64 mref);
int ntfs_index_remove(struct ntfs_inode *ni, const void *key, const u32 keylen);
struct ntfs_inode *ntfs_ia_open(struct ntfs_index_context *icx, struct ntfs_inode *ni);
struct index_entry *ntfs_index_walk_down(struct index_entry *ie, struct ntfs_index_context *ictx);
struct index_entry *ntfs_index_next(struct index_entry *ie, struct ntfs_index_context *ictx);
int ntfs_index_rm(struct ntfs_index_context *icx);
void ntfs_index_ctx_reinit(struct ntfs_index_context *icx);
int ntfs_ie_add(struct ntfs_index_context *icx, struct index_entry *ie);
int ntfs_icx_ib_sync_write(struct ntfs_index_context *icx);

#endif /* _LINUX_NTFS_INDEX_H */
