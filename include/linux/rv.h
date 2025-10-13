/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime Verification.
 *
 * For futher information, see: kernel/trace/rv/rv.c.
 */
#ifndef _LINUX_RV_H
#define _LINUX_RV_H

#define MAX_DA_NAME_LEN			32
#define MAX_DA_RETRY_RACING_EVENTS	3

#ifdef CONFIG_RV
#include <linux/array_size.h>
#include <linux/bitops.h>
#include <linux/list.h>
#include <linux/types.h>

/*
 * Deterministic automaton per-object variables.
 */
struct da_monitor {
	bool		monitoring;
	unsigned int	curr_state;
};

#ifdef CONFIG_RV_LTL_MONITOR

/*
 * In the future, if the number of atomic propositions or the size of Buchi
 * automaton is larger, we can switch to dynamic allocation. For now, the code
 * is simpler this way.
 */
#define RV_MAX_LTL_ATOM 32
#define RV_MAX_BA_STATES 32

/**
 * struct ltl_monitor - A linear temporal logic runtime verification monitor
 * @states:	States in the Buchi automaton. As Buchi automaton is a
 *		non-deterministic state machine, the monitor can be in multiple
 *		states simultaneously. This is a bitmask of all possible states.
 *		If this is zero, that means either:
 *		    - The monitor has not started yet (e.g. because not all
 *		      atomic propositions are known).
 *		    - There is no possible state to be in. In other words, a
 *		      violation of the LTL property is detected.
 * @atoms:	The values of atomic propositions.
 * @unknown_atoms: Atomic propositions which are still unknown.
 */
struct ltl_monitor {
	DECLARE_BITMAP(states, RV_MAX_BA_STATES);
	DECLARE_BITMAP(atoms, RV_MAX_LTL_ATOM);
	DECLARE_BITMAP(unknown_atoms, RV_MAX_LTL_ATOM);
};

static inline bool rv_ltl_valid_state(struct ltl_monitor *mon)
{
	for (int i = 0; i < ARRAY_SIZE(mon->states); ++i) {
		if (mon->states[i])
			return true;
	}
	return false;
}

static inline bool rv_ltl_all_atoms_known(struct ltl_monitor *mon)
{
	for (int i = 0; i < ARRAY_SIZE(mon->unknown_atoms); ++i) {
		if (mon->unknown_atoms[i])
			return false;
	}
	return true;
}

#else

struct ltl_monitor {};

#endif /* CONFIG_RV_LTL_MONITOR */

#define RV_PER_TASK_MONITOR_INIT	(CONFIG_RV_PER_TASK_MONITORS)

union rv_task_monitor {
	struct da_monitor	da_mon;
	struct ltl_monitor	ltl_mon;
};

#ifdef CONFIG_RV_REACTORS
struct rv_reactor {
	const char		*name;
	const char		*description;
	__printf(1, 2) void	(*react)(const char *msg, ...);
	struct list_head	list;
};
#endif

struct rv_monitor {
	const char		*name;
	const char		*description;
	bool			enabled;
	int			(*enable)(void);
	void			(*disable)(void);
	void			(*reset)(void);
#ifdef CONFIG_RV_REACTORS
	struct rv_reactor	*reactor;
	__printf(1, 2) void	(*react)(const char *msg, ...);
#endif
	struct list_head	list;
	struct rv_monitor	*parent;
	struct dentry		*root_d;
};

bool rv_monitoring_on(void);
int rv_unregister_monitor(struct rv_monitor *monitor);
int rv_register_monitor(struct rv_monitor *monitor, struct rv_monitor *parent);
int rv_get_task_monitor_slot(void);
void rv_put_task_monitor_slot(int slot);

#ifdef CONFIG_RV_REACTORS
bool rv_reacting_on(void);
int rv_unregister_reactor(struct rv_reactor *reactor);
int rv_register_reactor(struct rv_reactor *reactor);
#else
static inline bool rv_reacting_on(void)
{
	return false;
}
#endif /* CONFIG_RV_REACTORS */

#endif /* CONFIG_RV */
#endif /* _LINUX_RV_H */
