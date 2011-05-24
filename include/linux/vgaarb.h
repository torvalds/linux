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


/* Legacy VGA regions */
#define VGA_RSRC_NONE	       0x00
#define VGA_RSRC_LEGACY_IO     0x01
#define VGA_RSRC_LEGACY_MEM    0x02
#define VGA_RSRC_LEGACY_MASK   (VGA_RSRC_LEGACY_IO | VGA_RSRC_LEGACY_MEM)
/* Non-legacy access */
#define VGA_RSRC_NORMAL_IO     0x04
#define VGA_RSRC_NORMAL_MEM    0x08

/* Passing that instead of a pci_dev to use the system "default"
 * device, that is the one used by vgacon. Archs will probably
 * have to provide their own vga_default_device();
 */
#define VGA_DEFAULT_DEVICE     (NULL)

/* For use by clients */

/**
 *     vga_set_legacy_decoding
 *
 *     @pdev: pci device of the VGA card
 *     @decodes: bit mask of what legacy regions the card decodes
 *
 *     Indicates to the arbiter if the card decodes legacy VGA IOs,
 *     legacy VGA Memory, both, or none. All cards default to both,
 *     the card driver (fbdev for example) should tell the arbiter
 *     if it has disabled legacy decoding, so the card can be left
 *     out of the arbitration process (and can be safe to take
 *     interrupts at any time.
 */
extern void vga_set_legacy_decoding(struct pci_dev *pdev,
				    unsigned int decodes);

/**
 *     vga_get         - acquire & locks VGA resources
 *
 *     @pdev: pci device of the VGA card or NULL for the system default
 *     @rsrc: bit mask of resources to acquire and lock
 *     @interruptible: blocking should be interruptible by signals ?
 *
 *     This function acquires VGA resources for the given
 *     card and mark those resources locked. If the resource requested
 *     are "normal" (and not legacy) resources, the arbiter will first check
 *     wether the card is doing legacy decoding for that type of resource. If
 *     yes, the lock is "converted" into a legacy resource lock.
 *     The arbiter will first look for all VGA cards that might conflict
 *     and disable their IOs and/or Memory access, including VGA forwarding
 *     on P2P bridges if necessary, so that the requested resources can
 *     be used. Then, the card is marked as locking these resources and
 *     the IO and/or Memory accesse are enabled on the card (including
 *     VGA forwarding on parent P2P bridges if any).
 *     This function will block if some conflicting card is already locking
 *     one of the required resources (or any resource on a different bus
 *     segment, since P2P bridges don't differenciate VGA memory and IO
 *     afaik). You can indicate wether this blocking should be interruptible
 *     by a signal (for userland interface) or not.
 *     Must not be called at interrupt time or in atomic context.
 *     If the card already owns the resources, the function succeeds.
 *     Nested calls are supported (a per-resource counter is maintained)
 */

#if defined(CONFIG_VGA_ARB)
extern int vga_get(struct pci_dev *pdev, unsigned int rsrc, int interruptible);
#else
static inline int vga_get(struct pci_dev *pdev, unsigned int rsrc, int interruptible) { return 0; }
#endif

/**
 *     vga_get_interruptible
 *
 *     Shortcut to vga_get
 */

static inline int vga_get_interruptible(struct pci_dev *pdev,
					unsigned int rsrc)
{
       return vga_get(pdev, rsrc, 1);
}

/**
 *     vga_get_uninterruptible
 *
 *     Shortcut to vga_get
 */

static inline int vga_get_uninterruptible(struct pci_dev *pdev,
					  unsigned int rsrc)
{
       return vga_get(pdev, rsrc, 0);
}

/**
 *     vga_tryget      - try to acquire & lock legacy VGA resources
 *
 *     @pdev: pci devivce of VGA card or NULL for system default
 *     @rsrc: bit mask of resources to acquire and lock
 *
 *     This function performs the same operation as vga_get(), but
 *     will return an error (-EBUSY) instead of blocking if the resources
 *     are already locked by another card. It can be called in any context
 */

