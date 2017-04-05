/*
 * Module-based API test facility for ww_mutexes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 */

#include <linux/kernel.h>

#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/ww_mutex.h>

static DEFINE_WW_CLASS(ww_class);
struct workqueue_struct *wq;

struct test_mutex {
	struct work_struct work;
	struct ww_mutex mutex;
	struct completion ready, go, done;
	unsigned int flags;
};

#define TEST_MTX_SPIN BIT(0)
#define TEST_MTX_TRY BIT(1)
#define TEST_MTX_CTX BIT(2)
#define __TEST_MTX_LAST BIT(3)

static void test_mutex_work(struct work_struct *work)
{
	struct test_mutex *mtx = container_of(work, typeof(*mtx), work);

	complete(&mtx->ready);
	wait_for_completion(&mtx->go);

	if (mtx->flags & TEST_MTX_TRY) {
		while (!ww_mutex_trylock(&mtx->mutex))
			cond_resched();
	} else {
		ww_mutex_lock(&mtx->mutex, NULL);
	}
	complete(&mtx->done);
	ww_mutex_unlock(&mtx->mutex);
}

static int __test_mutex(unsigned int flags)
{
#define TIMEOUT (HZ / 16)
	struct test_mutex mtx;
	struct ww_acquire_ctx ctx;
	int ret;

	ww_mutex_init(&mtx.mutex, &ww_class);
	ww_acquire_init(&ctx, &ww_class);

	INIT_WORK_ONSTACK(&mtx.work, test_mutex_work);
	init_completion(&mtx.ready);
	init_completion(&mtx.go);
	init_completion(&mtx.done);
	mtx.flags = flags;

	schedule_work(&mtx.work);

	wait_for_completion(&mtx.ready);
	ww_mutex_lock(&mtx.mutex, (flags & TEST_MTX_CTX) ? &ctx : NULL);
	complete(&mtx.go);
	if (flags & TEST_MTX_SPIN) {
		unsigned long timeout = jiffies + TIMEOUT;

		ret = 0;
		do {
			if (completion_done(&mtx.done)) {
				ret = -EINVAL;
				break;
			}
			cond_resched();
		} while (time_before(jiffies, timeout));
	} else {
		ret = wait_for_completion_timeout(&mtx.done, TIMEOUT);
	}
	ww_mutex_unlock(&mtx.mutex);
	ww_acquire_fini(&ctx);

	if (ret) {
		pr_err("%s(flags=%x): mutual exclusion failure\n",
		       __func__, flags);
		ret = -EINVAL;
	}

	flush_work(&mtx.work);
	destroy_work_on_stack(&mtx.work);
	return ret;
#undef TIMEOUT
}

static int test_mutex(void)
{
	int ret;
	int i;

	for (i = 0; i < __TEST_MTX_LAST; i++) {
		ret = __test_mutex(i);
		if (ret)
			return ret;
	}

	return 0;
}

static int test_aa(void)
{
	struct ww_mutex mutex;
	struct ww_acquire_ctx ctx;
	int ret;

	ww_mutex_init(&mutex, &ww_class);
	ww_acquire_init(&ctx, &ww_class);

	ww_mutex_lock(&mutex, &ctx);

	if (ww_mutex_trylock(&mutex))  {
		pr_err("%s: trylocked itself!\n", __func__);
		ww_mutex_unlock(&mutex);
		ret = -EINVAL;
		goto out;
	}

	ret = ww_mutex_lock(&mutex, &ctx);
	if (ret != -EALREADY) {
		pr_err("%s: missed deadlock for recursing, ret=%d\n",
		       __func__, ret);
		if (!ret)
			ww_mutex_unlock(&mutex);
		ret = -EINVAL;
		goto out;
	}

	ret = 0;
out:
	ww_mutex_unlock(&mutex);
	ww_acquire_fini(&ctx);
	return ret;
}

struct test_abba {
	struct work_struct work;
	struct ww_mutex a_mutex;
	struct ww_mutex b_mutex;
	struct completion a_ready;
	struct completion b_ready;
	bool resolve;
	int result;
};

