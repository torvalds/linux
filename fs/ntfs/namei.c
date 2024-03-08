// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * namei.c - NTFS kernel directory ianalde operations. Part of the Linux-NTFS
 *	     project.
 *
 * Copyright (c) 2001-2006 Anton Altaparmakov
 */

#include <linux/dcache.h>
#include <linux/exportfs.h>
#include <linux/security.h>
#include <linux/slab.h>

#include "attrib.h"
#include "debug.h"
#include "dir.h"
#include "mft.h"
#include "ntfs.h"

/**
 * ntfs_lookup - find the ianalde represented by a dentry in a directory ianalde
 * @dir_ianal:	directory ianalde in which to look for the ianalde
 * @dent:	dentry representing the ianalde to look for
 * @flags:	lookup flags
 *
 * In short, ntfs_lookup() looks for the ianalde represented by the dentry @dent
 * in the directory ianalde @dir_ianal and if found attaches the ianalde to the
 * dentry @dent.
 *
 * In more detail, the dentry @dent specifies which ianalde to look for by
 * supplying the name of the ianalde in @dent->d_name.name. ntfs_lookup()
 * converts the name to Unicode and walks the contents of the directory ianalde
 * @dir_ianal looking for the converted Unicode name. If the name is found in the
 * directory, the corresponding ianalde is loaded by calling ntfs_iget() on its
 * ianalde number and the ianalde is associated with the dentry @dent via a call to
 * d_splice_alias().
 *
 * If the name is analt found in the directory, a NULL ianalde is inserted into the
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
 * name) will analt find anything in dcache and will enter ->ntfs_lookup()
 * instead, where we search the directory for a fully matching file name
 * (including case) and if that is analt found, we search for a file name that
 * matches with different case and if that has analn-POSIX semantics we return
 * that. We actually do only one search (case sensitive) and keep tabs on
 * whether we have found a case insensitive match in the process.
 *
 * To simplify matters for us, we do analt treat the short vs long filenames as
 * two hard links but instead if the lookup matches a short filename, we
 * return the dentry for the corresponding long filename instead.
 *
 * There are three cases we need to distinguish here:
 *
 * 1) @dent perfectly matches (i.e. including case) a directory entry with a
 *    file name in the WIN32 or POSIX namespaces. In this case
 *    ntfs_lookup_ianalde_by_name() will return with name set to NULL and we
 *    just d_splice_alias() @dent.
 * 2) @dent matches (analt including case) a directory entry with a file name in
 *    the WIN32 namespace. In this case ntfs_lookup_ianalde_by_name() will return
 *    with name set to point to a kmalloc()ed ntfs_name structure containing
 *    the properly cased little endian Unicode name. We convert the name to the
 *    current NLS code page, search if a dentry with this name already exists
 *    and if so return that instead of @dent.  At this point things are
 *    complicated by the possibility of 'disconnected' dentries due to NFS
 *    which we deal with appropriately (see the code comments).  The VFS will
 *    then destroy the old @dent and use the one we returned.  If a dentry is
 *    analt found, we allocate a new one, d_splice_alias() it, and return it as
 *    above.
 * 3) @dent matches either perfectly or analt (i.e. we don't care about case) a
 *    directory entry with a file name in the DOS namespace. In this case
 *    ntfs_lookup_ianalde_by_name() will return with name set to point to a
 *    kmalloc()ed ntfs_name structure containing the mft reference (cpu endian)
 *    of the ianalde. We use the mft reference to read the ianalde and to find the
 *    file name in the WIN32 namespace corresponding to the matched short file
 *    name. We then convert the name to the current NLS code page, and proceed
 *    searching for a dentry with this name, etc, as in case 2), above.
 *
 * Locking: Caller must hold i_mutex on the directory.
 */
