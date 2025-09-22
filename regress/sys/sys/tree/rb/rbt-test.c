/*
 * Copyright 2002 Niels Provos <provos@citi.umich.edu>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/time.h>

#include <assert.h>
#include <err.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

struct timespec start, end, diff, rstart, rend, rdiff, rtot = {0, 0};
#ifndef timespecsub
#define	timespecsub(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec - (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec - (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec < 0) {				\
			(vsp)->tv_sec--;				\
			(vsp)->tv_nsec += 1000000000L;			\
		}							\
	} while (0)
#endif
#ifndef timespecadd
#define	timespecadd(tsp, usp, vsp)					\
	do {								\
		(vsp)->tv_sec = (tsp)->tv_sec + (usp)->tv_sec;		\
		(vsp)->tv_nsec = (tsp)->tv_nsec + (usp)->tv_nsec;	\
		if ((vsp)->tv_nsec >= 1000000000L) {			\
			(vsp)->tv_sec++;				\
			(vsp)->tv_nsec -= 1000000000L;			\
		}							\
	} while (0)
#endif

//#define RBT_SMALL
//#define RBT_TEST_RANK
//#define RBT_TEST_DIAGNOSTIC
//#define _RBT_DIAGNOSTIC

#ifdef DOAUGMENT
#define RBT_AUGMENT(elm) tree_augment(elm)
#endif

#include <sys/tree.h>

#define TDEBUGF(fmt, ...)						\
	fprintf(stderr, "%s:%d:%s(): " fmt "\n",			\
        __FILE__, __LINE__, __func__, ##__VA_ARGS__)


#ifdef __OpenBSD__
#define SEED_RANDOM srandom_deterministic
#else
#define SEED_RANDOM srandom
#endif

int ITER=150000;
int RANK_TEST_ITERATIONS=10000;

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* declarations */
struct node;
struct tree;
static int compare(const struct node *, const struct node *);
static void mix_operations(int *, int, struct node *, int, int, int, int);

#ifdef DOAUGMENT
static int tree_augment(struct node *);
#else
#define tree_augment(x) (0)
#endif

#ifdef RBT_TEST_DIAGNOSTIC
static void print_helper(const struct node *, int);
static void print_tree(const struct tree *);
#else
#define print_helper(x, y)      do {} while (0)
#define print_tree(x)	   do {} while (0)
#endif

/* definitions */
struct node {
	RBT_ENTRY(node)   node_link;
	int		 key;
	size_t		 height;
	size_t		 size;
};

RBT_HEAD(tree, node);
struct tree root = RBT_INITIALIZER(&root);

RBT_PROTOTYPE(tree, node, node_link, compare)

RBT_GENERATE(tree, node, node_link, compare);

#ifndef RBT_RANK
#define RBT_RANK(x, y)   0
#endif
#ifndef _RBT_GET_RDIFF
#define _RBT_GET_RDIFF(x, y, z) 0
#endif

