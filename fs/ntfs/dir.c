/**
 * dir.c - NTFS kernel directory operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
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

#include <linux/buffer_head.h>
#include <linux/slab.h>

#include "dir.h"
#include "aops.h"
#include "attrib.h"
#include "mft.h"
#include "debug.h"
#include "ntfs.h"

/**
 * The little endian Unicode string $I30 as a global constant.
 */
ntfschar I30[5] = { cpu_to_le16('$'), cpu_to_le16('I'),
		cpu_to_le16('3'),	cpu_to_le16('0'), 0 };

/**
 * ntfs_lookup_inode_by_name - find an inode in a directory given its name
 * @dir_ni:	ntfs inode of the directory in which to search for the name
 * @uname:	Unicode name for which to search in the directory
 * @uname_len:	length of the name @uname in Unicode characters
 * @res:	return the found file name if necessary (see below)
 *
 * Look for an inode with name @uname in the directory with inode @dir_ni.
 * ntfs_lookup_inode_by_name() walks the contents of the directory looking for
 * the Unicode name. If the name is found in the directory, the corresponding
 * inode number (>= 0) is returned as a mft reference in cpu format, i.e. it
 * is a 64-bit number containing the sequence number.
 *
 * On error, a negative value is returned corresponding to the error code. In
 * particular if the inode is not found -ENOENT is returned. Note that you
 * can't just check the return value for being negative, you have to check the
 * inode number for being negative which you can extract using MREC(return
 * value).
 *
 * Note, @uname_len does not include the (optional) terminating NULL character.
 *
 * Note, we look for a case sensitive match first but we also look for a case
 * insensitive match at the same time. If we find a case insensitive match, we
 * save that for the case that we don't find an exact match, where we return
 * the case insensitive match and setup @res (which we allocate!) with the mft
 * reference, the file name type, length and with a copy of the little endian
 * Unicode file name itself. If we match a file name which is in the DOS name
 * space, we only return the mft reference and file name type in @res.
 * ntfs_lookup() then uses this to find the long file name in the inode itself.
 * This is to avoid polluting the dcache with short file names. We want them to
 * work but we don't care for how quickly one can access them. This also fixes
 * the dcache aliasing issues.
 *
 * Locking:  - Caller must hold i_mutex on the directory.
 *	     - Each page cache page in the index allocation mapping must be
 *	       locked whilst being accessed otherwise we may find a corrupt
 *	       page due to it being under ->writepage at the moment which
 *	       applies the mst protection fixups before writing out and then
 *	       removes them again after the write is complete after which it 
 *	       unlocks the page.
 */
