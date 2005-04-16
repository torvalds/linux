/*
 *	PCI defines and function prototypes
 *	Copyright 2003 Dell Inc.
 *        by Matt Domsch <Matt_Domsch@dell.com>
 */

#ifndef LINUX_PCI_DYNIDS_H
#define LINUX_PCI_DYNIDS_H

#include <linux/list.h>
#include <linux/mod_devicetable.h>

struct dynid {
	struct list_head        node;
	struct pci_device_id    id;
};

#endif
