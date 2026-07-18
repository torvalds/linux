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

#ifdef CONFIG_KCMP
struct file *get_epoll_tfile_raw_ptr(struct file *file, int tfd, unsigned long toff);
#endif

/* Used to release the epoll bits inside the "struct file" */
void eventpoll_release_file(struct file *file);

/* Copy ready events to userspace */
int epoll_sendevents(struct file *file, struct epoll_event __user *events,
		     int maxevents);

/*
 * This is called from inside fs/file_table.c:__fput() to unlink files
 * from the eventpoll interface. We need to have this facility to cleanup
 * correctly files that are closed without being removed from the eventpoll
 * interface.
 */
static inline void eventpoll_release(struct file *file)
{

	/*
	 * Fast check to skip the slow path in the common case where the
	 * file was never attached to an epoll. Safe without file->f_lock
	 * because every f_ep writer excludes a concurrent __fput() on
	 * @file:
	 *   - ep_insert() requires the file alive (refcount > 0);
	 *   - ep_remove() holds @file pinned via epi_fget() across the
	 *     write;
	 *   - eventpoll_release_file() runs from __fput() itself.
	 * We are in __fput() here, so none of those can race us: a NULL
	 * observation truly means no epoll path has work left on @file.
	 */
	if (likely(!READ_ONCE(file->f_ep)))
		return;

	/*
	 * The file is being closed while it is still linked to an epoll
	 * descriptor. We need to handle this by correctly unlinking it
	 * from its containers.
	 */
	eventpoll_release_file(file);
}

struct epoll_key {
	struct file *file;
	int fd;
} __packed;

int do_epoll_ctl_file(struct file *f, int op, struct epoll_key *tf,
		      struct epoll_event *epds, bool nonblock);
int do_epoll_ctl(int epfd, int op, int fd, struct epoll_event *epds,
		 bool nonblock);
bool is_file_epoll(struct file *f);

/* Tells if the epoll_ctl(2) operation needs an event copy from userspace */
static inline int ep_op_has_event(int op)
{
	return op != EPOLL_CTL_DEL;
}

#else

static inline void eventpoll_release(struct file *file) {}

#endif

#if defined(CONFIG_ARM) && defined(CONFIG_OABI_COMPAT)
/* ARM OABI has an incompatible struct layout and needs a special handler */
extern struct epoll_event __user *
epoll_put_uevent(__poll_t revents, __u64 data,
		 struct epoll_event __user *uevent);
#else
static inline struct epoll_event __user *
epoll_put_uevent(__poll_t revents, __u64 data,
		 struct epoll_event __user *uevent)
{
	scoped_user_write_access_size(uevent, sizeof(*uevent), efault) {
		unsafe_put_user(revents, &uevent->events, efault);
		unsafe_put_user(data, &uevent->data, efault);
	}
	return uevent+1;

efault:
	return NULL;
}
#endif

#endif /* #ifndef _LINUX_EVENTPOLL_H */
