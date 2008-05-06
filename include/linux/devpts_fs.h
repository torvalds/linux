/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/include/linux/devpts_fs.h
 *
 *  Copyright 1998-2004 H. Peter Anvin -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#ifndef _LINUX_DEVPTS_FS_H
#define _LINUX_DEVPTS_FS_H

#include <linux/errno.h>

#ifdef CONFIG_UNIX98_PTYS

int devpts_new_index(void);
void devpts_kill_index(int idx);
int devpts_pty_new(struct tty_struct *tty);      /* mknod in devpts */
struct tty_struct *devpts_get_tty(int number);	 /* get tty structure */
void devpts_pty_kill(int number);		 /* unlink */

#else

/* Dummy stubs in the no-pty case */
static inline int devpts_new_index(void) { return -EINVAL; }
static inline void devpts_kill_index(int idx) { }
static inline int devpts_pty_new(struct tty_struct *tty) { return -EINVAL; }
static inline struct tty_struct *devpts_get_tty(int number) { return NULL; }
static inline void devpts_pty_kill(int number) { }

#endif


#endif /* _LINUX_DEVPTS_FS_H */
