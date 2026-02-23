/* SPDX-License-Identifier: GPL-2.0 */

#ifdef CPU_FIELD
CPU_FIELD(__u32, yld_count, "sched_yield() count",
	  "%11u", false, yld_count, v16);
CPU_FIELD(__u32, array_exp, "Legacy counter can be ignored",
	  "%11u", false, array_exp, v16);
CPU_FIELD(__u32, sched_count, "schedule() called",
	  "%11u", false, sched_count, v16);
CPU_FIELD(__u32, sched_goidle, "schedule() left the processor idle",
	  "%11u", true, sched_count, v16);
CPU_FIELD(__u32, ttwu_count, "try_to_wake_up() was called",
	  "%11u", false, ttwu_count, v16);
CPU_FIELD(__u32, ttwu_local, "try_to_wake_up() was called to wake up the local cpu",
	  "%11u", true, ttwu_count, v16);
CPU_FIELD(__u64, rq_cpu_time, "total runtime by tasks on this processor (in jiffies)",
	  "%11llu", false, rq_cpu_time, v16);
CPU_FIELD(__u64, run_delay, "total waittime by tasks on this processor (in jiffies)",
	  "%11llu", true, rq_cpu_time, v16);
CPU_FIELD(__u64, pcount, "total timeslices run on this cpu",
	  "%11llu", false, pcount, v16);
#endif /* CPU_FIELD */

#ifdef DOMAIN_FIELD
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category busy> ");
#endif
DOMAIN_FIELD(__u32, busy_lb_count,
	     "load_balance() count on cpu busy", "%11u", true, v16);
DOMAIN_FIELD(__u32, busy_lb_balanced,
	     "load_balance() found balanced on cpu busy", "%11u", true, v16);
DOMAIN_FIELD(__u32, busy_lb_failed,
	     "load_balance() move task failed on cpu busy", "%11u", true, v16);
DOMAIN_FIELD(__u32, busy_lb_imbalance,
	     "imbalance sum on cpu busy", "%11u", false, v16);
DOMAIN_FIELD(__u32, busy_lb_gained,
	     "pull_task() count on cpu busy", "%11u", false, v16);
DOMAIN_FIELD(__u32, busy_lb_hot_gained,
	     "pull_task() when target task was cache-hot on cpu busy", "%11u", false, v16);
DOMAIN_FIELD(__u32, busy_lb_nobusyq,
	     "load_balance() failed to find busier queue on cpu busy", "%11u", true, v16);
DOMAIN_FIELD(__u32, busy_lb_nobusyg,
	     "load_balance() failed to find busier group on cpu busy", "%11u", true, v16);
#ifdef DERIVED_CNT_FIELD
DERIVED_CNT_FIELD(busy_lb_success_count, "load_balance() success count on cpu busy", "%11u",
		  busy_lb_count, busy_lb_balanced, busy_lb_failed, v16);
#endif
#ifdef DERIVED_AVG_FIELD
DERIVED_AVG_FIELD(busy_lb_avg_pulled,
		  "avg task pulled per successful lb attempt (cpu busy)", "%11.2Lf",
		  busy_lb_count, busy_lb_balanced, busy_lb_failed, busy_lb_gained, v16);
#endif
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category idle> ");
#endif
DOMAIN_FIELD(__u32, idle_lb_count,
	     "load_balance() count on cpu idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, idle_lb_balanced,
	     "load_balance() found balanced on cpu idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, idle_lb_failed,
	     "load_balance() move task failed on cpu idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, idle_lb_imbalance,
	     "imbalance sum on cpu idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, idle_lb_gained,
	     "pull_task() count on cpu idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, idle_lb_hot_gained,
	     "pull_task() when target task was cache-hot on cpu idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, idle_lb_nobusyq,
	     "load_balance() failed to find busier queue on cpu idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, idle_lb_nobusyg,
	     "load_balance() failed to find busier group on cpu idle", "%11u", true, v16);
