/*
 * eeh.h
 * Copyright (C) 2001  Dave Engebretsen & Todd Inglett IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _PPC64_EEH_H
#define _PPC64_EEH_H

#include <linux/config.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/string.h>

struct pci_dev;
struct device_node;

#ifdef CONFIG_EEH

/* Values for eeh_mode bits in device_node */
#define EEH_MODE_SUPPORTED	(1<<0)
#define EEH_MODE_NOCHECK	(1<<1)
#define EEH_MODE_ISOLATED	(1<<2)

/* Max number of EEH freezes allowed before we consider the device
 * to be permanently disabled. */
#define EEH_MAX_ALLOWED_FREEZES 5

void __init eeh_init(void);
unsigned long eeh_check_failure(const volatile void __iomem *token,
				unsigned long val);
int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev);
void __init pci_addr_cache_build(void);

/**
 * eeh_add_device_early
 * eeh_add_device_late
 *
 * Perform eeh initialization for devices added after boot.
 * Call eeh_add_device_early before doing any i/o to the
 * device (including config space i/o).  Call eeh_add_device_late
 * to finish the eeh setup for this device.
 */
void eeh_add_device_early(struct device_node *);
void eeh_add_device_late(struct pci_dev *);

/**
 * eeh_remove_device - undo EEH setup for the indicated pci device
 * @dev: pci device to be removed
 *
 * This routine should be called when a device is removed from
 * a running system (e.g. by hotplug or dlpar).  It unregisters
 * the PCI device from the EEH subsystem.  I/O errors affecting
 * this device will no longer be detected after this call; thus,
 * i/o errors affecting this slot may leave this device unusable.
 */
void eeh_remove_device(struct pci_dev *);

/**
 * EEH_POSSIBLE_ERROR() -- test for possible MMIO failure.
 *
 * If this macro yields TRUE, the caller relays to eeh_check_failure()
 * which does further tests out of line.
 */
#define EEH_POSSIBLE_ERROR(val, type)	((val) == (type)~0)

/*
 * Reads from a device which has been isolated by EEH will return
 * all 1s.  This macro gives an all-1s value of the given size (in
 * bytes: 1, 2, or 4) for comparing with the result of a read.
 */
#define EEH_IO_ERROR_VALUE(size)	(~0U >> ((4 - (size)) * 8))

#else /* !CONFIG_EEH */
static inline void eeh_init(void) { }

static inline unsigned long eeh_check_failure(const volatile void __iomem *token, unsigned long val)
{
	return val;
}

static inline int eeh_dn_check_failure(struct device_node *dn, struct pci_dev *dev)
{
	return 0;
}

static inline void pci_addr_cache_build(void) { }

static inline void eeh_add_device_early(struct device_node *dn) { }

static inline void eeh_add_device_late(struct pci_dev *dev) { }

static inline void eeh_remove_device(struct pci_dev *dev) { }

#define EEH_POSSIBLE_ERROR(val, type) (0)
#define EEH_IO_ERROR_VALUE(size) (-1UL)
#endif /* CONFIG_EEH */

/*
 * MMIO read/write operations with EEH support.
 */
