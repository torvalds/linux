/*
 * namei.c - NTFS kernel directory inode operations. Part of the Linux-NTFS
 *	     project.
 *
 * Copyright (c) 2001-2006 Anton Altaparmakov
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

#include <linux/dcache.h>
#include <linux/exportfs.h>
#include <linux/security.h>

#include "attrib.h"
#include "debug.h"
#include "dir.h"
#include "mft.h"
#include "ntfs.h"

/**
 * ntfs_lookup - find the inode represented by a dentry in a directory inode
 * @dir_ino:	directory inode in which to look for the inode
 * @dent:	dentry representing the inode to look for
 * @nd:		lookup nameidata
 *
 * In short, ntfs_lookup() looks for the inode represented by the dentry @dent
 * in the directory inode @dir_ino and if found attaches the inode to the
 * dentry @dent.
 *
 * In more detail, the dentry @dent specifies which inode to look for by
 * supplying the name of the inode in @dent->d_name.name. ntfs_lookup()
 * converts the name to Unicode and walks the contents of the directory inode
 * @dir_ino looking for the converted Unicode name. If the name is found in the
 * directory, the corresponding inode is loaded by calling ntfs_iget() on its
 * inode number and the inode is associated with the dentry @dent via a call to
 * d_splice_alias().
 *
 * If the name is not found in the directory, a NULL inode is inserted into the
 * dentry @dent via a call to d_add(). The dentry is then termed a negative
 * dentry.
 *
 * Only if an actual error occurs, do we return an error via ERR_PTR().
 *
 * In order to handle the case insensitivity issues of NTFS with regards to the
 * dcache and the dcache requiring only one dentry per directory, we deal with
 * dentry aliases that only differ in case in ->ntfs_lookup() while maintaining
 * a case sensitive dcache. This means that we get the full benefit of dcache
 * speed when the file/directory is looked up with the same case as returned by
 * ->ntfs_readdir() but that a lookup for any other case (or for the short file
 * name) will not find anything in dcache and will enter ->ntfs_lookup()
 * instead, where we search the directory for a fully matching file name
 * (including case) and if that is not found, we search for a file name that
 * matches with different case and if that has non-POSIX semantics we return
 * that. We actually do only one search (case sensitive) and keep tabs on
 * whether we have found a case insensitive match in the process.
 *
 * To simplify matters for us, we do not treat the short vs long filenames as
 * two hard links but instead if the lookup matches a short filename, we
 * return the dentry for the corresponding long filename instead.
 *
 * There are three cases we need to distinguish here:
 *
 * 1) @dent perfectly matches (i.e. including case) a directory entry with a
 *    file name in the WIN32 or POSIX namespaces. In this case
 *    ntfs_lookup_inode_by_name() will return with name set to NULL and we
 *    just d_splice_alias() @dent.
 * 2) @dent matches (not including case) a directory entry with a file name in
 *    the WIN32 namespace. In this case ntfs_lookup_inode_by_name() will return
 *    with name set to point to a kmalloc()ed ntfs_name structure containing
 *    the properly cased little endian Unicode name. We convert the name to the
 *    current NLS code page, search if a dentry with this name already exists
 *    and if so return that instead of @dent.  At this point things are
 *    complicated by the possibility of 'disconnected' dentries due to NFS
 *    which we deal with appropriately (see the code comments).  The VFS will
 *    then destroy the old @dent and use the one we returned.  If a dentry is
 *    not found, we allocate a new one, d_splice_alias() it, and return it as
 *    above.
 * 3) @dent matches either perfectly or not (i.e. we don't care about case) a
 *    directory entry with a file name in the DOS namespace. In this case
 *    ntfs_lookup_inode_by_name() will return with name set to point to a
 *    kmalloc()ed ntfs_name structure containing the mft reference (cpu endian)
 *    of the inode. We use the mft reference to read the inode and to find the
 *    file name in the WIN32 namespace corresponding to the matched short file
 *    name. We then convert the name to the current NLS code page, and proceed
 *    searching for a dentry with this name, etc, as in case 2), above.
 *
 * Locking: Caller must hold i_mutex on the directory.
 */
