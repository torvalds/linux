/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Simple pci display device.
 *
 * Framebuffer memory is pci bar 0.
 * Configuration (read-only) is in pci config space.
 * Format field uses drm fourcc codes.
 * ATM only DRM_FORMAT_XRGB8888 is supported.
 */

/* pci ids */
#define MDPY_PCI_VENDOR_ID	0x1b36 /* redhat */
#define MDPY_PCI_DEVICE_ID	0x000f
#define MDPY_PCI_SUBVENDOR_ID	PCI_SUBVENDOR_ID_REDHAT_QUMRANET
#define MDPY_PCI_SUBDEVICE_ID	PCI_SUBDEVICE_ID_QEMU

/* pci cfg space offsets for fb config (dword) */
#define MDPY_VENDORCAP_OFFSET   0x40
#define MDPY_VENDORCAP_SIZE     0x10
#define MDPY_FORMAT_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x04)
#define MDPY_WIDTH_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x08)
#define MDPY_HEIGHT_OFFSET	(MDPY_VENDORCAP_OFFSET + 0x0c)
