/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Android scheduler hooks and modifications
 *
 * Put all of the android-specific scheduler hooks and changes
 * in this .h file to make merges and modifications easier.  It's also
 * simpler to notice what is, and is not, an upstream change this way over time.
 */


/*
 * task_may_not_preempt - check whether a task may not be preemptible soon
 */
static inline bool task_may_not_preempt(struct task_struct *task, int cpu)
{
	return false;
}

static inline bool uclamp_boosted(struct task_struct *p)
{
	return false;
}

static inline bool uclamp_latency_sensitive(struct task_struct *p)
{
	return false;
}
