/* Public domain. */

#ifndef _LINUX_PM_H
#define _LINUX_PM_H

#include <linux/completion.h>

struct dev_pm_ops {
	int (*suspend)(struct device *);
	int (*resume)(struct device *);
};

#define DEFINE_SIMPLE_DEV_PM_OPS(name, suspend_fn, resume_fn)	\
    const struct dev_pm_ops name = { 				\
	    .suspend = suspend_fn, .resume = resume_fn		\
    }

struct dev_pm_domain {
};

typedef struct {
} pm_message_t;

#endif
