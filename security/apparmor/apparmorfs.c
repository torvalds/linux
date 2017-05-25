/*
 * AppArmor security module
 *
 * This file contains AppArmor /sys/kernel/security/apparmor interface functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/ctype.h>
#include <linux/security.h>
#include <linux/vmalloc.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/capability.h>
#include <linux/rcupdate.h>
#include <linux/fs.h>
#include <uapi/linux/major.h>
#include <uapi/linux/magic.h>

#include "include/apparmor.h"
#include "include/apparmorfs.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/crypto.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/resource.h"
#include "include/policy_unpack.h"

/**
 * aa_mangle_name - mangle a profile name to std profile layout form
 * @name: profile name to mangle  (NOT NULL)
 * @target: buffer to store mangled name, same length as @name (MAYBE NULL)
 *
 * Returns: length of mangled name
 */
static int mangle_name(const char *name, char *target)
{
	char *t = target;

	while (*name == '/' || *name == '.')
		name++;

	if (target) {
		for (; *name; name++) {
			if (*name == '/')
				*(t)++ = '.';
			else if (isspace(*name))
				*(t)++ = '_';
			else if (isalnum(*name) || strchr("._-", *name))
				*(t)++ = *name;
		}

		*t = 0;
	} else {
		int len = 0;
		for (; *name; name++) {
			if (isalnum(*name) || isspace(*name) ||
			    strchr("/._-", *name))
				len++;
		}

		return len;
	}

	return t - target;
}


/*
 * aafs - core fns and data for the policy tree
 */

#define AAFS_NAME		"apparmorfs"
static struct vfsmount *aafs_mnt;
static int aafs_count;


static int aafs_show_path(struct seq_file *seq, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);

	seq_printf(seq, "%s:[%lu]", AAFS_NAME, inode->i_ino);
	return 0;
}

static void aafs_evict_inode(struct inode *inode)
{
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
	if (S_ISLNK(inode->i_mode))
		kfree(inode->i_link);
}

static const struct super_operations aafs_super_ops = {
	.statfs = simple_statfs,
	.evict_inode = aafs_evict_inode,
	.show_path = aafs_show_path,
};

static int fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr files[] = { {""} };
	int error;

	error = simple_fill_super(sb, AAFS_MAGIC, files);
	if (error)
		return error;
	sb->s_op = &aafs_super_ops;

	return 0;
}

static struct dentry *aafs_mount(struct file_system_type *fs_type,
				 int flags, const char *dev_name, void *data)
{
	return mount_single(fs_type, flags, data, fill_super);
}

static struct file_system_type aafs_ops = {
	.owner = THIS_MODULE,
	.name = AAFS_NAME,
	.mount = aafs_mount,
	.kill_sb = kill_anon_super,
};

/**
 * __aafs_setup_d_inode - basic inode setup for apparmorfs
 * @dir: parent directory for the dentry
 * @dentry: dentry we are seting the inode up for
 * @mode: permissions the file should have
 * @data: data to store on inode.i_private, available in open()
 * @link: if symlink, symlink target string
 * @fops: struct file_operations that should be used
 * @iops: struct of inode_operations that should be used
 */
static int __aafs_setup_d_inode(struct inode *dir, struct dentry *dentry,
			       umode_t mode, void *data, char *link,
			       const struct file_operations *fops,
			       const struct inode_operations *iops)
{
	struct inode *inode = new_inode(dir->i_sb);

	AA_BUG(!dir);
	AA_BUG(!dentry);

	if (!inode)
		return -ENOMEM;

	inode->i_ino = get_next_ino();
	inode->i_mode = mode;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_private = data;
	if (S_ISDIR(mode)) {
		inode->i_op = iops ? iops : &simple_dir_inode_operations;
		inode->i_fop = &simple_dir_operations;
		inc_nlink(inode);
		inc_nlink(dir);
	} else if (S_ISLNK(mode)) {
		inode->i_op = iops ? iops : &simple_symlink_inode_operations;
		inode->i_link = link;
	} else {
		inode->i_fop = fops;
	}
	d_instantiate(dentry, inode);
	dget(dentry);

	return 0;
}

/**
 * aafs_create - create a dentry in the apparmorfs filesystem
 *
 * @name: name of dentry to create
 * @mode: permissions the file should have
 * @parent: parent directory for this dentry
 * @data: data to store on inode.i_private, available in open()
 * @link: if symlink, symlink target string
 * @fops: struct file_operations that should be used for
 * @iops: struct of inode_operations that should be used
 *
 * This is the basic "create a xxx" function for apparmorfs.
 *
 * Returns a pointer to a dentry if it succeeds, that must be free with
 * aafs_remove(). Will return ERR_PTR on failure.
 */
static struct dentry *aafs_create(const char *name, umode_t mode,
				  struct dentry *parent, void *data, void *link,
				  const struct file_operations *fops,
				  const struct inode_operations *iops)
{
	struct dentry *dentry;
	struct inode *dir;
	int error;

	AA_BUG(!name);
	AA_BUG(!parent);

	if (!(mode & S_IFMT))
		mode = (mode & S_IALLUGO) | S_IFREG;

	error = simple_pin_fs(&aafs_ops, &aafs_mnt, &aafs_count);
	if (error)
		return ERR_PTR(error);

	dir = d_inode(parent);

	inode_lock(dir);
	dentry = lookup_one_len(name, parent, strlen(name));
	if (IS_ERR(dentry))
		goto fail_lock;

	if (d_really_is_positive(dentry)) {
		error = -EEXIST;
		goto fail_dentry;
	}

	error = __aafs_setup_d_inode(dir, dentry, mode, data, link, fops, iops);
	if (error)
		goto fail_dentry;
	inode_unlock(dir);

	return dentry;

fail_dentry:
	dput(dentry);

fail_lock:
	inode_unlock(dir);
	simple_release_fs(&aafs_mnt, &aafs_count);

	return ERR_PTR(error);
}

/**
 * aafs_create_file - create a file in the apparmorfs filesystem
 *
 * @name: name of dentry to create
 * @mode: permissions the file should have
 * @parent: parent directory for this dentry
 * @data: data to store on inode.i_private, available in open()
 * @fops: struct file_operations that should be used for
 *
 * see aafs_create
 */
static struct dentry *aafs_create_file(const char *name, umode_t mode,
				       struct dentry *parent, void *data,
				       const struct file_operations *fops)
{
	return aafs_create(name, mode, parent, data, NULL, fops, NULL);
}

/**
 * aafs_create_dir - create a directory in the apparmorfs filesystem
 *
 * @name: name of dentry to create
 * @parent: parent directory for this dentry
 *
 * see aafs_create
 */