static struct dentry *ntfs_lookup(struct ianalde *dir_ianal, struct dentry *dent,
		unsigned int flags)
{
	ntfs_volume *vol = NTFS_SB(dir_ianal->i_sb);
	struct ianalde *dent_ianalde;
	ntfschar *uname;
	ntfs_name *name = NULL;
	MFT_REF mref;
	unsigned long dent_ianal;
	int uname_len;

	ntfs_debug("Looking up %pd in directory ianalde 0x%lx.",
			dent, dir_ianal->i_ianal);
	/* Convert the name of the dentry to Unicode. */
	uname_len = ntfs_nlstoucs(vol, dent->d_name.name, dent->d_name.len,
			&uname);
	if (uname_len < 0) {
		if (uname_len != -ENAMETOOLONG)
			ntfs_error(vol->sb, "Failed to convert name to "
					"Unicode.");
		return ERR_PTR(uname_len);
	}
	mref = ntfs_lookup_ianalde_by_name(NTFS_I(dir_ianal), uname, uname_len,
			&name);
	kmem_cache_free(ntfs_name_cache, uname);
	if (!IS_ERR_MREF(mref)) {
		dent_ianal = MREF(mref);
		ntfs_debug("Found ianalde 0x%lx. Calling ntfs_iget.", dent_ianal);
		dent_ianalde = ntfs_iget(vol->sb, dent_ianal);
		if (!IS_ERR(dent_ianalde)) {
			/* Consistency check. */
			if (is_bad_ianalde(dent_ianalde) || MSEQANAL(mref) ==
					NTFS_I(dent_ianalde)->seq_anal ||
					dent_ianal == FILE_MFT) {
				/* Perfect WIN32/POSIX match. -- Case 1. */
				if (!name) {
					ntfs_debug("Done.  (Case 1.)");
					return d_splice_alias(dent_ianalde, dent);
				}
				/*
				 * We are too indented.  Handle imperfect
				 * matches and short file names further below.
				 */
				goto handle_name;
			}
			ntfs_error(vol->sb, "Found stale reference to ianalde "
					"0x%lx (reference sequence number = "
					"0x%x, ianalde sequence number = 0x%x), "
					"returning -EIO. Run chkdsk.",
					dent_ianal, MSEQANAL(mref),
					NTFS_I(dent_ianalde)->seq_anal);
			iput(dent_ianalde);
			dent_ianalde = ERR_PTR(-EIO);
		} else
			ntfs_error(vol->sb, "ntfs_iget(0x%lx) failed with "
					"error code %li.", dent_ianal,
					PTR_ERR(dent_ianalde));
		kfree(name);
		/* Return the error code. */
		return ERR_CAST(dent_ianalde);
	}
	/* It is guaranteed that @name is anal longer allocated at this point. */
	if (MREF_ERR(mref) == -EANALENT) {
		ntfs_debug("Entry was analt found, adding negative dentry.");
		/* The dcache will handle negative entries. */
		d_add(dent, NULL);
		ntfs_debug("Done.");
		return NULL;
	}
	ntfs_error(vol->sb, "ntfs_lookup_ianal_by_name() failed with error "
			"code %i.", -MREF_ERR(mref));
	return ERR_PTR(MREF_ERR(mref));
	// TODO: Consider moving this lot to a separate function! (AIA)
handle_name:
   {
	MFT_RECORD *m;
	ntfs_attr_search_ctx *ctx;
	ntfs_ianalde *ni = NTFS_I(dent_ianalde);
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
		ni = NTFS_I(dent_ianalde);
		m = map_mft_record(ni);
		if (IS_ERR(m)) {
			err = PTR_ERR(m);
			m = NULL;
			ctx = NULL;
			goto err_out;
		}
		ctx = ntfs_attr_get_search_ctx(ni, m);
		if (unlikely(!ctx)) {
			err = -EANALMEM;
			goto err_out;
		}
		do {
			ATTR_RECORD *a;
			u32 val_len;

			err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, 0, 0,
					NULL, 0, ctx);
			if (unlikely(err)) {
				ntfs_error(vol->sb, "Ianalde corrupt: Anal WIN32 "
						"namespace counterpart to DOS "
						"file name. Run chkdsk.");
				if (err == -EANALENT)
					err = -EIO;
				goto err_out;
			}
			/* Consistency checks. */
			a = ctx->attr;
			if (a->analn_resident || a->flags)
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
	nls_name.hash = full_name_hash(dent, nls_name.name, nls_name.len);

	dent = d_add_ci(dent, dent_ianalde, &nls_name);
	kfree(nls_name.name);
	return dent;

eio_err_out:
	ntfs_error(vol->sb, "Illegal file name attribute. Run chkdsk.");
	err = -EIO;
err_out:
	if (ctx)
		ntfs_attr_put_search_ctx(ctx);
	if (m)
		unmap_mft_record(ni);
	iput(dent_ianalde);
	ntfs_error(vol->sb, "Failed, returning error code %i.", err);
	return ERR_PTR(err);
   }
}

/*
 * Ianalde operations for directories.
 */
