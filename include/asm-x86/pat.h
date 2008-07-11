
#ifndef _ASM_PAT_H
#define _ASM_PAT_H 1

#include <linux/types.h>

#ifdef CONFIG_X86_PAT
extern int pat_wc_enabled;
extern void validate_pat_support(struct cpuinfo_x86 *c);
#else
static const int pat_wc_enabled = 0;
static inline void validate_pat_support(struct cpuinfo_x86 *c) { }
#endif

extern void pat_init(void);

extern int reserve_memtype(u64 start, u64 end,
		unsigned long req_type, unsigned long *ret_type);
extern int free_memtype(u64 start, u64 end);

extern void pat_disable(char *reason);

#endif

