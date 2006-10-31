/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2006 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *              Michael C. Thompson <mcthomps@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/skbuff.h>
#include <linux/crypto.h>
#include <linux/netlink.h>
#include <linux/mount.h>
#include <linux/dcache.h>
#include <linux/pagemap.h>
#include <linux/key.h>
#include <linux/parser.h>
#include "ecryptfs_kernel.h"

/**
 * Module parameter that defines the ecryptfs_verbosity level.
 */
int ecryptfs_verbosity = 0;

module_param(ecryptfs_verbosity, int, 0);
MODULE_PARM_DESC(ecryptfs_verbosity,
		 "Initial verbosity level (0 or 1; defaults to "
		 "0, which is Quiet)");

void __ecryptfs_printk(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	if (fmt[1] == '7') { /* KERN_DEBUG */
		if (ecryptfs_verbosity >= 1)
			vprintk(fmt, args);
	} else
		vprintk(fmt, args);
	va_end(args);
}

/**
 * ecryptfs_interpose
 * @lower_dentry: Existing dentry in the lower filesystem
 * @dentry: ecryptfs' dentry
 * @sb: ecryptfs's super_block
 * @flag: If set to true, then d_add is called, else d_instantiate is called
 *
 * Interposes upper and lower dentries.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_interpose(struct dentry *lower_dentry, struct dentry *dentry,
		       struct super_block *sb, int flag)
{
	struct inode *lower_inode;
	struct inode *inode;
	int rc = 0;

	lower_inode = lower_dentry->d_inode;
	if (lower_inode->i_sb != ecryptfs_superblock_to_lower(sb)) {
		rc = -EXDEV;
		goto out;
	}
	if (!igrab(lower_inode)) {
		rc = -ESTALE;
		goto out;
	}
	inode = iget5_locked(sb, (unsigned long)lower_inode,
			     ecryptfs_inode_test, ecryptfs_inode_set,
			     lower_inode);
	if (!inode) {
		rc = -EACCES;
		iput(lower_inode);
		goto out;
	}
	if (inode->i_state & I_NEW)
		unlock_new_inode(inode);
	else
		iput(lower_inode);
	if (S_ISLNK(lower_inode->i_mode))
		inode->i_op = &ecryptfs_symlink_iops;
	else if (S_ISDIR(lower_inode->i_mode))
		inode->i_op = &ecryptfs_dir_iops;
	if (S_ISDIR(lower_inode->i_mode))
		inode->i_fop = &ecryptfs_dir_fops;
	if (special_file(lower_inode->i_mode))
		init_special_inode(inode, lower_inode->i_mode,
				   lower_inode->i_rdev);
	dentry->d_op = &ecryptfs_dops;
	if (flag)
		d_add(dentry, inode);
	else
		d_instantiate(dentry, inode);
	ecryptfs_copy_attr_all(inode, lower_inode);
	/* This size will be overwritten for real files w/ headers and
	 * other metadata */
	ecryptfs_copy_inode_size(inode, lower_inode);
out:
	return rc;
}

enum { ecryptfs_opt_sig, ecryptfs_opt_ecryptfs_sig, ecryptfs_opt_debug,
       ecryptfs_opt_ecryptfs_debug, ecryptfs_opt_cipher,
       ecryptfs_opt_ecryptfs_cipher, ecryptfs_opt_ecryptfs_key_bytes,
       ecryptfs_opt_passthrough, ecryptfs_opt_err };

static match_table_t tokens = {
	{ecryptfs_opt_sig, "sig=%s"},
	{ecryptfs_opt_ecryptfs_sig, "ecryptfs_sig=%s"},
	{ecryptfs_opt_debug, "debug=%u"},
	{ecryptfs_opt_ecryptfs_debug, "ecryptfs_debug=%u"},
	{ecryptfs_opt_cipher, "cipher=%s"},
	{ecryptfs_opt_ecryptfs_cipher, "ecryptfs_cipher=%s"},
	{ecryptfs_opt_ecryptfs_key_bytes, "ecryptfs_key_bytes=%u"},
	{ecryptfs_opt_passthrough, "ecryptfs_passthrough"},
	{ecryptfs_opt_err, NULL}
};

