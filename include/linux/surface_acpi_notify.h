/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Interface for Surface ACPI Notify (SAN) driver.
 *
 * Provides access to discrete GPU notifications sent from ACPI via the SAN
 * driver, which are not handled by this driver directly.
 *
 * Copyright (C) 2019-2020 Maximilian Luz <luzmaximilian@gmail.com>
 */

#ifndef _LINUX_SURFACE_ACPI_NOTIFY_H
#define _LINUX_SURFACE_ACPI_NOTIFY_H

#include <linux/notifier.h>
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
int san_dgpu_notifier_register(struct notifier_block *nb);
int san_dgpu_notifier_unregister(struct notifier_block *nb);

#endif /* _LINUX_SURFACE_ACPI_NOTIFY_H */
