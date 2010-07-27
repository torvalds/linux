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
	int			seq_nr;
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
 * struct padata_queue - The percpu padata queues.
 *
 * @parallel: List to wait for parallelization.
 * @reorder: List to wait for reordering after parallel processing.
 * @serial: List to wait for serialization after reordering.
 * @pwork: work struct for parallelization.
 * @swork: work struct for serialization.
 * @pd: Backpointer to the internal control structure.
 * @num_obj: Number of objects that are processed by this cpu.
 * @cpu_index: Index of the cpu.
 */
struct padata_queue {
	struct padata_list	parallel;
	struct padata_list	reorder;
	struct padata_list	serial;
	struct work_struct	pwork;
	struct work_struct	swork;
	struct parallel_data    *pd;
	atomic_t		num_obj;
	int			cpu_index;
};

/**
 * struct parallel_data - Internal control structure, covers everything
 * that depends on the cpumask in use.
 *
 * @pinst: padata instance.
 * @queue: percpu padata queues.
 * @seq_nr: The sequence number that will be attached to the next object.
 * @reorder_objects: Number of objects waiting in the reorder queues.
 * @refcnt: Number of objects holding a reference on this parallel_data.
 * @max_seq_nr:  Maximal used sequence number.
 * @cpumask: cpumask in use.
 * @lock: Reorder lock.
 * @timer: Reorder timer.
 */
struct parallel_data {
	struct padata_instance	*pinst;
	struct padata_queue	*queue;
	atomic_t		seq_nr;
	atomic_t		reorder_objects;
	atomic_t                refcnt;
	unsigned int		max_seq_nr;
	cpumask_var_t		cpumask;
	spinlock_t              lock;
	struct timer_list       timer;
};

/**
 * struct padata_instance - The overall control structure.
 *
 * @cpu_notifier: cpu hotplug notifier.
 * @wq: The workqueue in use.
 * @pd: The internal control structure.
 * @cpumask: User supplied cpumask.
 * @lock: padata instance lock.
 * @flags: padata flags.
 */
struct padata_instance {
	struct notifier_block   cpu_notifier;
	struct workqueue_struct *wq;
	struct parallel_data	*pd;
	cpumask_var_t           cpumask;
	struct mutex		lock;
	u8			flags;
#define	PADATA_INIT		1
#define	PADATA_RESET		2
};

extern struct padata_instance *padata_alloc(const struct cpumask *cpumask,
					    struct workqueue_struct *wq);
extern void padata_free(struct padata_instance *pinst);
extern int padata_do_parallel(struct padata_instance *pinst,
			      struct padata_priv *padata, int cb_cpu);
extern void padata_do_serial(struct padata_priv *padata);
extern int padata_set_cpumask(struct padata_instance *pinst,
			      cpumask_var_t cpumask);
extern int padata_add_cpu(struct padata_instance *pinst, int cpu);
extern int padata_remove_cpu(struct padata_instance *pinst, int cpu);
extern void padata_start(struct padata_instance *pinst);
extern void padata_stop(struct padata_instance *pinst);
#endif
