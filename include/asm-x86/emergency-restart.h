#ifndef _ASM_EMERGENCY_RESTART_H
#define _ASM_EMERGENCY_RESTART_H

enum reboot_type {
	BOOT_TRIPLE = 't',
	BOOT_KBD = 'k',
	BOOT_ACPI = 'a',
	BOOT_EFI = 'e'
};

extern enum reboot_type reboot_type;

extern void machine_emergency_restart(void);

#endif /* _ASM_EMERGENCY_RESTART_H */
