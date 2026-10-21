// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024, Alibaba Cloud
 */
#include <linux/backing-file.h>
#include <linux/xxhash.h>
#include <linux/mount.h>
#include <linux/security.h>
#include "internal.h"
#include "xattr.h"

static struct vfsmount *erofs_ishare_mnt;

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
	static const struct file_operations empty_fops = {};
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);
	const struct address_space_operations *aops;
	struct erofs_inode *vi = EROFS_I(inode);
	struct erofs_inode_fingerprint fp;
	struct dentry *sd;
	struct inode *si;

	aops = erofs_get_aops(inode);
	if (IS_ERR(aops))
		return false;
	if (erofs_xattr_fill_inode_fingerprint(&fp, inode, sbi->domain_id))
		return false;

	si = iget5_locked(erofs_ishare_mnt->mnt_sb,
			  xxh32(fp.opaque, fp.size, 0),
			  erofs_ishare_iget5_eq, erofs_ishare_iget5_set, &fp);
	if (si && (inode_state_read_once(si) & I_NEW)) {
		si->i_fop = &empty_fops;
		si->i_mapping->a_ops = aops;
		si->i_mode = 0444 | S_IFREG;
		si->i_size = inode->i_size;
		mapping_set_large_folios(si->i_mapping);
		unlock_new_inode(si);
	} else {
		kfree(fp.opaque);
		if (!si || aops != si->i_mapping->a_ops) {
			iput(si);
			return false;
		}
		if (si->i_size != inode->i_size) {
			erofs_warn(inode->i_sb, "i_size mismatch (%lld != %lld) for the same fingerprint",
				   inode->i_size, si->i_size);
			iput(si);
			return false;
		}
	}
	sd = d_obtain_alias(si); /* disconnected denties for sharedinodes */
	if (IS_ERR(sd))
		return false;
	vi->sharedentry = sd;
	INIT_LIST_HEAD(&vi->ishare_list);
	spin_lock(&EROFS_I(si)->ishare_lock);
	list_add(&vi->ishare_list, &EROFS_I(si)->ishare_list);
	spin_unlock(&EROFS_I(si)->ishare_lock);
	return true;
}

void erofs_ishare_free_inode(struct inode *inode)
{
	struct erofs_inode *vi = EROFS_I(inode), *svi;

	if (!vi->sharedentry)
		return;
	svi = EROFS_I(d_inode(vi->sharedentry));
	spin_lock(&svi->ishare_lock);
	list_del(&vi->ishare_list);
	spin_unlock(&svi->ishare_lock);
	dput(vi->sharedentry);
	vi->sharedentry = NULL;
}

static int erofs_ishare_file_open(struct inode *inode, struct file *file)
{
	struct path sharedpath = {
		.mnt = erofs_ishare_mnt,
		.dentry = EROFS_I(inode)->sharedentry,
	};
	struct file *rf;

	if (file->f_flags & O_DIRECT)
		return -EINVAL;

	rf = backing_file_open(file, file->f_flags | O_NOATIME,
			       &sharedpath, current_cred());
	if (IS_ERR(rf))
		return PTR_ERR(rf);
	file->private_data = rf;
	return 0;
}

static int erofs_ishare_file_release(struct inode *inode, struct file *file)
{
	fput(file->private_data);
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
	int err;

	vma_set_file(vma, realfile);

	err = security_mmap_backing_file(vma, realfile, file);
	if (err)
		return err;

	return generic_file_readonly_mmap(file, vma);
}

static ssize_t erofs_ishare_splice_read(struct file *in, loff_t *ppos,
					struct pipe_inode_info *pipe,
					size_t len, unsigned int flags)
{
	return filemap_splice_read(in->private_data, ppos, pipe, len, flags);
}

static int erofs_ishare_fadvise(struct file *file, loff_t offset,
				loff_t len, int advice)
{
	return vfs_fadvise(file->private_data, offset, len, advice);
}

const struct file_operations erofs_ishare_fops = {
	.open		= erofs_ishare_file_open,
	.llseek		= erofs_file_llseek,
	.read_iter	= erofs_ishare_file_read_iter,
	.mmap		= erofs_ishare_mmap,
	.release	= erofs_ishare_file_release,
	.get_unmapped_area = thp_get_unmapped_area,
	.splice_read	= erofs_ishare_splice_read,
	.fadvise	= erofs_ishare_fadvise,
};

struct inode *erofs_real_inode(struct inode *inode, bool *need_iput)
{
	struct erofs_inode *vi, *vi_share;
	struct inode *realinode;

	*need_iput = false;
	if (inode->i_sb != erofs_ishare_mnt->mnt_sb)
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
	struct vfsmount *mnt;
	int ret;

	mnt = kern_mount(&erofs_anon_fs_type);
	if (IS_ERR(mnt))
		return PTR_ERR(mnt);
	/* generic_fadvise() doesn't work if s_bdi == &noop_backing_dev_info */
	ret = super_setup_bdi(mnt->mnt_sb);
	if (ret)
		kern_unmount(mnt);
	else
		erofs_ishare_mnt = mnt;
	return ret;
}

void erofs_exit_ishare(void)
{
	kern_unmount(erofs_ishare_mnt);
}
