#ifndef __TRACE_EVENTS_H
#define __TRACE_EVENTS_H

#include "trace.h"

typedef int (*trace_print_func)(struct trace_seq *s, struct trace_entry *entry,
				int flags);

struct trace_event {
	struct hlist_node	node;
	int			type;
	trace_print_func	trace;
	trace_print_func	latency_trace;
	trace_print_func	raw;
	trace_print_func	hex;
	trace_print_func	binary;
};

extern int trace_seq_printf(struct trace_seq *s, const char *fmt, ...);
extern int
seq_print_ip_sym(struct trace_seq *s, unsigned long ip,
		unsigned long sym_flags);
extern ssize_t trace_seq_to_user(struct trace_seq *s, char __user *ubuf,
				 size_t cnt);
int trace_seq_puts(struct trace_seq *s, const char *str);
int trace_seq_putc(struct trace_seq *s, unsigned char c);
int trace_seq_putmem(struct trace_seq *s, void *mem, size_t len);
int trace_seq_putmem_hex(struct trace_seq *s, void *mem, size_t len);
int trace_seq_path(struct trace_seq *s, struct path *path);
int seq_print_userip_objs(const struct userstack_entry *entry,
			  struct trace_seq *s, unsigned long sym_flags);
int seq_print_user_ip(struct trace_seq *s, struct mm_struct *mm,
		      unsigned long ip, unsigned long sym_flags);

struct trace_event *ftrace_find_event(int type);
int register_ftrace_event(struct trace_event *event);
int unregister_ftrace_event(struct trace_event *event);

int
trace_nop_print(struct trace_seq *s, struct trace_entry *entry, int flags);

#define MAX_MEMHEX_BYTES	8
#define HEX_CHARS		(MAX_MEMHEX_BYTES*2 + 1)

#define SEQ_PUT_FIELD_RET(s, x)				\
do {							\
	if (!trace_seq_putmem(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

#define SEQ_PUT_HEX_FIELD_RET(s, x)			\
do {							\
	BUILD_BUG_ON(sizeof(x) > MAX_MEMHEX_BYTES);	\
	if (!trace_seq_putmem_hex(s, &(x), sizeof(x)))	\
		return 0;				\
} while (0)

#endif

