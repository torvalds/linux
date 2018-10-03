/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_FANOTIFY_H
#define _LINUX_FANOTIFY_H

#include <uapi/linux/fanotify.h>

#define FAN_GROUP_FLAG(group, flag) \
	((group)->fanotify_data.flags & (flag))

#endif /* _LINUX_FANOTIFY_H */