MFT_REF ntfs_lookup_inode_by_name(ntfs_inode *dir_ni, const ntfschar *uname,
		const int uname_len, ntfs_name **res)
{
	ntfs_volume *vol = dir_ni->vol;
	struct super_block *sb = vol->sb;
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *index_end;
	u64 mref;
	ntfs_attr_search_ctx *ctx;
	int err, rc;
	VCN vcn, old_vcn;
	struct address_space *ia_mapping;
	struct page *page;
	u8 *kaddr;
	ntfs_name *name = NULL;

	BUG_ON(!S_ISDIR(VFS_I(dir_ni)->i_mode));
	BUG_ON(NInoAttr(dir_ni));
	/* Get hold of the mft record for the directory. */
	m = map_mft_record(dir_ni);
	if (IS_ERR(m)) {
		ntfs_error(sb, "map_mft_record() failed with error code %ld.",
				-PTR_ERR(m));
		return ERR_MREF(PTR_ERR(m));
	}
	ctx = ntfs_attr_get_search_ctx(dir_ni, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Find the index root attribute in the mft record. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL,
			0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT) {
			ntfs_error(sb, "Index root attribute missing in "
					"directory inode 0x%lx.",
					dir_ni->mft_no);
			err = -EIO;
		}
		goto err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
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
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end)
			goto dir_err_out;
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len)) {
found_it:
			/*
			 * We have a perfect match, so we don't need to care
			 * about having matched imperfectly before, so we can
			 * free name and set *res to NULL.
			 * However, if the perfect match is a short file name,
			 * we need to signal this through *res, so that
			 * ntfs_lookup() can fix dcache aliasing issues.
			 * As an optimization we just reuse an existing
			 * allocation of *res.
			 */
			if (ie->key.file_name.file_name_type == FILE_NAME_DOS) {
				if (!name) {
					name = kmalloc(sizeof(ntfs_name),
							GFP_NOFS);
					if (!name) {
						err = -ENOMEM;
						goto err_out;
					}
				}
				name->mref = le64_to_cpu(
						ie->data.dir.indexed_file);
				name->type = FILE_NAME_DOS;
				name->len = 0;
				*res = name;
			} else {
				kfree(name);
				*res = NULL;
			}
			mref = le64_to_cpu(ie->data.dir.indexed_file);
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(dir_ni);
			return mref;
		}
		/*
		 * For a case insensitive mount, we also perform a case
		 * insensitive comparison (provided the file name is not in the
		 * POSIX namespace). If the comparison matches, and the name is
		 * in the WIN32 namespace, we cache the filename in *res so
		 * that the caller, ntfs_lookup(), can work on it. If the
		 * comparison matches, and the name is in the DOS namespace, we
		 * only cache the mft reference and the file name type (we set
		 * the name length to zero for simplicity).
		 */
		if (!NVolCaseSensitive(vol) &&
				ie->key.file_name.file_name_type &&
				ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				IGNORE_CASE, vol->upcase, vol->upcase_len)) {
			int name_size = sizeof(ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb, "Found already allocated name "
						"in phase 1. Please run chkdsk "
						"and if that doesn't find any "
						"errors please report you saw "
						"this message to "
						"linux-ntfs-dev@lists."
						"sourceforge.net.");
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(ntfschar);
			name = kmalloc(name_size, GFP_NOFS);
			if (!name) {
				err = -ENOMEM;
				goto err_out;
			}
			name->mref = le64_to_cpu(ie->data.dir.indexed_file);
			name->type = type;
			if (type != FILE_NAME_DOS) {
				name->len = len;
				memcpy(name->name, ie->key.file_name.file_name,
						len * sizeof(ntfschar));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it;
	}
	/*
	 * We have finished with this index without success. Check for the
	 * presence of a child node and if not present return -ENOENT, unless
	 * we have got a matching name cached in name in which case return the
	 * mft reference associated with it.
	 */
	if (!(ie->flags & INDEX_ENTRY_NODE)) {
		if (name) {
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(dir_ni);
			return name->mref;
		}
		ntfs_debug("Entry not found.");
		err = -ENOENT;
		goto err_out;
	} /* Child node present, descend into it. */
	/* Consistency check: Verify that an index allocation exists. */
	if (!NInoIndexAllocPresent(dir_ni)) {
		ntfs_error(sb, "No index allocation attribute but index entry "
				"requires one. Directory inode 0x%lx is "
				"corrupt or driver bug.", dir_ni->mft_no);
		goto err_out;
	}
	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((sle64*)((u8*)ie + le16_to_cpu(ie->length) - 8));
	ia_mapping = VFS_I(dir_ni)->i_mapping;
	/*
	 * We are done with the index root and the mft record. Release them,
	 * otherwise we deadlock with ntfs_map_page().
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(dir_ni);
	m = NULL;
	ctx = NULL;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_CACHE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	page = ntfs_map_page(ia_mapping, vcn <<
			dir_ni->itype.index.vcn_size_bits >> PAGE_CACHE_SHIFT);
	if (IS_ERR(page)) {
		ntfs_error(sb, "Failed to map directory index page, error %ld.",
				-PTR_ERR(page));
		err = PTR_ERR(page);
		goto err_out;
	}
	lock_page(page);
	kaddr = (u8*)page_address(page);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (INDEX_ALLOCATION*)(kaddr + ((vcn <<
			dir_ni->itype.index.vcn_size_bits) & ~PAGE_CACHE_MASK));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%lx or driver bug.", dir_ni->mft_no);
		goto unm_err_out;
	}
	/* Catch multi sector transfer fixup errors. */
	if (unlikely(!ntfs_is_indx_record(ia->magic))) {
		ntfs_error(sb, "Directory index record with vcn 0x%llx is "
				"corrupt.  Corrupt inode 0x%lx.  Run chkdsk.",
				(unsigned long long)vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb, "Actual VCN (0x%llx) of index buffer is "
				"different from expected VCN (0x%llx). "
				"Directory inode 0x%lx is corrupt or driver "
				"bug.", (unsigned long long)
				sle64_to_cpu(ia->index_block_vcn),
				(unsigned long long)vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			dir_ni->itype.index.block_size) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(unsigned long long)vcn, dir_ni->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				dir_ni->itype.index.block_size);
		goto unm_err_out;
	}
	index_end = (u8*)ia + dir_ni->itype.index.block_size;
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (unsigned long long)vcn,
				dir_ni->mft_no);
		goto unm_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + dir_ni->itype.index.block_size) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%llx) of directory "
				"inode 0x%lx exceeds maximum size.",
				(unsigned long long)vcn, dir_ni->mft_no);
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
		/* Bounds check. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end) {
			ntfs_error(sb, "Index entry out of bounds in "
					"directory inode 0x%lx.",
					dir_ni->mft_no);
			goto unm_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len)) {
found_it2:
			/*
			 * We have a perfect match, so we don't need to care
			 * about having matched imperfectly before, so we can
			 * free name and set *res to NULL.
			 * However, if the perfect match is a short file name,
			 * we need to signal this through *res, so that
			 * ntfs_lookup() can fix dcache aliasing issues.
			 * As an optimization we just reuse an existing
			 * allocation of *res.
			 */
			if (ie->key.file_name.file_name_type == FILE_NAME_DOS) {
				if (!name) {
					name = kmalloc(sizeof(ntfs_name),
							GFP_NOFS);
					if (!name) {
						err = -ENOMEM;
						goto unm_err_out;
					}
				}
				name->mref = le64_to_cpu(
						ie->data.dir.indexed_file);
				name->type = FILE_NAME_DOS;
				name->len = 0;
				*res = name;
			} else {
				kfree(name);
				*res = NULL;
			}
			mref = le64_to_cpu(ie->data.dir.indexed_file);
			unlock_page(page);
			ntfs_unmap_page(page);
			return mref;
		}
		/*
		 * For a case insensitive mount, we also perform a case
		 * insensitive comparison (provided the file name is not in the
		 * POSIX namespace). If the comparison matches, and the name is
		 * in the WIN32 namespace, we cache the filename in *res so
		 * that the caller, ntfs_lookup(), can work on it. If the
		 * comparison matches, and the name is in the DOS namespace, we
		 * only cache the mft reference and the file name type (we set
		 * the name length to zero for simplicity).
		 */
		if (!NVolCaseSensitive(vol) &&
				ie->key.file_name.file_name_type &&
				ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				IGNORE_CASE, vol->upcase, vol->upcase_len)) {
			int name_size = sizeof(ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb, "Found already allocated name "
						"in phase 2. Please run chkdsk "
						"and if that doesn't find any "
						"errors please report you saw "
						"this message to "
						"linux-ntfs-dev@lists."
						"sourceforge.net.");
				unlock_page(page);
				ntfs_unmap_page(page);
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(ntfschar);
			name = kmalloc(name_size, GFP_NOFS);
			if (!name) {
				err = -ENOMEM;
				goto unm_err_out;
			}
			name->mref = le64_to_cpu(ie->data.dir.indexed_file);
			name->type = type;
			if (type != FILE_NAME_DOS) {
				name->len = len;
				memcpy(name->name, ie->key.file_name.file_name,
						len * sizeof(ntfschar));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it2;
	}
	/*
	 * We have finished with this index buffer without success. Check for
	 * the presence of a child node.
	 */
	if (ie->flags & INDEX_ENTRY_NODE) {
		if ((ia->index.flags & NODE_MASK) == LEAF_NODE) {
			ntfs_error(sb, "Index entry with child node found in "
					"a leaf node in directory inode 0x%lx.",
					dir_ni->mft_no);
			goto unm_err_out;
		}
		/* Child node present, descend into it. */
		old_vcn = vcn;
		vcn = sle64_to_cpup((sle64*)((u8*)ie +
				le16_to_cpu(ie->length) - 8));
		if (vcn >= 0) {
			/* If vcn is in the same page cache page as old_vcn we
			 * recycle the mapped page. */
			if (old_vcn << vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT == vcn <<
					vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT)
				goto fast_descend_into_child_node;
			unlock_page(page);
			ntfs_unmap_page(page);
			goto descend_into_child_node;
		}
		ntfs_error(sb, "Negative child node vcn in directory inode "
				"0x%lx.", dir_ni->mft_no);
		goto unm_err_out;
	}
	/*
	 * No child node present, return -ENOENT, unless we have got a matching
	 * name cached in name in which case return the mft reference
	 * associated with it.
	 */
	if (name) {
		unlock_page(page);
		ntfs_unmap_page(page);
		return name->mref;
	}
	ntfs_debug("Entry not found.");
	err = -ENOENT;
unm_err_out:
	unlock_page(page);
	ntfs_unmap_page(page);
err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(dir_ni);
	if (name) {
		kfree(name);
		*res = NULL;
	}
	return ERR_MREF(err);
dir_err_out:
	ntfs_error(sb, "Corrupt directory.  Aborting lookup.");
	goto err_out;
}