static struct dentry *ntfs_lookup(struct inode *dir_ino, struct dentry *dent,
		struct nameidata *nd)
{
	ntfs_volume *vol = NTFS_SB(dir_ino->i_sb);
	struct inode *dent_inode;
	ntfschar *uname;
	ntfs_name *name = NULL;
	MFT_REF mref;
	unsigned long dent_ino;
	int uname_len;

	ntfs_debug("Looking up %s in directory inode 0x%lx.",
			dent->d_name.name, dir_ino->i_ino);
	/* Convert the name of the dentry to Unicode. */
	uname_len = ntfs_nlstoucs(vol, dent->d_name.name, dent->d_name.len,
			&uname);
	if (uname_len < 0) {
		if (uname_len != -ENAMETOOLONG)
			ntfs_error(vol->sb, "Failed to convert name to "
					"Unicode.");
		return ERR_PTR(uname_len);
	}
	mref = ntfs_lookup_inode_by_name(NTFS_I(dir_ino), uname, uname_len,
			&name);
	kmem_cache_free(ntfs_name_cache, uname);
	if (!IS_ERR_MREF(mref)) {
		dent_ino = MREF(mref);
		ntfs_debug("Found inode 0x%lx. Calling ntfs_iget.", dent_ino);
		dent_inode = ntfs_iget(vol->sb, dent_ino);
		if (likely(!IS_ERR(dent_inode))) {
			/* Consistency check. */
			if (is_bad_inode(dent_inode) || MSEQNO(mref) ==
					NTFS_I(dent_inode)->seq_no ||
					dent_ino == FILE_MFT) {
				/* Perfect WIN32/POSIX match. -- Case 1. */
				if (!name) {
					ntfs_debug("Done.  (Case 1.)");
					return d_splice_alias(dent_inode, dent);
				}
				/*
				 * We are too indented.  Handle imperfect
				 * matches and short file names further below.
				 */
				goto handle_name;
			}
			ntfs_error(vol->sb, "Found stale reference to inode "
					"0x%lx (reference sequence number = "
					"0x%x, inode sequence number = 0x%x), "
					"returning -EIO. Run chkdsk.",
					dent_ino, MSEQNO(mref),
					NTFS_I(dent_inode)->seq_no);
			iput(dent_inode);
			dent_inode = ERR_PTR(-EIO);
		} else
			ntfs_error(vol->sb, "ntfs_iget(0x%lx) failed with "
					"error code %li.", dent_ino,
					PTR_ERR(dent_inode));
		kfree(name);
		/* Return the error code. */
		return (struct dentry *)dent_inode;
	}
	/* It is guaranteed that @name is no longer allocated at this point. */
	if (MREF_ERR(mref) == -ENOENT) {
		ntfs_debug("Entry was not found, adding negative dentry.");
		/* The dcache will handle negative entries. */
		d_add(dent, NULL);
		ntfs_debug("Done.");
		return NULL;
	}
	ntfs_error(vol->sb, "ntfs_lookup_ino_by_name() failed with error "
			"code %i.", -MREF_ERR(mref));
	return ERR_PTR(MREF_ERR(mref));
	// TODO: Consider moving this lot to a separate function! (AIA)
handle_name:
   {
	struct dentry *real_dent, *new_dent;
	MFT_RECORD *m;
	ntfs_attr_search_ctx *ctx;
	ntfs_inode *ni = NTFS_I(dent_inode);
	int err;
	struct qstr nls_name;

	nls_name.name = NULL;
	if (name->type != FILE_NAME_DOS) {			/* Case 2. */
		ntfs_debug("Case 2.");
		nls_name.len = (unsigned)ntfs_ucstonls(vol,
				(ntfschar*)&name->name, name->len,
				(unsigned char**)&nls_name.name, 0);
		kfree(name);
	} else /* if (name->type == FILE_NAME_DOS) */ {		/* Case 3. */
		FILE_NAME_ATTR *fn;

		ntfs_debug("Case 3.");
		kfree(name);

		/* Find the WIN32 name corresponding to the matched DOS name. */
		ni = NTFS_I(dent_inode);
		m = map_mft_record(ni);
		if (IS_ERR(m)) {
			err = PTR_ERR(m);
			m = NULL;
			ctx = NULL;
			goto err_out;
		}
		ctx = ntfs_attr_get_search_ctx(ni, m);
		if (unlikely(!ctx)) {
			err = -ENOMEM;
			goto err_out;
		}
		do {
			ATTR_RECORD *a;
			u32 val_len;

			err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0,
					NULL, 0, ctx);
			if (unlikely(err)) {
				ntfs_error(vol->sb, "Inode corrupt: No WIN32 "
						"namespace counterpart to DOS "
						"file name. Run chkdsk.");
				if (err == -ENOENT)
					err = -EIO;
				goto err_out;
			}
			/* Consistency checks. */
			a = ctx->attr;
			if (a->non_resident || a->flags)
				goto eio_err_out;
			val_len = le32_to_cpu(a->data.resident.value_length);
			if (le16_to_cpu(a->data.resident.value_offset) +
					val_len > le32_to_cpu(a->length))
				goto eio_err_out;
			fn = (FILE_NAME_ATTR*)((u8*)ctx->attr + le16_to_cpu(
					ctx->attr->data.resident.value_offset));
			if ((u32)(fn->file_name_length * sizeof(ntfschar) +
					sizeof(FILE_NAME_ATTR)) > val_len)
				goto eio_err_out;
		} while (fn->file_name_type != FILE_NAME_WIN32);

		/* Convert the found WIN32 name to current NLS code page. */
		nls_name.len = (unsigned)ntfs_ucstonls(vol,
				(ntfschar*)&fn->file_name, fn->file_name_length,
				(unsigned char**)&nls_name.name, 0);

		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
	}
	m = NULL;
	ctx = NULL;

