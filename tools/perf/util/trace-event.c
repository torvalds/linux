
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/kernel.h>
#include <traceevent/event-parse.h>
#include "trace-event.h"
#include "util.h"

/*
 * global trace_event object used by trace_event__tp_format
 *
 * TODO There's no cleanup call for this. Add some sort of
 * __exit function support and call trace_event__cleanup
 * there.
 */
static struct trace_event tevent;

int trace_event__init(struct trace_event *t)
{
	struct pevent *pevent = pevent_alloc();

	if (pevent) {
		t->plugin_list = traceevent_load_plugins(pevent);
		t->pevent  = pevent;
	}

	return pevent ? 0 : -1;
}

void trace_event__cleanup(struct trace_event *t)
{
	traceevent_unload_plugins(t->plugin_list, t->pevent);
	pevent_free(t->pevent);
}

static struct event_format*
tp_format(const char *sys, const char *name)
{
	struct pevent *pevent = tevent.pevent;
	struct event_format *event = NULL;
	char path[PATH_MAX];
	size_t size;
	char *data;

	scnprintf(path, PATH_MAX, "%s/%s/%s/format",
		  tracing_events_path, sys, name);

	if (filename__read_str(path, &data, &size))
		return NULL;

	pevent_parse_format(pevent, &event, data, size, sys);

	free(data);
	return event;
}

struct event_format*
trace_event__tp_format(const char *sys, const char *name)
{
	static bool initialized;

	if (!initialized) {
		int be = traceevent_host_bigendian();
		struct pevent *pevent;

		if (trace_event__init(&tevent))
			return NULL;

		pevent = tevent.pevent;
		pevent_set_flag(pevent, PEVENT_NSEC_OUTPUT);
		pevent_set_file_bigendian(pevent, be);
		pevent_set_host_bigendian(pevent, be);
		initialized = true;
	}

	return tp_format(sys, name);
}
