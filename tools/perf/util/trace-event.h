/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _PERF_UTIL_TRACE_EVENT_H
#define _PERF_UTIL_TRACE_EVENT_H

#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <linux/types.h>

struct machine;
struct tep_format_field;
struct tep_plugin_list;

struct trace_event {
	struct tep_handle	*pevent;
	struct tep_plugin_list	*plugin_list;
};

/* Computes a version number comparable with LIBTRACEEVENT_VERSION from Makefile.config. */
#define MAKE_LIBTRACEEVENT_VERSION(a, b, c) ((a)*255*255+(b)*255+(c))

typedef char *(tep_func_resolver_t)(void *priv,
				    unsigned long long *addrp, char **modp);

bool have_tracepoints(struct list_head *evlist);

int trace_event__init(struct trace_event *t);
void trace_event__cleanup(struct trace_event *t);
void trace_event__exit(void);
int trace_event__register_resolver(struct machine *machine,
				   tep_func_resolver_t *func);
struct tep_event*
trace_event__tp_format(const char *sys, const char *name);

struct tep_event *trace_event__tp_format_id(int id);

void event_format__fprintf(const struct tep_event *event,
			   int cpu, void *data, int size, FILE *fp);

int parse_ftrace_file(struct tep_handle *pevent, char *buf, unsigned long size);
int parse_event_file(struct tep_handle *pevent,
		     char *buf, unsigned long size, char *sys);

unsigned long long
raw_field_value(struct tep_event *event, const char *name, void *data);

const char *parse_task_states(struct tep_format_field *state_field);

void parse_proc_kallsyms(struct tep_handle *pevent, char *file, unsigned int size);
void parse_ftrace_printk(struct tep_handle *pevent, char *file, unsigned int size);
void parse_saved_cmdline(struct tep_handle *pevent, char *file, unsigned int size);

ssize_t trace_report(int fd, struct trace_event *tevent, bool repipe);

unsigned long long read_size(struct tep_event *event, void *ptr, int size);
unsigned long long eval_flag(const char *flag);

int read_tracing_data(int fd, struct list_head *pattrs);

/*
 * Return the tracepoint name in the format "subsystem:event_name",
 * callers should free the returned string.
 */
char *tracepoint_id_to_name(u64 config);

struct tracing_data {
	/* size is only valid if temp is 'true' */
	ssize_t size;
	bool temp;
	char temp_file[50];
};

struct tracing_data *tracing_data_get(struct list_head *pattrs,
				      int fd, bool temp);
int tracing_data_put(struct tracing_data *tdata);

#if defined(LIBTRACEEVENT_VERSION) &&  LIBTRACEEVENT_VERSION >= MAKE_LIBTRACEEVENT_VERSION(1, 5, 0)
#include <event-parse.h>

static inline bool tep_field_is_relative(unsigned long flags)
{
	return (flags & TEP_FIELD_IS_RELATIVE) != 0;
}
#else
#include <linux/compiler.h>

static inline bool tep_field_is_relative(unsigned long flags __maybe_unused)
{
	return false;
}
#endif

#endif /* _PERF_UTIL_TRACE_EVENT_H */
