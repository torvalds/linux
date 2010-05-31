/*
 * padata.c - generic interface to process data streams in parallel
 *
 * Copyright (C) 2008, 2009 secunet Security Networks AG
 * Copyright (C) 2008, 2009 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/padata.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/rcupdate.h>

#define MAX_SEQ_NR INT_MAX - NR_CPUS
#define MAX_OBJ_NUM 1000

static int padata_index_to_cpu(struct parallel_data *pd, int cpu_index)
{
	int cpu, target_cpu;

	target_cpu = cpumask_first(pd->cpumask);
	for (cpu = 0; cpu < cpu_index; cpu++)
		target_cpu = cpumask_next(target_cpu, pd->cpumask);

	return target_cpu;
}

static int padata_cpu_hash(struct padata_priv *padata)
{
	int cpu_index;
	struct parallel_data *pd;

	pd =  padata->pd;

	/*
	 * Hash the sequence numbers to the cpus by taking
	 * seq_nr mod. number of cpus in use.
	 */
	cpu_index =  padata->seq_nr % cpumask_weight(pd->cpumask);

	return padata_index_to_cpu(pd, cpu_index);
}

static void padata_parallel_worker(struct work_struct *work)
{
	struct padata_queue *queue;
	struct parallel_data *pd;
	struct padata_instance *pinst;
	LIST_HEAD(local_list);

	local_bh_disable();
	queue = container_of(work, struct padata_queue, pwork);
	pd = queue->pd;
	pinst = pd->pinst;

	spin_lock(&queue->parallel.lock);
	list_replace_init(&queue->parallel.list, &local_list);
	spin_unlock(&queue->parallel.lock);

	while (!list_empty(&local_list)) {
		struct padata_priv *padata;

		padata = list_entry(local_list.next,
				    struct padata_priv, list);

		list_del_init(&padata->list);

		padata->parallel(padata);
	}

	local_bh_enable();
}

/**
 * padata_do_parallel - padata parallelization function
 *
 * @pinst: padata instance
 * @padata: object to be parallelized
 * @cb_cpu: cpu the serialization callback function will run on,
 *          must be in the cpumask of padata.
 *
 * The parallelization callback function will run with BHs off.
 * Note: Every object which is parallelized by padata_do_parallel
 * must be seen by padata_do_serial.
 */
int padata_do_parallel(struct padata_instance *pinst,
		       struct padata_priv *padata, int cb_cpu)
{
	int target_cpu, err;
	struct padata_queue *queue;
	struct parallel_data *pd;

	rcu_read_lock_bh();

	pd = rcu_dereference(pinst->pd);

	err = 0;
	if (!(pinst->flags & PADATA_INIT))
		goto out;

	err =  -EBUSY;
	if ((pinst->flags & PADATA_RESET))
		goto out;

	if (atomic_read(&pd->refcnt) >= MAX_OBJ_NUM)
		goto out;

	err = -EINVAL;
	if (!cpumask_test_cpu(cb_cpu, pd->cpumask))
		goto out;

	err = -EINPROGRESS;
	atomic_inc(&pd->refcnt);
	padata->pd = pd;
	padata->cb_cpu = cb_cpu;

	if (unlikely(atomic_read(&pd->seq_nr) == pd->max_seq_nr))
		atomic_set(&pd->seq_nr, -1);

	padata->seq_nr = atomic_inc_return(&pd->seq_nr);

	target_cpu = padata_cpu_hash(padata);
	queue = per_cpu_ptr(pd->queue, target_cpu);

	spin_lock(&queue->parallel.lock);
	list_add_tail(&padata->list, &queue->parallel.list);
	spin_unlock(&queue->parallel.lock);

	queue_work_on(target_cpu, pinst->wq, &queue->pwork);

out:
	rcu_read_unlock_bh();

	return err;
}
EXPORT_SYMBOL(padata_do_parallel);

/*
 * padata_get_next - Get the next object that needs serialization.
 *
 * Return values are:
 *
 * A pointer to the control struct of the next object that needs
 * serialization, if present in one of the percpu reorder queues.
 *
 * NULL, if all percpu reorder queues are empty.
 *
 * -EINPROGRESS, if the next object that needs serialization will
 *  be parallel processed by another cpu and is not yet present in
 *  the cpu's reorder queue.
 *
 * -ENODATA, if this cpu has to do the parallel processing for
 *  the next object.
 */
