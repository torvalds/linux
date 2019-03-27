/*-
 * Copyright (c) 2017 Miles Fertel
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 * $FreeBSD$
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#ifdef WIKI
#include "stdlib/wiki.c"
#endif

/*
 * Integer comparison function for stdlib sorting algorithms
 */
static int
sorthelp(const void *a, const void *b)
{
	if (*(int *)a > *(int *)b)
		return 1;
	if (*(int *)a < *(int *)b)
		return -1;
	return 0;
}

#define NARGS 5

/*
 * Enumerated types for the different types of tests and sorting algorithms
 */
enum test { RAND, SORT, PART, REV, INVALID_TEST };

#ifdef WIKI
enum sort { MERGE, WIKI, QUICK, HEAP, INVALID_ALG };
#else
enum sort { MERGE, QUICK, HEAP, INVALID_ALG };
#endif

/*
 * Sort an array with the given algorithm
 */
static void
sort(int *testarray, int elts, enum sort s)
{
	switch (s) {
	case MERGE:
		mergesort(testarray, (size_t)elts, sizeof(int), sorthelp);
		break;
#ifdef WIKI
	case WIKI:
		WikiSort(testarray, (size_t)elts, sizeof(int), sorthelp);
		break;
#endif
	case QUICK:
		qsort(testarray, (size_t)elts, sizeof(int), sorthelp);
		break;
	case HEAP:
		heapsort(testarray, (size_t)elts, sizeof(int), sorthelp);
		break;
	// Should never be reached
	case INVALID_ALG:
		exit(EX_DATAERR);
	}
}

/*
 * Sort an array of randomly generated integers
 */
static void
rand_bench(int elts, enum sort s)
{
	size_t size = sizeof(int) * elts;
	int *array = malloc(size);
	arc4random_buf(array, size);
	sort(array, elts, s);
	free(array);
}

/*
 * Sort an array of increasing integers
 */
static void
sort_bench(int elts, enum sort s)
{
	size_t size = sizeof(int) * elts;
	int *array = malloc(size);
	for (int i = 0; i < elts; i++) {
		array[i] = i;
	}
	sort(array, elts, s);
	free(array);
}

/*
 * Sort an array of partially increasing, partially random integers
 */
static void
partial_bench(int elts, enum sort s)
{
	size_t size = sizeof(int) * elts;
	int *array = malloc(size);
	for (int i = 0; i < elts; i++) {
		if (i <= elts / 2)
			array[i] = i;
		else
			array[i] = arc4random();
	}
	sort(array, elts, s);
	free(array);
}

/*
 * Sort an array of decreasing integers
 */
static void
reverse_bench(int elts, enum sort s)
{
	size_t size = sizeof(int) * elts;
	int *array = malloc(size);
	for (int i = 0; i < elts; i++) {
		array[i] = elts - i;
	}
	sort(array, elts, s);
	free(array);
}

static void
run_bench(enum sort s, enum test t, int runs, int elts)
{
	for (int i = 0; i < runs; i++) {
		switch (t) {
		case RAND:
			rand_bench(elts, s);
			break;
		case SORT:
			sort_bench(elts, s);
			break;
		case PART:
			partial_bench(elts, s);
			break;
		case REV:
			reverse_bench(elts, s);
			break;
		// Should never be reached
		case INVALID_TEST:
			exit(EX_DATAERR);
		}
	}
}

static enum sort
parse_alg(char *alg)
{
	if (strcmp(alg, "merge") == 0)
		return MERGE;
#ifdef WIKI
	else if (strcmp(alg, "wiki") == 0)
		return WIKI;
#endif
	else if (strcmp(alg, "quick") == 0)
		return QUICK;
	else if (strcmp(alg, "heap") == 0)
		return HEAP;
	else
		return INVALID_ALG;
}

static enum test
parse_test(char *test)
{
	if (strcmp(test, "rand") == 0)
		return RAND;
	else if (strcmp(test, "sort") == 0)
		return SORT;
	else if (strcmp(test, "part") == 0)
		return PART;
	else if (strcmp(test, "rev") == 0)
		return REV;
	else
		return INVALID_TEST;
}

static void
usage(const char *progname)
{
	printf("Usage:\n");
	printf("\t%s: [alg] [test] [runs] [elt_power]\n", progname);
	printf("\n");
	printf("Valid algs:\n");
#ifdef WIKI
	printf("\theap merge quick wiki\n");
#else
	printf("\theap merge quick\n");
#endif
	printf("Valid tests:\n");
	printf("\trand sort part rev\n");
	printf("\trand: Random element array \n");
	printf("\tsort: Increasing order array \n");
	printf("\tpart: Partially ordered array\n");
	printf("\trev: Decreasing order array\n");
	printf("Run the algorithm [runs] times with 2^[elt_power] elements\n");
	exit(EX_USAGE);
}

/*
 * Runs a sorting algorithm with a provided data configuration according to
 * command line arguments
 */
int
main(int argc, char *argv[])
{
	const char *progname = argv[0];
	int runs, elts;
	if (argc != NARGS)
		usage(progname);

	enum sort s = parse_alg(argv[1]);
	if (s == INVALID_ALG)
		usage(progname);

	enum test t = parse_test(argv[2]);
	if (t == INVALID_TEST)
		usage(progname);

	runs = atoi(argv[3]);
	elts = pow(2, atoi(argv[4]));

	run_bench(s, t, runs, elts);
}
