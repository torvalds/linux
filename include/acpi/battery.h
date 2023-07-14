/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_BATTERY_H
#define __ACPI_BATTERY_H

#include <linux/power_supply.h>

#define ACPI_BATTERY_CLASS "battery"

#define ACPI_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_BATTERY_NOTIFY_INFO	0x81
#define ACPI_BATTERY_NOTIFY_THRESHOLD   0x82

struct acpi_battery_hook {
	const char *name;
	int (*add_battery)(struct power_supply *battery, struct acpi_battery_hook *hook);
	int (*remove_battery)(struct power_supply *battery, struct acpi_battery_hook *hook);
	struct list_head list;
};

void battery_hook_register(struct acpi_battery_hook *hook);
void battery_hook_unregister(struct acpi_battery_hook *hook);

#endif
