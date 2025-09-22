/*
 * testcode/unittcpreuse.c - unit test for tcp_reuse.
 *
 * Copyright (c) 2021, NLnet Labs. All rights reserved.
 *
 * This software is open source.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NLNET LABS nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**
 * \file
 * Tests the tcp_reuse functionality.
 */

#include "config.h"
#include "testcode/unitmain.h"
#include "util/log.h"
#include "util/random.h"
#include "services/outside_network.h"

#define MAX_TCP_WAITING_NODES 5

/** add number of new IDs to the reuse tree, randomly chosen */
static void tcpid_addmore(struct reuse_tcp* reuse,
	struct outside_network* outnet, unsigned int addnum)
{
	unsigned int i;
	struct waiting_tcp* w;
	for(i=0; i<addnum; i++) {
		uint16_t id = reuse_tcp_select_id(reuse, outnet);
		unit_assert(!reuse_tcp_by_id_find(reuse, id));
		w = calloc(1, sizeof(*w));
		unit_assert(w);
		w->id = id;
		w->outnet = outnet;
		w->next_waiting = (void*)reuse->pending;
		reuse_tree_by_id_insert(reuse, w);
	}
}

/** fill up the reuse ID tree and test assertions */
static void tcpid_fillup(struct reuse_tcp* reuse,
	struct outside_network* outnet)
{
	int t, numtest=3;
	for(t=0; t<numtest; t++) {
		rbtree_init(&reuse->tree_by_id, reuse_id_cmp);
		tcpid_addmore(reuse, outnet, 65535);
		reuse_del_readwait(&reuse->tree_by_id);
	}
}

/** test TCP ID selection */
static void tcpid_test(void)
{
	struct pending_tcp pend;
	struct outside_network outnet;
	unit_show_func("services/outside_network.c", "reuse_tcp_select_id");
	memset(&pend, 0, sizeof(pend));
	pend.reuse.pending = &pend;
	memset(&outnet, 0, sizeof(outnet));
	outnet.rnd = ub_initstate(NULL);
	rbtree_init(&pend.reuse.tree_by_id, reuse_id_cmp);
	tcpid_fillup(&pend.reuse, &outnet);
	ub_randfree(outnet.rnd);
}

/** check that the tree has present number of nodes and the LRU is linked
 * properly. */
static void check_tree_and_list(struct outside_network* outnet, int present)
{
	int i;
	struct reuse_tcp *reuse, *next_reuse;
	unit_assert(present == (int)outnet->tcp_reuse.count);
	if(present < 1) {
		unit_assert(outnet->tcp_reuse_first == NULL);
		unit_assert(outnet->tcp_reuse_last == NULL);
		return;
	}
	unit_assert(outnet->tcp_reuse_first->item_on_lru_list);
	unit_assert(!outnet->tcp_reuse_first->lru_prev);
	reuse = outnet->tcp_reuse_first;
	for(i=0; i<present-1; i++) {
		unit_assert(reuse->item_on_lru_list);
		unit_assert(reuse->lru_next);
		unit_assert(reuse->lru_next != reuse);
		next_reuse = reuse->lru_next;
		unit_assert(next_reuse->lru_prev == reuse);
		reuse = next_reuse;
	}
	unit_assert(!reuse->lru_next);
	unit_assert(outnet->tcp_reuse_last->item_on_lru_list);
	unit_assert(outnet->tcp_reuse_last == reuse);
}

/** creates pending_tcp. Copy of outside_network.c:create_pending_tcp without
 *  the comm_point creation */
static int create_pending_tcp(struct outside_network* outnet)
{
	size_t i;
	if(outnet->num_tcp == 0)
		return 1; /* no tcp needed, nothing to do */
	if(!(outnet->tcp_conns = (struct pending_tcp **)calloc(
			outnet->num_tcp, sizeof(struct pending_tcp*))))
		return 0;
	for(i=0; i<outnet->num_tcp; i++) {
		if(!(outnet->tcp_conns[i] = (struct pending_tcp*)calloc(1,
			sizeof(struct pending_tcp))))
			return 0;
		outnet->tcp_conns[i]->next_free = outnet->tcp_free;
		outnet->tcp_free = outnet->tcp_conns[i];
	}
	return 1;
}

/** empty the tcp_reuse tree and LRU list */
static void empty_tree(struct outside_network* outnet)
{
	size_t i;
	struct reuse_tcp* reuse;
	reuse = outnet->tcp_reuse_first;
	i = outnet->tcp_reuse.count;
	while(reuse) {
		reuse_tcp_remove_tree_list(outnet, reuse);
		check_tree_and_list(outnet, --i);
		reuse = outnet->tcp_reuse_first;
	}
}

