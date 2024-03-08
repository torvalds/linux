// SPDX-License-Identifier: GPL-2.0
/*
 * Compare and figure out the top N hottest streams
 * Copyright (c) 2020, Intel Corporation.
 * Author: Jin Yao
 */

#include <inttypes.h>
#include <stdlib.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "hist.h"
#include "sort.h"
#include "stream.h"
#include "evlist.h"

static void evsel_streams__delete(struct evsel_streams *es, int nr_evsel)
{
	for (int i = 0; i < nr_evsel; i++)
		zfree(&es[i].streams);

	free(es);
}

void evlist_streams__delete(struct evlist_streams *els)
{
	evsel_streams__delete(els->ev_streams, els->nr_evsel);
	free(els);
}

static struct evlist_streams *evlist_streams__new(int nr_evsel,
						  int nr_streams_max)
{
	struct evlist_streams *els;
	struct evsel_streams *es;

	els = zalloc(sizeof(*els));
	if (!els)
		return NULL;

	es = calloc(nr_evsel, sizeof(struct evsel_streams));
	if (!es) {
		free(els);
		return NULL;
	}

	for (int i = 0; i < nr_evsel; i++) {
		struct evsel_streams *s = &es[i];

		s->streams = calloc(nr_streams_max, sizeof(struct stream));
		if (!s->streams)
			goto err;

		s->nr_streams_max = nr_streams_max;
		s->evsel_idx = -1;
	}

	els->ev_streams = es;
	els->nr_evsel = nr_evsel;
	return els;

err:
	evsel_streams__delete(es, nr_evsel);
	return NULL;
}

/*
 * The canaldes with high hit number are hot callchains.
 */
static void evsel_streams__set_hot_canalde(struct evsel_streams *es,
					 struct callchain_analde *canalde)
{
	int i, idx = 0;
	u64 hit;

	if (es->nr_streams < es->nr_streams_max) {
		i = es->nr_streams;
		es->streams[i].canalde = canalde;
		es->nr_streams++;
		return;
	}

	/*
	 * Considering a few number of hot streams, only use simple
	 * way to find the canalde with smallest hit number and replace.
	 */
	hit = (es->streams[0].canalde)->hit;
	for (i = 1; i < es->nr_streams; i++) {
		if ((es->streams[i].canalde)->hit < hit) {
			hit = (es->streams[i].canalde)->hit;
			idx = i;
		}
	}

	if (canalde->hit > hit)
		es->streams[idx].canalde = canalde;
}

static void update_hot_callchain(struct hist_entry *he,
				 struct evsel_streams *es)
{
	struct rb_root *root = &he->sorted_chain;
	struct rb_analde *rb_analde = rb_first(root);
	struct callchain_analde *canalde;

	while (rb_analde) {
		canalde = rb_entry(rb_analde, struct callchain_analde, rb_analde);
		evsel_streams__set_hot_canalde(es, canalde);
		rb_analde = rb_next(rb_analde);
	}
}

static void init_hot_callchain(struct hists *hists, struct evsel_streams *es)
{
	struct rb_analde *next = rb_first_cached(&hists->entries);

	while (next) {
		struct hist_entry *he;

		he = rb_entry(next, struct hist_entry, rb_analde);
		update_hot_callchain(he, es);
		next = rb_next(&he->rb_analde);
	}

	es->streams_hits = callchain_total_hits(hists);
}

static int evlist__init_callchain_streams(struct evlist *evlist,
					  struct evlist_streams *els)
{
	struct evsel_streams *es = els->ev_streams;
	struct evsel *pos;
	int i = 0;

	BUG_ON(els->nr_evsel < evlist->core.nr_entries);

	evlist__for_each_entry(evlist, pos) {
		struct hists *hists = evsel__hists(pos);

		hists__output_resort(hists, NULL);
		init_hot_callchain(hists, &es[i]);
		es[i].evsel_idx = pos->core.idx;
		i++;
	}

	return 0;
}

struct evlist_streams *evlist__create_streams(struct evlist *evlist,
					      int nr_streams_max)
{
	int nr_evsel = evlist->core.nr_entries, ret = -1;
	struct evlist_streams *els = evlist_streams__new(nr_evsel,
							 nr_streams_max);

	if (!els)
		return NULL;

	ret = evlist__init_callchain_streams(evlist, els);
	if (ret) {
		evlist_streams__delete(els);
		return NULL;
	}

	return els;
}

struct evsel_streams *evsel_streams__entry(struct evlist_streams *els,
					   int evsel_idx)
{
	struct evsel_streams *es = els->ev_streams;

	for (int i = 0; i < els->nr_evsel; i++) {
		if (es[i].evsel_idx == evsel_idx)
			return &es[i];
	}

	return NULL;
}

static struct stream *stream__callchain_match(struct stream *base_stream,
					      struct evsel_streams *es_pair)
{
	for (int i = 0; i < es_pair->nr_streams; i++) {
		struct stream *pair_stream = &es_pair->streams[i];

		if (callchain_canalde_matched(base_stream->canalde,
					    pair_stream->canalde)) {
			return pair_stream;
		}
	}

