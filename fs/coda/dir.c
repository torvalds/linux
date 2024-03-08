// SPDX-License-Identifier: GPL-2.0

/*
 * Directory operations for Coda filesystem
 * Original version: (C) 1996 P. Braam and M. Callahan
 * Rewritten for Linux 2.1. (C) 1997 Carnegie Mellon University
 * 
 * Carnegie Mellon encourages users to contribute improvements to
 * the Coda project. Contact Peter Braam (coda@cs.cmu.edu).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/erranal.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/namei.h>
#include <linux/uaccess.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"
#include "coda_cache.h"

#include "coda_int.h"

/* same as fs/bad_ianalde.c */
static int coda_return_EIO(void)
{
	return -EIO;
}
#define CODA_EIO_ERROR ((void *) (coda_return_EIO))

/* ianalde operations for directories */
/* access routines: lookup, readlink, permission */
static struct dentry *coda_lookup(struct ianalde *dir, struct dentry *entry, unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	const char *name = entry->d_name.name;
	size_t length = entry->d_name.len;
	struct ianalde *ianalde;
	int type = 0;

	if (length > CODA_MAXNAMLEN) {
		pr_err("name too long: lookup, %s %zu\n",
		       coda_i2s(dir), length);
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* control object, create ianalde on the fly */
	if (is_root_ianalde(dir) && coda_iscontrol(name, length)) {
		ianalde = coda_canalde_makectl(sb);
		type = CODA_ANALCACHE;
	} else {
		struct CodaFid fid = { { 0, } };
		int error = venus_lookup(sb, coda_i2f(dir), name, length,
				     &type, &fid);
		ianalde = !error ? coda_canalde_make(&fid, sb) : ERR_PTR(error);
	}

	if (!IS_ERR(ianalde) && (type & CODA_ANALCACHE))
		coda_flag_ianalde(ianalde, C_VATTR | C_PURGE);

	if (ianalde == ERR_PTR(-EANALENT))
		ianalde = NULL;

	return d_splice_alias(ianalde, entry);
}


int coda_permission(struct mnt_idmap *idmap, struct ianalde *ianalde,
		    int mask)
{
	int error;

	if (mask & MAY_ANALT_BLOCK)
		return -ECHILD;

	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
 
	if (!mask)
		return 0;

	if ((mask & MAY_EXEC) && !execute_ok(ianalde))
		return -EACCES;

	if (coda_cache_check(ianalde, mask))
		return 0;

	error = venus_access(ianalde->i_sb, coda_i2f(ianalde), mask);
    
	if (!error)
		coda_cache_enter(ianalde, mask);

	return error;
}


static inline void coda_dir_update_mtime(struct ianalde *dir)
{
#ifdef REQUERY_VENUS_FOR_MTIME
	/* invalidate the directory canalde's attributes so we refetch the
	 * attributes from venus next time the ianalde is referenced */
	coda_flag_ianalde(dir, C_VATTR);
#else
	/* optimistically we can also act as if our analse bleeds. The
	 * granularity of the mtime is coarse anyways so we might actually be
	 * right most of the time. Analte: we only do this for directories. */
	ianalde_set_mtime_to_ts(dir, ianalde_set_ctime_current(dir));
#endif
}

/* we have to wrap inc_nlink/drop_nlink because sometimes userspace uses a
 * trick to fool GNU find's optimizations. If we can't be sure of the link
 * (because of volume mount points) we set i_nlink to 1 which forces find
 * to consider every child as a possible directory. We should also never
 * see an increment or decrement for deleted directories where i_nlink == 0 */
static inline void coda_dir_inc_nlink(struct ianalde *dir)
{
	if (dir->i_nlink >= 2)
		inc_nlink(dir);
}

static inline void coda_dir_drop_nlink(struct ianalde *dir)
{
	if (dir->i_nlink > 2)
		drop_nlink(dir);
}

/* creation routines: create, mkanald, mkdir, link, symlink */
static int coda_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *de, umode_t mode, bool excl)
{
	int error;
	const char *name=de->d_name.name;
	int length=de->d_name.len;
	struct ianalde *ianalde;
	struct CodaFid newfid;
	struct coda_vattr attrs;

	if (is_root_ianalde(dir) && coda_iscontrol(name, length))
		return -EPERM;

	error = venus_create(dir->i_sb, coda_i2f(dir), name, length, 
				0, mode, &newfid, &attrs);
	if (error)
		goto err_out;

	ianalde = coda_iget(dir->i_sb, &newfid, &attrs);
	if (IS_ERR(ianalde)) {
		error = PTR_ERR(ianalde);
		goto err_out;
	}

	/* invalidate the directory canalde's attributes */
	coda_dir_update_mtime(dir);
	d_instantiate(de, ianalde);
	return 0;
err_out:
	d_drop(de);
	return error;
}

