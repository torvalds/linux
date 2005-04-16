/*
 * inode.c - basic inode and dentry operations.
 *
 * sysfs is Copyright (c) 2001-3 Patrick Mochel
 *
 * Please see Documentation/filesystems/sysfs.txt for more information.
 */

#undef DEBUG 

#include <linux/pagemap.h>
#include <linux/namei.h>
#include <linux/backing-dev.h>
#include "sysfs.h"

extern struct super_block * sysfs_sb;

static struct address_space_operations sysfs_aops = {
	.readpage	= simple_readpage,
	.prepare_write	= simple_prepare_write,
	.commit_write	= simple_commit_write
};

static struct backing_dev_info sysfs_backing_dev_info = {
	.ra_pages	= 0,	/* No readahead */
	.capabilities	= BDI_CAP_NO_ACCT_DIRTY | BDI_CAP_NO_WRITEBACK,
};

struct inode * sysfs_new_inode(mode_t mode)
{
	struct inode * inode = new_inode(sysfs_sb);
	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		inode->i_mapping->a_ops = &sysfs_aops;
		inode->i_mapping->backing_dev_info = &sysfs_backing_dev_info;
	}
	return inode;
}

int sysfs_create(struct dentry * dentry, int mode, int (*init)(struct inode *))
{
	int error = 0;
	struct inode * inode = NULL;
	if (dentry) {
		if (!dentry->d_inode) {
			if ((inode = sysfs_new_inode(mode))) {
				if (dentry->d_parent && dentry->d_parent->d_inode) {
					struct inode *p_inode = dentry->d_parent->d_inode;
					p_inode->i_mtime = p_inode->i_ctime = CURRENT_TIME;
				}
				goto Proceed;
			}
			else 
				error = -ENOMEM;
		} else
			error = -EEXIST;
	} else 
		error = -ENOENT;
	goto Done;

 Proceed:
	if (init)
		error = init(inode);
	if (!error) {
		d_instantiate(dentry, inode);
		if (S_ISDIR(mode))
			dget(dentry);  /* pin only directory dentry in core */
	} else
		iput(inode);
 Done:
	return error;
}

struct dentry * sysfs_get_dentry(struct dentry * parent, const char * name)
{
	struct qstr qstr;

	qstr.name = name;
	qstr.len = strlen(name);
	qstr.hash = full_name_hash(name,qstr.len);
	return lookup_hash(&qstr,parent);
}

/*
 * Get the name for corresponding element represented by the given sysfs_dirent
 */
const unsigned char * sysfs_get_name(struct sysfs_dirent *sd)
{
	struct attribute * attr;
	struct bin_attribute * bin_attr;
	struct sysfs_symlink  * sl;

	if (!sd || !sd->s_element)
		BUG();

	switch (sd->s_type) {
		case SYSFS_DIR:
			/* Always have a dentry so use that */
			return sd->s_dentry->d_name.name;

		case SYSFS_KOBJ_ATTR:
			attr = sd->s_element;
			return attr->name;

		case SYSFS_KOBJ_BIN_ATTR:
			bin_attr = sd->s_element;
			return bin_attr->attr.name;

		case SYSFS_KOBJ_LINK:
			sl = sd->s_element;
			return sl->link_name;
	}
	return NULL;
}


/*
 * Unhashes the dentry corresponding to given sysfs_dirent
 * Called with parent inode's i_sem held.
 */
void sysfs_drop_dentry(struct sysfs_dirent * sd, struct dentry * parent)
{
	struct dentry * dentry = sd->s_dentry;

	if (dentry) {
		spin_lock(&dcache_lock);
		spin_lock(&dentry->d_lock);
		if (!(d_unhashed(dentry) && dentry->d_inode)) {
			dget_locked(dentry);
			__d_drop(dentry);
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dcache_lock);
			simple_unlink(parent->d_inode, dentry);
		} else {
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dcache_lock);
		}
	}
}

void sysfs_hash_and_remove(struct dentry * dir, const char * name)
{
	struct sysfs_dirent * sd;
	struct sysfs_dirent * parent_sd = dir->d_fsdata;

	down(&dir->d_inode->i_sem);
	list_for_each_entry(sd, &parent_sd->s_children, s_sibling) {
		if (!sd->s_element)
			continue;
		if (!strcmp(sysfs_get_name(sd), name)) {
			list_del_init(&sd->s_sibling);
			sysfs_drop_dentry(sd, dir);
			sysfs_put(sd);
			break;
		}
	}
	up(&dir->d_inode->i_sem);
}


