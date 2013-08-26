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
#include <linux/hrtimer.h>

#define CPUIDLE_STATE_MAX	10
#define CPUIDLE_NAME_LEN	16
#define CPUIDLE_DESC_LEN	32

struct module;

struct cpuidle_device;
struct cpuidle_driver;


/****************************
 * CPUIDLE DEVICE INTERFACE *
 ****************************/

struct cpuidle_state_usage {
	unsigned long long	disable;
	unsigned long long	usage;
	unsigned long long	time; /* in US */
};

struct cpuidle_state {
	char		name[CPUIDLE_NAME_LEN];
	char		desc[CPUIDLE_DESC_LEN];

	unsigned int	flags;
	unsigned int	exit_latency; /* in US */
	int		power_usage; /* in mW */
	unsigned int	target_residency; /* in US */
	bool		disabled; /* disabled on all CPUs */

	int (*enter)	(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index);

	int (*enter_dead) (struct cpuidle_device *dev, int index);
};

/* Idle State Flags */
#define CPUIDLE_FLAG_TIME_VALID	(0x01) /* is residency time measurable? */
#define CPUIDLE_FLAG_COUPLED	(0x02) /* state applies to multiple cpus */
#define CPUIDLE_FLAG_TIMER_STOP (0x04)  /* timer is stopped on this state */

#define CPUIDLE_DRIVER_FLAGS_MASK (0xFFFF0000)

struct cpuidle_device_kobj;
struct cpuidle_state_kobj;
struct cpuidle_driver_kobj;

struct cpuidle_device {
	unsigned int		registered:1;
	unsigned int		enabled:1;
	unsigned int		cpu;

	int			last_residency;
	int			state_count;
	struct cpuidle_state_usage	states_usage[CPUIDLE_STATE_MAX];
	struct cpuidle_state_kobj *kobjs[CPUIDLE_STATE_MAX];
	struct cpuidle_driver_kobj *kobj_driver;
	struct cpuidle_device_kobj *kobj_dev;
	struct list_head 	device_list;

#ifdef CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED
	int			safe_state_index;
	cpumask_t		coupled_cpus;
	struct cpuidle_coupled	*coupled;
#endif
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
	const char		*name;
	struct module 		*owner;
	int                     refcnt;

        /* used by the cpuidle framework to setup the broadcast timer */
	unsigned int            bctimer:1;
	/* states array must be ordered in decreasing power consumption */
	struct cpuidle_state	states[CPUIDLE_STATE_MAX];
	int			state_count;
	int			safe_state_index;

	/* the driver handles the cpus in cpumask */
	struct cpumask       *cpumask;
};

#ifdef CONFIG_CPU_IDLE
extern void disable_cpuidle(void);
extern int cpuidle_idle_call(void);
extern int cpuidle_register_driver(struct cpuidle_driver *drv);
extern struct cpuidle_driver *cpuidle_get_driver(void);
extern struct cpuidle_driver *cpuidle_driver_ref(void);
extern void cpuidle_driver_unref(void);
extern void cpuidle_unregister_driver(struct cpuidle_driver *drv);
extern int cpuidle_register_device(struct cpuidle_device *dev);
extern void cpuidle_unregister_device(struct cpuidle_device *dev);
extern int cpuidle_register(struct cpuidle_driver *drv,
			    const struct cpumask *const coupled_cpus);
extern void cpuidle_unregister(struct cpuidle_driver *drv);
extern void cpuidle_pause_and_lock(void);
extern void cpuidle_resume_and_unlock(void);
extern void cpuidle_pause(void);
extern void cpuidle_resume(void);
extern int cpuidle_enable_device(struct cpuidle_device *dev);
extern void cpuidle_disable_device(struct cpuidle_device *dev);
extern int cpuidle_play_dead(void);

extern struct cpuidle_driver *cpuidle_get_cpu_driver(struct cpuidle_device *dev);
#else
static inline void disable_cpuidle(void) { }
static inline int cpuidle_idle_call(void) { return -ENODEV; }
static inline int cpuidle_register_driver(struct cpuidle_driver *drv)
{return -ENODEV; }
static inline struct cpuidle_driver *cpuidle_get_driver(void) {return NULL; }
static inline struct cpuidle_driver *cpuidle_driver_ref(void) {return NULL; }
static inline void cpuidle_driver_unref(void) {}
static inline void cpuidle_unregister_driver(struct cpuidle_driver *drv) { }
static inline int cpuidle_register_device(struct cpuidle_device *dev)
{return -ENODEV; }
static inline void cpuidle_unregister_device(struct cpuidle_device *dev) { }
static inline int cpuidle_register(struct cpuidle_driver *drv,
				   const struct cpumask *const coupled_cpus)
{return -ENODEV; }
static inline void cpuidle_unregister(struct cpuidle_driver *drv) { }
static inline void cpuidle_pause_and_lock(void) { }
static inline void cpuidle_resume_and_unlock(void) { }
static inline void cpuidle_pause(void) { }
static inline void cpuidle_resume(void) { }
static inline int cpuidle_enable_device(struct cpuidle_device *dev)
{return -ENODEV; }
static inline void cpuidle_disable_device(struct cpuidle_device *dev) { }
static inline int cpuidle_play_dead(void) {return -ENODEV; }
#endif

#ifdef CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED
void cpuidle_coupled_parallel_barrier(struct cpuidle_device *dev, atomic_t *a);
#else
static inline void cpuidle_coupled_parallel_barrier(struct cpuidle_device *dev, atomic_t *a)
{
}
#endif

/******************************
 * CPUIDLE GOVERNOR INTERFACE *
 ******************************/

struct cpuidle_governor {
	char			name[CPUIDLE_NAME_LEN];
	struct list_head 	governor_list;
	unsigned int		rating;

	int  (*enable)		(struct cpuidle_driver *drv,
					struct cpuidle_device *dev);
	void (*disable)		(struct cpuidle_driver *drv,
					struct cpuidle_device *dev);

	int  (*select)		(struct cpuidle_driver *drv,
					struct cpuidle_device *dev);
	void (*reflect)		(struct cpuidle_device *dev, int index);

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
