/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * padata.h - header for the padata parallelization interface
 *
 * Copyright (C) 2008, 2009 secunet Security Networks AG
 * Copyright (C) 2008, 2009 Steffen Klassert <steffen.klassert@secunet.com>
 */

#ifndef PADATA_H
#define PADATA_H

#include <linux/compiler_types.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/kobject.h>

#define PADATA_CPU_SERIAL   0x01
#define PADATA_CPU_PARALLEL 0x02

/**
 * struct padata_priv - Represents one job
 *
 * @list: List entry, to attach to the padata lists.
 * @pd: Pointer to the internal control structure.
 * @cb_cpu: Callback cpu for serializatioon.
 * @cpu: Cpu for parallelization.
 * @seq_nr: Sequence number of the parallelized data object.
 * @info: Used to pass information from the parallel to the serial function.
 * @parallel: Parallel execution function.
 * @serial: Serial complete function.
 */
struct padata_priv {
	struct list_head	list;
	struct parallel_data	*pd;
	int			cb_cpu;
	int			cpu;
	unsigned int		seq_nr;
	int			info;
	void                    (*parallel)(struct padata_priv *padata);
	void                    (*serial)(struct padata_priv *padata);
};

/**
 * struct padata_list - one per work type per CPU
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
 * @work: work struct for parallelization.
 * @num_obj: Number of objects that are processed by this cpu.
 */
struct padata_parallel_queue {
       struct padata_list    parallel;
       struct padata_list    reorder;
       struct work_struct    work;
       atomic_t              num_obj;
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
 * @ps: padata_shell object.
 * @pqueue: percpu padata queues used for parallelization.
 * @squeue: percpu padata queues used for serialuzation.
 * @refcnt: Number of objects holding a reference on this parallel_data.
 * @seq_nr: Sequence number of the parallelized data object.
 * @processed: Number of already processed objects.
 * @cpu: Next CPU to be processed.
 * @cpumask: The cpumasks in use for parallel and serial workers.
 * @reorder_work: work struct for reordering.
 * @lock: Reorder lock.
 */
struct parallel_data {
	struct padata_shell		*ps;
	struct padata_parallel_queue	__percpu *pqueue;
	struct padata_serial_queue	__percpu *squeue;
	atomic_t			refcnt;
	atomic_t			seq_nr;
	unsigned int			processed;
	int				cpu;
	struct padata_cpumask		cpumask;
	struct work_struct		reorder_work;
	spinlock_t                      ____cacheline_aligned lock;
};

/**
 * struct padata_shell - Wrapper around struct parallel_data, its
 * purpose is to allow the underlying control structure to be replaced
 * on the fly using RCU.
 *
 * @pinst: padat instance.
 * @pd: Actual parallel_data structure which may be substituted on the fly.
 * @opd: Pointer to old pd to be freed by padata_replace.
 * @list: List entry in padata_instance list.
 */
struct padata_shell {
	struct padata_instance		*pinst;
	struct parallel_data __rcu	*pd;
	struct parallel_data		*opd;
	struct list_head		list;
};

/**
 * struct padata_instance - The overall control structure.
 *
 * @node: Used by CPU hotplug.
 * @parallel_wq: The workqueue used for parallel work.
 * @serial_wq: The workqueue used for serial work.
 * @pslist: List of padata_shell objects attached to this instance.
 * @cpumask: User supplied cpumasks for parallel and serial works.
 * @rcpumask: Actual cpumasks based on user cpumask and cpu_online_mask.
 * @kobj: padata instance kernel object.
 * @lock: padata instance lock.
 * @flags: padata flags.
 */
struct padata_instance {
	struct hlist_node		 node;
	struct workqueue_struct		*parallel_wq;
	struct workqueue_struct		*serial_wq;
	struct list_head		pslist;
	struct padata_cpumask		cpumask;
	struct padata_cpumask		rcpumask;
	struct kobject                   kobj;
	struct mutex			 lock;
	u8				 flags;
#define	PADATA_INIT	1
#define	PADATA_RESET	2
#define	PADATA_INVALID	4
};

extern struct padata_instance *padata_alloc_possible(const char *name);
extern void padata_free(struct padata_instance *pinst);
extern struct padata_shell *padata_alloc_shell(struct padata_instance *pinst);
extern void padata_free_shell(struct padata_shell *ps);
extern int padata_do_parallel(struct padata_shell *ps,
			      struct padata_priv *padata, int *cb_cpu);
extern void padata_do_serial(struct padata_priv *padata);
extern int padata_set_cpumask(struct padata_instance *pinst, int cpumask_type,
			      cpumask_var_t cpumask);
extern int padata_start(struct padata_instance *pinst);
extern void padata_stop(struct padata_instance *pinst);
#endif
