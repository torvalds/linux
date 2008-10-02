#ifndef _ASM_X86_FTRACE
#define _ASM_X86_FTRACE

#ifdef CONFIG_FTRACE
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	5 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif /* CONFIG_FTRACE */

#endif /* _ASM_X86_FTRACE */
