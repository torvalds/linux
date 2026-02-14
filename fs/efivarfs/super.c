// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 */

#include <linux/ctype.h>
#include <linux/efi.h>
#include <linux/fs.h>
#include <linux/fs_context.h>
#include <linux/fs_parser.h>
#include <linux/module.h>
#include <linux/pagemap.h>
#include <linux/ucs2_string.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/magic.h>
#include <linux/statfs.h>
#include <linux/notifier.h>
#include <linux/printk.h>
#include <linux/namei.h>

#include "internal.h"
#include "../internal.h"

static int efivarfs_ops_notifier(struct notifier_block *nb, unsigned long event,
				 void *data)
{
	struct efivarfs_fs_info *sfi = container_of(nb, struct efivarfs_fs_info, nb);

	switch (event) {
	case EFIVAR_OPS_RDONLY:
		sfi->sb->s_flags |= SB_RDONLY;
		break;
	case EFIVAR_OPS_RDWR:
		sfi->sb->s_flags &= ~SB_RDONLY;
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct inode *efivarfs_alloc_inode(struct super_block *sb)
{
	struct efivar_entry *entry = kzalloc(sizeof(*entry), GFP_KERNEL);

	if (!entry)
		return NULL;

	inode_init_once(&entry->vfs_inode);
	entry->removed = false;

	return &entry->vfs_inode;
}

static void efivarfs_free_inode(struct inode *inode)
{
	struct efivar_entry *entry = efivar_entry(inode);

	kfree(entry);
}

static int efivarfs_show_options(struct seq_file *m, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct efivarfs_fs_info *sbi = sb->s_fs_info;
	struct efivarfs_mount_opts *opts = &sbi->mount_opts;

	if (!uid_eq(opts->uid, GLOBAL_ROOT_UID))
		seq_printf(m, ",uid=%u",
				from_kuid_munged(&init_user_ns, opts->uid));
	if (!gid_eq(opts->gid, GLOBAL_ROOT_GID))
		seq_printf(m, ",gid=%u",
				from_kgid_munged(&init_user_ns, opts->gid));
	return 0;
}

static int efivarfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	const u32 attr = EFI_VARIABLE_NON_VOLATILE |
			 EFI_VARIABLE_BOOTSERVICE_ACCESS |
			 EFI_VARIABLE_RUNTIME_ACCESS;
	u64 storage_space, remaining_space, max_variable_size;
	u64 id = huge_encode_dev(dentry->d_sb->s_dev);
	efi_status_t status;

	/* Some UEFI firmware does not implement QueryVariableInfo() */
	storage_space = remaining_space = 0;
	if (efi_rt_services_supported(EFI_RT_SUPPORTED_QUERY_VARIABLE_INFO)) {
		status = efivar_query_variable_info(attr, &storage_space,
						    &remaining_space,
						    &max_variable_size);
		if (status != EFI_SUCCESS && status != EFI_UNSUPPORTED)
			pr_warn_ratelimited("query_variable_info() failed: 0x%lx\n",
					    status);
	}

	/*
	 * This is not a normal filesystem, so no point in pretending it has a block
	 * size; we declare f_bsize to 1, so that we can then report the exact value
	 * sent by EFI QueryVariableInfo in f_blocks and f_bfree
	 */
	buf->f_bsize	= 1;
	buf->f_namelen	= NAME_MAX;
	buf->f_blocks	= storage_space;
	buf->f_bfree	= remaining_space;
	buf->f_type	= dentry->d_sb->s_magic;
	buf->f_fsid	= u64_to_fsid(id);

	/*
	 * In f_bavail we declare the free space that the kernel will allow writing
	 * when the storage_paranoia x86 quirk is active. To use more, users
	 * should boot the kernel with efi_no_storage_paranoia.
	 */
	if (remaining_space > efivar_reserved_space())
		buf->f_bavail = remaining_space - efivar_reserved_space();
	else
		buf->f_bavail = 0;

	return 0;
}

static int efivarfs_freeze_fs(struct super_block *sb);
static int efivarfs_unfreeze_fs(struct super_block *sb);

static const struct super_operations efivarfs_ops = {
	.statfs = efivarfs_statfs,
	.drop_inode = inode_just_drop,
	.alloc_inode = efivarfs_alloc_inode,
	.free_inode = efivarfs_free_inode,
	.show_options = efivarfs_show_options,
	.freeze_fs = efivarfs_freeze_fs,
	.unfreeze_fs = efivarfs_unfreeze_fs,
};

/*
 * Compare two efivarfs file names.
 *
 * An efivarfs filename is composed of two parts,
 *
 *	1. A case-sensitive variable name
 *	2. A case-insensitive GUID
 *
 * So we need to perform a case-sensitive match on part 1 and a
 * case-insensitive match on part 2.
 */
static int efivarfs_d_compare(const struct dentry *dentry,
			      unsigned int len, const char *str,
			      const struct qstr *name)
{
	int guid = len - EFI_VARIABLE_GUID_LEN;

	/* Parallel lookups may produce a temporary invalid filename */
	if (guid <= 0)
		return 1;

	if (name->len != len)
		return 1;

	/* Case-sensitive compare for the variable name */
	if (memcmp(str, name->name, guid))
		return 1;

	/* Case-insensitive compare for the GUID */
	return strncasecmp(name->name + guid, str + guid, EFI_VARIABLE_GUID_LEN);
}

static int efivarfs_d_hash(const struct dentry *dentry, struct qstr *qstr)
{
	unsigned long hash = init_name_hash(dentry);
	const unsigned char *s = qstr->name;
	unsigned int len = qstr->len;

	while (len-- > EFI_VARIABLE_GUID_LEN)
		hash = partial_name_hash(*s++, hash);

	/* GUID is case-insensitive. */
	while (len--)
		hash = partial_name_hash(tolower(*s++), hash);

	qstr->hash = end_name_hash(hash);
	return 0;
}

static const struct dentry_operations efivarfs_d_ops = {
	.d_compare = efivarfs_d_compare,
	.d_hash = efivarfs_d_hash,
};

static struct dentry *efivarfs_alloc_dentry(struct dentry *parent, char *name)
{
	struct dentry *d;
	struct qstr q;
	int err;

	q.name = name;
	q.len = strlen(name);

	err = efivarfs_d_hash(parent, &q);
	if (err)
		return ERR_PTR(err);

	d = d_alloc(parent, &q);
	if (d)
		return d;

	return ERR_PTR(-ENOMEM);
}

bool efivarfs_variable_is_present(efi_char16_t *variable_name,
				  efi_guid_t *vendor, void *data)
{
	char *name = efivar_get_utf8name(variable_name, vendor);
	struct super_block *sb = data;
	struct dentry *dentry;

	if (!name)
		/*
		 * If the allocation failed there'll already be an
		 * error in the log (and likely a huge and growing
		 * number of them since they system will be under
		 * extreme memory pressure), so simply assume
		 * collision for safety but don't add to the log
		 * flood.
		 */
		return true;

	dentry = try_lookup_noperm(&QSTR(name), sb->s_root);
	kfree(name);
	if (!IS_ERR_OR_NULL(dentry))
		dput(dentry);

	return dentry != NULL;
}

static int efivarfs_create_dentry(struct super_block *sb, efi_char16_t *name16,
				  unsigned long name_size, efi_guid_t vendor,
				  char *name)
{
	struct efivar_entry *entry;
	struct inode *inode;
	struct dentry *dentry, *root = sb->s_root;
	unsigned long size = 0;
	int len;
	int err = -ENOMEM;
	bool is_removable = false;

	/* length of the variable name itself: remove GUID and separator */
	len = strlen(name) - EFI_VARIABLE_GUID_LEN - 1;

	if (efivar_variable_is_removable(vendor, name, len))
		is_removable = true;

	inode = efivarfs_get_inode(sb, d_inode(root), S_IFREG | 0644, 0,
				   is_removable);
	if (!inode)
		goto fail_name;

	entry = efivar_entry(inode);

	memcpy(entry->var.VariableName, name16, name_size);
	memcpy(&(entry->var.VendorGuid), &vendor, sizeof(efi_guid_t));

	dentry = efivarfs_alloc_dentry(root, name);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto fail_inode;
	}

	__efivar_entry_get(entry, NULL, &size, NULL);

	/* copied by the above to local storage in the dentry. */
	kfree(name);

	inode_lock(inode);
	inode->i_private = entry;
	i_size_write(inode, size + sizeof(__u32)); /* attributes + data */
	inode_unlock(inode);
	d_add(dentry, inode);

	return 0;

fail_inode:
	iput(inode);
fail_name:
	kfree(name);

	return err;
}

