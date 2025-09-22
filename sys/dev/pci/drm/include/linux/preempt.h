/* Public domain. */

#ifndef _LINUX_PREEMPT_H
#define _LINUX_PREEMPT_H

#include <asm/preempt.h>
#include <sys/param.h> /* for curcpu in machine/cpu.h */

static inline void
preempt_enable(void)
{
}

static inline void
preempt_disable(void)
{
}

static inline void
migrate_enable(void)
{
}

static inline void
migrate_disable(void)
{
}

static inline bool
in_irq(void)
{
	return (curcpu()->ci_idepth > 0);
}

static inline bool
in_interrupt(void)
{
	return in_irq();
}

static inline bool
in_task(void)
{
	return !in_irq();
}

static inline bool
in_atomic(void)
{
	return false;
}

#endif
