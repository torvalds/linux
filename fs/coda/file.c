/*
 * File operations for Coda.
 * Original version: (C) 1996 Peter Braam 
 * Rewritten for Linux 2.1: (C) 1997 Carnegie Mellon University
 *
 * Carnegie Mellon encourages users of this code to contribute improvements
 * to the Coda project. Contact Peter Braam <coda@cs.cmu.edu>.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/smp_lock.h>
#include <linux/string.h>
#include <asm/uaccess.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_psdev.h>
#include <linux/coda_proc.h>

#include "coda_int.h"

/* if CODA_STORE fails with EOPNOTSUPP, venus clearly doesn't support
 * CODA_STORE/CODA_RELEASE and we fall back on using the CODA_CLOSE upcall */
static int use_coda_close;

static ssize_t
coda_file_read(struct file *coda_file, char __user *buf, size_t count, loff_t *ppos)
{
	struct coda_file_info *cfi;
	struct file *host_file;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);
	host_file = cfi->cfi_container;

	if (!host_file->f_op || !host_file->f_op->read)
		return -EINVAL;

	return host_file->f_op->read(host_file, buf, count, ppos);
}

static ssize_t
coda_file_sendfile(struct file *coda_file, loff_t *ppos, size_t count,
		   read_actor_t actor, void *target)
{
	struct coda_file_info *cfi;
	struct file *host_file;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);
	host_file = cfi->cfi_container;

	if (!host_file->f_op || !host_file->f_op->sendfile)
		return -EINVAL;

	return host_file->f_op->sendfile(host_file, ppos, count, actor, target);
}

static ssize_t
coda_file_write(struct file *coda_file, const char __user *buf, size_t count, loff_t *ppos)
{
	struct inode *host_inode, *coda_inode = coda_file->f_dentry->d_inode;
	struct coda_file_info *cfi;
	struct file *host_file;
	ssize_t ret;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);
	host_file = cfi->cfi_container;

	if (!host_file->f_op || !host_file->f_op->write)
		return -EINVAL;

	host_inode = host_file->f_dentry->d_inode;
	mutex_lock(&coda_inode->i_mutex);

	ret = host_file->f_op->write(host_file, buf, count, ppos);

	coda_inode->i_size = host_inode->i_size;
	coda_inode->i_blocks = (coda_inode->i_size + 511) >> 9;
	coda_inode->i_mtime = coda_inode->i_ctime = CURRENT_TIME_SEC;
	mutex_unlock(&coda_inode->i_mutex);

	return ret;
}

static int
coda_file_mmap(struct file *coda_file, struct vm_area_struct *vma)
{
	struct coda_file_info *cfi;
	struct coda_inode_info *cii;
	struct file *host_file;
	struct inode *coda_inode, *host_inode;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);
	host_file = cfi->cfi_container;

	if (!host_file->f_op || !host_file->f_op->mmap)
		return -ENODEV;

	coda_inode = coda_file->f_dentry->d_inode;
	host_inode = host_file->f_dentry->d_inode;
	coda_file->f_mapping = host_file->f_mapping;
	if (coda_inode->i_mapping == &coda_inode->i_data)
		coda_inode->i_mapping = host_inode->i_mapping;

	/* only allow additional mmaps as long as userspace isn't changing
	 * the container file on us! */
	else if (coda_inode->i_mapping != host_inode->i_mapping)
		return -EBUSY;

	/* keep track of how often the coda_inode/host_file has been mmapped */
	cii = ITOC(coda_inode);
	cii->c_mapcount++;
	cfi->cfi_mapcount++;

	return host_file->f_op->mmap(host_file, vma);
}

int coda_open(struct inode *coda_inode, struct file *coda_file)
{
	struct file *host_file = NULL;
	int error;
	unsigned short flags = coda_file->f_flags & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_file_info *cfi;

	coda_vfs_stat.open++;

	cfi = kmalloc(sizeof(struct coda_file_info), GFP_KERNEL);
	if (!cfi) {
		unlock_kernel();
		return -ENOMEM;
	}

	lock_kernel();

	error = venus_open(coda_inode->i_sb, coda_i2f(coda_inode), coda_flags,
			   &host_file); 
	if (error || !host_file) {
		kfree(cfi);
		unlock_kernel();
		return error;
	}

	host_file->f_flags |= coda_file->f_flags & (O_APPEND | O_SYNC);

	cfi->cfi_magic = CODA_MAGIC;
	cfi->cfi_mapcount = 0;
	cfi->cfi_container = host_file;

	BUG_ON(coda_file->private_data != NULL);
	coda_file->private_data = cfi;

	unlock_kernel();
	return 0;
}