#if 0

// TODO: (AIA)
// The algorithm embedded in this code will be required for the time when we
// want to support adding of entries to directories, where we require correct
// collation of file names in order not to cause corruption of the filesystem.

/**
 * ntfs_lookup_inode_by_name - find an inode in a directory given its name
 * @dir_ni:	ntfs inode of the directory in which to search for the name
 * @uname:	Unicode name for which to search in the directory
 * @uname_len:	length of the name @uname in Unicode characters
 *
 * Look for an inode with name @uname in the directory with inode @dir_ni.
 * ntfs_lookup_inode_by_name() walks the contents of the directory looking for
 * the Unicode name. If the name is found in the directory, the corresponding
 * inode number (>= 0) is returned as a mft reference in cpu format, i.e. it
 * is a 64-bit number containing the sequence number.
 *
 * On error, a negative value is returned corresponding to the error code. In
 * particular if the inode is not found -ENOENT is returned. Note that you
 * can't just check the return value for being negative, you have to check the
 * inode number for being negative which you can extract using MREC(return
 * value).
 *
 * Note, @uname_len does not include the (optional) terminating NULL character.
 */
u64 ntfs_lookup_inode_by_name(ntfs_inode *dir_ni, const ntfschar *uname,
		const int uname_len)
{
	ntfs_volume *vol = dir_ni->vol;
	struct super_block *sb = vol->sb;
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *index_end;
	u64 mref;
	ntfs_attr_search_ctx *ctx;
	int err, rc;
	IGNORE_CASE_BOOL ic;
	VCN vcn, old_vcn;
	struct address_space *ia_mapping;
	struct page *page;
	u8 *kaddr;

	/* Get hold of the mft record for the directory. */
	m = map_mft_record(dir_ni);
	if (IS_ERR(m)) {
		ntfs_error(sb, "map_mft_record() failed with error code %ld.",
				-PTR_ERR(m));
		return ERR_MREF(PTR_ERR(m));
	}
	ctx = ntfs_attr_get_search_ctx(dir_ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Find the index root attribute in the mft record. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL,
			0, ctx);
	if (unlikely(err)) {
		if (err == -ENOENT) {
			ntfs_error(sb, "Index root attribute missing in "
					"directory inode 0x%lx.",
					dir_ni->mft_no);
			err = -EIO;
		}
		goto err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
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
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end)
			goto dir_err_out;
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/*
		 * If the current entry has a name type of POSIX, the name is
		 * case sensitive and not otherwise. This has the effect of us
		 * not being able to access any POSIX file names which collate
		 * after the non-POSIX one when they only differ in case, but
		 * anyone doing screwy stuff like that deserves to burn in
		 * hell... Doing that kind of stuff on NT4 actually causes
		 * corruption on the partition even when using SP6a and Linux
		 * is not involved at all.
		 */
		ic = ie->key.file_name.file_name_type ? IGNORE_CASE :
				CASE_SENSITIVE;
		/*
		 * If the names match perfectly, we are done and return the
		 * mft reference of the inode (i.e. the inode number together
		 * with the sequence number for consistency checking. We
		 * convert it to cpu format before returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, ic,
				vol->upcase, vol->upcase_len)) {
found_it:
			mref = le64_to_cpu(ie->data.dir.indexed_file);
			ntfs_attr_put_search_ctx(ctx);
			unmap_mft_record(dir_ni);
			return mref;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it;
	}
	/*
	 * We have finished with this index without success. Check for the
	 * presence of a child node.
	 */
	if (!(ie->flags & INDEX_ENTRY_NODE)) {
		/* No child node, return -ENOENT. */
		err = -ENOENT;
		goto err_out;
	} /* Child node present, descend into it. */
	/* Consistency check: Verify that an index allocation exists. */
	if (!NInoIndexAllocPresent(dir_ni)) {
		ntfs_error(sb, "No index allocation attribute but index entry "
				"requires one. Directory inode 0x%lx is "
				"corrupt or driver bug.", dir_ni->mft_no);
		goto err_out;
	}
	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((u8*)ie + le16_to_cpu(ie->length) - 8);
	ia_mapping = VFS_I(dir_ni)->i_mapping;
	/*
	 * We are done with the index root and the mft record. Release them,
	 * otherwise we deadlock with ntfs_map_page().
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(dir_ni);
	m = NULL;
	ctx = NULL;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_CACHE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	page = ntfs_map_page(ia_mapping, vcn <<
			dir_ni->itype.index.vcn_size_bits >> PAGE_CACHE_SHIFT);
	if (IS_ERR(page)) {
		ntfs_error(sb, "Failed to map directory index page, error %ld.",
				-PTR_ERR(page));
		err = PTR_ERR(page);
		goto err_out;
	}
	lock_page(page);
	kaddr = (u8*)page_address(page);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (INDEX_ALLOCATION*)(kaddr + ((vcn <<
			dir_ni->itype.index.vcn_size_bits) & ~PAGE_CACHE_MASK));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%lx or driver bug.", dir_ni->mft_no);
		goto unm_err_out;
	}
	/* Catch multi sector transfer fixup errors. */
	if (unlikely(!ntfs_is_indx_record(ia->magic))) {
		ntfs_error(sb, "Directory index record with vcn 0x%llx is "
				"corrupt.  Corrupt inode 0x%lx.  Run chkdsk.",
				(unsigned long long)vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb, "Actual VCN (0x%llx) of index buffer is "
				"different from expected VCN (0x%llx). "
				"Directory inode 0x%lx is corrupt or driver "
				"bug.", (unsigned long long)
				sle64_to_cpu(ia->index_block_vcn),
				(unsigned long long)vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			dir_ni->itype.index.block_size) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(unsigned long long)vcn, dir_ni->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				dir_ni->itype.index.block_size);
		goto unm_err_out;
	}
	index_end = (u8*)ia + dir_ni->itype.index.block_size;
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (unsigned long long)vcn,
				dir_ni->mft_no);
		goto unm_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + dir_ni->itype.index.block_size) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%llx) of directory "
				"inode 0x%lx exceeds maximum size.",
				(unsigned long long)vcn, dir_ni->mft_no);
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
		/* Bounds check. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end) {
			ntfs_error(sb, "Index entry out of bounds in "
					"directory inode 0x%lx.",
					dir_ni->mft_no);
			goto unm_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/*
		 * If the current entry has a name type of POSIX, the name is
		 * case sensitive and not otherwise. This has the effect of us
		 * not being able to access any POSIX file names which collate
		 * after the non-POSIX one when they only differ in case, but
		 * anyone doing screwy stuff like that deserves to burn in
		 * hell... Doing that kind of stuff on NT4 actually causes
		 * corruption on the partition even when using SP6a and Linux
		 * is not involved at all.
		 */
		ic = ie->key.file_name.file_name_type ? IGNORE_CASE :
				CASE_SENSITIVE;
		/*
		 * If the names match perfectly, we are done and return the
		 * mft reference of the inode (i.e. the inode number together
		 * with the sequence number for consistency checking. We
		 * convert it to cpu format before returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, ic,
				vol->upcase, vol->upcase_len)) {
found_it2:
			mref = le64_to_cpu(ie->data.dir.indexed_file);
			unlock_page(page);
			ntfs_unmap_page(page);
			return mref;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(ntfschar*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it2;
	}
	/*
	 * We have finished with this index buffer without success. Check for
	 * the presence of a child node.
	 */
	if (ie->flags & INDEX_ENTRY_NODE) {
		if ((ia->index.flags & NODE_MASK) == LEAF_NODE) {
			ntfs_error(sb, "Index entry with child node found in "
					"a leaf node in directory inode 0x%lx.",
					dir_ni->mft_no);
			goto unm_err_out;
		}
		/* Child node present, descend into it. */
		old_vcn = vcn;
		vcn = sle64_to_cpup((u8*)ie + le16_to_cpu(ie->length) - 8);
		if (vcn >= 0) {
			/* If vcn is in the same page cache page as old_vcn we
			 * recycle the mapped page. */
			if (old_vcn << vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT == vcn <<
					vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT)
				goto fast_descend_into_child_node;
			unlock_page(page);
			ntfs_unmap_page(page);
			goto descend_into_child_node;
		}
		ntfs_error(sb, "Negative child node vcn in directory inode "
				"0x%lx.", dir_ni->mft_no);
		goto unm_err_out;
	}
	/* No child node, return -ENOENT. */
	ntfs_debug("Entry not found.");
	err = -ENOENT;
