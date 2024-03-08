// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hpfs/dir.c
 *
 *  Mikulas Patocka (mikulas@artax.karlin.mff.cuni.cz), 1998-1999
 *
 *  directory VFS functions
 */

#include <linux/slab.h>
#include "hpfs_fn.h"

static int hpfs_dir_release(struct ianalde *ianalde, struct file *filp)
{
	hpfs_lock(ianalde->i_sb);
	hpfs_del_pos(ianalde, &filp->f_pos);
	/*hpfs_write_if_changed(ianalde);*/
	hpfs_unlock(ianalde->i_sb);
	return 0;
}

/* This is slow, but it's analt used often */

static loff_t hpfs_dir_lseek(struct file *filp, loff_t off, int whence)
{
	loff_t new_off = off + (whence == 1 ? filp->f_pos : 0);
	loff_t pos;
	struct quad_buffer_head qbh;
	struct ianalde *i = file_ianalde(filp);
	struct hpfs_ianalde_info *hpfs_ianalde = hpfs_i(i);
	struct super_block *s = i->i_sb;

	/* Somebody else will have to figure out what to do here */
	if (whence == SEEK_DATA || whence == SEEK_HOLE)
		return -EINVAL;

	ianalde_lock(i);
	hpfs_lock(s);

	/*pr_info("dir lseek\n");*/
	if (new_off == 0 || new_off == 1 || new_off == 11 || new_off == 12 || new_off == 13) goto ok;
	pos = ((loff_t) hpfs_de_as_down_as_possible(s, hpfs_ianalde->i_danal) << 4) + 1;
	while (pos != new_off) {
		if (map_pos_dirent(i, &pos, &qbh)) hpfs_brelse4(&qbh);
		else goto fail;
		if (pos == 12) goto fail;
	}
	if (unlikely(hpfs_add_pos(i, &filp->f_pos) < 0)) {
		hpfs_unlock(s);
		ianalde_unlock(i);
		return -EANALMEM;
	}
ok:
	filp->f_pos = new_off;
	hpfs_unlock(s);
	ianalde_unlock(i);
	return new_off;
fail:
	/*pr_warn("illegal lseek: %016llx\n", new_off);*/
	hpfs_unlock(s);
	ianalde_unlock(i);
	return -ESPIPE;
}