int coda_flush(struct file *coda_file)
{
	unsigned short flags = coda_file->f_flags & ~O_EXCL;
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_file_info *cfi;
	struct inode *coda_inode;
	int err = 0, fcnt;

	lock_kernel();

	coda_vfs_stat.flush++;

	/* last close semantics */
	fcnt = file_count(coda_file);
	if (fcnt > 1)
		goto out;

	/* No need to make an upcall when we have not made any modifications
	 * to the file */
	if ((coda_file->f_flags & O_ACCMODE) == O_RDONLY)
		goto out;

	if (use_coda_close)
		goto out;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);

	coda_inode = coda_file->f_dentry->d_inode;

	err = venus_store(coda_inode->i_sb, coda_i2f(coda_inode), coda_flags,
			  coda_file->f_uid);

	if (err == -EOPNOTSUPP) {
		use_coda_close = 1;
		err = 0;
	}

out:
	unlock_kernel();
	return err;
}

int coda_release(struct inode *coda_inode, struct file *coda_file)
{
	unsigned short flags = (coda_file->f_flags) & (~O_EXCL);
	unsigned short coda_flags = coda_flags_to_cflags(flags);
	struct coda_file_info *cfi;
	struct coda_inode_info *cii;
	struct inode *host_inode;
	int err = 0;

	lock_kernel();
	coda_vfs_stat.release++;
 
	if (!use_coda_close) {
		err = venus_release(coda_inode->i_sb, coda_i2f(coda_inode),
				    coda_flags);
		if (err == -EOPNOTSUPP) {
			use_coda_close = 1;
			err = 0;
		}
	}

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);

	if (use_coda_close)
		err = venus_close(coda_inode->i_sb, coda_i2f(coda_inode),
				  coda_flags, coda_file->f_uid);

	host_inode = cfi->cfi_container->f_dentry->d_inode;
	cii = ITOC(coda_inode);

	/* did we mmap this file? */
	if (coda_inode->i_mapping == &host_inode->i_data) {
		cii->c_mapcount -= cfi->cfi_mapcount;
		if (!cii->c_mapcount)
			coda_inode->i_mapping = &coda_inode->i_data;
	}

	fput(cfi->cfi_container);
	kfree(coda_file->private_data);
	coda_file->private_data = NULL;

	unlock_kernel();
	return err;
}

int coda_fsync(struct file *coda_file, struct dentry *coda_dentry, int datasync)
{
	struct file *host_file;
	struct dentry *host_dentry;
	struct inode *host_inode, *coda_inode = coda_dentry->d_inode;
	struct coda_file_info *cfi;
	int err = 0;

	if (!(S_ISREG(coda_inode->i_mode) || S_ISDIR(coda_inode->i_mode) ||
	      S_ISLNK(coda_inode->i_mode)))
		return -EINVAL;

	cfi = CODA_FTOC(coda_file);
	BUG_ON(!cfi || cfi->cfi_magic != CODA_MAGIC);
	host_file = cfi->cfi_container;

	coda_vfs_stat.fsync++;

	if (host_file->f_op && host_file->f_op->fsync) {
		host_dentry = host_file->f_dentry;
		host_inode = host_dentry->d_inode;
		mutex_lock(&host_inode->i_mutex);
		err = host_file->f_op->fsync(host_file, host_dentry, datasync);
		mutex_unlock(&host_inode->i_mutex);
	}

	if ( !err && !datasync ) {
		lock_kernel();
		err = venus_fsync(coda_inode->i_sb, coda_i2f(coda_inode));
		unlock_kernel();
	}

	return err;
}

struct file_operations coda_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= coda_file_read,
	.write		= coda_file_write,
	.mmap		= coda_file_mmap,
	.open		= coda_open,
	.flush		= coda_flush,
	.release	= coda_release,
	.fsync		= coda_fsync,
	.sendfile	= coda_file_sendfile,
};

