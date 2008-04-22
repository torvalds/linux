
#ifndef _ASM_PAT_H
#define _ASM_PAT_H 1

#include <linux/types.h>

extern int pat_wc_enabled;

extern void pat_init(void);

extern int reserve_memtype(u64 start, u64 end,
		unsigned long req_type, unsigned long *ret_type);
extern int free_memtype(u64 start, u64 end);

#endif

