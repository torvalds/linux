/*
 * include/asm-ppc/mv64x60.h
 *
 * Prototypes, etc. for the Marvell/Galileo MV64x60 host bridge routines.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2001-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */
#ifndef __ASMPPC_MV64x60_H
#define __ASMPPC_MV64x60_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/config.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <asm/mv64x60_defs.h>

extern u8	mv64x60_pci_exclude_bridge;

extern spinlock_t mv64x60_lock;

/* 32-bit Window table entry defines */
#define	MV64x60_CPU2MEM_0_WIN			0
#define	MV64x60_CPU2MEM_1_WIN			1
#define	MV64x60_CPU2MEM_2_WIN			2
#define	MV64x60_CPU2MEM_3_WIN			3
#define	MV64x60_CPU2DEV_0_WIN			4
#define	MV64x60_CPU2DEV_1_WIN			5
#define	MV64x60_CPU2DEV_2_WIN			6
#define	MV64x60_CPU2DEV_3_WIN			7
#define	MV64x60_CPU2BOOT_WIN			8
#define	MV64x60_CPU2PCI0_IO_WIN			9
#define	MV64x60_CPU2PCI0_MEM_0_WIN		10
#define	MV64x60_CPU2PCI0_MEM_1_WIN		11
#define	MV64x60_CPU2PCI0_MEM_2_WIN		12
#define	MV64x60_CPU2PCI0_MEM_3_WIN		13
#define	MV64x60_CPU2PCI1_IO_WIN			14
#define	MV64x60_CPU2PCI1_MEM_0_WIN		15
#define	MV64x60_CPU2PCI1_MEM_1_WIN		16
#define	MV64x60_CPU2PCI1_MEM_2_WIN		17
#define	MV64x60_CPU2PCI1_MEM_3_WIN		18
#define	MV64x60_CPU2SRAM_WIN			19
#define	MV64x60_CPU2PCI0_IO_REMAP_WIN		20
#define	MV64x60_CPU2PCI1_IO_REMAP_WIN		21
#define	MV64x60_CPU_PROT_0_WIN			22
#define	MV64x60_CPU_PROT_1_WIN			23
#define	MV64x60_CPU_PROT_2_WIN			24
#define	MV64x60_CPU_PROT_3_WIN			25
#define	MV64x60_CPU_SNOOP_0_WIN			26
#define	MV64x60_CPU_SNOOP_1_WIN			27
#define	MV64x60_CPU_SNOOP_2_WIN			28
#define	MV64x60_CPU_SNOOP_3_WIN			29
#define	MV64x60_PCI02MEM_REMAP_0_WIN		30
#define	MV64x60_PCI02MEM_REMAP_1_WIN		31
#define	MV64x60_PCI02MEM_REMAP_2_WIN		32
#define	MV64x60_PCI02MEM_REMAP_3_WIN		33
#define	MV64x60_PCI12MEM_REMAP_0_WIN		34
#define	MV64x60_PCI12MEM_REMAP_1_WIN		35
#define	MV64x60_PCI12MEM_REMAP_2_WIN		36
#define	MV64x60_PCI12MEM_REMAP_3_WIN		37
#define	MV64x60_ENET2MEM_0_WIN			38
#define	MV64x60_ENET2MEM_1_WIN			39
#define	MV64x60_ENET2MEM_2_WIN			40
#define	MV64x60_ENET2MEM_3_WIN			41
#define	MV64x60_ENET2MEM_4_WIN			42
#define	MV64x60_ENET2MEM_5_WIN			43
#define	MV64x60_MPSC2MEM_0_WIN			44
#define	MV64x60_MPSC2MEM_1_WIN			45
#define	MV64x60_MPSC2MEM_2_WIN			46
#define	MV64x60_MPSC2MEM_3_WIN			47
#define	MV64x60_IDMA2MEM_0_WIN			48
#define	MV64x60_IDMA2MEM_1_WIN			49
#define	MV64x60_IDMA2MEM_2_WIN			50
#define	MV64x60_IDMA2MEM_3_WIN			51
#define	MV64x60_IDMA2MEM_4_WIN			52
#define	MV64x60_IDMA2MEM_5_WIN			53
#define	MV64x60_IDMA2MEM_6_WIN			54
#define	MV64x60_IDMA2MEM_7_WIN			55

