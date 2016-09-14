#ifndef __CPUHOTPLUG_H
#define __CPUHOTPLUG_H

enum cpuhp_state {
	CPUHP_OFFLINE,
	CPUHP_CREATE_THREADS,
	CPUHP_NOTIFY_PREPARE,
	CPUHP_BRINGUP_CPU,
	CPUHP_AP_IDLE_DEAD,
	CPUHP_AP_OFFLINE,
	CPUHP_AP_SCHED_STARTING,
	CPUHP_AP_NOTIFY_STARTING,
	CPUHP_AP_ONLINE,
	CPUHP_TEARDOWN_CPU,
	CPUHP_AP_ONLINE_IDLE,
	CPUHP_AP_SMPBOOT_THREADS,
	CPUHP_AP_NOTIFY_ONLINE,
	CPUHP_AP_ONLINE_DYN,
	CPUHP_AP_ONLINE_DYN_END		= CPUHP_AP_ONLINE_DYN + 30,
	CPUHP_AP_ACTIVE,
	CPUHP_ONLINE,
};

int __cpuhp_setup_state(enum cpuhp_state state,	const char *name, bool invoke,
			int (*startup)(unsigned int cpu),
			int (*teardown)(unsigned int cpu));

/**
 * cpuhp_setup_state - Setup hotplug state callbacks with calling the callbacks
 * @state:	The state for which the calls are installed
 * @name:	Name of the callback (will be used in debug output)
 * @startup:	startup callback function
 * @teardown:	teardown callback function
 *
 * Installs the callback functions and invokes the startup callback on
 * the present cpus which have already reached the @state.
 */
static inline int cpuhp_setup_state(enum cpuhp_state state,
				    const char *name,
				    int (*startup)(unsigned int cpu),
				    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, true, startup, teardown);
}

/**
 * cpuhp_setup_state_nocalls - Setup hotplug state callbacks without calling the
 *			       callbacks
 * @state:	The state for which the calls are installed
 * @name:	Name of the callback.
 * @startup:	startup callback function
 * @teardown:	teardown callback function
 *
 * Same as @cpuhp_setup_state except that no calls are executed are invoked
 * during installation of this callback. NOP if SMP=n or HOTPLUG_CPU=n.
 */
static inline int cpuhp_setup_state_nocalls(enum cpuhp_state state,
					    const char *name,
					    int (*startup)(unsigned int cpu),
					    int (*teardown)(unsigned int cpu))
{
	return __cpuhp_setup_state(state, name, false, startup, teardown);
}

void __cpuhp_remove_state(enum cpuhp_state state, bool invoke);

/**
 * cpuhp_remove_state - Remove hotplug state callbacks and invoke the teardown
 * @state:	The state for which the calls are removed
 *
 * Removes the callback functions and invokes the teardown callback on
 * the present cpus which have already reached the @state.
 */
static inline void cpuhp_remove_state(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, true);
}

/**
 * cpuhp_remove_state_nocalls - Remove hotplug state callbacks without invoking
 *				teardown
 * @state:	The state for which the calls are removed
 */
static inline void cpuhp_remove_state_nocalls(enum cpuhp_state state)
{
	__cpuhp_remove_state(state, false);
}

#ifdef CONFIG_SMP
void cpuhp_online_idle(enum cpuhp_state state);
#else
static inline void cpuhp_online_idle(enum cpuhp_state state) { }
#endif

#endif
