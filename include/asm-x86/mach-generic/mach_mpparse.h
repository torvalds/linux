#ifndef ASM_X86__MACH_GENERIC__MACH_MPPARSE_H
#define ASM_X86__MACH_GENERIC__MACH_MPPARSE_H


extern int mps_oem_check(struct mp_config_table *mpc, char *oem,
			 char *productid);

extern int acpi_madt_oem_check(char *oem_id, char *oem_table_id);

#endif /* ASM_X86__MACH_GENERIC__MACH_MPPARSE_H */
