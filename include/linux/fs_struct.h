/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#include <linux/sched.h>
#include <linux/path.h>
#include <linux/spinlock.h>
#include <linux/seqlock.h>
#include <linux/vfsdebug.h>

struct fs_struct {
	int users;
	seqlock_t seq;
	int umask;
	int in_exec;
	struct path root, pwd;
} __randomize_layout;

extern struct kmem_cache *fs_cachep;
extern struct fs_struct *userspace_init_fs;

extern void exit_fs(struct task_struct *);
extern void set_fs_root(struct fs_struct *, const struct path *);
extern void set_fs_pwd(struct fs_struct *, const struct path *);
extern struct fs_struct *copy_fs_struct(struct fs_struct *);
extern void free_fs_struct(struct fs_struct *);
extern int unshare_fs_struct(void);

static inline void get_fs_root(struct fs_struct *fs, struct path *root)
{
	read_seqlock_excl(&fs->seq);
	*root = fs->root;
	path_get(root);
	read_sequnlock_excl(&fs->seq);
}

static inline void get_fs_pwd(struct fs_struct *fs, struct path *pwd)
{
	read_seqlock_excl(&fs->seq);
	*pwd = fs->pwd;
	path_get(pwd);
	read_sequnlock_excl(&fs->seq);
}

struct fs_struct *switch_fs_struct(struct fs_struct *new_fs);

extern bool current_chrooted(void);

static inline int current_umask(void)
{
	return current->fs->umask;
}

/*
 * Temporarily use userspace_init_fs for path resolution in kthreads.
 * Callers should use scoped_with_init_fs() which automatically
 * restores the original fs_struct at scope exit.
 */
static inline struct fs_struct *__override_init_fs(void)
{
	struct fs_struct *old_fs;

	old_fs = current->fs;
	WRITE_ONCE(current->fs, userspace_init_fs);
	return old_fs;
}

static inline void __revert_init_fs(struct fs_struct *old_fs)
{
	VFS_WARN_ON_ONCE(current->fs != userspace_init_fs);
	WRITE_ONCE(current->fs, old_fs);
}

DEFINE_CLASS(__override_init_fs,
	     struct fs_struct *,
	     __revert_init_fs(_T),
	     __override_init_fs(), void)

#define scoped_with_init_fs() \
	scoped_class(__override_init_fs, __UNIQUE_ID(label))

void __init init_userspace_fs(void);

#endif /* _LINUX_FS_STRUCT_H */
