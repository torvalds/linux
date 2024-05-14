/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SMT_H
#define __SMT_H 1

struct cpu_topology;

/* Returns true if SMT (aka hyperthreading) is enabled. */
bool smt_on(const struct cpu_topology *topology);

/*
 * Returns true when system wide and all SMT threads for a core are in the
 * user_requested_cpus map.
 */
bool core_wide(bool system_wide, const char *user_requested_cpu_list,
	       const struct cpu_topology *topology);

#endif /* __SMT_H */
