#ifndef _ASM_PARISC_UNALIGNED_H_
#define _ASM_PARISC_UNALIGNED_H_

#include <asm-generic/unaligned.h>

#ifdef __KERNEL__
struct pt_regs;
void handle_unaligned(struct pt_regs *regs);
int check_unaligned(struct pt_regs *regs);
#endif

#endif /* _ASM_PARISC_UNALIGNED_H_ */
