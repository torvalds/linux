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
#include <linux/erryes.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/namei.h>
#include <linux/uaccess.h>

#include <linux/coda.h>
#include "coda_psdev.h"
#include "coda_linux.h"
#include "coda_cache.h"

#include "coda_int.h"

/* same as fs/bad_iyesde.c */
static int coda_return_EIO(void)
{
	return -EIO;
}
#define CODA_EIO_ERROR ((void *) (coda_return_EIO))

/* iyesde operations for directories */
/* access routines: lookup, readlink, permission */
static struct dentry *coda_lookup(struct iyesde *dir, struct dentry *entry, unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	const char *name = entry->d_name.name;
	size_t length = entry->d_name.len;
	struct iyesde *iyesde;
	int type = 0;

	if (length > CODA_MAXNAMLEN) {
		pr_err("name too long: lookup, %s %zu\n",
		       coda_i2s(dir), length);
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* control object, create iyesde on the fly */
	if (is_root_iyesde(dir) && coda_iscontrol(name, length)) {
		iyesde = coda_cyesde_makectl(sb);
		type = CODA_NOCACHE;
	} else {
		struct CodaFid fid = { { 0, } };
		int error = venus_lookup(sb, coda_i2f(dir), name, length,
				     &type, &fid);
		iyesde = !error ? coda_cyesde_make(&fid, sb) : ERR_PTR(error);
	}

	if (!IS_ERR(iyesde) && (type & CODA_NOCACHE))
		coda_flag_iyesde(iyesde, C_VATTR | C_PURGE);

	if (iyesde == ERR_PTR(-ENOENT))
		iyesde = NULL;

	return d_splice_alias(iyesde, entry);
}


int coda_permission(struct iyesde *iyesde, int mask)
{
	int error;

	if (mask & MAY_NOT_BLOCK)
		return -ECHILD;

	mask &= MAY_READ | MAY_WRITE | MAY_EXEC;
 
	if (!mask)
		return 0;

	if ((mask & MAY_EXEC) && !execute_ok(iyesde))
		return -EACCES;

	if (coda_cache_check(iyesde, mask))
		return 0;

	error = venus_access(iyesde->i_sb, coda_i2f(iyesde), mask);
    
	if (!error)
		coda_cache_enter(iyesde, mask);

	return error;
}


static inline void coda_dir_update_mtime(struct iyesde *dir)
{
#ifdef REQUERY_VENUS_FOR_MTIME
	/* invalidate the directory cyesde's attributes so we refetch the
	 * attributes from venus next time the iyesde is referenced */
	coda_flag_iyesde(dir, C_VATTR);
#else
	/* optimistically we can also act as if our yesse bleeds. The
	 * granularity of the mtime is coarse anyways so we might actually be
	 * right most of the time. Note: we only do this for directories. */
	dir->i_mtime = dir->i_ctime = current_time(dir);
#endif
}

/* we have to wrap inc_nlink/drop_nlink because sometimes userspace uses a
 * trick to fool GNU find's optimizations. If we can't be sure of the link
 * (because of volume mount points) we set i_nlink to 1 which forces find
 * to consider every child as a possible directory. We should also never
 * see an increment or decrement for deleted directories where i_nlink == 0 */
static inline void coda_dir_inc_nlink(struct iyesde *dir)
{
	if (dir->i_nlink >= 2)
		inc_nlink(dir);
}

static inline void coda_dir_drop_nlink(struct iyesde *dir)
{
	if (dir->i_nlink > 2)
		drop_nlink(dir);
}

/* creation routines: create, mkyesd, mkdir, link, symlink */
static int coda_create(struct iyesde *dir, struct dentry *de, umode_t mode, bool excl)
{
	int error;
	const char *name=de->d_name.name;
	int length=de->d_name.len;
	struct iyesde *iyesde;
	struct CodaFid newfid;
	struct coda_vattr attrs;

	if (is_root_iyesde(dir) && coda_iscontrol(name, length))
		return -EPERM;

	error = venus_create(dir->i_sb, coda_i2f(dir), name, length, 
				0, mode, &newfid, &attrs);
	if (error)
		goto err_out;

	iyesde = coda_iget(dir->i_sb, &newfid, &attrs);
	if (IS_ERR(iyesde)) {
		error = PTR_ERR(iyesde);
		goto err_out;
	}

	/* invalidate the directory cyesde's attributes */
	coda_dir_update_mtime(dir);
	d_instantiate(de, iyesde);
	return 0;
err_out:
	d_drop(de);
	return error;
}

static int coda_mkdir(struct iyesde *dir, struct dentry *de, umode_t mode)
{
	struct iyesde *iyesde;
	struct coda_vattr attrs;
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int error;
	struct CodaFid newfid;

	if (is_root_iyesde(dir) && coda_iscontrol(name, len))
		return -EPERM;

	attrs.va_mode = mode;
	error = venus_mkdir(dir->i_sb, coda_i2f(dir), 
			       name, len, &newfid, &attrs);
	if (error)
		goto err_out;
         
	iyesde = coda_iget(dir->i_sb, &newfid, &attrs);
	if (IS_ERR(iyesde)) {
		error = PTR_ERR(iyesde);
		goto err_out;
	}

	/* invalidate the directory cyesde's attributes */
	coda_dir_inc_nlink(dir);
	coda_dir_update_mtime(dir);
	d_instantiate(de, iyesde);
	return 0;
err_out:
	d_drop(de);
	return error;
}

/* try to make de an entry in dir_iyesdde linked to source_de */ 
static int coda_link(struct dentry *source_de, struct iyesde *dir_iyesde, 
	  struct dentry *de)
{
	struct iyesde *iyesde = d_iyesde(source_de);
        const char * name = de->d_name.name;
	int len = de->d_name.len;
	int error;

	if (is_root_iyesde(dir_iyesde) && coda_iscontrol(name, len))
		return -EPERM;

	error = venus_link(dir_iyesde->i_sb, coda_i2f(iyesde),
			   coda_i2f(dir_iyesde), (const char *)name, len);
	if (error) {
		d_drop(de);
		return error;
	}

	coda_dir_update_mtime(dir_iyesde);
	ihold(iyesde);
	d_instantiate(de, iyesde);
	inc_nlink(iyesde);
	return 0;
}


static int coda_symlink(struct iyesde *dir_iyesde, struct dentry *de,
			const char *symname)
{
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int symlen;
	int error;

	if (is_root_iyesde(dir_iyesde) && coda_iscontrol(name, len))
		return -EPERM;

	symlen = strlen(symname);
	if (symlen > CODA_MAXPATHLEN)
		return -ENAMETOOLONG;

	/*
	 * This entry is yesw negative. Since we do yest create
	 * an iyesde for the entry we have to drop it.
	 */
	d_drop(de);
	error = venus_symlink(dir_iyesde->i_sb, coda_i2f(dir_iyesde), name, len,
			      symname, symlen);

	/* mtime is yes good anymore */
	if (!error)
		coda_dir_update_mtime(dir_iyesde);

	return error;
}

/* destruction routines: unlink, rmdir */
static int coda_unlink(struct iyesde *dir, struct dentry *de)
{
        int error;
	const char *name = de->d_name.name;
	int len = de->d_name.len;

	error = venus_remove(dir->i_sb, coda_i2f(dir), name, len);
	if (error)
		return error;

	coda_dir_update_mtime(dir);
	drop_nlink(d_iyesde(de));
	return 0;
}

static int coda_rmdir(struct iyesde *dir, struct dentry *de)
{
	const char *name = de->d_name.name;
	int len = de->d_name.len;
	int error;

	error = venus_rmdir(dir->i_sb, coda_i2f(dir), name, len);
	if (!error) {
		/* VFS may delete the child */
		if (d_really_is_positive(de))
			clear_nlink(d_iyesde(de));

		/* fix the link count of the parent */
		coda_dir_drop_nlink(dir);
		coda_dir_update_mtime(dir);
	}
	return error;
}

/* rename */
static int coda_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		       struct iyesde *new_dir, struct dentry *new_dentry,
		       unsigned int flags)
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
			coda_dir_update_mtime(old_dir);
			coda_dir_update_mtime(new_dir);
			coda_flag_iyesde(d_iyesde(new_dentry), C_VATTR);
		} else {
			coda_flag_iyesde(old_dir, C_VATTR);
			coda_flag_iyesde(new_dir, C_VATTR);
		}
	}
	return error;
}

