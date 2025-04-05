/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_BUG_H
#define _LINUX_BUG_H

#include <asm/bug.h>
#include <linux/compiler.h>
#include <linux/build_bug.h>

enum bug_trap_type {
	BUG_TRAP_TYPE_NONE = 0,
	BUG_TRAP_TYPE_WARN = 1,
	BUG_TRAP_TYPE_BUG = 2,
};

struct pt_regs;

#ifdef __CHECKER__
#define MAYBE_BUILD_BUG_ON(cond) (0)
#else /* __CHECKER__ */

#define MAYBE_BUILD_BUG_ON(cond)			\
	do {						\
		if (__builtin_constant_p((cond)))       \
			BUILD_BUG_ON(cond);             \
		else                                    \
			BUG_ON(cond);                   \
	} while (0)

#endif	/* __CHECKER__ */

#ifdef CONFIG_GENERIC_BUG
#include <asm-generic/bug.h>

static inline int is_warning_bug(const struct bug_entry *bug)
{
	return bug->flags & BUGFLAG_WARNING;
}

void bug_get_file_line(struct bug_entry *bug, const char **file,
		       unsigned int *line);

struct bug_entry *find_bug(unsigned long bugaddr);

enum bug_trap_type report_bug(unsigned long bug_addr, struct pt_regs *regs);

/* These are defined by the architecture */
int is_valid_bugaddr(unsigned long addr);

void generic_bug_clear_once(void);

#else	/* !CONFIG_GENERIC_BUG */

static inline void *find_bug(unsigned long bugaddr)
{
	return NULL;
}

static inline enum bug_trap_type report_bug(unsigned long bug_addr,
					    struct pt_regs *regs)
{
	return BUG_TRAP_TYPE_BUG;
}

struct bug_entry;
static inline void bug_get_file_line(struct bug_entry *bug, const char **file,
				     unsigned int *line)
{
	*file = NULL;
	*line = 0;
}

static inline void generic_bug_clear_once(void) {}

#endif	/* CONFIG_GENERIC_BUG */

#ifdef CONFIG_PRINTK
void mem_dump_obj(void *object);
#else
static inline void mem_dump_obj(void *object) {}
#endif

/*
 * Since detected data corruption should stop operation on the affected
 * structures. Return value must be checked and sanely acted on by caller.
 */
static inline __must_check bool check_data_corruption(bool v) { return v; }
#define CHECK_DATA_CORRUPTION(condition, addr, fmt, ...)		 \
	check_data_corruption(({					 \
		bool corruption = unlikely(condition);			 \
		if (corruption) {					 \
			if (addr)					 \
				mem_dump_obj(addr);			 \
			if (IS_ENABLED(CONFIG_BUG_ON_DATA_CORRUPTION)) { \
				pr_err(fmt, ##__VA_ARGS__);		 \
				BUG();					 \
			} else						 \
				WARN(1, fmt, ##__VA_ARGS__);		 \
		}							 \
		corruption;						 \
	}))

#endif	/* _LINUX_BUG_H */