/** check removal of the LRU element on the given position of total elements */
static void check_removal(struct outside_network* outnet, int position, int total)
{
	int i;
	struct reuse_tcp* reuse;
	empty_tree(outnet);
	for(i=0; i<total; i++) {
		reuse_tcp_insert(outnet, outnet->tcp_conns[i]);
	}
	check_tree_and_list(outnet, total);
	reuse = outnet->tcp_reuse_first;
	for(i=0; i<position; i++) reuse = reuse->lru_next;
	reuse_tcp_remove_tree_list(outnet, reuse);
	check_tree_and_list(outnet, total-1);
}

/** check snipping off the last element of the LRU with total elements */
static void check_snip(struct outside_network* outnet, int total)
{
	int i;
	struct reuse_tcp* reuse;
	empty_tree(outnet);
	for(i=0; i<total; i++) {
		reuse_tcp_insert(outnet, outnet->tcp_conns[i]);
	}
	check_tree_and_list(outnet, total);
	reuse = reuse_tcp_lru_snip(outnet);
	while(reuse) {
		reuse_tcp_remove_tree_list(outnet, reuse);
		check_tree_and_list(outnet, --total);
		reuse = reuse_tcp_lru_snip(outnet);
	}
	unit_assert(outnet->tcp_reuse_first == NULL);
	unit_assert(outnet->tcp_reuse_last == NULL);
	unit_assert(outnet->tcp_reuse.count == 0);
}

/** test tcp_reuse tree and LRU list functions */
static void tcp_reuse_tree_list_test(void)
{
	size_t i;
	struct outside_network outnet;
	struct reuse_tcp* reuse;
	memset(&outnet, 0, sizeof(outnet));
	rbtree_init(&outnet.tcp_reuse, reuse_cmp);
	outnet.num_tcp = 5;
	outnet.tcp_reuse_max = outnet.num_tcp;
	if(!create_pending_tcp(&outnet)) fatal_exit("out of memory");
	/* add all to the tree */
	unit_show_func("services/outside_network.c", "reuse_tcp_insert");
	for(i=0; i<outnet.num_tcp; i++) {
		reuse_tcp_insert(&outnet, outnet.tcp_conns[i]);
		check_tree_and_list(&outnet, i+1);
	}
	/* check touching */
	unit_show_func("services/outside_network.c", "reuse_tcp_lru_touch");
	for(i=0; i<outnet.tcp_reuse.count; i++) {
		for(reuse = outnet.tcp_reuse_first; reuse->lru_next; reuse = reuse->lru_next);
		reuse_tcp_lru_touch(&outnet, reuse);
		check_tree_and_list(&outnet, outnet.num_tcp);
	}
	/* check removal */
	unit_show_func("services/outside_network.c", "reuse_tcp_remove_tree_list");
	check_removal(&outnet, 2, 5);
	check_removal(&outnet, 1, 3);
	check_removal(&outnet, 1, 2);
	/* check snip */
	unit_show_func("services/outside_network.c", "reuse_tcp_lru_snip");
	check_snip(&outnet, 4);

	for(i=0; i<outnet.num_tcp; i++)
		if(outnet.tcp_conns[i]) {
			free(outnet.tcp_conns[i]);
		}
	free(outnet.tcp_conns);
}

static void check_waiting_tcp_list(struct outside_network* outnet,
	struct waiting_tcp* first, struct waiting_tcp* last, size_t total)
{
	size_t i, j;
	struct waiting_tcp* w = outnet->tcp_wait_first;
	struct waiting_tcp* n = NULL;
	if(first) unit_assert(outnet->tcp_wait_first == first);
	if(last) unit_assert(outnet->tcp_wait_last == last && !last->next_waiting);
	for(i=0; w; i++) {
		unit_assert(i<total); /* otherwise we are looping */
		unit_assert(w->on_tcp_waiting_list);
		n = w->next_waiting;
		for(j=0; n; j++) {
			unit_assert(j<total-i-1); /* otherwise we are looping */
			unit_assert(n != w);
			n = n->next_waiting;
		}
		w = w->next_waiting;
	}
}

/** clear the tcp waiting list */
static void waiting_tcp_list_clear(struct outside_network* outnet)
{
	struct waiting_tcp* w = outnet->tcp_wait_first, *n = NULL;
	if(!w) return;
	unit_assert(outnet->tcp_wait_first);
	unit_assert(outnet->tcp_wait_last);
	while(w) {
		n = w->next_waiting;
		w->on_tcp_waiting_list = 0;
		w->next_waiting = (struct waiting_tcp*)1; /* In purpose faux value */
		w = n;
	}
	outnet->tcp_wait_first = NULL;
	outnet->tcp_wait_last = NULL;
}

