/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SMT_H
#define __SMT_H 1

struct cpu_topology;

/* Returns true if SMT (aka hyperthreading) is enabled. */
bool smt_on(const struct cpu_topology *topology);

#endif /* __SMT_H */
