/*
 * Copyright 2001-2003 Pavel Machek <pavel@suse.cz>
 * Based on code
 * Copyright 2001 Patrick Mochel <mochel@osdl.org>
 */
#include <asm/desc.h>
#include <asm/i387.h>

static inline int
arch_prepare_suspend(void)
{
	return 0;
}

/* Image of the saved processor state. If you touch this, fix acpi_wakeup.S. */
struct saved_context {
  	u16 ds, es, fs, gs, ss;
	unsigned long gs_base, gs_kernel_base, fs_base;
	unsigned long cr0, cr2, cr3, cr4, cr8;
	unsigned long efer;
	u16 gdt_pad;
	u16 gdt_limit;
	unsigned long gdt_base;
	u16 idt_pad;
	u16 idt_limit;
	unsigned long idt_base;
	u16 ldt;
	u16 tss;
	unsigned long tr;
	unsigned long safety;
	unsigned long return_address;
	unsigned long eflags;
} __attribute__((packed));

/* We'll access these from assembly, so we'd better have them outside struct */
extern unsigned long saved_context_eax, saved_context_ebx, saved_context_ecx, saved_context_edx;
extern unsigned long saved_context_esp, saved_context_ebp, saved_context_esi, saved_context_edi;
extern unsigned long saved_context_r08, saved_context_r09, saved_context_r10, saved_context_r11;
extern unsigned long saved_context_r12, saved_context_r13, saved_context_r14, saved_context_r15;
extern unsigned long saved_context_eflags;

#define loaddebug(thread,register) \
	set_debugreg((thread)->debugreg##register, register)

extern void fix_processor_context(void);

extern unsigned long saved_rip;
extern unsigned long saved_rsp;
extern unsigned long saved_rbp;
extern unsigned long saved_rbx;
extern unsigned long saved_rsi;
extern unsigned long saved_rdi;

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