static struct dentry *aafs_create_dir(const char *name, struct dentry *parent)
{
	return aafs_create(name, S_IFDIR | 0755, parent, NULL, NULL, NULL,
			   NULL);
}

/**
 * aafs_create_symlink - create a symlink in the apparmorfs filesystem
 * @name: name of dentry to create
 * @parent: parent directory for this dentry
 * @target: if symlink, symlink target string
 * @iops: struct of inode_operations that should be used
 *
 * If @target parameter is %NULL, then the @iops parameter needs to be
 * setup to handle .readlink and .get_link inode_operations.
 */
static struct dentry *aafs_create_symlink(const char *name,
					  struct dentry *parent,
					  const char *target,
					  const struct inode_operations *iops)
{
	struct dentry *dent;
	char *link = NULL;

	if (target) {
		link = kstrdup(target, GFP_KERNEL);
		if (!link)
			return ERR_PTR(-ENOMEM);
	}
	dent = aafs_create(name, S_IFLNK | 0444, parent, NULL, link, NULL,
			   iops);
	if (IS_ERR(dent))
		kfree(link);

	return dent;
}

/**
 * aafs_remove - removes a file or directory from the apparmorfs filesystem
 *
 * @dentry: dentry of the file/directory/symlink to removed.
 */
static void aafs_remove(struct dentry *dentry)
{
	struct inode *dir;

	if (!dentry || IS_ERR(dentry))
		return;

	dir = d_inode(dentry->d_parent);
	inode_lock(dir);
	if (simple_positive(dentry)) {
		if (d_is_dir(dentry))
			simple_rmdir(dir, dentry);
		else
			simple_unlink(dir, dentry);
		dput(dentry);
	}
	inode_unlock(dir);
	simple_release_fs(&aafs_mnt, &aafs_count);
}


/*
 * aa_fs - policy load/replace/remove
 */

/**
 * aa_simple_write_to_buffer - common routine for getting policy from user
 * @userbuf: user buffer to copy data from  (NOT NULL)
 * @alloc_size: size of user buffer (REQUIRES: @alloc_size >= @copy_size)
 * @copy_size: size of data to copy from user buffer
 * @pos: position write is at in the file (NOT NULL)
 *
 * Returns: kernel buffer containing copy of user buffer data or an
 *          ERR_PTR on failure.
 */
static struct aa_loaddata *aa_simple_write_to_buffer(const char __user *userbuf,
						     size_t alloc_size,
						     size_t copy_size,
						     loff_t *pos)
{
	struct aa_loaddata *data;

	AA_BUG(copy_size > alloc_size);

	if (*pos != 0)
		/* only writes from pos 0, that is complete writes */
		return ERR_PTR(-ESPIPE);

	/* freed by caller to simple_write_to_buffer */
	data = aa_loaddata_alloc(alloc_size);
	if (IS_ERR(data))
		return data;

	data->size = copy_size;
	if (copy_from_user(data->data, userbuf, copy_size)) {
		kvfree(data);
		return ERR_PTR(-EFAULT);
	}

	return data;
}

static ssize_t policy_update(int binop, const char __user *buf, size_t size,
			     loff_t *pos, struct aa_ns *ns)
{
	ssize_t error;
	struct aa_loaddata *data;
	struct aa_profile *profile = aa_current_profile();
	const char *op = binop == PROF_ADD ? OP_PROF_LOAD : OP_PROF_REPL;
	/* high level check about policy management - fine grained in
	 * below after unpack
	 */
	error = aa_may_manage_policy(profile, ns, op);
	if (error)
		return error;

	data = aa_simple_write_to_buffer(buf, size, size, pos);
	error = PTR_ERR(data);
	if (!IS_ERR(data)) {
		error = aa_replace_profiles(ns ? ns : profile->ns, profile,
					    binop, data);
		aa_put_loaddata(data);
	}

	return error;
}

/* .load file hook fn to load policy */
static ssize_t profile_load(struct file *f, const char __user *buf, size_t size,
			    loff_t *pos)
{
	struct aa_ns *ns = aa_get_ns(f->f_inode->i_private);
	int error = policy_update(PROF_ADD, buf, size, pos, ns);

	aa_put_ns(ns);

	return error;
}

static const struct file_operations aa_fs_profile_load = {
	.write = profile_load,
	.llseek = default_llseek,
};

/* .replace file hook fn to load and/or replace policy */
static ssize_t profile_replace(struct file *f, const char __user *buf,
			       size_t size, loff_t *pos)
{
	struct aa_ns *ns = aa_get_ns(f->f_inode->i_private);
	int error = policy_update(PROF_REPLACE, buf, size, pos, ns);

	aa_put_ns(ns);

	return error;
}

static const struct file_operations aa_fs_profile_replace = {
	.write = profile_replace,
	.llseek = default_llseek,
};

/* .remove file hook fn to remove loaded policy */
static ssize_t profile_remove(struct file *f, const char __user *buf,
			      size_t size, loff_t *pos)
{
	struct aa_loaddata *data;
	struct aa_profile *profile;
	ssize_t error;
	struct aa_ns *ns = aa_get_ns(f->f_inode->i_private);

	profile = aa_current_profile();
	/* high level check about policy management - fine grained in
	 * below after unpack
	 */
	error = aa_may_manage_policy(profile, ns, OP_PROF_RM);
	if (error)
		goto out;

	/*
	 * aa_remove_profile needs a null terminated string so 1 extra
	 * byte is allocated and the copied data is null terminated.
	 */
	data = aa_simple_write_to_buffer(buf, size + 1, size, pos);

	error = PTR_ERR(data);
	if (!IS_ERR(data)) {
		data->data[size] = 0;
		error = aa_remove_profiles(ns ? ns : profile->ns, profile,
					   data->data, size);
		aa_put_loaddata(data);
	}
 out:
	aa_put_ns(ns);
	return error;
}

static const struct file_operations aa_fs_profile_remove = {
	.write = profile_remove,
	.llseek = default_llseek,
};

void __aa_bump_ns_revision(struct aa_ns *ns)
{
	ns->revision++;
}

/**
 * query_data - queries a policy and writes its data to buf
 * @buf: the resulting data is stored here (NOT NULL)
 * @buf_len: size of buf
 * @query: query string used to retrieve data
 * @query_len: size of query including second NUL byte
 *
 * The buffers pointed to by buf and query may overlap. The query buffer is
 * parsed before buf is written to.
 *
 * The query should look like "<LABEL>\0<KEY>\0", where <LABEL> is the name of
 * the security confinement context and <KEY> is the name of the data to
 * retrieve. <LABEL> and <KEY> must not be NUL-terminated.
 *
 * Don't expect the contents of buf to be preserved on failure.
 *
 * Returns: number of characters written to buf or -errno on failure
 */
