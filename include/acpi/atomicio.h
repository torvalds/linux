#ifndef ACPI_ATOMIC_IO_H
#define ACPI_ATOMIC_IO_H

int acpi_pre_map_gar(struct acpi_generic_address *reg);
int acpi_post_unmap_gar(struct acpi_generic_address *reg);

int acpi_atomic_read(u64 *val, struct acpi_generic_address *reg);
int acpi_atomic_write(u64 val, struct acpi_generic_address *reg);

#endif
