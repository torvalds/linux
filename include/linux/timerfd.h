/*
 *  include/linux/timerfd.h
 *
 *  Copyright (C) 2007  Davide Libenzi <davidel@xmailserver.org>
 *
 */

#ifndef _LINUX_TIMERFD_H
#define _LINUX_TIMERFD_H

/* For O_CLOEXEC */
#include <linux/fcntl.h>

/* Flags for timerfd_settime.  */
#define TFD_TIMER_ABSTIME (1 << 0)

/* Flags for timerfd_create.  */
#define TFD_CLOEXEC O_CLOEXEC


#endif /* _LINUX_TIMERFD_H */

