/*
 * Copyright 2001-2003 Pavel Machek <pavel@suse.cz>
 * Based on code
 * Copyright 2001 Patrick Mochel <mochel@osdl.org>
 */
#ifndef __ASM_X86_64_SUSPEND_H
#define __ASM_X86_64_SUSPEND_H

#include <asm/desc.h>
#include <asm/i387.h>

static inline int
arch_prepare_suspend(void)
{
	return 0;
}

/* Image of the saved processor state. If you touch this, fix acpi/wakeup.S. */
struct saved_context {
	struct pt_regs regs;
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
} __attribute__((packed));

#define loaddebug(thread,register) \
	set_debugreg((thread)->debugreg##register, register)

extern void fix_processor_context(void);

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern char core_restore_code;
extern char restore_registers;

#endif /* __ASM_X86_64_SUSPEND_H */
