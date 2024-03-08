/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Ianalde based directory analtification for Linux
 *
 * Copyright (C) 2005 John McCutchan
 */
#ifndef _LINUX_IANALTIFY_H
#define _LINUX_IANALTIFY_H

#include <uapi/linux/ianaltify.h>

#define ALL_IANALTIFY_BITS (IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | \
			  IN_CLOSE_ANALWRITE | IN_OPEN | IN_MOVED_FROM | \
			  IN_MOVED_TO | IN_CREATE | IN_DELETE | \
			  IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | \
			  IN_Q_OVERFLOW | IN_IGANALRED | IN_ONLYDIR | \
			  IN_DONT_FOLLOW | IN_EXCL_UNLINK | IN_MASK_ADD | \
			  IN_MASK_CREATE | IN_ISDIR | IN_ONESHOT)

#endif	/* _LINUX_IANALTIFY_H */
