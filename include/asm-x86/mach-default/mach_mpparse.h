#ifndef ASM_X86__MACH_DEFAULT__MACH_MPPARSE_H
#define ASM_X86__MACH_DEFAULT__MACH_MPPARSE_H

static inline int mps_oem_check(struct mp_config_table *mpc, char *oem, 
		char *productid)
{
	return 0;
}

/* Hook from generic ACPI tables.c */
static inline int acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	return 0;
}


#endif /* ASM_X86__MACH_DEFAULT__MACH_MPPARSE_H */