int
main(int argc, char **argv)
{
	char *test_target = NULL;
	struct node *tmp, *ins, *nodes;
	int i, r, rank, *perm, *nums;

	if (argc > 1)
		test_target = argv[1];

	nodes = calloc((ITER + 5), sizeof(struct node));
	perm = calloc(ITER, sizeof(int));
	nums = calloc(ITER, sizeof(int));

	// for determinism
	SEED_RANDOM(4201);

	TDEBUGF("generating a 'random' permutation");
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
	perm[0] = 0;
	nums[0] = 0;
	for(i = 1; i < ITER; i++) {
		r = random() % i; // arc4random_uniform(i);
		perm[i] = perm[r];
		perm[r] = i;
		nums[i] = i;
	}
	/*
	fprintf(stderr, "{");
	for(int i = 0; i < ITER; i++) {
		fprintf(stderr, "%d, ", perm[i]);
	}
	fprintf(stderr, "}\n");
	int nperm[10] = {2, 4, 9, 7, 8, 3, 0, 1, 6, 5};
	int nperm[6] = {2, 6, 1, 4, 5, 3};
	int nperm[10] = {10, 3, 7, 8, 6, 1, 9, 2, 5, 4};
	int nperm[2] = {0, 1};

	int nperm[100] = {
		54, 47, 31, 35, 40, 73, 29, 66, 15, 45, 9, 71, 51, 32, 28, 62,
		12, 46, 50, 26, 36, 91, 10, 76, 33, 43, 34, 58, 55, 72, 37, 24,
		75, 4, 90, 88, 30, 25, 82, 18, 67, 81, 80, 65, 23, 41, 61, 86,
		20, 99, 59, 14, 79, 21, 68, 27, 1, 7, 94, 44, 89, 64, 96, 2, 49,
		53, 74, 13, 48, 42, 60, 52, 95, 17, 11, 0, 22, 97, 77, 69, 6,
		16, 84, 78, 8, 83, 98, 93, 39, 38, 85, 70, 3, 19, 57, 5, 87,
		92, 63, 56
	};
	ITER = 100;
	for(int i = 0; i < ITER; i++){
		perm[i] = nperm[i];
	}
	*/

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
	timespecsub(&end, &start, &diff);
	TDEBUGF("done generating a 'random' permutation in: %llu.%09llu s",
	    (unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

	RBT_INIT(tree, &root);

	// testing random inserts
	// due to codependency between inserts and removals, this also tests
	// root removals
	if (test_target == NULL ||
	    strcmp(test_target, "random-inserts") == 0
	) {
		TDEBUGF("starting random insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done random insertions in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

#ifdef DOAUGMENT
		ins = RBT_ROOT(tree, &root);
		assert(ITER + 1 == ins->size);
#endif

		TDEBUGF("getting min");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		ins = RBT_MIN(tree, &root);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done getting min in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
		assert(0 == ins->key);

		TDEBUGF("getting max");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		ins = RBT_MAX(tree, &root);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done getting max in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
		assert(ITER + 5 == ins->key);

		ins = RBT_ROOT(tree, &root);
		TDEBUGF("getting root");
		assert(RBT_REMOVE(tree, &root, ins) == ins);

#ifdef DOAUGMENT
		assert(ITER == (RBT_ROOT(tree, &root))->size);
#endif

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER; i++) {
			tmp = RBT_ROOT(tree, &root);
			assert(NULL != tmp);
			assert(RBT_REMOVE(tree, &root, tmp) == tmp);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				assert(-2 != rank);
				print_tree(&root);
			}
#endif

#ifdef DOAUGMENT
			if (!(RBT_EMPTY(&root)) && (RBT_ROOT(tree, &root))->size != ITER - 1 - i)
				errx(1, "RBT_REMOVE size error");
#endif

		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}

	// testing insertions in increasing sequential order
	// removals are done using root removals (separate test)
	if (test_target == NULL ||
	    strcmp(test_target, "sequential-inserts") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			assert(NULL != tmp);
			assert(RBT_REMOVE(tree, &root, tmp) == tmp);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}

	// testing the RBT_FIND function and using it to do removals in
	// sequential order
	// insertions are done using sequential insertions (separate test)
	if (test_target == NULL ||
	    strcmp(test_target, "sequential-removes") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing find and remove in sequential order");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = malloc(sizeof(struct node));
		for(i = 0; i < ITER; i++) {
			tmp->key = i;
			ins = RBT_FIND(tree, &root, tmp);
			assert(NULL != tmp);
			assert(RBT_REMOVE(tree, &root, ins) == ins);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		free(tmp);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done removals in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		ins = RBT_ROOT(tree, &root);
		if (ins == NULL)
			errx(1, "RBT_ROOT error");
		if (RBT_REMOVE(tree, &root, ins) != ins)
			errx(1, "RBT_REMOVE failed");
	}

	// testing removals in random order
	// the elements are found using RBT_FIND
	// insertions are done using sequential insertions (separate test)
	if (test_target == NULL ||
	    strcmp(test_target, "random-removes") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);


		TDEBUGF("doing find and remove in random order");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = malloc(sizeof(struct node));
		for(i = 0; i < ITER; i++) {
			tmp->key = perm[i];
			ins = RBT_FIND(tree, &root, tmp);
			if (ins == NULL) {
				errx(1, "RBT_FIND %d failed: %d", i, perm[i]);
			}
			if (RBT_REMOVE(tree, &root, ins) == NULL)
				errx(1, "RBT_REMOVE failed: %d", i);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		free(tmp);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done removals in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		ins = RBT_ROOT(tree, &root);
		if (ins == NULL)
			errx(1, "RBT_ROOT error");
		if (RBT_REMOVE(tree, &root, ins) != ins)
			errx(1, "RBT_REMOVE failed");
	}

	// testing removals by finding the next largest element
	// this is testing the RBT_NFIND macro
	// insertions are done using sequential insertions (separate test)
	if (test_target == NULL ||
	    strcmp(test_target, "remove-nfind") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing nfind and remove");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = malloc(sizeof(struct node));
		for(i = 0; i < ITER + 1; i++) {
			tmp->key = i;
			ins = RBT_NFIND(tree, &root, tmp);
			if (ins == NULL)
				errx(1, "RBT_NFIND failed");
			if (RBT_REMOVE(tree, &root, ins) == NULL)
				errx(1, "RBT_REMOVE failed: %d", i);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		free(tmp);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done removals in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}

#ifdef RBT_PFIND
	// testing removals by finding the previous element
	// this is testing the RBT_PFIND macro
	// insertions are done using sequential insertions (separate test)
	if (test_target == NULL ||
	    strcmp(test_target, "remove-pfind") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing pfind and remove");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = malloc(sizeof(struct node));
		for(i = 0; i < ITER + 1; i++) {
			tmp->key = ITER + 6;
			ins = RBT_PFIND(tree, &root, tmp);
			if (ins == NULL)
				errx(1, "RBT_PFIND failed");
			if (RBT_REMOVE(tree, &root, ins) == NULL)
				errx(1, "RBT_REMOVE failed: %d", i);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		free(tmp);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done removals in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}
#endif

	// iterate over the tree using RBT_NEXT/RBT_PREV
	// insertions and removals have separate tests
	if (test_target == NULL ||
	    strcmp(test_target, "node-iterations") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree with RBT_NEXT");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = RBT_MIN(tree, &root);
		assert(tmp != NULL);
		assert(tmp->key == 0);
		for(i = 1; i < ITER; i++) {
			tmp = RBT_NEXT(tree, tmp);
			assert(tmp != NULL);
			assert(tmp->key == i);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree with RBT_PREV");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = RBT_MAX(tree, &root);
		assert(tmp != NULL);
		assert(tmp->key == ITER + 5);
		for(i = 0; i < ITER; i++) {
			tmp = RBT_PREV(tree, tmp);
			assert(tmp != NULL);
			assert(tmp->key == ITER - 1 - i);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			assert(NULL != tmp);
			assert(RBT_REMOVE(tree, &root, tmp) == tmp);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}

	// iterate over the tree using RBT_FOREACH* macros
	// the *_SAFE macros are tested by using them to clear the tree
	// insertions and removals have separate tests
	if (test_target == NULL ||
	    strcmp(test_target, "iteration-macros") == 0
	) {
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

#ifdef RBT_FOREACH
		TDEBUGF("iterating over tree with RBT_FOREACH");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		i = 0;
		RBT_FOREACH(ins, tree, &root) {
			if (i < ITER)
				assert(ins->key == i);
			else
				assert(ins->key == ITER + 5);
			i++;
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
#endif

#ifdef RBT_FOREACH_REVERSE
		TDEBUGF("iterating over tree with RBT_FOREACH_REVERSE");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		i = ITER + 5;
		RBT_FOREACH_REVERSE(ins, tree, &root) {
			assert(ins->key == i);
			if (i > ITER)
				i = ITER - 1;
			else
				i--;
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
#endif

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			assert(NULL != tmp);
			assert(RBT_REMOVE(tree, &root, tmp) == tmp);
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

#ifdef RBT_FOREACH_SAFE
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree and clearing with RBT_FOREACH_SAFE");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		i = 0;
		RBT_FOREACH_SAFE(ins, tree, &root, tmp) {
			if (i < ITER)
				assert(ins->key == i);
			else
				assert(ins->key == ITER + 5);
			i++;
			assert(RBT_REMOVE(tree, &root, ins) == ins);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
#endif

#ifdef RBT_FOREACH_REVERSE_SAFE
		TDEBUGF("starting sequential insertions");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(nums, ITER, nodes, ITER, ITER, 0, 0);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done sequential insertions in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree and clearing with RBT_FOREACH_REVERSE_SAFE");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		i = ITER + 5;
		RBT_FOREACH_REVERSE_SAFE(ins, tree, &root, tmp) {
			assert(ins->key == i);
			if (i > ITER)
				i = ITER - 1;
			else
				i--;
			assert(RBT_REMOVE(tree, &root, ins) == ins);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
#endif
	}

#ifdef RBT_INSERT_NEXT
	// testing insertions at specific points in the tree
	// the insertions are done at the next position in the tree at a give node
	// this assumes that the location is correct for the given ordering
	if (test_target == NULL ||
	    strcmp(test_target, "insert-next") == 0
	) {
		TDEBUGF("starting sequential insertions using INSERT_NEXT");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = &(nodes[0]);
		tmp->size = 1;
		tmp->height = 1;
		tmp->key = 0;
		if (RBT_INSERT(tree, &root, tmp) != NULL)
			errx(1, "RBT_INSERT failed");
		ins = tmp;
		for(i = 1; i < ITER; i++) {
			tmp = &(nodes[i]);
			tmp->size = 1;
			tmp->height = 1;
			tmp->key = i;
			if (RBT_INSERT_NEXT(tree, &root, ins, tmp) != NULL)
				errx(1, "RBT_INSERT failed");
			ins = tmp;
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done insertions in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree and clearing with RBT_FOREACH_REVERSE_SAFE");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		RBT_FOREACH_REVERSE_SAFE(ins, tree, &root, tmp) {
			assert(RBT_REMOVE(tree, &root, ins) == ins);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}
#endif

#ifdef RBT_INSERT_PREV
	// testing insertions at specific points in the tree
	// the insertions are done at the next position in the tree at a give node
	// this assumes that the location is correct for the given ordering
	if (test_target == NULL ||
	    strcmp(test_target, "insert-prev") == 0
	) {
		TDEBUGF("starting sequential insertions using INSERT_PREV");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		tmp = &(nodes[ITER]);
		tmp->size = 1;
		tmp->height = 1;
		tmp->key = ITER;
		if (RBT_INSERT(tree, &root, tmp) != NULL)
			errx(1, "RBT_INSERT failed");
		ins = tmp;
		for(i = ITER - 1; i >= 0; i--) {
			tmp = &(nodes[i]);
			tmp->size = 1;
			tmp->height = 1;
			tmp->key = i;
			if (RBT_INSERT_PREV(tree, &root, ins, tmp) != NULL)
				errx(1, "RBT_INSERT failed");
			ins = tmp;
#ifdef RBT_TEST_RANK
			if (i % RANK_TEST_ITERATIONS == 0) {
				rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
				if (rank == -2)
					errx(1, "rank error");
			}
#endif
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done insertions in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("iterating over tree and clearing with RBT_FOREACH_REVERSE_SAFE");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		RBT_FOREACH_REVERSE_SAFE(ins, tree, &root, tmp) {
			assert(RBT_REMOVE(tree, &root, ins) == ins);
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done iterations in %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}
#endif

	if (test_target == NULL ||
	    strcmp(test_target, "benchmarks") == 0
	) {
		TDEBUGF("doing 50%% insertions, 50%% lookups");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes, ITER, ITER / 2, ITER / 2, 1);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done operations in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER / 2 + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			if (tmp == NULL)
				errx(1, "RBT_ROOT error");
			if (RBT_REMOVE(tree, &root, tmp) != tmp)
				errx(1, "RBT_REMOVE error");
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing 20%% insertions, 80%% lookups");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes, ITER, ITER / 5, 4 * (ITER / 5), 1);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done operations in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER / 5 + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			if (tmp == NULL)
				errx(1, "RBT_ROOT error");
			if (RBT_REMOVE(tree, &root, tmp) != tmp)
				errx(1, "RBT_REMOVE error");
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing 10%% insertions, 90%% lookups");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes, ITER, ITER / 10, 9 * (ITER / 10), 1);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done operations in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < ITER / 10 + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			if (tmp == NULL)
				errx(1, "RBT_ROOT error");
			if (RBT_REMOVE(tree, &root, tmp) != tmp)
				errx(1, "RBT_REMOVE error");
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing 5%% insertions, 95%% lookups");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes,
		ITER, 5 * (ITER / 100),
		95 * (ITER / 100), 1);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done operations in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < 5 * (ITER / 100) + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			if (tmp == NULL)
				errx(1, "RBT_ROOT error");
			if (RBT_REMOVE(tree, &root, tmp) != tmp)
				errx(1, "RBT_REMOVE error");
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing 2%% insertions, 98%% lookups");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		mix_operations(perm, ITER, nodes, ITER,
		2 * (ITER / 100),
		98 * (ITER / 100), 1);
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done operations in: %lld.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);

		TDEBUGF("doing root removals");
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);
		for (i = 0; i < 2 * (ITER / 100) + 1; i++) {
			tmp = RBT_ROOT(tree, &root);
			if (tmp == NULL)
				errx(1, "RBT_ROOT error");
			if (RBT_REMOVE(tree, &root, tmp) != tmp)
				errx(1, "RBT_REMOVE error");
		}
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
		timespecsub(&end, &start, &diff);
		TDEBUGF("done root removals in: %llu.%09llu s",
		(unsigned long long)diff.tv_sec, (unsigned long long)diff.tv_nsec);
	}

	free(nodes);
	free(perm);
	free(nums);

	return 0;
}


static int
compare(const struct node *a, const struct node *b)
{
	return a->key - b->key;
}

#ifdef RBT_TEST_DIAGNOSTIC
static void
print_helper(const struct node *n, int indent)
{
	if (RBT_RIGHT(n, node_link))
  		print_helper(RBT_RIGHT(n, node_link), indent + 4);
	TDEBUGF("%*s key=%d :: size=%zu :: rank=%d :: rdiff %lu:%lu",
	    indent, "", n->key, n->size, RBT_RANK(tree, n),
	    _RBT_GET_RDIFF(n, _RBT_LDIR, node_link),
	    _RBT_GET_RDIFF(n, _RBT_RDIR, node_link));
	if (RBT_LEFT(n, node_link))
  		print_helper(RBT_LEFT(n, node_link), indent + 4);
}

static void
print_tree(const struct tree *t)
{
	if (RBT_ROOT(tree, t)) print_helper(RBT_ROOT(tree, t), 0);
}
#endif

#ifdef DOAUGMENT
static int
tree_augment(struct node *elm)
{
	size_t newsize = 1, newheight = 0;
	if ((RBT_LEFT(elm, node_link))) {
		newsize += (RBT_LEFT(elm, node_link))->size;
		newheight = MAX((RBT_LEFT(elm, node_link))->height, newheight);
	}
	if ((RBT_RIGHT(elm, node_link))) {
		newsize += (RBT_RIGHT(elm, node_link))->size;
		newheight = MAX((RBT_RIGHT(elm, node_link))->height, newheight);
	}
	newheight += 1;
	if (elm->size != newsize || elm->height != newheight) {
		elm->size = newsize;
		elm->height = newheight;
		return 1;
	}
	return 0;
}
#endif


void
mix_operations(int *perm, int psize, struct node *nodes, int nsize,
    int insertions, int reads, int do_reads)
{
	int i, rank;
	struct node *tmp, *ins;
	struct node it;
	assert(psize == nsize);
	assert(insertions + reads <= psize);

	for(i = 0; i < insertions; i++) {
                //TDEBUGF("iteration %d", i);
		tmp = &(nodes[i]);
		if (tmp == NULL) err(1, "malloc");
		tmp->size = 1;
		tmp->height = 1;
		tmp->key = perm[i];
		//TDEBUGF("inserting %d", tmp->key);
		if (RBT_INSERT(tree, &root, tmp) != NULL)
			errx(1, "RBT_INSERT failed");
                print_tree(&root);
#ifdef DOAUGMENT
                //TDEBUGF("size = %zu", RBT_ROOT(tree, &root)->size);
                assert(RBT_ROOT(tree, &root)->size == i + 1);
#endif

#ifdef RBT_TEST_RANK
		if (i % RANK_TEST_ITERATIONS == 0) {
			rank = RBT_RANK(tree, RBT_ROOT(tree, &root));
			if (rank == -2)
				errx(1, "rank error");
		}
#endif
	}
	tmp = &(nodes[insertions]);
	tmp->key = ITER + 5;
	tmp->size = 1;
	tmp->height = 1;
	RBT_INSERT(tree, &root, tmp);
	if (do_reads) {
		for (i = 0; i < insertions; i++) {
			it.key = perm[i];
			ins = RBT_FIND(tree, &root, &it);
			if ((ins == NULL) || ins->key != it.key)
				errx(1, "RBT_FIND failed");
		}
		for (i = insertions; i < insertions + reads; i++) {
			it.key = perm[i];
			ins = RBT_NFIND(tree, &root, &it);
			if (ins->key < it.key)
				errx(1, "RBT_NFIND failed");
		}
	}
}
