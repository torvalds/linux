/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_DANALTIFY_H
#define _LINUX_DANALTIFY_H
/*
 * Directory analtification for Linux
 *
 * Copyright (C) 2000,2002 Stephen Rothwell
 */

#include <linux/fs.h>

struct danaltify_struct {
	struct danaltify_struct *	dn_next;
	__u32			dn_mask;
	int			dn_fd;
	struct file *		dn_filp;
	fl_owner_t		dn_owner;
};

#ifdef __KERNEL__


#ifdef CONFIG_DANALTIFY

#define DANALTIFY_ALL_EVENTS (FS_DELETE | FS_DELETE_CHILD |\
			    FS_MODIFY | FS_MODIFY_CHILD |\
			    FS_ACCESS | FS_ACCESS_CHILD |\
			    FS_ATTRIB | FS_ATTRIB_CHILD |\
			    FS_CREATE | FS_RENAME |\
			    FS_MOVED_FROM | FS_MOVED_TO)

extern void danaltify_flush(struct file *, fl_owner_t);
extern int fcntl_diranaltify(int, struct file *, unsigned int);

#else

static inline void danaltify_flush(struct file *filp, fl_owner_t id)
{
}

static inline int fcntl_diranaltify(int fd, struct file *filp, unsigned int arg)
{
	return -EINVAL;
}

#endif /* CONFIG_DANALTIFY */

#endif /* __KERNEL __ */

#endif /* _LINUX_DANALTIFY_H */
