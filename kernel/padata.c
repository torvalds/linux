// SPDX-License-Identifier: GPL-2.0
/*
 * padata.c - generic interface to process data streams in parallel
 *
 * See Documentation/core-api/padata.rst for more information.
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

#include <linux/export.h>
#include <linux/cpumask.h>
#include <linux/err.h>
#include <linux/cpu.h>
#include <linux/padata.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/rcupdate.h>
#include <linux/module.h>

#define MAX_OBJ_NUM 1000

static void padata_free_pd(struct parallel_data *pd);

static int padata_index_to_cpu(struct parallel_data *pd, int cpu_index)
{
	int cpu, target_cpu;

	target_cpu = cpumask_first(pd->cpumask.pcpu);
	for (cpu = 0; cpu < cpu_index; cpu++)
		target_cpu = cpumask_next(target_cpu, pd->cpumask.pcpu);

	return target_cpu;
}

static int padata_cpu_hash(struct parallel_data *pd, unsigned int seq_nr)
{
	/*
	 * Hash the sequence numbers to the cpus by taking
	 * seq_nr mod. number of cpus in use.
	 */
	int cpu_index = seq_nr % cpumask_weight(pd->cpumask.pcpu);

	return padata_index_to_cpu(pd, cpu_index);
}

static void padata_parallel_worker(struct work_struct *parallel_work)
{
	struct padata_parallel_queue *pqueue;
	LIST_HEAD(local_list);

	local_bh_disable();
	pqueue = container_of(parallel_work,
			      struct padata_parallel_queue, work);

	spin_lock(&pqueue->parallel.lock);
	list_replace_init(&pqueue->parallel.list, &local_list);
	spin_unlock(&pqueue->parallel.lock);

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
 * @ps: padatashell
 * @padata: object to be parallelized
 * @cb_cpu: pointer to the CPU that the serialization callback function should
 *          run on.  If it's not in the serial cpumask of @pinst
 *          (i.e. cpumask.cbcpu), this function selects a fallback CPU and if
 *          none found, returns -EINVAL.
 *
 * The parallelization callback function will run with BHs off.
 * Note: Every object which is parallelized by padata_do_parallel
 * must be seen by padata_do_serial.
 *
 * Return: 0 on success or else negative error code.
 */
int padata_do_parallel(struct padata_shell *ps,
		       struct padata_priv *padata, int *cb_cpu)
{
	struct padata_instance *pinst = ps->pinst;
	int i, cpu, cpu_index, target_cpu, err;
	struct padata_parallel_queue *queue;
	struct parallel_data *pd;

	rcu_read_lock_bh();

	pd = rcu_dereference_bh(ps->pd);

	err = -EINVAL;
	if (!(pinst->flags & PADATA_INIT) || pinst->flags & PADATA_INVALID)
		goto out;

	if (!cpumask_test_cpu(*cb_cpu, pd->cpumask.cbcpu)) {
		if (!cpumask_weight(pd->cpumask.cbcpu))
			goto out;

		/* Select an alternate fallback CPU and notify the caller. */
		cpu_index = *cb_cpu % cpumask_weight(pd->cpumask.cbcpu);

		cpu = cpumask_first(pd->cpumask.cbcpu);
		for (i = 0; i < cpu_index; i++)
			cpu = cpumask_next(cpu, pd->cpumask.cbcpu);

		*cb_cpu = cpu;
	}

	err =  -EBUSY;
	if ((pinst->flags & PADATA_RESET))
		goto out;

	if (atomic_read(&pd->refcnt) >= MAX_OBJ_NUM)
		goto out;

	err = 0;
	atomic_inc(&pd->refcnt);
	padata->pd = pd;
	padata->cb_cpu = *cb_cpu;

	padata->seq_nr = atomic_inc_return(&pd->seq_nr);
	target_cpu = padata_cpu_hash(pd, padata->seq_nr);
	padata->cpu = target_cpu;
	queue = per_cpu_ptr(pd->pqueue, target_cpu);

	spin_lock(&queue->parallel.lock);
	list_add_tail(&padata->list, &queue->parallel.list);
	spin_unlock(&queue->parallel.lock);

	queue_work(pinst->parallel_wq, &queue->work);

out:
	rcu_read_unlock_bh();

	return err;
}
EXPORT_SYMBOL(padata_do_parallel);

/*
 * padata_find_next - Find the next object that needs serialization.
 *
 * Return:
 * * A pointer to the control struct of the next object that needs
 *   serialization, if present in one of the percpu reorder queues.
 * * NULL, if the next object that needs serialization will
 *   be parallel processed by another cpu and is not yet present in
 *   the cpu's reorder queue.
 */
static struct padata_priv *padata_find_next(struct parallel_data *pd,
					    bool remove_object)
{
	struct padata_parallel_queue *next_queue;
	struct padata_priv *padata;
	struct padata_list *reorder;
	int cpu = pd->cpu;

	next_queue = per_cpu_ptr(pd->pqueue, cpu);
	reorder = &next_queue->reorder;

	spin_lock(&reorder->lock);
	if (list_empty(&reorder->list)) {
		spin_unlock(&reorder->lock);
		return NULL;
	}

	padata = list_entry(reorder->list.next, struct padata_priv, list);

	/*
	 * Checks the rare case where two or more parallel jobs have hashed to
	 * the same CPU and one of the later ones finishes first.
	 */
	if (padata->seq_nr != pd->processed) {
		spin_unlock(&reorder->lock);
		return NULL;
	}

	if (remove_object) {
		list_del_init(&padata->list);
		++pd->processed;
		pd->cpu = cpumask_next_wrap(cpu, pd->cpumask.pcpu, -1, false);
	}

	spin_unlock(&reorder->lock);
	return padata;
}

static void padata_reorder(struct parallel_data *pd)
{
	struct padata_instance *pinst = pd->ps->pinst;
	int cb_cpu;
	struct padata_priv *padata;
	struct padata_serial_queue *squeue;
	struct padata_parallel_queue *next_queue;

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
		padata = padata_find_next(pd, true);

		/*
		 * If the next object that needs serialization is parallel
		 * processed by another cpu and is still on it's way to the
		 * cpu's reorder queue, nothing to do for now.
		 */
		if (!padata)
			break;

		cb_cpu = padata->cb_cpu;
		squeue = per_cpu_ptr(pd->squeue, cb_cpu);

		spin_lock(&squeue->serial.lock);
		list_add_tail(&padata->list, &squeue->serial.list);
		spin_unlock(&squeue->serial.lock);

		queue_work_on(cb_cpu, pinst->serial_wq, &squeue->work);
	}

	spin_unlock_bh(&pd->lock);

	/*
	 * The next object that needs serialization might have arrived to
	 * the reorder queues in the meantime.
	 *
	 * Ensure reorder queue is read after pd->lock is dropped so we see
	 * new objects from another task in padata_do_serial.  Pairs with
	 * smp_mb__after_atomic in padata_do_serial.
	 */
	smp_mb();

	next_queue = per_cpu_ptr(pd->pqueue, pd->cpu);
	if (!list_empty(&next_queue->reorder.list) &&
	    padata_find_next(pd, false))
		queue_work(pinst->serial_wq, &pd->reorder_work);
}

