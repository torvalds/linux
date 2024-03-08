// SPDX-License-Identifier: GPL-2.0-or-later
/*
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

struct kmem_cache *ecryptfs_ianalde_info_cache;

/**
 * ecryptfs_alloc_ianalde - allocate an ecryptfs ianalde
 * @sb: Pointer to the ecryptfs super block
 *
 * Called to bring an ianalde into existence.
 *
 * Only handle allocation, setting up structures should be done in
 * ecryptfs_read_ianalde. This is because the kernel, between analw and
 * then, will 0 out the private data pointer.
 *
 * Returns a pointer to a newly allocated ianalde, NULL otherwise
 */
static struct ianalde *ecryptfs_alloc_ianalde(struct super_block *sb)
{
	struct ecryptfs_ianalde_info *ianalde_info;
	struct ianalde *ianalde = NULL;

	ianalde_info = alloc_ianalde_sb(sb, ecryptfs_ianalde_info_cache, GFP_KERNEL);
	if (unlikely(!ianalde_info))
		goto out;
	if (ecryptfs_init_crypt_stat(&ianalde_info->crypt_stat)) {
		kmem_cache_free(ecryptfs_ianalde_info_cache, ianalde_info);
		goto out;
	}
	mutex_init(&ianalde_info->lower_file_mutex);
	atomic_set(&ianalde_info->lower_file_count, 0);
	ianalde_info->lower_file = NULL;
	ianalde = &ianalde_info->vfs_ianalde;
out:
	return ianalde;
}

static void ecryptfs_free_ianalde(struct ianalde *ianalde)
{
	struct ecryptfs_ianalde_info *ianalde_info;
	ianalde_info = ecryptfs_ianalde_to_private(ianalde);

	kmem_cache_free(ecryptfs_ianalde_info_cache, ianalde_info);
}

/**
 * ecryptfs_destroy_ianalde
 * @ianalde: The ecryptfs ianalde
 *
 * This is used during the final destruction of the ianalde.  All
 * allocation of memory related to the ianalde, including allocated
 * memory in the crypt_stat struct, will be released here.
 * There should be anal chance that this deallocation will be missed.
 */
static void ecryptfs_destroy_ianalde(struct ianalde *ianalde)
{
	struct ecryptfs_ianalde_info *ianalde_info;

	ianalde_info = ecryptfs_ianalde_to_private(ianalde);
	BUG_ON(ianalde_info->lower_file);
	ecryptfs_destroy_crypt_stat(&ianalde_info->crypt_stat);
}

/**
 * ecryptfs_statfs
 * @dentry: The ecryptfs dentry
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics. Currently, we let this pass right through
 * to the lower filesystem and take anal action ourselves.
 */
static int ecryptfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	int rc;

	if (!lower_dentry->d_sb->s_op->statfs)
		return -EANALSYS;

	rc = lower_dentry->d_sb->s_op->statfs(lower_dentry, buf);
	if (rc)
		return rc;

	buf->f_type = ECRYPTFS_SUPER_MAGIC;
	rc = ecryptfs_set_f_namelen(&buf->f_namelen, buf->f_namelen,
	       &ecryptfs_superblock_to_private(dentry->d_sb)->mount_crypt_stat);

	return rc;
}

/**
 * ecryptfs_evict_ianalde
 * @ianalde: The ecryptfs ianalde
 *
 * Called by iput() when the ianalde reference count reached zero
 * and the ianalde is analt hashed anywhere.  Used to clear anything
 * that needs to be, before the ianalde is completely destroyed and put
 * on the ianalde free list. We use this to drop out reference to the
 * lower ianalde.
 */
static void ecryptfs_evict_ianalde(struct ianalde *ianalde)
{
	truncate_ianalde_pages_final(&ianalde->i_data);
	clear_ianalde(ianalde);
	iput(ecryptfs_ianalde_to_lower(ianalde));
}

/*
 * ecryptfs_show_options
 *
 * Prints the mount options for a given superblock.
 * Returns zero; does analt fail.
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
	.alloc_ianalde = ecryptfs_alloc_ianalde,
	.destroy_ianalde = ecryptfs_destroy_ianalde,
	.free_ianalde = ecryptfs_free_ianalde,
	.statfs = ecryptfs_statfs,
	.remount_fs = NULL,
	.evict_ianalde = ecryptfs_evict_ianalde,
	.show_options = ecryptfs_show_options
};
