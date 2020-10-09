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
 * The cnodes with high hit number are hot callchains.
 */
static void evsel_streams__set_hot_cnode(struct evsel_streams *es,
					 struct callchain_node *cnode)
{
	int i, idx = 0;
	u64 hit;

	if (es->nr_streams < es->nr_streams_max) {
		i = es->nr_streams;
		es->streams[i].cnode = cnode;
		es->nr_streams++;
		return;
	}

	/*
	 * Considering a few number of hot streams, only use simple
	 * way to find the cnode with smallest hit number and replace.
	 */
	hit = (es->streams[0].cnode)->hit;
	for (i = 1; i < es->nr_streams; i++) {
		if ((es->streams[i].cnode)->hit < hit) {
			hit = (es->streams[i].cnode)->hit;
			idx = i;
		}
	}

	if (cnode->hit > hit)
		es->streams[idx].cnode = cnode;
}

static void update_hot_callchain(struct hist_entry *he,
				 struct evsel_streams *es)
{
	struct rb_root *root = &he->sorted_chain;
	struct rb_node *rb_node = rb_first(root);
	struct callchain_node *cnode;

	while (rb_node) {
		cnode = rb_entry(rb_node, struct callchain_node, rb_node);
		evsel_streams__set_hot_cnode(es, cnode);
		rb_node = rb_next(rb_node);
	}
}

static void init_hot_callchain(struct hists *hists, struct evsel_streams *es)
{
	struct rb_node *next = rb_first_cached(&hists->entries);

	while (next) {
		struct hist_entry *he;

		he = rb_entry(next, struct hist_entry, rb_node);
		update_hot_callchain(he, es);
		next = rb_next(&he->rb_node);
	}
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
		es[i].evsel_idx = pos->idx;
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
