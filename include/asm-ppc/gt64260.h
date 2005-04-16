/*
 * include/asm-ppc/gt64260.h
 *
 * Prototypes, etc. for the Marvell/Galileo GT64260 host bridge routines.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASMPPC_GT64260_H
#define __ASMPPC_GT64260_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/gt64260_defs.h>


extern u32     gt64260_base;
extern u32     gt64260_irq_base;     /* We handle the next 96 IRQs from here */
extern u32     gt64260_revision;
extern u8      gt64260_pci_exclude_bridge;

#ifndef	TRUE
#define	TRUE	1
#endif

#ifndef	FALSE
#define	FALSE	0
#endif

/* IRQs defined by the 64260 */
#define	GT64260_IRQ_MPSC0		40
#define	GT64260_IRQ_MPSC1		42
#define	GT64260_IRQ_SDMA		36

/*
 * Define a default physical memory map to be set up on the bridge.
 * Also define a struct to pass that info from board-specific routines to
 * GT64260 generic set up routines.  By passing this info in, the board
 * support developer can modify it at will.
 */

/*
 * This is the default memory map:
 *			CPU			PCI
 *			---			---
 * PCI 0 I/O:	0xfa000000-0xfaffffff	0x00000000-0x00ffffff
 * PCI 1 I/O:	0xfb000000-0xfbffffff	0x01000000-0x01ffffff
 * PCI 0 MEM:	0x80000000-0x8fffffff	0x80000000-0x8fffffff
 * PCI 1 MEM:	0x90000000-0x9fffffff	0x90000000-0x9fffffff
 */

/* Default physical memory map for the GT64260 bridge */

/*
 * PCI Bus 0 Definitions
 */
#define GT64260_PCI_0_IO_SIZE		0x01000000U
#define	GT64260_PCI_0_MEM_SIZE		0x10000000U

/* Processor Physical addresses */
#define	GT64260_PCI_0_IO_START_PROC	0xfa000000U
#define	GT64260_PCI_0_IO_END_PROC	(GT64260_PCI_0_IO_START_PROC + \
					 GT64260_PCI_0_IO_SIZE - 1)

/* PCI 0 addresses */
#define	GT64260_PCI_0_IO_START		0x00000000U
#define	GT64260_PCI_0_IO_END		(GT64260_PCI_0_IO_START + \
					 GT64260_PCI_0_IO_SIZE - 1)

/* Processor Physical addresses */
#define	GT64260_PCI_0_MEM_START_PROC	0x80000000U
#define	GT64260_PCI_0_MEM_END_PROC	(GT64260_PCI_0_MEM_START_PROC + \
					 GT64260_PCI_0_MEM_SIZE - 1)

/* PCI 0 addresses */
#define	GT64260_PCI_0_MEM_START		0x80000000U
#define	GT64260_PCI_0_MEM_END		(GT64260_PCI_0_MEM_START + \
					 GT64260_PCI_0_MEM_SIZE - 1)

/*
 * PCI Bus 1 Definitions
 */
#define GT64260_PCI_1_IO_SIZE		0x01000000U
#define	GT64260_PCI_1_MEM_SIZE		0x10000000U

/* PCI 1 addresses */
#define	GT64260_PCI_1_IO_START		0x01000000U
#define	GT64260_PCI_1_IO_END		(GT64260_PCI_1_IO_START + \
					 GT64260_PCI_1_IO_SIZE - 1)

/* Processor Physical addresses */
#define	GT64260_PCI_1_IO_START_PROC	0xfb000000U
#define	GT64260_PCI_1_IO_END_PROC	(GT64260_PCI_1_IO_START_PROC + \
					 GT64260_PCI_1_IO_SIZE - 1)

/* PCI 1 addresses */
#define	GT64260_PCI_1_MEM_START		0x90000000U
#define	GT64260_PCI_1_MEM_END		(GT64260_PCI_1_MEM_START + \
					 GT64260_PCI_1_MEM_SIZE - 1)

/* Processor Physical addresses */
#define	GT64260_PCI_1_MEM_START_PROC	0x90000000U
#define	GT64260_PCI_1_MEM_END_PROC	(GT64260_PCI_1_MEM_START_PROC + \
					 GT64260_PCI_1_MEM_SIZE - 1)

