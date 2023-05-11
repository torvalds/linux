/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_IOPRIO_H
#define _UAPI_LINUX_IOPRIO_H

/*
 * Gives us 8 prio classes with 13-bits of data for each class
 */
#define IOPRIO_CLASS_SHIFT	13
#define IOPRIO_CLASS_MASK	0x07
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(ioprio)	\
	(((ioprio) >> IOPRIO_CLASS_SHIFT) & IOPRIO_CLASS_MASK)
#define IOPRIO_PRIO_DATA(ioprio)	((ioprio) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	\
	((((class) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 ((data) & IOPRIO_PRIO_MASK))

/*
 * These are the io priority classes as implemented by the BFQ and mq-deadline
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
 * The RT and BE priority classes both support up to 8 priority levels that
 * can be specified using the lower 3-bits of the priority data.
 */
#define IOPRIO_LEVEL_NR_BITS		3
#define IOPRIO_NR_LEVELS		(1 << IOPRIO_LEVEL_NR_BITS)
#define IOPRIO_LEVEL_MASK		(IOPRIO_NR_LEVELS - 1)
#define IOPRIO_PRIO_LEVEL(ioprio)	((ioprio) & IOPRIO_LEVEL_MASK)

#define IOPRIO_BE_NR			IOPRIO_NR_LEVELS

/*
 * Possible values for the "which" argument of the ioprio_get() and
 * ioprio_set() system calls (see "man ioprio_set").
 */
enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

/*
 * Fallback BE class priority level.
 */
#define IOPRIO_NORM	4
#define IOPRIO_BE_NORM	IOPRIO_NORM

/*
 * The 10 bits between the priority class and the priority level are used to
 * optionally define I/O hints for any combination of I/O priority class and
 * level. Depending on the kernel configuration, I/O scheduler being used and
 * the target I/O device being used, hints can influence how I/Os are processed
 * without affecting the I/O scheduling ordering defined by the I/O priority
 * class and level.
 */
#define IOPRIO_HINT_SHIFT		IOPRIO_LEVEL_NR_BITS
#define IOPRIO_HINT_NR_BITS		10
#define IOPRIO_NR_HINTS			(1 << IOPRIO_HINT_NR_BITS)
#define IOPRIO_HINT_MASK		(IOPRIO_NR_HINTS - 1)
#define IOPRIO_PRIO_HINT(ioprio)	\
	(((ioprio) >> IOPRIO_HINT_SHIFT) & IOPRIO_HINT_MASK)

/*
 * Alternate macro for IOPRIO_PRIO_VALUE() to define an I/O priority with
 * a class, level and hint.
 */
#define IOPRIO_PRIO_VALUE_HINT(class, level, hint)		 \
	((((class) & IOPRIO_CLASS_MASK) << IOPRIO_CLASS_SHIFT) | \
	 (((hint) & IOPRIO_HINT_MASK) << IOPRIO_HINT_SHIFT) |	 \
	 ((level) & IOPRIO_LEVEL_MASK))

/*
 * I/O hints.
 */
enum {
	/* No hint */
	IOPRIO_HINT_NONE = 0,

	/*
	 * Device command duration limits: indicate to the device a desired
	 * duration limit for the commands that will be used to process an I/O.
	 * These will currently only be effective for SCSI and ATA devices that
	 * support the command duration limits feature. If this feature is
	 * enabled, then the commands issued to the device to process an I/O with
	 * one of these hints set will have the duration limit index (dld field)
	 * set to the value of the hint.
	 */
	IOPRIO_HINT_DEV_DURATION_LIMIT_1 = 1,
	IOPRIO_HINT_DEV_DURATION_LIMIT_2 = 2,
	IOPRIO_HINT_DEV_DURATION_LIMIT_3 = 3,
	IOPRIO_HINT_DEV_DURATION_LIMIT_4 = 4,
	IOPRIO_HINT_DEV_DURATION_LIMIT_5 = 5,
	IOPRIO_HINT_DEV_DURATION_LIMIT_6 = 6,
	IOPRIO_HINT_DEV_DURATION_LIMIT_7 = 7,
};

#endif /* _UAPI_LINUX_IOPRIO_H */
