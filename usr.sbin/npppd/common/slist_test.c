/*	$OpenBSD: slist_test.c,v 1.6 2016/03/16 15:41:11 krw Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
/*

 cc -g -Wall -o slist_test slist_test.c slist.c
 ./slist_test


 */
#include <sys/types.h>
#include <stdlib.h>
#include <stdio.h>
#include "slist.h"

#define	TEST(f)			\
    {				\
	printf("%-10s .. ", #f);	\
	f();			\
	printf("ok\n");		\
    }

#define ASSERT(x)	\
	if (!(x)) { \
	    fprintf(stderr, \
		"\nASSERT(%s) failed on %s() at %s:%d.\n" \
		, #x, __func__, __FILE__, __LINE__); \
	    dump(l);				    \
	    abort(); \
	}

static void
dump(slist *l)
{
	int i;

	fprintf(stderr,
	    "\tl->itr_curr = %d\n"
	    "\tl->itr_next = %d\n"
	    "\tl->first_idx = %d\n"
	    "\tl->last_idx = %d\n"
	    "\tl->list_size = %d\n"
	    , l->itr_curr, l->itr_next, l->first_idx, l->last_idx
	    , l->list_size);
	for (i = 0; i < slist_length(l); i++) {
		if ((i % 16) == 0)
			fprintf(stderr, "%08x ", i);
		fprintf(stderr, " %3d", (int)slist_get(l, i));
		if ((i % 16) == 7)
			fprintf(stderr, " -");
		if ((i % 16) == 15)
			fprintf(stderr, "\n");
	}
	if ((i % 16) != 0)
		fprintf(stderr, "\n");
}

/* Test code for removing of the first, last and middle item. */
static void
test_01a()
{
	int i, f;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);
	slist_add(&sl, (void *)1);

	ASSERT(sl.list_size == 256);

#define	SETUP()						\
    {							\
	l->last_idx =  64;				\
	l->first_idx = 192;				\
	for (i = 0; i < slist_length(l); i++) {		\
		slist_set(l, i, (void *)i);		\
	}						\
    }

	/* Remove the first item. */
	SETUP();
	f = 0;
	while (slist_length(l) > 0) {
		slist_remove(l, 0);
		f++;
		for (i = 0; i < slist_length(l); i++) {
			ASSERT((int)slist_get(l, i) == i + f);
		}
	}

	/* Remove the last item. */
	SETUP();
	while (slist_length(l) > 0) {
		slist_remove(l, slist_length(l) - 1);
		for (i = 0; i < slist_length(l); i++) {
			ASSERT((int)slist_get(l, i) == i);
		}
	}
	/* Remove the second item from the end. */
	SETUP();
	while (slist_length(l) > 1) {
		slist_remove(l, slist_length(l) - 2);
		for (i = 0; i < slist_length(l) - 1; i++) {
			ASSERT((int)slist_get(l, i) == i);
		}
		if (slist_length(l) > 0) {
			ASSERT((int)slist_get(l, slist_length(l) - 1) == 127);
		}
	}
	slist_remove(l, slist_length(l) - 1);
	ASSERT(slist_length(l) == 0);
}

static void
test_01()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);


	for (i = 0; i < 255; i++) {
		slist_add(&sl, (void *)i);
	}
	for (i = 0; i < 128; i++) {
		slist_remove_first(&sl);
	}
	for (i = 0; i < 128; i++) {
		slist_add(&sl, (void *)(i + 255));
	}
	ASSERT((int)slist_get(&sl, 127) == 255);
	ASSERT((int)slist_get(&sl, 254) == 129 + 253);
	ASSERT((int)slist_length(&sl) == 255);

	/* dump(&sl); */
	/* printf("==\n"); */
	slist_add(&sl, (void *)(128 + 255));
	ASSERT((int)slist_get(&sl, 127) == 255);
	/* ASSERT((int)slist_get(&sl, 255) == 128 + 255); */
	ASSERT((int)slist_length(&sl) == 256);
	/* dump(&sl); */
}

