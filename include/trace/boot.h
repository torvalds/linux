#ifndef _LINUX_TRACE_BOOT_H
#define _LINUX_TRACE_BOOT_H

/*
 * Structure which defines the trace of an initcall.
 * You don't have to fill the func field since it is
 * only used internally by the tracer.
 */
struct boot_trace {
	pid_t			caller;
	char			func[KSYM_NAME_LEN];
	int			result;
	unsigned long long	duration;		/* usecs */
	ktime_t			calltime;
	ktime_t			rettime;
};

#ifdef CONFIG_BOOT_TRACER
/* Append the trace on the ring-buffer */
extern void trace_boot(struct boot_trace *it, initcall_t fn);

/* Tells the tracer that smp_pre_initcall is finished.
 * So we can start the tracing
 */
extern void start_boot_trace(void);

/* Resume the tracing of other necessary events
 * such as sched switches
 */
extern void enable_boot_trace(void);

/* Suspend this tracing. Actually, only sched_switches tracing have
 * to be suspended. Initcalls doesn't need it.)
 */
extern void disable_boot_trace(void);
#else
static inline void trace_boot(struct boot_trace *it, initcall_t fn) { }
static inline void start_boot_trace(void) { }
static inline void enable_boot_trace(void) { }
static inline void disable_boot_trace(void) { }
#endif /* CONFIG_BOOT_TRACER */

#endif /* __LINUX_TRACE_BOOT_H */
