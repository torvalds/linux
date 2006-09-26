#ifndef __ASM_I386_THERM_THROT_H__
#define __ASM_I386_THERM_THROT_H__ 1

#include <asm/atomic.h>

extern atomic_t therm_throt_en;
int therm_throt_process(int curr);

#endif /* __ASM_I386_THERM_THROT_H__ */
