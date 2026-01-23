// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Alibaba Cloud
 */
#include <linux/xxhash.h>
#include <linux/mount.h>
#include "internal.h"
#include "xattr.h"

#include "../internal.h"

static struct vfsmount *erofs_ishare_mnt;

static inline bool erofs_is_ishare_inode(struct inode *inode)
{
	/* assumed FS_ONDEMAND is excluded with FS_PAGE_CACHE_SHARE feature */
	return inode->i_sb->s_type == &erofs_anon_fs_type;
}

static int erofs_ishare_iget5_eq(struct inode *inode, void *data)
{
	struct erofs_inode_fingerprint *fp1 = &EROFS_I(inode)->fingerprint;
	struct erofs_inode_fingerprint *fp2 = data;

	return fp1->size == fp2->size &&
		!memcmp(fp1->opaque, fp2->opaque, fp2->size);
}

static int erofs_ishare_iget5_set(struct inode *inode, void *data)
{
	struct erofs_inode *vi = EROFS_I(inode);

	vi->fingerprint = *(struct erofs_inode_fingerprint *)data;
	INIT_LIST_HEAD(&vi->ishare_list);
	spin_lock_init(&vi->ishare_lock);
	return 0;
}

bool erofs_ishare_fill_inode(struct inode *inode)
{
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_fingerprint fp;
	struct inode *sharedinode;
	unsigned long hash;

	if (erofs_xattr_fill_inode_fingerprint(&fp, inode, sbi->domain_id))
		return false;
	hash = xxh32(fp.opaque, fp.size, 0);
	sharedinode = iget5_locked(erofs_ishare_mnt->mnt_sb, hash,
				   erofs_ishare_iget5_eq, erofs_ishare_iget5_set,
				   &fp);
	if (!sharedinode) {
		kfree(fp.opaque);
		return false;
	}

	if (inode_state_read_once(sharedinode) & I_NEW) {
		if (erofs_inode_set_aops(sharedinode, inode, true)) {
			iget_failed(sharedinode);
			kfree(fp.opaque);
			return false;
		}
		sharedinode->i_size = vi->vfs_inode.i_size;
		unlock_new_inode(sharedinode);
	} else {
		kfree(fp.opaque);
		if (sharedinode->i_size != vi->vfs_inode.i_size) {
			_erofs_printk(inode->i_sb, KERN_WARNING
				"size(%lld:%lld) not matches for the same fingerprint\n",
				vi->vfs_inode.i_size, sharedinode->i_size);
			iput(sharedinode);
			return false;
		}
	}
	vi->sharedinode = sharedinode;
	INIT_LIST_HEAD(&vi->ishare_list);
	spin_lock(&EROFS_I(sharedinode)->ishare_lock);
	list_add(&vi->ishare_list, &EROFS_I(sharedinode)->ishare_list);
	spin_unlock(&EROFS_I(sharedinode)->ishare_lock);
	return true;
}

void erofs_ishare_free_inode(struct inode *inode)
{
	struct erofs_inode *vi = EROFS_I(inode);
	struct inode *sharedinode = vi->sharedinode;

	if (!sharedinode)
		return;
	spin_lock(&EROFS_I(sharedinode)->ishare_lock);
	list_del(&vi->ishare_list);
	spin_unlock(&EROFS_I(sharedinode)->ishare_lock);
	iput(sharedinode);
	vi->sharedinode = NULL;
}

static int erofs_ishare_file_open(struct inode *inode, struct file *file)
{
	struct inode *sharedinode = EROFS_I(inode)->sharedinode;
	struct file *realfile;

	if (file->f_flags & O_DIRECT)
		return -EINVAL;
	realfile = alloc_empty_backing_file(O_RDONLY|O_NOATIME, current_cred());
	if (IS_ERR(realfile))
		return PTR_ERR(realfile);
	ihold(sharedinode);
	realfile->f_op = &erofs_file_fops;
	realfile->f_inode = sharedinode;
	realfile->f_mapping = sharedinode->i_mapping;
	path_get(&file->f_path);
	backing_file_set_user_path(realfile, &file->f_path);

	file_ra_state_init(&realfile->f_ra, file->f_mapping);
	realfile->private_data = EROFS_I(inode);
	file->private_data = realfile;
	return 0;
}

static int erofs_ishare_file_release(struct inode *inode, struct file *file)
{
	struct file *realfile = file->private_data;

	iput(realfile->f_inode);
	fput(realfile);
	file->private_data = NULL;
	return 0;
}

static ssize_t erofs_ishare_file_read_iter(struct kiocb *iocb,
					   struct iov_iter *to)
{
	struct file *realfile = iocb->ki_filp->private_data;
	struct kiocb dedup_iocb;
	ssize_t nread;

	if (!iov_iter_count(to))
		return 0;
	kiocb_clone(&dedup_iocb, iocb, realfile);
	nread = filemap_read(&dedup_iocb, to, 0);
	iocb->ki_pos = dedup_iocb.ki_pos;
	return nread;
}

static int erofs_ishare_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct file *realfile = file->private_data;

	vma_set_file(vma, realfile);
	return generic_file_readonly_mmap(file, vma);
}

static int erofs_ishare_fadvise(struct file *file, loff_t offset,
				loff_t len, int advice)
{
	return vfs_fadvise(file->private_data, offset, len, advice);
}

const struct file_operations erofs_ishare_fops = {
	.open		= erofs_ishare_file_open,
	.llseek		= generic_file_llseek,
	.read_iter	= erofs_ishare_file_read_iter,
	.mmap		= erofs_ishare_mmap,
	.release	= erofs_ishare_file_release,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= filemap_splice_read,
	.fadvise	= erofs_ishare_fadvise,
};

struct inode *erofs_real_inode(struct inode *inode, bool *need_iput)
{
	struct erofs_inode *vi, *vi_share;
	struct inode *realinode;

	*need_iput = false;
	if (!erofs_is_ishare_inode(inode))
		return inode;

	vi_share = EROFS_I(inode);
	spin_lock(&vi_share->ishare_lock);
	/* fetch any one as real inode */
	DBG_BUGON(list_empty(&vi_share->ishare_list));
	list_for_each_entry(vi, &vi_share->ishare_list, ishare_list) {
		realinode = igrab(&vi->vfs_inode);
		if (realinode) {
			*need_iput = true;
			break;
		}
	}
	spin_unlock(&vi_share->ishare_lock);

	DBG_BUGON(!realinode);
	return realinode;
}

int __init erofs_init_ishare(void)
{
	erofs_ishare_mnt = kern_mount(&erofs_anon_fs_type);
	return PTR_ERR_OR_ZERO(erofs_ishare_mnt);
}

void erofs_exit_ishare(void)
{
	kern_unmount(erofs_ishare_mnt);
}