static ssize_t query_data(char *buf, size_t buf_len,
			  char *query, size_t query_len)
{
	char *out;
	const char *key;
	struct aa_profile *profile;
	struct aa_data *data;
	u32 bytes, blocks;
	__le32 outle32;

	if (!query_len)
		return -EINVAL; /* need a query */

	key = query + strnlen(query, query_len) + 1;
	if (key + 1 >= query + query_len)
		return -EINVAL; /* not enough space for a non-empty key */
	if (key + strnlen(key, query + query_len - key) >= query + query_len)
		return -EINVAL; /* must end with NUL */

	if (buf_len < sizeof(bytes) + sizeof(blocks))
		return -EINVAL; /* not enough space */

	profile = aa_current_profile();

	/* We are going to leave space for two numbers. The first is the total
	 * number of bytes we are writing after the first number. This is so
	 * users can read the full output without reallocation.
	 *
	 * The second number is the number of data blocks we're writing. An
	 * application might be confined by multiple policies having data in
	 * the same key.
	 */
	memset(buf, 0, sizeof(bytes) + sizeof(blocks));
	out = buf + sizeof(bytes) + sizeof(blocks);

	blocks = 0;
	if (profile->data) {
		data = rhashtable_lookup_fast(profile->data, &key,
					      profile->data->p);

		if (data) {
			if (out + sizeof(outle32) + data->size > buf + buf_len)
				return -EINVAL; /* not enough space */
			outle32 = __cpu_to_le32(data->size);
			memcpy(out, &outle32, sizeof(outle32));
			out += sizeof(outle32);
			memcpy(out, data->data, data->size);
			out += data->size;
			blocks++;
		}
	}

	outle32 = __cpu_to_le32(out - buf - sizeof(bytes));
	memcpy(buf, &outle32, sizeof(outle32));
	outle32 = __cpu_to_le32(blocks);
	memcpy(buf + sizeof(bytes), &outle32, sizeof(outle32));

	return out - buf;
}

#define QUERY_CMD_DATA		"data\0"
#define QUERY_CMD_DATA_LEN	5

/**
 * aa_write_access - generic permissions and data query
 * @file: pointer to open apparmorfs/access file
 * @ubuf: user buffer containing the complete query string (NOT NULL)
 * @count: size of ubuf
 * @ppos: position in the file (MUST BE ZERO)
 *
 * Allows for one permissions or data query per open(), write(), and read()
 * sequence. The only queries currently supported are label-based queries for
 * permissions or data.
 *
 * For permissions queries, ubuf must begin with "label\0", followed by the
 * profile query specific format described in the query_label() function
 * documentation.
 *
 * For data queries, ubuf must have the form "data\0<LABEL>\0<KEY>\0", where
 * <LABEL> is the name of the security confinement context and <KEY> is the
 * name of the data to retrieve.
 *
 * Returns: number of bytes written or -errno on failure
 */
static ssize_t aa_write_access(struct file *file, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	char *buf;
	ssize_t len;

	if (*ppos)
		return -ESPIPE;

	buf = simple_transaction_get(file, ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	if (count > QUERY_CMD_DATA_LEN &&
		   !memcmp(buf, QUERY_CMD_DATA, QUERY_CMD_DATA_LEN)) {
		len = query_data(buf, SIMPLE_TRANSACTION_LIMIT,
				 buf + QUERY_CMD_DATA_LEN,
				 count - QUERY_CMD_DATA_LEN);
	} else
		len = -EINVAL;

	if (len < 0)
		return len;

	simple_transaction_set(file, len);

	return count;
}

static const struct file_operations aa_fs_access = {
	.write		= aa_write_access,
	.read		= simple_transaction_read,
	.release	= simple_transaction_release,
	.llseek		= generic_file_llseek,
};

static int aa_fs_seq_show(struct seq_file *seq, void *v)
{
	struct aa_fs_entry *fs_file = seq->private;

	if (!fs_file)
		return 0;

	switch (fs_file->v_type) {
	case AA_FS_TYPE_BOOLEAN:
		seq_printf(seq, "%s\n", fs_file->v.boolean ? "yes" : "no");
		break;
	case AA_FS_TYPE_STRING:
		seq_printf(seq, "%s\n", fs_file->v.string);
		break;
	case AA_FS_TYPE_U64:
		seq_printf(seq, "%#08lx\n", fs_file->v.u64);
		break;
	default:
		/* Ignore unpritable entry types. */
		break;
	}

	return 0;
}

static int aa_fs_seq_open(struct inode *inode, struct file *file)
{
	return single_open(file, aa_fs_seq_show, inode->i_private);
}

const struct file_operations aa_fs_seq_file_ops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/*
 * profile based file operations
 *     policy/profiles/XXXX/profiles/ *
 */

#define SEQ_PROFILE_FOPS(NAME)						      \
static int seq_profile_ ##NAME ##_open(struct inode *inode, struct file *file)\
{									      \
	return seq_profile_open(inode, file, seq_profile_ ##NAME ##_show);    \
}									      \
									      \
static const struct file_operations seq_profile_ ##NAME ##_fops = {	      \
	.owner		= THIS_MODULE,					      \
	.open		= seq_profile_ ##NAME ##_open,			      \
	.read		= seq_read,					      \
	.llseek		= seq_lseek,					      \
	.release	= seq_profile_release,				      \
}									      \

static int seq_profile_open(struct inode *inode, struct file *file,
			    int (*show)(struct seq_file *, void *))
{
	struct aa_proxy *proxy = aa_get_proxy(inode->i_private);
	int error = single_open(file, show, proxy);

	if (error) {
		file->private_data = NULL;
		aa_put_proxy(proxy);
	}

	return error;
}

static int seq_profile_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *) file->private_data;
	if (seq)
		aa_put_proxy(seq->private);
	return single_release(inode, file);
}

static int seq_profile_name_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	seq_printf(seq, "%s\n", profile->base.name);
	aa_put_profile(profile);

	return 0;
}

static int seq_profile_mode_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	seq_printf(seq, "%s\n", aa_profile_mode_names[profile->mode]);
	aa_put_profile(profile);

	return 0;
}

static int seq_profile_attach_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	if (profile->attach)
		seq_printf(seq, "%s\n", profile->attach);
	else if (profile->xmatch)
		seq_puts(seq, "<unknown>\n");
	else
		seq_printf(seq, "%s\n", profile->base.name);
	aa_put_profile(profile);

	return 0;
}

static int seq_profile_hash_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	unsigned int i, size = aa_hash_size();

	if (profile->hash) {
		for (i = 0; i < size; i++)
			seq_printf(seq, "%.2x", profile->hash[i]);
		seq_putc(seq, '\n');
	}
	aa_put_profile(profile);

	return 0;
}

