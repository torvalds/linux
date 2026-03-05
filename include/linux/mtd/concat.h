/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * MTD device concatenation layer definitions
 *
 * Copyright © 2002      Robert Kaiser <rkaiser@sysgo.de>
 */

#ifndef MTD_CONCAT_H
#define MTD_CONCAT_H


/*
 * Our storage structure:
 * Subdev points to an array of pointers to struct mtd_info objects
 * which is allocated along with this structure
 *
 */
struct mtd_concat {
	struct mtd_info mtd;
	int num_subdev;
	struct mtd_info *subdev[];
};

struct mtd_info *mtd_concat_create(
    struct mtd_info *subdev[],  /* subdevices to concatenate */
    int num_devs,               /* number of subdevices      */
    const char *name);          /* name for the new device   */

void mtd_concat_destroy(struct mtd_info *mtd);

/**
 * mtd_virt_concat_node_create - Create a component for concatenation
 *
 * Returns a positive number representing the no. of devices found for
 * concatenation, or a negative error code.
 *
 * List all the devices for concatenations found in DT and create a
 * component for concatenation.
 */
int mtd_virt_concat_node_create(void);

/**
 * mtd_virt_concat_add - add mtd_info object to the list of subdevices for concatenation
 * @mtd: pointer to new MTD device info structure
 *
 * Returns true if the mtd_info object is added successfully else returns false.
 *
 * The mtd_info object is added to the list of subdevices for concatenation.
 * It returns true if a match is found, and false if all subdevices have
 * already been added or if the mtd_info object does not match any of the
 * intended MTD devices.
 */
bool mtd_virt_concat_add(struct mtd_info *mtd);

/**
 * mtd_virt_concat_create_join - Create and register the concatenated MTD device
 *
 * Returns 0 on succes, or a negative error code.
 *
 * Creates and registers the concatenated MTD device
 */
int mtd_virt_concat_create_join(void);

/**
 * mtd_virt_concat_destroy - Remove the concat that includes a specific mtd device
 *                           as one of its components.
 * @mtd: pointer to MTD device info structure.
 *
 * Returns 0 on succes, or a negative error code.
 *
 * If the mtd_info object is part of a concatenated device, all other MTD devices
 * within that concat are registered individually. The concatenated device is then
 * removed, along with its concatenation component.
 *
 */
int mtd_virt_concat_destroy(struct mtd_info *mtd);

void mtd_virt_concat_destroy_joins(void);
void mtd_virt_concat_destroy_items(void);

#endif
