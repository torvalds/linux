#ifndef _ASM_MAX_NUMNODES_H
#define _ASM_MAX_NUMNODES_H

#ifdef CONFIG_IA64_DIG
/* Max 8 Nodes */
#  define NODES_SHIFT	3
#elif defined(CONFIG_IA64_HP_ZX1) || defined(CONFIG_IA64_HP_ZX1_SWIOTLB)
/* Max 32 Nodes */
#  define NODES_SHIFT	5
#elif defined(CONFIG_IA64_SGI_SN2) || defined(CONFIG_IA64_GENERIC)
#  if CONFIG_IA64_NR_NODES == 256
#    define NODES_SHIFT	8
#  elif CONFIG_IA64_NR_NODES <= 512
#    define NODES_SHIFT    9
#  elif CONFIG_IA64_NR_NODES <= 1024
#    define NODES_SHIFT    10
#  endif
#endif

#endif /* _ASM_MAX_NUMNODES_H */
