#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>

/*
 * Gives us 8 prio classes with 13-bits of data for each class
 */
#define IOPRIO_BITS		(16)
#define IOPRIO_CLASS_SHIFT	(13)
#define IOPRIO_PRIO_MASK	((1UL << IOPRIO_CLASS_SHIFT) - 1)

#define IOPRIO_PRIO_CLASS(mask)	((mask) >> IOPRIO_CLASS_SHIFT)
#define IOPRIO_PRIO_DATA(mask)	((mask) & IOPRIO_PRIO_MASK)
#define IOPRIO_PRIO_VALUE(class, data)	(((class) << IOPRIO_CLASS_SHIFT) | data)

#define ioprio_valid(mask)	(IOPRIO_PRIO_CLASS((mask)) != IOPRIO_CLASS_NONE)

/*
 * These are the io priority groups as implemented by CFQ. RT is the realtime
 * class, it always gets premium service. BE is the best-effort scheduling
 * class, the default for any process. IDLE is the idle scheduling class, it
 * is only served when no one else is using the disk.
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
 * if process has set io priority explicitly, use that. if not, convert
 * the cpu scheduler nice value to an io priority
 */
#define IOPRIO_NORM	(4)
static inline int task_ioprio(struct task_struct *task)
{
	if (ioprio_valid(task->ioprio))
		return IOPRIO_PRIO_DATA(task->ioprio);

	return IOPRIO_NORM;
}

static inline int task_ioprio_class(struct task_struct *task)
{
	if (ioprio_valid(task->ioprio))
		return IOPRIO_PRIO_CLASS(task->ioprio);

	return IOPRIO_CLASS_BE;
}

static inline int task_nice_ioprio(struct task_struct *task)
{
	return (task_nice(task) + 20) / 5;
}

/*
 * For inheritance, return the highest of the two given priorities
 */
extern int ioprio_best(unsigned short aprio, unsigned short bprio);

#endif