unm_err_out:
	unlock_page(page);
	ntfs_unmap_page(page);
err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(dir_ni);
	return ERR_MREF(err);
dir_err_out:
	ntfs_error(sb, "Corrupt directory. Aborting lookup.");
	goto err_out;
}

#endif

/**
 * ntfs_filldir - ntfs specific filldir method
 * @vol:	current ntfs volume
 * @fpos:	position in the directory
 * @ndir:	ntfs inode of current directory
 * @ia_page:	page in which the index allocation buffer @ie is in resides
 * @ie:		current index entry
 * @name:	buffer to use for the converted name
 * @dirent:	vfs filldir callback context
 * @filldir:	vfs filldir callback
 *
 * Convert the Unicode @name to the loaded NLS and pass it to the @filldir
 * callback.
 *
 * If @ia_page is not NULL it is the locked page containing the index
 * allocation block containing the index entry @ie.
 *
 * Note, we drop (and then reacquire) the page lock on @ia_page across the
 * @filldir() call otherwise we would deadlock with NFSd when it calls ->lookup
 * since ntfs_lookup() will lock the same page.  As an optimization, we do not
 * retake the lock if we are returning a non-zero value as ntfs_readdir()
 * would need to drop the lock immediately anyway.
 */
static inline int ntfs_filldir(ntfs_volume *vol, loff_t fpos,
		ntfs_inode *ndir, struct page *ia_page, INDEX_ENTRY *ie,
		u8 *name, void *dirent, filldir_t filldir)
{
	unsigned long mref;
	int name_len, rc;
	unsigned dt_type;
	FILE_NAME_TYPE_FLAGS name_type;

	name_type = ie->key.file_name.file_name_type;
	if (name_type == FILE_NAME_DOS) {
		ntfs_debug("Skipping DOS name space entry.");
		return 0;
	}
	if (MREF_LE(ie->data.dir.indexed_file) == FILE_root) {
		ntfs_debug("Skipping root directory self reference entry.");
		return 0;
	}
	if (MREF_LE(ie->data.dir.indexed_file) < FILE_first_user &&
			!NVolShowSystemFiles(vol)) {
		ntfs_debug("Skipping system file.");
		return 0;
	}
	name_len = ntfs_ucstonls(vol, (ntfschar*)&ie->key.file_name.file_name,
			ie->key.file_name.file_name_length, &name,
			NTFS_MAX_NAME_LEN * NLS_MAX_CHARSET_SIZE + 1);
	if (name_len <= 0) {
		ntfs_warning(vol->sb, "Skipping unrepresentable inode 0x%llx.",
				(long long)MREF_LE(ie->data.dir.indexed_file));
		return 0;
	}
	if (ie->key.file_name.file_attributes &
			FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT)
		dt_type = DT_DIR;
	else
		dt_type = DT_REG;
	mref = MREF_LE(ie->data.dir.indexed_file);
	/*
	 * Drop the page lock otherwise we deadlock with NFS when it calls
	 * ->lookup since ntfs_lookup() will lock the same page.
	 */
	if (ia_page)
		unlock_page(ia_page);
	ntfs_debug("Calling filldir for %s with len %i, fpos 0x%llx, inode "
			"0x%lx, DT_%s.", name, name_len, fpos, mref,
			dt_type == DT_DIR ? "DIR" : "REG");
	rc = filldir(dirent, name, name_len, fpos, mref, dt_type);
	/* Relock the page but not if we are aborting ->readdir. */
	if (!rc && ia_page)
		lock_page(ia_page);
	return rc;
}

