/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GHES_H
#define GHES_H

#include <acpi/apei.h>
#include <acpi/hed.h>

/*
 * One struct ghes is created for each generic hardware error source.
 * It provides the context for APEI hardware error timer/IRQ/SCI/NMI
 * handler.
 *
 * estatus: memory buffer for error status block, allocated during
 * HEST parsing.
 */
#define GHES_TO_CLEAR		0x0001
#define GHES_EXITING		0x0002

struct ghes {
	union {
		struct acpi_hest_generic *generic;
		struct acpi_hest_generic_v2 *generic_v2;
	};
	struct acpi_hest_generic_status *estatus;
	u64 buffer_paddr;
	unsigned long flags;
	union {
		struct list_head list;
		struct timer_list timer;
		unsigned int irq;
	};
};

struct ghes_estatus_node {
	struct llist_node llnode;
	struct acpi_hest_generic *generic;
	struct ghes *ghes;
};

struct ghes_estatus_cache {
	u32 estatus_len;
	atomic_t count;
	struct acpi_hest_generic *generic;
	unsigned long long time_in;
	struct rcu_head rcu;
};

enum {
	GHES_SEV_NO = 0x0,
	GHES_SEV_CORRECTED = 0x1,
	GHES_SEV_RECOVERABLE = 0x2,
	GHES_SEV_PANIC = 0x3,
};

/* From drivers/edac/ghes_edac.c */

#ifdef CONFIG_EDAC_GHES
void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
				struct cper_sec_mem_err *mem_err);

int ghes_edac_register(struct ghes *ghes, struct device *dev);

void ghes_edac_unregister(struct ghes *ghes);

#else
static inline void ghes_edac_report_mem_error(struct ghes *ghes, int sev,
				       struct cper_sec_mem_err *mem_err)
{
}

static inline int ghes_edac_register(struct ghes *ghes, struct device *dev)
{
	return 0;
}

static inline void ghes_edac_unregister(struct ghes *ghes)
{
}
#endif

static inline int acpi_hest_get_version(struct acpi_hest_generic_data *gdata)
{
	return gdata->revision >> 8;
}

static inline void *acpi_hest_get_payload(struct acpi_hest_generic_data *gdata)
{
	if (acpi_hest_get_version(gdata) >= 3)
		return (void *)(((struct acpi_hest_generic_data_v300 *)(gdata)) + 1);

	return gdata + 1;
}

static inline int acpi_hest_get_error_length(struct acpi_hest_generic_data *gdata)
{
	return ((struct acpi_hest_generic_data *)(gdata))->error_data_length;
}

static inline int acpi_hest_get_size(struct acpi_hest_generic_data *gdata)
{
	if (acpi_hest_get_version(gdata) >= 3)
		return sizeof(struct acpi_hest_generic_data_v300);

	return sizeof(struct acpi_hest_generic_data);
}

static inline int acpi_hest_get_record_size(struct acpi_hest_generic_data *gdata)
{
	return (acpi_hest_get_size(gdata) + acpi_hest_get_error_length(gdata));
}

static inline void *acpi_hest_get_next(struct acpi_hest_generic_data *gdata)
{
	return (void *)(gdata) + acpi_hest_get_record_size(gdata);
}

#define apei_estatus_for_each_section(estatus, section)			\
	for (section = (struct acpi_hest_generic_data *)(estatus + 1);	\
	     (void *)section - (void *)(estatus + 1) < estatus->data_length; \
	     section = acpi_hest_get_next(section))

int ghes_notify_sea(void);

#endif /* GHES_H */
