#ifndef _PERF_UTIL_TRACE_EVENT_H
#define _PERF_UTIL_TRACE_EVENT_H

#include <traceevent/event-parse.h>
#include "parse-events.h"
#include "session.h"

struct machine;
struct perf_sample;
union perf_event;
struct perf_tool;
struct thread;

int bigendian(void);

struct pevent *read_trace_init(int file_bigendian, int host_bigendian);
void event_format__print(struct event_format *event,
			 int cpu, void *data, int size);

int parse_ftrace_file(struct pevent *pevent, char *buf, unsigned long size);
int parse_event_file(struct pevent *pevent,
		     char *buf, unsigned long size, char *sys);

unsigned long long
raw_field_value(struct event_format *event, const char *name, void *data);

void parse_proc_kallsyms(struct pevent *pevent, char *file, unsigned int size);
void parse_ftrace_printk(struct pevent *pevent, char *file, unsigned int size);

ssize_t trace_report(int fd, struct pevent **pevent, bool repipe);

struct event_format *trace_find_next_event(struct pevent *pevent,
					   struct event_format *event);
unsigned long long read_size(struct event_format *event, void *ptr, int size);
unsigned long long eval_flag(const char *flag);

int read_tracing_data(int fd, struct list_head *pattrs);

struct tracing_data {
	/* size is only valid if temp is 'true' */
	ssize_t size;
	bool temp;
	char temp_file[50];
};

struct tracing_data *tracing_data_get(struct list_head *pattrs,
				      int fd, bool temp);
int tracing_data_put(struct tracing_data *tdata);


struct addr_location;

struct perf_session;

struct scripting_ops {
	const char *name;
	int (*start_script) (const char *script, int argc, const char **argv);
	int (*stop_script) (void);
	void (*process_event) (union perf_event *event,
			       struct perf_sample *sample,
			       struct perf_evsel *evsel,
			       struct machine *machine,
			       struct thread *thread,
				   struct addr_location *al);
	int (*generate_script) (struct pevent *pevent, const char *outfile);
};

int script_spec_register(const char *spec, struct scripting_ops *ops);

void setup_perl_scripting(void);
void setup_python_scripting(void);

struct scripting_context {
	struct pevent *pevent;
	void *event_data;
};

int common_pc(struct scripting_context *context);
int common_flags(struct scripting_context *context);
int common_lock_depth(struct scripting_context *context);

#endif /* _PERF_UTIL_TRACE_EVENT_H */
