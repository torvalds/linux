#ifndef __LINUX_PREEMPT_H
#define __LINUX_PREEMPT_H

extern int preempt_count;

#define preempt_disable()	uatomic_inc(&preempt_count)
#define preempt_enable()	uatomic_dec(&preempt_count)

static inline int in_interrupt(void)
{
	return 0;
}

#endif /* __LINUX_PREEMPT_H */
