#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

#include <linux/acpi.h>

static inline void mpc_oem_bus_info(struct mpc_config_bus *m, char *name, 
				struct mpc_config_translation *translation)
{
	Dprintk("Bus #%d is %s\n", m->mpc_busid, name);
}

static inline void mpc_oem_pci_bus(struct mpc_config_bus *m, 
				struct mpc_config_translation *translation)
{
}

extern int parse_unisys_oem (char *oemptr);
extern int find_unisys_acpi_oem_table(unsigned long *oem_addr);
extern void setup_unisys(void);

static inline int mps_oem_check(struct mp_config_table *mpc, char *oem,
		char *productid)
{
	if (mpc->mpc_oemptr) {
		struct mp_config_oemtable *oem_table = 
			(struct mp_config_oemtable *)mpc->mpc_oemptr;
		if (!strncmp(oem, "UNISYS", 6))
			return parse_unisys_oem((char *)oem_table);
	}
	return 0;
}

static inline int es7000_check_dsdt()
{
	struct acpi_table_header *header = NULL;
	if(!acpi_get_table_header_early(ACPI_DSDT, &header))
		acpi_table_print(header, 0);
	if (!strncmp(header->oem_id, "UNISYS", 6))
		return 1;
	return 0;
}

/* Hook from generic ACPI tables.c */
static inline int acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	unsigned long oem_addr; 
	if (!find_unisys_acpi_oem_table(&oem_addr)) {
		if (es7000_check_dsdt())
			return parse_unisys_oem((char *)oem_addr);
		else {
			setup_unisys();
			return 1;
		}
	}
	return 0;
}


#endif /* __ASM_MACH_MPPARSE_H */
