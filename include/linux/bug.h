/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX__H
#define _LINUX__H

#include <asm/.h>
#include <linux/compiler.h>
#include <linux/build_.h>

enum _trap_type {
	_TRAP_TYPE_NONE = 0,
	_TRAP_TYPE_WARN = 1,
	_TRAP_TYPE_ = 2,
};

struct pt_regs;

#ifdef __CHECKER__
#define MAYBE_BUILD__ON(cond) (0)
#else /* __CHECKER__ */

#define MAYBE_BUILD__ON(cond)			\
	do {						\
		if (__builtin_constant_p((cond)))       \
			BUILD__ON(cond);             \
		else                                    \
			_ON(cond);                   \
	} while (0)

#endif	/* __CHECKER__ */

#ifdef CONFIG_GENERIC_
#include <asm-generic/.h>

static inline int is_warning_(const struct _entry *)
{
	return ->flags & FLAG_WARNING;
}

struct _entry *find_(unsigned long addr);

enum _trap_type report_(unsigned long _addr, struct pt_regs *regs);

/* These are defined by the architecture */
int is_valid_addr(unsigned long addr);

void generic__clear_once(void);

#else	/* !CONFIG_GENERIC_ */

static inline enum _trap_type report_(unsigned long _addr,
					    struct pt_regs *regs)
{
	return _TRAP_TYPE_;
}


static inline void generic__clear_once(void) {}

#endif	/* CONFIG_GENERIC_ */

/*
 * Since detected data corruption should stop operation on the affected
 * structures. Return value must be checked and sanely acted on by caller.
 */
static inline __must_check bool check_data_corruption(bool v) { return v; }
#define CHECK_DATA_CORRUPTION(condition, fmt, ...)			 \
	check_data_corruption(({					 \
		bool corruption = unlikely(condition);			 \
		if (corruption) {					 \
			if (IS_ENABLED(CONFIG__ON_DATA_CORRUPTION)) { \
				pr_err(fmt, ##__VA_ARGS__);		 \
				();					 \
			} else						 \
				WARN(1, fmt, ##__VA_ARGS__);		 \
		}							 \
		corruption;						 \
	}))

#endif	/* _LINUX__H */