#define	MV64x60_32BIT_WIN_COUNT			56

/* 64-bit Window table entry defines */
#define	MV64x60_CPU2PCI0_MEM_0_REMAP_WIN	0
#define	MV64x60_CPU2PCI0_MEM_1_REMAP_WIN	1
#define	MV64x60_CPU2PCI0_MEM_2_REMAP_WIN	2
#define	MV64x60_CPU2PCI0_MEM_3_REMAP_WIN	3
#define	MV64x60_CPU2PCI1_MEM_0_REMAP_WIN	4
#define	MV64x60_CPU2PCI1_MEM_1_REMAP_WIN	5
#define	MV64x60_CPU2PCI1_MEM_2_REMAP_WIN	6
#define	MV64x60_CPU2PCI1_MEM_3_REMAP_WIN	7
#define	MV64x60_PCI02MEM_ACC_CNTL_0_WIN		8
#define	MV64x60_PCI02MEM_ACC_CNTL_1_WIN		9
#define	MV64x60_PCI02MEM_ACC_CNTL_2_WIN		10
#define	MV64x60_PCI02MEM_ACC_CNTL_3_WIN		11
#define	MV64x60_PCI12MEM_ACC_CNTL_0_WIN		12
#define	MV64x60_PCI12MEM_ACC_CNTL_1_WIN		13
#define	MV64x60_PCI12MEM_ACC_CNTL_2_WIN		14
#define	MV64x60_PCI12MEM_ACC_CNTL_3_WIN		15
#define	MV64x60_PCI02MEM_SNOOP_0_WIN		16
#define	MV64x60_PCI02MEM_SNOOP_1_WIN		17
#define	MV64x60_PCI02MEM_SNOOP_2_WIN		18
#define	MV64x60_PCI02MEM_SNOOP_3_WIN		19
#define	MV64x60_PCI12MEM_SNOOP_0_WIN		20
#define	MV64x60_PCI12MEM_SNOOP_1_WIN		21
#define	MV64x60_PCI12MEM_SNOOP_2_WIN		22
#define	MV64x60_PCI12MEM_SNOOP_3_WIN		23

#define	MV64x60_64BIT_WIN_COUNT			24

/*
 * Define a structure that's used to pass in config information to the
 * core routines.
 */
struct mv64x60_pci_window {
	u32	cpu_base;
	u32	pci_base_hi;
	u32	pci_base_lo;
	u32	size;
	u32	swap;
};

struct mv64x60_pci_info {
	u8	enable_bus;	/* allow access to this PCI bus? */

	struct mv64x60_pci_window	pci_io;
	struct mv64x60_pci_window	pci_mem[3];

	u32	acc_cntl_options[MV64x60_CPU2MEM_WINDOWS];
	u32	snoop_options[MV64x60_CPU2MEM_WINDOWS];
	u16	pci_cmd_bits;
	u16	latency_timer;
};

struct mv64x60_setup_info {
	u32	phys_reg_base;
	u32	window_preserve_mask_32_hi;
	u32	window_preserve_mask_32_lo;
	u32	window_preserve_mask_64;

	u32	cpu_prot_options[MV64x60_CPU2MEM_WINDOWS];
	u32	cpu_snoop_options[MV64x60_CPU2MEM_WINDOWS];
	u32	enet_options[MV64x60_CPU2MEM_WINDOWS];
	u32	mpsc_options[MV64x60_CPU2MEM_WINDOWS];
	u32	idma_options[MV64x60_CPU2MEM_WINDOWS];

	struct mv64x60_pci_info	pci_0;
	struct mv64x60_pci_info	pci_1;
};

/* Define what the top bits in the extra member of a window entry means. */
#define	MV64x60_EXTRA_INVALID		0x00000000
#define	MV64x60_EXTRA_CPUWIN_ENAB	0x10000000
#define	MV64x60_EXTRA_CPUPROT_ENAB	0x20000000
#define	MV64x60_EXTRA_ENET_ENAB		0x30000000
#define	MV64x60_EXTRA_MPSC_ENAB		0x40000000
#define	MV64x60_EXTRA_IDMA_ENAB		0x50000000
#define	MV64x60_EXTRA_PCIACC_ENAB	0x60000000