SEQ_PROFILE_FOPS(name);
SEQ_PROFILE_FOPS(mode);
SEQ_PROFILE_FOPS(attach);
SEQ_PROFILE_FOPS(hash);

/*
 * namespace based files
 *     several root files and
 *     policy/ *
 */

#define SEQ_NS_FOPS(NAME)						      \
static int seq_ns_ ##NAME ##_open(struct inode *inode, struct file *file)     \
{									      \
	return single_open(file, seq_ns_ ##NAME ##_show, inode->i_private);   \
}									      \
									      \
static const struct file_operations seq_ns_ ##NAME ##_fops = {	      \
	.owner		= THIS_MODULE,					      \
	.open		= seq_ns_ ##NAME ##_open,			      \
	.read		= seq_read,					      \
	.llseek		= seq_lseek,					      \
	.release	= single_release,				      \
}									      \

static int seq_ns_level_show(struct seq_file *seq, void *v)
{
	struct aa_ns *ns = aa_current_profile()->ns;

	seq_printf(seq, "%d\n", ns->level);

	return 0;
}

static int seq_ns_name_show(struct seq_file *seq, void *v)
{
	struct aa_ns *ns = aa_current_profile()->ns;

	seq_printf(seq, "%s\n", ns->base.name);

	return 0;
}

SEQ_NS_FOPS(level);
SEQ_NS_FOPS(name);


/* policy/raw_data/ * file ops */

#define SEQ_RAWDATA_FOPS(NAME)						      \
static int seq_rawdata_ ##NAME ##_open(struct inode *inode, struct file *file)\
{									      \
	return seq_rawdata_open(inode, file, seq_rawdata_ ##NAME ##_show);    \
}									      \
									      \
static const struct file_operations seq_rawdata_ ##NAME ##_fops = {	      \
	.owner		= THIS_MODULE,					      \
	.open		= seq_rawdata_ ##NAME ##_open,			      \
	.read		= seq_read,					      \
	.llseek		= seq_lseek,					      \
	.release	= seq_rawdata_release,				      \
}									      \

static int seq_rawdata_open(struct inode *inode, struct file *file,
			    int (*show)(struct seq_file *, void *))
{
	struct aa_loaddata *data = __aa_get_loaddata(inode->i_private);
	int error;

	if (!data)
		/* lost race this ent is being reaped */
		return -ENOENT;

	error = single_open(file, show, data);
	if (error) {
		AA_BUG(file->private_data &&
		       ((struct seq_file *)file->private_data)->private);
		aa_put_loaddata(data);
	}

	return error;
}

static int seq_rawdata_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *) file->private_data;

	if (seq)
		aa_put_loaddata(seq->private);

	return single_release(inode, file);
}

static int seq_rawdata_abi_show(struct seq_file *seq, void *v)
{
	struct aa_loaddata *data = seq->private;

	seq_printf(seq, "v%d\n", data->abi);

	return 0;
}

static int seq_rawdata_revision_show(struct seq_file *seq, void *v)
{
	struct aa_loaddata *data = seq->private;

	seq_printf(seq, "%ld\n", data->revision);

	return 0;
}

static int seq_rawdata_hash_show(struct seq_file *seq, void *v)
{
	struct aa_loaddata *data = seq->private;
	unsigned int i, size = aa_hash_size();

	if (data->hash) {
		for (i = 0; i < size; i++)
			seq_printf(seq, "%.2x", data->hash[i]);
		seq_putc(seq, '\n');
	}

	return 0;
}

SEQ_RAWDATA_FOPS(abi);
SEQ_RAWDATA_FOPS(revision);
SEQ_RAWDATA_FOPS(hash);

static ssize_t rawdata_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	struct aa_loaddata *rawdata = file->private_data;

	return simple_read_from_buffer(buf, size, ppos, rawdata->data,
				       rawdata->size);
}

static int rawdata_release(struct inode *inode, struct file *file)
{
	aa_put_loaddata(file->private_data);

	return 0;
}

static int rawdata_open(struct inode *inode, struct file *file)
{
	if (!policy_view_capable(NULL))
		return -EACCES;
	file->private_data = __aa_get_loaddata(inode->i_private);
	if (!file->private_data)
		/* lost race: this entry is being reaped */
		return -ENOENT;

	return 0;
}

static const struct file_operations rawdata_fops = {
	.open = rawdata_open,
	.read = rawdata_read,
	.llseek = generic_file_llseek,
	.release = rawdata_release,
};

static void remove_rawdata_dents(struct aa_loaddata *rawdata)
{
	int i;

	for (i = 0; i < AAFS_LOADDATA_NDENTS; i++) {
		if (!IS_ERR_OR_NULL(rawdata->dents[i])) {
			/* no refcounts on i_private */
			securityfs_remove(rawdata->dents[i]);
			rawdata->dents[i] = NULL;
		}
	}
}

void __aa_fs_remove_rawdata(struct aa_loaddata *rawdata)
{
	AA_BUG(rawdata->ns && !mutex_is_locked(&rawdata->ns->lock));

	if (rawdata->ns) {
		remove_rawdata_dents(rawdata);
		list_del_init(&rawdata->list);
		aa_put_ns(rawdata->ns);
		rawdata->ns = NULL;
	}
}

int __aa_fs_create_rawdata(struct aa_ns *ns, struct aa_loaddata *rawdata)
{
	struct dentry *dent, *dir;

	AA_BUG(!ns);
	AA_BUG(!rawdata);
	AA_BUG(!mutex_is_locked(&ns->lock));
	AA_BUG(!ns_subdata_dir(ns));

	/*
	 * just use ns revision dir was originally created at. This is
	 * under ns->lock and if load is successful revision will be
	 * bumped and is guaranteed to be unique
	 */
	rawdata->name = kasprintf(GFP_KERNEL, "%ld", ns->revision);
	if (!rawdata->name)
		return -ENOMEM;

	dir = securityfs_create_dir(rawdata->name, ns_subdata_dir(ns));
	if (IS_ERR(dir))
		/* ->name freed when rawdata freed */
		return PTR_ERR(dir);
	rawdata->dents[AAFS_LOADDATA_DIR] = dir;

	dent = securityfs_create_file("abi", S_IFREG | 0444, dir, rawdata,
				      &seq_rawdata_abi_fops);
	if (IS_ERR(dent))
		goto fail;
	rawdata->dents[AAFS_LOADDATA_ABI] = dent;

	dent = securityfs_create_file("revision", S_IFREG | 0444, dir, rawdata,
				      &seq_rawdata_revision_fops);
	if (IS_ERR(dent))
		goto fail;
	rawdata->dents[AAFS_LOADDATA_REVISION] = dent;

	if (aa_g_hash_policy) {
		dent = securityfs_create_file("sha1", S_IFREG | 0444, dir,
					      rawdata, &seq_rawdata_hash_fops);
		if (IS_ERR(dent))
			goto fail;
		rawdata->dents[AAFS_LOADDATA_HASH] = dent;
	}

	dent = securityfs_create_file("raw_data", S_IFREG | 0444,
				      dir, rawdata, &rawdata_fops);
	if (IS_ERR(dent))
		goto fail;
	rawdata->dents[AAFS_LOADDATA_DATA] = dent;
	d_inode(dent)->i_size = rawdata->size;

	rawdata->ns = aa_get_ns(ns);
	list_add(&rawdata->list, &ns->rawdata_list);
	/* no refcount on inode rawdata */

	return 0;

fail:
	remove_rawdata_dents(rawdata);

	return PTR_ERR(dent);
}