/*
 * We use the same basic approach as the old NTFS driver, i.e. we parse the
 * index root entries and then the index allocation entries that are marked
 * as in use in the index bitmap.
 *
 * While this will return the names in random order this doesn't matter for
 * ->readdir but OTOH results in a faster ->readdir.
 *
 * VFS calls ->readdir without BKL but with i_mutex held. This protects the VFS
 * parts (e.g. ->f_pos and ->i_size, and it also protects against directory
 * modifications).
 *
 * Locking:  - Caller must hold i_mutex on the directory.
 *	     - Each page cache page in the index allocation mapping must be
 *	       locked whilst being accessed otherwise we may find a corrupt
 *	       page due to it being under ->writepage at the moment which
 *	       applies the mst protection fixups before writing out and then
 *	       removes them again after the write is complete after which it 
 *	       unlocks the page.
 */
static int ntfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	s64 ia_pos, ia_start, prev_ia_pos, bmp_pos;
	loff_t fpos, i_size;
	struct inode *bmp_vi, *vdir = filp->f_path.dentry->d_inode;
	struct super_block *sb = vdir->i_sb;
	ntfs_inode *ndir = NTFS_I(vdir);
	ntfs_volume *vol = NTFS_SB(sb);
	MFT_RECORD *m;
	INDEX_ROOT *ir = NULL;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *name = NULL;
	int rc, err, ir_pos, cur_bmp_pos;
	struct address_space *ia_mapping, *bmp_mapping;
	struct page *bmp_page = NULL, *ia_page = NULL;
	u8 *kaddr, *bmp, *index_end;
	ntfs_attr_search_ctx *ctx;

	fpos = filp->f_pos;
	ntfs_debug("Entering for inode 0x%lx, fpos 0x%llx.",
			vdir->i_ino, fpos);
	rc = err = 0;
	/* Are we at end of dir yet? */
	i_size = i_size_read(vdir);
	if (fpos >= i_size + vol->mft_record_size)
		goto done;
	/* Emulate . and .. for all directories. */
	if (!fpos) {
		ntfs_debug("Calling filldir for . with len 1, fpos 0x0, "
				"inode 0x%lx, DT_DIR.", vdir->i_ino);
		rc = filldir(dirent, ".", 1, fpos, vdir->i_ino, DT_DIR);
		if (rc)
			goto done;
		fpos++;
	}
	if (fpos == 1) {
		ntfs_debug("Calling filldir for .. with len 2, fpos 0x1, "
				"inode 0x%lx, DT_DIR.",
				(unsigned long)parent_ino(filp->f_path.dentry));
		rc = filldir(dirent, "..", 2, fpos,
				parent_ino(filp->f_path.dentry), DT_DIR);
		if (rc)
			goto done;
		fpos++;
	}
	m = NULL;
	ctx = NULL;
	/*
	 * Allocate a buffer to store the current name being processed
	 * converted to format determined by current NLS.
	 */
	name = kmalloc(NTFS_MAX_NAME_LEN * NLS_MAX_CHARSET_SIZE + 1, GFP_NOFS);
	if (unlikely(!name)) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Are we jumping straight into the index allocation attribute? */
	if (fpos >= vol->mft_record_size)
		goto skip_index_root;
	/* Get hold of the mft record for the directory. */
	m = map_mft_record(ndir);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		m = NULL;
		goto err_out;
	}
	ctx = ntfs_attr_get_search_ctx(ndir, m);
	if (unlikely(!ctx)) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Get the offset into the index root attribute. */
	ir_pos = (s64)fpos;
	/* Find the index root attribute in the mft record. */
	err = ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL,
			0, ctx);
	if (unlikely(err)) {
		ntfs_error(sb, "Index root attribute missing in directory "
				"inode 0x%lx.", vdir->i_ino);
		goto err_out;
	}
	/*
	 * Copy the index root attribute value to a buffer so that we can put
	 * the search context and unmap the mft record before calling the
	 * filldir() callback.  We need to do this because of NFSd which calls
	 * ->lookup() from its filldir callback() and this causes NTFS to
	 * deadlock as ntfs_lookup() maps the mft record of the directory and
	 * we have got it mapped here already.  The only solution is for us to
	 * unmap the mft record here so that a call to ntfs_lookup() is able to
	 * map the mft record without deadlocking.
	 */
	rc = le32_to_cpu(ctx->attr->data.resident.value_length);
	ir = kmalloc(rc, GFP_NOFS);
	if (unlikely(!ir)) {
		err = -ENOMEM;
		goto err_out;
	}
	/* Copy the index root value (it has been verified in read_inode). */
	memcpy(ir, (u8*)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset), rc);
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ndir);
	ctx = NULL;
	m = NULL;
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry or until filldir tells us it has had enough
	 * or signals an error (both covered by the rc test).
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		ntfs_debug("In index root, offset 0x%zx.", (u8*)ie - (u8*)ir);
		/* Bounds checks. */
		if (unlikely((u8*)ie < (u8*)ir || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end))
			goto err_out;
		/* The last entry cannot contain a name. */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Skip index root entry if continuing previous readdir. */
		if (ir_pos > (u8*)ie - (u8*)ir)
			continue;
		/* Advance the position even if going to skip the entry. */
		fpos = (u8*)ie - (u8*)ir;
		/* Submit the name to the filldir callback. */
		rc = ntfs_filldir(vol, fpos, ndir, NULL, ie, name, dirent,
				filldir);
		if (rc) {
			kfree(ir);
			goto abort;
		}
	}
	/* We are done with the index root and can free the buffer. */
	kfree(ir);
	ir = NULL;
	/* If there is no index allocation attribute we are finished. */
	if (!NInoIndexAllocPresent(ndir))
		goto EOD;
	/* Advance fpos to the beginning of the index allocation. */
	fpos = vol->mft_record_size;
