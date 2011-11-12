#ifndef _LINUX_ZENTUNE_H
#define _LINUX_ZENTUNE_H

#ifdef __KERNEL__

/* CPU Scheduler Related */

#ifdef CONFIG_SCHED_BFS
#define rr_interval_custom 3;
#define sched_iso_cpu_custom 25;
#endif

#ifdef CONFIG_SCHED_CFS
#define sysctl_sched_latency_custom 3000000ULL;
#define normalized_sysctl_sched_latency_custom 3000000ULL;
#define sysctl_sched_min_granularity_custom 300000ULL;
#define normalized_sysctl_sched_min_granularity_custom 300000ULL;
#define sched_nr_latency_custom 10
#endif

/* MM Related */
#define vm_dirty_ratio_custom 50;
#define dirty_background_ratio_custom 20;


#endif /* __KERNEL__ */
#endif /* _LINUX_ZENTUNE_H */