static void invoke_padata_reorder(struct work_struct *work)
{
	struct parallel_data *pd;

	local_bh_disable();
	pd = container_of(work, struct parallel_data, reorder_work);
	padata_reorder(pd);
	local_bh_enable();
}

static void padata_serial_worker(struct work_struct *serial_work)
{
	struct padata_serial_queue *squeue;
	struct parallel_data *pd;
	LIST_HEAD(local_list);
	int cnt;

	local_bh_disable();
	squeue = container_of(serial_work, struct padata_serial_queue, work);
	pd = squeue->pd;

	spin_lock(&squeue->serial.lock);
	list_replace_init(&squeue->serial.list, &local_list);
	spin_unlock(&squeue->serial.lock);

	cnt = 0;

	while (!list_empty(&local_list)) {
		struct padata_priv *padata;

		padata = list_entry(local_list.next,
				    struct padata_priv, list);

		list_del_init(&padata->list);

		padata->serial(padata);
		cnt++;
	}
	local_bh_enable();

	if (atomic_sub_and_test(cnt, &pd->refcnt))
		padata_free_pd(pd);
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
	struct parallel_data *pd = padata->pd;
	struct padata_parallel_queue *pqueue = per_cpu_ptr(pd->pqueue,
							   padata->cpu);
	struct padata_priv *cur;

	spin_lock(&pqueue->reorder.lock);
	/* Sort in ascending order of sequence number. */
	list_for_each_entry_reverse(cur, &pqueue->reorder.list, list)
		if (cur->seq_nr < padata->seq_nr)
			break;
	list_add(&padata->list, &cur->list);
	spin_unlock(&pqueue->reorder.lock);

	/*
	 * Ensure the addition to the reorder list is ordered correctly
	 * with the trylock of pd->lock in padata_reorder.  Pairs with smp_mb
	 * in padata_reorder.
	 */
	smp_mb__after_atomic();

	padata_reorder(pd);
}
EXPORT_SYMBOL(padata_do_serial);

