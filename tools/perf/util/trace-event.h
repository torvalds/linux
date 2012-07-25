#ifndef _PERF_UTIL_TRACE_EVENT_H
#define _PERF_UTIL_TRACE_EVENT_H

#include "parse-events.h"
#include "event-parse.h"
#include "session.h"

struct machine;
struct perf_sample;
union perf_event;
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

int read_trace_init(int file_bigendian, int host_bigendian);
void print_trace_event(int cpu, void *data, int size);

void print_event(int cpu, void *data, int size, unsigned long long nsecs,
		  char *comm);

int parse_ftrace_file(char *buf, unsigned long size);
int parse_event_file(char *buf, unsigned long size, char *sys);

struct pevent_record *trace_peek_data(int cpu);
struct event_format *trace_find_event(int type);

unsigned long long
raw_field_value(struct event_format *event, const char *name, void *data);
void *raw_field_ptr(struct event_format *event, const char *name, void *data);

void parse_proc_kallsyms(char *file, unsigned int size __unused);
void parse_ftrace_printk(char *file, unsigned int size __unused);

ssize_t trace_report(int fd, bool repipe);

int trace_parse_common_type(void *data);
int trace_parse_common_pid(void *data);

struct event_format *trace_find_next_event(struct event_format *event);
unsigned long long read_size(void *ptr, int size);
unsigned long long eval_flag(const char *flag);

struct pevent_record *trace_read_data(int cpu);
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
	int (*generate_script) (const char *outfile);
};

int script_spec_register(const char *spec, struct scripting_ops *ops);

void setup_perl_scripting(void);
void setup_python_scripting(void);

struct scripting_context {
	void *event_data;
};

int common_pc(struct scripting_context *context);
int common_flags(struct scripting_context *context);
int common_lock_depth(struct scripting_context *context);

#endif /* _PERF_UTIL_TRACE_EVENT_H */
