/* Public domain. */

#ifndef _LINUX_SUSPEND_H
#define _LINUX_SUSPEND_H

#include <linux/notifier.h>

typedef int suspend_state_t;
extern suspend_state_t pm_suspend_target_state;

#define PM_SUSPEND_ON		0
#define PM_SUSPEND_MEM		1
#define PM_SUSPEND_TO_IDLE	2

enum {
	PM_HIBERNATION_PREPARE,
	PM_POST_HIBERNATION,
	PM_SUSPEND_PREPARE,
	PM_POST_SUSPEND
};

static inline int
register_pm_notifier(struct notifier_block *n)
{
	return 0;
}

static inline int
unregister_pm_notifier(struct notifier_block *n)
{
	return 0;
}

#endif
