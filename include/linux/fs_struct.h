#ifndef _LINUX_FS_STRUCT_H
#define _LINUX_FS_STRUCT_H

#include <linux/path.h>

struct fs_struct {
	int users;
	rwlock_t lock;
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

#endif /* _LINUX_FS_STRUCT_H */