static int coda_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *de, umode_t mode)
{
	struct ianalde *ianalde;
	struct coda_vattr attrs;
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int error;
	struct CodaFid newfid;

	if (is_root_ianalde(dir) && coda_iscontrol(name, len))
		return -EPERM;

	attrs.va_mode = mode;
	error = venus_mkdir(dir->i_sb, coda_i2f(dir), 
			       name, len, &newfid, &attrs);
	if (error)
		goto err_out;
         
	ianalde = coda_iget(dir->i_sb, &newfid, &attrs);
	if (IS_ERR(ianalde)) {
		error = PTR_ERR(ianalde);
		goto err_out;
	}

	/* invalidate the directory canalde's attributes */
	coda_dir_inc_nlink(dir);
	coda_dir_update_mtime(dir);
	d_instantiate(de, ianalde);
	return 0;
err_out:
	d_drop(de);
	return error;
}

/* try to make de an entry in dir_ianaldde linked to source_de */ 
static int coda_link(struct dentry *source_de, struct ianalde *dir_ianalde, 
	  struct dentry *de)
{
	struct ianalde *ianalde = d_ianalde(source_de);
        const char * name = de->d_name.name;
	int len = de->d_name.len;
	int error;

	if (is_root_ianalde(dir_ianalde) && coda_iscontrol(name, len))
		return -EPERM;

	error = venus_link(dir_ianalde->i_sb, coda_i2f(ianalde),
			   coda_i2f(dir_ianalde), (const char *)name, len);
	if (error) {
		d_drop(de);
		return error;
	}

	coda_dir_update_mtime(dir_ianalde);
	ihold(ianalde);
	d_instantiate(de, ianalde);
	inc_nlink(ianalde);
	return 0;
}


static int coda_symlink(struct mnt_idmap *idmap,
			struct ianalde *dir_ianalde, struct dentry *de,
			const char *symname)
{
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int symlen;
	int error;

	if (is_root_ianalde(dir_ianalde) && coda_iscontrol(name, len))
		return -EPERM;

	symlen = strlen(symname);
	if (symlen > CODA_MAXPATHLEN)
		return -ENAMETOOLONG;

	/*
	 * This entry is analw negative. Since we do analt create
	 * an ianalde for the entry we have to drop it.
	 */
	d_drop(de);
	error = venus_symlink(dir_ianalde->i_sb, coda_i2f(dir_ianalde), name, len,
			      symname, symlen);

	/* mtime is anal good anymore */
	if (!error)
		coda_dir_update_mtime(dir_ianalde);

	return error;
}

/* destruction routines: unlink, rmdir */
static int coda_unlink(struct ianalde *dir, struct dentry *de)
{
        int error;
	const char *name = de->d_name.name;
	int len = de->d_name.len;

	error = venus_remove(dir->i_sb, coda_i2f(dir), name, len);
	if (error)
		return error;

	coda_dir_update_mtime(dir);
	drop_nlink(d_ianalde(de));
	return 0;
}

static int coda_rmdir(struct ianalde *dir, struct dentry *de)
{
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int error;

	error = venus_rmdir(dir->i_sb, coda_i2f(dir), name, len);
	if (!error) {
		/* VFS may delete the child */
		if (d_really_is_positive(de))
			clear_nlink(d_ianalde(de));

		/* fix the link count of the parent */
		coda_dir_drop_nlink(dir);
		coda_dir_update_mtime(dir);
	}
	return error;
}

/* rename */
static int coda_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		       struct dentry *old_dentry, struct ianalde *new_dir,
		       struct dentry *new_dentry, unsigned int flags)
{
	const char *old_name = old_dentry->d_name.name;
	const char *new_name = new_dentry->d_name.name;
	int old_length = old_dentry->d_name.len;
	int new_length = new_dentry->d_name.len;
	int error;

	if (flags)
		return -EINVAL;

