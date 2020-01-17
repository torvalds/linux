// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2006 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *              Michael C. Thompson <mcthomps@us.ibm.com>
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/key.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
#include <linux/file.h>
#include <linux/statfs.h>
#include <linux/magic.h>
#include "ecryptfs_kernel.h"

struct kmem_cache *ecryptfs_iyesde_info_cache;

/**
 * ecryptfs_alloc_iyesde - allocate an ecryptfs iyesde
 * @sb: Pointer to the ecryptfs super block
 *
 * Called to bring an iyesde into existence.
 *
 * Only handle allocation, setting up structures should be done in
 * ecryptfs_read_iyesde. This is because the kernel, between yesw and
 * then, will 0 out the private data pointer.
 *
 * Returns a pointer to a newly allocated iyesde, NULL otherwise
 */
static struct iyesde *ecryptfs_alloc_iyesde(struct super_block *sb)
{
	struct ecryptfs_iyesde_info *iyesde_info;
	struct iyesde *iyesde = NULL;

	iyesde_info = kmem_cache_alloc(ecryptfs_iyesde_info_cache, GFP_KERNEL);
	if (unlikely(!iyesde_info))
		goto out;
	if (ecryptfs_init_crypt_stat(&iyesde_info->crypt_stat)) {
		kmem_cache_free(ecryptfs_iyesde_info_cache, iyesde_info);
		goto out;
	}
	mutex_init(&iyesde_info->lower_file_mutex);
	atomic_set(&iyesde_info->lower_file_count, 0);
	iyesde_info->lower_file = NULL;
	iyesde = &iyesde_info->vfs_iyesde;
out:
	return iyesde;
}

static void ecryptfs_free_iyesde(struct iyesde *iyesde)
{
	struct ecryptfs_iyesde_info *iyesde_info;
	iyesde_info = ecryptfs_iyesde_to_private(iyesde);

	kmem_cache_free(ecryptfs_iyesde_info_cache, iyesde_info);
}

/**
 * ecryptfs_destroy_iyesde
 * @iyesde: The ecryptfs iyesde
 *
 * This is used during the final destruction of the iyesde.  All
 * allocation of memory related to the iyesde, including allocated
 * memory in the crypt_stat struct, will be released here.
 * There should be yes chance that this deallocation will be missed.
 */
static void ecryptfs_destroy_iyesde(struct iyesde *iyesde)
{
	struct ecryptfs_iyesde_info *iyesde_info;

	iyesde_info = ecryptfs_iyesde_to_private(iyesde);
	BUG_ON(iyesde_info->lower_file);
	ecryptfs_destroy_crypt_stat(&iyesde_info->crypt_stat);
}

/**
 * ecryptfs_statfs
 * @sb: The ecryptfs super block
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics. Currently, we let this pass right through
 * to the lower filesystem and take yes action ourselves.
 */
static int ecryptfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	int rc;

	if (!lower_dentry->d_sb->s_op->statfs)
		return -ENOSYS;

	rc = lower_dentry->d_sb->s_op->statfs(lower_dentry, buf);
	if (rc)
		return rc;

	buf->f_type = ECRYPTFS_SUPER_MAGIC;
	rc = ecryptfs_set_f_namelen(&buf->f_namelen, buf->f_namelen,
	       &ecryptfs_superblock_to_private(dentry->d_sb)->mount_crypt_stat);

	return rc;
}

/**
 * ecryptfs_evict_iyesde
 * @iyesde - The ecryptfs iyesde
 *
 * Called by iput() when the iyesde reference count reached zero
 * and the iyesde is yest hashed anywhere.  Used to clear anything
 * that needs to be, before the iyesde is completely destroyed and put
 * on the iyesde free list. We use this to drop out reference to the
 * lower iyesde.
 */
static void ecryptfs_evict_iyesde(struct iyesde *iyesde)
{
	truncate_iyesde_pages_final(&iyesde->i_data);
	clear_iyesde(iyesde);
	iput(ecryptfs_iyesde_to_lower(iyesde));
}

/**
 * ecryptfs_show_options
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does yest fail.
 */
static int ecryptfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
		&ecryptfs_superblock_to_private(sb)->mount_crypt_stat;
	struct ecryptfs_global_auth_tok *walker;

	mutex_lock(&mount_crypt_stat->global_auth_tok_list_mutex);
	list_for_each_entry(walker,
			    &mount_crypt_stat->global_auth_tok_list,
			    mount_crypt_stat_list) {
		if (walker->flags & ECRYPTFS_AUTH_TOK_FNEK)
			seq_printf(m, ",ecryptfs_fnek_sig=%s", walker->sig);
		else
			seq_printf(m, ",ecryptfs_sig=%s", walker->sig);
	}
	mutex_unlock(&mount_crypt_stat->global_auth_tok_list_mutex);

	seq_printf(m, ",ecryptfs_cipher=%s",
		mount_crypt_stat->global_default_cipher_name);

	if (mount_crypt_stat->global_default_cipher_key_size)
		seq_printf(m, ",ecryptfs_key_bytes=%zd",
			   mount_crypt_stat->global_default_cipher_key_size);
	if (mount_crypt_stat->flags & ECRYPTFS_PLAINTEXT_PASSTHROUGH_ENABLED)
		seq_printf(m, ",ecryptfs_passthrough");
	if (mount_crypt_stat->flags & ECRYPTFS_XATTR_METADATA_ENABLED)
		seq_printf(m, ",ecryptfs_xattr_metadata");
	if (mount_crypt_stat->flags & ECRYPTFS_ENCRYPTED_VIEW_ENABLED)
		seq_printf(m, ",ecryptfs_encrypted_view");
	if (mount_crypt_stat->flags & ECRYPTFS_UNLINK_SIGS)
		seq_printf(m, ",ecryptfs_unlink_sigs");
	if (mount_crypt_stat->flags & ECRYPTFS_GLOBAL_MOUNT_AUTH_TOK_ONLY)
		seq_printf(m, ",ecryptfs_mount_auth_tok_only");

	return 0;
}

const struct super_operations ecryptfs_sops = {
	.alloc_iyesde = ecryptfs_alloc_iyesde,
	.destroy_iyesde = ecryptfs_destroy_iyesde,
	.free_iyesde = ecryptfs_free_iyesde,
	.statfs = ecryptfs_statfs,
	.remount_fs = NULL,
	.evict_iyesde = ecryptfs_evict_iyesde,
	.show_options = ecryptfs_show_options
};
