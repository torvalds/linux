/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/list.h>
#include <linux/acpi.h>
#include <cxl.h>

struct cxl_mock_ops {
	struct list_head list;
	bool (*is_mock_adev)(struct acpi_device *dev);
	int (*acpi_table_parse_cedt)(enum acpi_cedt_type id,
				     acpi_tbl_entry_handler_arg handler_arg,
				     void *arg);
	bool (*is_mock_bridge)(struct device *dev);
	acpi_status (*acpi_evaluate_integer)(acpi_handle handle,
					     acpi_string pathname,
					     struct acpi_object_list *arguments,
					     unsigned long long *data);
	resource_size_t (*cxl_rcrb_to_component)(struct device *dev,
						 resource_size_t rcrb,
						 enum cxl_rcrb which);
	struct acpi_pci_root *(*acpi_pci_find_root)(acpi_handle handle);
	bool (*is_mock_bus)(struct pci_bus *bus);
	bool (*is_mock_port)(struct device *dev);
	bool (*is_mock_dev)(struct device *dev);
	int (*devm_cxl_port_enumerate_dports)(struct cxl_port *port);
	struct cxl_hdm *(*devm_cxl_setup_hdm)(struct cxl_port *port);
	int (*devm_cxl_add_passthrough_decoder)(struct cxl_port *port);
	int (*devm_cxl_enumerate_decoders)(struct cxl_hdm *hdm);
};

void register_cxl_mock_ops(struct cxl_mock_ops *ops);
void unregister_cxl_mock_ops(struct cxl_mock_ops *ops);
struct cxl_mock_ops *get_cxl_mock_ops(int *index);
void put_cxl_mock_ops(int index);