static struct padata_priv *padata_get_next(struct parallel_data *pd)
{
	int cpu, num_cpus, empty, calc_seq_nr;
	int seq_nr, next_nr, overrun, next_overrun;
	struct padata_queue *queue, *next_queue;
	struct padata_priv *padata;
	struct padata_list *reorder;

	empty = 0;
	next_nr = -1;
	next_overrun = 0;
	next_queue = NULL;

	num_cpus = cpumask_weight(pd->cpumask);

	for_each_cpu(cpu, pd->cpumask) {
		queue = per_cpu_ptr(pd->queue, cpu);
		reorder = &queue->reorder;

		/*
		 * Calculate the seq_nr of the object that should be
		 * next in this reorder queue.
		 */
		overrun = 0;
		calc_seq_nr = (atomic_read(&queue->num_obj) * num_cpus)
			       + queue->cpu_index;

		if (unlikely(calc_seq_nr > pd->max_seq_nr)) {
			calc_seq_nr = calc_seq_nr - pd->max_seq_nr - 1;
			overrun = 1;
		}

		if (!list_empty(&reorder->list)) {
			padata = list_entry(reorder->list.next,
					    struct padata_priv, list);

			seq_nr  = padata->seq_nr;
			BUG_ON(calc_seq_nr != seq_nr);
		} else {
			seq_nr = calc_seq_nr;
			empty++;
		}

		if (next_nr < 0 || seq_nr < next_nr
		    || (next_overrun && !overrun)) {
			next_nr = seq_nr;
			next_overrun = overrun;
			next_queue = queue;
		}
	}

	padata = NULL;

	if (empty == num_cpus)
		goto out;

	reorder = &next_queue->reorder;

	if (!list_empty(&reorder->list)) {
		padata = list_entry(reorder->list.next,
				    struct padata_priv, list);

		if (unlikely(next_overrun)) {
			for_each_cpu(cpu, pd->cpumask) {
				queue = per_cpu_ptr(pd->queue, cpu);
				atomic_set(&queue->num_obj, 0);
			}
		}

		spin_lock(&reorder->lock);
		list_del_init(&padata->list);
		atomic_dec(&pd->reorder_objects);
		spin_unlock(&reorder->lock);

		atomic_inc(&next_queue->num_obj);

		goto out;
	}

	queue = per_cpu_ptr(pd->queue, smp_processor_id());
	if (queue->cpu_index == next_queue->cpu_index) {
		padata = ERR_PTR(-ENODATA);
		goto out;
	}

	padata = ERR_PTR(-EINPROGRESS);
out:
	return padata;
}

static void padata_reorder(struct parallel_data *pd)
{
	struct padata_priv *padata;
	struct padata_queue *queue;
	struct padata_instance *pinst = pd->pinst;

	/*
	 * We need to ensure that only one cpu can work on dequeueing of
	 * the reorder queue the time. Calculating in which percpu reorder
	 * queue the next object will arrive takes some time. A spinlock
	 * would be highly contended. Also it is not clear in which order
	 * the objects arrive to the reorder queues. So a cpu could wait to
	 * get the lock just to notice that there is nothing to do at the
	 * moment. Therefore we use a trylock and let the holder of the lock
	 * care for all the objects enqueued during the holdtime of the lock.
	 */
	if (!spin_trylock_bh(&pd->lock))
		return;

	while (1) {
		padata = padata_get_next(pd);

		/*
		 * All reorder queues are empty, or the next object that needs
		 * serialization is parallel processed by another cpu and is
		 * still on it's way to the cpu's reorder queue, nothing to
		 * do for now.
		 */
		if (!padata || PTR_ERR(padata) == -EINPROGRESS)
			break;

		/*
		 * This cpu has to do the parallel processing of the next
		 * object. It's waiting in the cpu's parallelization queue,
		 * so exit imediately.
		 */
		if (PTR_ERR(padata) == -ENODATA) {
			del_timer(&pd->timer);
			spin_unlock_bh(&pd->lock);
			return;
		}

		queue = per_cpu_ptr(pd->queue, padata->cb_cpu);

		spin_lock(&queue->serial.lock);
		list_add_tail(&padata->list, &queue->serial.list);
		spin_unlock(&queue->serial.lock);

		queue_work_on(padata->cb_cpu, pinst->wq, &queue->swork);
	}

	spin_unlock_bh(&pd->lock);

	/*
	 * The next object that needs serialization might have arrived to
	 * the reorder queues in the meantime, we will be called again
	 * from the timer function if noone else cares for it.
	 */
	if (atomic_read(&pd->reorder_objects)
			&& !(pinst->flags & PADATA_RESET))
		mod_timer(&pd->timer, jiffies + HZ);
	else
		del_timer(&pd->timer);

	return;
}

