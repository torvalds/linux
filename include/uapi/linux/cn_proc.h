/* SPDX-License-Identifier: LGPL-2.1 WITH Linux-syscall-note */
/*
 * cn_proc.h - process events connector
 *
 * Copyright (C) Matt Helsley, IBM Corp. 2005
 * Based on cn_fork.h by Nguyen Anh Quynh and Guillaume Thouvenin
 * Copyright (C) 2005 Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2005 Guillaume Thouvenin <guillaume.thouvenin@bull.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2.1 of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _UAPICN_PROC_H
#define _UAPICN_PROC_H

#include <linux/types.h>

/*
 * Userspace sends this enum to register with the kernel that it is listening
 * for events on the connector.
 */
enum proc_cn_mcast_op {
	PROC_CN_MCAST_LISTEN = 1,
	PROC_CN_MCAST_IGNORE = 2
};

#define PROC_EVENT_ALL (PROC_EVENT_FORK | PROC_EVENT_EXEC | PROC_EVENT_UID |  \
			PROC_EVENT_GID | PROC_EVENT_SID | PROC_EVENT_PTRACE | \
			PROC_EVENT_COMM | PROC_EVENT_NONZERO_EXIT |           \
			PROC_EVENT_COREDUMP | PROC_EVENT_EXIT)

/*
 * If you add an entry in proc_cn_event, make sure you add it in
 * PROC_EVENT_ALL above as well.
 */
enum proc_cn_event {
	/* Use successive bits so the enums can be used to record
	 * sets of events as well
	 */
	PROC_EVENT_NONE = 0x00000000,
	PROC_EVENT_FORK = 0x00000001,
	PROC_EVENT_EXEC = 0x00000002,
	PROC_EVENT_UID  = 0x00000004,
	PROC_EVENT_GID  = 0x00000040,
	PROC_EVENT_SID  = 0x00000080,
	PROC_EVENT_PTRACE = 0x00000100,
	PROC_EVENT_COMM = 0x00000200,
	/* "next" should be 0x00000400 */
	/* "last" is the last process event: exit,
	 * while "next to last" is coredumping event
	 * before that is report only if process dies
	 * with non-zero exit status
	 */
	PROC_EVENT_NONZERO_EXIT = 0x20000000,
	PROC_EVENT_COREDUMP = 0x40000000,
	PROC_EVENT_EXIT = 0x80000000
};

struct proc_input {
	enum proc_cn_mcast_op mcast_op;
	enum proc_cn_event event_type;
};

static inline enum proc_cn_event valid_event(enum proc_cn_event ev_type)
{
	return (enum proc_cn_event)(ev_type & PROC_EVENT_ALL);
}

/*
 * From the user's point of view, the process
 * ID is the thread group ID and thread ID is the internal
 * kernel "pid". So, fields are assigned as follow:
 *
 *  In user space     -  In  kernel space
 *
 * parent process ID  =  parent->tgid
 * parent thread  ID  =  parent->pid
 * child  process ID  =  child->tgid
 * child  thread  ID  =  child->pid
 */

struct proc_event {
	enum proc_cn_event what;
	__u32 cpu;
	__u64 __attribute__((aligned(8))) timestamp_ns;
		/* Number of nano seconds since system boot */
	union { /* must be last field of proc_event struct */
		struct {
			__u32 err;
		} ack;

		struct fork_proc_event {
			__kernel_pid_t parent_pid;
			__kernel_pid_t parent_tgid;
			__kernel_pid_t child_pid;
			__kernel_pid_t child_tgid;
		} fork;

		struct exec_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
		} exec;

		struct id_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
			union {
				__u32 ruid; /* task uid */
				__u32 rgid; /* task gid */
			} r;
			union {
				__u32 euid;
				__u32 egid;
			} e;
		} id;

		struct sid_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
		} sid;

		struct ptrace_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
			__kernel_pid_t tracer_pid;
			__kernel_pid_t tracer_tgid;
		} ptrace;

		struct comm_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
			char           comm[16];
		} comm;

		struct coredump_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
			__kernel_pid_t parent_pid;
			__kernel_pid_t parent_tgid;
		} coredump;

		struct exit_proc_event {
			__kernel_pid_t process_pid;
			__kernel_pid_t process_tgid;
			__u32 exit_code, exit_signal;
			__kernel_pid_t parent_pid;
			__kernel_pid_t parent_tgid;
		} exit;

	} event_data;
};

#endif /* _UAPICN_PROC_H */
