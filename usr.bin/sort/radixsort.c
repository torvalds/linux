/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
 * Copyright (C) 2012 Gabor Kovesdan <gabor@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <err.h>
#include <langinfo.h>
#include <math.h>
#if defined(SORT_THREADS)
#include <pthread.h>
#include <semaphore.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>
#include <unistd.h>

#include "coll.h"
#include "radixsort.h"

#define DEFAULT_SORT_FUNC_RADIXSORT mergesort

#define TINY_NODE(sl) ((sl)->tosort_num < 65)
#define SMALL_NODE(sl) ((sl)->tosort_num < 5)

/* are we sorting in reverse order ? */
static bool reverse_sort;

/* sort sub-levels array size */
static const size_t slsz = 256 * sizeof(struct sort_level*);

/* one sort level structure */
struct sort_level
{
	struct sort_level	**sublevels;
	struct sort_list_item	**leaves;
	struct sort_list_item	**sorted;
	struct sort_list_item	**tosort;
	size_t			  leaves_num;
	size_t			  leaves_sz;
	size_t			  level;
	size_t			  real_sln;
	size_t			  start_position;
	size_t			  sln;
	size_t			  tosort_num;
	size_t			  tosort_sz;
};

/* stack of sort levels ready to be sorted */
struct level_stack {
	struct level_stack	 *next;
	struct sort_level	 *sl;
};

static struct level_stack *g_ls;

#if defined(SORT_THREADS)
/* stack guarding mutex */
static pthread_cond_t g_ls_cond;
static pthread_mutex_t g_ls_mutex;

/* counter: how many items are left */
static size_t sort_left;
/* guarding mutex */

/* semaphore to count threads */
static sem_t mtsem;

/*
 * Decrement items counter
 */
static inline void
sort_left_dec(size_t n)
{
	pthread_mutex_lock(&g_ls_mutex);
	sort_left -= n;
	if (sort_left == 0 && nthreads > 1)
		pthread_cond_broadcast(&g_ls_cond);
	pthread_mutex_unlock(&g_ls_mutex);
}

/*
 * Do we have something to sort ?
 *
 * This routine does not need to be locked.
 */
static inline bool
have_sort_left(void)
{
	bool ret;

	ret = (sort_left > 0);

	return (ret);
}

#else

#define sort_left_dec(n)

#endif /* SORT_THREADS */

static void
_push_ls(struct level_stack *ls)
{

	ls->next = g_ls;
	g_ls = ls;
}

/*
 * Push sort level to the stack
 */
static inline void
push_ls(struct sort_level *sl)
{
	struct level_stack *new_ls;

	new_ls = sort_malloc(sizeof(struct level_stack));
	new_ls->sl = sl;

#if defined(SORT_THREADS)
	if (nthreads > 1) {
		pthread_mutex_lock(&g_ls_mutex);
		_push_ls(new_ls);
		pthread_cond_signal(&g_ls_cond);
		pthread_mutex_unlock(&g_ls_mutex);
	} else
#endif
		_push_ls(new_ls);
}

/*
 * Pop sort level from the stack (single-threaded style)
 */
static inline struct sort_level*
pop_ls_st(void)
{
	struct sort_level *sl;

	if (g_ls) {
		struct level_stack *saved_ls;

		sl = g_ls->sl;
		saved_ls = g_ls;
		g_ls = g_ls->next;
		sort_free(saved_ls);
	} else
		sl = NULL;

	return (sl);
}

#if defined(SORT_THREADS)

/*
 * Pop sort level from the stack (multi-threaded style)
 */
static inline struct sort_level*
pop_ls_mt(void)
{
	struct level_stack *saved_ls;
	struct sort_level *sl;

	pthread_mutex_lock(&g_ls_mutex);

	for (;;) {
		if (g_ls) {
			sl = g_ls->sl;
			saved_ls = g_ls;
			g_ls = g_ls->next;
			break;
		}
		sl = NULL;
		saved_ls = NULL;

		if (have_sort_left() == 0)
			break;
		pthread_cond_wait(&g_ls_cond, &g_ls_mutex);
	}

	pthread_mutex_unlock(&g_ls_mutex);

	sort_free(saved_ls);

	return (sl);
}

#endif /* defined(SORT_THREADS) */

