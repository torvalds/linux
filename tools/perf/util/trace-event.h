#ifndef _PERF_UTIL_TRACE_EVENT_H
#define _PERF_UTIL_TRACE_EVENT_H

#include "parse-events.h"
#include "event-parse.h"
#include "session.h"

struct machine;
struct perf_sample;
union perf_event;
struct perf_tool;
struct thread;

extern int header_page_size_size;
extern int header_page_ts_size;
extern int header_page_data_offset;

extern bool latency_format;
extern struct pevent *perf_pevent;

enum {
	RINGBUF_TYPE_PADDING		= 29,
	RINGBUF_TYPE_TIME_EXTEND	= 30,
	RINGBUF_TYPE_TIME_STAMP		= 31,
};

#ifndef TS_SHIFT
#define TS_SHIFT		27
#endif

int bigendian(void);

struct pevent *read_trace_init(int file_bigendian, int host_bigendian);
void print_trace_event(struct pevent *pevent, int cpu, void *data, int size);
void event_format__print(struct event_format *event,
			 int cpu, void *data, int size);

void print_event(struct pevent *pevent, int cpu, void *data, int size,
		 unsigned long long nsecs, char *comm);

int parse_ftrace_file(struct pevent *pevent, char *buf, unsigned long size);
int parse_event_file(struct pevent *pevent,
		     char *buf, unsigned long size, char *sys);

struct pevent_record *trace_peek_data(struct pevent *pevent, int cpu);

unsigned long long
raw_field_value(struct event_format *event, const char *name, void *data);
void *raw_field_ptr(struct event_format *event, const char *name, void *data);

void parse_proc_kallsyms(struct pevent *pevent, char *file, unsigned int size);
void parse_ftrace_printk(struct pevent *pevent, char *file, unsigned int size);

ssize_t trace_report(int fd, struct pevent **pevent, bool repipe);

int trace_parse_common_type(struct pevent *pevent, void *data);
int trace_parse_common_pid(struct pevent *pevent, void *data);

struct event_format *trace_find_next_event(struct pevent *pevent,
					   struct event_format *event);
unsigned long long read_size(struct event_format *event, void *ptr, int size);
unsigned long long eval_flag(const char *flag);

struct pevent_record *trace_read_data(struct pevent *pevent, int cpu);
int read_tracing_data(int fd, struct list_head *pattrs);

struct tracing_data {
	/* size is only valid if temp is 'true' */
	ssize_t size;
	bool temp;
	char temp_file[50];
};

struct tracing_data *tracing_data_get(struct list_head *pattrs,
				      int fd, bool temp);
void tracing_data_put(struct tracing_data *tdata);


struct scripting_ops {
	const char *name;
	int (*start_script) (const char *script, int argc, const char **argv);
	int (*stop_script) (void);
	void (*process_event) (union perf_event *event,
			       struct perf_sample *sample,
			       struct perf_evsel *evsel,
			       struct machine *machine,
			       struct thread *thread);
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