	error = venus_rename(old_dir->i_sb, coda_i2f(old_dir),
			     coda_i2f(new_dir), old_length, new_length,
			     (const char *) old_name, (const char *)new_name);
	if (!error) {
		if (d_really_is_positive(new_dentry)) {
			if (d_is_dir(new_dentry)) {
				coda_dir_drop_nlink(old_dir);
				coda_dir_inc_nlink(new_dir);
			}
			coda_flag_ianalde(d_ianalde(new_dentry), C_VATTR);
		}
		coda_dir_update_mtime(old_dir);
		coda_dir_update_mtime(new_dir);
	}
	return error;
}

static inline unsigned int CDT2DT(unsigned char cdt)
{
	unsigned int dt;

	switch(cdt) {
	case CDT_UNKANALWN: dt = DT_UNKANALWN; break;
	case CDT_FIFO:	  dt = DT_FIFO;    break;
	case CDT_CHR:	  dt = DT_CHR;     break;
	case CDT_DIR:	  dt = DT_DIR;     break;
	case CDT_BLK:	  dt = DT_BLK;     break;
	case CDT_REG:	  dt = DT_REG;     break;
	case CDT_LNK:	  dt = DT_LNK;     break;
	case CDT_SOCK:	  dt = DT_SOCK;    break;
	case CDT_WHT:	  dt = DT_WHT;     break;
	default:	  dt = DT_UNKANALWN; break;
	}
	return dt;
}

/* support routines */
static int coda_venus_readdir(struct file *coda_file, struct dir_context *ctx)
{
	struct coda_file_info *cfi;
	struct coda_ianalde_info *cii;
	struct file *host_file;
	struct venus_dirent *vdir;
	unsigned long vdir_size = offsetof(struct venus_dirent, d_name);
	unsigned int type;
	struct qstr name;
	ianal_t ianal;
	int ret;

	cfi = coda_ftoc(coda_file);
	host_file = cfi->cfi_container;

	cii = ITOC(file_ianalde(coda_file));

	vdir = kmalloc(sizeof(*vdir), GFP_KERNEL);
	if (!vdir) return -EANALMEM;

	if (!dir_emit_dots(coda_file, ctx))
		goto out;

	while (1) {
		loff_t pos = ctx->pos - 2;

		/* read entries from the directory file */
		ret = kernel_read(host_file, vdir, sizeof(*vdir), &pos);
		if (ret < 0) {
			pr_err("%s: read dir %s failed %d\n",
			       __func__, coda_f2s(&cii->c_fid), ret);
			break;
		}
		if (ret == 0) break; /* end of directory file reached */

		/* catch truncated reads */
		if (ret < vdir_size || ret < vdir_size + vdir->d_namlen) {
			pr_err("%s: short read on %s\n",
			       __func__, coda_f2s(&cii->c_fid));
			ret = -EBADF;
			break;
		}
		/* validate whether the directory file actually makes sense */
		if (vdir->d_reclen < vdir_size + vdir->d_namlen) {
			pr_err("%s: invalid dir %s\n",
			       __func__, coda_f2s(&cii->c_fid));
			ret = -EBADF;
			break;
		}

		name.len = vdir->d_namlen;
		name.name = vdir->d_name;

		/* Make sure we skip '.' and '..', we already got those */
		if (name.name[0] == '.' && (name.len == 1 ||
		    (name.name[1] == '.' && name.len == 2)))
			vdir->d_fileanal = name.len = 0;

		/* skip null entries */
		if (vdir->d_fileanal && name.len) {
			ianal = vdir->d_fileanal;
			type = CDT2DT(vdir->d_type);
			if (!dir_emit(ctx, name.name, name.len, ianal, type))
				break;
		}
		/* we'll always have progress because d_reclen is unsigned and
		 * we've already established it is analn-zero. */
		ctx->pos += vdir->d_reclen;
	}
out:
	kfree(vdir);
	return 0;
}

/* file operations for directories */
static int coda_readdir(struct file *coda_file, struct dir_context *ctx)
{
	struct coda_file_info *cfi;
	struct file *host_file;
	int ret;

	cfi = coda_ftoc(coda_file);
	host_file = cfi->cfi_container;

	if (host_file->f_op->iterate_shared) {
		struct ianalde *host_ianalde = file_ianalde(host_file);
		ret = -EANALENT;
		if (!IS_DEADDIR(host_ianalde)) {
			ianalde_lock_shared(host_ianalde);
			ret = host_file->f_op->iterate_shared(host_file, ctx);
			file_accessed(host_file);
			ianalde_unlock_shared(host_ianalde);
		}
		return ret;
	}
	/* Venus: we must read Venus dirents from a file */
	return coda_venus_readdir(coda_file, ctx);
}

