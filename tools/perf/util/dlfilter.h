/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dlfilter.h: Interface to perf script --dlfilter shared object
 * Copyright (c) 2021, Intel Corporation.
 */

#ifndef PERF_UTIL_DLFILTER_H
#define PERF_UTIL_DLFILTER_H

struct perf_session;
union  perf_event;
struct perf_sample;
struct evsel;
struct machine;
struct addr_location;
struct perf_dlfilter_fns;
struct perf_dlfilter_sample;
struct perf_dlfilter_al;

struct dlfilter {
	char				*file;
	void				*handle;
	void				*data;
	struct perf_session		*session;
	bool				ctx_valid;
	bool				in_start;
	bool				in_stop;
	int				dlargc;
	char				**dlargv;

	union perf_event		*event;
	struct perf_sample		*sample;
	struct evsel			*evsel;
	struct machine			*machine;
	struct addr_location		*al;
	struct addr_location		*addr_al;
	struct perf_dlfilter_sample	*d_sample;
	struct perf_dlfilter_al		*d_ip_al;
	struct perf_dlfilter_al		*d_addr_al;

	int (*start)(void **data, void *ctx);
	int (*stop)(void *data, void *ctx);

	int (*filter_event)(void *data,
			    const struct perf_dlfilter_sample *sample,
			    void *ctx);
	int (*filter_event_early)(void *data,
				  const struct perf_dlfilter_sample *sample,
				  void *ctx);

	struct perf_dlfilter_fns *fns;
};

struct dlfilter *dlfilter__new(const char *file, int dlargc, char **dlargv);

int dlfilter__start(struct dlfilter *d, struct perf_session *session);

int dlfilter__do_filter_event(struct dlfilter *d,
			      union perf_event *event,
			      struct perf_sample *sample,
			      struct evsel *evsel,
			      struct machine *machine,
			      struct addr_location *al,
			      struct addr_location *addr_al,
			      bool early);

void dlfilter__cleanup(struct dlfilter *d);

static inline int dlfilter__filter_event(struct dlfilter *d,
					 union perf_event *event,
					 struct perf_sample *sample,
					 struct evsel *evsel,
					 struct machine *machine,
					 struct addr_location *al,
					 struct addr_location *addr_al)
{
	if (!d || !d->filter_event)
		return 0;
	return dlfilter__do_filter_event(d, event, sample, evsel, machine, al, addr_al, false);
}

static inline int dlfilter__filter_event_early(struct dlfilter *d,
					       union perf_event *event,
					       struct perf_sample *sample,
					       struct evsel *evsel,
					       struct machine *machine,
					       struct addr_location *al,
					       struct addr_location *addr_al)
{
	if (!d || !d->filter_event_early)
		return 0;
	return dlfilter__do_filter_event(d, event, sample, evsel, machine, al, addr_al, true);
}

int list_available_dlfilters(const struct option *opt, const char *s, int unset);

#endif
