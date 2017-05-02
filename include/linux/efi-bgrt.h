#ifndef _LINUX_EFI_BGRT_H
#define _LINUX_EFI_BGRT_H

#include <linux/acpi.h>

#ifdef CONFIG_ACPI_BGRT

void efi_bgrt_init(struct acpi_table_header *table);

/* The BGRT data itself; only valid if bgrt_image != NULL. */
extern size_t bgrt_image_size;
extern struct acpi_table_bgrt bgrt_tab;

#else /* !CONFIG_ACPI_BGRT */

static inline void efi_bgrt_init(struct acpi_table_header *table) {}

#endif /* !CONFIG_ACPI_BGRT */

#endif /* _LINUX_EFI_BGRT_H */
