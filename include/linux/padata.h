/*
 * padata.h - header for the padata parallelization interface
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

#ifndef PADATA_H
#define PADATA_H

#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/kobject.h>

#define PADATA_CPU_SERIAL   0x01
#define PADATA_CPU_PARALLEL 0x02

/**
 * struct padata_priv -  Embedded to the users data structure.
 *
 * @list: List entry, to attach to the padata lists.
 * @pd: Pointer to the internal control structure.
 * @cb_cpu: Callback cpu for serializatioon.
 * @seq_nr: Sequence number of the parallelized data object.
 * @info: Used to pass information from the parallel to the serial function.
 * @parallel: Parallel execution function.
 * @serial: Serial complete function.
 */
struct padata_priv {
	struct list_head	list;
	struct parallel_data	*pd;
	int			cb_cpu;
	int			info;
	void                    (*parallel)(struct padata_priv *padata);
	void                    (*serial)(struct padata_priv *padata);
};

/**
 * struct padata_list
 *
 * @list: List head.
 * @lock: List lock.
 */
struct padata_list {
	struct list_head        list;
	spinlock_t              lock;
};

/**
* struct padata_serial_queue - The percpu padata serial queue
*
* @serial: List to wait for serialization after reordering.
* @work: work struct for serialization.
* @pd: Backpointer to the internal control structure.
*/
struct padata_serial_queue {
       struct padata_list    serial;
       struct work_struct    work;
       struct parallel_data *pd;
};

/**
 * struct padata_parallel_queue - The percpu padata parallel queue
 *
 * @parallel: List to wait for parallelization.
 * @reorder: List to wait for reordering after parallel processing.
 * @serial: List to wait for serialization after reordering.
 * @pwork: work struct for parallelization.
 * @swork: work struct for serialization.
 * @pd: Backpointer to the internal control structure.
 * @work: work struct for parallelization.
 * @num_obj: Number of objects that are processed by this cpu.
 * @cpu_index: Index of the cpu.
 */
struct padata_parallel_queue {
       struct padata_list    parallel;
       struct padata_list    reorder;
       struct parallel_data *pd;
       struct work_struct    work;
       atomic_t              num_obj;
       int                   cpu_index;
};

/**
 * struct padata_cpumask - The cpumasks for the parallel/serial workers
 *
 * @pcpu: cpumask for the parallel workers.
 * @cbcpu: cpumask for the serial (callback) workers.
 */
struct padata_cpumask {
	cpumask_var_t	pcpu;
	cpumask_var_t	cbcpu;
};

/**
 * struct parallel_data - Internal control structure, covers everything
 * that depends on the cpumask in use.
 *
 * @pinst: padata instance.
 * @pqueue: percpu padata queues used for parallelization.
 * @squeue: percpu padata queues used for serialuzation.
 * @reorder_objects: Number of objects waiting in the reorder queues.
 * @refcnt: Number of objects holding a reference on this parallel_data.
 * @max_seq_nr:  Maximal used sequence number.
 * @cpumask: The cpumasks in use for parallel and serial workers.
 * @lock: Reorder lock.
 * @processed: Number of already processed objects.
 * @timer: Reorder timer.
 */
struct parallel_data {
	struct padata_instance		*pinst;
	struct padata_parallel_queue	__percpu *pqueue;
	struct padata_serial_queue	__percpu *squeue;
	atomic_t			reorder_objects;
	atomic_t			refcnt;
	atomic_t			seq_nr;
	struct padata_cpumask		cpumask;
	spinlock_t                      lock ____cacheline_aligned;
	unsigned int			processed;
	struct timer_list		timer;
};

/**
 * struct padata_instance - The overall control structure.
 *
 * @cpu_notifier: cpu hotplug notifier.
 * @wq: The workqueue in use.
 * @pd: The internal control structure.
 * @cpumask: User supplied cpumasks for parallel and serial works.
 * @cpumask_change_notifier: Notifiers chain for user-defined notify
 *            callbacks that will be called when either @pcpu or @cbcpu
 *            or both cpumasks change.
 * @kobj: padata instance kernel object.
 * @lock: padata instance lock.
 * @flags: padata flags.
 */
struct padata_instance {
	struct hlist_node		 node;
	struct workqueue_struct		*wq;
	struct parallel_data		*pd;
	struct padata_cpumask		cpumask;
	struct blocking_notifier_head	 cpumask_change_notifier;
	struct kobject                   kobj;
	struct mutex			 lock;
	u8				 flags;
#define	PADATA_INIT	1
#define	PADATA_RESET	2
#define	PADATA_INVALID	4
};

extern struct padata_instance *padata_alloc_possible(
					struct workqueue_struct *wq);
extern void padata_free(struct padata_instance *pinst);
extern int padata_do_parallel(struct padata_instance *pinst,
			      struct padata_priv *padata, int cb_cpu);
extern void padata_do_serial(struct padata_priv *padata);
extern int padata_set_cpumask(struct padata_instance *pinst, int cpumask_type,
			      cpumask_var_t cpumask);
extern int padata_start(struct padata_instance *pinst);
extern void padata_stop(struct padata_instance *pinst);
extern int padata_register_cpumask_notifier(struct padata_instance *pinst,
					    struct notifier_block *nblock);
extern int padata_unregister_cpumask_notifier(struct padata_instance *pinst,
					      struct notifier_block *nblock);
#endif