#define	MV64x60_EXTRA_MASK		0xf0000000

/*
 * Define the 'handle' struct that will be passed between the 64x60 core
 * code and the platform-specific code that will use it.  The handle
 * will contain pointers to chip-specific routines & information.
 */
struct mv64x60_32bit_window {
	u32	base_reg;
	u32	size_reg;
	u8	base_bits;
	u8	size_bits;
	u32	(*get_from_field)(u32 val, u32 num_bits);
	u32	(*map_to_field)(u32 val, u32 num_bits);
	u32	extra;
};

struct mv64x60_64bit_window {
	u32	base_hi_reg;
	u32	base_lo_reg;
	u32	size_reg;
	u8	base_lo_bits;
	u8	size_bits;
	u32	(*get_from_field)(u32 val, u32 num_bits);
	u32	(*map_to_field)(u32 val, u32 num_bits);
	u32	extra;
};

typedef struct mv64x60_handle	mv64x60_handle_t;
struct mv64x60_chip_info {
	u32	(*translate_size)(u32 base, u32 size, u32 num_bits);
	u32	(*untranslate_size)(u32 base, u32 size, u32 num_bits);
	void	(*set_pci2mem_window)(struct pci_controller *hose, u32 bus,
			u32 window, u32 base);
	void 	(*set_pci2regs_window)(struct mv64x60_handle *bh,
			struct pci_controller *hose, u32 bus, u32 base);
	u32	(*is_enabled_32bit)(mv64x60_handle_t *bh, u32 window);
	void	(*enable_window_32bit)(mv64x60_handle_t *bh, u32 window);
	void	(*disable_window_32bit)(mv64x60_handle_t *bh, u32 window);
	void	(*enable_window_64bit)(mv64x60_handle_t *bh, u32 window);
	void	(*disable_window_64bit)(mv64x60_handle_t *bh, u32 window);
	void	(*disable_all_windows)(mv64x60_handle_t *bh,
			struct mv64x60_setup_info *si);
	void	(*config_io2mem_windows)(mv64x60_handle_t *bh,
			struct mv64x60_setup_info *si,
			u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);
	void 	(*set_mpsc2regs_window)(struct mv64x60_handle *bh, u32 base);
	void	(*chip_specific_init)(mv64x60_handle_t *bh,
			struct mv64x60_setup_info *si);

	struct mv64x60_32bit_window	*window_tab_32bit;
	struct mv64x60_64bit_window	*window_tab_64bit;
};

struct mv64x60_handle {
	u32		type;		/* type of bridge */
	u32		rev;		/* revision of bridge */
	void		*v_base;	/* virtual base addr of bridge regs */
	phys_addr_t	p_base;		/* physical base addr of bridge regs */

	u32		pci_mode_a;	/* pci 0 mode: conventional pci, pci-x*/
	u32		pci_mode_b;	/* pci 1 mode: conventional pci, pci-x*/

	u32		io_base_a;	/* vaddr of pci 0's I/O space */
	u32		io_base_b;	/* vaddr of pci 1's I/O space */

	struct pci_controller	*hose_a;
	struct pci_controller	*hose_b;

	struct mv64x60_chip_info *ci;	/* chip/bridge-specific info */
};


/* Define I/O routines for accessing registers on the 64x60 bridge. */
extern inline void
mv64x60_write(struct mv64x60_handle *bh, u32 offset, u32 val) {
	ulong	flags;

	spin_lock_irqsave(&mv64x60_lock, flags);
	out_le32(bh->v_base + offset, val);
	spin_unlock_irqrestore(&mv64x60_lock, flags);
}

extern inline u32
mv64x60_read(struct mv64x60_handle *bh, u32 offset) {
	ulong	flags;
	u32     reg;

	spin_lock_irqsave(&mv64x60_lock, flags);
	reg = in_le32(bh->v_base + offset);
	spin_unlock_irqrestore(&mv64x60_lock, flags);
	return reg;
}

