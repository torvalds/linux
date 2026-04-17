// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NTFS kernel directory operations.
 *
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (c) 2002 Richard Russon
 * Copyright (c) 2025 LG Electronics Co., Ltd.
 */

#include <linux/blkdev.h>

#include "dir.h"
#include "mft.h"
#include "ntfs.h"
#include "index.h"
#include "reparse.h"

#include <linux/filelock.h>

/*
 * The little endian Unicode string $I30 as a global constant.
 */
__le16 I30[5] = { cpu_to_le16('$'), cpu_to_le16('I'),
		cpu_to_le16('3'),	cpu_to_le16('0'), 0 };

/*
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
u64 ntfs_lookup_inode_by_name(struct ntfs_inode *dir_ni, const __le16 *uname,
		const int uname_len, struct ntfs_name **res)
{
	struct ntfs_volume *vol = dir_ni->vol;
	struct super_block *sb = vol->sb;
	struct inode *ia_vi = NULL;
	struct mft_record *m;
	struct index_root *ir;
	struct index_entry *ie;
	struct index_block *ia;
	u8 *index_end;
	u64 mref;
	struct ntfs_attr_search_ctx *ctx;
	int err, rc;
	s64 vcn, old_vcn;
	struct address_space *ia_mapping;
	struct folio *folio;
	u8 *kaddr = NULL;
	struct ntfs_name *name = NULL;

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
			ntfs_error(sb,
				"Index root attribute missing in directory inode 0x%llx.",
				dir_ni->mft_no);
			err = -EIO;
		}
		goto err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (struct index_root *)((u8 *)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));
	index_end = (u8 *)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (struct index_entry *)((u8 *)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (struct index_entry *)((u8 *)ie + le16_to_cpu(ie->length))) {
		/* Bounds checks. */
		if ((u8 *)ie < (u8 *)ctx->mrec ||
		    (u8 *)ie + sizeof(struct index_entry_header) > index_end ||
		    (u8 *)ie + sizeof(struct index_entry_header) + le16_to_cpu(ie->key_length) >
				index_end || (u8 *)ie + le16_to_cpu(ie->length) > index_end)
			goto dir_err_out;
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Key length should not be zero if it is not last entry. */
		if (!ie->key_length)
			goto dir_err_out;
		/* Check the consistency of an index entry */
		if (ntfs_index_entry_inconsistent(NULL, vol, ie, COLLATION_FILE_NAME,
				dir_ni->mft_no))
			goto dir_err_out;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(__le16 *)&ie->key.file_name.file_name,
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
					name = kmalloc(sizeof(struct ntfs_name),
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
		if ((!NVolCaseSensitive(vol) ||
		     ie->key.file_name.file_name_type == FILE_NAME_DOS) &&
		    ntfs_are_names_equal(uname, uname_len,
					 (__le16 *)&ie->key.file_name.file_name,
					 ie->key.file_name.file_name_length,
					 IGNORE_CASE, vol->upcase,
					 vol->upcase_len)) {
			int name_size = sizeof(struct ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb,
					"Found already allocated name in phase 1. Please run chkdsk");
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(__le16);
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
						len * sizeof(__le16));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(__le16 *)&ie->key.file_name.file_name,
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
				(__le16 *)&ie->key.file_name.file_name,
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

	/* Get the starting vcn of the index_block holding the child node. */
	vcn = le64_to_cpup((__le64 *)((u8 *)ie + le16_to_cpu(ie->length) - 8));

	/*
	 * We are done with the index root and the mft record. Release them,
	 * otherwise we deadlock with read_mapping_folio().
	 */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(dir_ni);
	m = NULL;
	ctx = NULL;

	ia_vi = ntfs_index_iget(VFS_I(dir_ni), I30, 4);
	if (IS_ERR(ia_vi)) {
		err = PTR_ERR(ia_vi);
		goto err_out;
	}

	ia_mapping = ia_vi->i_mapping;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	folio = read_mapping_folio(ia_mapping, vcn <<
			dir_ni->itype.index.vcn_size_bits >> PAGE_SHIFT, NULL);
	if (IS_ERR(folio)) {
		ntfs_error(sb, "Failed to map directory index page, error %ld.",
				-PTR_ERR(folio));
		err = PTR_ERR(folio);
		goto err_out;
	}

	folio_lock(folio);
	kaddr = kmalloc(PAGE_SIZE, GFP_NOFS);
	if (!kaddr) {
		err = -ENOMEM;
		folio_unlock(folio);
		folio_put(folio);
		goto unm_err_out;
	}

	memcpy_from_folio(kaddr, folio, 0, PAGE_SIZE);
	post_read_mst_fixup((struct ntfs_record *)kaddr, PAGE_SIZE);
	folio_unlock(folio);
	folio_put(folio);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (struct index_block *)(kaddr + ((vcn <<
			dir_ni->itype.index.vcn_size_bits) & ~PAGE_MASK));
	/* Bounds checks. */
	if ((u8 *)ia < kaddr || (u8 *)ia > kaddr + PAGE_SIZE) {
		ntfs_error(sb,
			"Out of bounds check failed. Corrupt directory inode 0x%llx or driver bug.",
			dir_ni->mft_no);
		goto unm_err_out;
	}
	/* Catch multi sector transfer fixup errors. */
	if (unlikely(!ntfs_is_indx_record(ia->magic))) {
		ntfs_error(sb,
			"Directory index record with vcn 0x%llx is corrupt.  Corrupt inode 0x%llx.  Run chkdsk.",
			vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (le64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb,
			"Actual VCN (0x%llx) of index buffer is different from expected VCN (0x%llx). Directory inode 0x%llx is corrupt or driver bug.",
			le64_to_cpu(ia->index_block_vcn),
			vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			dir_ni->itype.index.block_size) {
		ntfs_error(sb,
			"Index buffer (VCN 0x%llx) of directory inode 0x%llx has a size (%u) differing from the directory specified size (%u). Directory inode is corrupt or driver bug.",
			vcn, dir_ni->mft_no,
			le32_to_cpu(ia->index.allocated_size) + 0x18,
			dir_ni->itype.index.block_size);
		goto unm_err_out;
	}
	index_end = (u8 *)ia + dir_ni->itype.index.block_size;
	if (index_end > kaddr + PAGE_SIZE) {
		ntfs_error(sb,
			"Index buffer (VCN 0x%llx) of directory inode 0x%llx crosses page boundary. Impossible! Cannot access! This is probably a bug in the driver.",
			vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	index_end = (u8 *)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8 *)ia + dir_ni->itype.index.block_size) {
		ntfs_error(sb,
			"Size of index buffer (VCN 0x%llx) of directory inode 0x%llx exceeds maximum size.",
			vcn, dir_ni->mft_no);
		goto unm_err_out;
	}
	/* The first index entry. */
	ie = (struct index_entry *)((u8 *)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Iterate similar to above big loop but applied to index buffer, thus
	 * loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (struct index_entry *)((u8 *)ie + le16_to_cpu(ie->length))) {
		/* Bounds checks. */
		if ((u8 *)ie < (u8 *)ia ||
		    (u8 *)ie + sizeof(struct index_entry_header) > index_end ||
		    (u8 *)ie + sizeof(struct index_entry_header) + le16_to_cpu(ie->key_length) >
				index_end || (u8 *)ie + le16_to_cpu(ie->length) > index_end) {
			ntfs_error(sb, "Index entry out of bounds in directory inode 0x%llx.",
					dir_ni->mft_no);
			goto unm_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->flags & INDEX_ENTRY_END)
			break;
		/* Key length should not be zero if it is not last entry. */
		if (!ie->key_length)
			goto unm_err_out;
		/* Check the consistency of an index entry */
		if (ntfs_index_entry_inconsistent(NULL, vol, ie, COLLATION_FILE_NAME,
				dir_ni->mft_no))
			goto unm_err_out;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(__le16 *)&ie->key.file_name.file_name,
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
					name = kmalloc(sizeof(struct ntfs_name),
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
			kfree(kaddr);
			iput(ia_vi);
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
		if ((!NVolCaseSensitive(vol) ||
		     ie->key.file_name.file_name_type == FILE_NAME_DOS) &&
		    ntfs_are_names_equal(uname, uname_len,
					 (__le16 *)&ie->key.file_name.file_name,
					 ie->key.file_name.file_name_length,
					 IGNORE_CASE, vol->upcase,
					 vol->upcase_len)) {
			int name_size = sizeof(struct ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb,
					"Found already allocated name in phase 2. Please run chkdsk");
				kfree(kaddr);
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(__le16);
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
						len * sizeof(__le16));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(__le16 *)&ie->key.file_name.file_name,
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
				(__le16 *)&ie->key.file_name.file_name,
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
			ntfs_error(sb,
				"Index entry with child node found in a leaf node in directory inode 0x%llx.",
				dir_ni->mft_no);
			goto unm_err_out;
		}
		/* Child node present, descend into it. */
		old_vcn = vcn;
		vcn = le64_to_cpup((__le64 *)((u8 *)ie +
				le16_to_cpu(ie->length) - 8));
		if (vcn >= 0) {
			/*
			 * If vcn is in the same page cache page as old_vcn we
			 * recycle the mapped page.
			 */
			if (ntfs_cluster_to_pidx(vol, old_vcn) ==
			    ntfs_cluster_to_pidx(vol, vcn))
				goto fast_descend_into_child_node;
			kfree(kaddr);
			kaddr = NULL;
			goto descend_into_child_node;
		}
		ntfs_error(sb, "Negative child node vcn in directory inode 0x%llx.",
				dir_ni->mft_no);
		goto unm_err_out;
	}
	/*
	 * No child node present, return -ENOENT, unless we have got a matching
	 * name cached in name in which case return the mft reference
	 * associated with it.
	 */
	if (name) {
		kfree(kaddr);
		iput(ia_vi);
		return name->mref;
	}
	ntfs_debug("Entry not found.");
	err = -ENOENT;
unm_err_out:
	kfree(kaddr);
err_out:
	if (!err)
		err = -EIO;
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(dir_ni);
	kfree(name);
	*res = NULL;
	if (!IS_ERR_OR_NULL(ia_vi))
		iput(ia_vi);
	return ERR_MREF(err);
dir_err_out:
	ntfs_error(sb, "Corrupt directory.  Aborting lookup.");
	goto err_out;
}

/*
 * ntfs_filldir - ntfs specific filldir method
 * @vol:	current ntfs volume
 * @ndir:	ntfs inode of current directory
 * @ia_page:	page in which the index allocation buffer @ie is in resides
 * @ie:		current index entry
 * @name:	buffer to use for the converted name
 * @actor:	what to feed the entries to
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
static inline int ntfs_filldir(struct ntfs_volume *vol,
		struct ntfs_inode *ndir, struct page *ia_page, struct index_entry *ie,
		u8 *name, struct dir_context *actor)
{
	unsigned long mref;
	int name_len;
	unsigned int dt_type;
	u8 name_type;

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
	if (!NVolShowHiddenFiles(vol) &&
	    (ie->key.file_name.file_attributes & FILE_ATTR_HIDDEN)) {
		ntfs_debug("Skipping hidden file.");
		return 0;
	}

	name_len = ntfs_ucstonls(vol, (__le16 *)&ie->key.file_name.file_name,
			ie->key.file_name.file_name_length, &name,
			NTFS_MAX_NAME_LEN * NLS_MAX_CHARSET_SIZE + 1);
	if (name_len <= 0) {
		ntfs_warning(vol->sb, "Skipping unrepresentable inode 0x%llx.",
				(long long)MREF_LE(ie->data.dir.indexed_file));
		return 0;
	}

	mref = MREF_LE(ie->data.dir.indexed_file);
	if (ie->key.file_name.file_attributes &
			FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT)
		dt_type = DT_DIR;
	else if (ie->key.file_name.file_attributes & FILE_ATTR_REPARSE_POINT)
		dt_type = ntfs_reparse_tag_dt_types(vol, mref);
	else
		dt_type = DT_REG;

	/*
	 * Drop the page lock otherwise we deadlock with NFS when it calls
	 * ->lookup since ntfs_lookup() will lock the same page.
	 */
	if (ia_page)
		unlock_page(ia_page);
	ntfs_debug("Calling filldir for %s with len %i, fpos 0x%llx, inode 0x%lx, DT_%s.",
		name, name_len, actor->pos, mref, dt_type == DT_DIR ? "DIR" : "REG");
	if (!dir_emit(actor, name, name_len, mref, dt_type))
		return 1;
	/* Relock the page but not if we are aborting ->readdir. */
	if (ia_page)
		lock_page(ia_page);
	return 0;
}

struct ntfs_file_private {
	void *key;
	__le16 key_length;
	bool end_in_iterate;
	loff_t curr_pos;
};

struct ntfs_index_ra {
	unsigned long start_index;
	unsigned int count;
	struct rb_node rb_node;
};

static void ntfs_insert_rb(struct ntfs_index_ra *nir, struct rb_root *root)
{
	struct rb_node **new = &root->rb_node, *parent = NULL;
	struct ntfs_index_ra *cnir;

	while (*new) {
		parent = *new;
		cnir = rb_entry(parent, struct ntfs_index_ra, rb_node);
		if (nir->start_index < cnir->start_index)
			new = &parent->rb_left;
		else if (nir->start_index >= cnir->start_index + cnir->count)
			new = &parent->rb_right;
		else {
			pr_err("nir start index : %ld, count : %d, cnir start_index : %ld, count : %d\n",
				nir->start_index, nir->count, cnir->start_index, cnir->count);
			return;
		}
	}

	rb_link_node(&nir->rb_node, parent, new);
	rb_insert_color(&nir->rb_node, root);
}

static int ntfs_ia_blocks_readahead(struct ntfs_inode *ia_ni, loff_t pos)
{
	unsigned long dir_start_index, dir_end_index;
	struct inode *ia_vi = VFS_I(ia_ni);
	struct file_ra_state *dir_ra;

	dir_end_index = (i_size_read(ia_vi) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dir_start_index = (pos + PAGE_SIZE - 1) >> PAGE_SHIFT;

	if (dir_start_index >= dir_end_index)
		return 0;

	dir_ra = kzalloc(sizeof(*dir_ra), GFP_NOFS);
	if (!dir_ra)
		return -ENOMEM;

	file_ra_state_init(dir_ra, ia_vi->i_mapping);
	dir_end_index = (i_size_read(ia_vi) + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dir_start_index = (pos + PAGE_SIZE - 1) >> PAGE_SHIFT;
	dir_ra->ra_pages = dir_end_index - dir_start_index;
	page_cache_sync_readahead(ia_vi->i_mapping, dir_ra, NULL,
			dir_start_index, dir_end_index - dir_start_index);
	kfree(dir_ra);

	return 0;
}

static int ntfs_readdir(struct file *file, struct dir_context *actor)
{
	struct inode *vdir = file_inode(file);
	struct super_block *sb = vdir->i_sb;
	struct ntfs_inode *ndir = NTFS_I(vdir);
	struct ntfs_volume *vol = NTFS_SB(sb);
	struct ntfs_attr_search_ctx *ctx = NULL;
	struct ntfs_index_context *ictx = NULL;
	u8 *name;
	struct index_root *ir;
	struct index_entry *next = NULL;
	struct ntfs_file_private *private = NULL;
	int err = 0;
	loff_t ie_pos = 2; /* initialize it with dot and dotdot size */
	struct ntfs_index_ra *nir = NULL;
	unsigned long index;
	struct rb_root ra_root = RB_ROOT;
	struct file_ra_state *ra;

	ntfs_debug("Entering for inode 0x%llx, fpos 0x%llx.",
			ndir->mft_no, actor->pos);

	if (file->private_data) {
		private = file->private_data;

		if (actor->pos != private->curr_pos) {
			/*
			 * If actor->pos is different from the previous passed
			 * one, Discard the private->key and fill dirent buffer
			 * with linear lookup.
			 */
			kfree(private->key);
			private->key = NULL;
			private->end_in_iterate = false;
		} else if (private->end_in_iterate) {
			kfree(private->key);
			kfree(file->private_data);
			file->private_data = NULL;
			return 0;
		}
	}

	/* Emulate . and .. for all directories. */
	if (!dir_emit_dots(file, actor))
		return 0;

	/*
	 * Allocate a buffer to store the current name being processed
	 * converted to format determined by current NLS.
	 */
	name = kmalloc(NTFS_MAX_NAME_LEN * NLS_MAX_CHARSET_SIZE + 1, GFP_NOFS);
	if (unlikely(!name))
		return -ENOMEM;

	mutex_lock_nested(&ndir->mrec_lock, NTFS_INODE_MUTEX_PARENT);
	ictx = ntfs_index_ctx_get(ndir, I30, 4);
	if (!ictx) {
		kfree(name);
		mutex_unlock(&ndir->mrec_lock);
		return -ENOMEM;
	}

	ra = kzalloc(sizeof(struct file_ra_state), GFP_NOFS);
	if (!ra) {
		kfree(name);
		ntfs_index_ctx_put(ictx);
		mutex_unlock(&ndir->mrec_lock);
		return -ENOMEM;
	}
	file_ra_state_init(ra, vol->mft_ino->i_mapping);

	if (private && private->key) {
		/*
		 * Find index witk private->key using ntfs_index_lookup()
		 * instead of linear index lookup.
		 */
		err = ntfs_index_lookup(private->key,
					le16_to_cpu(private->key_length),
					ictx);
		if (!err) {
			next = ictx->entry;
			/*
			 * Update ie_pos with private->curr_pos
			 * to make next d_off of dirent correct.
			 */
			ie_pos = private->curr_pos;

			if (actor->pos > vol->mft_record_size && ictx->ia_ni) {
				err = ntfs_ia_blocks_readahead(ictx->ia_ni, actor->pos);
				if (err)
					goto out;
			}

			goto nextdir;
		} else {
			goto out;
		}
	} else if (!private) {
		private = kzalloc(sizeof(struct ntfs_file_private), GFP_KERNEL);
		if (!private) {
			err = -ENOMEM;
			goto out;
		}
		file->private_data = private;
	}

	ctx = ntfs_attr_get_search_ctx(ndir, NULL);
	if (!ctx) {
		err = -ENOMEM;
		goto out;
	}

	/* Find the index root attribute in the mft record. */
	if (ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
				ctx)) {
		ntfs_error(sb, "Index root attribute missing in directory inode %llu",
				ndir->mft_no);
		ntfs_attr_put_search_ctx(ctx);
		err = -ENOMEM;
		goto out;
	}

	/* Get to the index root value. */
	ir = (struct index_root *)((u8 *)ctx->attr +
			le16_to_cpu(ctx->attr->data.resident.value_offset));

	ictx->ir = ir;
	ictx->actx = ctx;
	ictx->parent_vcn[ictx->pindex] = VCN_INDEX_ROOT_PARENT;
	ictx->is_in_root = true;
	ictx->parent_pos[ictx->pindex] = 0;

	ictx->block_size = le32_to_cpu(ir->index_block_size);
	if (ictx->block_size < NTFS_BLOCK_SIZE) {
		ntfs_error(sb, "Index block size (%d) is smaller than the sector size (%d)",
				ictx->block_size, NTFS_BLOCK_SIZE);
		err = -EIO;
		goto out;
	}

	if (vol->cluster_size <= ictx->block_size)
		ictx->vcn_size_bits = vol->cluster_size_bits;
	else
		ictx->vcn_size_bits = NTFS_BLOCK_SIZE_BITS;

	/* The first index entry. */
	next = (struct index_entry *)((u8 *)&ir->index +
			le32_to_cpu(ir->index.entries_offset));

	if (next->flags & INDEX_ENTRY_NODE) {
		ictx->ia_ni = ntfs_ia_open(ictx, ictx->idx_ni);
		if (!ictx->ia_ni) {
			err = -EINVAL;
			goto out;
		}

		err = ntfs_ia_blocks_readahead(ictx->ia_ni, actor->pos);
		if (err)
			goto out;
	}

	if (next->flags & INDEX_ENTRY_NODE) {
		next = ntfs_index_walk_down(next, ictx);
		if (!next) {
			err = -EIO;
			goto out;
		}
	}

	if (next && !(next->flags & INDEX_ENTRY_END))
		goto nextdir;

	while ((next = ntfs_index_next(next, ictx)) != NULL) {
nextdir:
		/* Check the consistency of an index entry */
		if (ntfs_index_entry_inconsistent(ictx, vol, next, COLLATION_FILE_NAME,
					ndir->mft_no)) {
			err = -EIO;
			goto out;
		}

		if (ie_pos < actor->pos) {
			ie_pos += le16_to_cpu(next->length);
			continue;
		}

		actor->pos = ie_pos;

		index = ntfs_mft_no_to_pidx(vol,
				MREF_LE(next->data.dir.indexed_file));
		if (nir) {
			struct ntfs_index_ra *cnir;
			struct rb_node *node = ra_root.rb_node;

			if (nir->start_index <= index &&
			    index < nir->start_index + nir->count) {
				/* No behavior */
				goto filldir;
			}

			while (node) {
				cnir = rb_entry(node, struct ntfs_index_ra, rb_node);
				if (cnir->start_index <= index &&
				    index < cnir->start_index + cnir->count) {
					goto filldir;
				} else if (cnir->start_index + cnir->count == index) {
					cnir->count++;
					goto filldir;
				} else if (!cnir->start_index && cnir->start_index - 1 == index) {
					cnir->start_index = index;
					goto filldir;
				}

				if (index < cnir->start_index)
					node = node->rb_left;
				else if (index >= cnir->start_index + cnir->count)
					node = node->rb_right;
			}

			if (nir->start_index + nir->count == index) {
				nir->count++;
			} else if (!nir->start_index && nir->start_index - 1 == index) {
				nir->start_index = index;
			} else if (nir->count > 2) {
				ntfs_insert_rb(nir, &ra_root);
				nir = NULL;
			} else {
				nir->start_index = index;
				nir->count = 1;
			}
		}

		if (!nir) {
			nir = kzalloc(sizeof(struct ntfs_index_ra), GFP_KERNEL);
			if (nir) {
				nir->start_index = index;
				nir->count = 1;
			}
		}

filldir:
		/* Submit the name to the filldir callback. */
		err = ntfs_filldir(vol, ndir, NULL, next, name, actor);
		if (err) {
			/*
			 * Store index key value to file private_data to start
			 * from current index offset on next round.
			 */
			private = file->private_data;
			kfree(private->key);
			private->key = kmalloc(le16_to_cpu(next->key_length), GFP_KERNEL);
			if (!private->key) {
				err = -ENOMEM;
				goto out;
			}

			memcpy(private->key, &next->key.file_name, le16_to_cpu(next->key_length));
			private->key_length = next->key_length;
			break;
		}
		ie_pos += le16_to_cpu(next->length);
	}

	if (!err)
		private->end_in_iterate = true;
	else
		err = 0;

	private->curr_pos = actor->pos = ie_pos;
out:
	while (!RB_EMPTY_ROOT(&ra_root)) {
		struct ntfs_index_ra *cnir;
		struct rb_node *node;

		node = rb_first(&ra_root);
		cnir = rb_entry(node, struct ntfs_index_ra, rb_node);
		ra->ra_pages = cnir->count;
		page_cache_sync_readahead(vol->mft_ino->i_mapping, ra, NULL,
				cnir->start_index, cnir->count);
		rb_erase(node, &ra_root);
		kfree(cnir);
	}

	if (err) {
		if (private) {
			private->curr_pos = actor->pos;
			private->end_in_iterate = true;
		}
		err = 0;
	}
	ntfs_index_ctx_put(ictx);
	kfree(name);
	kfree(nir);
	kfree(ra);
	mutex_unlock(&ndir->mrec_lock);
	return err;
}

int ntfs_check_empty_dir(struct ntfs_inode *ni, struct mft_record *ni_mrec)
{
	struct ntfs_attr_search_ctx *ctx;
	int ret = 0;

	if (!(ni_mrec->flags & MFT_RECORD_IS_DIRECTORY))
		return 0;

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx) {
		ntfs_error(ni->vol->sb, "Failed to get search context");
		return -ENOMEM;
	}

	/* Find the index root attribute in the mft record. */
	ret = ntfs_attr_lookup(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL,
				0, ctx);
	if (ret) {
		ntfs_error(ni->vol->sb, "Index root attribute missing in directory inode %llu",
				ni->mft_no);
		ntfs_attr_put_search_ctx(ctx);
		return ret;
	}

	/* Non-empty directory? */
	if (le32_to_cpu(ctx->attr->data.resident.value_length) !=
	    sizeof(struct index_root) + sizeof(struct index_entry_header)) {
		/* Both ENOTEMPTY and EEXIST are ok. We use the more common. */
		ret = -ENOTEMPTY;
		ntfs_debug("Directory is not empty\n");
	}

	ntfs_attr_put_search_ctx(ctx);

	return ret;
}

/*
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

static int ntfs_dir_release(struct inode *vi, struct file *filp)
{
	if (filp->private_data) {
		kfree(((struct ntfs_file_private *)filp->private_data)->key);
		kfree(filp->private_data);
		filp->private_data = NULL;
	}
	return 0;
}

/*
 * ntfs_dir_fsync - sync a directory to disk
 * @filp:	file describing the directory to be synced
 * @start:	start offset to be synced
 * @end:	end offset to be synced
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
 */
static int ntfs_dir_fsync(struct file *filp, loff_t start, loff_t end,
			  int datasync)
{
	struct inode *bmp_vi, *vi = filp->f_mapping->host;
	struct ntfs_volume *vol = NTFS_I(vi)->vol;
	struct ntfs_inode *ni = NTFS_I(vi);
	struct ntfs_attr_search_ctx *ctx;
	struct inode *parent_vi, *ia_vi;
	int err, ret;
	struct ntfs_attr na;

	ntfs_debug("Entering for inode 0x%llx.", ni->mft_no);

	if (NVolShutdown(vol))
		return -EIO;

	ctx = ntfs_attr_get_search_ctx(ni, NULL);
	if (!ctx)
		return -ENOMEM;

	mutex_lock_nested(&ni->mrec_lock, NTFS_INODE_MUTEX_NORMAL_CHILD);
	while (!(err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0, NULL, 0, ctx))) {
		struct file_name_attr *fn = (struct file_name_attr *)((u8 *)ctx->attr +
				le16_to_cpu(ctx->attr->data.resident.value_offset));

		if (MREF_LE(fn->parent_directory) == ni->mft_no)
			continue;

		parent_vi = ntfs_iget(vi->i_sb, MREF_LE(fn->parent_directory));
		if (IS_ERR(parent_vi))
			continue;
		mutex_lock_nested(&NTFS_I(parent_vi)->mrec_lock, NTFS_INODE_MUTEX_NORMAL);
		ia_vi = ntfs_index_iget(parent_vi, I30, 4);
		mutex_unlock(&NTFS_I(parent_vi)->mrec_lock);
		if (IS_ERR(ia_vi)) {
			iput(parent_vi);
			continue;
		}
		write_inode_now(ia_vi, 1);
		iput(ia_vi);
		write_inode_now(parent_vi, 1);
		iput(parent_vi);
	}
	mutex_unlock(&ni->mrec_lock);
	ntfs_attr_put_search_ctx(ctx);

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;
	inode_lock(vi);

	/* If the bitmap attribute inode is in memory sync it, too. */
	na.mft_no = vi->i_ino;
	na.type = AT_BITMAP;
	na.name = I30;
	na.name_len = 4;
	bmp_vi = ilookup5(vi->i_sb, vi->i_ino, ntfs_test_inode, &na);
	if (bmp_vi) {
		write_inode_now(bmp_vi, !datasync);
		iput(bmp_vi);
	}
	ret = __ntfs_write_inode(vi, 1);

	write_inode_now(vi, !datasync);

	write_inode_now(vol->mftbmp_ino, 1);
	down_write(&vol->lcnbmp_lock);
	write_inode_now(vol->lcnbmp_ino, 1);
	up_write(&vol->lcnbmp_lock);
	write_inode_now(vol->mft_ino, 1);

	err = sync_blockdev(vi->i_sb->s_bdev);
	if (unlikely(err && !ret))
		ret = err;
	if (likely(!ret))
		ntfs_debug("Done.");
	else
		ntfs_warning(vi->i_sb,
			"Failed to f%ssync inode 0x%llx.  Error %u.",
			datasync ? "data" : "", ni->mft_no, -ret);
	inode_unlock(vi);
	return ret;
}

const struct file_operations ntfs_dir_ops = {
	.llseek		= generic_file_llseek,	/* Seek inside directory. */
	.read		= generic_read_dir,	/* Return -EISDIR. */
	.iterate_shared	= ntfs_readdir,		/* Read directory contents. */
	.fsync		= ntfs_dir_fsync,	/* Sync a directory to disk. */
	.open		= ntfs_dir_open,	/* Open directory. */
	.release	= ntfs_dir_release,
	.unlocked_ioctl	= ntfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ntfs_compat_ioctl,
#endif
	.setlease	= generic_setlease,
};