/** fns to setup dynamic per profile/namespace files **/
void __aa_fs_profile_rmdir(struct aa_profile *profile)
{
	struct aa_profile *child;
	int i;

	if (!profile)
		return;

	list_for_each_entry(child, &profile->base.profiles, base.list)
		__aa_fs_profile_rmdir(child);

	for (i = AAFS_PROF_SIZEOF - 1; i >= 0; --i) {
		struct aa_proxy *proxy;
		if (!profile->dents[i])
			continue;

		proxy = d_inode(profile->dents[i])->i_private;
		securityfs_remove(profile->dents[i]);
		aa_put_proxy(proxy);
		profile->dents[i] = NULL;
	}
}

void __aa_fs_profile_migrate_dents(struct aa_profile *old,
				   struct aa_profile *new)
{
	int i;

	for (i = 0; i < AAFS_PROF_SIZEOF; i++) {
		new->dents[i] = old->dents[i];
		if (new->dents[i])
			new->dents[i]->d_inode->i_mtime = current_time(new->dents[i]->d_inode);
		old->dents[i] = NULL;
	}
}

static struct dentry *create_profile_file(struct dentry *dir, const char *name,
					  struct aa_profile *profile,
					  const struct file_operations *fops)
{
	struct aa_proxy *proxy = aa_get_proxy(profile->proxy);
	struct dentry *dent;

	dent = securityfs_create_file(name, S_IFREG | 0444, dir, proxy, fops);
	if (IS_ERR(dent))
		aa_put_proxy(proxy);

	return dent;
}

static int profile_depth(struct aa_profile *profile)
{
	int depth = 0;

	rcu_read_lock();
	for (depth = 0; profile; profile = rcu_access_pointer(profile->parent))
		depth++;
	rcu_read_unlock();

	return depth;
}

static int gen_symlink_name(char *buffer, size_t bsize, int depth,
			    const char *dirname, const char *fname)
{
	int error;

	for (; depth > 0; depth--) {
		if (bsize < 7)
			return -ENAMETOOLONG;
		strcpy(buffer, "../../");
		buffer += 6;
		bsize -= 6;
	}

	error = snprintf(buffer, bsize, "raw_data/%s/%s", dirname, fname);
	if (error >= bsize || error < 0)
		return -ENAMETOOLONG;

	return 0;
}

/*
 * Requires: @profile->ns->lock held
 */
int __aa_fs_profile_mkdir(struct aa_profile *profile, struct dentry *parent)
{
	struct aa_profile *child;
	struct dentry *dent = NULL, *dir;
	int error;

	if (!parent) {
		struct aa_profile *p;
		p = aa_deref_parent(profile);
		dent = prof_dir(p);
		/* adding to parent that previously didn't have children */
		dent = securityfs_create_dir("profiles", dent);
		if (IS_ERR(dent))
			goto fail;
		prof_child_dir(p) = parent = dent;
	}

	if (!profile->dirname) {
		int len, id_len;
		len = mangle_name(profile->base.name, NULL);
		id_len = snprintf(NULL, 0, ".%ld", profile->ns->uniq_id);

		profile->dirname = kmalloc(len + id_len + 1, GFP_KERNEL);
		if (!profile->dirname) {
			error = -ENOMEM;
			goto fail2;
		}

		mangle_name(profile->base.name, profile->dirname);
		sprintf(profile->dirname + len, ".%ld", profile->ns->uniq_id++);
	}

	dent = securityfs_create_dir(profile->dirname, parent);
	if (IS_ERR(dent))
		goto fail;
	prof_dir(profile) = dir = dent;

	dent = create_profile_file(dir, "name", profile,
				   &seq_profile_name_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_NAME] = dent;

	dent = create_profile_file(dir, "mode", profile,
				   &seq_profile_mode_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_MODE] = dent;

	dent = create_profile_file(dir, "attach", profile,
				   &seq_profile_attach_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_ATTACH] = dent;

	if (profile->hash) {
		dent = create_profile_file(dir, "sha1", profile,
					   &seq_profile_hash_fops);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_HASH] = dent;
	}

	if (profile->rawdata) {
		char target[64];
		int depth = profile_depth(profile);

		error = gen_symlink_name(target, sizeof(target), depth,
					 profile->rawdata->name, "sha1");
		if (error < 0)
			goto fail2;
		dent = securityfs_create_symlink("raw_sha1", dir, target, NULL);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_HASH] = dent;

		error = gen_symlink_name(target, sizeof(target), depth,
					 profile->rawdata->name, "abi");
		if (error < 0)
			goto fail2;
		dent = securityfs_create_symlink("raw_abi", dir, target, NULL);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_ABI] = dent;

		error = gen_symlink_name(target, sizeof(target), depth,
					 profile->rawdata->name, "raw_data");
		if (error < 0)
			goto fail2;
		dent = securityfs_create_symlink("raw_data", dir, target, NULL);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_DATA] = dent;
	}

	list_for_each_entry(child, &profile->base.profiles, base.list) {
		error = __aa_fs_profile_mkdir(child, prof_child_dir(profile));
		if (error)
			goto fail2;
	}

	return 0;

fail:
	error = PTR_ERR(dent);

fail2:
	__aa_fs_profile_rmdir(profile);

	return error;
}

static void __aa_fs_list_remove_rawdata(struct aa_ns *ns)
{
	struct aa_loaddata *ent, *tmp;

	AA_BUG(!mutex_is_locked(&ns->lock));

	list_for_each_entry_safe(ent, tmp, &ns->rawdata_list, list)
		__aa_fs_remove_rawdata(ent);
}

