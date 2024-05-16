/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_PCI_ADMIN_H
#define _LINUX_VIRTIO_PCI_ADMIN_H

#include <linux/types.h>
#include <linux/pci.h>

#ifdef CONFIG_VIRTIO_PCI_ADMIN_LEGACY
bool virtio_pci_admin_has_legacy_io(struct pci_dev *pdev);
int virtio_pci_admin_legacy_common_io_write(struct pci_dev *pdev, u8 offset,
					    u8 size, u8 *buf);
int virtio_pci_admin_legacy_common_io_read(struct pci_dev *pdev, u8 offset,
					   u8 size, u8 *buf);
int virtio_pci_admin_legacy_device_io_write(struct pci_dev *pdev, u8 offset,
					    u8 size, u8 *buf);
int virtio_pci_admin_legacy_device_io_read(struct pci_dev *pdev, u8 offset,
					   u8 size, u8 *buf);
int virtio_pci_admin_legacy_io_notify_info(struct pci_dev *pdev,
					   u8 req_bar_flags, u8 *bar,
					   u64 *bar_offset);
#endif

#endif /* _LINUX_VIRTIO_PCI_ADMIN_H */
