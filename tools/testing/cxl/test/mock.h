/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/list.h>
#include <linux/acpi.h>

struct cxl_mock_ops {
	struct list_head list;
	bool (*is_mock_adev)(struct acpi_device *dev);
	acpi_status (*acpi_get_table)(char *signature, u32 instance,
				      struct acpi_table_header **out_table);
	void (*acpi_put_table)(struct acpi_table_header *table);
	bool (*is_mock_bridge)(struct device *dev);
	acpi_status (*acpi_evaluate_integer)(acpi_handle handle,
					     acpi_string pathname,
					     struct acpi_object_list *arguments,
					     unsigned long long *data);
	struct acpi_pci_root *(*acpi_pci_find_root)(acpi_handle handle);
	struct platform_device *(*mock_port)(struct pci_bus *bus, int index);
	bool (*is_mock_bus)(struct pci_bus *bus);
	bool (*is_mock_port)(struct platform_device *pdev);
	bool (*is_mock_dev)(struct device *dev);
};

void register_cxl_mock_ops(struct cxl_mock_ops *ops);
void unregister_cxl_mock_ops(struct cxl_mock_ops *ops);
struct cxl_mock_ops *get_cxl_mock_ops(int *index);
void put_cxl_mock_ops(int index);