static int padata_setup_cpumasks(struct padata_instance *pinst)
{
	struct workqueue_attrs *attrs;
	int err;

	attrs = alloc_workqueue_attrs();
	if (!attrs)
		return -ENOMEM;

	/* Restrict parallel_wq workers to pd->cpumask.pcpu. */
	cpumask_copy(attrs->cpumask, pinst->cpumask.pcpu);
	err = apply_workqueue_attrs(pinst->parallel_wq, attrs);
	free_workqueue_attrs(attrs);

	return err;
}

static int pd_setup_cpumasks(struct parallel_data *pd,
			     const struct cpumask *pcpumask,
			     const struct cpumask *cbcpumask)
{
	int err = -ENOMEM;

	if (!alloc_cpumask_var(&pd->cpumask.pcpu, GFP_KERNEL))
		goto out;
	if (!alloc_cpumask_var(&pd->cpumask.cbcpu, GFP_KERNEL))
		goto free_pcpu_mask;

	cpumask_copy(pd->cpumask.pcpu, pcpumask);
	cpumask_copy(pd->cpumask.cbcpu, cbcpumask);

	return 0;

free_pcpu_mask:
	free_cpumask_var(pd->cpumask.pcpu);
out:
	return err;
}

static void __padata_list_init(struct padata_list *pd_list)
{
	INIT_LIST_HEAD(&pd_list->list);
	spin_lock_init(&pd_list->lock);
}

/* Initialize all percpu queues used by serial workers */
static void padata_init_squeues(struct parallel_data *pd)
{
	int cpu;
	struct padata_serial_queue *squeue;

	for_each_cpu(cpu, pd->cpumask.cbcpu) {
		squeue = per_cpu_ptr(pd->squeue, cpu);
		squeue->pd = pd;
		__padata_list_init(&squeue->serial);
		INIT_WORK(&squeue->work, padata_serial_worker);
	}
}

/* Initialize all percpu queues used by parallel workers */
static void padata_init_pqueues(struct parallel_data *pd)
{
	int cpu;
	struct padata_parallel_queue *pqueue;

	for_each_cpu(cpu, pd->cpumask.pcpu) {
		pqueue = per_cpu_ptr(pd->pqueue, cpu);

		__padata_list_init(&pqueue->reorder);
		__padata_list_init(&pqueue->parallel);
		INIT_WORK(&pqueue->work, padata_parallel_worker);
		atomic_set(&pqueue->num_obj, 0);
	}
}