static void
add_to_sublevel(struct sort_level *sl, struct sort_list_item *item, size_t indx)
{
	struct sort_level *ssl;

	ssl = sl->sublevels[indx];

	if (ssl == NULL) {
		ssl = sort_malloc(sizeof(struct sort_level));
		memset(ssl, 0, sizeof(struct sort_level));

		ssl->level = sl->level + 1;
		sl->sublevels[indx] = ssl;

		++(sl->real_sln);
	}

	if (++(ssl->tosort_num) > ssl->tosort_sz) {
		ssl->tosort_sz = ssl->tosort_num + 128;
		ssl->tosort = sort_realloc(ssl->tosort,
		    sizeof(struct sort_list_item*) * (ssl->tosort_sz));
	}

	ssl->tosort[ssl->tosort_num - 1] = item;
}

static inline void
add_leaf(struct sort_level *sl, struct sort_list_item *item)
{

	if (++(sl->leaves_num) > sl->leaves_sz) {
		sl->leaves_sz = sl->leaves_num + 128;
		sl->leaves = sort_realloc(sl->leaves,
		    (sizeof(struct sort_list_item*) * (sl->leaves_sz)));
	}
	sl->leaves[sl->leaves_num - 1] = item;
}

static inline int
get_wc_index(struct sort_list_item *sli, size_t level)
{
	const struct key_value *kv;
	const struct bwstring *bws;

	kv = get_key_from_keys_array(&sli->ka, 0);
	bws = kv->k;

	if ((BWSLEN(bws) > level))
		return (unsigned char) BWS_GET(bws,level);
	return (-1);
}

static void
place_item(struct sort_level *sl, size_t item)
{
	struct sort_list_item *sli;
	int c;

	sli = sl->tosort[item];
	c = get_wc_index(sli, sl->level);

	if (c == -1)
		add_leaf(sl, sli);
	else
		add_to_sublevel(sl, sli, c);
}

static void
free_sort_level(struct sort_level *sl)
{

	if (sl) {
		if (sl->leaves)
			sort_free(sl->leaves);

		if (sl->level > 0)
			sort_free(sl->tosort);

		if (sl->sublevels) {
			struct sort_level *slc;
			size_t sln;

			sln = sl->sln;

			for (size_t i = 0; i < sln; ++i) {
				slc = sl->sublevels[i];
				if (slc)
					free_sort_level(slc);
			}

			sort_free(sl->sublevels);
		}

		sort_free(sl);
	}
}