static inline unsigned int CDT2DT(unsigned char cdt)
{
	unsigned int dt;

	switch(cdt) {
	case CDT_UNKNOWN: dt = DT_UNKNOWN; break;
	case CDT_FIFO:	  dt = DT_FIFO;    break;
	case CDT_CHR:	  dt = DT_CHR;     break;
	case CDT_DIR:	  dt = DT_DIR;     break;
	case CDT_BLK:	  dt = DT_BLK;     break;
	case CDT_REG:	  dt = DT_REG;     break;
	case CDT_LNK:	  dt = DT_LNK;     break;
	case CDT_SOCK:	  dt = DT_SOCK;    break;
	case CDT_WHT:	  dt = DT_WHT;     break;
	default:	  dt = DT_UNKNOWN; break;
	}
	return dt;
}

/* support routines */
static int coda_venus_readdir(struct file *coda_file, struct dir_context *ctx)
{
	struct coda_file_info *cfi;
	struct coda_iyesde_info *cii;
	struct file *host_file;
	struct venus_dirent *vdir;
	unsigned long vdir_size = offsetof(struct venus_dirent, d_name);
	unsigned int type;
	struct qstr name;
	iyes_t iyes;
	int ret;

	cfi = coda_ftoc(coda_file);
	host_file = cfi->cfi_container;

	cii = ITOC(file_iyesde(coda_file));

	vdir = kmalloc(sizeof(*vdir), GFP_KERNEL);
	if (!vdir) return -ENOMEM;

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
			vdir->d_fileyes = name.len = 0;

		/* skip null entries */
		if (vdir->d_fileyes && name.len) {
			iyes = vdir->d_fileyes;
			type = CDT2DT(vdir->d_type);
			if (!dir_emit(ctx, name.name, name.len, iyes, type))
				break;
		}
		/* we'll always have progress because d_reclen is unsigned and
		 * we've already established it is yesn-zero. */
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

	if (host_file->f_op->iterate || host_file->f_op->iterate_shared) {
		struct iyesde *host_iyesde = file_iyesde(host_file);
		ret = -ENOENT;
		if (!IS_DEADDIR(host_iyesde)) {
			if (host_file->f_op->iterate_shared) {
				iyesde_lock_shared(host_iyesde);
				ret = host_file->f_op->iterate_shared(host_file, ctx);
				file_accessed(host_file);
				iyesde_unlock_shared(host_iyesde);
			} else {
				iyesde_lock(host_iyesde);
				ret = host_file->f_op->iterate(host_file, ctx);
				file_accessed(host_file);
				iyesde_unlock(host_iyesde);
			}
		}
		return ret;
	}
	/* Venus: we must read Venus dirents from a file */
	return coda_venus_readdir(coda_file, ctx);
}

