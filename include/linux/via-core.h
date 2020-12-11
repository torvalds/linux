/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 1998-2009 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.
 * Copyright 2009-2010 Jonathan Corbet <corbet@lwn.net>
 * Copyright 2010 Florian Tobias Schandinat <FlorianSchandinat@gmx.de>
 */

#ifndef __VIA_CORE_H__
#define __VIA_CORE_H__
#include <linux/types.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/pci.h>

/*
 * A description of each known serial I2C/GPIO port.
 */
enum via_port_type {
	VIA_PORT_NONE = 0,
	VIA_PORT_I2C,
	VIA_PORT_GPIO,
};

enum via_port_mode {
	VIA_MODE_OFF = 0,
	VIA_MODE_I2C,		/* Used as I2C port */
	VIA_MODE_GPIO,	/* Two GPIO ports */
};

enum viafb_i2c_adap {
	VIA_PORT_26 = 0,
	VIA_PORT_31,
	VIA_PORT_25,
	VIA_PORT_2C,
	VIA_PORT_3D,
};
#define VIAFB_NUM_PORTS 5

struct via_port_cfg {
	enum via_port_type	type;
	enum via_port_mode	mode;
	u16			io_port;
	u8			ioport_index;
};

/*
 * Allow subdevs to register suspend/resume hooks.
 */
struct viafb_pm_hooks {
	struct list_head list;
	int (*suspend)(void *private);
	int (*resume)(void *private);
	void *private;
};

void viafb_pm_register(struct viafb_pm_hooks *hooks);
void viafb_pm_unregister(struct viafb_pm_hooks *hooks);

/*
 * This is the global viafb "device" containing stuff needed by
 * all subdevs.
 */
struct viafb_dev {
	struct pci_dev *pdev;
	int chip_type;
	struct via_port_cfg *port_cfg;
	/*
	 * Spinlock for access to device registers.  Not yet
	 * globally used.
	 */
	spinlock_t reg_lock;
	/*
	 * The framebuffer MMIO region.  Little, if anything, touches
	 * this memory directly, and certainly nothing outside of the
	 * framebuffer device itself.  We *do* have to be able to allocate
	 * chunks of this memory for other devices, though.
	 */
	unsigned long fbmem_start;
	long fbmem_len;
	void __iomem *fbmem;
#if defined(CONFIG_VIDEO_VIA_CAMERA) || defined(CONFIG_VIDEO_VIA_CAMERA_MODULE)
	long camera_fbmem_offset;
	long camera_fbmem_size;
#endif
	/*
	 * The MMIO region for device registers.
	 */
	unsigned long engine_start;
	unsigned long engine_len;
	void __iomem *engine_mmio;

};

/*
 * Interrupt management.
 */

void viafb_irq_enable(u32 mask);
void viafb_irq_disable(u32 mask);

/*
 * The global interrupt control register and its bits.
 */
#define VDE_INTERRUPT	0x200	/* Video interrupt flags/masks */
#define   VDE_I_DVISENSE  0x00000001  /* DVI sense int status */
#define   VDE_I_VBLANK    0x00000002  /* Vertical blank status */
#define   VDE_I_MCCFI	  0x00000004  /* MCE compl. frame int status */
#define   VDE_I_VSYNC	  0x00000008  /* VGA VSYNC int status */
#define   VDE_I_DMA0DDONE 0x00000010  /* DMA 0 descr done */
#define   VDE_I_DMA0TDONE 0x00000020  /* DMA 0 transfer done */
#define   VDE_I_DMA1DDONE 0x00000040  /* DMA 1 descr done */
#define   VDE_I_DMA1TDONE 0x00000080  /* DMA 1 transfer done */
#define   VDE_I_C1AV      0x00000100  /* Cap Eng 1 act vid end */
#define   VDE_I_HQV0	  0x00000200  /* First HQV engine */
#define   VDE_I_HQV1      0x00000400  /* Second HQV engine */
#define   VDE_I_HQV1EN	  0x00000800  /* Second HQV engine enable */
#define   VDE_I_C0AV      0x00001000  /* Cap Eng 0 act vid end */
#define   VDE_I_C0VBI     0x00002000  /* Cap Eng 0 VBI end */
#define   VDE_I_C1VBI     0x00004000  /* Cap Eng 1 VBI end */
#define   VDE_I_VSYNC2    0x00008000  /* Sec. Disp. VSYNC */
#define   VDE_I_DVISNSEN  0x00010000  /* DVI sense enable */
#define   VDE_I_VSYNC2EN  0x00020000  /* Sec Disp VSYNC enable */
#define   VDE_I_MCCFIEN	  0x00040000  /* MC comp frame int mask enable */
#define   VDE_I_VSYNCEN   0x00080000  /* VSYNC enable */
#define   VDE_I_DMA0DDEN  0x00100000  /* DMA 0 descr done enable */
#define   VDE_I_DMA0TDEN  0x00200000  /* DMA 0 trans done enable */
#define   VDE_I_DMA1DDEN  0x00400000  /* DMA 1 descr done enable */
#define   VDE_I_DMA1TDEN  0x00800000  /* DMA 1 trans done enable */
#define   VDE_I_C1AVEN    0x01000000  /* cap 1 act vid end enable */
#define   VDE_I_HQV0EN	  0x02000000  /* First hqv engine enable */
#define   VDE_I_C1VBIEN	  0x04000000  /* Cap 1 VBI end enable */
#define   VDE_I_LVDSSI    0x08000000  /* LVDS sense interrupt */
#define   VDE_I_C0AVEN    0x10000000  /* Cap 0 act vid end enable */
#define   VDE_I_C0VBIEN   0x20000000  /* Cap 0 VBI end enable */
#define   VDE_I_LVDSSIEN  0x40000000  /* LVDS Sense enable */
#define   VDE_I_ENABLE	  0x80000000  /* Global interrupt enable */

