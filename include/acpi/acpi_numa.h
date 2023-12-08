/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_NUMA_H
#define __ACPI_NUMA_H

#ifdef CONFIG_ACPI_NUMA
#include <linux/numa.h>

/* Proximity bitmap length */
#if MAX_NUMNODES > 256
#define MAX_PXM_DOMAINS MAX_NUMNODES
#else
#define MAX_PXM_DOMAINS (256)	/* Old pxm spec is defined 8 bit */
#endif

extern int pxm_to_node(int);
extern int node_to_pxm(int);
extern int acpi_map_pxm_to_node(int);
extern unsigned char acpi_srat_revision;
extern void disable_srat(void);

extern void bad_srat(void);
extern int srat_disabled(void);

#else				/* CONFIG_ACPI_NUMA */
static inline void disable_srat(void)
{
}
static inline int pxm_to_node(int pxm)
{
	return 0;
}
static inline int node_to_pxm(int node)
{
	return 0;
}
#endif				/* CONFIG_ACPI_NUMA */

#ifdef CONFIG_ACPI_HMAT
extern void disable_hmat(void);
#else				/* CONFIG_ACPI_HMAT */
static inline void disable_hmat(void)
{
}
#endif				/* CONFIG_ACPI_HMAT */
#endif				/* __ACPI_NUMA_H */
