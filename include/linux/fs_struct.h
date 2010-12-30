#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#include <linux/path.h>

struct fs_struct {
	int users;
	spinlock_t lock;
	int umask;
	int in_exec;
	struct path root, pwd;
};

extern struct kmem_cache *fs_cachep;

extern void exit_fs(struct task_struct *);
extern void set_fs_root(struct fs_struct *, struct path *);
extern void set_fs_pwd(struct fs_struct *, struct path *);
extern struct fs_struct *copy_fs_struct(struct fs_struct *);
extern void free_fs_struct(struct fs_struct *);
extern void daemonize_fs_struct(void);
extern int unshare_fs_struct(void);

static inline void get_fs_root(struct fs_struct *fs, struct path *root)
{
	spin_lock(&fs->lock);
	*root = fs->root;
	path_get(root);
	spin_unlock(&fs->lock);
}

static inline void get_fs_pwd(struct fs_struct *fs, struct path *pwd)
{
	spin_lock(&fs->lock);
	*pwd = fs->pwd;
	path_get(pwd);
	spin_unlock(&fs->lock);
}

static inline void get_fs_root_and_pwd(struct fs_struct *fs, struct path *root,
				       struct path *pwd)
{
	spin_lock(&fs->lock);
	*root = fs->root;
	path_get(root);
	*pwd = fs->pwd;
	path_get(pwd);
	spin_unlock(&fs->lock);
}

#endif /* _LINUX_FS_STRUCT_H */
