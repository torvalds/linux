/* taskstats.h - exporting per-task statistics
 *
 * Copyright (C) Shailabh Nagar, IBM Corp. 2006
 *           (C) Balbir Singh,   IBM Corp. 2006
 *           (C) Jay Lan,        SGI, 2006
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _LINUX_TASKSTATS_H
#define _LINUX_TASKSTATS_H

#include <linux/types.h>

/* Format for per-task data returned to userland when
 *	- a task exits
 *	- listener requests stats for a task
 *
 * The struct is versioned. Newer versions should only add fields to
 * the bottom of the struct to maintain backward compatibility.
 *
 *
 * To add new fields
 *	a) bump up TASKSTATS_VERSION
 *	b) add comment indicating new version number at end of struct
 *	c) add new fields after version comment; maintain 64-bit alignment
 */


#define TASKSTATS_VERSION	8
#define TS_COMM_LEN		32	/* should be >= TASK_COMM_LEN
					 * in linux/sched.h */

struct taskstats {

	/* The version number of this struct. This field is always set to
	 * TAKSTATS_VERSION, which is defined in <linux/taskstats.h>.
	 * Each time the struct is changed, the value should be incremented.
	 */
	__u16	version;
	__u32	ac_exitcode;		/* Exit status */

	/* The accounting flags of a task as defined in <linux/acct.h>
	 * Defined values are AFORK, ASU, ACOMPAT, ACORE, and AXSIG.
	 */
	__u8	ac_flag;		/* Record flags */
	__u8	ac_nice;		/* task_nice */

	/* Delay accounting fields start
	 *
	 * All values, until comment "Delay accounting fields end" are
	 * available only if delay accounting is enabled, even though the last
	 * few fields are not delays
	 *
	 * xxx_count is the number of delay values recorded
	 * xxx_delay_total is the corresponding cumulative delay in nanoseconds
	 *
	 * xxx_delay_total wraps around to zero on overflow
	 * xxx_count incremented regardless of overflow
	 */

	/* Delay waiting for cpu, while runnable
	 * count, delay_total NOT updated atomically
	 */
	__u64	cpu_count __attribute__((aligned(8)));
	__u64	cpu_delay_total;

	/* Following four fields atomically updated using task->delays->lock */

	/* Delay waiting for synchronous block I/O to complete
	 * does not account for delays in I/O submission
	 */
	__u64	blkio_count;
	__u64	blkio_delay_total;

	/* Delay waiting for page fault I/O (swap in only) */
	__u64	swapin_count;
	__u64	swapin_delay_total;

	/* cpu "wall-clock" running time
	 * On some architectures, value will adjust for cpu time stolen
	 * from the kernel in involuntary waits due to virtualization.
	 * Value is cumulative, in nanoseconds, without a corresponding count
	 * and wraps around to zero silently on overflow
	 */
	__u64	cpu_run_real_total;

	/* cpu "virtual" running time
	 * Uses time intervals seen by the kernel i.e. no adjustment
	 * for kernel's involuntary waits due to virtualization.
	 * Value is cumulative, in nanoseconds, without a corresponding count
	 * and wraps around to zero silently on overflow
	 */
	__u64	cpu_run_virtual_total;
	/* Delay accounting fields end */
	/* version 1 ends here */

	/* Basic Accounting Fields start */
	char	ac_comm[TS_COMM_LEN];	/* Command name */
	__u8	ac_sched __attribute__((aligned(8)));
					/* Scheduling discipline */
	__u8	ac_pad[3];
	__u32	ac_uid __attribute__((aligned(8)));
					/* User ID */
	__u32	ac_gid;			/* Group ID */
	__u32	ac_pid;			/* Process ID */
	__u32	ac_ppid;		/* Parent process ID */
	__u32	ac_btime;		/* Begin time [sec since 1970] */
	__u64	ac_etime __attribute__((aligned(8)));
					/* Elapsed time [usec] */
	__u64	ac_utime;		/* User CPU time [usec] */
	__u64	ac_stime;		/* SYstem CPU time [usec] */
	__u64	ac_minflt;		/* Minor Page Fault Count */
	__u64	ac_majflt;		/* Major Page Fault Count */
	/* Basic Accounting Fields end */