/* Allocate and initialize the internal cpumask dependend resources. */
static struct parallel_data *padata_alloc_pd(struct padata_shell *ps)
{
	struct padata_instance *pinst = ps->pinst;
	const struct cpumask *cbcpumask;
	const struct cpumask *pcpumask;
	struct parallel_data *pd;

	cbcpumask = pinst->rcpumask.cbcpu;
	pcpumask = pinst->rcpumask.pcpu;

	pd = kzalloc(sizeof(struct parallel_data), GFP_KERNEL);
	if (!pd)
		goto err;

	pd->pqueue = alloc_percpu(struct padata_parallel_queue);
	if (!pd->pqueue)
		goto err_free_pd;

	pd->squeue = alloc_percpu(struct padata_serial_queue);
	if (!pd->squeue)
		goto err_free_pqueue;

	pd->ps = ps;
	if (pd_setup_cpumasks(pd, pcpumask, cbcpumask))
		goto err_free_squeue;

	padata_init_pqueues(pd);
	padata_init_squeues(pd);
	atomic_set(&pd->seq_nr, -1);
	atomic_set(&pd->refcnt, 1);
	spin_lock_init(&pd->lock);
	pd->cpu = cpumask_first(pd->cpumask.pcpu);
	INIT_WORK(&pd->reorder_work, invoke_padata_reorder);

	return pd;

err_free_squeue:
	free_percpu(pd->squeue);
err_free_pqueue:
	free_percpu(pd->pqueue);
err_free_pd:
	kfree(pd);
err:
	return NULL;
}

static void padata_free_pd(struct parallel_data *pd)
{
	free_cpumask_var(pd->cpumask.pcpu);
	free_cpumask_var(pd->cpumask.cbcpu);
	free_percpu(pd->pqueue);
	free_percpu(pd->squeue);
	kfree(pd);
}

static void __padata_start(struct padata_instance *pinst)
{
	pinst->flags |= PADATA_INIT;
}

static void __padata_stop(struct padata_instance *pinst)
{
	if (!(pinst->flags & PADATA_INIT))
		return;

	pinst->flags &= ~PADATA_INIT;

	synchronize_rcu();
}

/* Replace the internal control structure with a new one. */
static int padata_replace_one(struct padata_shell *ps)
{
	struct parallel_data *pd_new;

	pd_new = padata_alloc_pd(ps);
	if (!pd_new)
		return -ENOMEM;

	ps->opd = rcu_dereference_protected(ps->pd, 1);
	rcu_assign_pointer(ps->pd, pd_new);

	return 0;
}

static int padata_replace(struct padata_instance *pinst)
{
	struct padata_shell *ps;
	int err = 0;

	pinst->flags |= PADATA_RESET;

	cpumask_and(pinst->rcpumask.pcpu, pinst->cpumask.pcpu,
		    cpu_online_mask);

	cpumask_and(pinst->rcpumask.cbcpu, pinst->cpumask.cbcpu,
		    cpu_online_mask);

	list_for_each_entry(ps, &pinst->pslist, list) {
		err = padata_replace_one(ps);
		if (err)
			break;
	}

	synchronize_rcu();

	list_for_each_entry_continue_reverse(ps, &pinst->pslist, list)
		if (atomic_dec_and_test(&ps->opd->refcnt))
			padata_free_pd(ps->opd);

	pinst->flags &= ~PADATA_RESET;

	return err;
}

/* If cpumask contains no active cpu, we mark the instance as invalid. */
static bool padata_validate_cpumask(struct padata_instance *pinst,
				    const struct cpumask *cpumask)
{
	if (!cpumask_intersects(cpumask, cpu_online_mask)) {
		pinst->flags |= PADATA_INVALID;
		return false;
	}

	pinst->flags &= ~PADATA_INVALID;
	return true;
}

