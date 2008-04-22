#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

extern void mpc_oem_bus_info(struct mpc_config_bus *m, char *name,
			     struct mpc_config_translation *translation);
extern void mpc_oem_pci_bus(struct mpc_config_bus *m,
	struct mpc_config_translation *translation);

/* Hook from generic ACPI tables.c */
static inline void acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
}

#endif /* __ASM_MACH_MPPARSE_H */
