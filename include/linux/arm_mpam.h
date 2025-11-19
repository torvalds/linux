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

/**
 * mpam_register_requestor() - Register a requestor with the MPAM driver
 * @partid_max:		The maximum PARTID value the requestor can generate.
 * @pmg_max:		The maximum PMG value the requestor can generate.
 *
 * Registers a requestor with the MPAM driver to ensure the chosen system-wide
 * minimum PARTID and PMG values will allow the requestors features to be used.
 *
 * Returns an error if the registration is too late, and a larger PARTID/PMG
 * value has been advertised to user-space. In this case the requestor should
 * not use its MPAM features. Returns 0 on success.
 */
int mpam_register_requestor(u16 partid_max, u8 pmg_max);

#endif /* __LINUX_ARM_MPAM_H */
