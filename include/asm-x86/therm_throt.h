#ifndef ASM_X86__THERM_THROT_H
#define ASM_X86__THERM_THROT_H

#include <asm/atomic.h>

extern atomic_t therm_throt_en;
int therm_throt_process(int curr);

#endif /* ASM_X86__THERM_THROT_H */
