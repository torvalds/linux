/*
 * include/asm-arm/arch-ixp2000/platform.h
 *
 * Various bits of code used by platform-level code.
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2004 (c) MontaVista Software, Inc. 
 * 
 * This file is licensed under  the terms of the GNU General Public 
 * License version 2. This program is licensed "as is" without any 
 * warranty of any kind, whether express or implied.
 */


#ifndef __ASSEMBLY__

/*
 * The IXP2400 B0 silicon contains an erratum (#66) that causes writes
 * to on-chip I/O register to not complete fully. What this means is
 * that if you have a write to on-chip I/O followed by a back-to-back
 * read or write, the first write will happen twice. OR...if it's
 * not a back-to-back transaction, the read or write will generate
 * incorrect data.
 *
 * The official work around for this is to set the on-chip I/O regions
 * as XCB=101 and then force a read-back from the register.
 *
 */
#if defined(CONFIG_ARCH_ENP2611) || defined(CONFIG_ARCH_IXDP2400) || defined(CONFIG_ARCH_IXDP2401)

#include <asm/system.h>		/* Pickup local_irq_ functions */

static inline void ixp2000_reg_write(volatile void *reg, unsigned long val)
{
	unsigned long dummy;
	unsigned long flags;

	local_irq_save(flags);
	*((volatile unsigned long *)reg) = val;
	barrier();
	dummy = *((volatile unsigned long *)reg);
	local_irq_restore(flags);
}
#else
static inline void ixp2000_reg_write(volatile void *reg, unsigned long val)
{
	*((volatile unsigned long *)reg) = val;
}
#endif	/* IXDP2400 || IXDP2401 */
#define ixp2000_reg_read(reg)	(*((volatile unsigned long *)reg))

/*
 * Boards may multiplex different devices on the 2nd channel of 
 * the slowport interface that each need different configuration 
 * settings.  For example, the IXDP2400 uses channel 2 on the interface 
 * to access the CPLD, the switch fabric card, and the media card.  Each
 * one needs a different mode so drivers must save/restore the mode 
 * before and after each operation.  
 *
 * acquire_slowport(&your_config);
 * ...
 * do slowport operations
 * ...
 * release_slowport();
 *
 * Note that while you have the slowport, you are holding a spinlock,
 * so your code should be written as if you explicitly acquired a lock.
 *
 * The configuration only affects device 2 on the slowport, so the
 * MTD map driver does not acquire/release the slowport.  
 */
struct slowport_cfg {
	unsigned long CCR;	/* Clock divide */
	unsigned long WTC;	/* Write Timing Control */
	unsigned long RTC;	/* Read Timing Control */
	unsigned long PCR;	/* Protocol Control Register */
	unsigned long ADC;	/* Address/Data Width Control */
};


void ixp2000_acquire_slowport(struct slowport_cfg *, struct slowport_cfg *);
void ixp2000_release_slowport(struct slowport_cfg *);

/*
 * IXP2400 A0/A1 and  IXP2800 A0/A1/A2 have broken slowport that requires
 * tweaking of addresses in the MTD driver.
 */
static inline unsigned ixp2000_has_broken_slowport(void)
{
	unsigned long id = *IXP2000_PRODUCT_ID;
	unsigned long id_prod = id & (IXP2000_MAJ_PROD_TYPE_MASK |
				      IXP2000_MIN_PROD_TYPE_MASK);
	return (((id_prod ==
		  /* fixed in IXP2400-B0 */
		  (IXP2000_MAJ_PROD_TYPE_IXP2000 |
		   IXP2000_MIN_PROD_TYPE_IXP2400)) &&
		 ((id & IXP2000_MAJ_REV_MASK) == 0)) ||
		((id_prod ==
		  /* fixed in IXP2800-B0 */
		  (IXP2000_MAJ_PROD_TYPE_IXP2000 |
		   IXP2000_MIN_PROD_TYPE_IXP2800)) &&
		 ((id & IXP2000_MAJ_REV_MASK) == 0)) ||
		((id_prod ==
		  /* fixed in IXP2850-B0 */
		  (IXP2000_MAJ_PROD_TYPE_IXP2000 |
		   IXP2000_MIN_PROD_TYPE_IXP2850)) &&
		 ((id & IXP2000_MAJ_REV_MASK) == 0)));
}

static inline unsigned int ixp2000_has_flash(void)
{
	return ((*IXP2000_STRAP_OPTIONS) & (CFG_BOOT_PROM));
}

static inline unsigned int ixp2000_is_pcimaster(void)
{
	return ((*IXP2000_STRAP_OPTIONS) & (CFG_PCI_BOOT_HOST));
}

void ixp2000_map_io(void);
void ixp2000_uart_init(void);
void ixp2000_init_irq(void);
void ixp2000_init_time(unsigned long);
unsigned long ixp2000_gettimeoffset(void);

struct pci_sys_data;

u32 *ixp2000_pci_config_addr(unsigned int bus, unsigned int devfn, int where);
void ixp2000_pci_preinit(void);
int ixp2000_pci_setup(int, struct pci_sys_data*);
struct pci_bus* ixp2000_pci_scan_bus(int, struct pci_sys_data*);
int ixp2000_pci_read_config(struct pci_bus*, unsigned int, int, int, u32 *);
int ixp2000_pci_write_config(struct pci_bus*, unsigned int, int, int, u32);

/*
 * Several of the IXP2000 systems have banked flash so we need to extend the
 * flash_platform_data structure with some private pointers
 */
struct ixp2000_flash_data {
	struct flash_platform_data *platform_data;
	int nr_banks;
	unsigned long (*bank_setup)(unsigned long);
};

struct ixp2000_i2c_pins {
	unsigned long sda_pin;
	unsigned long scl_pin;
};


#endif /*  !__ASSEMBLY__ */