	/* Check if a conversion error occurred. */
	if ((signed)nls_name.len < 0) {
		err = (signed)nls_name.len;
		goto err_out;
	}
	nls_name.hash = full_name_hash(nls_name.name, nls_name.len);

	/*
	 * Note: No need for dent->d_lock lock as i_mutex is held on the
	 * parent inode.
	 */

	/* Does a dentry matching the nls_name exist already? */
	real_dent = d_lookup(dent->d_parent, &nls_name);
	/* If not, create it now. */
	if (!real_dent) {
		real_dent = d_alloc(dent->d_parent, &nls_name);
		kfree(nls_name.name);
		if (!real_dent) {
			err = -ENOMEM;
			goto err_out;
		}
		new_dent = d_splice_alias(dent_inode, real_dent);
		if (new_dent)
			dput(real_dent);
		else
			new_dent = real_dent;
		ntfs_debug("Done.  (Created new dentry.)");
		return new_dent;
	}
	kfree(nls_name.name);
	/* Matching dentry exists, check if it is negative. */
	if (real_dent->d_inode) {
		if (unlikely(real_dent->d_inode != dent_inode)) {
			/* This can happen because bad inodes are unhashed. */
			BUG_ON(!is_bad_inode(dent_inode));
			BUG_ON(!is_bad_inode(real_dent->d_inode));
		}
		/*
		 * Already have the inode and the dentry attached, decrement
		 * the reference count to balance the ntfs_iget() we did
		 * earlier on.  We found the dentry using d_lookup() so it
		 * cannot be disconnected and thus we do not need to worry
		 * about any NFS/disconnectedness issues here.
		 */
		iput(dent_inode);
		ntfs_debug("Done.  (Already had inode and dentry.)");
		return real_dent;
	}
	/*
	 * Negative dentry: instantiate it unless the inode is a directory and
	 * has a 'disconnected' dentry (i.e. IS_ROOT and DCACHE_DISCONNECTED),
	 * in which case d_move() that in place of the found dentry.
	 */
	if (!S_ISDIR(dent_inode->i_mode)) {
		/* Not a directory; everything is easy. */
		d_instantiate(real_dent, dent_inode);
		ntfs_debug("Done.  (Already had negative file dentry.)");
		return real_dent;
	}
	spin_lock(&dcache_lock);
	if (list_empty(&dent_inode->i_dentry)) {
		/*
		 * Directory without a 'disconnected' dentry; we need to do
		 * d_instantiate() by hand because it takes dcache_lock which
		 * we already hold.
		 */
		list_add(&real_dent->d_alias, &dent_inode->i_dentry);
		real_dent->d_inode = dent_inode;
		spin_unlock(&dcache_lock);
		security_d_instantiate(real_dent, dent_inode);
		ntfs_debug("Done.  (Already had negative directory dentry.)");
		return real_dent;
	}
	/*
	 * Directory with a 'disconnected' dentry; get a reference to the
	 * 'disconnected' dentry.
	 */
	new_dent = list_entry(dent_inode->i_dentry.next, struct dentry,
			d_alias);
	dget_locked(new_dent);
	spin_unlock(&dcache_lock);
	/* Do security vodoo. */
	security_d_instantiate(real_dent, dent_inode);
	/* Move new_dent in place of real_dent. */
	d_move(new_dent, real_dent);
	/* Balance the ntfs_iget() we did above. */
	iput(dent_inode);
	/* Throw away real_dent. */
	dput(real_dent);
	/* Use new_dent as the actual dentry. */
	ntfs_debug("Done.  (Already had negative, disconnected directory "
			"dentry.)");
	return new_dent;

eio_err_out:
	ntfs_error(vol->sb, "Illegal file name attribute. Run chkdsk.");
	err = -EIO;
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(ni);
	iput(dent_inode);
	ntfs_error(vol->sb, "Failed, returning error code %i.", err);
	return ERR_PTR(err);
   }
}