skip_index_root:
	kaddr = NULL;
	prev_ia_pos = -1LL;
	/* Get the offset into the index allocation attribute. */
	ia_pos = (s64)fpos - vol->mft_record_size;
	ia_mapping = vdir->i_mapping;
	ntfs_debug("Inode 0x%lx, getting index bitmap.", vdir->i_ino);
	bmp_vi = ntfs_attr_iget(vdir, AT_BITMAP, I30, 4);
	if (IS_ERR(bmp_vi)) {
		ntfs_error(sb, "Failed to get bitmap attribute.");
		err = PTR_ERR(bmp_vi);
		goto err_out;
	}
	bmp_mapping = bmp_vi->i_mapping;
	/* Get the starting bitmap bit position and sanity check it. */
	bmp_pos = ia_pos >> ndir->itype.index.block_size_bits;
	if (unlikely(bmp_pos >> 3 >= i_size_read(bmp_vi))) {
		ntfs_error(sb, "Current index allocation position exceeds "
				"index bitmap size.");
		goto iput_err_out;
	}
	/* Get the starting bit position in the current bitmap page. */
	cur_bmp_pos = bmp_pos & ((PAGE_CACHE_SIZE * 8) - 1);
	bmp_pos &= ~(u64)((PAGE_CACHE_SIZE * 8) - 1);