static void padata_reorder_timer(unsigned long arg)
{
	struct parallel_data *pd = (struct parallel_data *)arg;

	padata_reorder(pd);
}

static void padata_serial_worker(struct work_struct *work)
{
	struct padata_queue *queue;
	struct parallel_data *pd;
	LIST_HEAD(local_list);

	local_bh_disable();
	queue = container_of(work, struct padata_queue, swork);
	pd = queue->pd;

	spin_lock(&queue->serial.lock);
	list_replace_init(&queue->serial.list, &local_list);
	spin_unlock(&queue->serial.lock);

	while (!list_empty(&local_list)) {
		struct padata_priv *padata;

		padata = list_entry(local_list.next,
				    struct padata_priv, list);

		list_del_init(&padata->list);

		padata->serial(padata);
		atomic_dec(&pd->refcnt);
	}
	local_bh_enable();
}

/**
 * padata_do_serial - padata serialization function
 *
 * @padata: object to be serialized.
 *
 * padata_do_serial must be called for every parallelized object.
 * The serialization callback function will run with BHs off.
 */
void padata_do_serial(struct padata_priv *padata)
{
	int cpu;
	struct padata_queue *queue;
	struct parallel_data *pd;

	pd = padata->pd;

	cpu = get_cpu();
	queue = per_cpu_ptr(pd->queue, cpu);

	spin_lock(&queue->reorder.lock);
	atomic_inc(&pd->reorder_objects);
	list_add_tail(&padata->list, &queue->reorder.list);
	spin_unlock(&queue->reorder.lock);

	put_cpu();

	padata_reorder(pd);
}
EXPORT_SYMBOL(padata_do_serial);

/* Allocate and initialize the internal cpumask dependend resources. */
static struct parallel_data *padata_alloc_pd(struct padata_instance *pinst,
					     const struct cpumask *cpumask)
{
	int cpu, cpu_index, num_cpus;
	struct padata_queue *queue;
	struct parallel_data *pd;

	cpu_index = 0;

	pd = kzalloc(sizeof(struct parallel_data), GFP_KERNEL);
	if (!pd)
		goto err;

	pd->queue = alloc_percpu(struct padata_queue);
	if (!pd->queue)
		goto err_free_pd;

	if (!alloc_cpumask_var(&pd->cpumask, GFP_KERNEL))
		goto err_free_queue;

	cpumask_and(pd->cpumask, cpumask, cpu_active_mask);

	for_each_cpu(cpu, pd->cpumask) {
		queue = per_cpu_ptr(pd->queue, cpu);

		queue->pd = pd;

		queue->cpu_index = cpu_index;
		cpu_index++;

		INIT_LIST_HEAD(&queue->reorder.list);
		INIT_LIST_HEAD(&queue->parallel.list);
		INIT_LIST_HEAD(&queue->serial.list);
		spin_lock_init(&queue->reorder.lock);
		spin_lock_init(&queue->parallel.lock);
		spin_lock_init(&queue->serial.lock);

		INIT_WORK(&queue->pwork, padata_parallel_worker);
		INIT_WORK(&queue->swork, padata_serial_worker);
		atomic_set(&queue->num_obj, 0);
	}

	num_cpus = cpumask_weight(pd->cpumask);
	pd->max_seq_nr = (MAX_SEQ_NR / num_cpus) * num_cpus - 1;

	setup_timer(&pd->timer, padata_reorder_timer, (unsigned long)pd);
	atomic_set(&pd->seq_nr, -1);
	atomic_set(&pd->reorder_objects, 0);
	atomic_set(&pd->refcnt, 0);
	pd->pinst = pinst;
	spin_lock_init(&pd->lock);

	return pd;

err_free_queue:
	free_percpu(pd->queue);
err_free_pd:
	kfree(pd);
err:
	return NULL;
}

