/*
 * Arch specific extensions to struct device
 *
 * This file is released under the GPLv2
 */
#ifndef _ASM_I386_DEVICE_H
#define _ASM_I386_DEVICE_H

struct dev_archdata {
#ifdef CONFIG_ACPI
	void	*acpi_handle;
#endif
};

#endif /* _ASM_I386_DEVICE_H */
