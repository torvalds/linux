// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
#ifndef __PERF_EVSWITCH_H
#define __PERF_EVSWITCH_H 1

#include <stdbool.h>

struct evsel;

struct evswitch {
	struct evsel *on, *off;
	bool	     discarding;
	bool	     show_on_off_events;
};

#endif /* __PERF_EVSWITCH_H */
