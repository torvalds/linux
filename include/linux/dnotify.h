#ifndef _LINUX_DNOTIFY_H
#define _LINUX_DNOTIFY_H
/*
 * Directory notification for Linux
 *
 * Copyright (C) 2000,2002 Stephen Rothwell
 */

#include <linux/fs.h>

struct dnotify_struct {
	struct dnotify_struct *	dn_next;
	unsigned long		dn_mask;
	int			dn_fd;
	struct file *		dn_filp;
	fl_owner_t		dn_owner;
};

#ifdef __KERNEL__


#ifdef CONFIG_DNOTIFY

extern void __inode_dir_notify(struct inode *, unsigned long);
extern void dnotify_flush(struct file *, fl_owner_t);
extern int fcntl_dirnotify(int, struct file *, unsigned long);
extern void dnotify_parent(struct dentry *, unsigned long);

static inline void inode_dir_notify(struct inode *inode, unsigned long event)
{
	if (inode->i_dnotify_mask & (event))
		__inode_dir_notify(inode, event);
}

#else

static inline void __inode_dir_notify(struct inode *inode, unsigned long event)
{
}

static inline void dnotify_flush(struct file *filp, fl_owner_t id)
{
}

static inline int fcntl_dirnotify(int fd, struct file *filp, unsigned long arg)
{
	return -EINVAL;
}

static inline void dnotify_parent(struct dentry *dentry, unsigned long event)
{
}

static inline void inode_dir_notify(struct inode *inode, unsigned long event)
{
}

#endif /* CONFIG_DNOTIFY */

#endif /* __KERNEL __ */

#endif /* _LINUX_DNOTIFY_H */
