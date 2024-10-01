/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_THREADS_H
#define __PERF_THREADS_H

#include "hashmap.h"
#include "rwsem.h"

struct thread;

#define THREADS__TABLE_BITS	3
#define THREADS__TABLE_SIZE	(1 << THREADS__TABLE_BITS)

struct threads_table_entry {
	/* Key is tid, value is struct thread. */
	struct hashmap	       shard;
	struct rw_semaphore    lock;
	struct thread	       *last_match;
};

struct threads {
	struct threads_table_entry table[THREADS__TABLE_SIZE];
};

void threads__init(struct threads *threads);
void threads__exit(struct threads *threads);
size_t threads__nr(struct threads *threads);
struct thread *threads__find(struct threads *threads, pid_t tid);
struct thread *threads__findnew(struct threads *threads, pid_t pid, pid_t tid, bool *created);
void threads__remove_all_threads(struct threads *threads);
void threads__remove(struct threads *threads, struct thread *thread);
int threads__for_each_thread(struct threads *threads,
			     int (*fn)(struct thread *thread, void *data),
			     void *data);

#endif	/* __PERF_THREADS_H */
