/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The proc filesystem constants/structures
 */
#ifndef _LINUX_PROC_FS_H
#define _LINUX_PROC_FS_H

#include <linux/types.h>
#include <linux/fs.h>

struct proc_dir_entry;
struct seq_file;
struct seq_operations;

#ifdef CONFIG_PROC_FS

typedef int (*proc_write_t)(struct file *, char *, size_t);

extern void proc_root_init(void);
extern void proc_flush_task(struct task_struct *);

extern struct proc_dir_entry *proc_symlink(const char *,
		struct proc_dir_entry *, const char *);
extern struct proc_dir_entry *proc_mkdir(const char *, struct proc_dir_entry *);
extern struct proc_dir_entry *proc_mkdir_data(const char *, umode_t,
					      struct proc_dir_entry *, void *);
extern struct proc_dir_entry *proc_mkdir_mode(const char *, umode_t,
					      struct proc_dir_entry *);
struct proc_dir_entry *proc_create_mount_point(const char *name);

struct proc_dir_entry *proc_create_seq_private(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data);
#define proc_create_seq_data(name, mode, parent, ops, data) \
	proc_create_seq_private(name, mode, parent, ops, 0, data)
#define proc_create_seq(name, mode, parent, ops) \
	proc_create_seq_private(name, mode, parent, ops, 0, NULL)
struct proc_dir_entry *proc_create_single_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data);
#define proc_create_single(name, mode, parent, show) \
	proc_create_single_data(name, mode, parent, show, NULL)
 
extern struct proc_dir_entry *proc_create_data(const char *, umode_t,
					       struct proc_dir_entry *,
					       const struct file_operations *,
					       void *);

struct proc_dir_entry *proc_create(const char *name, umode_t mode, struct proc_dir_entry *parent, const struct file_operations *proc_fops);
extern void proc_set_size(struct proc_dir_entry *, loff_t);
extern void proc_set_user(struct proc_dir_entry *, kuid_t, kgid_t);
extern void *PDE_DATA(const struct inode *);
extern void *proc_get_parent_data(const struct inode *);
extern void proc_remove(struct proc_dir_entry *);
extern void remove_proc_entry(const char *, struct proc_dir_entry *);
extern int remove_proc_subtree(const char *, struct proc_dir_entry *);

struct proc_dir_entry *proc_create_net_data(const char *name, umode_t mode,
		struct proc_dir_entry *parent, const struct seq_operations *ops,
		unsigned int state_size, void *data);
#define proc_create_net(name, mode, parent, state_size, ops) \
	proc_create_net_data(name, mode, parent, state_size, ops, NULL)
struct proc_dir_entry *proc_create_net_single(const char *name, umode_t mode,
		struct proc_dir_entry *parent,
		int (*show)(struct seq_file *, void *), void *data);
struct proc_dir_entry *proc_create_net_data_write(const char *name, umode_t mode,
						  struct proc_dir_entry *parent,
						  const struct seq_operations *ops,
						  proc_write_t write,
						  unsigned int state_size, void *data);
struct proc_dir_entry *proc_create_net_single_write(const char *name, umode_t mode,
						    struct proc_dir_entry *parent,
						    int (*show)(struct seq_file *, void *),
						    proc_write_t write,
						    void *data);

#else /* CONFIG_PROC_FS */

static inline void proc_root_init(void)
{
}

static inline void proc_flush_task(struct task_struct *task)
{
}

static inline struct proc_dir_entry *proc_symlink(const char *name,
		struct proc_dir_entry *parent,const char *dest) { return NULL;}
static inline struct proc_dir_entry *proc_mkdir(const char *name,
	struct proc_dir_entry *parent) {return NULL;}
static inline struct proc_dir_entry *proc_create_mount_point(const char *name) { return NULL; }
static inline struct proc_dir_entry *proc_mkdir_data(const char *name,
	umode_t mode, struct proc_dir_entry *parent, void *data) { return NULL; }
static inline struct proc_dir_entry *proc_mkdir_mode(const char *name,
	umode_t mode, struct proc_dir_entry *parent) { return NULL; }
#define proc_create_seq_private(name, mode, parent, ops, size, data) ({NULL;})
#define proc_create_seq_data(name, mode, parent, ops, data) ({NULL;})
#define proc_create_seq(name, mode, parent, ops) ({NULL;})
#define proc_create_single(name, mode, parent, show) ({NULL;})
#define proc_create_single_data(name, mode, parent, show, data) ({NULL;})
#define proc_create(name, mode, parent, proc_fops) ({NULL;})
#define proc_create_data(name, mode, parent, proc_fops, data) ({NULL;})

static inline void proc_set_size(struct proc_dir_entry *de, loff_t size) {}
static inline void proc_set_user(struct proc_dir_entry *de, kuid_t uid, kgid_t gid) {}
static inline void *PDE_DATA(const struct inode *inode) {BUG(); return NULL;}
static inline void *proc_get_parent_data(const struct inode *inode) { BUG(); return NULL; }

static inline void proc_remove(struct proc_dir_entry *de) {}
#define remove_proc_entry(name, parent) do {} while (0)
static inline int remove_proc_subtree(const char *name, struct proc_dir_entry *parent) { return 0; }

#define proc_create_net_data(name, mode, parent, ops, state_size, data) ({NULL;})
#define proc_create_net(name, mode, parent, state_size, ops) ({NULL;})
#define proc_create_net_single(name, mode, parent, show, data) ({NULL;})

#endif /* CONFIG_PROC_FS */

struct net;

static inline struct proc_dir_entry *proc_net_mkdir(
	struct net *net, const char *name, struct proc_dir_entry *parent)
{
	return proc_mkdir_data(name, 0, parent, net);
}

struct ns_common;
int open_related_ns(struct ns_common *ns,
		   struct ns_common *(*get_ns)(struct ns_common *ns));

/* get the associated pid namespace for a file in procfs */
static inline struct pid_namespace *proc_pid_ns(const struct inode *inode)
{
	return inode->i_sb->s_fs_info;
}

#endif /* _LINUX_PROC_FS_H */
