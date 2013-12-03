
#include <traceevent/event-parse.h>
#include "trace-event.h"

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
	pevent_free(t->pevent);
	traceevent_unload_plugins(t->plugin_list);
}