static inline u8 eeh_readb(const volatile void __iomem *addr)
{
	u8 val = in_8(addr);
	if (EEH_POSSIBLE_ERROR(val, u8))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writeb(u8 val, volatile void __iomem *addr)
{
	out_8(addr, val);
}

static inline u16 eeh_readw(const volatile void __iomem *addr)
{
	u16 val = in_le16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writew(u16 val, volatile void __iomem *addr)
{
	out_le16(addr, val);
}
static inline u16 eeh_raw_readw(const volatile void __iomem *addr)
{
	u16 val = in_be16(addr);
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writew(u16 val, volatile void __iomem *addr) {
	volatile u16 __iomem *vaddr = (volatile u16 __iomem *) addr;
	out_be16(vaddr, val);
}

static inline u32 eeh_readl(const volatile void __iomem *addr)
{
	u32 val = in_le32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writel(u32 val, volatile void __iomem *addr)
{
	out_le32(addr, val);
}
static inline u32 eeh_raw_readl(const volatile void __iomem *addr)
{
	u32 val = in_be32(addr);
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writel(u32 val, volatile void __iomem *addr)
{
	out_be32(addr, val);
}

static inline u64 eeh_readq(const volatile void __iomem *addr)
{
	u64 val = in_le64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_writeq(u64 val, volatile void __iomem *addr)
{
	out_le64(addr, val);
}
static inline u64 eeh_raw_readq(const volatile void __iomem *addr)
{
	u64 val = in_be64(addr);
	if (EEH_POSSIBLE_ERROR(val, u64))
		return eeh_check_failure(addr, val);
	return val;
}
static inline void eeh_raw_writeq(u64 val, volatile void __iomem *addr)
{
	out_be64(addr, val);
}

#define EEH_CHECK_ALIGN(v,a) \
	((((unsigned long)(v)) & ((a) - 1)) == 0)

static inline void eeh_memset_io(volatile void __iomem *addr, int c,
				 unsigned long n)
{
	void *p = (void __force *)addr;
	u32 lc = c;
	lc |= lc << 8;
	lc |= lc << 16;

	while(n && !EEH_CHECK_ALIGN(p, 4)) {
		*((volatile u8 *)p) = c;
		p++;
		n--;
	}
	while(n >= 4) {
		*((volatile u32 *)p) = lc;
		p += 4;
		n -= 4;
	}
	while(n) {
		*((volatile u8 *)p) = c;
		p++;
		n--;
	}
	__asm__ __volatile__ ("sync" : : : "memory");
}
static inline void eeh_memcpy_fromio(void *dest, const volatile void __iomem *src,
				     unsigned long n)
{
	void *vsrc = (void __force *) src;
	void *destsave = dest;
	unsigned long nsave = n;

	while(n && (!EEH_CHECK_ALIGN(vsrc, 4) || !EEH_CHECK_ALIGN(dest, 4))) {
		*((u8 *)dest) = *((volatile u8 *)vsrc);
		__asm__ __volatile__ ("eieio" : : : "memory");
		vsrc++;
		dest++;
		n--;
	}
	while(n > 4) {
		*((u32 *)dest) = *((volatile u32 *)vsrc);
		__asm__ __volatile__ ("eieio" : : : "memory");
		vsrc += 4;
		dest += 4;
		n -= 4;
	}
	while(n) {
		*((u8 *)dest) = *((volatile u8 *)vsrc);
		__asm__ __volatile__ ("eieio" : : : "memory");
		vsrc++;
		dest++;
		n--;
	}
	__asm__ __volatile__ ("sync" : : : "memory");

	/* Look for ffff's here at dest[n].  Assume that at least 4 bytes
	 * were copied. Check all four bytes.
	 */
	if ((nsave >= 4) &&
		(EEH_POSSIBLE_ERROR((*((u32 *) destsave+nsave-4)), u32))) {
		eeh_check_failure(src, (*((u32 *) destsave+nsave-4)));
	}
}

static inline void eeh_memcpy_toio(volatile void __iomem *dest, const void *src,
				   unsigned long n)
{
	void *vdest = (void __force *) dest;

	while(n && (!EEH_CHECK_ALIGN(vdest, 4) || !EEH_CHECK_ALIGN(src, 4))) {
		*((volatile u8 *)vdest) = *((u8 *)src);
		src++;
		vdest++;
		n--;
	}
	while(n > 4) {
		*((volatile u32 *)vdest) = *((volatile u32 *)src);
		src += 4;
		vdest += 4;
		n-=4;
	}
	while(n) {
		*((volatile u8 *)vdest) = *((u8 *)src);
		src++;
		vdest++;
		n--;
	}
	__asm__ __volatile__ ("sync" : : : "memory");
}

#undef EEH_CHECK_ALIGN

static inline u8 eeh_inb(unsigned long port)
{
	u8 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_8((u8 __iomem *)(port+pci_io_base));
	if (EEH_POSSIBLE_ERROR(val, u8))
		return eeh_check_failure((void __iomem *)(port), val);
	return val;
}

static inline void eeh_outb(u8 val, unsigned long port)
{
	if (_IO_IS_VALID(port))
		out_8((u8 __iomem *)(port+pci_io_base), val);
}

static inline u16 eeh_inw(unsigned long port)
{
	u16 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_le16((u16 __iomem *)(port+pci_io_base));
	if (EEH_POSSIBLE_ERROR(val, u16))
		return eeh_check_failure((void __iomem *)(port), val);
	return val;
}

static inline void eeh_outw(u16 val, unsigned long port)
{
	if (_IO_IS_VALID(port))
		out_le16((u16 __iomem *)(port+pci_io_base), val);
}

static inline u32 eeh_inl(unsigned long port)
{
	u32 val;
	if (!_IO_IS_VALID(port))
		return ~0;
	val = in_le32((u32 __iomem *)(port+pci_io_base));
	if (EEH_POSSIBLE_ERROR(val, u32))
		return eeh_check_failure((void __iomem *)(port), val);
	return val;
}

static inline void eeh_outl(u32 val, unsigned long port)
{
	if (_IO_IS_VALID(port))
		out_le32((u32 __iomem *)(port+pci_io_base), val);
}

/* in-string eeh macros */
static inline void eeh_insb(unsigned long port, void * buf, int ns)
{
	_insb((u8 __iomem *)(port+pci_io_base), buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u8*)buf)+ns-1)), u8))
		eeh_check_failure((void __iomem *)(port), *(u8*)buf);
}

static inline void eeh_insw_ns(unsigned long port, void * buf, int ns)
{
	_insw_ns((u16 __iomem *)(port+pci_io_base), buf, ns);
	if (EEH_POSSIBLE_ERROR((*(((u16*)buf)+ns-1)), u16))
		eeh_check_failure((void __iomem *)(port), *(u16*)buf);
}

static inline void eeh_insl_ns(unsigned long port, void * buf, int nl)
{
	_insl_ns((u32 __iomem *)(port+pci_io_base), buf, nl);
	if (EEH_POSSIBLE_ERROR((*(((u32*)buf)+nl-1)), u32))
		eeh_check_failure((void __iomem *)(port), *(u32*)buf);
}

#endif /* _PPC64_EEH_H */