#if defined(CONFIG_VIDEO_VIA_CAMERA) || defined(CONFIG_VIDEO_VIA_CAMERA_MODULE)
/*
 * DMA management.
 */
int viafb_request_dma(void);
void viafb_release_dma(void);
/* void viafb_dma_copy_out(unsigned int offset, dma_addr_t paddr, int len); */
int viafb_dma_copy_out_sg(unsigned int offset, struct scatterlist *sg, int nsg);

/*
 * DMA Controller registers.
 */
#define VDMA_MR0	0xe00		/* Mod reg 0 */
#define   VDMA_MR_CHAIN   0x01		/* Chaining mode */
#define   VDMA_MR_TDIE    0x02		/* Transfer done int enable */
#define VDMA_CSR0	0xe04		/* Control/status */
#define	  VDMA_C_ENABLE	  0x01		  /* DMA Enable */
#define	  VDMA_C_START	  0x02		  /* Start a transfer */
#define	  VDMA_C_ABORT	  0x04		  /* Abort a transfer */
#define	  VDMA_C_DONE	  0x08		  /* Transfer is done */
#define VDMA_MARL0	0xe20		/* Mem addr low */
#define VDMA_MARH0	0xe24		/* Mem addr high */
#define VDMA_DAR0	0xe28		/* Device address */
#define VDMA_DQWCR0	0xe2c		/* Count (16-byte) */
#define VDMA_TMR0	0xe30		/* Tile mode reg */
#define VDMA_DPRL0	0xe34		/* Not sure */
#define	  VDMA_DPR_IN	  0x08		/* Inbound transfer to FB */
#define VDMA_DPRH0	0xe38
#define VDMA_PMR0	(0xe00 + 0x134) /* Pitch mode */

/*
 * Useful stuff that probably belongs somewhere global.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
#endif /* CONFIG_VIDEO_VIA_CAMERA */

/*
 * Indexed port operations.  Note that these are all multi-op
 * functions; every invocation will be racy if you're not holding
 * reg_lock.
 */

#define VIAStatus   0x3DA  /* Non-indexed port */
#define VIACR       0x3D4
#define VIASR       0x3C4
#define VIAGR       0x3CE
#define VIAAR       0x3C0

static inline u8 via_read_reg(u16 port, u8 index)
{
	outb(index, port);
	return inb(port + 1);
}

static inline void via_write_reg(u16 port, u8 index, u8 data)
{
	outb(index, port);
	outb(data, port + 1);
}

static inline void via_write_reg_mask(u16 port, u8 index, u8 data, u8 mask)
{
	u8 old;

	outb(index, port);
	old = inb(port + 1);
	outb((data & mask) | (old & ~mask), port + 1);
}

#define VIA_MISC_REG_READ	0x03CC
#define VIA_MISC_REG_WRITE	0x03C2

static inline void via_write_misc_reg_mask(u8 data, u8 mask)
{
	u8 old = inb(VIA_MISC_REG_READ);
	outb((data & mask) | (old & ~mask), VIA_MISC_REG_WRITE);
}


#endif /* __VIA_CORE_H__ */
