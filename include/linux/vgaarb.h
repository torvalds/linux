/*
 * The VGA aribiter manages VGA space routing and VGA resource decode to
 * allow multiple VGA devices to be used in a system in a safe way.
 *
 * (C) Copyright 2005 Benjamin Herrenschmidt <benh@kernel.crashing.org>
 * (C) Copyright 2007 Paulo R. Zanoni <przanoni@gmail.com>
 * (C) Copyright 2007, 2009 Tiago Vignatti <vignatti@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS
 * IN THE SOFTWARE.
 *
 */

#ifndef LINUX_VGA_H
#define LINUX_VGA_H

#include <video/vga.h>

struct pci_dev;

/* Legacy VGA regions */
#define VGA_RSRC_NONE	       0x00
#define VGA_RSRC_LEGACY_IO     0x01
#define VGA_RSRC_LEGACY_MEM    0x02
#define VGA_RSRC_LEGACY_MASK   (VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM)
/* Non-legacy access */
#define VGA_RSRC_NORMAL_IO     0x04
#define VGA_RSRC_NORMAL_MEM    0x08

#ifdef CONFIG_VGA_ARB
void vga_set_legacy_decoding(struct pci_dev *pdev, unsigned int decodes);
int vga_get(struct pci_dev *pdev, unsigned int rsrc, int interruptible);
void vga_put(struct pci_dev *pdev, unsigned int rsrc);
struct pci_dev *vga_default_device(void);
void vga_set_default_device(struct pci_dev *pdev);
int vga_remove_vgacon(struct pci_dev *pdev);
int vga_client_register(struct pci_dev *pdev,
		unsigned int (*set_decode)(struct pci_dev *pdev, bool state));
#else /* CONFIG_VGA_ARB */
static inline void vga_set_legacy_decoding(struct pci_dev *pdev,
		unsigned int decodes)
{
};
static inline int vga_get(struct pci_dev *pdev, unsigned int rsrc,
		int interruptible)
{
	return 0;
}
static inline void vga_put(struct pci_dev *pdev, unsigned int rsrc)
{
}
static inline struct pci_dev *vga_default_device(void)
{
	return NULL;
}
static inline void vga_set_default_device(struct pci_dev *pdev)
{
}
static inline int vga_remove_vgacon(struct pci_dev *pdev)
{
	return 0;
}
static inline int vga_client_register(struct pci_dev *pdev,
		unsigned int (*set_decode)(struct pci_dev *pdev, bool state))
{
	return 0;
}
#endif /* CONFIG_VGA_ARB */

/**
 * vga_get_interruptible
 * @pdev: pci device of the VGA card or NULL for the system default
 * @rsrc: bit mask of resources to acquire and lock
 *
 * Shortcut to vga_get with interruptible set to true.
 *
 * On success, release the VGA resource again with vga_put().
 */
static inline int vga_get_interruptible(struct pci_dev *pdev,
					unsigned int rsrc)
{
       return vga_get(pdev, rsrc, 1);
}

/**
 * vga_get_uninterruptible - shortcut to vga_get()
 * @pdev: pci device of the VGA card or NULL for the system default
 * @rsrc: bit mask of resources to acquire and lock
 *
 * Shortcut to vga_get with interruptible set to false.
 *
 * On success, release the VGA resource again with vga_put().
 */
static inline int vga_get_uninterruptible(struct pci_dev *pdev,
					  unsigned int rsrc)
{
       return vga_get(pdev, rsrc, 0);
}

static inline void vga_client_unregister(struct pci_dev *pdev)
{
	vga_client_register(pdev, NULL);
}

#endif /* LINUX_VGA_H */