/**
 * ecryptfs_verify_version
 * @version: The version number to confirm
 *
 * Returns zero on good version; non-zero otherwise
 */
static int ecryptfs_verify_version(u16 version)
{
	int rc = 0;
	unsigned char major;
	unsigned char minor;

	major = ((version >> 8) & 0xFF);
	minor = (version & 0xFF);
	if (major != ECRYPTFS_VERSION_MAJOR) {
		ecryptfs_printk(KERN_ERR, "Major version number mismatch. "
				"Expected [%d]; got [%d]\n",
				ECRYPTFS_VERSION_MAJOR, major);
		rc = -EINVAL;
		goto out;
	}
	if (minor != ECRYPTFS_VERSION_MINOR) {
		ecryptfs_printk(KERN_ERR, "Minor version number mismatch. "
				"Expected [%d]; got [%d]\n",
				ECRYPTFS_VERSION_MINOR, minor);
		rc = -EINVAL;
		goto out;
	}
out:
	return rc;
}

/**
 * ecryptfs_parse_options
 * @sb: The ecryptfs super block
 * @options: The options pased to the kernel
 *
 * Parse mount options:
 * debug=N 	   - ecryptfs_verbosity level for debug output
 * sig=XXX	   - description(signature) of the key to use
 *
 * Returns the dentry object of the lower-level (lower/interposed)
 * directory; We want to mount our stackable file system on top of
 * that lower directory.
 *
 * The signature of the key to use must be the description of a key
 * already in the keyring. Mounting will fail if the key can not be
 * found.
 *
 * Returns zero on success; non-zero on error
 */
