#ifndef _ASM_X86_32_NUMA_H
#define _ASM_X86_32_NUMA_H 1

extern int pxm_to_nid(int pxm);
extern void numa_remove_cpu(int cpu);

#ifdef CONFIG_NUMA
extern void __init remap_numa_kva(void);
extern void set_highmem_pages_init(void);
#else
static inline void remap_numa_kva(void)
{
}
#endif

#endif /* _ASM_X86_32_NUMA_H */