/** check removal of the waiting_tcp element on the given position of total
 *  elements */
static void check_waiting_tcp_removal(int is_pop,
	struct outside_network* outnet, struct waiting_tcp* store,
	size_t position, size_t total)
{
	size_t i;
	struct waiting_tcp* w;
	waiting_tcp_list_clear(outnet);
	for(i=0; i<total; i++) {
		outnet_waiting_tcp_list_add(outnet, &store[i], 0);
	}
	check_waiting_tcp_list(outnet, &store[0], &store[total-1], total);

	if(is_pop) {
		w = outnet_waiting_tcp_list_pop(outnet);
		unit_assert(w); /* please clang-analyser */
	} else {
		w = outnet->tcp_wait_first;
		for(i=0; i<position; i++) {
			unit_assert(w); /* please clang-analyser */
			w = w->next_waiting;
		}
		unit_assert(w); /* please clang-analyser */
		outnet_waiting_tcp_list_remove(outnet, w);
	}
	unit_assert(!(w->on_tcp_waiting_list || w->next_waiting));

	if(position == 0 && total == 1) {
		/* the list should be empty */
		check_waiting_tcp_list(outnet, NULL, NULL, total-1);
	} else if(position == 0) {
		/* first element should be gone */
		check_waiting_tcp_list(outnet, &store[1], &store[total-1], total-1);
	} else if(position == total - 1) {
		/* last element should be gone */
		check_waiting_tcp_list(outnet, &store[0], &store[total-2], total-1);
	} else {
		/* an element should be gone */
		check_waiting_tcp_list(outnet, &store[0], &store[total-1], total-1);
	}
}

static void waiting_tcp_list_test(void)
{
	size_t i = 0;
	struct outside_network outnet;
	struct waiting_tcp* w, *t = NULL;
	struct waiting_tcp store[MAX_TCP_WAITING_NODES];
	memset(&outnet, 0, sizeof(outnet));
	memset(&store, 0, sizeof(store));

	/* Check add first on empty list */
	unit_show_func("services/outside_network.c", "outnet_waiting_tcp_list_add_first");
	t = &store[i];
	outnet_waiting_tcp_list_add_first(&outnet, t, 0);
	check_waiting_tcp_list(&outnet, t, t, 1);

	/* Check add */
	unit_show_func("services/outside_network.c", "outnet_waiting_tcp_list_add");
	for(i=1; i<MAX_TCP_WAITING_NODES-1; i++) {
		w = &store[i];
		outnet_waiting_tcp_list_add(&outnet, w, 0);
	}
	check_waiting_tcp_list(&outnet, t, w, MAX_TCP_WAITING_NODES-1);

	/* Check add first on populated list */
	unit_show_func("services/outside_network.c", "outnet_waiting_tcp_list_add_first");
	w = &store[i];
	t = outnet.tcp_wait_last;
	outnet_waiting_tcp_list_add_first(&outnet, w, 0);
	check_waiting_tcp_list(&outnet, w, t, MAX_TCP_WAITING_NODES);

	/* Check removal */
	unit_show_func("services/outside_network.c", "outnet_waiting_tcp_list_remove");
	check_waiting_tcp_removal(0, &outnet, store, 2, 5);
	check_waiting_tcp_removal(0, &outnet, store, 1, 3);
	check_waiting_tcp_removal(0, &outnet, store, 0, 2);
	check_waiting_tcp_removal(0, &outnet, store, 1, 2);
	check_waiting_tcp_removal(0, &outnet, store, 0, 1);

	/* Check pop */
	unit_show_func("services/outside_network.c", "outnet_waiting_tcp_list_pop");
	check_waiting_tcp_removal(1, &outnet, store, 0, 3);
	check_waiting_tcp_removal(1, &outnet, store, 0, 2);
	check_waiting_tcp_removal(1, &outnet, store, 0, 1);
}

