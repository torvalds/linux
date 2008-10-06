/*
 * Copyright 2001-2003 Pavel Machek <pavel@suse.cz>
 * Based on code
 * Copyright 2001 Patrick Mochel <mochel@osdl.org>
 */
#ifndef ASM_X86__SUSPEND_64_H
#define ASM_X86__SUSPEND_64_H

#include <asm/desc.h>
#include <asm/i387.h>

static inline int arch_prepare_suspend(void)
{
	return 0;
}

/*
 * Image of the saved processor state, used by the low level ACPI suspend to
 * RAM code and by the low level hibernation code.
 *
 * If you modify it, fix arch/x86/kernel/acpi/wakeup_64.S and make sure that
 * __save/__restore_processor_state(), defined in arch/x86/kernel/suspend_64.c,
 * still work as required.
 */
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

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern char core_restore_code;
extern char restore_registers;

#endif /* ASM_X86__SUSPEND_64_H */
