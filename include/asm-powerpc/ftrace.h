#ifndef _ASM_POWERPC_FTRACE
#define _ASM_POWERPC_FTRACE

#ifdef CONFIG_FTRACE
#define MCOUNT_ADDR		((long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(void);
#endif

#endif

#endif /* _ASM_POWERPC_FTRACE */
