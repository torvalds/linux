/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * Copyright(c) 2021 Intel Corporation
 */

#ifndef __SOUND_SOC_SOF_PCI_H
#define __SOUND_SOC_SOF_PCI_H

extern const struct dev_pm_ops sof_pci_pm;
int sof_pci_probe(struct pci_dev *pci, const struct pci_device_id *pci_id);
void sof_pci_remove(struct pci_dev *pci);
void sof_pci_shutdown(struct pci_dev *pci);

#endif