void __aa_fs_ns_rmdir(struct aa_ns *ns)
{
	struct aa_ns *sub;
	struct aa_profile *child;
	int i;

	if (!ns)
		return;

	list_for_each_entry(child, &ns->base.profiles, base.list)
		__aa_fs_profile_rmdir(child);

	list_for_each_entry(sub, &ns->sub_ns, base.list) {
		mutex_lock(&sub->lock);
		__aa_fs_ns_rmdir(sub);
		mutex_unlock(&sub->lock);
	}

	__aa_fs_list_remove_rawdata(ns);

	if (ns_subns_dir(ns)) {
		sub = d_inode(ns_subns_dir(ns))->i_private;
		aa_put_ns(sub);
	}
	if (ns_subload(ns)) {
		sub = d_inode(ns_subload(ns))->i_private;
		aa_put_ns(sub);
	}
	if (ns_subreplace(ns)) {
		sub = d_inode(ns_subreplace(ns))->i_private;
		aa_put_ns(sub);
	}
	if (ns_subremove(ns)) {
		sub = d_inode(ns_subremove(ns))->i_private;
		aa_put_ns(sub);
	}

	for (i = AAFS_NS_SIZEOF - 1; i >= 0; --i) {
		securityfs_remove(ns->dents[i]);
		ns->dents[i] = NULL;
	}
}

/* assumes cleanup in caller */
static int __aa_fs_ns_mkdir_entries(struct aa_ns *ns, struct dentry *dir)
{
	struct dentry *dent;

	AA_BUG(!ns);
	AA_BUG(!dir);

	dent = securityfs_create_dir("profiles", dir);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	ns_subprofs_dir(ns) = dent;

	dent = securityfs_create_dir("raw_data", dir);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	ns_subdata_dir(ns) = dent;

	dent = securityfs_create_file(".load", 0640, dir, ns,
				      &aa_fs_profile_load);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	aa_get_ns(ns);
	ns_subload(ns) = dent;

	dent = securityfs_create_file(".replace", 0640, dir, ns,
				      &aa_fs_profile_replace);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	aa_get_ns(ns);
	ns_subreplace(ns) = dent;

	dent = securityfs_create_file(".remove", 0640, dir, ns,
				      &aa_fs_profile_remove);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	aa_get_ns(ns);
	ns_subremove(ns) = dent;

	dent = securityfs_create_dir("namespaces", dir);
	if (IS_ERR(dent))
		return PTR_ERR(dent);
	aa_get_ns(ns);
	ns_subns_dir(ns) = dent;

	return 0;
}

int __aa_fs_ns_mkdir(struct aa_ns *ns, struct dentry *parent, const char *name)
{
	struct aa_ns *sub;
	struct aa_profile *child;
	struct dentry *dent, *dir;
	int error;

	AA_BUG(!ns);
	AA_BUG(!parent);
	AA_BUG(!mutex_is_locked(&ns->lock));

	if (!name)
		name = ns->base.name;

	/* create ns dir if it doesn't already exist */
	dent = securityfs_create_dir(name, parent);
	if (IS_ERR(dent))
		goto fail;

	ns_dir(ns) = dir = dent;
	error = __aa_fs_ns_mkdir_entries(ns, dir);
	if (error)
		goto fail2;

	/* profiles */
	list_for_each_entry(child, &ns->base.profiles, base.list) {
		error = __aa_fs_profile_mkdir(child, ns_subprofs_dir(ns));
		if (error)
			goto fail2;
	}

	/* subnamespaces */
	list_for_each_entry(sub, &ns->sub_ns, base.list) {
		mutex_lock(&sub->lock);
		error = __aa_fs_ns_mkdir(sub, ns_subns_dir(ns), NULL);
		mutex_unlock(&sub->lock);
		if (error)
			goto fail2;
	}

	return 0;

fail:
	error = PTR_ERR(dent);

fail2:
	__aa_fs_ns_rmdir(ns);

	return error;
}


#define list_entry_is_head(pos, head, member) (&pos->member == (head))

/**
 * __next_ns - find the next namespace to list
 * @root: root namespace to stop search at (NOT NULL)
 * @ns: current ns position (NOT NULL)
 *
 * Find the next namespace from @ns under @root and handle all locking needed
 * while switching current namespace.
 *
 * Returns: next namespace or NULL if at last namespace under @root
 * Requires: ns->parent->lock to be held
 * NOTE: will not unlock root->lock
 */
static struct aa_ns *__next_ns(struct aa_ns *root, struct aa_ns *ns)
{
	struct aa_ns *parent, *next;

	/* is next namespace a child */
	if (!list_empty(&ns->sub_ns)) {
		next = list_first_entry(&ns->sub_ns, typeof(*ns), base.list);
		mutex_lock(&next->lock);
		return next;
	}

	/* check if the next ns is a sibling, parent, gp, .. */
	parent = ns->parent;
	while (ns != root) {
		mutex_unlock(&ns->lock);
		next = list_next_entry(ns, base.list);
		if (!list_entry_is_head(next, &parent->sub_ns, base.list)) {
			mutex_lock(&next->lock);
			return next;
		}
		ns = parent;
		parent = parent->parent;
	}

	return NULL;
}

/**
 * __first_profile - find the first profile in a namespace
 * @root: namespace that is root of profiles being displayed (NOT NULL)
 * @ns: namespace to start in   (NOT NULL)
 *
 * Returns: unrefcounted profile or NULL if no profile
 * Requires: profile->ns.lock to be held
 */
static struct aa_profile *__first_profile(struct aa_ns *root,
					  struct aa_ns *ns)
{
	for (; ns; ns = __next_ns(root, ns)) {
		if (!list_empty(&ns->base.profiles))
			return list_first_entry(&ns->base.profiles,
						struct aa_profile, base.list);
	}
	return NULL;
}

/**
 * __next_profile - step to the next profile in a profile tree
 * @profile: current profile in tree (NOT NULL)
 *
 * Perform a depth first traversal on the profile tree in a namespace
 *
 * Returns: next profile or NULL if done
 * Requires: profile->ns.lock to be held
 */
static struct aa_profile *__next_profile(struct aa_profile *p)
{
	struct aa_profile *parent;
	struct aa_ns *ns = p->ns;

	/* is next profile a child */
	if (!list_empty(&p->base.profiles))
		return list_first_entry(&p->base.profiles, typeof(*p),
					base.list);

	/* is next profile a sibling, parent sibling, gp, sibling, .. */
	parent = rcu_dereference_protected(p->parent,
					   mutex_is_locked(&p->ns->lock));
	while (parent) {
		p = list_next_entry(p, base.list);
		if (!list_entry_is_head(p, &parent->base.profiles, base.list))
			return p;
		p = parent;
		parent = rcu_dereference_protected(parent->parent,
					    mutex_is_locked(&parent->ns->lock));
	}

	/* is next another profile in the namespace */
	p = list_next_entry(p, base.list);
	if (!list_entry_is_head(p, &ns->base.profiles, base.list))
		return p;

	return NULL;
}

/**
 * next_profile - step to the next profile in where ever it may be
 * @root: root namespace  (NOT NULL)
 * @profile: current profile  (NOT NULL)
 *
 * Returns: next profile or NULL if there isn't one
 */
