/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2025 Arm Ltd. */

#ifndef __LINUX_ARM_MPAM_H
#define __LINUX_ARM_MPAM_H

#include <linux/acpi.h>
#include <linux/types.h>

struct mpam_msc;

enum mpam_msc_iface {
	MPAM_IFACE_MMIO,	/* a real MPAM MSC */
	MPAM_IFACE_PCC,		/* a fake MPAM MSC */
};

enum mpam_class_types {
	MPAM_CLASS_CACHE,	/* Caches, e.g. L2, L3 */
	MPAM_CLASS_MEMORY,	/* Main memory */
	MPAM_CLASS_UNKNOWN,	/* Everything else, e.g. SMMU */
};

#define MPAM_CLASS_ID_DEFAULT	255

#ifdef CONFIG_ACPI_MPAM
int acpi_mpam_parse_resources(struct mpam_msc *msc,
			      struct acpi_mpam_msc_node *tbl_msc);

int acpi_mpam_count_msc(void);
#else
static inline int acpi_mpam_parse_resources(struct mpam_msc *msc,
					    struct acpi_mpam_msc_node *tbl_msc)
{
	return -EINVAL;
}

static inline int acpi_mpam_count_msc(void) { return -EINVAL; }
#endif

#ifdef CONFIG_ARM64_MPAM_DRIVER
int mpam_ris_create(struct mpam_msc *msc, u8 ris_idx,
		    enum mpam_class_types type, u8 class_id, int component_id);
#else
static inline int mpam_ris_create(struct mpam_msc *msc, u8 ris_idx,
				  enum mpam_class_types type, u8 class_id,
				  int component_id)
{
	return -EINVAL;
}
#endif

#endif /* __LINUX_ARM_MPAM_H */
