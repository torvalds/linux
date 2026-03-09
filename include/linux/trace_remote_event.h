/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _LINUX_TRACE_REMOTE_EVENTS_H
#define _LINUX_TRACE_REMOTE_EVENTS_H

struct trace_remote;
struct trace_event_fields;

struct remote_event_hdr {
	unsigned short	id;
};

#define REMOTE_EVENT_NAME_MAX 30
struct remote_event {
	char				name[REMOTE_EVENT_NAME_MAX];
	unsigned short			id;
	bool				enabled;
	struct trace_remote		*remote;
	struct trace_event_fields	*fields;
	char				*print_fmt;
	void				(*print)(void *evt, struct trace_seq *seq);
};
#endif
