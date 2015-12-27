#ifndef ACPI_BUTTON_H
#define ACPI_BUTTON_H

#include <linux/notifier.h>

#if IS_ENABLED(CONFIG_ACPI_BUTTON)
extern int acpi_lid_notifier_register(struct notifier_block *nb);
extern int acpi_lid_notifier_unregister(struct notifier_block *nb);
extern int acpi_lid_open(void);
#else
static inline int acpi_lid_notifier_register(struct notifier_block *nb)
{
	return 0;
}
static inline int acpi_lid_notifier_unregister(struct notifier_block *nb)
{
	return 0;
}
static inline int acpi_lid_open(void)
{
	return 1;
}
#endif /* IS_ENABLED(CONFIG_ACPI_BUTTON) */

#endif /* ACPI_BUTTON_H */
