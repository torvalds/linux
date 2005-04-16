/*
 *  linux/include/linux/cpufreq.h
 *
 *  Copyright (C) 2001 Russell King
 *            (C) 2002 - 2003 Dominik Brodowski <linux@brodo.de>
 *            
 *
 * $Id: cpufreq.h,v 1.36 2003/01/20 17:31:48 db Exp $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _LINUX_CPUFREQ_H
#define _LINUX_CPUFREQ_H

#include <linux/config.h>
#include <linux/notifier.h>
#include <linux/threads.h>
#include <linux/device.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/cpumask.h>

#define CPUFREQ_NAME_LEN 16


/*********************************************************************
 *                     CPUFREQ NOTIFIER INTERFACE                    *
 *********************************************************************/

int cpufreq_register_notifier(struct notifier_block *nb, unsigned int list);
int cpufreq_unregister_notifier(struct notifier_block *nb, unsigned int list);

#define CPUFREQ_TRANSITION_NOTIFIER	(0)
#define CPUFREQ_POLICY_NOTIFIER		(1)


/* if (cpufreq_driver->target) exists, the ->governor decides what frequency
 * within the limits is used. If (cpufreq_driver->setpolicy> exists, these
 * two generic policies are available:
 */

#define CPUFREQ_POLICY_POWERSAVE	(1)
#define CPUFREQ_POLICY_PERFORMANCE	(2)

/* Frequency values here are CPU kHz so that hardware which doesn't run 
 * with some frequencies can complain without having to guess what per 
 * cent / per mille means. 
 * Maximum transition latency is in microseconds - if it's unknown,
 * CPUFREQ_ETERNAL shall be used.
 */

struct cpufreq_governor;

#define CPUFREQ_ETERNAL			(-1)
struct cpufreq_cpuinfo {
	unsigned int		max_freq;
	unsigned int		min_freq;
	unsigned int		transition_latency; /* in 10^(-9) s = nanoseconds */
};

struct cpufreq_real_policy {
	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
        unsigned int		policy; /* see above */
	struct cpufreq_governor	*governor; /* see below */
};

struct cpufreq_policy {
	cpumask_t		cpus;	/* affected CPUs */
	unsigned int		cpu;    /* cpu nr of registered CPU */
	struct cpufreq_cpuinfo	cpuinfo;/* see above */

	unsigned int		min;    /* in kHz */
	unsigned int		max;    /* in kHz */
	unsigned int		cur;    /* in kHz, only needed if cpufreq
					 * governors are used */
        unsigned int		policy; /* see above */
	struct cpufreq_governor	*governor; /* see below */

 	struct semaphore	lock;   /* CPU ->setpolicy or ->target may
					   only be called once a time */

	struct work_struct	update; /* if update_policy() needs to be
					 * called, but you're in IRQ context */

	struct cpufreq_real_policy	user_policy;

	struct kobject		kobj;
	struct completion	kobj_unregister;
};

#define CPUFREQ_ADJUST		(0)
#define CPUFREQ_INCOMPATIBLE	(1)
#define CPUFREQ_NOTIFY		(2)


/******************** cpufreq transition notifiers *******************/

#define CPUFREQ_PRECHANGE	(0)
#define CPUFREQ_POSTCHANGE	(1)
#define CPUFREQ_RESUMECHANGE	(8)

struct cpufreq_freqs {
	unsigned int cpu;	/* cpu nr */
	unsigned int old;
	unsigned int new;
	u8 flags;		/* flags of cpufreq_driver, see below. */
};


/**
 * cpufreq_scale - "old * mult / div" calculation for large values (32-bit-arch safe)
 * @old:   old value
 * @div:   divisor
 * @mult:  multiplier
 *
 *
 *    new = old * mult / div
 */
static inline unsigned long cpufreq_scale(unsigned long old, u_int div, u_int mult)
{
#if BITS_PER_LONG == 32

	u64 result = ((u64) old) * ((u64) mult);
	do_div(result, div);
	return (unsigned long) result;

#elif BITS_PER_LONG == 64

	unsigned long result = old * ((u64) mult);
	result /= div;
	return result;

#endif
};

/*********************************************************************
 *                          CPUFREQ GOVERNORS                        *
 *********************************************************************/

#define CPUFREQ_GOV_START  1
#define CPUFREQ_GOV_STOP   2
#define CPUFREQ_GOV_LIMITS 3

struct cpufreq_governor {
	char	name[CPUFREQ_NAME_LEN];
	int 	(*governor)	(struct cpufreq_policy *policy,
				 unsigned int event);
	struct list_head	governor_list;
	struct module		*owner;
};

/* pass a target to the cpufreq driver 
 */
extern int cpufreq_driver_target(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);
extern int __cpufreq_driver_target(struct cpufreq_policy *policy,
				   unsigned int target_freq,
				   unsigned int relation);


/* pass an event to the cpufreq governor */
int cpufreq_governor(unsigned int cpu, unsigned int event);

int cpufreq_register_governor(struct cpufreq_governor *governor);
void cpufreq_unregister_governor(struct cpufreq_governor *governor);


/*********************************************************************
 *                      CPUFREQ DRIVER INTERFACE                     *
 *********************************************************************/