static int __padata_set_cpumasks(struct padata_instance *pinst,
				 cpumask_var_t pcpumask,
				 cpumask_var_t cbcpumask)
{
	int valid;
	int err;

	valid = padata_validate_cpumask(pinst, pcpumask);
	if (!valid) {
		__padata_stop(pinst);
		goto out_replace;
	}

	valid = padata_validate_cpumask(pinst, cbcpumask);
	if (!valid)
		__padata_stop(pinst);

out_replace:
	cpumask_copy(pinst->cpumask.pcpu, pcpumask);
	cpumask_copy(pinst->cpumask.cbcpu, cbcpumask);

	err = padata_setup_cpumasks(pinst) ?: padata_replace(pinst);

	if (valid)
		__padata_start(pinst);

	return err;
}

/**
 * padata_set_cpumask - Sets specified by @cpumask_type cpumask to the value
 *                      equivalent to @cpumask.
 * @pinst: padata instance
 * @cpumask_type: PADATA_CPU_SERIAL or PADATA_CPU_PARALLEL corresponding
 *                to parallel and serial cpumasks respectively.
 * @cpumask: the cpumask to use
 *
 * Return: 0 on success or negative error code
 */
int padata_set_cpumask(struct padata_instance *pinst, int cpumask_type,
		       cpumask_var_t cpumask)
{
	struct cpumask *serial_mask, *parallel_mask;
	int err = -EINVAL;

	get_online_cpus();
	mutex_lock(&pinst->lock);

	switch (cpumask_type) {
	case PADATA_CPU_PARALLEL:
		serial_mask = pinst->cpumask.cbcpu;
		parallel_mask = cpumask;
		break;
	case PADATA_CPU_SERIAL:
		parallel_mask = pinst->cpumask.pcpu;
		serial_mask = cpumask;
		break;
	default:
		 goto out;
	}

	err =  __padata_set_cpumasks(pinst, parallel_mask, serial_mask);

out:
	mutex_unlock(&pinst->lock);
	put_online_cpus();

	return err;
}
EXPORT_SYMBOL(padata_set_cpumask);

/**
 * padata_start - start the parallel processing
 *
 * @pinst: padata instance to start
 *
 * Return: 0 on success or negative error code
 */
int padata_start(struct padata_instance *pinst)
{
	int err = 0;

	mutex_lock(&pinst->lock);

	if (pinst->flags & PADATA_INVALID)
		err = -EINVAL;

	__padata_start(pinst);

	mutex_unlock(&pinst->lock);

	return err;
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
	__padata_stop(pinst);
	mutex_unlock(&pinst->lock);
}
EXPORT_SYMBOL(padata_stop);

#ifdef CONFIG_HOTPLUG_CPU

static int __padata_add_cpu(struct padata_instance *pinst, int cpu)
{
	int err = 0;

	if (cpumask_test_cpu(cpu, cpu_online_mask)) {
		err = padata_replace(pinst);

		if (padata_validate_cpumask(pinst, pinst->cpumask.pcpu) &&
		    padata_validate_cpumask(pinst, pinst->cpumask.cbcpu))
			__padata_start(pinst);
	}

	return err;
}

static int __padata_remove_cpu(struct padata_instance *pinst, int cpu)
{
	int err = 0;

	if (!cpumask_test_cpu(cpu, cpu_online_mask)) {
		if (!padata_validate_cpumask(pinst, pinst->cpumask.pcpu) ||
		    !padata_validate_cpumask(pinst, pinst->cpumask.cbcpu))
			__padata_stop(pinst);

		err = padata_replace(pinst);
	}

	return err;
}

static inline int pinst_has_cpu(struct padata_instance *pinst, int cpu)
{
	return cpumask_test_cpu(cpu, pinst->cpumask.pcpu) ||
		cpumask_test_cpu(cpu, pinst->cpumask.cbcpu);
}

static int padata_cpu_online(unsigned int cpu, struct hlist_node *node)
{
	struct padata_instance *pinst;
	int ret;

	pinst = hlist_entry_safe(node, struct padata_instance, node);
	if (!pinst_has_cpu(pinst, cpu))
		return 0;

	mutex_lock(&pinst->lock);
	ret = __padata_add_cpu(pinst, cpu);
	mutex_unlock(&pinst->lock);
	return ret;
}

