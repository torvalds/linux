/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_STREAM_H
#define __PERF_STREAM_H

struct callchain_node;
struct evlist;
struct evsel;

struct stream {
	struct callchain_node	*cnode;
	struct callchain_node	*pair_cnode;
};

struct evsel_streams {
	struct stream		*streams;
	const struct evsel	*evsel;
	int			nr_streams_max;
	int			nr_streams;
	u64			streams_hits;
};

struct evlist_streams {
	struct evsel_streams	*ev_streams;
	int			nr_evsel;
};

void evlist_streams__delete(struct evlist_streams *els);

struct evlist_streams *evlist__create_streams(struct evlist *evlist,
					      int nr_streams_max);

struct evsel_streams *evsel_streams__entry(struct evlist_streams *els,
					   const struct evsel *evsel);

void evsel_streams__match(struct evsel_streams *es_base,
			  struct evsel_streams *es_pair);

void evsel_streams__report(struct evsel_streams *es_base,
			   struct evsel_streams *es_pair);

#endif /* __PERF_STREAM_H */
