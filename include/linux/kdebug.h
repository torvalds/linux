#ifndef _LINUX_KDEBUG_H
#define _LINUX_KDEBUG_H

#include <asm/kdebug.h>

struct die_args {
	struct pt_regs *regs;
	const char *str;
	long err;
	int trapnr;
	int signr;
};

int register_die_notifier(struct notifier_block *nb);
int unregister_die_notifier(struct notifier_block *nb);

int notify_die(enum die_val val, const char *str,
	       struct pt_regs *regs, long err, int trap, int sig);

#endif /* _LINUX_KDEBUG_H */