static int efivarfs_callback(efi_char16_t *name16, efi_guid_t vendor,
			     unsigned long name_size, void *data)
{
	struct super_block *sb = (struct super_block *)data;
	char *name;

	if (guid_equal(&vendor, &LINUX_EFI_RANDOM_SEED_TABLE_GUID))
		return 0;

	name = efivar_get_utf8name(name16, &vendor);
	if (!name)
		return -ENOMEM;

	return efivarfs_create_dentry(sb, name16, name_size, vendor, name);
}

enum {
	Opt_uid, Opt_gid,
};

static const struct fs_parameter_spec efivarfs_parameters[] = {
	fsparam_uid("uid", Opt_uid),
	fsparam_gid("gid", Opt_gid),
	{},
};

static int efivarfs_parse_param(struct fs_context *fc, struct fs_parameter *param)
{
	struct efivarfs_fs_info *sbi = fc->s_fs_info;
	struct efivarfs_mount_opts *opts = &sbi->mount_opts;
	struct fs_parse_result result;
	int opt;

	opt = fs_parse(fc, efivarfs_parameters, param, &result);
	if (opt < 0)
		return opt;

	switch (opt) {
	case Opt_uid:
		opts->uid = result.uid;
		break;
	case Opt_gid:
		opts->gid = result.gid;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int efivarfs_fill_super(struct super_block *sb, struct fs_context *fc)
{
	struct efivarfs_fs_info *sfi = sb->s_fs_info;
	struct inode *inode = NULL;
	struct dentry *root;
	int err;

	sb->s_maxbytes          = MAX_LFS_FILESIZE;
	sb->s_blocksize         = PAGE_SIZE;
	sb->s_blocksize_bits    = PAGE_SHIFT;
	sb->s_magic             = EFIVARFS_MAGIC;
	sb->s_op                = &efivarfs_ops;
	set_default_d_op(sb, &efivarfs_d_ops);
	sb->s_d_flags |= DCACHE_DONTCACHE;
	sb->s_time_gran         = 1;

	if (!efivar_supports_writes())
		sb->s_flags |= SB_RDONLY;

	inode = efivarfs_get_inode(sb, NULL, S_IFDIR | 0755, 0, true);
	if (!inode)
		return -ENOMEM;
	inode->i_op = &efivarfs_dir_inode_operations;

	root = d_make_root(inode);
	sb->s_root = root;
	if (!root)
		return -ENOMEM;

	sfi->sb = sb;
	sfi->nb.notifier_call = efivarfs_ops_notifier;
	err = blocking_notifier_chain_register(&efivar_ops_nh, &sfi->nb);
	if (err)
		return err;

	return efivar_init(efivarfs_callback, sb, true);
}

static int efivarfs_get_tree(struct fs_context *fc)
{
	return get_tree_single(fc, efivarfs_fill_super);
}

static int efivarfs_reconfigure(struct fs_context *fc)
{
	if (!efivar_supports_writes() && !(fc->sb_flags & SB_RDONLY)) {
		pr_err("Firmware does not support SetVariableRT. Can not remount with rw\n");
		return -EINVAL;
	}

	return 0;
}

static void efivarfs_free(struct fs_context *fc)
{
	kfree(fc->s_fs_info);
}

static const struct fs_context_operations efivarfs_context_ops = {
	.get_tree	= efivarfs_get_tree,
	.parse_param	= efivarfs_parse_param,
	.reconfigure	= efivarfs_reconfigure,
	.free		= efivarfs_free,
};

static int efivarfs_check_missing(efi_char16_t *name16, efi_guid_t vendor,
				  unsigned long name_size, void *data)
{
	char *name;
	struct super_block *sb = data;
	struct dentry *dentry;
	int err;

	if (guid_equal(&vendor, &LINUX_EFI_RANDOM_SEED_TABLE_GUID))
		return 0;

	name = efivar_get_utf8name(name16, &vendor);
	if (!name)
		return -ENOMEM;

	dentry = try_lookup_noperm(&QSTR(name), sb->s_root);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out;
	}

	if (!dentry) {
		/* found missing entry */
		pr_info("efivarfs: creating variable %s\n", name);
		return efivarfs_create_dentry(sb, name16, name_size, vendor, name);
	}

	dput(dentry);
	err = 0;

 out:
	kfree(name);

	return err;
}

static struct file_system_type efivarfs_type;

static int efivarfs_freeze_fs(struct super_block *sb)
{
	/* Nothing for us to do. */
	return 0;
}

static int efivarfs_unfreeze_fs(struct super_block *sb)
{
	struct dentry *child = NULL;

	/*
	 * Unconditionally resync the variable state on a thaw request.
	 * Given the size of efivarfs it really doesn't matter to simply
	 * iterate through all of the entries and resync. Freeze/thaw
	 * requests are rare enough for that to not matter and the
	 * number of entries is pretty low too. So we really don't care.
	 */
	pr_info("efivarfs: resyncing variable state\n");
	for (;;) {
		int err;
		unsigned long size = 0;
		struct inode *inode;
		struct efivar_entry *entry;

		child = find_next_child(sb->s_root, child);
		if (!child)
			break;

		inode = d_inode(child);
		entry = efivar_entry(inode);

		err = efivar_entry_size(entry, &size);
		if (err)
			size = 0;
		else
			size += sizeof(__u32);

		inode_lock(inode);
		i_size_write(inode, size);
		inode_unlock(inode);

		/* The variable doesn't exist anymore, delete it. */
		if (!size) {
			pr_info("efivarfs: removing variable %pd\n", child);
			simple_recursive_removal(child, NULL);
		}
	}

	efivar_init(efivarfs_check_missing, sb, false);
	pr_info("efivarfs: finished resyncing variable state\n");
	return 0;
}

static int efivarfs_init_fs_context(struct fs_context *fc)
{
	struct efivarfs_fs_info *sfi;

	if (!efivar_is_available())
		return -EOPNOTSUPP;

	sfi = kzalloc(sizeof(*sfi), GFP_KERNEL);
	if (!sfi)
		return -ENOMEM;

	sfi->mount_opts.uid = GLOBAL_ROOT_UID;
	sfi->mount_opts.gid = GLOBAL_ROOT_GID;

	fc->s_fs_info = sfi;
	fc->ops = &efivarfs_context_ops;

	return 0;
}

static void efivarfs_kill_sb(struct super_block *sb)
{
	struct efivarfs_fs_info *sfi = sb->s_fs_info;

	blocking_notifier_chain_unregister(&efivar_ops_nh, &sfi->nb);
	kill_litter_super(sb);

	kfree(sfi);
}

static struct file_system_type efivarfs_type = {
	.owner   = THIS_MODULE,
	.name    = "efivarfs",
	.init_fs_context = efivarfs_init_fs_context,
	.kill_sb = efivarfs_kill_sb,
	.parameters = efivarfs_parameters,
	.fs_flags = FS_POWER_FREEZE,
};

static __init int efivarfs_init(void)
{
	return register_filesystem(&efivarfs_type);
}

static __exit void efivarfs_exit(void)
{
	unregister_filesystem(&efivarfs_type);
}

MODULE_AUTHOR("Matthew Garrett, Jeremy Kerr");
MODULE_DESCRIPTION("EFI Variable Filesystem");
MODULE_LICENSE("GPL");
MODULE_ALIAS_FS("efivarfs");

module_init(efivarfs_init);
module_exit(efivarfs_exit);
