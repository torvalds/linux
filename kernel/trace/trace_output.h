// SPDX-License-Identifier: GPL-2.0
#ifndef __TRACE_EVENTS_H
#define __TRACE_EVENTS_H

#include <linux/trace_seq.h>
#include "trace.h"

extern enum print_line_t
trace_print_bputs_msg_only(struct trace_iterator *iter);
extern enum print_line_t
trace_print_bprintk_msg_only(struct trace_iterator *iter);
extern enum print_line_t
trace_print_printk_msg_only(struct trace_iterator *iter);

extern int
seq_print_ip_sym(struct trace_seq *s, unsigned long ip,
		unsigned long sym_flags);

extern void trace_seq_print_sym(struct trace_seq *s, unsigned long address, bool offset);
extern int trace_print_context(struct trace_iterator *iter);
extern int trace_print_lat_context(struct trace_iterator *iter);
extern enum print_line_t print_event_fields(struct trace_iterator *iter,
					    struct trace_event *event);

extern void trace_event_read_lock(void);
extern void trace_event_read_unlock(void);
extern struct trace_event *ftrace_find_event(int type);

extern enum print_line_t trace_nop_print(struct trace_iterator *iter,
					 int flags, struct trace_event *event);
extern int
trace_print_lat_fmt(struct trace_seq *s, struct trace_entry *entry);

/* used by module unregistering */
extern int __unregister_trace_event(struct trace_event *event);
extern struct rw_semaphore trace_event_sem;

#define SEQ_PUT_FIELD(s, x)				\
	trace_seq_putmem(s, &(x), sizeof(x))

#define SEQ_PUT_HEX_FIELD(s, x)				\
	trace_seq_putmem_hex(s, &(x), sizeof(x))

#endif

