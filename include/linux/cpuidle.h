/*
 * cpuidle.h - a generic framework for CPU idle power management
 *
 * (C) 2007 Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *          Shaohua Li <shaohua.li@intel.com>
 *          Adam Belay <abelay@novell.com>
 *
 * This code is licenced under the GPL.
 */

#ifndef _LINUX_CPUIDLE_H
#define _LINUX_CPUIDLE_H

#include <linux/percpu.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/completion.h>

#define CPUIDLE_STATE_MAX	8
#define CPUIDLE_NAME_LEN	16
#define CPUIDLE_DESC_LEN	32

struct cpuidle_device;


/****************************
 * CPUIDLE DEVICE INTERFACE *
 ****************************/

struct cpuidle_state {
	char		name[CPUIDLE_NAME_LEN];
	char		desc[CPUIDLE_DESC_LEN];
	void		*driver_data;

	unsigned int	flags;
	unsigned int	exit_latency; /* in US */
	unsigned int	power_usage; /* in mW */
	unsigned int	target_residency; /* in US */

	unsigned long long	usage;
	unsigned long long	time; /* in US */

	int (*enter)	(struct cpuidle_device *dev,
			 struct cpuidle_state *state);
};

/* Idle State Flags */
#define CPUIDLE_FLAG_TIME_VALID	(0x01) /* is residency time measurable? */
#define CPUIDLE_FLAG_CHECK_BM	(0x02) /* BM activity will exit state */
#define CPUIDLE_FLAG_POLL	(0x10) /* no latency, no savings */
#define CPUIDLE_FLAG_SHALLOW	(0x20) /* low latency, minimal savings */
#define CPUIDLE_FLAG_BALANCED	(0x40) /* medium latency, moderate savings */
#define CPUIDLE_FLAG_DEEP	(0x80) /* high latency, large savings */
#define CPUIDLE_FLAG_IGNORE	(0x100) /* ignore during this idle period */

#define CPUIDLE_DRIVER_FLAGS_MASK (0xFFFF0000)

/**
 * cpuidle_get_statedata - retrieves private driver state data
 * @state: the state
 */
static inline void * cpuidle_get_statedata(struct cpuidle_state *state)
{
	return state->driver_data;
}

/**
 * cpuidle_set_statedata - stores private driver state data
 * @state: the state
 * @data: the private data
 */
static inline void
cpuidle_set_statedata(struct cpuidle_state *state, void *data)
{
	state->driver_data = data;
}

struct cpuidle_state_kobj {
	struct cpuidle_state *state;
	struct completion kobj_unregister;
	struct kobject kobj;
};

struct cpuidle_device {
	unsigned int		registered:1;
	unsigned int		enabled:1;
	unsigned int		power_specified:1;
	unsigned int		cpu;

	int			last_residency;
	int			state_count;
	struct cpuidle_state	states[CPUIDLE_STATE_MAX];
	struct cpuidle_state_kobj *kobjs[CPUIDLE_STATE_MAX];
	struct cpuidle_state	*last_state;

	struct list_head 	device_list;
	struct kobject		kobj;
	struct completion	kobj_unregister;
	void			*governor_data;
	struct cpuidle_state	*safe_state;

	int (*prepare)		(struct cpuidle_device *dev);
};

DECLARE_PER_CPU(struct cpuidle_device *, cpuidle_devices);

/**
 * cpuidle_get_last_residency - retrieves the last state's residency time
 * @dev: the target CPU
 *
 * NOTE: this value is invalid if CPUIDLE_FLAG_TIME_VALID isn't set
 */
static inline int cpuidle_get_last_residency(struct cpuidle_device *dev)
{
	return dev->last_residency;
}


/****************************
 * CPUIDLE DRIVER INTERFACE *
 ****************************/

struct cpuidle_driver {
	char			name[CPUIDLE_NAME_LEN];
	struct module 		*owner;
};

#ifdef CONFIG_CPU_IDLE

extern int cpuidle_register_driver(struct cpuidle_driver *drv);
struct cpuidle_driver *cpuidle_get_driver(void);
extern void cpuidle_unregister_driver(struct cpuidle_driver *drv);
extern int cpuidle_register_device(struct cpuidle_device *dev);
extern void cpuidle_unregister_device(struct cpuidle_device *dev);

extern void cpuidle_pause_and_lock(void);
extern void cpuidle_resume_and_unlock(void);
extern int cpuidle_enable_device(struct cpuidle_device *dev);
extern void cpuidle_disable_device(struct cpuidle_device *dev);

#else

static inline int cpuidle_register_driver(struct cpuidle_driver *drv)
{return -ENODEV; }
static inline struct cpuidle_driver *cpuidle_get_driver(void) {return NULL; }
static inline void cpuidle_unregister_driver(struct cpuidle_driver *drv) { }
static inline int cpuidle_register_device(struct cpuidle_device *dev)
{return -ENODEV; }
static inline void cpuidle_unregister_device(struct cpuidle_device *dev) { }

static inline void cpuidle_pause_and_lock(void) { }
static inline void cpuidle_resume_and_unlock(void) { }
static inline int cpuidle_enable_device(struct cpuidle_device *dev)
{return -ENODEV; }
static inline void cpuidle_disable_device(struct cpuidle_device *dev) { }

#endif

/******************************
 * CPUIDLE GOVERNOR INTERFACE *
 ******************************/

struct cpuidle_governor {
	char			name[CPUIDLE_NAME_LEN];
	struct list_head 	governor_list;
	unsigned int		rating;

	int  (*enable)		(struct cpuidle_device *dev);
	void (*disable)		(struct cpuidle_device *dev);

	int  (*select)		(struct cpuidle_device *dev);
	void (*reflect)		(struct cpuidle_device *dev);

	struct module 		*owner;
};

#ifdef CONFIG_CPU_IDLE

extern int cpuidle_register_governor(struct cpuidle_governor *gov);
extern void cpuidle_unregister_governor(struct cpuidle_governor *gov);

#else

static inline int cpuidle_register_governor(struct cpuidle_governor *gov)
{return 0;}
static inline void cpuidle_unregister_governor(struct cpuidle_governor *gov) { }

#endif

#ifdef CONFIG_ARCH_HAS_CPU_RELAX
#define CPUIDLE_DRIVER_STATE_START	1
#else
#define CPUIDLE_DRIVER_STATE_START	0
#endif

#endif /* _LINUX_CPUIDLE_H */