static int ecryptfs_parse_options(struct super_block *sb, char *options)
{
	char *p;
	int rc = 0;
	int sig_set = 0;
	int cipher_name_set = 0;
	int cipher_key_bytes;
	int cipher_key_bytes_set = 0;
	struct key *auth_tok_key = NULL;
	struct ecryptfs_auth_tok *auth_tok = NULL;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat =
		&ecryptfs_superblock_to_private(sb)->mount_crypt_stat;
	substring_t args[MAX_OPT_ARGS];
	int token;
	char *sig_src;
	char *sig_dst;
	char *debug_src;
	char *cipher_name_dst;
	char *cipher_name_src;
	char *cipher_key_bytes_src;
	int cipher_name_len;

	if (!options) {
		rc = -EINVAL;
		goto out;
	}
	while ((p = strsep(&options, ",")) != NULL) {
		if (!*p)
			continue;
		token = match_token(p, tokens, args);
		switch (token) {
		case ecryptfs_opt_sig:
		case ecryptfs_opt_ecryptfs_sig:
			sig_src = args[0].from;
			sig_dst =
				mount_crypt_stat->global_auth_tok_sig;
			memcpy(sig_dst, sig_src, ECRYPTFS_SIG_SIZE_HEX);
			sig_dst[ECRYPTFS_SIG_SIZE_HEX] = '\0';
			ecryptfs_printk(KERN_DEBUG,
					"The mount_crypt_stat "
					"global_auth_tok_sig set to: "
					"[%s]\n", sig_dst);
			sig_set = 1;
			break;
		case ecryptfs_opt_debug:
		case ecryptfs_opt_ecryptfs_debug:
			debug_src = args[0].from;
			ecryptfs_verbosity =
				(int)simple_strtol(debug_src, &debug_src,
						   0);
			ecryptfs_printk(KERN_DEBUG,
					"Verbosity set to [%d]" "\n",
					ecryptfs_verbosity);
			break;
		case ecryptfs_opt_cipher:
		case ecryptfs_opt_ecryptfs_cipher:
			cipher_name_src = args[0].from;
			cipher_name_dst =
				mount_crypt_stat->
				global_default_cipher_name;
			strncpy(cipher_name_dst, cipher_name_src,
				ECRYPTFS_MAX_CIPHER_NAME_SIZE);
			ecryptfs_printk(KERN_DEBUG,
					"The mount_crypt_stat "
					"global_default_cipher_name set to: "
					"[%s]\n", cipher_name_dst);
			cipher_name_set = 1;
			break;
		case ecryptfs_opt_ecryptfs_key_bytes:
			cipher_key_bytes_src = args[0].from;
			cipher_key_bytes =
				(int)simple_strtol(cipher_key_bytes_src,
						   &cipher_key_bytes_src, 0);
			mount_crypt_stat->global_default_cipher_key_size =
				cipher_key_bytes;
			ecryptfs_printk(KERN_DEBUG,
					"The mount_crypt_stat "
					"global_default_cipher_key_size "
					"set to: [%d]\n", mount_crypt_stat->
					global_default_cipher_key_size);
			cipher_key_bytes_set = 1;
			break;
		case ecryptfs_opt_passthrough:
			mount_crypt_stat->flags |=
				ECRYPTFS_PLAINTEXT_PASSTHROUGH_ENABLED;
			break;
		case ecryptfs_opt_err:
		default:
			ecryptfs_printk(KERN_WARNING,
					"eCryptfs: unrecognized option '%s'\n",
					p);
		}
	}
	/* Do not support lack of mount-wide signature in 0.1
	 * release */
	if (!sig_set) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "You must supply a valid "
				"passphrase auth tok signature as a mount "
				"parameter; see the eCryptfs README\n");
		goto out;
	}
	if (!cipher_name_set) {
		cipher_name_len = strlen(ECRYPTFS_DEFAULT_CIPHER);
		if (unlikely(cipher_name_len
			     >= ECRYPTFS_MAX_CIPHER_NAME_SIZE)) {
			rc = -EINVAL;
			BUG();
			goto out;
		}
		memcpy(mount_crypt_stat->global_default_cipher_name,
		       ECRYPTFS_DEFAULT_CIPHER, cipher_name_len);
		mount_crypt_stat->global_default_cipher_name[cipher_name_len]
		    = '\0';
	}
	if (!cipher_key_bytes_set) {
		mount_crypt_stat->global_default_cipher_key_size = 0;
	}
	rc = ecryptfs_process_cipher(
		&mount_crypt_stat->global_key_tfm,
		mount_crypt_stat->global_default_cipher_name,
		&mount_crypt_stat->global_default_cipher_key_size);
	if (rc) {
		printk(KERN_ERR "Error attempting to initialize cipher [%s] "
		       "with key size [%Zd] bytes; rc = [%d]\n",
		       mount_crypt_stat->global_default_cipher_name,
		       mount_crypt_stat->global_default_cipher_key_size, rc);
		rc = -EINVAL;
		goto out;
	}
	mutex_init(&mount_crypt_stat->global_key_tfm_mutex);
	ecryptfs_printk(KERN_DEBUG, "Requesting the key with description: "
			"[%s]\n", mount_crypt_stat->global_auth_tok_sig);
	/* The reference to this key is held until umount is done The
	 * call to key_put is done in ecryptfs_put_super() */
	auth_tok_key = request_key(&key_type_user,
				   mount_crypt_stat->global_auth_tok_sig,
				   NULL);
	if (!auth_tok_key || IS_ERR(auth_tok_key)) {
		ecryptfs_printk(KERN_ERR, "Could not find key with "
				"description: [%s]\n",
				mount_crypt_stat->global_auth_tok_sig);
		process_request_key_err(PTR_ERR(auth_tok_key));
		rc = -EINVAL;
		goto out;
	}
	auth_tok = ecryptfs_get_key_payload_data(auth_tok_key);
	if (ecryptfs_verify_version(auth_tok->version)) {
		ecryptfs_printk(KERN_ERR, "Data structure version mismatch. "
				"Userspace tools must match eCryptfs kernel "
				"module with major version [%d] and minor "
				"version [%d]\n", ECRYPTFS_VERSION_MAJOR,
				ECRYPTFS_VERSION_MINOR);
		rc = -EINVAL;
		goto out;
	}
	if (auth_tok->token_type != ECRYPTFS_PASSWORD) {
		ecryptfs_printk(KERN_ERR, "Invalid auth_tok structure "
				"returned from key\n");
		rc = -EINVAL;
		goto out;
	}
	mount_crypt_stat->global_auth_tok_key = auth_tok_key;
	mount_crypt_stat->global_auth_tok = auth_tok;
