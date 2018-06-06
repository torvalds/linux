/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ACPI_BATTERY_H
#define __ACPI_BATTERY_H

#define ACPI_BATTERY_CLASS "battery"

#define ACPI_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_BATTERY_NOTIFY_INFO	0x81
#define ACPI_BATTERY_NOTIFY_THRESHOLD   0x82

struct acpi_battery_hook {
	const char *name;
	int (*add_battery)(struct power_supply *battery);
	int (*remove_battery)(struct power_supply *battery);
	struct list_head list;
};

void battery_hook_register(struct acpi_battery_hook *hook);
void battery_hook_unregister(struct acpi_battery_hook *hook);

#endif
