// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
#ifndef __PERF_EVSWITCH_H
#define __PERF_EVSWITCH_H 1

#include <stdbool.h>
#include <stdio.h>

struct evsel;
struct evlist;

struct evswitch {
	struct evsel *on, *off;
	const char   *on_name, *off_name;
	bool	     discarding;
	bool	     show_on_off_events;
};

int evswitch__init(struct evswitch *evswitch, struct evlist *evlist, FILE *fp);

bool evswitch__discard(struct evswitch *evswitch, struct evsel *evsel);

#define OPTS_EVSWITCH(evswitch)								  \
	OPT_STRING(0, "switch-on", &(evswitch)->on_name,				  \
		   "event", "Consider events after the ocurrence of this event"),	  \
	OPT_STRING(0, "switch-off", &(evswitch)->off_name,				  \
		   "event", "Stop considering events after the ocurrence of this event"), \
	OPT_BOOLEAN(0, "show-on-off-events", &(evswitch)->show_on_off_events,		  \
		    "Show the on/off switch events, used with --switch-on and --switch-off")

#endif /* __PERF_EVSWITCH_H */
