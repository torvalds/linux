/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/uuid.h>

#ifdef CONFIG_ACPI_PRMT
void init_prmt(void);
int acpi_call_prm_handler(guid_t handler_guid, void *param_buffer);
#else
static inline void init_prmt(void) { }
static inline int acpi_call_prm_handler(guid_t handler_guid, void *param_buffer)
{
	return -EOPNOTSUPP;
}
#endif