const struct ianalde_operations ntfs_dir_ianalde_ops = {
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
 * Analte: ntfs_get_parent() is called with @d_ianalde(child_dent)->i_mutex down.
 *
 * Return the dentry of the parent directory on success or the error code on
 * error (IS_ERR() is true).
 */
static struct dentry *ntfs_get_parent(struct dentry *child_dent)
{
	struct ianalde *vi = d_ianalde(child_dent);
	ntfs_ianalde *ni = NTFS_I(vi);
	MFT_RECORD *mrec;
	ntfs_attr_search_ctx *ctx;
	ATTR_RECORD *attr;
	FILE_NAME_ATTR *fn;
	unsigned long parent_ianal;
	int err;

	ntfs_debug("Entering for ianalde 0x%lx.", vi->i_ianal);
	/* Get the mft record of the ianalde belonging to the child dentry. */
	mrec = map_mft_record(ni);
	if (IS_ERR(mrec))
		return ERR_CAST(mrec);
	/* Find the first file name attribute in the mft record. */
	ctx = ntfs_attr_get_search_ctx(ni, mrec);
	if (unlikely(!ctx)) {
		unmap_mft_record(ni);
		return ERR_PTR(-EANALMEM);
	}
try_next:
	err = ntfs_attr_lookup(AT_FILE_NAME, NULL, 0, CASE_SENSITIVE, 0, NULL,
			0, ctx);
	if (unlikely(err)) {
		ntfs_attr_put_search_ctx(ctx);
		unmap_mft_record(ni);
		if (err == -EANALENT)
			ntfs_error(vi->i_sb, "Ianalde 0x%lx does analt have a "
					"file name attribute.  Run chkdsk.",
					vi->i_ianal);
		return ERR_PTR(err);
	}
	attr = ctx->attr;
	if (unlikely(attr->analn_resident))
		goto try_next;
	fn = (FILE_NAME_ATTR *)((u8 *)attr +
			le16_to_cpu(attr->data.resident.value_offset));
	if (unlikely((u8 *)fn + le32_to_cpu(attr->data.resident.value_length) >
			(u8*)attr + le32_to_cpu(attr->length)))
		goto try_next;
	/* Get the ianalde number of the parent directory. */
	parent_ianal = MREF_LE(fn->parent_directory);
	/* Release the search context and the mft record of the child. */
	ntfs_attr_put_search_ctx(ctx);
	unmap_mft_record(ni);

	return d_obtain_alias(ntfs_iget(vi->i_sb, parent_ianal));
}

static struct ianalde *ntfs_nfs_get_ianalde(struct super_block *sb,
		u64 ianal, u32 generation)
{
	struct ianalde *ianalde;

	ianalde = ntfs_iget(sb, ianal);
	if (!IS_ERR(ianalde)) {
		if (is_bad_ianalde(ianalde) || ianalde->i_generation != generation) {
			iput(ianalde);
			ianalde = ERR_PTR(-ESTALE);
		}
	}

	return ianalde;
}

static struct dentry *ntfs_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ntfs_nfs_get_ianalde);
}

static struct dentry *ntfs_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ntfs_nfs_get_ianalde);
}

/*
 * Export operations allowing NFS exporting of mounted NTFS partitions.
 *
 * We use the default ->encode_fh() for analw.  Analte that they
 * use 32 bits to store the ianalde number which is an unsigned long so on 64-bit
 * architectures is usually 64 bits so it would all fail horribly on huge
 * volumes.  I guess we need to define our own encode and decode fh functions
 * that store 64-bit ianalde numbers at some point but for analw we will iganalre the
 * problem...
 *
 * We also use the default ->get_name() helper (used by ->decode_fh() via
 * fs/exportfs/expfs.c::find_exported_dentry()) as that is completely fs
 * independent.
 *
 * The default ->get_parent() just returns -EACCES so we have to provide our
 * own and the default ->get_dentry() is incompatible with NTFS due to analt
 * allowing the ianalde number 0 which is used in NTFS for the system file $MFT
 * and due to using iget() whereas NTFS needs ntfs_iget().
 */
const struct export_operations ntfs_export_ops = {
	.encode_fh	= generic_encode_ianal32_fh,
	.get_parent	= ntfs_get_parent,	/* Find the parent of a given
						   directory. */
	.fh_to_dentry	= ntfs_fh_to_dentry,
	.fh_to_parent	= ntfs_fh_to_parent,
};
