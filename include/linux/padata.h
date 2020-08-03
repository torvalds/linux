/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * padata.h - header for the padata parallelization interface
 *
 * Copyright (C) 2008, 2009 secunet Security Networks AG
 * Copyright (C) 2008, 2009 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * Copyright (c) 2020 Oracle and/or its affiliates.
 * Author: Daniel Jordan <daniel.m.jordan@oracle.com>
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
 * @seq_nr: Sequence number of the parallelized data object.
 * @info: Used to pass information from the parallel to the serial function.
 * @parallel: Parallel execution function.
 * @serial: Serial complete function.
 */
struct padata_priv {
	struct list_head	list;
	struct parallel_data	*pd;
	int			cb_cpu;
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
 * @reorder: List to wait for reordering after parallel processing.
 * @num_obj: Number of objects that are processed by this cpu.
 */
struct padata_parallel_queue {
       struct padata_list    reorder;
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
	unsigned int			seq_nr;
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
 * struct padata_mt_job - represents one multithreaded job
 *
 * @thread_fn: Called for each chunk of work that a padata thread does.
 * @fn_arg: The thread function argument.
 * @start: The start of the job (units are job-specific).
 * @size: size of this node's work (units are job-specific).
 * @align: Ranges passed to the thread function fall on this boundary, with the
 *         possible exceptions of the beginning and end of the job.
 * @min_chunk: The minimum chunk size in job-specific units.  This allows
 *             the client to communicate the minimum amount of work that's
 *             appropriate for one worker thread to do at once.
 * @max_threads: Max threads to use for the job, actual number may be less
 *               depending on task size and minimum chunk size.
 */
struct padata_mt_job {
	void (*thread_fn)(unsigned long start, unsigned long end, void *arg);
	void			*fn_arg;
	unsigned long		start;
	unsigned long		size;
	unsigned long		align;
	unsigned long		min_chunk;
	int			max_threads;
};

/**
 * struct padata_instance - The overall control structure.
 *
 * @cpu_online_node: Linkage for CPU online callback.
 * @cpu_dead_node: Linkage for CPU offline callback.
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
	struct hlist_node		cpu_online_node;
	struct hlist_node		cpu_dead_node;
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

#ifdef CONFIG_PADATA
extern void __init padata_init(void);
#else
static inline void __init padata_init(void) {}
#endif

extern struct padata_instance *padata_alloc_possible(const char *name);
extern void padata_free(struct padata_instance *pinst);
extern struct padata_shell *padata_alloc_shell(struct padata_instance *pinst);
extern void padata_free_shell(struct padata_shell *ps);
extern int padata_do_parallel(struct padata_shell *ps,
			      struct padata_priv *padata, int *cb_cpu);
extern void padata_do_serial(struct padata_priv *padata);
extern void __init padata_do_multithreaded(struct padata_mt_job *job);
extern int padata_set_cpumask(struct padata_instance *pinst, int cpumask_type,
			      cpumask_var_t cpumask);
extern int padata_start(struct padata_instance *pinst);
extern void padata_stop(struct padata_instance *pinst);
#endif
