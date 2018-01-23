/*
 * /proc/uid support
 */

#include <linux/cpufreq_times.h>
#include <linux/fs.h>
#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include "internal.h"

struct proc_dir_entry *proc_uid;

#define UID_HASH_BITS 10

static DECLARE_HASHTABLE(proc_uid_hash_table, UID_HASH_BITS);

/*
 * use rt_mutex here to avoid priority inversion between high-priority readers
 * of these files and tasks calling proc_register_uid().
 */
static DEFINE_RT_MUTEX(proc_uid_lock); /* proc_uid_hash_table */

struct uid_hash_entry {
	uid_t uid;
	struct hlist_node hash;
};

/* Caller must hold proc_uid_lock */
static bool uid_hash_entry_exists_locked(uid_t uid)
{
	struct uid_hash_entry *entry;

	hash_for_each_possible(proc_uid_hash_table, entry, hash, uid) {
		if (entry->uid == uid)
			return true;
	}
	return false;
}

void proc_register_uid(kuid_t kuid)
{
	struct uid_hash_entry *entry;
	bool exists;
	uid_t uid = from_kuid_munged(current_user_ns(), kuid);

	rt_mutex_lock(&proc_uid_lock);
	exists = uid_hash_entry_exists_locked(uid);
	rt_mutex_unlock(&proc_uid_lock);
	if (exists)
		return;

	entry = kzalloc(sizeof(struct uid_hash_entry), GFP_KERNEL);
	if (!entry)
		return;
	entry->uid = uid;

	rt_mutex_lock(&proc_uid_lock);
	if (uid_hash_entry_exists_locked(uid))
		kfree(entry);
	else
		hash_add(proc_uid_hash_table, &entry->hash, uid);
	rt_mutex_unlock(&proc_uid_lock);
}

struct uid_entry {
	const char *name;
	int len;
	umode_t mode;
	const struct inode_operations *iop;
	const struct file_operations *fop;
};

#define NOD(NAME, MODE, IOP, FOP) {			\
	.name	= (NAME),				\
	.len	= sizeof(NAME) - 1,			\
	.mode	= MODE,					\
	.iop	= IOP,					\
	.fop	= FOP,					\
}

