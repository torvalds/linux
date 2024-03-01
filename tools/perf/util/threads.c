// SPDX-License-Identifier: GPL-2.0
#include "threads.h"
#include "machine.h"
#include "thread.h"

struct thread_rb_node {
	struct rb_node rb_node;
	struct thread *thread;
};

static struct threads_table_entry *threads__table(struct threads *threads, pid_t tid)
{
	/* Cast it to handle tid == -1 */
	return &threads->table[(unsigned int)tid % THREADS__TABLE_SIZE];
}

void threads__init(struct threads *threads)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		table->entries = RB_ROOT_CACHED;
		init_rwsem(&table->lock);
		table->nr = 0;
		table->last_match = NULL;
	}
}

void threads__exit(struct threads *threads)
{
	threads__remove_all_threads(threads);
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		exit_rwsem(&table->lock);
	}
}

size_t threads__nr(struct threads *threads)
{
	size_t nr = 0;

	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];

		down_read(&table->lock);
		nr += table->nr;
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
	struct rb_node **p;
	struct thread *res = NULL;

	down_read(&table->lock);
	res = __threads_table_entry__get_last_match(table, tid);
	if (res)
		return res;

	p = &table->entries.rb_root.rb_node;
	while (*p != NULL) {
		struct rb_node *parent = *p;
		struct thread *th = rb_entry(parent, struct thread_rb_node, rb_node)->thread;

		if (thread__tid(th) == tid) {
			res = thread__get(th);
			break;
		}

		if (tid < thread__tid(th))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	up_read(&table->lock);
	if (res)
		threads_table_entry__set_last_match(table, res);
	return res;
}

struct thread *threads__findnew(struct threads *threads, pid_t pid, pid_t tid, bool *created)
{
	struct threads_table_entry *table  = threads__table(threads, tid);
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct thread *res = NULL;
	struct thread_rb_node *nd;
	bool leftmost = true;

	*created = false;
	down_write(&table->lock);
	p = &table->entries.rb_root.rb_node;
	while (*p != NULL) {
		struct thread *th;

		parent = *p;
		th = rb_entry(parent, struct thread_rb_node, rb_node)->thread;

		if (thread__tid(th) == tid) {
			__threads_table_entry__set_last_match(table, th);
			res = thread__get(th);
			goto out_unlock;
		}

		if (tid < thread__tid(th))
			p = &(*p)->rb_left;
		else {
			leftmost = false;
			p = &(*p)->rb_right;
		}
	}
	nd = malloc(sizeof(*nd));
	if (nd == NULL)
		goto out_unlock;
	res = thread__new(pid, tid);
	if (!res)
		free(nd);
	else {
		*created = true;
		nd->thread = thread__get(res);
		rb_link_node(&nd->rb_node, parent, p);
		rb_insert_color_cached(&nd->rb_node, &table->entries, leftmost);
		++table->nr;
		__threads_table_entry__set_last_match(table, res);
	}
out_unlock:
	up_write(&table->lock);
	return res;
}

void threads__remove_all_threads(struct threads *threads)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];
		struct rb_node *nd;

		down_write(&table->lock);
		__threads_table_entry__set_last_match(table, NULL);
		nd = rb_first_cached(&table->entries);
		while (nd) {
			struct thread_rb_node *trb = rb_entry(nd, struct thread_rb_node, rb_node);

			nd = rb_next(nd);
			thread__put(trb->thread);
			rb_erase_cached(&trb->rb_node, &table->entries);
			RB_CLEAR_NODE(&trb->rb_node);
			--table->nr;

			free(trb);
		}
		assert(table->nr == 0);
		up_write(&table->lock);
	}
}

void threads__remove(struct threads *threads, struct thread *thread)
{
	struct rb_node **p;
	struct threads_table_entry *table  = threads__table(threads, thread__tid(thread));
	pid_t tid = thread__tid(thread);

	down_write(&table->lock);
	if (table->last_match && RC_CHK_EQUAL(table->last_match, thread))
		__threads_table_entry__set_last_match(table, NULL);

	p = &table->entries.rb_root.rb_node;
	while (*p != NULL) {
		struct rb_node *parent = *p;
		struct thread_rb_node *nd = rb_entry(parent, struct thread_rb_node, rb_node);
		struct thread *th = nd->thread;

		if (RC_CHK_EQUAL(th, thread)) {
			thread__put(nd->thread);
			rb_erase_cached(&nd->rb_node, &table->entries);
			RB_CLEAR_NODE(&nd->rb_node);
			--table->nr;
			free(nd);
			break;
		}

		if (tid < thread__tid(th))
			p = &(*p)->rb_left;
		else
			p = &(*p)->rb_right;
	}
	up_write(&table->lock);
}

int threads__for_each_thread(struct threads *threads,
			     int (*fn)(struct thread *thread, void *data),
			     void *data)
{
	for (int i = 0; i < THREADS__TABLE_SIZE; i++) {
		struct threads_table_entry *table = &threads->table[i];
		struct rb_node *nd;

		down_read(&table->lock);
		for (nd = rb_first_cached(&table->entries); nd; nd = rb_next(nd)) {
			struct thread_rb_node *trb = rb_entry(nd, struct thread_rb_node, rb_node);
			int rc = fn(trb->thread, data);

			if (rc != 0) {
				up_read(&table->lock);
				return rc;
			}
		}
		up_read(&table->lock);
	}
	return 0;

}
