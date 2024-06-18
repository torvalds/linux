/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_EXT_H
#define _LINUX_SCHED_EXT_H

#ifdef CONFIG_SCHED_CLASS_EXT
#error "NOT IMPLEMENTED YET"
#else	/* !CONFIG_SCHED_CLASS_EXT */

static inline void sched_ext_free(struct task_struct *p) {}

#endif	/* CONFIG_SCHED_CLASS_EXT */
#endif	/* _LINUX_SCHED_EXT_H */