static void padata_free_pd(struct parallel_data *pd)
{
	free_cpumask_var(pd->cpumask);
	free_percpu(pd->queue);
	kfree(pd);
}

/* Flush all objects out of the padata queues. */
static void padata_flush_queues(struct parallel_data *pd)
{
	int cpu;
	struct padata_queue *queue;

	for_each_cpu(cpu, pd->cpumask) {
		queue = per_cpu_ptr(pd->queue, cpu);
		flush_work(&queue->pwork);
	}

	del_timer_sync(&pd->timer);

	if (atomic_read(&pd->reorder_objects))
		padata_reorder(pd);

	for_each_cpu(cpu, pd->cpumask) {
		queue = per_cpu_ptr(pd->queue, cpu);
		flush_work(&queue->swork);
	}

	BUG_ON(atomic_read(&pd->refcnt) != 0);
}

/* Replace the internal control stucture with a new one. */
static void padata_replace(struct padata_instance *pinst,
			   struct parallel_data *pd_new)
{
	struct parallel_data *pd_old = pinst->pd;

	pinst->flags |= PADATA_RESET;

	rcu_assign_pointer(pinst->pd, pd_new);

	synchronize_rcu();

	padata_flush_queues(pd_old);
	padata_free_pd(pd_old);

	pinst->flags &= ~PADATA_RESET;
}

/**
 * padata_set_cpumask - set the cpumask that padata should use
 *
 * @pinst: padata instance
 * @cpumask: the cpumask to use
 */
int padata_set_cpumask(struct padata_instance *pinst,
			cpumask_var_t cpumask)
{
	struct parallel_data *pd;
	int err = 0;

	mutex_lock(&pinst->lock);

	get_online_cpus();

	pd = padata_alloc_pd(pinst, cpumask);
	if (!pd) {
		err = -ENOMEM;
		goto out;
	}

	cpumask_copy(pinst->cpumask, cpumask);

	padata_replace(pinst, pd);

out:
	put_online_cpus();

	mutex_unlock(&pinst->lock);

	return err;
}
EXPORT_SYMBOL(padata_set_cpumask);

static int __padata_add_cpu(struct padata_instance *pinst, int cpu)
{
	struct parallel_data *pd;

	if (cpumask_test_cpu(cpu, cpu_active_mask)) {
		pd = padata_alloc_pd(pinst, pinst->cpumask);
		if (!pd)
			return -ENOMEM;

		padata_replace(pinst, pd);
	}

	return 0;
}

/**
 * padata_add_cpu - add a cpu to the padata cpumask
 *
 * @pinst: padata instance
 * @cpu: cpu to add
 */
int padata_add_cpu(struct padata_instance *pinst, int cpu)
{
	int err;

	mutex_lock(&pinst->lock);

	get_online_cpus();
	cpumask_set_cpu(cpu, pinst->cpumask);
	err = __padata_add_cpu(pinst, cpu);
	put_online_cpus();

	mutex_unlock(&pinst->lock);

	return err;
}
EXPORT_SYMBOL(padata_add_cpu);

static int __padata_remove_cpu(struct padata_instance *pinst, int cpu)
{
	struct parallel_data *pd;

	if (cpumask_test_cpu(cpu, cpu_online_mask)) {
		pd = padata_alloc_pd(pinst, pinst->cpumask);
		if (!pd)
			return -ENOMEM;

		padata_replace(pinst, pd);
	}

	return 0;
}

/**
 * padata_remove_cpu - remove a cpu from the padata cpumask
 *
 * @pinst: padata instance
 * @cpu: cpu to remove
 */
int padata_remove_cpu(struct padata_instance *pinst, int cpu)
{
	int err;

	mutex_lock(&pinst->lock);

	get_online_cpus();
	cpumask_clear_cpu(cpu, pinst->cpumask);
	err = __padata_remove_cpu(pinst, cpu);
	put_online_cpus();

	mutex_unlock(&pinst->lock);

	return err;
}
EXPORT_SYMBOL(padata_remove_cpu);

/**
 * padata_start - start the parallel processing
 *
 * @pinst: padata instance to start
 */
