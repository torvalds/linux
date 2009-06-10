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

/*
 * CAREFUL: Check include/asm-generic/fcntl.h when defining
 * new flags, since they might collide with O_* ones. We want
 * to re-use O_* flags that couldn't possibly have a meaning
 * from eventfd, in order to leave a free define-space for
 * shared O_* flags.
 */
#define EFD_SEMAPHORE (1 << 0)
#define EFD_CLOEXEC O_CLOEXEC
#define EFD_NONBLOCK O_NONBLOCK

#define EFD_SHARED_FCNTL_FLAGS (O_CLOEXEC | O_NONBLOCK)
#define EFD_FLAGS_SET (EFD_SHARED_FCNTL_FLAGS | EFD_SEMAPHORE)

struct file *eventfd_fget(int fd);
int eventfd_signal(struct file *file, int n);

#else /* CONFIG_EVENTFD */

#define eventfd_fget(fd) ERR_PTR(-ENOSYS)
static inline int eventfd_signal(struct file *file, int n)
{ return 0; }

#endif /* CONFIG_EVENTFD */

#endif /* _LINUX_EVENTFD_H */

