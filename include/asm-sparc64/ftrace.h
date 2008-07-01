#ifndef _ASM_SPARC64_FTRACE
#define _ASM_SPARC64_FTRACE

#ifdef CONFIG_MCOUNT
#define MCOUNT_ADDR		((long)(_mcount))
#define MCOUNT_INSN_SIZE	4 /* sizeof mcount call */

#ifndef __ASSEMBLY__
extern void _mcount(void);
#endif

#endif

#endif /* _ASM_SPARC64_FTRACE */
