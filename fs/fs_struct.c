// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/fs_struct.h>
#include "internal.h"

/*
 * Replace the fs->{rootmnt,root} with {mnt,dentry}. Put the old values.
 * It can block.
 */
void set_fs_root(struct fs_struct *fs, const struct path *path)
{
	struct path old_root;

	path_get(path);
	write_seqlock(&fs->seq);
	old_root = fs->root;
	fs->root = *path;
	write_sequnlock(&fs->seq);
	if (old_root.dentry)
		path_put(&old_root);
}

/*
 * Replace the fs->{pwdmnt,pwd} with {mnt,dentry}. Put the old values.
 * It can block.
 */
void set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
	struct path old_pwd;

	path_get(path);
	write_seqlock(&fs->seq);
	old_pwd = fs->pwd;
	fs->pwd = *path;
	write_sequnlock(&fs->seq);

	if (old_pwd.dentry)
		path_put(&old_pwd);
}

static inline int replace_path(struct path *p, const struct path *old, const struct path *new)
{
	if (likely(p->dentry != old->dentry || p->mnt != old->mnt))
		return 0;
	*p = *new;
	return 1;
}

void chroot_fs_refs(const struct path *old_root, const struct path *new_root)
{
	struct task_struct *g, *p;
	struct fs_struct *fs;
	int count = 0;

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		task_lock(p);
		fs = p->fs;
		if (fs) {
			int hits = 0;
			write_seqlock(&fs->seq);
			hits += replace_path(&fs->root, old_root, new_root);
			hits += replace_path(&fs->pwd, old_root, new_root);
			while (hits--) {
				count++;
				path_get(new_root);
			}
			write_sequnlock(&fs->seq);
		}
		task_unlock(p);
	}
	read_unlock(&tasklist_lock);
	while (count--)
		path_put(old_root);
}

void free_fs_struct(struct fs_struct *fs)
{
	path_put(&fs->root);
	path_put(&fs->pwd);
	kmem_cache_free(fs_cachep, fs);
}

void exit_fs(struct task_struct *tsk)
{
	struct fs_struct *fs = tsk->fs;

	if (fs) {
		int kill;
		task_lock(tsk);
		read_seqlock_excl(&fs->seq);
		tsk->fs = NULL;
		kill = !--fs->users;
		read_sequnlock_excl(&fs->seq);
		task_unlock(tsk);
		if (kill)
			free_fs_struct(fs);
	}
}

struct fs_struct *copy_fs_struct(struct fs_struct *old)
{
	struct fs_struct *fs = kmem_cache_alloc(fs_cachep, GFP_KERNEL);
	/* We don't need to lock fs - think why ;-) */
	if (fs) {
		fs->users = 1;
		fs->in_exec = 0;
		seqlock_init(&fs->seq);
		fs->umask = old->umask;

		read_seqlock_excl(&old->seq);
		fs->root = old->root;
		path_get(&fs->root);
		fs->pwd = old->pwd;
		path_get(&fs->pwd);
		read_sequnlock_excl(&old->seq);
	}
	return fs;
}

int unshare_fs_struct(void)
{
	struct fs_struct *fs = current->fs;
	struct fs_struct *new_fs = copy_fs_struct(fs);
	int kill;

	if (!new_fs)
		return -ENOMEM;

	task_lock(current);
	read_seqlock_excl(&fs->seq);
	kill = !--fs->users;
	current->fs = new_fs;
	read_sequnlock_excl(&fs->seq);
	task_unlock(current);

	if (kill)
		free_fs_struct(fs);

	return 0;
}
EXPORT_SYMBOL_GPL(unshare_fs_struct);

int current_umask(void)
{
	return current->fs->umask;
}
EXPORT_SYMBOL(current_umask);

/* to be mentioned only in INIT_TASK */
struct fs_struct init_fs = {
	.users		= 1,
	.seq		= __SEQLOCK_UNLOCKED(init_fs.seq),
	.umask		= 0022,
};
