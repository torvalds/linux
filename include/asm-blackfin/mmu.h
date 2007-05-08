#ifndef __MMU_H
#define __MMU_H

/* Copyright (C) 2002, David McCullough <davidm@snapgear.com> */

struct sram_list_struct {
	struct sram_list_struct *next;
	void *addr;
	size_t length;
};

typedef struct {
	struct vm_list_struct *vmlist;
	unsigned long end_brk;
	unsigned long stack_start;

	/* Points to the location in SDRAM where the L1 stack is normally
	   saved, or NULL if the stack is always in SDRAM.  */
	void *l1_stack_save;

	struct sram_list_struct *sram_list;

#ifdef CONFIG_BINFMT_ELF_FDPIC
	unsigned long	exec_fdpic_loadmap;
	unsigned long	interp_fdpic_loadmap;
#endif

} mm_context_t;

#endif
