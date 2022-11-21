/* SPDX-License-Identifier: GPL-2.0 */
/* Simple ftrace probe wrapper */
#ifndef _LINUX_FPROBE_H
#define _LINUX_FPROBE_H

#include <linux/compiler.h>
#include <linux/ftrace.h>
#include <linux/rethook.h>

/**
 * struct fprobe - ftrace based probe.
 * @ops: The ftrace_ops.
 * @nmissed: The counter for missing events.
 * @flags: The status flag.
 * @rethook: The rethook data structure. (internal data)
 * @entry_handler: The callback function for function entry.
 * @exit_handler: The callback function for function exit.
 */
struct fprobe {
#ifdef CONFIG_FUNCTION_TRACER
	/*
	 * If CONFIG_FUNCTION_TRACER is not set, CONFIG_FPROBE is disabled too.
	 * But user of fprobe may keep embedding the struct fprobe on their own
	 * code. To avoid build error, this will keep the fprobe data structure
	 * defined here, but remove ftrace_ops data structure.
	 */
	struct ftrace_ops	ops;
#endif
	unsigned long		nmissed;
	unsigned int		flags;
	struct rethook		*rethook;

	void (*entry_handler)(struct fprobe *fp, unsigned long entry_ip, struct pt_regs *regs);
	void (*exit_handler)(struct fprobe *fp, unsigned long entry_ip, struct pt_regs *regs);
};

/* This fprobe is soft-disabled. */
#define FPROBE_FL_DISABLED	1

/*
 * This fprobe handler will be shared with kprobes.
 * This flag must be set before registering.
 */
#define FPROBE_FL_KPROBE_SHARED	2

static inline bool fprobe_disabled(struct fprobe *fp)
{
	return (fp) ? fp->flags & FPROBE_FL_DISABLED : false;
}

static inline bool fprobe_shared_with_kprobes(struct fprobe *fp)
{
	return (fp) ? fp->flags & FPROBE_FL_KPROBE_SHARED : false;
}

#ifdef CONFIG_FPROBE
int register_fprobe(struct fprobe *fp, const char *filter, const char *notfilter);
int register_fprobe_ips(struct fprobe *fp, unsigned long *addrs, int num);
int register_fprobe_syms(struct fprobe *fp, const char **syms, int num);
int unregister_fprobe(struct fprobe *fp);
#else
static inline int register_fprobe(struct fprobe *fp, const char *filter, const char *notfilter)
{
	return -EOPNOTSUPP;
}
static inline int register_fprobe_ips(struct fprobe *fp, unsigned long *addrs, int num)
{
	return -EOPNOTSUPP;
}
static inline int register_fprobe_syms(struct fprobe *fp, const char **syms, int num)
{
	return -EOPNOTSUPP;
}
static inline int unregister_fprobe(struct fprobe *fp)
{
	return -EOPNOTSUPP;
}
#endif

/**
 * disable_fprobe() - Disable fprobe
 * @fp: The fprobe to be disabled.
 *
 * This will soft-disable @fp. Note that this doesn't remove the ftrace
 * hooks from the function entry.
 */
static inline void disable_fprobe(struct fprobe *fp)
{
	if (fp)
		fp->flags |= FPROBE_FL_DISABLED;
}

/**
 * enable_fprobe() - Enable fprobe
 * @fp: The fprobe to be enabled.
 *
 * This will soft-enable @fp.
 */
static inline void enable_fprobe(struct fprobe *fp)
{
	if (fp)
		fp->flags &= ~FPROBE_FL_DISABLED;
}

#endif