static void check_reuse_write_wait(struct reuse_tcp* reuse,
	struct waiting_tcp* first, struct waiting_tcp* last, size_t total)
{
	size_t i, j;
	struct waiting_tcp* w = reuse->write_wait_first;
	struct waiting_tcp* n = NULL;
	if(first) unit_assert(reuse->write_wait_first == first && !first->write_wait_prev);
	if(last) unit_assert(reuse->write_wait_last == last && !last->write_wait_next);
	/* check one way */
	for(i=0; w; i++) {
		unit_assert(i<total); /* otherwise we are looping */
		unit_assert(w->write_wait_queued);
		n = w->write_wait_next;
		for(j=0; n; j++) {
			unit_assert(j<total-i-1); /* otherwise we are looping */
			unit_assert(n != w);
			n = n->write_wait_next;
		}
		w = w->write_wait_next;
	}
	/* check the other way */
	w = reuse->write_wait_last;
	for(i=0; w; i++) {
		unit_assert(i<total); /* otherwise we are looping */
		unit_assert(w->write_wait_queued);
		n = w->write_wait_prev;
		for(j=0; n; j++) {
			unit_assert(j<total-i-1); /* otherwise we are looping */
			unit_assert(n != w);
			n = n->write_wait_prev;
		}
		w = w->write_wait_prev;
	}
}

/** clear the tcp waiting list */
static void reuse_write_wait_clear(struct reuse_tcp* reuse)
{
	struct waiting_tcp* w = reuse->write_wait_first, *n = NULL;
	if(!w) return;
	unit_assert(reuse->write_wait_first);
	unit_assert(reuse->write_wait_last);
	while(w) {
		n = w->write_wait_next;
		w->write_wait_queued = 0;
		w->write_wait_next = (struct waiting_tcp*)1;  /* In purpose faux value */
		w->write_wait_prev = (struct waiting_tcp*)1;  /* In purpose faux value */
		w = n;
	}
	reuse->write_wait_first = NULL;
	reuse->write_wait_last = NULL;
}

/** check removal of the reuse_write_wait element on the given position of total
 *  elements */
static void check_reuse_write_wait_removal(int is_pop,
	struct reuse_tcp* reuse, struct waiting_tcp* store,
	size_t position, size_t total)
{
	size_t i;
	struct waiting_tcp* w;
	reuse_write_wait_clear(reuse);
	for(i=0; i<total; i++) {
		reuse_write_wait_push_back(reuse, &store[i]);
	}
	check_reuse_write_wait(reuse, &store[0], &store[total-1], total);

	if(is_pop) {
		w = reuse_write_wait_pop(reuse);
	} else {
		w = reuse->write_wait_first;
		for(i=0; i<position; i++) w = w->write_wait_next;
		reuse_write_wait_remove(reuse, w);
	}
	unit_assert(!(w->write_wait_queued || w->write_wait_next || w->write_wait_prev));

	if(position == 0 && total == 1) {
		/* the list should be empty */
		check_reuse_write_wait(reuse, NULL, NULL, total-1);
	} else if(position == 0) {
		/* first element should be gone */
		check_reuse_write_wait(reuse, &store[1], &store[total-1], total-1);
	} else if(position == total - 1) {
		/* last element should be gone */
		check_reuse_write_wait(reuse, &store[0], &store[total-2], total-1);
	} else {
		/* an element should be gone */
		check_reuse_write_wait(reuse, &store[0], &store[total-1], total-1);
	}
}

static void reuse_write_wait_test(void)
{
	size_t i;
	struct reuse_tcp reuse;
	struct waiting_tcp store[MAX_TCP_WAITING_NODES];
	struct waiting_tcp* w;
	memset(&reuse, 0, sizeof(reuse));
	memset(&store, 0, sizeof(store));

	/* Check adding */
	unit_show_func("services/outside_network.c", "reuse_write_wait_push_back");
	for(i=0; i<MAX_TCP_WAITING_NODES; i++) {
		w = &store[i];
		reuse_write_wait_push_back(&reuse, w);
	}
	check_reuse_write_wait(&reuse, &store[0], w, MAX_TCP_WAITING_NODES);

	/* Check removal */
	unit_show_func("services/outside_network.c", "reuse_write_wait_remove");
	check_reuse_write_wait_removal(0, &reuse, store, 2, 5);
	check_reuse_write_wait_removal(0, &reuse, store, 1, 3);
	check_reuse_write_wait_removal(0, &reuse, store, 0, 2);
	check_reuse_write_wait_removal(0, &reuse, store, 1, 2);
	check_reuse_write_wait_removal(0, &reuse, store, 0, 1);

	/* Check pop */
	unit_show_func("services/outside_network.c", "reuse_write_wait_pop");
	check_reuse_write_wait_removal(1, &reuse, store, 0, 3);
	check_reuse_write_wait_removal(1, &reuse, store, 0, 2);
	check_reuse_write_wait_removal(1, &reuse, store, 0, 1);
}

void tcpreuse_test(void)
{
    unit_show_feature("tcp_reuse");
    tcpid_test();
    tcp_reuse_tree_list_test();
    waiting_tcp_list_test();
    reuse_write_wait_test();
}
