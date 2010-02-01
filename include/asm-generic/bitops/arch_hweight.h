#ifndef _ASM_GENERIC_BITOPS_ARCH_HWEIGHT_H_
#define _ASM_GENERIC_BITOPS_ARCH_HWEIGHT_H_

#include <asm/types.h>

extern unsigned int __arch_hweight32(unsigned int w);
extern unsigned int __arch_hweight16(unsigned int w);
extern unsigned int __arch_hweight8(unsigned int w);
extern unsigned long __arch_hweight64(__u64 w);

#endif /* _ASM_GENERIC_BITOPS_HWEIGHT_H_ */
