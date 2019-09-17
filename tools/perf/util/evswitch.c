// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "evswitch.h"
#include "evlist.h"

bool evswitch__discard(struct evswitch *evswitch, struct evsel *evsel)
{
	if (evswitch->on && evswitch->discarding) {
		if (evswitch->on != evsel)
			return true;

		evswitch->discarding = false;

		if (!evswitch->show_on_off_events)
			return true;

		return false;
	}

	if (evswitch->off && !evswitch->discarding) {
		if (evswitch->off != evsel)
			return false;

		evswitch->discarding = true;

		if (!evswitch->show_on_off_events)
			return true;
	}

	return false;
}

static int evswitch__fprintf_enoent(FILE *fp, const char *evtype, const char *evname)
{
	int printed = fprintf(fp, "ERROR: switch-%s event not found (%s)\n", evtype, evname);

	return printed += fprintf(fp, "HINT:  use 'perf evlist' to see the available event names\n");
}

int evswitch__init(struct evswitch *evswitch, struct evlist *evlist, FILE *fp)
{
	if (evswitch->on_name) {
		evswitch->on = perf_evlist__find_evsel_by_str(evlist, evswitch->on_name);
		if (evswitch->on == NULL) {
			evswitch__fprintf_enoent(fp, "on", evswitch->on_name);
			return -ENOENT;
		}
		evswitch->discarding = true;
	}

	if (evswitch->off_name) {
		evswitch->off = perf_evlist__find_evsel_by_str(evlist, evswitch->off_name);
		if (evswitch->off == NULL) {
			evswitch__fprintf_enoent(fp, "off", evswitch->off_name);
			return -ENOENT;
		}
	}

	return 0;
}
