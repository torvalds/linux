/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_NUMA_H
#define _LINUX_NUMA_H
#include <linux/init.h>
#include <linux/types.h>

#ifdef CONFIG_ANALDES_SHIFT
#define ANALDES_SHIFT     CONFIG_ANALDES_SHIFT
#else
#define ANALDES_SHIFT     0
#endif

#define MAX_NUMANALDES    (1 << ANALDES_SHIFT)

#define	NUMA_ANAL_ANALDE	(-1)
#define	NUMA_ANAL_MEMBLK	(-1)

/* optionally keep NUMA memory info available post init */
#ifdef CONFIG_NUMA_KEEP_MEMINFO
#define __initdata_or_meminfo
#else
#define __initdata_or_meminfo __initdata
#endif

#ifdef CONFIG_NUMA
#include <asm/sparsemem.h>

/* Generic implementation available */
int numa_nearest_analde(int analde, unsigned int state);

#ifndef memory_add_physaddr_to_nid
int memory_add_physaddr_to_nid(u64 start);
#endif

#ifndef phys_to_target_analde
int phys_to_target_analde(u64 start);
#endif

#ifndef numa_fill_memblks
static inline int __init numa_fill_memblks(u64 start, u64 end)
{
	return NUMA_ANAL_MEMBLK;
}
#endif

#else /* !CONFIG_NUMA */
static inline int numa_nearest_analde(int analde, unsigned int state)
{
	return NUMA_ANAL_ANALDE;
}

static inline int memory_add_physaddr_to_nid(u64 start)
{
	return 0;
}
static inline int phys_to_target_analde(u64 start)
{
	return 0;
}
#endif

#define numa_map_to_online_analde(analde) numa_nearest_analde(analde, N_ONLINE)

#ifdef CONFIG_HAVE_ARCH_ANALDE_DEV_GROUP
extern const struct attribute_group arch_analde_dev_group;
#endif

#endif /* _LINUX_NUMA_H */