#ifdef CONFIG_CPU_FREQ_TIMES
const struct file_operations proc_uid_time_in_state_operations = {
	.open		= single_uid_time_in_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
#endif

static const struct uid_entry uid_base_stuff[] = {
#ifdef CONFIG_CPU_FREQ_TIMES
	NOD("time_in_state", 0444, NULL, &proc_uid_time_in_state_operations),
#endif
};

const struct inode_operations proc_uid_def_inode_operations = {
	.setattr	= proc_setattr,
};

struct inode *proc_uid_make_inode(struct super_block *sb, kuid_t kuid)
{
	struct inode *inode;

	inode = new_inode(sb);
	if (!inode)
		return NULL;

	inode->i_ino = get_next_ino();
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &proc_uid_def_inode_operations;
	inode->i_uid = kuid;

	return inode;
}

static int proc_uident_instantiate(struct inode *dir, struct dentry *dentry,
				   struct task_struct *unused, const void *ptr)
{
	const struct uid_entry *u = ptr;
	struct inode *inode;

	inode = proc_uid_make_inode(dir->i_sb, dir->i_uid);
	if (!inode)
		return -ENOENT;

	inode->i_mode = u->mode;
	if (S_ISDIR(inode->i_mode))
		set_nlink(inode, 2);
	if (u->iop)
		inode->i_op = u->iop;
	if (u->fop)
		inode->i_fop = u->fop;
	d_add(dentry, inode);
	return 0;
}

static struct dentry *proc_uid_base_lookup(struct inode *dir,
					   struct dentry *dentry,
					   unsigned int flags)
{
	const struct uid_entry *u, *last;
	unsigned int nents = ARRAY_SIZE(uid_base_stuff);

	if (nents == 0)
		return ERR_PTR(-ENOENT);

	last = &uid_base_stuff[nents - 1];
	for (u = uid_base_stuff; u <= last; u++) {
		if (u->len != dentry->d_name.len)
			continue;
		if (!memcmp(dentry->d_name.name, u->name, u->len))
			break;
	}
	if (u > last)
		return ERR_PTR(-ENOENT);

	return ERR_PTR(proc_uident_instantiate(dir, dentry, NULL, u));
}

static int proc_uid_base_readdir(struct file *file, struct dir_context *ctx)
{
	unsigned int nents = ARRAY_SIZE(uid_base_stuff);
	const struct uid_entry *u;

	if (!dir_emit_dots(file, ctx))
		return 0;

	if (ctx->pos >= nents + 2)
		return 0;

	for (u = uid_base_stuff + (ctx->pos - 2);
	     u <= uid_base_stuff + nents - 1; u++) {
		if (!proc_fill_cache(file, ctx, u->name, u->len,
				     proc_uident_instantiate, NULL, u))
			break;
		ctx->pos++;
	}

	return 0;
}

static const struct inode_operations proc_uid_base_inode_operations = {
	.lookup		= proc_uid_base_lookup,
	.setattr	= proc_setattr,
};

static const struct file_operations proc_uid_base_operations = {
	.read		= generic_read_dir,
	.iterate	= proc_uid_base_readdir,
	.llseek		= default_llseek,
};

static int proc_uid_instantiate(struct inode *dir, struct dentry *dentry,
				struct task_struct *unused, const void *ptr)
{
	unsigned int i, len;
	nlink_t nlinks;
	kuid_t *kuid = (kuid_t *)ptr;
	struct inode *inode = proc_uid_make_inode(dir->i_sb, *kuid);

	if (!inode)
		return -ENOENT;

	inode->i_mode = S_IFDIR | 0555;
	inode->i_op = &proc_uid_base_inode_operations;
	inode->i_fop = &proc_uid_base_operations;
	inode->i_flags |= S_IMMUTABLE;

	nlinks = 2;
	len = ARRAY_SIZE(uid_base_stuff);
	for (i = 0; i < len; ++i) {
		if (S_ISDIR(uid_base_stuff[i].mode))
			++nlinks;
	}
	set_nlink(inode, nlinks);

	d_add(dentry, inode);

	return 0;
}

static int proc_uid_readdir(struct file *file, struct dir_context *ctx)
{
	int last_shown, i;
	unsigned long bkt;
	struct uid_hash_entry *entry;

	if (!dir_emit_dots(file, ctx))
		return 0;

	i = 0;
	last_shown = ctx->pos - 2;
	rt_mutex_lock(&proc_uid_lock);
	hash_for_each(proc_uid_hash_table, bkt, entry, hash) {
		int len;
		char buf[PROC_NUMBUF];

		if (i < last_shown)
			continue;
		len = snprintf(buf, sizeof(buf), "%u", entry->uid);
		if (!proc_fill_cache(file, ctx, buf, len,
				     proc_uid_instantiate, NULL, &entry->uid))
			break;
		i++;
		ctx->pos++;
	}
	rt_mutex_unlock(&proc_uid_lock);
	return 0;
}

static struct dentry *proc_uid_lookup(struct inode *dir, struct dentry *dentry,
				      unsigned int flags)
{
	int result = -ENOENT;

	uid_t uid = name_to_int(&dentry->d_name);
	bool uid_exists;

	rt_mutex_lock(&proc_uid_lock);
	uid_exists = uid_hash_entry_exists_locked(uid);
	rt_mutex_unlock(&proc_uid_lock);
	if (uid_exists) {
		kuid_t kuid = make_kuid(current_user_ns(), uid);

		result = proc_uid_instantiate(dir, dentry, NULL, &kuid);
	}
	return ERR_PTR(result);
}

static const struct file_operations proc_uid_operations = {
	.read		= generic_read_dir,
	.iterate	= proc_uid_readdir,
	.llseek		= default_llseek,
};

static const struct inode_operations proc_uid_inode_operations = {
	.lookup		= proc_uid_lookup,
	.setattr	= proc_setattr,
};

int __init proc_uid_init(void)
{
	proc_uid = proc_mkdir("uid", NULL);
	proc_uid->proc_iops = &proc_uid_inode_operations;
	proc_uid->proc_fops = &proc_uid_operations;

	return 0;
}
