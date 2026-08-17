// SPDX-License-Identifier: GPL-2.0

#include <linux/acpi.h>
#include <acpi/acpi_bus.h>

__rust_helper bool rust_helper_acpi_of_match_device(const struct acpi_device *adev,
						    const struct of_device_id *of_match_table,
						    const struct of_device_id **of_id)
{
	return acpi_of_match_device(adev, of_match_table, of_id);
}

__rust_helper struct acpi_device *rust_helper_to_acpi_device_node(struct fwnode_handle *fwnode)
{
	return to_acpi_device_node(fwnode);
}