static void
test_02()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);


	/* Place 300 items for left side and 211 items for right side. */
	for (i = 0; i < 511; i++)
		slist_add(&sl, (void *)i);
	for (i = 0; i <= 300; i++)
		slist_remove_first(&sl);
	for (i = 0; i <= 300; i++)
		slist_add(&sl, (void *)i);


	/* Set values to make index number and value the same. */
	for (i = 0; i < slist_length(&sl); i++)
		slist_set(&sl, i, (void *)(i + 1));

	ASSERT(slist_length(&sl) == 511);      /* The logical length is 511. */
	ASSERT((int)sl.list[511] == 211);	/* The most right is 211th. */
	ASSERT((int)sl.list[0] == 212);		/* The most left is 212th. */
	ASSERT(sl.list_size == 512);		/* The physical size is 512. */

	slist_add(&sl, (void *)512);		/* Add 512th item. */

	ASSERT(sl.list_size == 768);	   /* The physical size is extended. */
	ASSERT(slist_length(&sl) == 512);      /* The logical length is 512. */
	ASSERT((int)sl.list[511] == 211);	/* boundary */
	ASSERT((int)sl.list[512] == 212);	/* boundary */
	ASSERT((int)sl.list[767] == 467);	/* The most right is 467th. */
	ASSERT((int)sl.list[0] == 468);		/* The most left is 468th. */

	/* Check all items */
	for (i = 0; i < slist_length(&sl); i++)
		ASSERT((int)slist_get(&sl, i) == i + 1);	/* check */
}

static void
test_03()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);
	slist_add(&sl, (void *)1);

	for (i = 0; i < 255; i++) {
		slist_add(&sl, (void *)1);
		ASSERT(sl.last_idx >= 0 && sl.last_idx < sl.list_size);
		slist_remove_first(&sl);
		ASSERT(sl.last_idx >= 0 && sl.last_idx < sl.list_size);
	}
	slist_remove(&sl, 0);
	ASSERT(slist_length(&sl) == 0);
	/* dump(&sl); */
	/* TEST(test_02); */
}

static void
test_itr_subr_01(slist *l)
{
	int i;

	for (i = 0; i < slist_length(l); i++)
		slist_set(l, i, (void *)(i + 1));

	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);	/* normal iterate */
	ASSERT((int)slist_itr_next(l) == 2);	/* normal iterate */
	slist_remove(l, 2);		      /* remove next. "3" is removed */
	ASSERT((int)slist_itr_next(l) == 4);	/* removed item is skipped */
	slist_remove(l, 1);		 /* remove past item. "2" is removed */
	ASSERT((int)slist_itr_next(l) == 5);	/* no influence */
	ASSERT((int)slist_get(l, 0) == 1);	/* checking for removing */
	ASSERT((int)slist_get(l, 1) == 4);	/* checking for removing */
	ASSERT((int)slist_get(l, 2) == 5);	/* checking for removing */

	/*
	 * Total number was 255. We removed 2 items and iterated 4 times.
	 * 1 removing was past item, so the remaining is 250.
	 */

	for (i = 0; i < 249; i++)
		ASSERT(slist_itr_next(l) != NULL);
	ASSERT(slist_itr_next(l) != NULL);
	ASSERT(slist_itr_next(l) == NULL);

	/*
	 * Same as above except removing before getting the last item.
	 */

	/* Reset (253 items) */
	for (i = 0; i < slist_length(l); i++)
		slist_set(l, i, (void *)(i + 1));
	slist_itr_first(l);

	ASSERT(slist_length(l) == 253);

	for (i = 0; i < 252; i++)
		ASSERT(slist_itr_next(l) != NULL);

	slist_remove(l, 252);
	ASSERT(slist_itr_next(l) == NULL);	/* The last item is NULL */

	slist_itr_first(l);
	while (slist_length(l) > 0)
		slist_remove_first(l);
	ASSERT(slist_length(l) == 0);
	ASSERT(slist_itr_next(l) == NULL);
}

static void
test_04()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);
	for (i = 0; i < 255; i++)
		slist_add(&sl, (void *)(i + 1));

	test_itr_subr_01(&sl);

	for (i = 0; i < 256; i++) {
		/* Verify any physical placements are OK by rotating. */
		sl.first_idx = i;
		sl.last_idx = sl.first_idx + 255;
		sl.last_idx %= sl.list_size;
		ASSERT(slist_length(&sl) == 255);
		test_itr_subr_01(&sl);
	}
}