out:
	return rc;
}

struct kmem_cache *ecryptfs_sb_info_cache;

/**
 * ecryptfs_fill_super
 * @sb: The ecryptfs super block
 * @raw_data: The options passed to mount
 * @silent: Not used but required by function prototype
 *
 * Sets up what we can of the sb, rest is done in ecryptfs_read_super
 *
 * Returns zero on success; non-zero otherwise
 */
static int
ecryptfs_fill_super(struct super_block *sb, void *raw_data, int silent)
{
	int rc = 0;

	/* Released in ecryptfs_put_super() */
	ecryptfs_set_superblock_private(sb,
					kmem_cache_alloc(ecryptfs_sb_info_cache,
							 SLAB_KERNEL));
	if (!ecryptfs_superblock_to_private(sb)) {
		ecryptfs_printk(KERN_WARNING, "Out of memory\n");
		rc = -ENOMEM;
		goto out;
	}
	memset(ecryptfs_superblock_to_private(sb), 0,
	       sizeof(struct ecryptfs_sb_info));
	sb->s_op = &ecryptfs_sops;
	/* Released through deactivate_super(sb) from get_sb_nodev */
	sb->s_root = d_alloc(NULL, &(const struct qstr) {
			     .hash = 0,.name = "/",.len = 1});
	if (!sb->s_root) {
		ecryptfs_printk(KERN_ERR, "d_alloc failed\n");
		rc = -ENOMEM;
		goto out;
	}
	sb->s_root->d_op = &ecryptfs_dops;
	sb->s_root->d_sb = sb;
	sb->s_root->d_parent = sb->s_root;
	/* Released in d_release when dput(sb->s_root) is called */
	/* through deactivate_super(sb) from get_sb_nodev() */
	ecryptfs_set_dentry_private(sb->s_root,
				    kmem_cache_alloc(ecryptfs_dentry_info_cache,
						     SLAB_KERNEL));
	if (!ecryptfs_dentry_to_private(sb->s_root)) {
		ecryptfs_printk(KERN_ERR,
				"dentry_info_cache alloc failed\n");
		rc = -ENOMEM;
		goto out;
	}
	memset(ecryptfs_dentry_to_private(sb->s_root), 0,
	       sizeof(struct ecryptfs_dentry_info));
	rc = 0;
out:
	/* Should be able to rely on deactivate_super called from
	 * get_sb_nodev */
	return rc;
}

/**
 * ecryptfs_read_super
 * @sb: The ecryptfs super block
 * @dev_name: The path to mount over
 *
 * Read the super block of the lower filesystem, and use
 * ecryptfs_interpose to create our initial inode and super block
 * struct.
 */
static int ecryptfs_read_super(struct super_block *sb, const char *dev_name)
{
	int rc;
	struct nameidata nd;
	struct dentry *lower_root;
	struct vfsmount *lower_mnt;

	memset(&nd, 0, sizeof(struct nameidata));
	rc = path_lookup(dev_name, LOOKUP_FOLLOW, &nd);
	if (rc) {
		ecryptfs_printk(KERN_WARNING, "path_lookup() failed\n");
		goto out_free;
	}
	lower_root = nd.dentry;
	if (!lower_root->d_inode) {
		ecryptfs_printk(KERN_WARNING,
				"No directory to interpose on\n");
		rc = -ENOENT;
		goto out_free;
	}
	lower_mnt = nd.mnt;
	ecryptfs_set_superblock_lower(sb, lower_root->d_sb);
	sb->s_maxbytes = lower_root->d_sb->s_maxbytes;
	ecryptfs_set_dentry_lower(sb->s_root, lower_root);
	ecryptfs_set_dentry_lower_mnt(sb->s_root, lower_mnt);
	if ((rc = ecryptfs_interpose(lower_root, sb->s_root, sb, 0)))
		goto out_free;
	rc = 0;
	goto out;
out_free:
	path_release(&nd);
out:
	return rc;
}