static void test_abba_work(struct work_struct *work)
{
	struct test_abba *abba = container_of(work, typeof(*abba), work);
	struct ww_acquire_ctx ctx;
	int err;

	ww_acquire_init(&ctx, &ww_class);
	ww_mutex_lock(&abba->b_mutex, &ctx);

	complete(&abba->b_ready);
	wait_for_completion(&abba->a_ready);

	err = ww_mutex_lock(&abba->a_mutex, &ctx);
	if (abba->resolve && err == -EDEADLK) {
		ww_mutex_unlock(&abba->b_mutex);
		ww_mutex_lock_slow(&abba->a_mutex, &ctx);
		err = ww_mutex_lock(&abba->b_mutex, &ctx);
	}

	if (!err)
		ww_mutex_unlock(&abba->a_mutex);
	ww_mutex_unlock(&abba->b_mutex);
	ww_acquire_fini(&ctx);

	abba->result = err;
}

static int test_abba(bool resolve)
{
	struct test_abba abba;
	struct ww_acquire_ctx ctx;
	int err, ret;

	ww_mutex_init(&abba.a_mutex, &ww_class);
	ww_mutex_init(&abba.b_mutex, &ww_class);
	INIT_WORK_ONSTACK(&abba.work, test_abba_work);
	init_completion(&abba.a_ready);
	init_completion(&abba.b_ready);
	abba.resolve = resolve;

	schedule_work(&abba.work);

	ww_acquire_init(&ctx, &ww_class);
	ww_mutex_lock(&abba.a_mutex, &ctx);

	complete(&abba.a_ready);
	wait_for_completion(&abba.b_ready);

	err = ww_mutex_lock(&abba.b_mutex, &ctx);
	if (resolve && err == -EDEADLK) {
		ww_mutex_unlock(&abba.a_mutex);
		ww_mutex_lock_slow(&abba.b_mutex, &ctx);
		err = ww_mutex_lock(&abba.a_mutex, &ctx);
	}

	if (!err)
		ww_mutex_unlock(&abba.b_mutex);
	ww_mutex_unlock(&abba.a_mutex);
	ww_acquire_fini(&ctx);

	flush_work(&abba.work);
	destroy_work_on_stack(&abba.work);

	ret = 0;
	if (resolve) {
		if (err || abba.result) {
			pr_err("%s: failed to resolve ABBA deadlock, A err=%d, B err=%d\n",
			       __func__, err, abba.result);
			ret = -EINVAL;
		}
	} else {
		if (err != -EDEADLK && abba.result != -EDEADLK) {
			pr_err("%s: missed ABBA deadlock, A err=%d, B err=%d\n",
			       __func__, err, abba.result);
			ret = -EINVAL;
		}
	}
	return ret;
}

struct test_cycle {
	struct work_struct work;
	struct ww_mutex a_mutex;
	struct ww_mutex *b_mutex;
	struct completion *a_signal;
	struct completion b_signal;
	int result;
};

static void test_cycle_work(struct work_struct *work)
{
	struct test_cycle *cycle = container_of(work, typeof(*cycle), work);
	struct ww_acquire_ctx ctx;
	int err;

	ww_acquire_init(&ctx, &ww_class);
	ww_mutex_lock(&cycle->a_mutex, &ctx);

	complete(cycle->a_signal);
	wait_for_completion(&cycle->b_signal);

	err = ww_mutex_lock(cycle->b_mutex, &ctx);
	if (err == -EDEADLK) {
		ww_mutex_unlock(&cycle->a_mutex);
		ww_mutex_lock_slow(cycle->b_mutex, &ctx);
		err = ww_mutex_lock(&cycle->a_mutex, &ctx);
	}

	if (!err)
		ww_mutex_unlock(cycle->b_mutex);
	ww_mutex_unlock(&cycle->a_mutex);
	ww_acquire_fini(&ctx);

	cycle->result = err;
}