void padata_start(struct padata_instance *pinst)
{
	mutex_lock(&pinst->lock);
	pinst->flags |= PADATA_INIT;
	mutex_unlock(&pinst->lock);
}
EXPORT_SYMBOL(padata_start);

/**
 * padata_stop - stop the parallel processing
 *
 * @pinst: padata instance to stop
 */
void padata_stop(struct padata_instance *pinst)
{
	mutex_lock(&pinst->lock);
	pinst->flags &= ~PADATA_INIT;
	mutex_unlock(&pinst->lock);
}
EXPORT_SYMBOL(padata_stop);

#ifdef CONFIG_HOTPLUG_CPU
static int padata_cpu_callback(struct notifier_block *nfb,
			       unsigned long action, void *hcpu)
{
	int err;
	struct padata_instance *pinst;
	int cpu = (unsigned long)hcpu;

	pinst = container_of(nfb, struct padata_instance, cpu_notifier);

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		if (!cpumask_test_cpu(cpu, pinst->cpumask))
			break;
		mutex_lock(&pinst->lock);
		err = __padata_add_cpu(pinst, cpu);
		mutex_unlock(&pinst->lock);
		if (err)
			return notifier_from_errno(err);
		break;

	case CPU_DOWN_PREPARE:
	case CPU_DOWN_PREPARE_FROZEN:
		if (!cpumask_test_cpu(cpu, pinst->cpumask))
			break;
		mutex_lock(&pinst->lock);
		err = __padata_remove_cpu(pinst, cpu);
		mutex_unlock(&pinst->lock);
		if (err)
			return notifier_from_errno(err);
		break;

	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		if (!cpumask_test_cpu(cpu, pinst->cpumask))
			break;
		mutex_lock(&pinst->lock);
		__padata_remove_cpu(pinst, cpu);
		mutex_unlock(&pinst->lock);

	case CPU_DOWN_FAILED:
	case CPU_DOWN_FAILED_FROZEN:
		if (!cpumask_test_cpu(cpu, pinst->cpumask))
			break;
		mutex_lock(&pinst->lock);
		__padata_add_cpu(pinst, cpu);
		mutex_unlock(&pinst->lock);
	}

	return NOTIFY_OK;
}
#endif

/**
 * padata_alloc - allocate and initialize a padata instance
 *
 * @cpumask: cpumask that padata uses for parallelization
 * @wq: workqueue to use for the allocated padata instance
 */
struct padata_instance *padata_alloc(const struct cpumask *cpumask,
				     struct workqueue_struct *wq)
{
	struct padata_instance *pinst;
	struct parallel_data *pd;

	pinst = kzalloc(sizeof(struct padata_instance), GFP_KERNEL);
	if (!pinst)
		goto err;

	get_online_cpus();

	pd = padata_alloc_pd(pinst, cpumask);
	if (!pd)
		goto err_free_inst;

	if (!alloc_cpumask_var(&pinst->cpumask, GFP_KERNEL))
		goto err_free_pd;

	rcu_assign_pointer(pinst->pd, pd);

	pinst->wq = wq;

	cpumask_copy(pinst->cpumask, cpumask);

	pinst->flags = 0;

#ifdef CONFIG_HOTPLUG_CPU
	pinst->cpu_notifier.notifier_call = padata_cpu_callback;
	pinst->cpu_notifier.priority = 0;
	register_hotcpu_notifier(&pinst->cpu_notifier);
#endif

	put_online_cpus();

	mutex_init(&pinst->lock);

	return pinst;

err_free_pd:
	padata_free_pd(pd);
err_free_inst:
	kfree(pinst);
	put_online_cpus();
err:
	return NULL;
}
EXPORT_SYMBOL(padata_alloc);

/**
 * padata_free - free a padata instance
 *
 * @padata_inst: padata instance to free
 */
void padata_free(struct padata_instance *pinst)
{
	padata_stop(pinst);

	synchronize_rcu();

#ifdef CONFIG_HOTPLUG_CPU
	unregister_hotcpu_notifier(&pinst->cpu_notifier);
#endif
	get_online_cpus();
	padata_flush_queues(pinst->pd);
	put_online_cpus();

	padata_free_pd(pinst->pd);
	free_cpumask_var(pinst->cpumask);
	kfree(pinst);
}
EXPORT_SYMBOL(padata_free);
