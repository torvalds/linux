#ifndef _LINUX_UNWIND_H
#define _LINUX_UNWIND_H

/*
 * Copyright (C) 2002-2006 Novell, Inc.
 *	Jan Beulich <jbeulich@novell.com>
 * This code is released under version 2 of the GNU GPL.
 *
 * A simple API for unwinding kernel stacks.  This is used for
 * debugging and error reporting purposes.  The kernel doesn't need
 * full-blown stack unwinding with all the bells and whistles, so there
 * is not much point in implementing the full Dwarf2 unwind API.
 */

struct module;

struct unwind_frame_info {};

static inline void unwind_init(void) {}
static inline void unwind_setup(void) {}

#ifdef CONFIG_MODULES

static inline void *unwind_add_table(struct module *mod,
                                     const void *table_start,
                                     unsigned long table_size)
{
	return NULL;
}

static inline void unwind_remove_table(void *handle, int init_only)
{
}

#endif

static inline int unwind_init_frame_info(struct unwind_frame_info *info,
                                         struct task_struct *tsk,
                                         const struct pt_regs *regs)
{
	return -ENOSYS;
}

static inline int unwind_init_blocked(struct unwind_frame_info *info,
                                      struct task_struct *tsk)
{
	return -ENOSYS;
}

static inline int unwind_init_running(struct unwind_frame_info *info,
                                      asmlinkage int (*cb)(struct unwind_frame_info *,
                                                           void *arg),
                                      void *arg)
{
	return -ENOSYS;
}

static inline int unwind(struct unwind_frame_info *info)
{
	return -ENOSYS;
}

static inline int unwind_to_user(struct unwind_frame_info *info)
{
	return -ENOSYS;
}

#endif /* _LINUX_UNWIND_H */
