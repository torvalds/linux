// SPDX-License-Identifier: GPL-2.0
#include <tracefs.h>
#include <stddef.h>

struct trace_events {
	struct trace_events *next;
	char *system;
	char *event;
	char *filter;
	char *trigger;
	char enabled;
	char filter_enabled;
	char trigger_enabled;
};

struct trace_instance {
	struct tracefs_instance		*inst;
	struct tep_handle		*tep;
	struct trace_seq		*seq;
	unsigned long long		missed_events;
	unsigned long long		processed_events;
};

int trace_instance_init(struct trace_instance *trace, char *tool_name);
int trace_instance_start(struct trace_instance *trace);
int trace_instance_stop(struct trace_instance *trace);
void trace_instance_destroy(struct trace_instance *trace);

struct trace_seq *get_trace_seq(void);
int enable_tracer_by_name(struct tracefs_instance *inst, const char *tracer_name);
void disable_tracer(struct tracefs_instance *inst);

struct tracefs_instance *create_instance(char *instance_name);
void destroy_instance(struct tracefs_instance *inst);

int save_trace_to_file(struct tracefs_instance *inst, const char *filename);
int collect_registered_events(struct tep_event *tep, struct tep_record *record,
			      int cpu, void *context);

struct trace_events *trace_event_alloc(const char *event_string);
void trace_events_disable(struct trace_instance *instance,
			  struct trace_events *events);
void trace_events_destroy(struct trace_instance *instance,
			  struct trace_events *events);
int trace_events_enable(struct trace_instance *instance,
			  struct trace_events *events);

int trace_event_add_filter(struct trace_events *event, char *filter);
int trace_event_add_trigger(struct trace_events *event, char *trigger);
int trace_set_buffer_size(struct trace_instance *trace, int size);
