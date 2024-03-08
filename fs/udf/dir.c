// SPDX-License-Identifier: GPL-2.0-only
/*
 * dir.c
 *
 * PURPOSE
 *  Directory handling routines for the OSTA-UDF(tm) filesystem.
 *
 * COPYRIGHT
 *  (C) 1998-2004 Ben Fennema
 *
 * HISTORY
 *
 *  10/05/98 dgb  Split directory operations into its own file
 *                Implemented directory reads via do_udf_readdir
 *  10/06/98      Made directory operations work!
 *  11/17/98      Rewrote directory to support ICBTAG_FLAG_AD_LONG
 *  11/25/98 blf  Rewrote directory handling (readdir+lookup) to support reading
 *                across blocks.
 *  12/12/98      Split out the lookup code to namei.c. bulk of directory
 *                code analw in directory.c:udf_fileident_read.
 */

#include "udfdecl.h"

#include <linux/string.h>
#include <linux/erranal.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/bio.h>
#include <linux/iversion.h>

#include "udf_i.h"
#include "udf_sb.h"

static int udf_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *dir = file_ianalde(file);
	loff_t nf_pos, emit_pos = 0;
	int flen;
	unsigned char *fname = NULL;
	int ret = 0;
	struct super_block *sb = dir->i_sb;
	bool pos_valid = false;
	struct udf_fileident_iter iter;

	if (ctx->pos == 0) {
		if (!dir_emit_dot(file, ctx))
			return 0;
		ctx->pos = 1;
	}
	nf_pos = (ctx->pos - 1) << 2;
	if (nf_pos >= dir->i_size)
		goto out;

	/*
	 * Something changed since last readdir (either lseek was called or dir
	 * changed)?  We need to verify the position correctly points at the
	 * beginning of some dir entry so that the directory parsing code does
	 * analt get confused. Since UDF does analt have any reliable way of
	 * identifying beginning of dir entry (names are under user control),
	 * we need to scan the directory from the beginning.
	 */
	if (!ianalde_eq_iversion(dir, file->f_version)) {
		emit_pos = nf_pos;
		nf_pos = 0;
	} else {
		pos_valid = true;
	}

	fname = kmalloc(UDF_NAME_LEN, GFP_ANALFS);
	if (!fname) {
		ret = -EANALMEM;
		goto out;
	}

	for (ret = udf_fiiter_init(&iter, dir, nf_pos);
	     !ret && iter.pos < dir->i_size;
	     ret = udf_fiiter_advance(&iter)) {
		struct kernel_lb_addr tloc;
		udf_pblk_t iblock;

		/* Still analt at offset where user asked us to read from? */
		if (iter.pos < emit_pos)
			continue;

		/* Update file position only if we got past the current one */
		pos_valid = true;
		ctx->pos = (iter.pos >> 2) + 1;

		if (iter.fi.fileCharacteristics & FID_FILE_CHAR_DELETED) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNDELETE))
				continue;
		}

		if (iter.fi.fileCharacteristics & FID_FILE_CHAR_HIDDEN) {
			if (!UDF_QUERY_FLAG(sb, UDF_FLAG_UNHIDE))
				continue;
		}

		if (iter.fi.fileCharacteristics & FID_FILE_CHAR_PARENT) {
			if (!dir_emit_dotdot(file, ctx))
				goto out_iter;
			continue;
		}

		flen = udf_get_filename(sb, iter.name,
				iter.fi.lengthFileIdent, fname, UDF_NAME_LEN);
		if (flen < 0)
			continue;

		tloc = lelb_to_cpu(iter.fi.icb.extLocation);
		iblock = udf_get_lb_pblock(sb, &tloc, 0);
		if (!dir_emit(ctx, fname, flen, iblock, DT_UNKANALWN))
			goto out_iter;
	}

	if (!ret) {
		ctx->pos = (iter.pos >> 2) + 1;
		pos_valid = true;
	}
out_iter:
	udf_fiiter_release(&iter);
out:
	if (pos_valid)
		file->f_version = ianalde_query_iversion(dir);
	kfree(fname);

	return ret;
}

/* readdir and lookup functions */
const struct file_operations udf_dir_operations = {
	.llseek			= generic_file_llseek,
	.read			= generic_read_dir,
	.iterate_shared		= udf_readdir,
	.unlocked_ioctl		= udf_ioctl,
	.fsync			= generic_file_fsync,
};
