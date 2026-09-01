// SPDX-License-Identifier: GPL-2.0-only
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/slab.h>
#include <linux/fs_struct.h>
#include <linux/init_task.h>
#include "internal.h"
#include "mount.h"

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
		if (p->flags & (PF_KTHREAD | PF_EXITING | PF_DUMPCORE))
			continue;

		task_lock(p);
		fs = p->real_fs;
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
	struct fs_struct *fs = tsk->real_fs;

	if (fs) {
		int kill;
		task_lock(tsk);
		read_seqlock_excl(&fs->seq);
		tsk->real_fs = NULL;
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
	struct fs_struct *fs = current->real_fs;
	struct fs_struct *new_fs = copy_fs_struct(fs);
	int kill;

	if (!new_fs)
		return -ENOMEM;

	task_lock(current);
	read_seqlock_excl(&fs->seq);
	VFS_WARN_ON_ONCE(fs != current->fs);
	kill = !--fs->users;
	current->fs = new_fs;
	current->real_fs = new_fs;
	read_sequnlock_excl(&fs->seq);
	task_unlock(current);

	if (kill)
		free_fs_struct(fs);

	return 0;
}
EXPORT_SYMBOL_GPL(unshare_fs_struct);

/*
 * PID 1 may choose to stop sharing fs_struct state with us.
 * Either via unshare(CLONE_FS) or unshare(CLONE_NEWNS). Of
 * course, PID 1 could have chosen to create arbitrary process
 * trees that all share fs_struct state via CLONE_FS. This is a
 * strong statement: We only care about PID 1 aka the thread-group
 * leader so subthread's fs_struct state doesn't matter.
 *
 * PID 1 unsharing fs_struct state is a bug. PID 1 relies on
 * various kthreads to be able to perform work based on its
 * fs_struct state. Breaking that contract sucks for both sides.
 * So just don't bother with extra work for this. No sane init
 * system should ever do this.
 *
 * On older kernels if PID 1 unshared its filesystem state with us the
 * kernel simply used the stale fs_struct state implicitly pinning
 * anything that PID 1 had last used. Even if PID 1 might've moved on to
 * some completely different fs_struct state and might've even unmounted
 * the old root.
 *
 * This has hilarious consequences: Think continuing to dump coredump
 * state into an implicitly pinned directory somewhere. Calling random
 * binaries in the old rootfs via usermodehelpers.
 *
 * Be aggressive about this: We simply reject operating on stale
 * fs_struct state by reverting to nullfs. Every kworker that does
 * lookups after this point will fail. Every usermodehelper call will
 * fail. Tough luck but let's be kind and emit a warning to userspace.
 */
static inline void validate_fs_switch(struct fs_struct *old_fs)
{
	might_sleep();

	if (likely(current->pid != 1))
		return;
	/* @old_fs may be dangling but for comparison it's fine */
	if (old_fs != userspace_init_fs)
		return;
	pr_warn("VFS: Pid 1 stopped sharing filesystem state\n");
	set_fs_root(userspace_init_fs, &init_fs.root);
	set_fs_pwd(userspace_init_fs, &init_fs.root);
}

struct fs_struct *switch_fs_struct(struct fs_struct *new_fs)
{
	struct fs_struct *fs;

	scoped_guard(task_lock, current) {
		fs = current->fs;
		VFS_WARN_ON_ONCE(fs != current->real_fs);
		read_seqlock_excl(&fs->seq);
		current->fs = new_fs;
		current->real_fs = new_fs;
		if (--fs->users)
			new_fs = NULL;
		else
			new_fs = fs;
		read_sequnlock_excl(&fs->seq);
	}

	validate_fs_switch(fs);
	return new_fs;
}

/* to be mentioned only in INIT_TASK */
struct fs_struct init_fs = {
	.users		= 1,
	.seq		= __SEQLOCK_UNLOCKED(init_fs.seq),
	.umask		= 0022,
};

struct fs_struct *userspace_init_fs __ro_after_init;
EXPORT_SYMBOL_GPL(userspace_init_fs);

void __init init_userspace_fs(void)
{
	struct mount *m;
	struct path root;

	/* Move PID 1 from nullfs into the initramfs. */
	m = topmost_overmount(current->nsproxy->mnt_ns->root);
	root.mnt = &m->mnt;
	root.dentry = root.mnt->mnt_root;

	VFS_WARN_ON_ONCE(current->pid != 1);

	set_fs_root(current->fs, &root);
	set_fs_pwd(current->fs, &root);

	/* Hold a reference for the global pointer. */
	read_seqlock_excl(&current->fs->seq);
	current->fs->users++;
	read_sequnlock_excl(&current->fs->seq);

	userspace_init_fs = current->fs;
}
