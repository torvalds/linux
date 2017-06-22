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
#include <uapi/linux/major.h>
#include <linux/fs.h>

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
	data = kvmalloc(sizeof(*data) + alloc_size);
	if (data == NULL)
		return ERR_PTR(-ENOMEM);
	kref_init(&data->count);
	data->size = copy_size;
	data->hash = NULL;
	data->abi = 0;

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

static int aa_fs_seq_profile_open(struct inode *inode, struct file *file,
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

static int aa_fs_seq_profile_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = (struct seq_file *) file->private_data;
	if (seq)
		aa_put_proxy(seq->private);
	return single_release(inode, file);
}

static int aa_fs_seq_profname_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	seq_printf(seq, "%s\n", profile->base.name);
	aa_put_profile(profile);

	return 0;
}

static int aa_fs_seq_profname_open(struct inode *inode, struct file *file)
{
	return aa_fs_seq_profile_open(inode, file, aa_fs_seq_profname_show);
}

static const struct file_operations aa_fs_profname_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_profname_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= aa_fs_seq_profile_release,
};

static int aa_fs_seq_profmode_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	seq_printf(seq, "%s\n", aa_profile_mode_names[profile->mode]);
	aa_put_profile(profile);

	return 0;
}

static int aa_fs_seq_profmode_open(struct inode *inode, struct file *file)
{
	return aa_fs_seq_profile_open(inode, file, aa_fs_seq_profmode_show);
}

static const struct file_operations aa_fs_profmode_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_profmode_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= aa_fs_seq_profile_release,
};

static int aa_fs_seq_profattach_show(struct seq_file *seq, void *v)
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

static int aa_fs_seq_profattach_open(struct inode *inode, struct file *file)
{
	return aa_fs_seq_profile_open(inode, file, aa_fs_seq_profattach_show);
}

static const struct file_operations aa_fs_profattach_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_profattach_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= aa_fs_seq_profile_release,
};

static int aa_fs_seq_hash_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	unsigned int i, size = aa_hash_size();

	if (profile->hash) {
		for (i = 0; i < size; i++)
			seq_printf(seq, "%.2x", profile->hash[i]);
		seq_puts(seq, "\n");
	}
	aa_put_profile(profile);

	return 0;
}

static int aa_fs_seq_hash_open(struct inode *inode, struct file *file)
{
	return single_open(file, aa_fs_seq_hash_show, inode->i_private);
}

static const struct file_operations aa_fs_seq_hash_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_hash_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};


static int aa_fs_seq_show_ns_level(struct seq_file *seq, void *v)
{
	struct aa_ns *ns = aa_current_profile()->ns;

	seq_printf(seq, "%d\n", ns->level);

	return 0;
}

static int aa_fs_seq_open_ns_level(struct inode *inode, struct file *file)
{
	return single_open(file, aa_fs_seq_show_ns_level, inode->i_private);
}

static const struct file_operations aa_fs_ns_level = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_open_ns_level,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int aa_fs_seq_show_ns_name(struct seq_file *seq, void *v)
{
	struct aa_ns *ns = aa_current_profile()->ns;

	seq_printf(seq, "%s\n", ns->base.name);

	return 0;
}

static int aa_fs_seq_open_ns_name(struct inode *inode, struct file *file)
{
	return single_open(file, aa_fs_seq_show_ns_name, inode->i_private);
}

static const struct file_operations aa_fs_ns_name = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_open_ns_name,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rawdata_release(struct inode *inode, struct file *file)
{
	/* TODO: switch to loaddata when profile switched to symlink */
	aa_put_loaddata(file->private_data);

	return 0;
}

static int aa_fs_seq_raw_abi_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);

	if (profile->rawdata->abi) {
		seq_printf(seq, "v%d", profile->rawdata->abi);
		seq_puts(seq, "\n");
	}
	aa_put_profile(profile);

	return 0;
}

static int aa_fs_seq_raw_abi_open(struct inode *inode, struct file *file)
{
	return aa_fs_seq_profile_open(inode, file, aa_fs_seq_raw_abi_show);
}

static const struct file_operations aa_fs_seq_raw_abi_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_raw_abi_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= aa_fs_seq_profile_release,
};

static int aa_fs_seq_raw_hash_show(struct seq_file *seq, void *v)
{
	struct aa_proxy *proxy = seq->private;
	struct aa_profile *profile = aa_get_profile_rcu(&proxy->profile);
	unsigned int i, size = aa_hash_size();

	if (profile->rawdata->hash) {
		for (i = 0; i < size; i++)
			seq_printf(seq, "%.2x", profile->rawdata->hash[i]);
		seq_puts(seq, "\n");
	}
	aa_put_profile(profile);

	return 0;
}

static int aa_fs_seq_raw_hash_open(struct inode *inode, struct file *file)
{
	return aa_fs_seq_profile_open(inode, file, aa_fs_seq_raw_hash_show);
}

