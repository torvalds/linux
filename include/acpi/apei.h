/*
 * apei.h - ACPI Platform Error Interface
 */

#ifndef ACPI_APEI_H
#define ACPI_APEI_H

extern int hest_disable;

typedef int (*apei_hest_func_t)(struct acpi_hest_header *hest_hdr, void *data);
int apei_hest_parse(apei_hest_func_t func, void *data);

#endif
