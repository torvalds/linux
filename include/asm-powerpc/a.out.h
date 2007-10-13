#ifndef _ASM_POWERPC_A_OUT_H
#define _ASM_POWERPC_A_OUT_H

struct exec
{
	unsigned long a_info;	/* Use macros N_MAGIC, etc for access */
	unsigned a_text;	/* length of text, in bytes */
	unsigned a_data;	/* length of data, in bytes */
	unsigned a_bss;		/* length of uninitialized data area for file, in bytes */
	unsigned a_syms;	/* length of symbol table data in file, in bytes */
	unsigned a_entry;	/* start address */
	unsigned a_trsize;	/* length of relocation info for text, in bytes */
	unsigned a_drsize;	/* length of relocation info for data, in bytes */
};

#define N_TRSIZE(a)	((a).a_trsize)
#define N_DRSIZE(a)	((a).a_drsize)
#define N_SYMSIZE(a)	((a).a_syms)

#ifdef __KERNEL__
#ifdef __powerpc64__

#define STACK_TOP_USER64 TASK_SIZE_USER64
#define STACK_TOP_USER32 TASK_SIZE_USER32

#define STACK_TOP (test_thread_flag(TIF_32BIT) ? \
		   STACK_TOP_USER32 : STACK_TOP_USER64)

#define STACK_TOP_MAX STACK_TOP_USER64

#else /* __powerpc64__ */

#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX	STACK_TOP

#endif /* __powerpc64__ */
#endif /* __KERNEL__ */

#endif /* _ASM_POWERPC_A_OUT_H */
