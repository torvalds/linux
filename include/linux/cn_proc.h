/*
 * cn_proc.h - process events connector
 *
 * Copyright (C) Matt Helsley, IBM Corp. 2005
 * Based on cn_fork.h by Nguyen Anh Quynh and Guillaume Thouvenin
 * Original copyright notice follows:
 * Copyright (C) 2005 Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (C) 2005 Guillaume Thouvenin <guillaume.thouvenin@bull.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef CN_PROC_H
#define CN_PROC_H

#include <linux/types.h>
#include <linux/time.h>
#include <linux/connector.h>

/*
 * Userspace sends this enum to register with the kernel that it is listening
 * for events on the connector.
 */
enum proc_cn_mcast_op {
	PROC_CN_MCAST_LISTEN = 1,
	PROC_CN_MCAST_IGNORE = 2
};

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
	enum what {
		/* Use successive bits so the enums can be used to record
		 * sets of events as well
		 */
		PROC_EVENT_NONE = 0x00000000,
		PROC_EVENT_FORK = 0x00000001,
		PROC_EVENT_EXEC = 0x00000002,
		PROC_EVENT_UID  = 0x00000004,
		PROC_EVENT_GID  = 0x00000040,
		/* "next" should be 0x00000400 */
		/* "last" is the last process event: exit */
		PROC_EVENT_EXIT = 0x80000000
	} what;
	__u32 cpu;
	struct timespec timestamp;
	union { /* must be last field of proc_event struct */
		struct {
			__u32 err;
		} ack;

		struct fork_proc_event {
			pid_t parent_pid;
			pid_t parent_tgid;
			pid_t child_pid;
			pid_t child_tgid;
		} fork;

		struct exec_proc_event {
			pid_t process_pid;
			pid_t process_tgid;
		} exec;

		struct id_proc_event {
			pid_t process_pid;
			pid_t process_tgid;
			union {
				__u32 ruid; /* task uid */
				__u32 rgid; /* task gid */
			} r;
			union {
				__u32 euid;
				__u32 egid;
			} e;
		} id;

		struct exit_proc_event {
			pid_t process_pid;
			pid_t process_tgid;
			__u32 exit_code, exit_signal;
		} exit;
	} event_data;
};

#ifdef __KERNEL__
#ifdef CONFIG_PROC_EVENTS
void proc_fork_connector(struct task_struct *task);
void proc_exec_connector(struct task_struct *task);
void proc_id_connector(struct task_struct *task, int which_id);
void proc_exit_connector(struct task_struct *task);
#else
static inline void proc_fork_connector(struct task_struct *task)
{}

static inline void proc_exec_connector(struct task_struct *task)
{}

static inline void proc_id_connector(struct task_struct *task,
				     int which_id)
{}

static inline void proc_exit_connector(struct task_struct *task)
{}
#endif	/* CONFIG_PROC_EVENTS */
#endif	/* __KERNEL__ */
#endif	/* CN_PROC_H */
