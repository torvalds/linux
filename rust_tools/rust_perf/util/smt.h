/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SMT_H
#define __SMT_H 1

/*
 * Returns true if SMT (aka hyperthreading) is enabled. Determined via sysfs or
 * the online topology.
 */
bool smt_on(void);

/*
 * Returns true when system wide and all SMT threads for a core are in the
 * user_requested_cpus map.
 */
bool core_wide(bool system_wide, const char *user_requested_cpu_list);

#endif /* __SMT_H */