#ifdef DERIVED_CNT_FIELD
DERIVED_CNT_FIELD(idle_lb_success_count, "load_balance() success count on cpu idle", "%11u",
		  idle_lb_count, idle_lb_balanced, idle_lb_failed, v16);
#endif
#ifdef DERIVED_AVG_FIELD
DERIVED_AVG_FIELD(idle_lb_avg_pulled,
		  "avg task pulled per successful lb attempt (cpu idle)", "%11.2Lf",
		  idle_lb_count, idle_lb_balanced, idle_lb_failed, idle_lb_gained, v16);
#endif
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category newidle> ");
#endif
DOMAIN_FIELD(__u32, newidle_lb_count,
	     "load_balance() count on cpu newly idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, newidle_lb_balanced,
	     "load_balance() found balanced on cpu newly idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, newidle_lb_failed,
	     "load_balance() move task failed on cpu newly idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, newidle_lb_imbalance,
	     "imbalance sum on cpu newly idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, newidle_lb_gained,
	     "pull_task() count on cpu newly idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, newidle_lb_hot_gained,
	     "pull_task() when target task was cache-hot on cpu newly idle", "%11u", false, v16);
DOMAIN_FIELD(__u32, newidle_lb_nobusyq,
	     "load_balance() failed to find busier queue on cpu newly idle", "%11u", true, v16);
DOMAIN_FIELD(__u32, newidle_lb_nobusyg,
	     "load_balance() failed to find busier group on cpu newly idle", "%11u", true, v16);
#ifdef DERIVED_CNT_FIELD
DERIVED_CNT_FIELD(newidle_lb_success_count,
		  "load_balance() success count on cpu newly idle", "%11u",
		  newidle_lb_count, newidle_lb_balanced, newidle_lb_failed, v16);
#endif
#ifdef DERIVED_AVG_FIELD
DERIVED_AVG_FIELD(newidle_lb_avg_count,
		  "avg task pulled per successful lb attempt (cpu newly idle)", "%11.2Lf",
		  newidle_lb_count, newidle_lb_balanced, newidle_lb_failed, newidle_lb_gained, v16);
#endif
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category active_load_balance()> ");
#endif
DOMAIN_FIELD(__u32, alb_count,
	     "active_load_balance() count", "%11u", false, v16);
DOMAIN_FIELD(__u32, alb_failed,
	     "active_load_balance() move task failed", "%11u", false, v16);
DOMAIN_FIELD(__u32, alb_pushed,
	     "active_load_balance() successfully moved a task", "%11u", false, v16);
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category sched_balance_exec()> ");
#endif
DOMAIN_FIELD(__u32, sbe_count,
	     "sbe_count is not used", "%11u", false, v16);
DOMAIN_FIELD(__u32, sbe_balanced,
	     "sbe_balanced is not used", "%11u", false, v16);
DOMAIN_FIELD(__u32, sbe_pushed,
	     "sbe_pushed is not used", "%11u", false, v16);
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Category sched_balance_fork()> ");
#endif
DOMAIN_FIELD(__u32, sbf_count,
	     "sbf_count is not used", "%11u", false, v16);
DOMAIN_FIELD(__u32, sbf_balanced,
	     "sbf_balanced is not used", "%11u", false, v16);
DOMAIN_FIELD(__u32, sbf_pushed,
	     "sbf_pushed is not used", "%11u", false, v16);
#ifdef DOMAIN_CATEGORY
DOMAIN_CATEGORY(" <Wakeup Info> ");
#endif
DOMAIN_FIELD(__u32, ttwu_wake_remote,
	     "try_to_wake_up() awoke a task that last ran on a diff cpu", "%11u", false, v16);
DOMAIN_FIELD(__u32, ttwu_move_affine,
	     "try_to_wake_up() moved task because cache-cold on own cpu", "%11u", false, v16);
DOMAIN_FIELD(__u32, ttwu_move_balance,
	     "try_to_wake_up() started passive balancing", "%11u", false, v16);
#endif /* DOMAIN_FIELD */
