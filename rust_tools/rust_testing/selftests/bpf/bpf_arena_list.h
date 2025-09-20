/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#pragma once
#include "bpf_arena_common.h"

struct arena_list_node;

typedef struct arena_list_node __arena arena_list_node_t;

struct arena_list_node {
	arena_list_node_t *next;
	arena_list_node_t * __arena *pprev;
};

struct arena_list_head {
	struct arena_list_node __arena *first;
};
typedef struct arena_list_head __arena arena_list_head_t;

#define list_entry(ptr, type, member) arena_container_of(ptr, type, member)

#define list_entry_safe(ptr, type, member) \
	({ typeof(*ptr) * ___ptr = (ptr); \
	 ___ptr ? ({ cast_kern(___ptr); list_entry(___ptr, type, member); }) : NULL; \
	 })

#ifndef __BPF__
static inline void *bpf_iter_num_new(struct bpf_iter_num *it, int i, int j) { return NULL; }
static inline void bpf_iter_num_destroy(struct bpf_iter_num *it) {}
static inline bool bpf_iter_num_next(struct bpf_iter_num *it) { return true; }
#define cond_break ({})
#define can_loop true
#endif

/* Safely walk link list elements. Deletion of elements is allowed. */
#define list_for_each_entry(pos, head, member)					\
	for (void * ___tmp = (pos = list_entry_safe((head)->first,		\
						    typeof(*(pos)), member),	\
			      (void *)0);					\
	     pos && ({ ___tmp = (void *)pos->member.next; 1; }) && can_loop;    \
	     pos = list_entry_safe((void __arena *)___tmp, typeof(*(pos)), member))

static inline void list_add_head(arena_list_node_t *n, arena_list_head_t *h)
{
	arena_list_node_t *first = h->first, * __arena *tmp;

	cast_user(first);
	cast_kern(n);
	WRITE_ONCE(n->next, first);
	cast_kern(first);
	if (first) {
		tmp = &n->next;
		cast_user(tmp);
		WRITE_ONCE(first->pprev, tmp);
	}
	cast_user(n);
	WRITE_ONCE(h->first, n);

	tmp = &h->first;
	cast_user(tmp);
	cast_kern(n);
	WRITE_ONCE(n->pprev, tmp);
}

static inline void __list_del(arena_list_node_t *n)
{
	arena_list_node_t *next = n->next, *tmp;
	arena_list_node_t * __arena *pprev = n->pprev;

	cast_user(next);
	cast_kern(pprev);
	tmp = *pprev;
	cast_kern(tmp);
	WRITE_ONCE(tmp, next);
	if (next) {
		cast_user(pprev);
		cast_kern(next);
		WRITE_ONCE(next->pprev, pprev);
	}
}

#define POISON_POINTER_DELTA 0

#define LIST_POISON1  ((void __arena *) 0x100 + POISON_POINTER_DELTA)
#define LIST_POISON2  ((void __arena *) 0x122 + POISON_POINTER_DELTA)

static inline void list_del(arena_list_node_t *n)
{
	__list_del(n);
	n->next = LIST_POISON1;
	n->pprev = LIST_POISON2;
}