get_next_bmp_page:
	ntfs_debug("Reading bitmap with page index 0x%llx, bit ofs 0x%llx",
			(unsigned long long)bmp_pos >> (3 + PAGE_CACHE_SHIFT),
			(unsigned long long)bmp_pos &
			(unsigned long long)((PAGE_CACHE_SIZE * 8) - 1));
	bmp_page = ntfs_map_page(bmp_mapping,
			bmp_pos >> (3 + PAGE_CACHE_SHIFT));
	if (IS_ERR(bmp_page)) {
		ntfs_error(sb, "Reading index bitmap failed.");
		err = PTR_ERR(bmp_page);
		bmp_page = NULL;
		goto iput_err_out;
	}
	bmp = (u8*)page_address(bmp_page);
	/* Find next index block in use. */
	while (!(bmp[cur_bmp_pos >> 3] & (1 << (cur_bmp_pos & 7)))) {
find_next_index_buffer:
		cur_bmp_pos++;
		/*
		 * If we have reached the end of the bitmap page, get the next
		 * page, and put away the old one.
		 */
		if (unlikely((cur_bmp_pos >> 3) >= PAGE_CACHE_SIZE)) {
			ntfs_unmap_page(bmp_page);
			bmp_pos += PAGE_CACHE_SIZE * 8;
			cur_bmp_pos = 0;
			goto get_next_bmp_page;
		}
		/* If we have reached the end of the bitmap, we are done. */
		if (unlikely(((bmp_pos + cur_bmp_pos) >> 3) >= i_size))
			goto unm_EOD;
		ia_pos = (bmp_pos + cur_bmp_pos) <<
				ndir->itype.index.block_size_bits;
	}
	ntfs_debug("Handling index buffer 0x%llx.",
			(unsigned long long)bmp_pos + cur_bmp_pos);
	/* If the current index buffer is in the same page we reuse the page. */
	if ((prev_ia_pos & (s64)PAGE_CACHE_MASK) !=
			(ia_pos & (s64)PAGE_CACHE_MASK)) {
		prev_ia_pos = ia_pos;
		if (likely(ia_page != NULL)) {
			unlock_page(ia_page);
			ntfs_unmap_page(ia_page);
		}
		/*
		 * Map the page cache page containing the current ia_pos,
		 * reading it from disk if necessary.
		 */
		ia_page = ntfs_map_page(ia_mapping, ia_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(ia_page)) {
			ntfs_error(sb, "Reading index allocation data failed.");
			err = PTR_ERR(ia_page);
			ia_page = NULL;
			goto err_out;
		}
		lock_page(ia_page);
		kaddr = (u8*)page_address(ia_page);
	}
	/* Get the current index buffer. */
	ia = (INDEX_ALLOCATION*)(kaddr + (ia_pos & ~PAGE_CACHE_MASK &
			~(s64)(ndir->itype.index.block_size - 1)));
	/* Bounds checks. */
	if (unlikely((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE)) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%lx or driver bug.", vdir->i_ino);
		goto err_out;
	}
	/* Catch multi sector transfer fixup errors. */
	if (unlikely(!ntfs_is_indx_record(ia->magic))) {
		ntfs_error(sb, "Directory index record with vcn 0x%llx is "
				"corrupt.  Corrupt inode 0x%lx.  Run chkdsk.",
				(unsigned long long)ia_pos >>
				ndir->itype.index.vcn_size_bits, vdir->i_ino);
		goto err_out;
	}
	if (unlikely(sle64_to_cpu(ia->index_block_vcn) != (ia_pos &
			~(s64)(ndir->itype.index.block_size - 1)) >>
			ndir->itype.index.vcn_size_bits)) {
		ntfs_error(sb, "Actual VCN (0x%llx) of index buffer is "
				"different from expected VCN (0x%llx). "
				"Directory inode 0x%lx is corrupt or driver "
				"bug. ", (unsigned long long)
				sle64_to_cpu(ia->index_block_vcn),
				(unsigned long long)ia_pos >>
				ndir->itype.index.vcn_size_bits, vdir->i_ino);
		goto err_out;
	}
	if (unlikely(le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			ndir->itype.index.block_size)) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(unsigned long long)ia_pos >>
				ndir->itype.index.vcn_size_bits, vdir->i_ino,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				ndir->itype.index.block_size);
		goto err_out;
	}
	index_end = (u8*)ia + ndir->itype.index.block_size;
	if (unlikely(index_end > kaddr + PAGE_CACHE_SIZE)) {
		ntfs_error(sb, "Index buffer (VCN 0x%llx) of directory inode "
				"0x%lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (unsigned long long)ia_pos >>
				ndir->itype.index.vcn_size_bits, vdir->i_ino);
		goto err_out;
	}
	ia_start = ia_pos & ~(s64)(ndir->itype.index.block_size - 1);
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (unlikely(index_end > (u8*)ia + ndir->itype.index.block_size)) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%llx) of directory "
				"inode 0x%lx exceeds maximum size.",
				(unsigned long long)ia_pos >>
				ndir->itype.index.vcn_size_bits, vdir->i_ino);
		goto err_out;
	}
	/* The first index entry in this index buffer. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry or until filldir tells us it has had enough
	 * or signals an error (both covered by the rc test).
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->length))) {
		ntfs_debug("In index allocation, offset 0x%llx.",
				(unsigned long long)ia_start +
				(unsigned long long)((u8*)ie - (u8*)ia));
		/* Bounds checks. */
		if (unlikely((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->key_length) >
				index_end))
			goto err_out;
		/* The last entry cannot contain a name. */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Skip index block entry if continuing previous readdir. */
		if (ia_pos - ia_start > (u8*)ie - (u8*)ia)
			continue;
		/* Advance the position even if going to skip the entry. */
		fpos = (u8*)ie - (u8*)ia +
				(sle64_to_cpu(ia->index_block_vcn) <<
				ndir->itype.index.vcn_size_bits) +
				vol->mft_record_size;
		/*
		 * Submit the name to the @filldir callback.  Note,
		 * ntfs_filldir() drops the lock on @ia_page but it retakes it
		 * before returning, unless a non-zero value is returned in
		 * which case the page is left unlocked.
		 */
		rc = ntfs_filldir(vol, fpos, ndir, ia_page, ie, name, dirent,
				filldir);
		if (rc) {
			/* @ia_page is already unlocked in this case. */
			ntfs_unmap_page(ia_page);
			ntfs_unmap_page(bmp_page);
			iput(bmp_vi);
			goto abort;
		}
	}
	goto find_next_index_buffer;