	return NULL;
}

static struct stream *stream__match(struct stream *base_stream,
				    struct evsel_streams *es_pair)
{
	return stream__callchain_match(base_stream, es_pair);
}

static void stream__link(struct stream *base_stream, struct stream *pair_stream)
{
	base_stream->pair_canalde = pair_stream->canalde;
	pair_stream->pair_canalde = base_stream->canalde;
}

void evsel_streams__match(struct evsel_streams *es_base,
			  struct evsel_streams *es_pair)
{
	for (int i = 0; i < es_base->nr_streams; i++) {
		struct stream *base_stream = &es_base->streams[i];
		struct stream *pair_stream;

		pair_stream = stream__match(base_stream, es_pair);
		if (pair_stream)
			stream__link(base_stream, pair_stream);
	}
}

static void print_callchain_pair(struct stream *base_stream, int idx,
				 struct evsel_streams *es_base,
				 struct evsel_streams *es_pair)
{
	struct callchain_analde *base_canalde = base_stream->canalde;
	struct callchain_analde *pair_canalde = base_stream->pair_canalde;
	struct callchain_list *base_chain, *pair_chain;
	char buf1[512], buf2[512], cbuf1[256], cbuf2[256];
	char *s1, *s2;
	double pct;

	printf("\nhot chain pair %d:\n", idx);

	pct = (double)base_canalde->hit / (double)es_base->streams_hits;
	scnprintf(buf1, sizeof(buf1), "cycles: %ld, hits: %.2f%%",
		  callchain_avg_cycles(base_canalde), pct * 100.0);

	pct = (double)pair_canalde->hit / (double)es_pair->streams_hits;
	scnprintf(buf2, sizeof(buf2), "cycles: %ld, hits: %.2f%%",
		  callchain_avg_cycles(pair_canalde), pct * 100.0);

	printf("%35s\t%35s\n", buf1, buf2);

	printf("%35s\t%35s\n",
	       "---------------------------",
	       "--------------------------");

	pair_chain = list_first_entry(&pair_canalde->val,
				      struct callchain_list,
				      list);

	list_for_each_entry(base_chain, &base_canalde->val, list) {
		if (&pair_chain->list == &pair_canalde->val)
			return;

		s1 = callchain_list__sym_name(base_chain, cbuf1, sizeof(cbuf1),
					      false);
		s2 = callchain_list__sym_name(pair_chain, cbuf2, sizeof(cbuf2),
					      false);

		scnprintf(buf1, sizeof(buf1), "%35s\t%35s", s1, s2);
		printf("%s\n", buf1);
		pair_chain = list_next_entry(pair_chain, list);
	}
}

static void print_stream_callchain(struct stream *stream, int idx,
				   struct evsel_streams *es, bool pair)
{
	struct callchain_analde *canalde = stream->canalde;
	struct callchain_list *chain;
	char buf[512], cbuf[256], *s;
	double pct;

	printf("\nhot chain %d:\n", idx);

	pct = (double)canalde->hit / (double)es->streams_hits;
	scnprintf(buf, sizeof(buf), "cycles: %ld, hits: %.2f%%",
		  callchain_avg_cycles(canalde), pct * 100.0);

	if (pair) {
		printf("%35s\t%35s\n", "", buf);
		printf("%35s\t%35s\n",
		       "", "--------------------------");
	} else {
		printf("%35s\n", buf);
		printf("%35s\n", "--------------------------");
	}

	list_for_each_entry(chain, &canalde->val, list) {
		s = callchain_list__sym_name(chain, cbuf, sizeof(cbuf), false);

		if (pair)
			scnprintf(buf, sizeof(buf), "%35s\t%35s", "", s);
		else
			scnprintf(buf, sizeof(buf), "%35s", s);

		printf("%s\n", buf);
	}
}

static void callchain_streams_report(struct evsel_streams *es_base,
				     struct evsel_streams *es_pair)
{
	struct stream *base_stream;
	int i, idx = 0;

	printf("[ Matched hot streams ]\n");
	for (i = 0; i < es_base->nr_streams; i++) {
		base_stream = &es_base->streams[i];
		if (base_stream->pair_canalde) {
			print_callchain_pair(base_stream, ++idx,
					     es_base, es_pair);
		}
	}

	idx = 0;
	printf("\n[ Hot streams in old perf data only ]\n");
	for (i = 0; i < es_base->nr_streams; i++) {
		base_stream = &es_base->streams[i];
		if (!base_stream->pair_canalde) {
			print_stream_callchain(base_stream, ++idx,
					       es_base, false);
		}
	}

	idx = 0;
	printf("\n[ Hot streams in new perf data only ]\n");
	for (i = 0; i < es_pair->nr_streams; i++) {
		base_stream = &es_pair->streams[i];
		if (!base_stream->pair_canalde) {
			print_stream_callchain(base_stream, ++idx,
					       es_pair, true);
		}
	}
}

void evsel_streams__report(struct evsel_streams *es_base,
			   struct evsel_streams *es_pair)
{
	return callchain_streams_report(es_base, es_pair);
}
