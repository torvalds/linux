/*
 *  include/asm-s390/timer.h
 *
 *  (C) Copyright IBM Corp. 2003,2006
 *  Virtual CPU timer
 *
 *  Author: Jan Glauber (jang@de.ibm.com)
 */

#ifndef _ASM_S390_TIMER_H
#define _ASM_S390_TIMER_H

#ifdef __KERNEL__

#include <linux/timer.h>

#define VTIMER_MAX_SLICE (0x7ffffffffffff000LL)

struct vtimer_list {
	struct list_head entry;

	int cpu;
	__u64 expires;
	__u64 interval;

	spinlock_t lock;
	unsigned long magic;

	void (*function)(unsigned long, struct pt_regs*);
	unsigned long data;
};

/* the offset value will wrap after ca. 71 years */
struct vtimer_queue {
	struct list_head list;
	spinlock_t lock;
	__u64 to_expire;	  /* current event expire time */
	__u64 offset;		  /* list offset to zero */
	__u64 idle;		  /* temp var for idle */
};

extern void init_virt_timer(struct vtimer_list *timer);
extern void add_virt_timer(void *new);
extern void add_virt_timer_periodic(void *new);
extern int mod_virt_timer(struct vtimer_list *timer, __u64 expires);
extern int del_virt_timer(struct vtimer_list *timer);

#endif /* __KERNEL__ */

#endif /* _ASM_S390_TIMER_H */
