/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/mutex.h>

struct rv_interface {
	struct dentry		*root_dir;
	struct dentry		*monitors_dir;
};

#include "../trace.h"
#include <linux/tracefs.h>
#include <linux/rv.h>

#define RV_MODE_WRITE			TRACE_MODE_WRITE
#define RV_MODE_READ			TRACE_MODE_READ

#define rv_create_dir			tracefs_create_dir
#define rv_create_file			tracefs_create_file
#define rv_remove			tracefs_remove

#define MAX_RV_MONITOR_NAME_SIZE	32
#define MAX_RV_REACTOR_NAME_SIZE	32

extern struct mutex rv_interface_lock;

#ifdef CONFIG_RV_REACTORS
struct rv_reactor_def {
	struct list_head	list;
	struct rv_reactor	*reactor;
	/* protected by the monitor interface lock */
	int			counter;
};
#endif

struct rv_monitor_def {
	struct list_head	list;
	struct rv_monitor	*monitor;
	struct dentry		*root_d;
#ifdef CONFIG_RV_REACTORS
	struct rv_reactor_def	*rdef;
	bool			reacting;
#endif
	bool			task_monitor;
};

struct dentry *get_monitors_root(void);
int rv_disable_monitor(struct rv_monitor_def *mdef);
int rv_enable_monitor(struct rv_monitor_def *mdef);

#ifdef CONFIG_RV_REACTORS
int reactor_populate_monitor(struct rv_monitor_def *mdef);
void reactor_cleanup_monitor(struct rv_monitor_def *mdef);
int init_rv_reactors(struct dentry *root_dir);
#else
static inline int reactor_populate_monitor(struct rv_monitor_def *mdef)
{
	return 0;
}

static inline void reactor_cleanup_monitor(struct rv_monitor_def *mdef)
{
	return;
}

static inline int init_rv_reactors(struct dentry *root_dir)
{
	return 0;
}
#endif
