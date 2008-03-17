#ifndef _MACH_MPPARSE_H
#define _MACH_MPPARSE_H 1

int mps_oem_check(struct mp_config_table *mpc, char *oem, char *productid); 
int acpi_madt_oem_check(char *oem_id, char *oem_table_id); 

#endif