/**
 * ecryptfs_get_sb
 * @fs_type
 * @flags
 * @dev_name: The path to mount over
 * @raw_data: The options passed into the kernel
 *
 * The whole ecryptfs_get_sb process is broken into 4 functions:
 * ecryptfs_parse_options(): handle options passed to ecryptfs, if any
 * ecryptfs_fill_super(): used by get_sb_nodev, fills out the super_block
 *                        with as much information as it can before needing
 *                        the lower filesystem.
 * ecryptfs_read_super(): this accesses the lower filesystem and uses
 *                        ecryptfs_interpolate to perform most of the linking
 * ecryptfs_interpolate(): links the lower filesystem into ecryptfs
 */
static int ecryptfs_get_sb(struct file_system_type *fs_type, int flags,
			const char *dev_name, void *raw_data,
			struct vfsmount *mnt)
{
	int rc;
	struct super_block *sb;

	rc = get_sb_nodev(fs_type, flags, raw_data, ecryptfs_fill_super, mnt);
	if (rc < 0) {
		printk(KERN_ERR "Getting sb failed; rc = [%d]\n", rc);
		goto out;
	}
	sb = mnt->mnt_sb;
	rc = ecryptfs_parse_options(sb, raw_data);
	if (rc) {
		printk(KERN_ERR "Error parsing options; rc = [%d]\n", rc);
		goto out_abort;
	}
	rc = ecryptfs_read_super(sb, dev_name);
	if (rc) {
		printk(KERN_ERR "Reading sb failed; rc = [%d]\n", rc);
		goto out_abort;
	}
	goto out;
out_abort:
	dput(sb->s_root);
	up_write(&sb->s_umount);
	deactivate_super(sb);
out:
	return rc;
}

/**
 * ecryptfs_kill_block_super
 * @sb: The ecryptfs super block
 *
 * Used to bring the superblock down and free the private data.
 * Private data is free'd in ecryptfs_put_super()
 */
static void ecryptfs_kill_block_super(struct super_block *sb)
{
	generic_shutdown_super(sb);
}

static struct file_system_type ecryptfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "ecryptfs",
	.get_sb = ecryptfs_get_sb,
	.kill_sb = ecryptfs_kill_block_super,
	.fs_flags = 0
};

/**
 * inode_info_init_once
 *
 * Initializes the ecryptfs_inode_info_cache when it is created
 */
static void
inode_info_init_once(void *vptr, struct kmem_cache *cachep, unsigned long flags)
{
	struct ecryptfs_inode_info *ei = (struct ecryptfs_inode_info *)vptr;

	if ((flags & (SLAB_CTOR_VERIFY | SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR)
		inode_init_once(&ei->vfs_inode);
}

static struct ecryptfs_cache_info {
	kmem_cache_t **cache;
	const char *name;
	size_t size;
	void (*ctor)(void*, struct kmem_cache *, unsigned long);
} ecryptfs_cache_infos[] = {
	{
		.cache = &ecryptfs_auth_tok_list_item_cache,
		.name = "ecryptfs_auth_tok_list_item",
		.size = sizeof(struct ecryptfs_auth_tok_list_item),
	},
	{
		.cache = &ecryptfs_file_info_cache,
		.name = "ecryptfs_file_cache",
		.size = sizeof(struct ecryptfs_file_info),
	},
	{
		.cache = &ecryptfs_dentry_info_cache,
		.name = "ecryptfs_dentry_info_cache",
		.size = sizeof(struct ecryptfs_dentry_info),
	},
	{
		.cache = &ecryptfs_inode_info_cache,
		.name = "ecryptfs_inode_cache",
		.size = sizeof(struct ecryptfs_inode_info),
		.ctor = inode_info_init_once,
	},
	{
		.cache = &ecryptfs_sb_info_cache,
		.name = "ecryptfs_sb_cache",
		.size = sizeof(struct ecryptfs_sb_info),
	},
	{
		.cache = &ecryptfs_header_cache_0,
		.name = "ecryptfs_headers_0",
		.size = PAGE_CACHE_SIZE,
	},
	{
		.cache = &ecryptfs_header_cache_1,
		.name = "ecryptfs_headers_1",
		.size = PAGE_CACHE_SIZE,
	},
	{
		.cache = &ecryptfs_header_cache_2,
		.name = "ecryptfs_headers_2",
		.size = PAGE_CACHE_SIZE,
	},
	{
		.cache = &ecryptfs_lower_page_cache,
		.name = "ecryptfs_lower_page_cache",
		.size = PAGE_CACHE_SIZE,
	},
};

static void ecryptfs_free_kmem_caches(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ecryptfs_cache_infos); i++) {
		struct ecryptfs_cache_info *info;

		info = &ecryptfs_cache_infos[i];
		if (*(info->cache))
			kmem_cache_destroy(*(info->cache));
	}
}