static int __test_cycle(unsigned int nthreads)
{
	struct test_cycle *cycles;
	unsigned int n, last = nthreads - 1;
	int ret;

	cycles = kmalloc_array(nthreads, sizeof(*cycles), GFP_KERNEL);
	if (!cycles)
		return -ENOMEM;

	for (n = 0; n < nthreads; n++) {
		struct test_cycle *cycle = &cycles[n];

		ww_mutex_init(&cycle->a_mutex, &ww_class);
		if (n == last)
			cycle->b_mutex = &cycles[0].a_mutex;
		else
			cycle->b_mutex = &cycles[n + 1].a_mutex;

		if (n == 0)
			cycle->a_signal = &cycles[last].b_signal;
		else
			cycle->a_signal = &cycles[n - 1].b_signal;
		init_completion(&cycle->b_signal);

		INIT_WORK(&cycle->work, test_cycle_work);
		cycle->result = 0;
	}

	for (n = 0; n < nthreads; n++)
		queue_work(wq, &cycles[n].work);

	flush_workqueue(wq);

	ret = 0;
	for (n = 0; n < nthreads; n++) {
		struct test_cycle *cycle = &cycles[n];

		if (!cycle->result)
			continue;

		pr_err("cylic deadlock not resolved, ret[%d/%d] = %d\n",
		       n, nthreads, cycle->result);
		ret = -EINVAL;
		break;
	}

	for (n = 0; n < nthreads; n++)
		ww_mutex_destroy(&cycles[n].a_mutex);
	kfree(cycles);
	return ret;
}

static int test_cycle(unsigned int ncpus)
{
	unsigned int n;
	int ret;

	for (n = 2; n <= ncpus + 1; n++) {
		ret = __test_cycle(n);
		if (ret)
			return ret;
	}

	return 0;
}

struct stress {
	struct work_struct work;
	struct ww_mutex *locks;
	int nlocks;
	int nloops;
};

static int *get_random_order(int count)
{
	int *order;
	int n, r, tmp;

	order = kmalloc_array(count, sizeof(*order), GFP_TEMPORARY);
	if (!order)
		return order;

	for (n = 0; n < count; n++)
		order[n] = n;

	for (n = count - 1; n > 1; n--) {
		r = get_random_int() % (n + 1);
		if (r != n) {
			tmp = order[n];
			order[n] = order[r];
			order[r] = tmp;
		}
	}

	return order;
}

static void dummy_load(struct stress *stress)
{
	usleep_range(1000, 2000);
}

static void stress_inorder_work(struct work_struct *work)
{
	struct stress *stress = container_of(work, typeof(*stress), work);
	const int nlocks = stress->nlocks;
	struct ww_mutex *locks = stress->locks;
	struct ww_acquire_ctx ctx;
	int *order;

	order = get_random_order(nlocks);
	if (!order)
		return;

	ww_acquire_init(&ctx, &ww_class);

	do {
		int contended = -1;
		int n, err;

retry:
		err = 0;
		for (n = 0; n < nlocks; n++) {
			if (n == contended)
				continue;

			err = ww_mutex_lock(&locks[order[n]], &ctx);
			if (err < 0)
				break;
		}
		if (!err)
			dummy_load(stress);

		if (contended > n)
			ww_mutex_unlock(&locks[order[contended]]);
		contended = n;
		while (n--)
			ww_mutex_unlock(&locks[order[n]]);

		if (err == -EDEADLK) {
			ww_mutex_lock_slow(&locks[order[contended]], &ctx);
			goto retry;
		}

		if (err) {
			pr_err_once("stress (%s) failed with %d\n",
				    __func__, err);
			break;
		}
	} while (--stress->nloops);

	ww_acquire_fini(&ctx);

	kfree(order);
	kfree(stress);
}

struct reorder_lock {
	struct list_head link;
	struct ww_mutex *lock;
};

