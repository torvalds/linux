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

struct padata_priv {
	struct list_head	list;
	struct parallel_data	*pd;
	int			cb_cpu;
	int			seq_nr;
	int			info;
	void                    (*parallel)(struct padata_priv *padata);
	void                    (*serial)(struct padata_priv *padata);
};

struct padata_list {
	struct list_head        list;
	spinlock_t              lock;
};

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

struct parallel_data {
	struct padata_instance	*pinst;
	struct padata_queue	*queue;
	atomic_t		seq_nr;
	atomic_t		reorder_objects;
	atomic_t                refcnt;
	unsigned int		max_seq_nr;
	cpumask_var_t		cpumask;
	spinlock_t              lock;
};

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
