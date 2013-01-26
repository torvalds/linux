#ifndef _LINUX_CONTEXT_TRACKING_H
#define _LINUX_CONTEXT_TRACKING_H

#ifdef CONFIG_CONTEXT_TRACKING
#include <linux/sched.h>

extern void user_enter(void);
extern void user_exit(void);
extern void context_tracking_task_switch(struct task_struct *prev,
					 struct task_struct *next);
#else
static inline void user_enter(void) { }
static inline void user_exit(void) { }
static inline void context_tracking_task_switch(struct task_struct *prev,
						struct task_struct *next) { }
#endif /* !CONFIG_CONTEXT_TRACKING */

#endif
