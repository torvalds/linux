#ifndef __ASM_LINKAGE_H
#define __ASM_LINKAGE_H

#define asmlinkage CPP_ASMLINKAGE __attribute__((regparm(0)))
#define FASTCALL(x)	x __attribute__((regparm(3)))
#define fastcall	__attribute__((regparm(3)))

#ifdef CONFIG_REGPARM
# define prevent_tail_call(ret) __asm__ ("" : "=r" (ret) : "0" (ret))
#endif

#ifdef CONFIG_X86_ALIGNMENT_16
#define __ALIGN .align 16,0x90
#define __ALIGN_STR ".align 16,0x90"
#endif

#endif