/**
 * Inode operations for directories.
 */
const struct inode_operations ntfs_dir_inode_ops = {
	.lookup	= ntfs_lookup,	/* VFS: Lookup directory. */
};

/**
 * ntfs_get_parent - find the dentry of the parent of a given directory dentry
 * @child_dent:		dentry of the directory whose parent directory to find
 *
 * Find the dentry for the parent directory of the directory specified by the
 * dentry @child_dent.  This function is called from
 * fs/exportfs/expfs.c::find_exported_dentry() which in turn is called from the
 * default ->decode_fh() which is export_decode_fh() in the same file.
 *
 * The code is based on the ext3 ->get_parent() implementation found in
 * fs/ext3/namei.c::ext3_get_parent().
 *
 * Note: ntfs_get_parent() is called with @child_dent->d_inode->i_mutex down.
 *
 * Return the dentry of the parent directory on success or the error code on
 * error (IS_ERR() is true).
 */
static struct dentry *ntfs_get_parent(struct dentry *child_dent)
{
	struct inode *vi = child_dent->d_inode;
	ntfs_inode *ni = NTFS_I(vi);
	MFT_RECORD *mrec;
	ntfs_attr_search_ctx *ctx;
	ATTR_RECORD *attr;
	FILE_NAME_ATTR *fn;
	struct inode *parent_vi;
	struct dentry *parent_dent;
	unsigned long parent_ino;
	int err;

	ntfs_debug("Entering for inode 0x%lx.", vi->i_ino);
	/* Get the mft record of the inode belonging to the child dentry. */
	mrec = map_mft_record(ni);
	if (IS_ERR(mrec))
		return (struct dentry *)mrec;
	/* Find the first file name attribute in the mft record. */
	ctx = ntfs_attr_get_search_ctx(ni, mrec);
	if (unlikely(!ctx)) {
		unmap_mft_record(ni);
		return ERR_PTR(-ENOMEM);
	}
try_next:
	err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, CASE_SENSITIVE, 0, NULL,
			0, ctx);
	if (unlikely(err)) {
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		if (err == -ENOENT)
			ntfs_error(vi->i_sb, "Inode 0x%lx does not have a "
					"file name attribute.  Run chkdsk.",
					vi->i_ino);
		return ERR_PTR(err);
	}
	attr = ctx->attr;
	if (unlikely(attr->non_resident))
		goto try_next;
	fn = (FILE_NAME_ATTR *)((u8 *)attr +
			le16_to_cpu(attr->data.resident.value_offset));
	if (unlikely((u8 *)fn + le32_to_cpu(attr->data.resident.value_length) >
			(u8*)attr + le32_to_cpu(attr->length)))
		goto try_next;
	/* Get the inode number of the parent directory. */
	parent_ino = MREF_LE(fn->parent_directory);
	/* Release the search context and the mft record of the child. */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ni);
	/* Get the inode of the parent directory. */
	parent_vi = ntfs_iget(vi->i_sb, parent_ino);
	if (IS_ERR(parent_vi) || unlikely(is_bad_inode(parent_vi))) {
		if (!IS_ERR(parent_vi))
			iput(parent_vi);
		ntfs_error(vi->i_sb, "Failed to get parent directory inode "
				"0x%lx of child inode 0x%lx.", parent_ino,
				vi->i_ino);
		return ERR_PTR(-EACCES);
	}
	/* Finally get a dentry for the parent directory and return it. */
	parent_dent = d_alloc_anon(parent_vi);
	if (unlikely(!parent_dent)) {
		iput(parent_vi);
		return ERR_PTR(-ENOMEM);
	}
	ntfs_debug("Done for inode 0x%lx.", vi->i_ino);
	return parent_dent;
}