static struct aa_profile *next_profile(struct aa_ns *root,
				       struct aa_profile *profile)
{
	struct aa_profile *next = __next_profile(profile);
	if (next)
		return next;

	/* finished all profiles in namespace move to next namespace */
	return __first_profile(root, __next_ns(root, profile->ns));
}

/**
 * p_start - start a depth first traversal of profile tree
 * @f: seq_file to fill
 * @pos: current position
 *
 * Returns: first profile under current namespace or NULL if none found
 *
 * acquires first ns->lock
 */
static void *p_start(struct seq_file *f, loff_t *pos)
{
	struct aa_profile *profile = NULL;
	struct aa_ns *root = aa_current_profile()->ns;
	loff_t l = *pos;
	f->private = aa_get_ns(root);


	/* find the first profile */
	mutex_lock(&root->lock);
	profile = __first_profile(root, root);

	/* skip to position */
	for (; profile && l > 0; l--)
		profile = next_profile(root, profile);

	return profile;
}

/**
 * p_next - read the next profile entry
 * @f: seq_file to fill
 * @p: profile previously returned
 * @pos: current position
 *
 * Returns: next profile after @p or NULL if none
 *
 * may acquire/release locks in namespace tree as necessary
 */
static void *p_next(struct seq_file *f, void *p, loff_t *pos)
{
	struct aa_profile *profile = p;
	struct aa_ns *ns = f->private;
	(*pos)++;

	return next_profile(ns, profile);
}

/**
 * p_stop - stop depth first traversal
 * @f: seq_file we are filling
 * @p: the last profile writen
 *
 * Release all locking done by p_start/p_next on namespace tree
 */
static void p_stop(struct seq_file *f, void *p)
{
	struct aa_profile *profile = p;
	struct aa_ns *root = f->private, *ns;

	if (profile) {
		for (ns = profile->ns; ns && ns != root; ns = ns->parent)
			mutex_unlock(&ns->lock);
	}
	mutex_unlock(&root->lock);
	aa_put_ns(root);
}

/**
 * seq_show_profile - show a profile entry
 * @f: seq_file to file
 * @p: current position (profile)    (NOT NULL)
 *
 * Returns: error on failure
 */
static int seq_show_profile(struct seq_file *f, void *p)
{
	struct aa_profile *profile = (struct aa_profile *)p;
	struct aa_ns *root = f->private;

	if (profile->ns != root)
		seq_printf(f, ":%s://", aa_ns_name(root, profile->ns, true));
	seq_printf(f, "%s (%s)\n", profile->base.hname,
		   aa_profile_mode_names[profile->mode]);

	return 0;
}

static const struct seq_operations aa_fs_profiles_op = {
	.start = p_start,
	.next = p_next,
	.stop = p_stop,
	.show = seq_show_profile,
};

static int profiles_open(struct inode *inode, struct file *file)
{
	if (!policy_view_capable(NULL))
		return -EACCES;

	return seq_open(file, &aa_fs_profiles_op);
}

static int profiles_release(struct inode *inode, struct file *file)
{
	return seq_release(inode, file);
}

static const struct file_operations aa_fs_profiles_fops = {
	.open = profiles_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = profiles_release,
};


/** Base file system setup **/
static struct aa_fs_entry aa_fs_entry_file[] = {
	AA_FS_FILE_STRING("mask", "create read write exec append mmap_exec " \
				  "link lock"),
	{ }
};

static struct aa_fs_entry aa_fs_entry_domain[] = {
	AA_FS_FILE_BOOLEAN("change_hat",	1),
	AA_FS_FILE_BOOLEAN("change_hatv",	1),
	AA_FS_FILE_BOOLEAN("change_onexec",	1),
	AA_FS_FILE_BOOLEAN("change_profile",	1),
	AA_FS_FILE_BOOLEAN("fix_binfmt_elf_mmap",	1),
	AA_FS_FILE_STRING("version", "1.2"),
	{ }
};

static struct aa_fs_entry aa_fs_entry_versions[] = {
	AA_FS_FILE_BOOLEAN("v5",	1),
	{ }
};

static struct aa_fs_entry aa_fs_entry_policy[] = {
	AA_FS_DIR("versions",                   aa_fs_entry_versions),
	AA_FS_FILE_BOOLEAN("set_load",		1),
	{ }
};

static struct aa_fs_entry aa_fs_entry_features[] = {
	AA_FS_DIR("policy",			aa_fs_entry_policy),
	AA_FS_DIR("domain",			aa_fs_entry_domain),
	AA_FS_DIR("file",			aa_fs_entry_file),
	AA_FS_FILE_U64("capability",		VFS_CAP_FLAGS_MASK),
	AA_FS_DIR("rlimit",			aa_fs_entry_rlimit),
	AA_FS_DIR("caps",			aa_fs_entry_caps),
	{ }
};

static struct aa_fs_entry aa_fs_entry_apparmor[] = {
	AA_FS_FILE_FOPS(".access", 0640, &aa_fs_access),
	AA_FS_FILE_FOPS(".ns_level", 0666, &seq_ns_level_fops),
	AA_FS_FILE_FOPS(".ns_name", 0640, &seq_ns_name_fops),
	AA_FS_FILE_FOPS("profiles", 0440, &aa_fs_profiles_fops),
	AA_FS_DIR("features", aa_fs_entry_features),
	{ }
};

static struct aa_fs_entry aa_fs_entry =
	AA_FS_DIR("apparmor", aa_fs_entry_apparmor);

/**
 * entry_create_file - create a file entry in the apparmor securityfs
 * @fs_file: aa_fs_entry to build an entry for (NOT NULL)
 * @parent: the parent dentry in the securityfs
 *
 * Use entry_remove_file to remove entries created with this fn.
 */
static int __init entry_create_file(struct aa_fs_entry *fs_file,
				    struct dentry *parent)
{
	int error = 0;

	fs_file->dentry = securityfs_create_file(fs_file->name,
						 S_IFREG | fs_file->mode,
						 parent, fs_file,
						 fs_file->file_ops);
	if (IS_ERR(fs_file->dentry)) {
		error = PTR_ERR(fs_file->dentry);
		fs_file->dentry = NULL;
	}
	return error;
}

static void __init entry_remove_dir(struct aa_fs_entry *fs_dir);
/**
 * entry_create_dir - recursively create a directory entry in the securityfs
 * @fs_dir: aa_fs_entry (and all child entries) to build (NOT NULL)
 * @parent: the parent dentry in the securityfs
 *
 * Use entry_remove_dir to remove entries created with this fn.
 */
static int __init entry_create_dir(struct aa_fs_entry *fs_dir,
				  struct dentry *parent)
{
	struct aa_fs_entry *fs_file;
	struct dentry *dir;
	int error;