static int padata_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct padata_instance *pinst;
	int ret;

	pinst = hlist_entry_safe(node, struct padata_instance, node);
	if (!pinst_has_cpu(pinst, cpu))
		return 0;

	mutex_lock(&pinst->lock);
	ret = __padata_remove_cpu(pinst, cpu);
	mutex_unlock(&pinst->lock);
	return ret;
}

static enum cpuhp_state hp_online;
#endif

static void __padata_free(struct padata_instance *pinst)
{
#ifdef CONFIG_HOTPLUG_CPU
	cpuhp_state_remove_instance_nocalls(CPUHP_PADATA_DEAD, &pinst->node);
	cpuhp_state_remove_instance_nocalls(hp_online, &pinst->node);
#endif

	WARN_ON(!list_empty(&pinst->pslist));

	padata_stop(pinst);
	free_cpumask_var(pinst->rcpumask.cbcpu);
	free_cpumask_var(pinst->rcpumask.pcpu);
	free_cpumask_var(pinst->cpumask.pcpu);
	free_cpumask_var(pinst->cpumask.cbcpu);
	destroy_workqueue(pinst->serial_wq);
	destroy_workqueue(pinst->parallel_wq);
	kfree(pinst);
}

#define kobj2pinst(_kobj)					\
	container_of(_kobj, struct padata_instance, kobj)
#define attr2pentry(_attr)					\
	container_of(_attr, struct padata_sysfs_entry, attr)

static void padata_sysfs_release(struct kobject *kobj)
{
	struct padata_instance *pinst = kobj2pinst(kobj);
	__padata_free(pinst);
}

struct padata_sysfs_entry {
	struct attribute attr;
	ssize_t (*show)(struct padata_instance *, struct attribute *, char *);
	ssize_t (*store)(struct padata_instance *, struct attribute *,
			 const char *, size_t);
};

static ssize_t show_cpumask(struct padata_instance *pinst,
			    struct attribute *attr,  char *buf)
{
	struct cpumask *cpumask;
	ssize_t len;

	mutex_lock(&pinst->lock);
	if (!strcmp(attr->name, "serial_cpumask"))
		cpumask = pinst->cpumask.cbcpu;
	else
		cpumask = pinst->cpumask.pcpu;

	len = snprintf(buf, PAGE_SIZE, "%*pb\n",
		       nr_cpu_ids, cpumask_bits(cpumask));
	mutex_unlock(&pinst->lock);
	return len < PAGE_SIZE ? len : -EINVAL;
}

static ssize_t store_cpumask(struct padata_instance *pinst,
			     struct attribute *attr,
			     const char *buf, size_t count)
{
	cpumask_var_t new_cpumask;
	ssize_t ret;
	int mask_type;

	if (!alloc_cpumask_var(&new_cpumask, GFP_KERNEL))
		return -ENOMEM;

	ret = bitmap_parse(buf, count, cpumask_bits(new_cpumask),
			   nr_cpumask_bits);
	if (ret < 0)
		goto out;

	mask_type = !strcmp(attr->name, "serial_cpumask") ?
		PADATA_CPU_SERIAL : PADATA_CPU_PARALLEL;
	ret = padata_set_cpumask(pinst, mask_type, new_cpumask);
	if (!ret)
		ret = count;

out:
	free_cpumask_var(new_cpumask);
	return ret;
}

#define PADATA_ATTR_RW(_name, _show_name, _store_name)		\
	static struct padata_sysfs_entry _name##_attr =		\
		__ATTR(_name, 0644, _show_name, _store_name)
#define PADATA_ATTR_RO(_name, _show_name)		\
	static struct padata_sysfs_entry _name##_attr = \
		__ATTR(_name, 0400, _show_name, NULL)

PADATA_ATTR_RW(serial_cpumask, show_cpumask, store_cpumask);
PADATA_ATTR_RW(parallel_cpumask, show_cpumask, store_cpumask);

