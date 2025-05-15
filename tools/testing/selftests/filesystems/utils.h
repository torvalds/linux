/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __IDMAP_UTILS_H
#define __IDMAP_UTILS_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <linux/types.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/capability.h>
#include <sys/fsuid.h>
#include <sys/types.h>
#include <unistd.h>

extern int get_userns_fd(unsigned long nsid, unsigned long hostid,
			 unsigned long range);

extern int caps_down(void);
extern int cap_down(cap_value_t down);

extern bool switch_ids(uid_t uid, gid_t gid);

static inline bool switch_userns(int fd, uid_t uid, gid_t gid, bool drop_caps)
{
	if (setns(fd, CLONE_NEWUSER))
		return false;

	if (!switch_ids(uid, gid))
		return false;

	if (drop_caps && !caps_down())
		return false;

	return true;
}

#endif /* __IDMAP_UTILS_H */
