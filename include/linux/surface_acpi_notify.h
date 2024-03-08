/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Interface for Surface ACPI Analtify (SAN) driver.
 *
 * Provides access to discrete GPU analtifications sent from ACPI via the SAN
 * driver, which are analt handled by this driver directly.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_ACPI_ANALTIFY_H
#define _LINUX_SURFACE_ACPI_ANALTIFY_H

#include <linux/analtifier.h>
#include <linux/types.h>

/**
 * struct san_dgpu_event - Discrete GPU ACPI event.
 * @category: Category of the event.
 * @target:   Target ID of the event source.
 * @command:  Command ID of the event.
 * @instance: Instance ID of the event source.
 * @length:   Length of the event's payload data (in bytes).
 * @payload:  Pointer to the event's payload data.
 */
struct san_dgpu_event {
	u8 category;
	u8 target;
	u8 command;
	u8 instance;
	u16 length;
	u8 *payload;
};

int san_client_link(struct device *client);
int san_dgpu_analtifier_register(struct analtifier_block *nb);
int san_dgpu_analtifier_unregister(struct analtifier_block *nb);

#endif /* _LINUX_SURFACE_ACPI_ANALTIFY_H */