static const struct file_operations aa_fs_seq_raw_hash_fops = {
	.owner		= THIS_MODULE,
	.open		= aa_fs_seq_raw_hash_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= aa_fs_seq_profile_release,
};

static ssize_t rawdata_read(struct file *file, char __user *buf, size_t size,
			    loff_t *ppos)
{
	struct aa_loaddata *rawdata = file->private_data;

	return simple_read_from_buffer(buf, size, ppos, rawdata->data,
				       rawdata->size);
}

static int rawdata_open(struct inode *inode, struct file *file)
{
	struct aa_proxy *proxy = inode->i_private;
	struct aa_profile *profile;

	if (!policy_view_capable(NULL))
		return -EACCES;
	profile = aa_get_profile_rcu(&proxy->profile);
	file->private_data = aa_get_loaddata(profile->rawdata);
	aa_put_profile(profile);

	return 0;
}

static const struct file_operations aa_fs_rawdata_fops = {
	.open = rawdata_open,
	.read = rawdata_read,
	.llseek = generic_file_llseek,
	.release = rawdata_release,
};

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

/* requires lock be held */
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
		if (!profile->dirname)
			goto fail;

		mangle_name(profile->base.name, profile->dirname);
		sprintf(profile->dirname + len, ".%ld", profile->ns->uniq_id++);
	}

	dent = securityfs_create_dir(profile->dirname, parent);
	if (IS_ERR(dent))
		goto fail;
	prof_dir(profile) = dir = dent;

	dent = create_profile_file(dir, "name", profile, &aa_fs_profname_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_NAME] = dent;

	dent = create_profile_file(dir, "mode", profile, &aa_fs_profmode_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_MODE] = dent;

	dent = create_profile_file(dir, "attach", profile,
				   &aa_fs_profattach_fops);
	if (IS_ERR(dent))
		goto fail;
	profile->dents[AAFS_PROF_ATTACH] = dent;

	if (profile->hash) {
		dent = create_profile_file(dir, "sha1", profile,
					   &aa_fs_seq_hash_fops);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_HASH] = dent;
	}

	if (profile->rawdata) {
		dent = create_profile_file(dir, "raw_sha1", profile,
					   &aa_fs_seq_raw_hash_fops);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_HASH] = dent;

		dent = create_profile_file(dir, "raw_abi", profile,
					   &aa_fs_seq_raw_abi_fops);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_ABI] = dent;

		dent = securityfs_create_file("raw_data", S_IFREG | 0444, dir,
					      profile->proxy,
					      &aa_fs_rawdata_fops);
		if (IS_ERR(dent))
			goto fail;
		profile->dents[AAFS_PROF_RAW_DATA] = dent;
		d_inode(dent)->i_size = profile->rawdata->size;
		aa_get_proxy(profile->proxy);
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
	AA_FS_FILE_FOPS(".ns_level", 0666, &aa_fs_ns_level),
	AA_FS_FILE_FOPS(".ns_name", 0640, &aa_fs_ns_name),
	AA_FS_FILE_FOPS("profiles", 0440, &aa_fs_profiles_fops),
	AA_FS_DIR("features", aa_fs_entry_features),
	{ }
};

static struct aa_fs_entry aa_fs_entry =
	AA_FS_DIR("apparmor", aa_fs_entry_apparmor);

/**
 * aafs_create_file - create a file entry in the apparmor securityfs
 * @fs_file: aa_fs_entry to build an entry for (NOT NULL)
 * @parent: the parent dentry in the securityfs
 *
 * Use aafs_remove_file to remove entries created with this fn.
 */
static int __init aafs_create_file(struct aa_fs_entry *fs_file,
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

static void __init aafs_remove_dir(struct aa_fs_entry *fs_dir);
/**
 * aafs_create_dir - recursively create a directory entry in the securityfs
 * @fs_dir: aa_fs_entry (and all child entries) to build (NOT NULL)
 * @parent: the parent dentry in the securityfs
 *
 * Use aafs_remove_dir to remove entries created with this fn.
 */
static int __init aafs_create_dir(struct aa_fs_entry *fs_dir,
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
			error = aafs_create_dir(fs_file, fs_dir->dentry);
		else
			error = aafs_create_file(fs_file, fs_dir->dentry);
		if (error)
			goto failed;
	}

	return 0;

failed:
	aafs_remove_dir(fs_dir);

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
 * aafs_remove_dir - recursively drop a directory entry from the securityfs
 * @fs_dir: aa_fs_entry (and all child entries) to detach (NOT NULL)
 */
static void __init aafs_remove_dir(struct aa_fs_entry *fs_dir)
{
	struct aa_fs_entry *fs_file;

	for (fs_file = fs_dir->v.files; fs_file && fs_file->name; ++fs_file) {
		if (fs_file->v_type == AA_FS_TYPE_DIR)
			aafs_remove_dir(fs_file);
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
	aafs_remove_dir(&aa_fs_entry);
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
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
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

	/* Populate fs tree. */
	error = aafs_create_dir(&aa_fs_entry, NULL);
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
