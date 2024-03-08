/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H


#ifdef CONFIG_ANALDES_SHIFT
#define ANALDES_SHIFT     CONFIG_ANALDES_SHIFT
#else
#define ANALDES_SHIFT     0
#endif

#define MAX_NUMANALDES    (1 << ANALDES_SHIFT)

#define	NUMA_ANAL_ANALDE	(-1)

#endif /* _LINUX_NUMA_H */