/*
 * Padata sysfs provides the following objects:
 * serial_cpumask   [RW] - cpumask for serial workers
 * parallel_cpumask [RW] - cpumask for parallel workers
 */
static struct attribute *padata_default_attrs[] = {
	&serial_cpumask_attr.attr,
	&parallel_cpumask_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(padata_default);

static ssize_t padata_sysfs_show(struct kobject *kobj,
				 struct attribute *attr, char *buf)
{
	struct padata_instance *pinst;
	struct padata_sysfs_entry *pentry;
	ssize_t ret = -EIO;

	pinst = kobj2pinst(kobj);
	pentry = attr2pentry(attr);
	if (pentry->show)
		ret = pentry->show(pinst, attr, buf);

	return ret;
}

static ssize_t padata_sysfs_store(struct kobject *kobj, struct attribute *attr,
				  const char *buf, size_t count)
{
	struct padata_instance *pinst;
	struct padata_sysfs_entry *pentry;
	ssize_t ret = -EIO;

	pinst = kobj2pinst(kobj);
	pentry = attr2pentry(attr);
	if (pentry->show)
		ret = pentry->store(pinst, attr, buf, count);

	return ret;
}

static const struct sysfs_ops padata_sysfs_ops = {
	.show = padata_sysfs_show,
	.store = padata_sysfs_store,
};

static struct kobj_type padata_attr_type = {
	.sysfs_ops = &padata_sysfs_ops,
	.default_groups = padata_default_groups,
	.release = padata_sysfs_release,
};

/**
 * padata_alloc - allocate and initialize a padata instance and specify
 *                cpumasks for serial and parallel workers.
 *
 * @name: used to identify the instance
 * @pcpumask: cpumask that will be used for padata parallelization
 * @cbcpumask: cpumask that will be used for padata serialization
 *
 * Return: new instance on success, NULL on error
 */
static struct padata_instance *padata_alloc(const char *name,
					    const struct cpumask *pcpumask,
					    const struct cpumask *cbcpumask)
{
	struct padata_instance *pinst;

	pinst = kzalloc(sizeof(struct padata_instance), GFP_KERNEL);
	if (!pinst)
		goto err;

	pinst->parallel_wq = alloc_workqueue("%s_parallel", WQ_UNBOUND, 0,
					     name);
	if (!pinst->parallel_wq)
		goto err_free_inst;

	get_online_cpus();

	pinst->serial_wq = alloc_workqueue("%s_serial", WQ_MEM_RECLAIM |
					   WQ_CPU_INTENSIVE, 1, name);
	if (!pinst->serial_wq)
		goto err_put_cpus;

	if (!alloc_cpumask_var(&pinst->cpumask.pcpu, GFP_KERNEL))
		goto err_free_serial_wq;
	if (!alloc_cpumask_var(&pinst->cpumask.cbcpu, GFP_KERNEL)) {
		free_cpumask_var(pinst->cpumask.pcpu);
		goto err_free_serial_wq;
	}
	if (!padata_validate_cpumask(pinst, pcpumask) ||
	    !padata_validate_cpumask(pinst, cbcpumask))
		goto err_free_masks;

	if (!alloc_cpumask_var(&pinst->rcpumask.pcpu, GFP_KERNEL))
		goto err_free_masks;
	if (!alloc_cpumask_var(&pinst->rcpumask.cbcpu, GFP_KERNEL))
		goto err_free_rcpumask_pcpu;

	INIT_LIST_HEAD(&pinst->pslist);

	cpumask_copy(pinst->cpumask.pcpu, pcpumask);
	cpumask_copy(pinst->cpumask.cbcpu, cbcpumask);
	cpumask_and(pinst->rcpumask.pcpu, pcpumask, cpu_online_mask);
	cpumask_and(pinst->rcpumask.cbcpu, cbcpumask, cpu_online_mask);

	if (padata_setup_cpumasks(pinst))
		goto err_free_rcpumask_cbcpu;

	pinst->flags = 0;

	kobject_init(&pinst->kobj, &padata_attr_type);
	mutex_init(&pinst->lock);

#ifdef CONFIG_HOTPLUG_CPU
	cpuhp_state_add_instance_nocalls_cpuslocked(hp_online, &pinst->node);
	cpuhp_state_add_instance_nocalls_cpuslocked(CPUHP_PADATA_DEAD,
						    &pinst->node);
#endif

	put_online_cpus();

	return pinst;

err_free_rcpumask_cbcpu:
	free_cpumask_var(pinst->rcpumask.cbcpu);
err_free_rcpumask_pcpu:
	free_cpumask_var(pinst->rcpumask.pcpu);
err_free_masks:
	free_cpumask_var(pinst->cpumask.pcpu);
	free_cpumask_var(pinst->cpumask.cbcpu);
err_free_serial_wq:
	destroy_workqueue(pinst->serial_wq);
err_put_cpus:
	put_online_cpus();
	destroy_workqueue(pinst->parallel_wq);
err_free_inst:
	kfree(pinst);
err:
	return NULL;
}

/**
 * padata_alloc_possible - Allocate and initialize padata instance.
 *                         Use the cpu_possible_mask for serial and
 *                         parallel workers.
 *
 * @name: used to identify the instance
 *
 * Return: new instance on success, NULL on error
 */
struct padata_instance *padata_alloc_possible(const char *name)
{
	return padata_alloc(name, cpu_possible_mask, cpu_possible_mask);
}
EXPORT_SYMBOL(padata_alloc_possible);

/**
 * padata_free - free a padata instance
 *
 * @pinst: padata instance to free
 */
void padata_free(struct padata_instance *pinst)
{
	kobject_put(&pinst->kobj);
}
EXPORT_SYMBOL(padata_free);

/**
 * padata_alloc_shell - Allocate and initialize padata shell.
 *
 * @pinst: Parent padata_instance object.
 *
 * Return: new shell on success, NULL on error
 */
struct padata_shell *padata_alloc_shell(struct padata_instance *pinst)
{
	struct parallel_data *pd;
	struct padata_shell *ps;