#define CPUFREQ_RELATION_L 0  /* lowest frequency at or above target */
#define CPUFREQ_RELATION_H 1  /* highest frequency below or at target */

struct freq_attr;

struct cpufreq_driver {
	struct module           *owner;
	char			name[CPUFREQ_NAME_LEN];
	u8			flags;

	/* needed by all drivers */
	int	(*init)		(struct cpufreq_policy *policy);
	int	(*verify)	(struct cpufreq_policy *policy);

	/* define one out of two */
	int	(*setpolicy)	(struct cpufreq_policy *policy);
	int	(*target)	(struct cpufreq_policy *policy,
				 unsigned int target_freq,
				 unsigned int relation);

	/* should be defined, if possible */
	unsigned int	(*get)	(unsigned int cpu);

	/* optional */
	int	(*exit)		(struct cpufreq_policy *policy);
	int	(*resume)	(struct cpufreq_policy *policy);
	struct freq_attr	**attr;
};

/* flags */

#define CPUFREQ_STICKY		0x01	/* the driver isn't removed even if 
					 * all ->init() calls failed */
#define CPUFREQ_CONST_LOOPS 	0x02	/* loops_per_jiffy or other kernel
					 * "constants" aren't affected by
					 * frequency transitions */


int cpufreq_register_driver(struct cpufreq_driver *driver_data);
int cpufreq_unregister_driver(struct cpufreq_driver *driver_data);


void cpufreq_notify_transition(struct cpufreq_freqs *freqs, unsigned int state);


static inline void cpufreq_verify_within_limits(struct cpufreq_policy *policy, unsigned int min, unsigned int max) 
{
	if (policy->min < min)
		policy->min = min;
	if (policy->max < min)
		policy->max = min;
	if (policy->min > max)
		policy->min = max;
	if (policy->max > max)
		policy->max = max;
	if (policy->min > policy->max)
		policy->min = policy->max;
	return;
}

struct freq_attr {
	struct attribute attr;
	ssize_t (*show)(struct cpufreq_policy *, char *);
	ssize_t (*store)(struct cpufreq_policy *, const char *, size_t count);
};


/*********************************************************************
 *                        CPUFREQ 2.6. INTERFACE                     *
 *********************************************************************/
int cpufreq_set_policy(struct cpufreq_policy *policy);
int cpufreq_get_policy(struct cpufreq_policy *policy, unsigned int cpu);
int cpufreq_update_policy(unsigned int cpu);

/* query the current CPU frequency (in kHz). If zero, cpufreq couldn't detect it */
unsigned int cpufreq_get(unsigned int cpu);


/*********************************************************************
 *                       CPUFREQ DEFAULT GOVERNOR                    *
 *********************************************************************/


#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_PERFORMANCE
extern struct cpufreq_governor cpufreq_gov_performance;
#define CPUFREQ_DEFAULT_GOVERNOR	&cpufreq_gov_performance
#elif defined(CONFIG_CPU_FREQ_DEFAULT_GOV_USERSPACE)
extern struct cpufreq_governor cpufreq_gov_userspace;
#define CPUFREQ_DEFAULT_GOVERNOR	&cpufreq_gov_userspace
#endif


/*********************************************************************
 *                     FREQUENCY TABLE HELPERS                       *
 *********************************************************************/

#define CPUFREQ_ENTRY_INVALID ~0
#define CPUFREQ_TABLE_END     ~1

struct cpufreq_frequency_table {
	unsigned int	index;     /* any */
	unsigned int	frequency; /* kHz - doesn't need to be in ascending
				    * order */
};

int cpufreq_frequency_table_cpuinfo(struct cpufreq_policy *policy,
				    struct cpufreq_frequency_table *table);

int cpufreq_frequency_table_verify(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table);

int cpufreq_frequency_table_target(struct cpufreq_policy *policy,
				   struct cpufreq_frequency_table *table,
				   unsigned int target_freq,
				   unsigned int relation,
				   unsigned int *index);

/* the following 3 funtions are for cpufreq core use only */
struct cpufreq_frequency_table *cpufreq_frequency_get_table(unsigned int cpu);
struct cpufreq_policy *cpufreq_cpu_get(unsigned int cpu);
void   cpufreq_cpu_put (struct cpufreq_policy *data);

/* the following are really really optional */
extern struct freq_attr cpufreq_freq_attr_scaling_available_freqs;

void cpufreq_frequency_table_get_attr(struct cpufreq_frequency_table *table, 
				      unsigned int cpu);

void cpufreq_frequency_table_put_attr(unsigned int cpu);


/*********************************************************************
 *                     UNIFIED DEBUG HELPERS                         *
 *********************************************************************/

#define CPUFREQ_DEBUG_CORE	1
#define CPUFREQ_DEBUG_DRIVER	2
#define CPUFREQ_DEBUG_GOVERNOR	4

#ifdef CONFIG_CPU_FREQ_DEBUG

extern void cpufreq_debug_printk(unsigned int type, const char *prefix, 
				 const char *fmt, ...);

#else

#define cpufreq_debug_printk(msg...) do { } while(0)

#endif /* CONFIG_CPU_FREQ_DEBUG */

#endif /* _LINUX_CPUFREQ_H */
