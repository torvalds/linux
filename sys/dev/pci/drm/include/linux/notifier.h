/* Public domain. */

#ifndef _LINUX_NOTIFIER_H
#define _LINUX_NOTIFIER_H

struct notifier_block {
	int (*notifier_call)(struct notifier_block *, unsigned long, void *);
	SLIST_ENTRY(notifier_block) link;
};

struct blocking_notifier_head {
};

#define ATOMIC_INIT_NOTIFIER_HEAD(x)

#define NOTIFY_DONE	0
#define NOTIFY_OK	1
#define NOTIFY_BAD	2

#endif