static void
run_sort_level_next(struct sort_level *sl)
{
	struct sort_level *slc;
	size_t i, sln, tosort_num;

	if (sl->sublevels) {
		sort_free(sl->sublevels);
		sl->sublevels = NULL;
	}

	switch (sl->tosort_num) {
	case 0:
		goto end;
	case (1):
		sl->sorted[sl->start_position] = sl->tosort[0];
		sort_left_dec(1);
		goto end;
	case (2):
		if (list_coll_offset(&(sl->tosort[0]), &(sl->tosort[1]),
		    sl->level) > 0) {
			sl->sorted[sl->start_position++] = sl->tosort[1];
			sl->sorted[sl->start_position] = sl->tosort[0];
		} else {
			sl->sorted[sl->start_position++] = sl->tosort[0];
			sl->sorted[sl->start_position] = sl->tosort[1];
		}
		sort_left_dec(2);

		goto end;
	default:
		if (TINY_NODE(sl) || (sl->level > 15)) {
			listcoll_t func;

			func = get_list_call_func(sl->level);

			sl->leaves = sl->tosort;
			sl->leaves_num = sl->tosort_num;
			sl->leaves_sz = sl->leaves_num;
			sl->leaves = sort_realloc(sl->leaves,
			    (sizeof(struct sort_list_item *) *
			    (sl->leaves_sz)));
			sl->tosort = NULL;
			sl->tosort_num = 0;
			sl->tosort_sz = 0;
			sl->sln = 0;
			sl->real_sln = 0;
			if (sort_opts_vals.sflag) {
				if (mergesort(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) func) == -1)
					/* NOTREACHED */
					err(2, "Radix sort error 3");
			} else
				DEFAULT_SORT_FUNC_RADIXSORT(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) func);

			memcpy(sl->sorted + sl->start_position,
			    sl->leaves, sl->leaves_num *
			    sizeof(struct sort_list_item*));

			sort_left_dec(sl->leaves_num);

			goto end;
		} else {
			sl->tosort_sz = sl->tosort_num;
			sl->tosort = sort_realloc(sl->tosort,
			    sizeof(struct sort_list_item*) * (sl->tosort_sz));
		}
	}

	sl->sln = 256;
	sl->sublevels = sort_malloc(slsz);
	memset(sl->sublevels, 0, slsz);

	sl->real_sln = 0;

	tosort_num = sl->tosort_num;
	for (i = 0; i < tosort_num; ++i)
		place_item(sl, i);

	sort_free(sl->tosort);
	sl->tosort = NULL;
	sl->tosort_num = 0;
	sl->tosort_sz = 0;

	if (sl->leaves_num > 1) {
		if (keys_num > 1) {
			if (sort_opts_vals.sflag) {
				mergesort(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) list_coll);
			} else {
				DEFAULT_SORT_FUNC_RADIXSORT(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) list_coll);
			}
		} else if (!sort_opts_vals.sflag && sort_opts_vals.complex_sort) {
			DEFAULT_SORT_FUNC_RADIXSORT(sl->leaves, sl->leaves_num,
			    sizeof(struct sort_list_item *),
			    (int(*)(const void *, const void *)) list_coll_by_str_only);
		}
	}

	sl->leaves_sz = sl->leaves_num;
	sl->leaves = sort_realloc(sl->leaves, (sizeof(struct sort_list_item *) *
	    (sl->leaves_sz)));

	if (!reverse_sort) {
		memcpy(sl->sorted + sl->start_position, sl->leaves,
		    sl->leaves_num * sizeof(struct sort_list_item*));
		sl->start_position += sl->leaves_num;
		sort_left_dec(sl->leaves_num);

		sort_free(sl->leaves);
		sl->leaves = NULL;
		sl->leaves_num = 0;
		sl->leaves_sz = 0;

		sln = sl->sln;

		for (i = 0; i < sln; ++i) {
			slc = sl->sublevels[i];

			if (slc) {
				slc->sorted = sl->sorted;
				slc->start_position = sl->start_position;
				sl->start_position += slc->tosort_num;
				if (SMALL_NODE(slc))
					run_sort_level_next(slc);
				else
					push_ls(slc);
				sl->sublevels[i] = NULL;
			}
		}

	} else {
		size_t n;

		sln = sl->sln;

		for (i = 0; i < sln; ++i) {
			n = sln - i - 1;
			slc = sl->sublevels[n];

			if (slc) {
				slc->sorted = sl->sorted;
				slc->start_position = sl->start_position;
				sl->start_position += slc->tosort_num;
				if (SMALL_NODE(slc))
					run_sort_level_next(slc);
				else
					push_ls(slc);
				sl->sublevels[n] = NULL;
			}
		}

		memcpy(sl->sorted + sl->start_position, sl->leaves,
		    sl->leaves_num * sizeof(struct sort_list_item*));
		sort_left_dec(sl->leaves_num);
	}

end:
	free_sort_level(sl);
}

/*
 * Single-threaded sort cycle
 */
static void
run_sort_cycle_st(void)
{
	struct sort_level *slc;

	for (;;) {
		slc = pop_ls_st();
		if (slc == NULL) {
			break;
		}
		run_sort_level_next(slc);
	}
}

#if defined(SORT_THREADS)

/*
 * Multi-threaded sort cycle
 */
static void
run_sort_cycle_mt(void)
{
	struct sort_level *slc;

	for (;;) {
		slc = pop_ls_mt();
		if (slc == NULL)
			break;
		run_sort_level_next(slc);
	}
}

/*
 * Sort cycle thread (in multi-threaded mode)
 */
static void*
sort_thread(void* arg)
{
	run_sort_cycle_mt();
	sem_post(&mtsem);

	return (arg);
}

#endif /* defined(SORT_THREADS) */

