// SPDX-License-Identifier: GPL-2.0

/* Do not include this file directly. */

#ifndef _TRACE_INTERNAL_PID_LIST_H
#define _TRACE_INTERNAL_PID_LIST_H

struct trace_pid_list {
	int			pid_max;
	unsigned long		*pids;
};

#endif /* _TRACE_INTERNAL_PID_LIST_H */
