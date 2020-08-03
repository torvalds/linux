/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Scheduler internal SMP callback types and methods between the scheduler
 * and other internal parts of the core kernel:
 */

extern void sched_ttwu_pending(void *arg);

extern void send_call_function_single_ipi(int cpu);
