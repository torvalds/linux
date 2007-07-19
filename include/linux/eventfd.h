/*
 *  include/linux/eventfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H


#ifdef __KERNEL__

#ifdef CONFIG_EVENTFD

struct file *eventfd_fget(int fd);
int eventfd_signal(struct file *file, int n);

#else /* CONFIG_EVENTFD */

#define eventfd_fget(fd) ERR_PTR(-ENOSYS)
static inline int eventfd_signal(struct file *file, int n)
{ return 0; }

#endif /* CONFIG_EVENTFD */

#endif /* __KERNEL__ */

#endif /* _LINUX_EVENTFD_H */

