// SPDX-License-Identifier: GPL-2.0-only
/*
 * These are various utility functions of the scheduler,
 * built in a single compilation unit for build efficiency reasons.
 *
 * ( Incidentally, the size of the compilation unit is roughly
 *   comparable to core.c, fair.c, smp.c and policy.c, the other
 *   big compilation units. This helps balance build time, while
 *   coalescing source files to amortize header inclusion
 *   cost. )
 */

#include "sched.h"
#include "sched-pelt.h"

#include <linux/sched_clock.h>

#include "clock.c"

#ifdef CONFIG_CGROUP_CPUACCT
# include "cpuacct.c"
#endif

#ifdef CONFIG_CPU_FREQ
# include "cpufreq.c"
#endif

#ifdef CONFIG_CPU_FREQ_GOV_SCHEDUTIL
# include "cpufreq_schedutil.c"
#endif

#ifdef CONFIG_SCHED_DEBUG
# include "debug.c"
#endif

#ifdef CONFIG_SCHEDSTATS
# include "stats.c"
#endif

#include "loadavg.c"
#include "completion.c"
#include "swait.c"
#include "wait_bit.c"
#include "wait.c"

#ifdef CONFIG_SMP
# include "cpupri.c"
# include "stop_task.c"
# include "topology.c"
#endif

#ifdef CONFIG_SCHED_CORE
# include "core_sched.c"
#endif

#ifdef CONFIG_PSI
# include "psi.c"
#endif

#ifdef CONFIG_MEMBARRIER
# include "membarrier.c"
#endif

#ifdef CONFIG_CPU_ISOLATION
# include "isolation.c"
#endif

#ifdef CONFIG_SCHED_AUTOGROUP
# include "autogroup.c"
#endif
