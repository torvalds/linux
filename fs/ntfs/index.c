/*
 * index.c - NTFS kernel index handling.  Part of the Linux-NTFS project.
 *
 * Copyright (c) 2004-2005 Anton Altaparmakov
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "aops.h"
#include "collate.h"
#include "debug.h"
#include "index.h"
#include "ntfs.h"

/**
 * ntfs_index_ctx_get - allocate and initialize a new index context
 * @idx_ni:	ntfs index inode with which to initialize the context
 *
 * Allocate a new index context, initialize it with @idx_ni and return it.
 * Return NULL if allocation failed.
 *
 * Locking:  Caller must hold i_sem on the index inode.
 */
ntfs_index_context *ntfs_index_ctx_get(ntfs_inode *idx_ni)
{
	ntfs_index_context *ictx;

	ictx = kmem_cache_alloc(ntfs_index_ctx_cache, SLAB_NOFS);
	if (ictx)
		*ictx = (ntfs_index_context){ .idx_ni = idx_ni };
	return ictx;
}

/**
 * ntfs_index_ctx_put - release an index context
 * @ictx:	index context to free
 *
 * Release the index context @ictx, releasing all associated resources.
 *
 * Locking:  Caller must hold i_sem on the index inode.
 */
void ntfs_index_ctx_put(ntfs_index_context *ictx)
{
	if (ictx->entry) {
		if (ictx->is_in_root) {
			if (ictx->actx)
				ntfs_attr_put_search_ctx(ictx->actx);
			if (ictx->base_ni)
				unmap_mft_record(ictx->base_ni);
		} else {
			struct page *page = ictx->page;
			if (page) {
				BUG_ON(!PageLocked(page));
				unlock_page(page);
				ntfs_unmap_page(page);
			}
		}
	}
	kmem_cache_free(ntfs_index_ctx_cache, ictx);
	return;
}

/**
 * ntfs_index_lookup - find a key in an index and return its index entry
 * @key:	[IN] key for which to search in the index
 * @key_len:	[IN] length of @key in bytes
 * @ictx:	[IN/OUT] context describing the index and the returned entry
 *
 * Before calling ntfs_index_lookup(), @ictx must have been obtained from a
 * call to ntfs_index_ctx_get().
 *
 * Look for the @key in the index specified by the index lookup context @ictx.
 * ntfs_index_lookup() walks the contents of the index looking for the @key.
 *
 * If the @key is found in the index, 0 is returned and @ictx is setup to
 * describe the index entry containing the matching @key.  @ictx->entry is the
 * index entry and @ictx->data and @ictx->data_len are the index entry data and
 * its length in bytes, respectively.
 *
 * If the @key is not found in the index, -ENOENT is returned and @ictx is
 * setup to describe the index entry whose key collates immediately after the
 * search @key, i.e. this is the position in the index at which an index entry
 * with a key of @key would need to be inserted.
 *
 * If an error occurs return the negative error code and @ictx is left
 * untouched.
 *
 * When finished with the entry and its data, call ntfs_index_ctx_put() to free
 * the context and other associated resources.
 *
 * If the index entry was modified, call flush_dcache_index_entry_page()
 * immediately after the modification and either ntfs_index_entry_mark_dirty()
 * or ntfs_index_entry_write() before the call to ntfs_index_ctx_put() to
 * ensure that the changes are written to disk.
 *
 * Locking:  - Caller must hold i_sem on the index inode.
 *	     - Each page cache page in the index allocation mapping must be
 *	       locked whilst being accessed otherwise we may find a corrupt
 *	       page due to it being under ->writepage at the moment which
 *	       applies the mst protection fixups before writing out and then
 *	       removes them again after the write is complete after which it 
 *	       unlocks the page.
 */
