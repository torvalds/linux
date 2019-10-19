// SPDX-License-Identifier: GPL-2.0
/*
 * Regression1
 * Description:
 * Salman Qazi describes the following radix-tree bug:
 *
 * In the following case, we get can get a deadlock:
 *
 * 0.  The radix tree contains two items, one has the index 0.
 * 1.  The reader (in this case find_get_pages) takes the rcu_read_lock.
 * 2.  The reader acquires slot(s) for item(s) including the index 0 item.
 * 3.  The non-zero index item is deleted, and as a consequence the other item
 *     is moved to the root of the tree. The place where it used to be is queued
 *     for deletion after the readers finish.
 * 3b. The zero item is deleted, removing it from the direct slot, it remains in
 *     the rcu-delayed indirect node.
 * 4.  The reader looks at the index 0 slot, and finds that the page has 0 ref
 *     count
 * 5.  The reader looks at it again, hoping that the item will either be freed
 *     or the ref count will increase. This never happens, as the slot it is
 *     looking at will never be updated. Also, this slot can never be reclaimed
 *     because the reader is holding rcu_read_lock and is in an infinite loop.
 *
 * The fix is to re-use the same "indirect" pointer case that requires a slot
 * lookup retry into a general "retry the lookup" bit.
 *
 * Running:
 * This test should run to completion in a few seconds. The above bug would
 * cause it to hang indefinitely.
 *
 * Upstream commit:
 * Not yet
 */
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/radix-tree.h>
#include <linux/rcupdate.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdio.h>
#include <assert.h>

#include "regression.h"

static RADIX_TREE(mt_tree, GFP_KERNEL);

struct page {
	pthread_mutex_t lock;
	struct rcu_head rcu;
	int count;
	unsigned long index;
};

static struct page *page_alloc(int index)
{
	struct page *p;
	p = malloc(sizeof(struct page));
	p->count = 1;
	p->index = index;
	pthread_mutex_init(&p->lock, NULL);

	return p;
}

static void page_rcu_free(struct rcu_head *rcu)
{
	struct page *p = container_of(rcu, struct page, rcu);
	assert(!p->count);
	pthread_mutex_destroy(&p->lock);
	free(p);
}

static void page_free(struct page *p)
{
	call_rcu(&p->rcu, page_rcu_free);
}

static unsigned find_get_pages(unsigned long start,
			    unsigned int nr_pages, struct page **pages)
{
	XA_STATE(xas, &mt_tree, start);
	struct page *page;
	unsigned int ret = 0;

	rcu_read_lock();
	xas_for_each(&xas, page, ULONG_MAX) {
		if (xas_retry(&xas, page))
			continue;

		pthread_mutex_lock(&page->lock);
		if (!page->count)
			goto unlock;

		/* don't actually update page refcount */
		pthread_mutex_unlock(&page->lock);

		/* Has the page moved? */
		if (unlikely(page != xas_reload(&xas)))
			goto put_page;

		pages[ret] = page;
		ret++;
		continue;
unlock:
		pthread_mutex_unlock(&page->lock);
put_page:
		xas_reset(&xas);
	}
	rcu_read_unlock();
	return ret;
}

static pthread_barrier_t worker_barrier;

static void *regression1_fn(void *arg)
{
	rcu_register_thread();

	if (pthread_barrier_wait(&worker_barrier) ==
			PTHREAD_BARRIER_SERIAL_THREAD) {
		int j;

		for (j = 0; j < 1000000; j++) {
			struct page *p;

			p = page_alloc(0);
			xa_lock(&mt_tree);
			radix_tree_insert(&mt_tree, 0, p);
			xa_unlock(&mt_tree);

			p = page_alloc(1);
			xa_lock(&mt_tree);
			radix_tree_insert(&mt_tree, 1, p);
			xa_unlock(&mt_tree);

			xa_lock(&mt_tree);
			p = radix_tree_delete(&mt_tree, 1);
			pthread_mutex_lock(&p->lock);
			p->count--;
			pthread_mutex_unlock(&p->lock);
			xa_unlock(&mt_tree);
			page_free(p);

			xa_lock(&mt_tree);
			p = radix_tree_delete(&mt_tree, 0);
			pthread_mutex_lock(&p->lock);
			p->count--;
			pthread_mutex_unlock(&p->lock);
			xa_unlock(&mt_tree);
			page_free(p);
		}
	} else {
		int j;

		for (j = 0; j < 100000000; j++) {
			struct page *pages[10];

			find_get_pages(0, 10, pages);
		}
	}

	rcu_unregister_thread();

	return NULL;
}

static pthread_t *threads;
void regression1_test(void)
{
	int nr_threads;
	int i;
	long arg;

	/* Regression #1 */
	printv(1, "running regression test 1, should finish in under a minute\n");
	nr_threads = 2;
	pthread_barrier_init(&worker_barrier, NULL, nr_threads);

	threads = malloc(nr_threads * sizeof(pthread_t *));

	for (i = 0; i < nr_threads; i++) {
		arg = i;
		if (pthread_create(&threads[i], NULL, regression1_fn, (void *)arg)) {
			perror("pthread_create");
			exit(1);
		}
	}

	for (i = 0; i < nr_threads; i++) {
		if (pthread_join(threads[i], NULL)) {
			perror("pthread_join");
			exit(1);
		}
	}

	free(threads);

	printv(1, "regression test 1, done\n");
}