#if defined(CONFIG_VGA_ARB)
extern int vga_tryget(struct pci_dev *pdev, unsigned int rsrc);
#else
static inline int vga_tryget(struct pci_dev *pdev, unsigned int rsrc) { return 0; }
#endif

/**
 *     vga_put         - release lock on legacy VGA resources
 *
 *     @pdev: pci device of VGA card or NULL for system default
 *     @rsrc: but mask of resource to release
 *
 *     This function releases resources previously locked by vga_get()
 *     or vga_tryget(). The resources aren't disabled right away, so
 *     that a subsequence vga_get() on the same card will succeed
 *     immediately. Resources have a counter, so locks are only
 *     released if the counter reaches 0.
 */

#if defined(CONFIG_VGA_ARB)
extern void vga_put(struct pci_dev *pdev, unsigned int rsrc);
#else
#define vga_put(pdev, rsrc)
#endif


/**
 *     vga_default_device
 *
 *     This can be defined by the platform. The default implementation
 *     is rather dumb and will probably only work properly on single
 *     vga card setups and/or x86 platforms.
 *
 *     If your VGA default device is not PCI, you'll have to return
 *     NULL here. In this case, I assume it will not conflict with
 *     any PCI card. If this is not true, I'll have to define two archs
 *     hooks for enabling/disabling the VGA default device if that is
 *     possible. This may be a problem with real _ISA_ VGA cards, in
 *     addition to a PCI one. I don't know at this point how to deal
 *     with that card. Can theirs IOs be disabled at all ? If not, then
 *     I suppose it's a matter of having the proper arch hook telling
 *     us about it, so we basically never allow anybody to succeed a
 *     vga_get()...
 */

#ifndef __ARCH_HAS_VGA_DEFAULT_DEVICE
extern struct pci_dev *vga_default_device(void);
#endif

/**
 *     vga_conflicts
 *
 *     Architectures should define this if they have several
 *     independent PCI domains that can afford concurrent VGA
 *     decoding
 */

#ifndef __ARCH_HAS_VGA_CONFLICT
static inline int vga_conflicts(struct pci_dev *p1, struct pci_dev *p2)
{
       return 1;
}
#endif

/**
 *	vga_client_register
 *
 *	@pdev: pci device of the VGA client
 *	@cookie: client cookie to be used in callbacks
 *	@irq_set_state: irq state change callback
 *	@set_vga_decode: vga decode change callback
 *
 * 	return value: 0 on success, -1 on failure
 * 	Register a client with the VGA arbitration logic
 *
 *	Clients have two callback mechanisms they can use.
 *	irq enable/disable callback -
 *		If a client can't disable its GPUs VGA resources, then we
 *		need to be able to ask it to turn off its irqs when we
 *		turn off its mem and io decoding.
 *	set_vga_decode
 *		If a client can disable its GPU VGA resource, it will
 *		get a callback from this to set the encode/decode state
 *
 * Rationale: we cannot disable VGA decode resources unconditionally
 * some single GPU laptops seem to require ACPI or BIOS access to the
 * VGA registers to control things like backlights etc.
 * Hopefully newer multi-GPU laptops do something saner, and desktops
 * won't have any special ACPI for this.
 * They driver will get a callback when VGA arbitration is first used
 * by userspace since we some older X servers have issues.
 */
#if defined(CONFIG_VGA_ARB)
int vga_client_register(struct pci_dev *pdev, void *cookie,
			void (*irq_set_state)(void *cookie, bool state),
			unsigned int (*set_vga_decode)(void *cookie, bool state));
#else
static inline int vga_client_register(struct pci_dev *pdev, void *cookie,
				      void (*irq_set_state)(void *cookie, bool state),
				      unsigned int (*set_vga_decode)(void *cookie, bool state))
{
	return 0;
}
#endif

#endif /* LINUX_VGA_H */
