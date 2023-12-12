# SPDX-License-Identifier: GPL-2.0-only

KCOV_INSTRUMENT := n
KCSAN_SANITIZE := n

obj-$(CONFIG_SCHED_WALT) += sched-walt.o
sched-walt-$(CONFIG_SCHED_WALT) := walt.o boost.o sched_avg.o walt_halt.o core_ctl.o trace.o input-boost.o sysctl.o cpufreq_walt.o fixup.o walt_lb.o walt_rt.o walt_cfs.o walt_tp.o mvp_locking.o

obj-$(CONFIG_SCHED_WALT_DEBUG) += sched-walt-debug.o
sched-walt-debug-$(CONFIG_SCHED_WALT_DEBUG) := walt_debug.o preemptirq_long.o