unm_EOD:
	if (ia_page) {
		unlock_page(ia_page);
		ntfs_unmap_page(ia_page);
	}
	ntfs_unmap_page(bmp_page);
	iput(bmp_vi);
EOD:
	/* We are finished, set fpos to EOD. */
	fpos = i_size + vol->mft_record_size;
abort:
	kfree(name);
done:
#ifdef DEBUG
	if (!rc)
		ntfs_debug("EOD, fpos 0x%llx, returning 0.", fpos);
	else
		ntfs_debug("filldir returned %i, fpos 0x%llx, returning 0.",
				rc, fpos);
#endif
	filp->f_pos = fpos;
	return 0;
err_out:
	if (bmp_page) {
		ntfs_unmap_page(bmp_page);
iput_err_out:
		iput(bmp_vi);
	}
	if (ia_page) {
		unlock_page(ia_page);
		ntfs_unmap_page(ia_page);
	}
	kfree(ir);
	kfree(name);
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(ndir);
	if (!err)
		err = -EIO;
	ntfs_debug("Failed. Returning error code %i.", -err);
	filp->f_pos = fpos;
	return err;
}

/**
 * ntfs_dir_open - called when an inode is about to be opened
 * @vi:		inode to be opened
 * @filp:	file structure describing the inode
 *
 * Limit directory size to the page cache limit on architectures where unsigned
 * long is 32-bits. This is the most we can do for now without overflowing the
 * page cache page index. Doing it this way means we don't run into problems
 * because of existing too large directories. It would be better to allow the
 * user to read the accessible part of the directory but I doubt very much
 * anyone is going to hit this check on a 32-bit architecture, so there is no
 * point in adding the extra complexity required to support this.
 *
 * On 64-bit architectures, the check is hopefully optimized away by the
 * compiler.
 */
static int ntfs_dir_open(struct inode *vi, struct file *filp)
{
	if (sizeof(unsigned long) < 8) {
		if (i_size_read(vi) > MAX_LFS_FILESIZE)
			return -EFBIG;
	}
	return 0;
}

#ifdef NTFS_RW

/**
 * ntfs_dir_fsync - sync a directory to disk
 * @filp:	directory to be synced
 * @dentry:	dentry describing the directory to sync
 * @datasync:	if non-zero only flush user data and not metadata
 *
 * Data integrity sync of a directory to disk.  Used for fsync, fdatasync, and
 * msync system calls.  This function is based on file.c::ntfs_file_fsync().
 *
 * Write the mft record and all associated extent mft records as well as the
 * $INDEX_ALLOCATION and $BITMAP attributes and then sync the block device.
 *
 * If @datasync is true, we do not wait on the inode(s) to be written out
 * but we always wait on the page cache pages to be written out.
 *
 * Note: In the past @filp could be NULL so we ignore it as we don't need it
 * anyway.
 *
 * Locking: Caller must hold i_mutex on the inode.
 *
 * TODO: We should probably also write all attribute/index inodes associated
 * with this inode but since we have no simple way of getting to them we ignore
 * this problem for now.  We do write the $BITMAP attribute if it is present
 * which is the important one for a directory so things are not too bad.
 */
static int ntfs_dir_fsync(struct file *filp, loff_t start, loff_t end,
			  int datasync)
{
	struct inode *bmp_vi, *vi = filp->f_mapping->host;
	int err, ret;
	ntfs_attr na;

	ntfs_debug("Entering for inode 0x%lx.", vi->i_ino);

	err = filemap_write_and_wait_range(vi->i_mapping, start, end);
	if (err)
		return err;
	mutex_lock(&vi->i_mutex);

	BUG_ON(!S_ISDIR(vi->i_mode));
	/* If the bitmap attribute inode is in memory sync it, too. */
	na.mft_no = vi->i_ino;
	na.type = AT_BITMAP;
	na.name = I30;
	na.name_len = 4;
	bmp_vi = ilookup5(vi->i_sb, vi->i_ino, (test_t)ntfs_test_inode, &na);
	if (bmp_vi) {
 		write_inode_now(bmp_vi, !datasync);
		iput(bmp_vi);
	}
	ret = __ntfs_write_inode(vi, 1);
	write_inode_now(vi, !datasync);
	err = sync_blockdev(vi->i_sb->s_bdev);
	if (unlikely(err && !ret))
		ret = err;
	if (likely(!ret))
		ntfs_debug("Done.");
	else
		ntfs_warning(vi->i_sb, "Failed to f%ssync inode 0x%lx.  Error "
				"%u.", datasync ? "data" : "", vi->i_ino, -ret);
	mutex_unlock(&vi->i_mutex);
	return ret;
}

#endif /* NTFS_RW */

const struct file_operations ntfs_dir_ops = {
	.llseek		= generic_file_llseek,	/* Seek inside directory. */
	.read		= generic_read_dir,	/* Return -EISDIR. */
	.readdir	= ntfs_readdir,		/* Read directory contents. */
#ifdef NTFS_RW
	.fsync		= ntfs_dir_fsync,	/* Sync a directory to disk. */
	/*.aio_fsync	= ,*/			/* Sync all outstanding async
						   i/o operations on a kiocb. */
#endif /* NTFS_RW */
	/*.ioctl	= ,*/			/* Perform function on the
						   mounted filesystem. */
	.open		= ntfs_dir_open,	/* Open directory. */
};