/* Verify removing the last item on the physical location */
static void
test_05()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(&sl);
	/* Fill */
	for (i = 0; i < 255; i++) {
		slist_add(&sl, (void *)i);
	}
	/* Remove 254 items */
	for (i = 0; i < 254; i++) {
		slist_remove_first(&sl);
	}
	slist_set(l, 0, NULL);
	/* Add 7 items */
	for (i = 0; i < 8; i++) {
		slist_add(&sl, (void *)i + 1);
	}
	ASSERT(sl.first_idx == 254);
	ASSERT(sl.last_idx == 7);

	slist_remove(l, 0);
	ASSERT((int)slist_get(l, 0) == 1);
	ASSERT((int)slist_get(l, 1) == 2);
	ASSERT((int)slist_get(l, 2) == 3);
	ASSERT((int)slist_get(l, 3) == 4);
	ASSERT((int)slist_get(l, 4) == 5);
	ASSERT((int)slist_get(l, 5) == 6);
	ASSERT((int)slist_get(l, 6) == 7);
	ASSERT((int)slist_get(l, 7) == 8);
	ASSERT(l->first_idx == 255);

	slist_remove(l, 0);
	ASSERT((int)slist_get(l, 0) == 2);
	ASSERT((int)slist_get(l, 1) == 3);
	ASSERT((int)slist_get(l, 2) == 4);
	ASSERT((int)slist_get(l, 3) == 5);
	ASSERT((int)slist_get(l, 4) == 6);
	ASSERT((int)slist_get(l, 5) == 7);
	ASSERT((int)slist_get(l, 6) == 8);
	ASSERT(l->first_idx == 0);
}

static void
test_06()
{
	int i, j;
	slist sl;
	slist *l = &sl;

	slist_init(l);
	for (i = 0; i < 255; i++)
		slist_add(l, (void *)i);

	i = 255;

	for (slist_itr_first(l); slist_itr_has_next(l); ) {
		ASSERT(slist_length(l) == i);
		slist_itr_next(l);
		ASSERT((int)slist_itr_remove(l) == 255 - i);
		ASSERT(slist_length(l) == i - 1);
		for (j = i; j < slist_length(l); j++)
			ASSERT((int)slist_get(l, j) == i + j);
		i--;
	}
}

static void
test_07()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_add(l, (void *)1);
	slist_remove_first(l);
	l->first_idx = 120;
	l->last_idx = 120;
	for (i = 0; i < 255; i++)
		slist_add(l, (void *)i);


	for (i = 0, slist_itr_first(l); slist_itr_has_next(l); i++) {
		ASSERT((int)slist_itr_next(l) == i);
		if (i > 200)
		    ASSERT((int)slist_itr_remove(l) == i);
	}
}

static void
test_08()
{
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_set_size(l, 4);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);

	/* [1, 2, 3] */

	slist_itr_first(l);
	slist_itr_has_next(l);
	slist_itr_next(l);
	slist_itr_remove(l);
	/* [2, 3] */

	slist_add(l, (void *)4);
	/* [2, 3, 4] */
	ASSERT((int)slist_get(l, 0) == 2);
	ASSERT((int)slist_get(l, 1) == 3);
	ASSERT((int)slist_get(l, 2) == 4);
	slist_add(l, (void *)5);

	/* [2, 3, 4, 5] */
	ASSERT((int)slist_get(l, 0) == 2);
	ASSERT((int)slist_get(l, 1) == 3);
	ASSERT((int)slist_get(l, 2) == 4);
	ASSERT((int)slist_get(l, 3) == 5);
}

