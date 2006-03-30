/*
 * include/asm-parisc/cache.h
 */

#ifndef __ARCH_PARISC_CACHE_H
#define __ARCH_PARISC_CACHE_H

#include <linux/config.h>

/*
 * PA 2.0 processors have 64-byte cachelines; PA 1.1 processors have
 * 32-byte cachelines.  The default configuration is not for SMP anyway,
 * so if you're building for SMP, you should select the appropriate
 * processor type.  There is a potential livelock danger when running
 * a machine with this value set too small, but it's more probable you'll
 * just ruin performance.
 */
#ifdef CONFIG_PA20
#define L1_CACHE_BYTES 64
#define L1_CACHE_SHIFT 6
#else
#define L1_CACHE_BYTES 32
#define L1_CACHE_SHIFT 5
#endif

#ifndef __ASSEMBLY__

#define L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))

#define SMP_CACHE_BYTES L1_CACHE_BYTES

#define __read_mostly __attribute__((__section__(".data.read_mostly")))

extern void flush_data_cache_local(void *);  /* flushes local data-cache only */
extern void flush_instruction_cache_local(void *); /* flushes local code-cache only */
#ifdef CONFIG_SMP
extern void flush_data_cache(void); /* flushes data-cache only (all processors) */
extern void flush_instruction_cache(void); /* flushes i-cache only (all processors) */
#else
#define flush_data_cache() flush_data_cache_local(NULL)
#define flush_instruction_cache() flush_instruction_cache_local(NULL)
#endif

extern void parisc_cache_init(void);	/* initializes cache-flushing */
extern void flush_all_caches(void);     /* flush everything (tlb & cache) */
extern int get_cache_info(char *);
extern void flush_user_icache_range_asm(unsigned long, unsigned long);
extern void flush_kernel_icache_range_asm(unsigned long, unsigned long);
extern void flush_user_dcache_range_asm(unsigned long, unsigned long);
extern void flush_kernel_dcache_range_asm(unsigned long, unsigned long);
extern void flush_kernel_dcache_page(void *);
extern void flush_kernel_icache_page(void *);
extern void disable_sr_hashing(void);   /* turns off space register hashing */
extern void disable_sr_hashing_asm(int); /* low level support for above */
extern void free_sid(unsigned long);
unsigned long alloc_sid(void);
extern void flush_user_dcache_page(unsigned long);
extern void flush_user_icache_page(unsigned long);

struct seq_file;
extern void show_cache_info(struct seq_file *m);

extern int split_tlb;
extern int dcache_stride;
extern int icache_stride;
extern struct pdc_cache_info cache_info;

#define pdtlb(addr)         asm volatile("pdtlb 0(%%sr1,%0)" : : "r" (addr));
#define pitlb(addr)         asm volatile("pitlb 0(%%sr1,%0)" : : "r" (addr));
#define pdtlb_kernel(addr)  asm volatile("pdtlb 0(%0)" : : "r" (addr));

#endif /* ! __ASSEMBLY__ */

/* Classes of processor wrt: disabling space register hashing */

#define SRHASH_PCXST    0   /* pcxs, pcxt, pcxt_ */
#define SRHASH_PCXL     1   /* pcxl */
#define SRHASH_PA20     2   /* pcxu, pcxu_, pcxw, pcxw_ */

#endif
