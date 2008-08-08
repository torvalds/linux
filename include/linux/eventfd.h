/*
 *  include/linux/eventfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_EVENTFD_H
#define _LINUX_EVENTFD_H

#ifdef CONFIG_EVENTFD

/* For O_CLOEXEC and O_NONBLOCK */
#include <linux/fcntl.h>

/* Flags for eventfd2.  */
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

struct file *eventfd_fget(int fd);
int eventfd_signal(struct file *file, int n);

#else /* CONFIG_EVENTFD */

#define eventfd_fget(fd) ERR_PTR(-ENOSYS)
static inline int eventfd_signal(struct file *file, int n)
{ return 0; }

#endif /* CONFIG_EVENTFD */

#endif /* _LINUX_EVENTFD_H */

