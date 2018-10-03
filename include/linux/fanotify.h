/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FANOTIFY_H
#define _LINUX_FANOTIFY_H

#include <uapi/linux/fanotify.h>

/* not valid from userspace, only kernel internal */
#define FAN_MARK_ONDIR		0x80000000

#define FAN_GROUP_FLAG(group, flag) \
	((group)->fanotify_data.flags & (flag))

#endif /* _LINUX_FANOTIFY_H */
