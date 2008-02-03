#ifndef _ASM_EMERGENCY_RESTART_H
#define _ASM_EMERGENCY_RESTART_H

enum reboot_type {
	BOOT_TRIPLE = 't',
	BOOT_KBD = 'k',
#ifdef CONFIG_X86_32
	BOOT_BIOS = 'b',
#endif
	BOOT_ACPI = 'a',
	BOOT_EFI = 'e'
};

extern enum reboot_type reboot_type;

extern void machine_emergency_restart(void);

#endif /* _ASM_EMERGENCY_RESTART_H */
