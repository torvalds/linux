/**
 * PCI Endpoint ConfigFS header file
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 of
 * the License as published by the Free Software Foundation.
 */

#ifndef __LINUX_PCI_EP_CFS_H
#define __LINUX_PCI_EP_CFS_H

#include <linux/configfs.h>

#ifdef CONFIG_PCI_ENDPOINT_CONFIGFS
struct config_group *pci_ep_cfs_add_epc_group(const char *name);
void pci_ep_cfs_remove_epc_group(struct config_group *group);
struct config_group *pci_ep_cfs_add_epf_group(const char *name);
void pci_ep_cfs_remove_epf_group(struct config_group *group);
#else
static inline struct config_group *pci_ep_cfs_add_epc_group(const char *name)
{
	return 0;
}

static inline void pci_ep_cfs_remove_epc_group(struct config_group *group)
{
}

static inline struct config_group *pci_ep_cfs_add_epf_group(const char *name)
{
	return 0;
}

static inline void pci_ep_cfs_remove_epf_group(struct config_group *group)
{
}
#endif
#endif /* __LINUX_PCI_EP_CFS_H */
