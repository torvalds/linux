/*
 * ia64/platform/hp/common/hp_acpi.h
 *
 * Copyright (C) 2003 Hewlett-Packard
 * Copyright (C) Alex Williamson
 * Copyright (C) Bjorn Helgaas
 *
 * Vendor specific extensions to ACPI.
 */
#ifndef _ASM_IA64_ACPI_EXT_H
#define _ASM_IA64_ACPI_EXT_H

#include <linux/types.h>

extern acpi_status hp_acpi_csr_space (acpi_handle, u64 *base, u64 *length);

#endif /* _ASM_IA64_ACPI_EXT_H */