/**
 * ntfs_get_dentry - find a dentry for the inode from a file handle sub-fragment
 * @sb:		super block identifying the mounted ntfs volume
 * @fh:		the file handle sub-fragment
 *
 * Find a dentry for the inode given a file handle sub-fragment.  This function
 * is called from fs/exportfs/expfs.c::find_exported_dentry() which in turn is
 * called from the default ->decode_fh() which is export_decode_fh() in the
 * same file.  The code is closely based on the default ->get_dentry() helper
 * fs/exportfs/expfs.c::get_object().
 *
 * The @fh contains two 32-bit unsigned values, the first one is the inode
 * number and the second one is the inode generation.
 *
 * Return the dentry on success or the error code on error (IS_ERR() is true).
 */
static struct dentry *ntfs_get_dentry(struct super_block *sb, void *fh)
{
	struct inode *vi;
	struct dentry *dent;
	unsigned long ino = ((u32 *)fh)[0];
	u32 gen = ((u32 *)fh)[1];

	ntfs_debug("Entering for inode 0x%lx, generation 0x%x.", ino, gen);
	vi = ntfs_iget(sb, ino);
	if (IS_ERR(vi)) {
		ntfs_error(sb, "Failed to get inode 0x%lx.", ino);
		return (struct dentry *)vi;
	}
	if (unlikely(is_bad_inode(vi) || vi->i_generation != gen)) {
		/* We didn't find the right inode. */
		ntfs_error(sb, "Inode 0x%lx, bad count: %d %d or version 0x%x "
				"0x%x.", vi->i_ino, vi->i_nlink,
				atomic_read(&vi->i_count), vi->i_generation,
				gen);
		iput(vi);
		return ERR_PTR(-ESTALE);
	}
	/* Now find a dentry.  If possible, get a well-connected one. */
	dent = d_alloc_anon(vi);
	if (unlikely(!dent)) {
		iput(vi);
		return ERR_PTR(-ENOMEM);
	}
	ntfs_debug("Done for inode 0x%lx, generation 0x%x.", ino, gen);
	return dent;
}

/**
 * Export operations allowing NFS exporting of mounted NTFS partitions.
 *
 * We use the default ->decode_fh() and ->encode_fh() for now.  Note that they
 * use 32 bits to store the inode number which is an unsigned long so on 64-bit
 * architectures is usually 64 bits so it would all fail horribly on huge
 * volumes.  I guess we need to define our own encode and decode fh functions
 * that store 64-bit inode numbers at some point but for now we will ignore the
 * problem...
 *
 * We also use the default ->get_name() helper (used by ->decode_fh() via
 * fs/exportfs/expfs.c::find_exported_dentry()) as that is completely fs
 * independent.
 *
 * The default ->get_parent() just returns -EACCES so we have to provide our
 * own and the default ->get_dentry() is incompatible with NTFS due to not
 * allowing the inode number 0 which is used in NTFS for the system file $MFT
 * and due to using iget() whereas NTFS needs ntfs_iget().
 */
struct export_operations ntfs_export_ops = {
	.get_parent	= ntfs_get_parent,	/* Find the parent of a given
						   directory. */
	.get_dentry	= ntfs_get_dentry,	/* Find a dentry for the inode
						   given a file handle
						   sub-fragment. */
};