	/* Extended accounting fields start */
	/* Accumulated RSS usage in duration of a task, in MBytes-usecs.
	 * The current rss usage is added to this counter every time
	 * a tick is charged to a task's system time. So, at the end we
	 * will have memory usage multiplied by system time. Thus an
	 * average usage per system time unit can be calculated.
	 */
	__u64	coremem;		/* accumulated RSS usage in MB-usec */
	/* Accumulated virtual memory usage in duration of a task.
	 * Same as acct_rss_mem1 above except that we keep track of VM usage.
	 */
	__u64	virtmem;		/* accumulated VM  usage in MB-usec */

	/* High watermark of RSS and virtual memory usage in duration of
	 * a task, in KBytes.
	 */
	__u64	hiwater_rss;		/* High-watermark of RSS usage, in KB */
	__u64	hiwater_vm;		/* High-water VM usage, in KB */

	/* The following four fields are I/O statistics of a task. */
	__u64	read_char;		/* bytes read */
	__u64	write_char;		/* bytes written */
	__u64	read_syscalls;		/* read syscalls */
	__u64	write_syscalls;		/* write syscalls */
	/* Extended accounting fields end */

#define TASKSTATS_HAS_IO_ACCOUNTING
	/* Per-task storage I/O accounting starts */
	__u64	read_bytes;		/* bytes of read I/O */
	__u64	write_bytes;		/* bytes of write I/O */
	__u64	cancelled_write_bytes;	/* bytes of cancelled write I/O */

	__u64  nvcsw;			/* voluntary_ctxt_switches */
	__u64  nivcsw;			/* nonvoluntary_ctxt_switches */

	/* time accounting for SMT machines */
	__u64	ac_utimescaled;		/* utime scaled on frequency etc */
	__u64	ac_stimescaled;		/* stime scaled on frequency etc */
	__u64	cpu_scaled_run_real_total; /* scaled cpu_run_real_total */

	/* Delay waiting for memory reclaim */
	__u64	freepages_count;
	__u64	freepages_delay_total;
};


/*
 * Commands sent from userspace
 * Not versioned. New commands should only be inserted at the enum's end
 * prior to __TASKSTATS_CMD_MAX
 */

enum {
	TASKSTATS_CMD_UNSPEC = 0,	/* Reserved */
	TASKSTATS_CMD_GET,		/* user->kernel request/get-response */
	TASKSTATS_CMD_NEW,		/* kernel->user event */
	__TASKSTATS_CMD_MAX,
};

#define TASKSTATS_CMD_MAX (__TASKSTATS_CMD_MAX - 1)

enum {
	TASKSTATS_TYPE_UNSPEC = 0,	/* Reserved */
	TASKSTATS_TYPE_PID,		/* Process id */
	TASKSTATS_TYPE_TGID,		/* Thread group id */
	TASKSTATS_TYPE_STATS,		/* taskstats structure */
	TASKSTATS_TYPE_AGGR_PID,	/* contains pid + stats */
	TASKSTATS_TYPE_AGGR_TGID,	/* contains tgid + stats */
	TASKSTATS_TYPE_NULL,		/* contains nothing */
	__TASKSTATS_TYPE_MAX,
};

#define TASKSTATS_TYPE_MAX (__TASKSTATS_TYPE_MAX - 1)

enum {
	TASKSTATS_CMD_ATTR_UNSPEC = 0,
	TASKSTATS_CMD_ATTR_PID,
	TASKSTATS_CMD_ATTR_TGID,
	TASKSTATS_CMD_ATTR_REGISTER_CPUMASK,
	TASKSTATS_CMD_ATTR_DEREGISTER_CPUMASK,
	__TASKSTATS_CMD_ATTR_MAX,
};

#define TASKSTATS_CMD_ATTR_MAX (__TASKSTATS_CMD_ATTR_MAX - 1)

/* NETLINK_GENERIC related info */

#define TASKSTATS_GENL_NAME	"TASKSTATS"
#define TASKSTATS_GENL_VERSION	0x1

#endif /* _LINUX_TASKSTATS_H */
