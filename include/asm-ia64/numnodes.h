#ifndef _ASM_MAX_NUMNODES_H
#define _ASM_MAX_NUMNODES_H

#ifdef CONFIG_IA64_DIG
/* Max 8 Nodes */
#define NODES_SHIFT	3
#elif defined(CONFIG_IA64_HP_ZX1) || defined(CONFIG_IA64_HP_ZX1_SWIOTLB)
/* Max 32 Nodes */
#define NODES_SHIFT	5
#elif defined(CONFIG_IA64_SGI_SN2) || defined(CONFIG_IA64_GENERIC)
/* Max 256 Nodes */
#define NODES_SHIFT	8
#endif

#endif /* _ASM_MAX_NUMNODES_H */