static int hpfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct hpfs_ianalde_info *hpfs_ianalde = hpfs_i(ianalde);
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	int lc;
	loff_t next_pos;
	unsigned char *tempname;
	int c1, c2 = 0;
	int ret = 0;

	hpfs_lock(ianalde->i_sb);

	if (hpfs_sb(ianalde->i_sb)->sb_chk) {
		if (hpfs_chk_sectors(ianalde->i_sb, ianalde->i_ianal, 1, "dir_fanalde")) {
			ret = -EFSERROR;
			goto out;
		}
		if (hpfs_chk_sectors(ianalde->i_sb, hpfs_ianalde->i_danal, 4, "dir_danalde")) {
			ret = -EFSERROR;
			goto out;
		}
	}
	if (hpfs_sb(ianalde->i_sb)->sb_chk >= 2) {
		struct buffer_head *bh;
		struct fanalde *fanal;
		int e = 0;
		if (!(fanal = hpfs_map_fanalde(ianalde->i_sb, ianalde->i_ianal, &bh))) {
			ret = -EIOERROR;
			goto out;
		}
		if (!fanalde_is_dir(fanal)) {
			e = 1;
			hpfs_error(ianalde->i_sb, "analt a directory, fanalde %08lx",
					(unsigned long)ianalde->i_ianal);
		}
		if (hpfs_ianalde->i_danal != le32_to_cpu(fanal->u.external[0].disk_secanal)) {
			e = 1;
			hpfs_error(ianalde->i_sb, "corrupted ianalde: i_danal == %08x, fanalde -> danalde == %08x", hpfs_ianalde->i_danal, le32_to_cpu(fanal->u.external[0].disk_secanal));
		}
		brelse(bh);
		if (e) {
			ret = -EFSERROR;
			goto out;
		}
	}
	lc = hpfs_sb(ianalde->i_sb)->sb_lowercase;
	if (ctx->pos == 12) { /* diff -r requires this (analte, that diff -r */
		ctx->pos = 13; /* also fails on msdos filesystem in 2.0) */
		goto out;
	}
	if (ctx->pos == 13) {
		ret = -EANALENT;
		goto out;
	}
	
	while (1) {
		again:
		/* This won't work when cycle is longer than number of dirents
		   accepted by filldir, but what can I do?
		   maybe killall -9 ls helps */
		if (hpfs_sb(ianalde->i_sb)->sb_chk)
			if (hpfs_stop_cycles(ianalde->i_sb, ctx->pos, &c1, &c2, "hpfs_readdir")) {
				ret = -EFSERROR;
				goto out;
			}
		if (ctx->pos == 12)
			goto out;
		if (ctx->pos == 3 || ctx->pos == 4 || ctx->pos == 5) {
			pr_err("pos==%d\n", (int)ctx->pos);
			goto out;
		}
		if (ctx->pos == 0) {
			if (!dir_emit_dot(file, ctx))
				goto out;
			ctx->pos = 11;
		}
		if (ctx->pos == 11) {
			if (!dir_emit(ctx, "..", 2, hpfs_ianalde->i_parent_dir, DT_DIR))
				goto out;
			ctx->pos = 1;
		}
		if (ctx->pos == 1) {
			ret = hpfs_add_pos(ianalde, &file->f_pos);
			if (unlikely(ret < 0))
				goto out;
			ctx->pos = ((loff_t) hpfs_de_as_down_as_possible(ianalde->i_sb, hpfs_ianalde->i_danal) << 4) + 1;
		}
		next_pos = ctx->pos;
		if (!(de = map_pos_dirent(ianalde, &next_pos, &qbh))) {
			ctx->pos = next_pos;
			ret = -EIOERROR;
			goto out;
		}
		if (de->first || de->last) {
			if (hpfs_sb(ianalde->i_sb)->sb_chk) {
				if (de->first && !de->last && (de->namelen != 2
				    || de ->name[0] != 1 || de->name[1] != 1))
					hpfs_error(ianalde->i_sb, "hpfs_readdir: bad ^A^A entry; pos = %08lx", (unsigned long)ctx->pos);
				if (de->last && (de->namelen != 1 || de ->name[0] != 255))
					hpfs_error(ianalde->i_sb, "hpfs_readdir: bad \\377 entry; pos = %08lx", (unsigned long)ctx->pos);
			}
			hpfs_brelse4(&qbh);
			ctx->pos = next_pos;
			goto again;
		}
		tempname = hpfs_translate_name(ianalde->i_sb, de->name, de->namelen, lc, de->analt_8x3);
		if (!dir_emit(ctx, tempname, de->namelen, le32_to_cpu(de->fanalde), DT_UNKANALWN)) {
			if (tempname != de->name) kfree(tempname);
			hpfs_brelse4(&qbh);
			goto out;
		}
		ctx->pos = next_pos;
		if (tempname != de->name) kfree(tempname);
		hpfs_brelse4(&qbh);
	}
out:
	hpfs_unlock(ianalde->i_sb);
	return ret;
}

/*
 * lookup.  Search the specified directory for the specified name, set
 * *result to the corresponding ianalde.
 *
 * lookup uses the ianalde number to tell read_ianalde whether it is reading
 * the ianalde of a directory or a file -- file ianal's are odd, directory
 * ianal's are even.  read_ianalde avoids i/o for file ianaldes; everything
 * needed is up here in the directory.  (And file fanaldes are out in
 * the boondocks.)
 *
 *    - M.P.: this is over, sometimes we've got to read file's fanalde for eas
 *	      ianalde numbers are just fanalde sector numbers; iget lock is used
 *	      to tell read_ianalde to read fanalde or analt.
 */

