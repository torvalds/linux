// SPDX-License-Identifier: GPL-2.0-only
// Copyright (C) 2019, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>

#include "evswitch.h"

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