int ntfs_index_lookup(const void *key, const int key_len,
		ntfs_index_context *ictx)
{
	VCN vcn, old_vcn;
	ntfs_inode *idx_ni = ictx->idx_ni;
	ntfs_volume *vol = idx_ni->vol;
	struct super_block *sb = vol->sb;
	ntfs_inode *base_ni = idx_ni->ext.base_ntfs_ino;
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *index_end, *kaddr;
	ntfs_attr_search_ctx *actx;
	struct address_space *ia_mapping;
	struct page *page;
	int rc, err = 0;

	ntfs_debug("Entering.");
	BUG_ON(!NInoAttr(idx_ni));
	BUG_ON(idx_ni->type != AT_INDEX_ALLOCATION);
	BUG_ON(idx_ni->nr_extents != -1);
	BUG_ON(!base_ni);
	BUG_ON(!key);
	BUG_ON(key_len <= 0);
	if (!ntfs_is_collation_rule_supported(
			idx_ni->itype.index.collation_rule)) {
		ntfs_error(sb, "Index uses unsupported collation rule 0x%x.  "
				"Aborting lookup.", le32_to_cpu(
				idx_ni->itype.index.collation_rule));
		return -EOPNOTSUPP;
	}
	/* Get hold of the mft record for the index inode. */
	m = map_mft_record(base_ni);
	if (IS_ERR(m)) {
		ntfs_error(sb, "map_mft_record() failed with error code %ld.",
				-PTR_ERR(m));
		return PTR_ERR(m);
	}
	actx = ntfs_attr_get_search_ctx(base_ni, m);
	if (unlikely(!actx)) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Find the index root attribute in the mft record. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, idx_ni->name, idx_ni->name_len,
			CASE_SENSITIVE, 0, NULL, 0, actx);
	if (unlikely(err)) {
		if (err == -ENOENT) {
			ntfs_error(sb, "Index root attribute missing in inode "
					"0x%lx.", idx_ni->mft_no);
			err = -EIO;
		}
		goto err_out;
	}
	/* Get to the index root value (it has been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)actx->attr +
			le16_to_cpu(actx->attr->data.resident.value_offset));
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		/* Bounds checks. */
		if ((u8*)ie < (u8*)actx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->length) > index_end)
			goto idx_err_out;
		/*
		 * The last entry cannot contain a key.  It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Further bounds checks. */
		if ((u32)sizeof(INDEX_ENTRY_HEADER) +
				le16_to_cpu(ie->key_length) >
				le16_to_cpu(ie->data.vi.data_offset) ||
				(u32)le16_to_cpu(ie->data.vi.data_offset) +
				le16_to_cpu(ie->data.vi.data_length) >
				le16_to_cpu(ie->length))
			goto idx_err_out;
		/* If the keys match perfectly, we setup @ictx and return 0. */
		if ((key_len == le16_to_cpu(ie->key_length)) && !memcmp(key,
				&ie->key, key_len)) {
ir_done:
			ictx->is_in_root = TRUE;
			ictx->ir = ir;
			ictx->actx = actx;
			ictx->base_ni = base_ni;
			ictx->ia = NULL;
			ictx->page = NULL;
done:
			ictx->entry = ie;
			ictx->data = (u8*)ie +
					le16_to_cpu(ie->data.vi.data_offset);
			ictx->data_len = le16_to_cpu(ie->data.vi.data_length);
			ntfs_debug("Done.");
			return err;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate(vol, idx_ni->itype.index.collation_rule, key,
				key_len, &ie->key, le16_to_cpu(ie->key_length));
		/*
		 * If @key collates before the key of the current entry, there
		 * is definitely no such key in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/*
		 * A match should never happen as the memcmp() call should have
		 * cought it, but we still treat it correctly.
		 */
		if (!rc)
			goto ir_done;
		/* The keys are not equal, continue the search. */
	}
	/*
	 * We have finished with this index without success.  Check for the
	 * presence of a child node and if not present setup @ictx and return
	 * -ENOENT.
	 */
	if (!(ie->flags & INDEX_ENTRY_NODE)) {
		ntfs_debug("Entry not found.");
		err = -ENOENT;
		goto ir_done;
	} /* Child node present, descend into it. */
	/* Consistency check: Verify that an index allocation exists. */
	if (!NInoIndexAllocPresent(idx_ni)) {
		ntfs_error(sb, "No index allocation attribute but index entry "
				"requires one.  Inode 0x%lx is corrupt or "
				"driver bug.", idx_ni->mft_no);
		goto err_out;
	}
	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((sle64*)((u8*)ie + le16_to_cpu(ie->length) - 8));
	ia_mapping = VFS_I(idx_ni)->i_mapping;
	/*
	 * We are done with the index root and the mft record.  Release them,
	 * otherwise we deadlock with ntfs_map_page().
	 */
	ntfs_attr_put_search_ctx(actx);
	unmap_mft_record(base_ni);
	m = NULL;
	actx = NULL;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_CACHE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	page = ntfs_map_page(ia_mapping, vcn <<
			idx_ni->itype.index.vcn_size_bits >> PAGE_CACHE_SHIFT);
	if (IS_ERR(page)) {
		ntfs_error(sb, "Failed to map index page, error %ld.",
				-PTR_ERR(page));
		err = PTR_ERR(page);
		goto err_out;
	}
	lock_page(page);
	kaddr = (u8*)page_address(page);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (INDEX_ALLOCATION*)(kaddr + ((vcn <<
			idx_ni->itype.index.vcn_size_bits) & ~PAGE_CACHE_MASK));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed.  Corrupt inode "
				"0x%lx or driver bug.", idx_ni->mft_no);
		goto unm_err_out;
	}
	/* Catch multi sector transfer fixup errors. */
	if (unlikely(!ntfs_is_indx_record(ia->magic))) {
		ntfs_error(sb, "Index record with vcn 0x%llx is corrupt.  "
				"Corrupt inode 0x%lx.  Run chkdsk.",
				(long long)vcn, idx_ni->mft_no);
		goto unm_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb, "Actual VCN (0x%llx) of index buffer is "
				"different from expected VCN (0x%llx).  Inode "
				"0x%lx is corrupt or driver bug.",
				(unsigned long long)
				sle64_to_cpu(ia->index_block_vcn),
				(unsigned long long)vcn, idx_ni->mft_no);
		goto unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			idx_ni->itype.index.block_size) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of inode 0x%lx has "
				"a size (%u) differing from the index "
				"specified size (%u).  Inode is corrupt or "
				"driver bug.", (unsigned long long)vcn,
				idx_ni->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				idx_ni->itype.index.block_size);
		goto unm_err_out;
	}
	index_end = (u8*)ia + idx_ni->itype.index.block_size;
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of inode 0x%lx "
				"crosses page boundary.  Impossible!  Cannot "
				"access!  This is probably a bug in the "
				"driver.", (unsigned long long)vcn,
				idx_ni->mft_no);
		goto unm_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + idx_ni->itype.index.block_size) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%llx) of inode "
				"0x%lx exceeds maximum size.",
				(unsigned long long)vcn, idx_ni->mft_no);
		goto unm_err_out;
	}
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Iterate similar to above big loop but applied to index buffer, thus
	 * loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->length) > index_end) {
			ntfs_error(sb, "Index entry out of bounds in inode "
					"0x%lx.", idx_ni->mft_no);
			goto unm_err_out;
		}
		/*
		 * The last entry cannot contain a key.  It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Further bounds checks. */
		if ((u32)sizeof(INDEX_ENTRY_HEADER) +
				le16_to_cpu(ie->key_length) >
				le16_to_cpu(ie->data.vi.data_offset) ||
				(u32)le16_to_cpu(ie->data.vi.data_offset) +
				le16_to_cpu(ie->data.vi.data_length) >
				le16_to_cpu(ie->length)) {
			ntfs_error(sb, "Index entry out of bounds in inode "
					"0x%lx.", idx_ni->mft_no);
			goto unm_err_out;
		}
		/* If the keys match perfectly, we setup @ictx and return 0. */
		if ((key_len == le16_to_cpu(ie->key_length)) && !memcmp(key,
				&ie->key, key_len)) {
ia_done:
			ictx->is_in_root = FALSE;
			ictx->actx = NULL;
			ictx->base_ni = NULL;
			ictx->ia = ia;
			ictx->page = page;
			goto done;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate(vol, idx_ni->itype.index.collation_rule, key,
				key_len, &ie->key, le16_to_cpu(ie->key_length));
		/*
		 * If @key collates before the key of the current entry, there
		 * is definitely no such key in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/*
		 * A match should never happen as the memcmp() call should have
		 * cought it, but we still treat it correctly.
		 */
		if (!rc)
			goto ia_done;
		/* The keys are not equal, continue the search. */
	}
	/*
	 * We have finished with this index buffer without success.  Check for
	 * the presence of a child node and if not present return -ENOENT.
	 */
	if (!(ie->flags & INDEX_ENTRY_NODE)) {
		ntfs_debug("Entry not found.");
		err = -ENOENT;
		goto ia_done;
	}
	if ((ia->index.flags & NODE_MASK) == LEAF_NODE) {
		ntfs_error(sb, "Index entry with child node found in a leaf "
				"node in inode 0x%lx.", idx_ni->mft_no);
		goto unm_err_out;
	}
	/* Child node present, descend into it. */
	old_vcn = vcn;
	vcn = sle64_to_cpup((sle64*)((u8*)ie + le16_to_cpu(ie->length) - 8));
	if (vcn >= 0) {
		/*
		 * If vcn is in the same page cache page as old_vcn we recycle
		 * the mapped page.
		 */
		if (old_vcn << vol->cluster_size_bits >>
				PAGE_CACHE_SHIFT == vcn <<
				vol->cluster_size_bits >>
				PAGE_CACHE_SHIFT)
			goto fast_descend_into_child_node;
		unlock_page(page);
		ntfs_unmap_page(page);
		goto descend_into_child_node;
	}
	ntfs_error(sb, "Negative child node vcn in inode 0x%lx.",
			idx_ni->mft_no);
unm_err_out:
	unlock_page(page);
	ntfs_unmap_page(page);
err_out:
	if (!err)
		err = -EIO;
	if (actx)
		ntfs_attr_put_search_ctx(actx);
	if (m)
		unmap_mft_record(base_ni);
	return err;
idx_err_out:
	ntfs_error(sb, "Corrupt index.  Aborting lookup.");
	goto err_out;
}