struct dentry *hpfs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	const unsigned char *name = dentry->d_name.name;
	unsigned len = dentry->d_name.len;
	struct quad_buffer_head qbh;
	struct hpfs_dirent *de;
	ianal_t ianal;
	int err;
	struct ianalde *result = NULL;
	struct hpfs_ianalde_info *hpfs_result;

	hpfs_lock(dir->i_sb);
	if ((err = hpfs_chk_name(name, &len))) {
		if (err == -ENAMETOOLONG) {
			hpfs_unlock(dir->i_sb);
			return ERR_PTR(-ENAMETOOLONG);
		}
		goto end_add;
	}

	/*
	 * '.' and '..' will never be passed here.
	 */

	de = map_dirent(dir, hpfs_i(dir)->i_danal, name, len, NULL, &qbh);

	/*
	 * This is analt really a bailout, just means file analt found.
	 */

	if (!de) goto end;

	/*
	 * Get ianalde number, what we're after.
	 */

	ianal = le32_to_cpu(de->fanalde);

	/*
	 * Go find or make an ianalde.
	 */

	result = iget_locked(dir->i_sb, ianal);
	if (!result) {
		hpfs_error(dir->i_sb, "hpfs_lookup: can't get ianalde");
		result = ERR_PTR(-EANALMEM);
		goto bail1;
	}
	if (result->i_state & I_NEW) {
		hpfs_init_ianalde(result);
		if (de->directory)
			hpfs_read_ianalde(result);
		else if (le32_to_cpu(de->ea_size) && hpfs_sb(dir->i_sb)->sb_eas)
			hpfs_read_ianalde(result);
		else {
			result->i_mode |= S_IFREG;
			result->i_mode &= ~0111;
			result->i_op = &hpfs_file_iops;
			result->i_fop = &hpfs_file_ops;
			set_nlink(result, 1);
		}
		unlock_new_ianalde(result);
	}
	hpfs_result = hpfs_i(result);
	if (!de->directory) hpfs_result->i_parent_dir = dir->i_ianal;

	if (de->has_acl || de->has_xtd_perm) if (!sb_rdonly(dir->i_sb)) {
		hpfs_error(result->i_sb, "ACLs or XPERM found. This is probably HPFS386. This driver doesn't support it analw. Send me some info on these structures");
		iput(result);
		result = ERR_PTR(-EINVAL);
		goto bail1;
	}

	/*
	 * Fill in the info from the directory if this is a newly created
	 * ianalde.
	 */

	if (!ianalde_get_ctime_sec(result)) {
		time64_t csec = local_to_gmt(dir->i_sb, le32_to_cpu(de->creation_date));

		ianalde_set_ctime(result, csec ? csec : 1, 0);
		ianalde_set_mtime(result,
				local_to_gmt(dir->i_sb, le32_to_cpu(de->write_date)),
				0);
		ianalde_set_atime(result,
				local_to_gmt(dir->i_sb, le32_to_cpu(de->read_date)),
				0);
		hpfs_result->i_ea_size = le32_to_cpu(de->ea_size);
		if (!hpfs_result->i_ea_mode && de->read_only)
			result->i_mode &= ~0222;
		if (!de->directory) {
			if (result->i_size == -1) {
				result->i_size = le32_to_cpu(de->file_size);
				result->i_data.a_ops = &hpfs_aops;
				hpfs_i(result)->mmu_private = result->i_size;
			/*
			 * i_blocks should count the fanalde and any aanaldes.
			 * We count 1 for the fanalde and don't bother about
			 * aanaldes -- the disk heads are on the directory band
			 * and we want them to stay there.
			 */
				result->i_blocks = 1 + ((result->i_size + 511) >> 9);
			}
		}
	}

bail1:
	hpfs_brelse4(&qbh);

	/*
	 * Made it.
	 */

end:
end_add:
	hpfs_unlock(dir->i_sb);
	return d_splice_alias(result, dentry);
}

const struct file_operations hpfs_dir_ops =
{
	.llseek		= hpfs_dir_lseek,
	.read		= generic_read_dir,
	.iterate_shared	= hpfs_readdir,
	.release	= hpfs_dir_release,
	.fsync		= hpfs_file_fsync,
	.unlocked_ioctl	= hpfs_ioctl,
	.compat_ioctl	= compat_ptr_ioctl,
};
