#ifndef ASM_X86__VISWS__LITHIUM_H
#define ASM_X86__VISWS__LITHIUM_H

#include <asm/fixmap.h>

/*
 * Lithium is the SGI Visual Workstation I/O ASIC
 */

#define	LI_PCI_A_PHYS		0xfc000000	/* Enet is dev 3 */
#define	LI_PCI_B_PHYS		0xfd000000	/* PIIX4 is here */

/* see set_fixmap() and asm/fixmap.h */
#define LI_PCIA_VADDR   (fix_to_virt(FIX_LI_PCIA))
#define LI_PCIB_VADDR   (fix_to_virt(FIX_LI_PCIB))

/* Not a standard PCI? (not in linux/pci.h) */
#define	LI_PCI_BUSNUM	0x44			/* lo8: primary, hi8: sub */
#define LI_PCI_INTEN    0x46

/* LI_PCI_INTENT bits */
#define	LI_INTA_0	0x0001
#define	LI_INTA_1	0x0002
#define	LI_INTA_2	0x0004
#define	LI_INTA_3	0x0008
#define	LI_INTA_4	0x0010
#define	LI_INTB		0x0020
#define	LI_INTC		0x0040
#define	LI_INTD		0x0080

/* More special purpose macros... */
static inline void li_pcia_write16(unsigned long reg, unsigned short v)
{
	*((volatile unsigned short *)(LI_PCIA_VADDR+reg))=v;
}

static inline unsigned short li_pcia_read16(unsigned long reg)
{
	 return *((volatile unsigned short *)(LI_PCIA_VADDR+reg));
}

static inline void li_pcib_write16(unsigned long reg, unsigned short v)
{
	*((volatile unsigned short *)(LI_PCIB_VADDR+reg))=v;
}

static inline unsigned short li_pcib_read16(unsigned long reg)
{
	return *((volatile unsigned short *)(LI_PCIB_VADDR+reg));
}

#endif /* ASM_X86__VISWS__LITHIUM_H */