static void stress_reorder_work(struct work_struct *work)
{
	struct stress *stress = container_of(work, typeof(*stress), work);
	LIST_HEAD(locks);
	struct ww_acquire_ctx ctx;
	struct reorder_lock *ll, *ln;
	int *order;
	int n, err;

	order = get_random_order(stress->nlocks);
	if (!order)
		return;

	for (n = 0; n < stress->nlocks; n++) {
		ll = kmalloc(sizeof(*ll), GFP_KERNEL);
		if (!ll)
			goto out;

		ll->lock = &stress->locks[order[n]];
		list_add(&ll->link, &locks);
	}
	kfree(order);
	order = NULL;

	ww_acquire_init(&ctx, &ww_class);

	do {
		list_for_each_entry(ll, &locks, link) {
			err = ww_mutex_lock(ll->lock, &ctx);
			if (!err)
				continue;

			ln = ll;
			list_for_each_entry_continue_reverse(ln, &locks, link)
				ww_mutex_unlock(ln->lock);

			if (err != -EDEADLK) {
				pr_err_once("stress (%s) failed with %d\n",
					    __func__, err);
				break;
			}

			ww_mutex_lock_slow(ll->lock, &ctx);
			list_move(&ll->link, &locks); /* restarts iteration */
		}

		dummy_load(stress);
		list_for_each_entry(ll, &locks, link)
			ww_mutex_unlock(ll->lock);
	} while (--stress->nloops);

	ww_acquire_fini(&ctx);

out:
	list_for_each_entry_safe(ll, ln, &locks, link)
		kfree(ll);
	kfree(order);
	kfree(stress);
}

static void stress_one_work(struct work_struct *work)
{
	struct stress *stress = container_of(work, typeof(*stress), work);
	const int nlocks = stress->nlocks;
	struct ww_mutex *lock = stress->locks + (get_random_int() % nlocks);
	int err;

	do {
		err = ww_mutex_lock(lock, NULL);
		if (!err) {
			dummy_load(stress);
			ww_mutex_unlock(lock);
		} else {
			pr_err_once("stress (%s) failed with %d\n",
				    __func__, err);
			break;
		}
	} while (--stress->nloops);

	kfree(stress);
}

#define STRESS_INORDER BIT(0)
#define STRESS_REORDER BIT(1)
#define STRESS_ONE BIT(2)
#define STRESS_ALL (STRESS_INORDER | STRESS_REORDER | STRESS_ONE)

static int stress(int nlocks, int nthreads, int nloops, unsigned int flags)
{
	struct ww_mutex *locks;
	int n;

	locks = kmalloc_array(nlocks, sizeof(*locks), GFP_KERNEL);
	if (!locks)
		return -ENOMEM;

	for (n = 0; n < nlocks; n++)
		ww_mutex_init(&locks[n], &ww_class);

	for (n = 0; nthreads; n++) {
		struct stress *stress;
		void (*fn)(struct work_struct *work);

		fn = NULL;
		switch (n & 3) {
		case 0:
			if (flags & STRESS_INORDER)
				fn = stress_inorder_work;
			break;
		case 1:
			if (flags & STRESS_REORDER)
				fn = stress_reorder_work;
			break;
		case 2:
			if (flags & STRESS_ONE)
				fn = stress_one_work;
			break;
		}

		if (!fn)
			continue;

		stress = kmalloc(sizeof(*stress), GFP_KERNEL);
		if (!stress)
			break;

		INIT_WORK(&stress->work, fn);
		stress->locks = locks;
		stress->nlocks = nlocks;
		stress->nloops = nloops;

		queue_work(wq, &stress->work);
		nthreads--;
	}

	flush_workqueue(wq);

	for (n = 0; n < nlocks; n++)
		ww_mutex_destroy(&locks[n]);
	kfree(locks);

	return 0;
}

static int __init test_ww_mutex_init(void)
{
	int ncpus = num_online_cpus();
	int ret;

	wq = alloc_workqueue("test-ww_mutex", WQ_UNBOUND, 0);
	if (!wq)
		return -ENOMEM;

	ret = test_mutex();
	if (ret)
		return ret;

	ret = test_aa();
	if (ret)
		return ret;

	ret = test_abba(false);
	if (ret)
		return ret;

	ret = test_abba(true);
	if (ret)
		return ret;

	ret = test_cycle(ncpus);
	if (ret)
		return ret;

	ret = stress(16, 2*ncpus, 1<<10, STRESS_INORDER);
	if (ret)
		return ret;

	ret = stress(16, 2*ncpus, 1<<10, STRESS_REORDER);
	if (ret)
		return ret;

	ret = stress(4095, hweight32(STRESS_ALL)*ncpus, 1<<12, STRESS_ALL);
	if (ret)
		return ret;

	return 0;
}

static void __exit test_ww_mutex_exit(void)
{
	destroy_workqueue(wq);
}

module_init(test_ww_mutex_init);
module_exit(test_ww_mutex_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");
