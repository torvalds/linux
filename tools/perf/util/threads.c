// SPDX-License-Identifier: GPL-2.0
#include "threads.h"
#include "machine.h"
#include "thread.h"

static struct threads_table_entry *threads__table(struct threads *threads, pid_t tid)
{
	/* Cast it to handle tid == -1 */
	return &threads->table[(unsigned int)tid % THREADS__TABLE_SIZE];
}

static size_t key_hash(long key, void *ctx __maybe_unused)
{
	/* The table lookup removes low bit entropy, but this is just ignored here. */
	return key;
}

static bool key_equal(long key1, long key2, void *ctx __maybe_unused)
{
	return key1 == key2;
}

void threads__init(struct threads *threads)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		hashmap__init(&table->shard, key_hash, key_equal, NULL);
		init_rwsem(&table->lock);
		table->last_match = NULL;
	}
}

void threads__exit(struct threads *threads)
{
	threads__remove_all_threads(threads);
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		hashmap__clear(&table->shard);
		exit_rwsem(&table->lock);
	}
}

size_t threads__nr(struct threads *threads)
{
	size_t nr = 0;

	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		down_read(&table->lock);
		nr += hashmap__size(&table->shard);
		up_read(&table->lock);
	}
	return nr;
}

/*
 * Front-end cache - TID lookups come in blocks,
 * so most of the time we dont have to look up
 * the full rbtree:
 */
static struct thread *__threads_table_entry__get_last_match(struct threads_table_entry *table,
							    pid_t tid)
{
	struct thread *th, *res = NULL;

	th = table->last_match;
	if (th != NULL) {
		if (thread__tid(th) == tid)
			res = thread__get(th);
	}
	return res;
}

static void __threads_table_entry__set_last_match(struct threads_table_entry *table,
						  struct thread *th)
{
	thread__put(table->last_match);
	table->last_match = thread__get(th);
}

static void threads_table_entry__set_last_match(struct threads_table_entry *table,
						struct thread *th)
{
	down_write(&table->lock);
	__threads_table_entry__set_last_match(table, th);
	up_write(&table->lock);
}

struct thread *threads__find(struct threads *threads, pid_t tid)
{
	struct threads_table_entry *table  = threads__table(threads, tid);
	struct thread *res;

	down_read(&table->lock);
	res = __threads_table_entry__get_last_match(table, tid);
	if (!res) {
		if (hashmap__find(&table->shard, tid, &res))
			res = thread__get(res);
	}
	up_read(&table->lock);
	if (res)
		threads_table_entry__set_last_match(table, res);
	return res;
}

struct thread *threads__findnew(struct threads *threads, pid_t pid, pid_t tid, bool *created)
{
	struct threads_table_entry *table  = threads__table(threads, tid);
	struct thread *res = NULL;

	*created = false;
	down_write(&table->lock);
	res = thread__new(pid, tid);
	if (res) {
		if (hashmap__add(&table->shard, tid, res)) {
			/* Add failed. Assume a race so find other entry. */
			thread__put(res);
			res = NULL;
			if (hashmap__find(&table->shard, tid, &res))
				res = thread__get(res);
		} else {
			res = thread__get(res);
			*created = true;
		}
		if (res)
			__threads_table_entry__set_last_match(table, res);
	}
	up_write(&table->lock);
	return res;
}

void threads__remove_all_threads(struct threads *threads)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];
		struct hashmap_entry *cur, *tmp;
		size_t bkt;

		down_write(&table->lock);
		__threads_table_entry__set_last_match(table, NULL);
		hashmap__for_each_entry_safe(&table->shard, cur, tmp, bkt) {
			struct thread *old_value;

			hashmap__delete(&table->shard, cur->key, /*old_key=*/NULL, &old_value);
			thread__put(old_value);
		}
		up_write(&table->lock);
	}
}

void threads__remove(struct threads *threads, struct thread *thread)
{
	struct threads_table_entry *table  = threads__table(threads, thread__tid(thread));
	struct thread *old_value;

	down_write(&table->lock);
	if (table->last_match && RC_CHK_EQUAL(table->last_match, thread))
		__threads_table_entry__set_last_match(table, NULL);

	hashmap__delete(&table->shard, thread__tid(thread), /*old_key=*/NULL, &old_value);
	thread__put(old_value);
	up_write(&table->lock);
}

int threads__for_each_thread(struct threads *threads,
			     int (*fn)(struct thread *thread, void *data),
			     void *data)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];
		struct hashmap_entry *cur;
		size_t bkt;

		down_read(&table->lock);
		hashmap__for_each_entry(&table->shard, cur, bkt) {
			int rc = fn((struct thread *)cur->pvalue, data);

			if (rc != 0) {
				up_read(&table->lock);
				return rc;
			}
		}
		up_read(&table->lock);
	}
	return 0;

}
