/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_PANIC_NOTIFIERS_H
#define _LINUX_PANIC_NOTIFIERS_H

#include <linux/notifier.h>
#include <linux/types.h>

extern struct atomic_notifier_head panic_notifier_list;

extern bool crash_kexec_post_notifiers;

#endif	/* _LINUX_PANIC_NOTIFIERS_H */
