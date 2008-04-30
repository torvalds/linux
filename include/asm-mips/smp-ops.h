/*
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * Copyright (C) 2000 - 2001 by Kanoj Sarcar (kanoj@sgi.com)
 * Copyright (C) 2000 - 2001 by Silicon Graphics, Inc.
 * Copyright (C) 2000, 2001, 2002 Ralf Baechle
 * Copyright (C) 2000, 2001 Broadcom Corporation
 */
#ifndef __ASM_SMP_OPS_H
#define __ASM_SMP_OPS_H

#ifdef CONFIG_SMP

#include <linux/cpumask.h>

struct plat_smp_ops {
	void (*send_ipi_single)(int cpu, unsigned int action);
	void (*send_ipi_mask)(cpumask_t mask, unsigned int action);
	void (*init_secondary)(void);
	void (*smp_finish)(void);
	void (*cpus_done)(void);
	void (*boot_secondary)(int cpu, struct task_struct *idle);
	void (*smp_setup)(void);
	void (*prepare_cpus)(unsigned int max_cpus);
};

extern void register_smp_ops(struct plat_smp_ops *ops);

static inline void plat_smp_setup(void)
{
	extern struct plat_smp_ops *mp_ops;	/* private */

	mp_ops->smp_setup();
}

#else /* !CONFIG_SMP */

struct plat_smp_ops;

static inline void plat_smp_setup(void)
{
	/* UP, nothing to do ...  */
}

static inline void register_smp_ops(struct plat_smp_ops *ops)
{
}

#endif /* !CONFIG_SMP */

extern struct plat_smp_ops up_smp_ops;
extern struct plat_smp_ops cmp_smp_ops;
extern struct plat_smp_ops vsmp_smp_ops;

#endif /* __ASM_SMP_OPS_H */