/**
 * ecryptfs_init_kmem_caches
 *
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_init_kmem_caches(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ecryptfs_cache_infos); i++) {
		struct ecryptfs_cache_info *info;

		info = &ecryptfs_cache_infos[i];
		*(info->cache) = kmem_cache_create(info->name, info->size,
				0, SLAB_HWCACHE_ALIGN, info->ctor, NULL);
		if (!*(info->cache)) {
			ecryptfs_free_kmem_caches();
			ecryptfs_printk(KERN_WARNING, "%s: "
					"kmem_cache_create failed\n",
					info->name);
			return -ENOMEM;
		}
	}
	return 0;
}

struct ecryptfs_obj {
	char *name;
	struct list_head slot_list;
	struct kobject kobj;
};

struct ecryptfs_attribute {
	struct attribute attr;
	ssize_t(*show) (struct ecryptfs_obj *, char *);
	ssize_t(*store) (struct ecryptfs_obj *, const char *, size_t);
};

static ssize_t
ecryptfs_attr_store(struct kobject *kobj,
		    struct attribute *attr, const char *buf, size_t len)
{
	struct ecryptfs_obj *obj = container_of(kobj, struct ecryptfs_obj,
						kobj);
	struct ecryptfs_attribute *attribute =
		container_of(attr, struct ecryptfs_attribute, attr);

	return (attribute->store ? attribute->store(obj, buf, len) : 0);
}

static ssize_t
ecryptfs_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct ecryptfs_obj *obj = container_of(kobj, struct ecryptfs_obj,
						kobj);
	struct ecryptfs_attribute *attribute =
		container_of(attr, struct ecryptfs_attribute, attr);

	return (attribute->show ? attribute->show(obj, buf) : 0);
}

static struct sysfs_ops ecryptfs_sysfs_ops = {
	.show = ecryptfs_attr_show,
	.store = ecryptfs_attr_store
};

static struct kobj_type ecryptfs_ktype = {
	.sysfs_ops = &ecryptfs_sysfs_ops
};

static decl_subsys(ecryptfs, &ecryptfs_ktype, NULL);

static ssize_t version_show(struct ecryptfs_obj *obj, char *buff)
{
	return snprintf(buff, PAGE_SIZE, "%d\n", ECRYPTFS_VERSIONING_MASK);
}

static struct ecryptfs_attribute sysfs_attr_version = __ATTR_RO(version);

struct ecryptfs_version_str_map_elem {
	u32 flag;
	char *str;
} ecryptfs_version_str_map[] = {
	{ECRYPTFS_VERSIONING_PASSPHRASE, "passphrase"},
	{ECRYPTFS_VERSIONING_PUBKEY, "pubkey"},
	{ECRYPTFS_VERSIONING_PLAINTEXT_PASSTHROUGH, "plaintext passthrough"},
	{ECRYPTFS_VERSIONING_POLICY, "policy"}
};

static ssize_t version_str_show(struct ecryptfs_obj *obj, char *buff)
{
	int i;
	int remaining = PAGE_SIZE;
	int total_written = 0;

	buff[0] = '\0';
	for (i = 0; i < ARRAY_SIZE(ecryptfs_version_str_map); i++) {
		int entry_size;

		if (!(ECRYPTFS_VERSIONING_MASK
		      & ecryptfs_version_str_map[i].flag))
			continue;
		entry_size = strlen(ecryptfs_version_str_map[i].str);
		if ((entry_size + 2) > remaining)
			goto out;
		memcpy(buff, ecryptfs_version_str_map[i].str, entry_size);
		buff[entry_size++] = '\n';
		buff[entry_size] = '\0';
		buff += entry_size;
		total_written += entry_size;
		remaining -= entry_size;
	}
out:
	return total_written;
}

static struct ecryptfs_attribute sysfs_attr_version_str = __ATTR_RO(version_str);

static int do_sysfs_registration(void)
{
	int rc;

	if ((rc = subsystem_register(&ecryptfs_subsys))) {
		printk(KERN_ERR
		       "Unable to register ecryptfs sysfs subsystem\n");
		goto out;
	}
	rc = sysfs_create_file(&ecryptfs_subsys.kset.kobj,
			       &sysfs_attr_version.attr);
	if (rc) {
		printk(KERN_ERR
		       "Unable to create ecryptfs version attribute\n");
		subsystem_unregister(&ecryptfs_subsys);
		goto out;
	}
	rc = sysfs_create_file(&ecryptfs_subsys.kset.kobj,
			       &sysfs_attr_version_str.attr);
	if (rc) {
		printk(KERN_ERR
		       "Unable to create ecryptfs version_str attribute\n");
		sysfs_remove_file(&ecryptfs_subsys.kset.kobj,
				  &sysfs_attr_version.attr);
		subsystem_unregister(&ecryptfs_subsys);
		goto out;
	}
out:
	return rc;
}

static int __init ecryptfs_init(void)
{
	int rc;

	if (ECRYPTFS_DEFAULT_EXTENT_SIZE > PAGE_CACHE_SIZE) {
		rc = -EINVAL;
		ecryptfs_printk(KERN_ERR, "The eCryptfs extent size is "
				"larger than the host's page size, and so "
				"eCryptfs cannot run on this system. The "
				"default eCryptfs extent size is [%d] bytes; "
				"the page size is [%d] bytes.\n",
				ECRYPTFS_DEFAULT_EXTENT_SIZE, PAGE_CACHE_SIZE);
		goto out;
	}
	rc = ecryptfs_init_kmem_caches();
	if (rc) {
		printk(KERN_ERR
		       "Failed to allocate one or more kmem_cache objects\n");
		goto out;
	}
	rc = register_filesystem(&ecryptfs_fs_type);
	if (rc) {
		printk(KERN_ERR "Failed to register filesystem\n");
		ecryptfs_free_kmem_caches();
		goto out;
	}
	kset_set_kset_s(&ecryptfs_subsys, fs_subsys);
	sysfs_attr_version.attr.owner = THIS_MODULE;
	sysfs_attr_version_str.attr.owner = THIS_MODULE;
	rc = do_sysfs_registration();
	if (rc) {
		printk(KERN_ERR "sysfs registration failed\n");
		unregister_filesystem(&ecryptfs_fs_type);
		ecryptfs_free_kmem_caches();
		goto out;
	}
out:
	return rc;
}

static void __exit ecryptfs_exit(void)
{
	sysfs_remove_file(&ecryptfs_subsys.kset.kobj,
			  &sysfs_attr_version.attr);
	sysfs_remove_file(&ecryptfs_subsys.kset.kobj,
			  &sysfs_attr_version_str.attr);
	subsystem_unregister(&ecryptfs_subsys);
	unregister_filesystem(&ecryptfs_fs_type);
	ecryptfs_free_kmem_caches();
}

MODULE_AUTHOR("Michael A. Halcrow <mhalcrow@us.ibm.com>");
MODULE_DESCRIPTION("eCryptfs");

MODULE_LICENSE("GPL");

module_init(ecryptfs_init)
module_exit(ecryptfs_exit)
