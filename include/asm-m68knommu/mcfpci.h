/****************************************************************************/

/*
 *	mcfpci.h -- PCI bridge on ColdFire eval boards.
 *
 *	(C) Copyright 2000, Greg Ungerer (gerg@snapgear.com)
 *	(C) Copyright 2000, Lineo Inc. (www.lineo.com)
 */

/****************************************************************************/
#ifndef	mcfpci_h
#define	mcfpci_h
/****************************************************************************/

#include <linux/config.h>

#ifdef CONFIG_PCI

/*
 *	Address regions in the PCI address space are not mapped into the
 *	normal memory space of the ColdFire. They must be accessed via
 *	handler routines. This is easy for I/O space (inb/outb/etc) but
 *	needs some code changes to support ordinary memory. Interrupts
 *	also need to be vectored through the PCI handler first, then it
 *	will call the actual driver sub-handlers.
 */

/*
 *	Un-define all the standard I/O access routines.
 */
#undef	inb
#undef	inw
#undef	inl
#undef	inb_p
#undef	inw_p
#undef	insb
#undef	insw
#undef	insl
#undef	outb
#undef	outw
#undef	outl
#undef	outb_p
#undef	outw_p
#undef	outsb
#undef	outsw
#undef	outsl

#undef	request_irq
#undef	free_irq

#undef	bus_to_virt
#undef	virt_to_bus


/*
 *	Re-direct all I/O memory accesses functions to PCI specific ones.
 */
#define	inb	pci_inb
#define	inw	pci_inw
#define	inl	pci_inl
#define	inb_p	pci_inb
#define	inw_p	pci_inw
#define	insb	pci_insb
#define	insw	pci_insw
#define	insl	pci_insl

#define	outb	pci_outb
#define	outw	pci_outw
#define	outl	pci_outl
#define	outb_p	pci_outb
#define	outw_p	pci_outw
#define	outsb	pci_outsb
#define	outsw	pci_outsw
#define	outsl	pci_outsl

#define	request_irq	pci_request_irq
#define	free_irq	pci_free_irq

#define	virt_to_bus	pci_virt_to_bus
#define	bus_to_virt	pci_bus_to_virt

#define	CONFIG_COMEMPCI	1


/*
 *	Prototypes of the real PCI functions (defined in bios32.c).
 */
unsigned char	pci_inb(unsigned int addr);
unsigned short	pci_inw(unsigned int addr);
unsigned int	pci_inl(unsigned int addr);
void		pci_insb(void *addr, void *buf, int len);
void		pci_insw(void *addr, void *buf, int len);
void		pci_insl(void *addr, void *buf, int len);

void		pci_outb(unsigned char val, unsigned int addr);
void		pci_outw(unsigned short val, unsigned int addr);
void		pci_outl(unsigned int val, unsigned int addr);
void		pci_outsb(void *addr, void *buf, int len);
void		pci_outsw(void *addr, void *buf, int len);
void		pci_outsl(void *addr, void *buf, int len);

int		pci_request_irq(unsigned int irq,
			void (*handler)(int, void *, struct pt_regs *),
			unsigned long flags,
			const char *device,
			void *dev_id);
void		pci_free_irq(unsigned int irq, void *dev_id);

void		*pci_bmalloc(int size);
void		pci_bmfree(void *bmp, int len);
void		pci_copytoshmem(unsigned long bmp, void *src, int size);
void		pci_copyfromshmem(void *dst, unsigned long bmp, int size);
unsigned long	pci_virt_to_bus(volatile void *address);
void		*pci_bus_to_virt(unsigned long address);
void		pci_bmcpyto(void *dst, void *src, int len);
void		pci_bmcpyfrom(void *dst, void *src, int len);

#endif /* CONFIG_PCI */
/****************************************************************************/
#endif	/* mcfpci_h */
