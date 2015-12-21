/*
 *  linux/fs/ext4/symlink.c
 *
 * Only fast symlinks left here - the rest is done by generic code. AV, 1999
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4 symlink handling code
 */

#include <linux/fs.h>
#include <linux/namei.h>
#include "ext4.h"
#include "xattr.h"

#ifdef CONFIG_EXT4_FS_ENCRYPTION
static const char *ext4_encrypted_follow_link(struct dentry *dentry, void **cookie)
{
	struct page *cpage = NULL;
	char *caddr, *paddr = NULL;
	struct ext4_str cstr, pstr;
	struct inode *inode = d_inode(dentry);
	struct ext4_encrypted_symlink_data *sd;
	loff_t size = min_t(loff_t, i_size_read(inode), PAGE_SIZE - 1);
	int res;
	u32 plen, max_size = inode->i_sb->s_blocksize;

	res = ext4_get_encryption_info(inode);
	if (res)
		return ERR_PTR(res);

	if (ext4_inode_is_fast_symlink(inode)) {
		caddr = (char *) EXT4_I(inode)->i_data;
		max_size = sizeof(EXT4_I(inode)->i_data);
	} else {
		cpage = read_mapping_page(inode->i_mapping, 0, NULL);
		if (IS_ERR(cpage))
			return ERR_CAST(cpage);
		caddr = kmap(cpage);
		caddr[size] = 0;
	}

	/* Symlink is encrypted */
	sd = (struct ext4_encrypted_symlink_data *)caddr;
	cstr.name = sd->encrypted_path;
	cstr.len  = le32_to_cpu(sd->len);
	if ((cstr.len +
	     sizeof(struct ext4_encrypted_symlink_data) - 1) >
	    max_size) {
		/* Symlink data on the disk is corrupted */
		res = -EFSCORRUPTED;
		goto errout;
	}
	plen = (cstr.len < EXT4_FNAME_CRYPTO_DIGEST_SIZE*2) ?
		EXT4_FNAME_CRYPTO_DIGEST_SIZE*2 : cstr.len;
	paddr = kmalloc(plen + 1, GFP_NOFS);
	if (!paddr) {
		res = -ENOMEM;
		goto errout;
	}
	pstr.name = paddr;
	pstr.len = plen;
	res = _ext4_fname_disk_to_usr(inode, NULL, &cstr, &pstr);
	if (res < 0)
		goto errout;
	/* Null-terminate the name */
	if (res <= plen)
		paddr[res] = '\0';
	if (cpage) {
		kunmap(cpage);
		page_cache_release(cpage);
	}
	return *cookie = paddr;
errout:
	if (cpage) {
		kunmap(cpage);
		page_cache_release(cpage);
	}
	kfree(paddr);
	return ERR_PTR(res);
}

const struct inode_operations ext4_encrypted_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link    = ext4_encrypted_follow_link,
	.put_link       = kfree_put_link,
	.setattr	= ext4_setattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext4_listxattr,
	.removexattr	= generic_removexattr,
};
#endif

const struct inode_operations ext4_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link	= page_follow_link_light,
	.put_link	= page_put_link,
	.setattr	= ext4_setattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext4_listxattr,
	.removexattr	= generic_removexattr,
};

const struct inode_operations ext4_fast_symlink_inode_operations = {
	.readlink	= generic_readlink,
	.follow_link    = simple_follow_link,
	.setattr	= ext4_setattr,
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext4_listxattr,
	.removexattr	= generic_removexattr,
};
