#ifndef _ASM_ARM_FTRACE
#define _ASM_ARM_FTRACE

#ifdef CONFIG_FTRACE
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif

#endif /* _ASM_ARM_FTRACE */