static void
test_09()
{
	slist sl;
	slist *l = &sl;

	/*
	 * #1
	 */
	slist_init(l);
	slist_set_size(l, 3);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);

	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);		/* 1 */
	ASSERT((int)slist_itr_next(l) == 2);		/* 2 */
	ASSERT((int)slist_itr_next(l) == 3);		/* 3 */
							/* reaches the last */
	slist_add(l, (void *)4);			/* add a new item */
	ASSERT(slist_itr_has_next(l));			/* iterates the new */
	ASSERT((int)slist_itr_next(l) == 4);
	slist_fini(l);


	/*
	 * #2
	 */
	slist_init(l);
	slist_set_size(l, 3);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);

	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);		/* 1 */
	ASSERT((int)slist_itr_next(l) == 2);		/* 2 */
	ASSERT((int)slist_itr_next(l) == 3);		/* 3 */
							/* reaches the last */
	slist_itr_remove(l);				/* and remove the last*/
	slist_add(l, (void *)4);			/* add 4 (new last)*/
	ASSERT(slist_itr_has_next(l));			/* */
	ASSERT((int)slist_itr_next(l) == 4);		/* 4 */
	slist_fini(l);

	/*
	 * #3
	 */
	slist_init(l);
	slist_set_size(l, 3);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);

	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);		/* 1 */
	ASSERT((int)slist_itr_next(l) == 2);		/* 2 */
	ASSERT((int)slist_itr_next(l) == 3);		/* 3 */
							/* reaches the last */
	slist_add(l, (void *)4);			/* add a new */
	slist_itr_remove(l);
	ASSERT(slist_itr_has_next(l));
	ASSERT((int)slist_itr_next(l) == 4);		/* 4 */
	slist_fini(l);

	/*
	 * #4 - remove iterator's next and it is the last
	 */
	slist_init(l);
	slist_set_size(l, 3);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);

	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);		/* 1 */
	ASSERT((int)slist_itr_next(l) == 2);		/* 2 */
	slist_remove(l, 2);				/* remove the next */
	slist_add(l, (void *)4);			/* add the new next */
	ASSERT(slist_itr_has_next(l));			/* iterates the new */
	ASSERT((int)slist_itr_next(l) == 4);
	slist_fini(l);
}
static void
test_10()
{
	int i;
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	slist_add(l, (void *)3);
	slist_itr_first(l);
	ASSERT((int)slist_itr_next(l) == 1);
	ASSERT((int)slist_itr_next(l) == 2);
	for (i = 4; i < 10000; i++) {
		ASSERT(slist_itr_has_next(l));
		ASSERT((int)slist_itr_next(l) == i - 1);
		if (i % 3 == 1)
			slist_add(l, (void *)i);
		if (i % 3 == 0)
			ASSERT((int)slist_itr_remove(l) == i - 1);
		if (i % 3 != 1)
			slist_add(l, (void *)i);
	}
	slist_itr_first(l);
	while (slist_itr_has_next(l)) {
		slist_itr_next(l);
		slist_itr_remove(l);
	}
	ASSERT((int)slist_length(l) == 0);

	slist_fini(l);
}

static void
test_11()
{
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_add(l, (void *)1);
	slist_add(l, (void *)2);
	ASSERT((int)slist_remove_last(l) == 2);
	ASSERT((int)slist_length(l) == 1);
	ASSERT((int)slist_remove_last(l) == 1);
	ASSERT((int)slist_length(l) == 0);
}

static int
test_12_compar(const void *a, const void *b)
{
	return (int)a - (int)b;
}

static void
test_12()
{
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_add(l, (void *)42);
	slist_add(l, (void *)15);
	slist_add(l, (void *)14);
	slist_add(l, (void *)13);
	slist_add(l, (void *)29);
	slist_add(l, (void *)15);
	slist_add(l, (void *)25);
	slist_add(l, (void *)55);
	slist_add(l, (void *)66);
	slist_add(l, (void *)23);
	slist_qsort(l, test_12_compar);
	ASSERT((int)slist_get(l, 0) == 13);
	ASSERT((int)slist_get(l, 1) == 14);
	ASSERT((int)slist_get(l, 2) == 15);
	ASSERT((int)slist_get(l, 3) == 15);
	ASSERT((int)slist_get(l, 4) == 23);
	ASSERT((int)slist_get(l, 5) == 25);
	ASSERT((int)slist_get(l, 6) == 29);
	ASSERT((int)slist_get(l, 7) == 42);
	ASSERT((int)slist_get(l, 8) == 55);
	ASSERT((int)slist_get(l, 9) == 66);
}

static void
test_13()
{
	slist sl;
	slist *l = &sl;

	slist_init(l);
	slist_qsort(l, test_12_compar);
	/* still alive without SIGFPE */
}

int
main(int argc, char *argv[])
{
	TEST(test_01);
	TEST(test_01a);
	TEST(test_02);
	TEST(test_03);
	TEST(test_04);
	TEST(test_05);
	TEST(test_06);
	TEST(test_07);
	TEST(test_08);
	TEST(test_09);
	TEST(test_10);
	TEST(test_11);
	TEST(test_12);
	TEST(test_13);
	return 0;
}