/* Define struct to pass mem-map info into gt64260_common.c code */
typedef struct {
	struct pci_controller	*hose_a;
	struct pci_controller	*hose_b;

	u32	mem_size;

	u32	pci_0_io_start_proc;
	u32	pci_0_io_start_pci;
	u32	pci_0_io_size;
	u32	pci_0_io_swap;

	u32	pci_0_mem_start_proc;
	u32	pci_0_mem_start_pci_hi;
	u32	pci_0_mem_start_pci_lo;
	u32	pci_0_mem_size;
	u32	pci_0_mem_swap;

	u32	pci_1_io_start_proc;
	u32	pci_1_io_start_pci;
	u32	pci_1_io_size;
	u32	pci_1_io_swap;

	u32	pci_1_mem_start_proc;
	u32	pci_1_mem_start_pci_hi;
	u32	pci_1_mem_start_pci_lo;
	u32	pci_1_mem_size;
	u32	pci_1_mem_swap;
} gt64260_bridge_info_t;

#define	GT64260_BRIDGE_INFO_DEFAULT(ip, ms) {				\
	(ip)->mem_size = (ms);						\
									\
	(ip)->pci_0_io_start_proc = GT64260_PCI_0_IO_START_PROC;	\
	(ip)->pci_0_io_start_pci  = GT64260_PCI_0_IO_START;		\
	(ip)->pci_0_io_size	  = GT64260_PCI_0_IO_SIZE;		\
	(ip)->pci_0_io_swap	  = GT64260_CPU_PCI_SWAP_NONE;		\
									\
	(ip)->pci_0_mem_start_proc   = GT64260_PCI_0_MEM_START_PROC;	\
	(ip)->pci_0_mem_start_pci_hi = 0x00000000;			\
	(ip)->pci_0_mem_start_pci_lo = GT64260_PCI_0_MEM_START;		\
	(ip)->pci_0_mem_size	     = GT64260_PCI_0_MEM_SIZE;		\
	(ip)->pci_0_mem_swap	     = GT64260_CPU_PCI_SWAP_NONE;	\
									\
	(ip)->pci_1_io_start_proc = GT64260_PCI_1_IO_START_PROC;	\
	(ip)->pci_1_io_start_pci  = GT64260_PCI_1_IO_START;		\
	(ip)->pci_1_io_size	  = GT64260_PCI_1_IO_SIZE;		\
	(ip)->pci_1_io_swap	  = GT64260_CPU_PCI_SWAP_NONE;		\
									\
	(ip)->pci_1_mem_start_proc   = GT64260_PCI_1_MEM_START_PROC;	\
	(ip)->pci_1_mem_start_pci_hi = 0x00000000;			\
	(ip)->pci_1_mem_start_pci_lo = GT64260_PCI_1_MEM_START;		\
	(ip)->pci_1_mem_size	     = GT64260_PCI_1_MEM_SIZE;		\
	(ip)->pci_1_mem_swap	     = GT64260_CPU_PCI_SWAP_NONE;	\
}

/*
 *****************************************************************************
 *
 *	I/O macros to access the 64260's registers
 *
 *****************************************************************************
 */

extern inline uint32_t gt_read(uint32_t offs){
	return (in_le32((volatile uint *)(gt64260_base + offs)));
}
extern inline void gt_write(uint32_t offs, uint32_t d){
	out_le32((volatile uint *)(gt64260_base + offs), d);
}

#if 0 /* paranoid SMP version */
extern inline void gt_modify(u32 offs, u32 data, u32 mask) \
{
	uint32_t reg;
	spin_lock(&gt64260_lock);
	reg = gt_read(offs) & (~mask); /* zero any bits we care about*/
	reg |= data & mask; /* set bits from the data */
	gt_write(offs, reg);
	spin_unlock(&gt64260_lock);
}
#else
extern inline void gt_modify(uint32_t offs, uint32_t data, uint32_t mask)
{
	uint32_t reg;
	reg = gt_read(offs) & (~(mask)); /* zero any bits we care about*/
	reg |= (data) & (mask); /* set bits from the data */
	gt_write(offs, reg);
}
#endif
#define	gt_set_bits(offs, bits) gt_modify(offs, ~0, bits)

#define	gt_clr_bits(offs, bits) gt_modify(offs, 0, bits)