static void
run_top_sort_level(struct sort_level *sl)
{
	struct sort_level *slc;

	reverse_sort = sort_opts_vals.kflag ? keys[0].sm.rflag :
	    default_sort_mods->rflag;

	sl->start_position = 0;
	sl->sln = 256;
	sl->sublevels = sort_malloc(slsz);
	memset(sl->sublevels, 0, slsz);

	for (size_t i = 0; i < sl->tosort_num; ++i)
		place_item(sl, i);

	if (sl->leaves_num > 1) {
		if (keys_num > 1) {
			if (sort_opts_vals.sflag) {
				mergesort(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) list_coll);
			} else {
				DEFAULT_SORT_FUNC_RADIXSORT(sl->leaves, sl->leaves_num,
				    sizeof(struct sort_list_item *),
				    (int(*)(const void *, const void *)) list_coll);
			}
		} else if (!sort_opts_vals.sflag && sort_opts_vals.complex_sort) {
			DEFAULT_SORT_FUNC_RADIXSORT(sl->leaves, sl->leaves_num,
			    sizeof(struct sort_list_item *),
			    (int(*)(const void *, const void *)) list_coll_by_str_only);
		}
	}

	if (!reverse_sort) {
		memcpy(sl->tosort + sl->start_position, sl->leaves,
		    sl->leaves_num * sizeof(struct sort_list_item*));
		sl->start_position += sl->leaves_num;
		sort_left_dec(sl->leaves_num);

		for (size_t i = 0; i < sl->sln; ++i) {
			slc = sl->sublevels[i];

			if (slc) {
				slc->sorted = sl->tosort;
				slc->start_position = sl->start_position;
				sl->start_position += slc->tosort_num;
				push_ls(slc);
				sl->sublevels[i] = NULL;
			}
		}

	} else {
		size_t n;

		for (size_t i = 0; i < sl->sln; ++i) {

			n = sl->sln - i - 1;
			slc = sl->sublevels[n];

			if (slc) {
				slc->sorted = sl->tosort;
				slc->start_position = sl->start_position;
				sl->start_position += slc->tosort_num;
				push_ls(slc);
				sl->sublevels[n] = NULL;
			}
		}

		memcpy(sl->tosort + sl->start_position, sl->leaves,
		    sl->leaves_num * sizeof(struct sort_list_item*));

		sort_left_dec(sl->leaves_num);
	}

#if defined(SORT_THREADS)
	if (nthreads < 2) {
#endif
		run_sort_cycle_st();
#if defined(SORT_THREADS)
	} else {
		size_t i;

		for(i = 0; i < nthreads; ++i) {
			pthread_attr_t attr;
			pthread_t pth;

			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_DETACHED);

			for (;;) {
				int res = pthread_create(&pth, &attr,
				    sort_thread, NULL);
				if (res >= 0)
					break;
				if (errno == EAGAIN) {
					pthread_yield();
					continue;
				}
				err(2, NULL);
			}

			pthread_attr_destroy(&attr);
		}

		for (i = 0; i < nthreads; ++i)
			sem_wait(&mtsem);
	}
#endif /* defined(SORT_THREADS) */
}

static void
run_sort(struct sort_list_item **base, size_t nmemb)
{
	struct sort_level *sl;

#if defined(SORT_THREADS)
	size_t nthreads_save = nthreads;
	if (nmemb < MT_SORT_THRESHOLD)
		nthreads = 1;

	if (nthreads > 1) {
		pthread_mutexattr_t mattr;

		pthread_mutexattr_init(&mattr);
		pthread_mutexattr_settype(&mattr, PTHREAD_MUTEX_ADAPTIVE_NP);

		pthread_mutex_init(&g_ls_mutex, &mattr);
		pthread_cond_init(&g_ls_cond, NULL);

		pthread_mutexattr_destroy(&mattr);

		sem_init(&mtsem, 0, 0);

	}
#endif

	sl = sort_malloc(sizeof(struct sort_level));
	memset(sl, 0, sizeof(struct sort_level));

	sl->tosort = base;
	sl->tosort_num = nmemb;
	sl->tosort_sz = nmemb;

#if defined(SORT_THREADS)
	sort_left = nmemb;
#endif

	run_top_sort_level(sl);

	free_sort_level(sl);

#if defined(SORT_THREADS)
	if (nthreads > 1) {
		sem_destroy(&mtsem);
		pthread_mutex_destroy(&g_ls_mutex);
	}
	nthreads = nthreads_save;
#endif
}

void
rxsort(struct sort_list_item **base, size_t nmemb)
{

	run_sort(base, nmemb);
}
