/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_IOPRIO_H
#define _UAPI_LINUX_IOPRIO_H

/*
 * Gives us 8 prio classes with 13-bits of data for each class
 */
#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

/*
 * These are the io priority groups as implemented by the BFQ and mq-deadline
 * schedulers. RT is the realtime class, it always gets premium service. For
 * ATA disks supporting NCQ IO priority, RT class IOs will be processed using
 * high priority NCQ commands. BE is the best-effort scheduling class, the
 * default for any process. IDLE is the idle scheduling class, it is only
 * served when no one else is using the disk.
 */
enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

/*
 * 8 best effort priority levels are supported
 */
#define IOPRIO_BE_NR	(8)

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

/*
 * Fallback BE priority
 */
#define IOPRIO_NORM	(4)

#endif /* _UAPI_LINUX_IOPRIO_H */