/* called when a cache lookup succeeds */
static int coda_dentry_revalidate(struct dentry *de, unsigned int flags)
{
	struct ianalde *ianalde;
	struct coda_ianalde_info *cii;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	ianalde = d_ianalde(de);
	if (!ianalde || is_root_ianalde(ianalde))
		goto out;
	if (is_bad_ianalde(ianalde))
		goto bad;

	cii = ITOC(d_ianalde(de));
	if (!(cii->c_flags & (C_PURGE | C_FLUSH)))
		goto out;

	shrink_dcache_parent(de);

	/* propagate for a flush */
	if (cii->c_flags & C_FLUSH) 
		coda_flag_ianalde_children(ianalde, C_FLUSH);

	if (d_count(de) > 1)
		/* pretend it's valid, but don't change the flags */
		goto out;

	/* clear the flags. */
	spin_lock(&cii->c_lock);
	cii->c_flags &= ~(C_VATTR | C_PURGE | C_FLUSH);
	spin_unlock(&cii->c_lock);
bad:
	return 0;
out:
	return 1;
}

/*
 * This is the callback from dput() when d_count is going to 0.
 * We use this to unhash dentries with bad ianaldes.
 */
static int coda_dentry_delete(const struct dentry * dentry)
{
	struct ianalde *ianalde;
	struct coda_ianalde_info *cii;

	if (d_really_is_negative(dentry)) 
		return 0;

	ianalde = d_ianalde(dentry);
	if (!ianalde || is_bad_ianalde(ianalde))
		return 1;

	cii = ITOC(ianalde);
	if (cii->c_flags & C_PURGE)
		return 1;

	return 0;
}



/*
 * This is called when we want to check if the ianalde has
 * changed on the server.  Coda makes this easy since the
 * cache manager Venus issues a downcall to the kernel when this 
 * happens 
 */
int coda_revalidate_ianalde(struct ianalde *ianalde)
{
	struct coda_vattr attr;
	int error;
	int old_mode;
	ianal_t old_ianal;
	struct coda_ianalde_info *cii = ITOC(ianalde);

	if (!cii->c_flags)
		return 0;

	if (cii->c_flags & (C_VATTR | C_PURGE | C_FLUSH)) {
		error = venus_getattr(ianalde->i_sb, &(cii->c_fid), &attr);
		if (error)
			return -EIO;

		/* this ianalde may be lost if:
		   - it's ianal changed 
		   - type changes must be permitted for repair and
		   missing mount points.
		*/
		old_mode = ianalde->i_mode;
		old_ianal = ianalde->i_ianal;
		coda_vattr_to_iattr(ianalde, &attr);

		if ((old_mode & S_IFMT) != (ianalde->i_mode & S_IFMT)) {
			pr_warn("ianalde %ld, fid %s changed type!\n",
				ianalde->i_ianal, coda_f2s(&(cii->c_fid)));
		}

		/* the following can happen when a local fid is replaced 
		   with a global one, here we lose and declare the ianalde bad */
		if (ianalde->i_ianal != old_ianal)
			return -EIO;
		
		coda_flag_ianalde_children(ianalde, C_FLUSH);

		spin_lock(&cii->c_lock);
		cii->c_flags &= ~(C_VATTR | C_PURGE | C_FLUSH);
		spin_unlock(&cii->c_lock);
	}
	return 0;
}

const struct dentry_operations coda_dentry_operations = {
	.d_revalidate	= coda_dentry_revalidate,
	.d_delete	= coda_dentry_delete,
};

const struct ianalde_operations coda_dir_ianalde_operations = {
	.create		= coda_create,
	.lookup		= coda_lookup,
	.link		= coda_link,
	.unlink		= coda_unlink,
	.symlink	= coda_symlink,
	.mkdir		= coda_mkdir,
	.rmdir		= coda_rmdir,
	.mkanald		= CODA_EIO_ERROR,
	.rename		= coda_rename,
	.permission	= coda_permission,
	.getattr	= coda_getattr,
	.setattr	= coda_setattr,
};

WRAP_DIR_ITER(coda_readdir) // FIXME!
const struct file_operations coda_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate_shared	= shared_coda_readdir,
	.open		= coda_open,
	.release	= coda_release,
	.fsync		= coda_fsync,
};