	ps = kzalloc(sizeof(*ps), GFP_KERNEL);
	if (!ps)
		goto out;

	ps->pinst = pinst;

	get_online_cpus();
	pd = padata_alloc_pd(ps);
	put_online_cpus();

	if (!pd)
		goto out_free_ps;

	mutex_lock(&pinst->lock);
	RCU_INIT_POINTER(ps->pd, pd);
	list_add(&ps->list, &pinst->pslist);
	mutex_unlock(&pinst->lock);

	return ps;

out_free_ps:
	kfree(ps);
out:
	return NULL;
}
EXPORT_SYMBOL(padata_alloc_shell);

/**
 * padata_free_shell - free a padata shell
 *
 * @ps: padata shell to free
 */
void padata_free_shell(struct padata_shell *ps)
{
	if (!ps)
		return;

	mutex_lock(&ps->pinst->lock);
	list_del(&ps->list);
	padata_free_pd(rcu_dereference_protected(ps->pd, 1));
	mutex_unlock(&ps->pinst->lock);

	kfree(ps);
}
EXPORT_SYMBOL(padata_free_shell);

#ifdef CONFIG_HOTPLUG_CPU

static __init int padata_driver_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "padata:online",
				      padata_cpu_online, NULL);
	if (ret < 0)
		return ret;
	hp_online = ret;

	ret = cpuhp_setup_state_multi(CPUHP_PADATA_DEAD, "padata:dead",
				      NULL, padata_cpu_dead);
	if (ret < 0) {
		cpuhp_remove_multi_state(hp_online);
		return ret;
	}
	return 0;
}
module_init(padata_driver_init);

static __exit void padata_driver_exit(void)
{
	cpuhp_remove_multi_state(CPUHP_PADATA_DEAD);
	cpuhp_remove_multi_state(hp_online);
}
module_exit(padata_driver_exit);
#endif
