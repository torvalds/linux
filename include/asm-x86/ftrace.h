#ifndef ASM_X86__FTRACE_H
#define ASM_X86__FTRACE_H

#ifdef CONFIG_FTRACE
#define MCOUNT_ADDR		((long)(mcount))
#define MCOUNT_INSN_SIZE	5 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void mcount(void);
#endif

#endif /* CONFIG_FTRACE */

#endif /* ASM_X86__FTRACE_H */
