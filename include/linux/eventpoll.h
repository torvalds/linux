/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  include/linux/eventpoll.h ( Efficient event polling implementation )
 *  Copyright (C) 2001,...,2006	 Davide Libenzi
 *
 *  Davide Libenzi <davidel@xmailserver.org>
 */
#ifndef _LINUX_EVENTPOLL_H
#define _LINUX_EVENTPOLL_H

#include <uapi/linux/eventpoll.h>
#include <uapi/linux/kcmp.h>


/* Forward declarations to avoid compiler errors */
struct file;


#ifdef CONFIG_EPOLL

#ifdef CONFIG_CHECKPOINT_RESTORE
struct file *get_epoll_tfile_raw_ptr(struct file *file, int tfd, unsigned long toff);
#endif

/* Used to release the epoll bits inside the "struct file" */
void eventpoll_release_file(struct file *file);

/*
 * This is called from inside fs/file_table.c:__fput() to unlink files
 * from the eventpoll interface. We need to have this facility to cleanup
 * correctly files that are closed without being removed from the eventpoll
 * interface.
 */
static inline void eventpoll_release(struct file *file)
{

	/*
	 * Fast check to avoid the get/release of the semaphore. Since
	 * we're doing this outside the semaphore lock, it might return
	 * false negatives, but we don't care. It'll help in 99.99% of cases
	 * to avoid the semaphore lock. False positives simply cannot happen
	 * because the file in on the way to be removed and nobody ( but
	 * eventpoll ) has still a reference to this file.
	 */
	if (likely(!file->f_ep))
		return;

	/*
	 * The file is being closed while it is still linked to an epoll
	 * descriptor. We need to handle this by correctly unlinking it
	 * from its containers.
	 */
	eventpoll_release_file(file);
}

int do_epoll_ctl(int epfd, int op, int fd, struct epoll_event *epds,
		 bool nonblock);

/* Tells if the epoll_ctl(2) operation needs an event copy from userspace */
static inline int ep_op_has_event(int op)
{
	return op != EPOLL_CTL_DEL;
}

#else

static inline void eventpoll_release(struct file *file) {}

#endif

#endif /* #ifndef _LINUX_EVENTPOLL_H */
