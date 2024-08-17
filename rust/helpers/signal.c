// SPDX-License-Identifier: GPL-2.0

#include <linux/export.h>
#include <linux/sched/signal.h>

int rust_helper_signal_pending(struct task_struct *t)
{
	return signal_pending(t);
}