	dir = securityfs_create_dir(fs_dir->name, parent);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	fs_dir->dentry = dir;

	for (fs_file = fs_dir->v.files; fs_file && fs_file->name; ++fs_file) {
		if (fs_file->v_type == AA_FS_TYPE_DIR)
			error = entry_create_dir(fs_file, fs_dir->dentry);
		else
			error = entry_create_file(fs_file, fs_dir->dentry);
		if (error)
			goto failed;
	}

	return 0;

failed:
	entry_remove_dir(fs_dir);

	return error;
}

/**
 * aafs_remove_file - drop a single file entry in the apparmor securityfs
 * @fs_file: aa_fs_entry to detach from the securityfs (NOT NULL)
 */
static void __init aafs_remove_file(struct aa_fs_entry *fs_file)
{
	if (!fs_file->dentry)
		return;

	securityfs_remove(fs_file->dentry);
	fs_file->dentry = NULL;
}

/**
 * entry_remove_dir - recursively drop a directory entry from the securityfs
 * @fs_dir: aa_fs_entry (and all child entries) to detach (NOT NULL)
 */
static void __init entry_remove_dir(struct aa_fs_entry *fs_dir)
{
	struct aa_fs_entry *fs_file;

	for (fs_file = fs_dir->v.files; fs_file && fs_file->name; ++fs_file) {
		if (fs_file->v_type == AA_FS_TYPE_DIR)
			entry_remove_dir(fs_file);
		else
			aafs_remove_file(fs_file);
	}

	aafs_remove_file(fs_dir);
}

/**
 * aa_destroy_aafs - cleanup and free aafs
 *
 * releases dentries allocated by aa_create_aafs
 */
void __init aa_destroy_aafs(void)
{
	entry_remove_dir(&aa_fs_entry);
}


#define NULL_FILE_NAME ".null"
struct path aa_null;

static int aa_mk_null_file(struct dentry *parent)
{
	struct vfsmount *mount = NULL;
	struct dentry *dentry;
	struct inode *inode;
	int count = 0;
	int error = simple_pin_fs(parent->d_sb->s_type, &mount, &count);

	if (error)
		return error;

	inode_lock(d_inode(parent));
	dentry = lookup_one_len(NULL_FILE_NAME, parent, strlen(NULL_FILE_NAME));
	if (IS_ERR(dentry)) {
		error = PTR_ERR(dentry);
		goto out;
	}
	inode = new_inode(parent->d_inode->i_sb);
	if (!inode) {
		error = -ENOMEM;
		goto out1;
	}

	inode->i_ino = get_next_ino();
	inode->i_mode = S_IFCHR | S_IRUGO | S_IWUGO;
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	init_special_inode(inode, S_IFCHR | S_IRUGO | S_IWUGO,
			   MKDEV(MEM_MAJOR, 3));
	d_instantiate(dentry, inode);
	aa_null.dentry = dget(dentry);
	aa_null.mnt = mntget(mount);

	error = 0;

out1:
	dput(dentry);
out:
	inode_unlock(d_inode(parent));
	simple_release_fs(&mount, &count);
	return error;
}



static const char *policy_get_link(struct dentry *dentry,
				   struct inode *inode,
				   struct delayed_call *done)
{
	struct aa_ns *ns;
	struct path path;

	if (!dentry)
		return ERR_PTR(-ECHILD);
	ns = aa_get_current_ns();
	path.mnt = mntget(aafs_mnt);
	path.dentry = dget(ns_dir(ns));
	nd_jump_link(&path);
	aa_put_ns(ns);

	return NULL;
}

static int ns_get_name(char *buf, size_t size, struct aa_ns *ns,
		       struct inode *inode)
{
	int res = snprintf(buf, size, "%s:[%lu]", AAFS_NAME, inode->i_ino);

	if (res < 0 || res >= size)
		res = -ENOENT;

	return res;
}

static int policy_readlink(struct dentry *dentry, char __user *buffer,
			   int buflen)
{
	struct aa_ns *ns;
	char name[32];
	int res;

	ns = aa_get_current_ns();
	res = ns_get_name(name, sizeof(name), ns, d_inode(dentry));
	if (res >= 0)
		res = readlink_copy(buffer, buflen, name);
	aa_put_ns(ns);

	return res;
}

static const struct inode_operations policy_link_iops = {
	.readlink	= policy_readlink,
	.get_link	= policy_get_link,
};


/**
 * aa_create_aafs - create the apparmor security filesystem
 *
 * dentries created here are released by aa_destroy_aafs
 *
 * Returns: error on failure
 */
static int __init aa_create_aafs(void)
{
	struct dentry *dent;
	int error;

	if (!apparmor_initialized)
		return 0;

	if (aa_fs_entry.dentry) {
		AA_ERROR("%s: AppArmor securityfs already exists\n", __func__);
		return -EEXIST;
	}

	/* setup apparmorfs used to virtualize policy/ */
	aafs_mnt = kern_mount(&aafs_ops);
	if (IS_ERR(aafs_mnt))
		panic("can't set apparmorfs up\n");
	aafs_mnt->mnt_sb->s_flags &= ~MS_NOUSER;

	/* Populate fs tree. */
	error = entry_create_dir(&aa_fs_entry, NULL);
	if (error)
		goto error;

	dent = securityfs_create_file(".load", 0666, aa_fs_entry.dentry,
				      NULL, &aa_fs_profile_load);
	if (IS_ERR(dent)) {
		error = PTR_ERR(dent);
		goto error;
	}
	ns_subload(root_ns) = dent;

	dent = securityfs_create_file(".replace", 0666, aa_fs_entry.dentry,
				      NULL, &aa_fs_profile_replace);
	if (IS_ERR(dent)) {
		error = PTR_ERR(dent);
		goto error;
	}
	ns_subreplace(root_ns) = dent;

	dent = securityfs_create_file(".remove", 0666, aa_fs_entry.dentry,
				      NULL, &aa_fs_profile_remove);
	if (IS_ERR(dent)) {
		error = PTR_ERR(dent);
		goto error;
	}
	ns_subremove(root_ns) = dent;

	mutex_lock(&root_ns->lock);
	error = __aa_fs_ns_mkdir(root_ns, aa_fs_entry.dentry, "policy");
	mutex_unlock(&root_ns->lock);

	if (error)
		goto error;

	error = aa_mk_null_file(aa_fs_entry.dentry);
	if (error)
		goto error;

	/* TODO: add default profile to apparmorfs */

	/* Report that AppArmor fs is enabled */
	aa_info_message("AppArmor Filesystem Enabled");
	return 0;

error:
	aa_destroy_aafs();
	AA_ERROR("Error creating AppArmor securityfs\n");
	return error;
}

fs_initcall(aa_create_aafs);