/*
 *****************************************************************************
 *
 *	Function Prototypes
 *
 *****************************************************************************
 */

int gt64260_find_bridges(u32 phys_base_addr, gt64260_bridge_info_t *info,
	int ((*map_irq)(struct pci_dev *, unsigned char, unsigned char)));
int gt64260_bridge_init(gt64260_bridge_info_t *info);
int gt64260_cpu_scs_set_window(u32 window,
			       u32 base_addr,
			       u32 size);
int gt64260_cpu_cs_set_window(u32 window,
			      u32 base_addr,
			      u32 size);
int gt64260_cpu_boot_set_window(u32 base_addr,
			        u32 size);
int gt64260_cpu_set_pci_io_window(u32 pci_bus,
			          u32 cpu_base_addr,
			          u32 pci_base_addr,
			          u32 size,
			          u32 swap);
int gt64260_cpu_set_pci_mem_window(u32 pci_bus,
			           u32 window,
			           u32 cpu_base_addr,
			           u32 pci_base_addr_hi,
			           u32 pci_base_addr_lo,
			           u32 size,
			           u32 swap_64bit);
int gt64260_cpu_prot_set_window(u32 window,
			        u32 base_addr,
			        u32 size,
			        u32 access_bits);
int gt64260_cpu_snoop_set_window(u32 window,
			         u32 base_addr,
			         u32 size,
			         u32  snoop_type);
void gt64260_cpu_disable_all_windows(void);
int gt64260_pci_bar_enable(u32 pci_bus, u32 enable_bits);
int gt64260_pci_slave_scs_set_window(struct pci_controller *hose,
				     u32 window,
				     u32 pci_base_addr,
				     u32 cpu_base_addr,
				     u32 size);
int gt64260_pci_slave_cs_set_window(struct pci_controller *hose,
				    u32 window,
				    u32 pci_base_addr,
				    u32 cpu_base_addr,
				    u32 size);
int gt64260_pci_slave_boot_set_window(struct pci_controller *hose,
				      u32 pci_base_addr,
				      u32 cpu_base_addr,
				      u32 size);
int gt64260_pci_slave_p2p_mem_set_window(struct pci_controller *hose,
				         u32 window,
				         u32 pci_base_addr,
				         u32 other_bus_base_addr,
				         u32 size);
int gt64260_pci_slave_p2p_io_set_window(struct pci_controller *hose,
				        u32 pci_base_addr,
				        u32 other_bus_base_addr,
				        u32 size);
int gt64260_pci_slave_dac_scs_set_window(struct pci_controller *hose,
				         u32 window,
				         u32 pci_base_addr_hi,
				         u32 pci_base_addr_lo,
				         u32 cpu_base_addr,
				         u32 size);
int gt64260_pci_slave_dac_cs_set_window(struct pci_controller *hose,
				        u32 window,
				        u32 pci_base_addr_hi,
				        u32 pci_base_addr_lo,
				        u32 cpu_base_addr,
				        u32 size);
int gt64260_pci_slave_dac_boot_set_window(struct pci_controller *hose,
				          u32 pci_base_addr_hi,
				          u32 pci_base_addr_lo,
				          u32 cpu_base_addr,
				          u32 size);
int gt64260_pci_slave_dac_p2p_mem_set_window(struct pci_controller *hose,
				             u32 window,
				             u32 pci_base_addr_hi,
				             u32 pci_base_addr_lo,
				             u32 other_bus_base_addr,
				             u32 size);
int gt64260_pci_acc_cntl_set_window(u32 pci_bus,
			            u32 window,
			            u32 base_addr_hi,
			            u32 base_addr_lo,
			            u32 size,
			            u32 features);
int gt64260_pci_snoop_set_window(u32 pci_bus,
			         u32 window,
			         u32 base_addr_hi,
			         u32 base_addr_lo,
			         u32 size,
			         u32 snoop_type);
int gt64260_set_base(u32 new_base);
int gt64260_get_base(u32 *base);
int gt64260_pci_exclude_device(u8 bus, u8 devfn);

void gt64260_init_irq(void);
int gt64260_get_irq(struct pt_regs *regs);

void gt64260_mpsc_progress(char *s, unsigned short hex);

#endif /* __ASMPPC_GT64260_H */
