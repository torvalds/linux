/*
 * apei.h - ACPI Platform Error Interface
 */

#ifndef ACPI_APEI_H
#define ACPI_APEI_H

#include <linux/acpi.h>
#include <linux/cper.h>
#include <asm/ioctls.h>

#define APEI_ERST_INVALID_RECORD_ID	0xffffffffffffffffULL

#define APEI_ERST_CLEAR_RECORD		_IOW('E', 1, u64)
#define APEI_ERST_GET_RECORD_COUNT	_IOR('E', 2, u32)

#ifdef __KERNEL__

extern int hest_disable;
extern int erst_disable;

typedef int (*apei_hest_func_t)(struct acpi_hest_header *hest_hdr, void *data);
int apei_hest_parse(apei_hest_func_t func, void *data);

int erst_write(const struct cper_record_header *record);
ssize_t erst_get_record_count(void);
int erst_get_next_record_id(u64 *record_id);
ssize_t erst_read(u64 record_id, struct cper_record_header *record,
		  size_t buflen);
ssize_t erst_read_next(struct cper_record_header *record, size_t buflen);
int erst_clear(u64 record_id);

#endif
#endif