/* called when a cache lookup succeeds */
static int coda_dentry_revalidate(struct dentry *de, unsigned int flags)
{
	struct iyesde *iyesde;
	struct coda_iyesde_info *cii;

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	iyesde = d_iyesde(de);
	if (!iyesde || is_root_iyesde(iyesde))
		goto out;
	if (is_bad_iyesde(iyesde))
		goto bad;

	cii = ITOC(d_iyesde(de));
	if (!(cii->c_flags & (C_PURGE | C_FLUSH)))
		goto out;

	shrink_dcache_parent(de);

	/* propagate for a flush */
	if (cii->c_flags & C_FLUSH) 
		coda_flag_iyesde_children(iyesde, C_FLUSH);

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
 * We use this to unhash dentries with bad iyesdes.
 */
static int coda_dentry_delete(const struct dentry * dentry)
{
	int flags;

	if (d_really_is_negative(dentry)) 
		return 0;

	flags = (ITOC(d_iyesde(dentry))->c_flags) & C_PURGE;
	if (is_bad_iyesde(d_iyesde(dentry)) || flags) {
		return 1;
	}
	return 0;
}



/*
 * This is called when we want to check if the iyesde has
 * changed on the server.  Coda makes this easy since the
 * cache manager Venus issues a downcall to the kernel when this 
 * happens 
 */
int coda_revalidate_iyesde(struct iyesde *iyesde)
{
	struct coda_vattr attr;
	int error;
	int old_mode;
	iyes_t old_iyes;
	struct coda_iyesde_info *cii = ITOC(iyesde);

	if (!cii->c_flags)
		return 0;

	if (cii->c_flags & (C_VATTR | C_PURGE | C_FLUSH)) {
		error = venus_getattr(iyesde->i_sb, &(cii->c_fid), &attr);
		if (error)
			return -EIO;

		/* this iyesde may be lost if:
		   - it's iyes changed 
		   - type changes must be permitted for repair and
		   missing mount points.
		*/
		old_mode = iyesde->i_mode;
		old_iyes = iyesde->i_iyes;
		coda_vattr_to_iattr(iyesde, &attr);

		if ((old_mode & S_IFMT) != (iyesde->i_mode & S_IFMT)) {
			pr_warn("iyesde %ld, fid %s changed type!\n",
				iyesde->i_iyes, coda_f2s(&(cii->c_fid)));
		}

		/* the following can happen when a local fid is replaced 
		   with a global one, here we lose and declare the iyesde bad */
		if (iyesde->i_iyes != old_iyes)
			return -EIO;
		
		coda_flag_iyesde_children(iyesde, C_FLUSH);

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

const struct iyesde_operations coda_dir_iyesde_operations = {
	.create		= coda_create,
	.lookup		= coda_lookup,
	.link		= coda_link,
	.unlink		= coda_unlink,
	.symlink	= coda_symlink,
	.mkdir		= coda_mkdir,
	.rmdir		= coda_rmdir,
	.mkyesd		= CODA_EIO_ERROR,
	.rename		= coda_rename,
	.permission	= coda_permission,
	.getattr	= coda_getattr,
	.setattr	= coda_setattr,
};

const struct file_operations coda_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= coda_readdir,
	.open		= coda_open,
	.release	= coda_release,
	.fsync		= coda_fsync,
};