extern inline void
mv64x60_modify(struct mv64x60_handle *bh, u32 offs, u32 data, u32 mask)
{
	u32	reg;
	ulong	flags;

	spin_lock_irqsave(&mv64x60_lock, flags);
	reg = in_le32(bh->v_base + offs) & (~mask);
	reg |= data & mask;
	out_le32(bh->v_base + offs, reg);
	spin_unlock_irqrestore(&mv64x60_lock, flags);
}

#define	mv64x60_set_bits(bh, offs, bits) mv64x60_modify(bh, offs, ~0, bits)
#define	mv64x60_clr_bits(bh, offs, bits) mv64x60_modify(bh, offs, 0, bits)


/* Externally visible function prototypes */
int mv64x60_init(struct mv64x60_handle *bh, struct mv64x60_setup_info *si);
u32 mv64x60_get_mem_size(u32 bridge_base, u32 chip_type);
void mv64x60_early_init(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si);
void mv64x60_alloc_hose(struct mv64x60_handle *bh, u32 cfg_addr,
	u32 cfg_data, struct pci_controller **hose);
int mv64x60_get_type(struct mv64x60_handle *bh);
int mv64x60_setup_for_chip(struct mv64x60_handle *bh);
void *mv64x60_get_bridge_vbase(void);
u32 mv64x60_get_bridge_type(void);
u32 mv64x60_get_bridge_rev(void);
void mv64x60_get_mem_windows(struct mv64x60_handle *bh,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);
void mv64x60_config_cpu2mem_windows(struct mv64x60_handle *bh,
	struct mv64x60_setup_info *si,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);
void mv64x60_config_cpu2pci_windows(struct mv64x60_handle *bh,
	struct mv64x60_pci_info *pi, u32 bus);
void mv64x60_config_pci2mem_windows(struct mv64x60_handle *bh,
	struct pci_controller *hose, struct mv64x60_pci_info *pi, u32 bus,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);
void mv64x60_config_resources(struct pci_controller *hose,
	struct mv64x60_pci_info *pi, u32 io_base);
void mv64x60_config_pci_params(struct pci_controller *hose,
	struct mv64x60_pci_info *pi);
void mv64x60_pd_fixup(struct mv64x60_handle *bh,
	struct platform_device *pd_devs[], u32 entries);
void mv64x60_get_32bit_window(struct mv64x60_handle *bh, u32 window,
	u32 *base, u32 *size);
void mv64x60_set_32bit_window(struct mv64x60_handle *bh, u32 window, u32 base,
	u32 size, u32 other_bits);
void mv64x60_get_64bit_window(struct mv64x60_handle *bh, u32 window,
	u32 *base_hi, u32 *base_lo, u32 *size);
void mv64x60_set_64bit_window(struct mv64x60_handle *bh, u32 window,
	u32 base_hi, u32 base_lo, u32 size, u32 other_bits);
void mv64x60_set_bus(struct mv64x60_handle *bh, u32 bus, u32 child_bus);
int mv64x60_pci_exclude_device(u8 bus, u8 devfn);


void gt64260_init_irq(void);
int gt64260_get_irq(struct pt_regs *regs);
void mv64360_init_irq(void);
int mv64360_get_irq(struct pt_regs *regs);

u32 mv64x60_mask(u32 val, u32 num_bits);
u32 mv64x60_shift_left(u32 val, u32 num_bits);
u32 mv64x60_shift_right(u32 val, u32 num_bits);
u32 mv64x60_calc_mem_size(struct mv64x60_handle *bh,
	u32 mem_windows[MV64x60_CPU2MEM_WINDOWS][2]);

void mv64x60_progress_init(u32 base);
void mv64x60_mpsc_progress(char *s, unsigned short hex);

extern struct mv64x60_32bit_window
	gt64260_32bit_windows[MV64x60_32BIT_WIN_COUNT];
extern struct mv64x60_64bit_window
	gt64260_64bit_windows[MV64x60_64BIT_WIN_COUNT];
extern struct mv64x60_32bit_window
	mv64360_32bit_windows[MV64x60_32BIT_WIN_COUNT];
extern struct mv64x60_64bit_window
	mv64360_64bit_windows[MV64x60_64BIT_WIN_COUNT];

#endif /* __ASMPPC_MV64x60_H */
