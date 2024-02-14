/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Runtime Verification.
 *
 * For futher information, see: kernel/trace/rv/rv.c.
 */
#ifndef _LINUX_RV_H
#define _LINUX_RV_H

#define MAX_DA_NAME_LEN	24

#ifdef CONFIG_RV
/*
 * Deterministic automaton per-object variables.
 */
struct da_monitor {
	bool		monitoring;
	unsigned int	curr_state;
};

/*
 * Per-task RV monitors count. Nowadays fixed in RV_PER_TASK_MONITORS.
 * If we find justification for more monitors, we can think about
 * adding more or developing a dynamic method. So far, none of
 * these are justified.
 */
#define RV_PER_TASK_MONITORS		1
#define RV_PER_TASK_MONITOR_INIT	(RV_PER_TASK_MONITORS)

/*
 * Futher monitor types are expected, so make this a union.
 */
union rv_task_monitor {
	struct da_monitor da_mon;
};

#ifdef CONFIG_RV_REACTORS
struct rv_reactor {
	const char		*name;
	const char		*description;
	void			(*react)(char *msg);
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
	void			(*react)(char *msg);
#endif
};

bool rv_monitoring_on(void);
int rv_unregister_monitor(struct rv_monitor *monitor);
int rv_register_monitor(struct rv_monitor *monitor);
int rv_get_task_monitor_slot(void);
void rv_put_task_monitor_slot(int slot);

#ifdef CONFIG_RV_REACTORS
bool rv_reacting_on(void);
int rv_unregister_reactor(struct rv_reactor *reactor);
int rv_register_reactor(struct rv_reactor *reactor);
#endif /* CONFIG_RV_REACTORS */

#endif /* CONFIG_RV */
#endif /* _LINUX_RV_H */
