/*
 * include/asm-ppc/dma.h: Defines for using and allocating dma channels.
 * Written by Hennus Bergman, 1992.
 * High DMA channel support & info by Hannu Savolainen
 * and John Boyd, Nov. 1992.
 * Changes for ppc sound by Christoph Nadig
 */

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/io.h>
#include <linux/spinlock.h>
#include <asm/system.h>

/*
 * Note: Adapted for PowerPC by Gary Thomas
 * Modified by Cort Dougan <cort@cs.nmt.edu>
 *
 * None of this really applies for Power Macintoshes.  There is
 * basically just enough here to get kernel/dma.c to compile.
 *
 * There may be some comments or restrictions made here which are
 * not valid for the PReP platform.  Take what you read
 * with a grain of salt.
 */

#ifndef _ASM_DMA_H
#define _ASM_DMA_H

#ifndef MAX_DMA_CHANNELS
#define MAX_DMA_CHANNELS	8
#endif

/* The maximum address that we can perform a DMA transfer to on this platform */
/* Doesn't really apply... */
#define MAX_DMA_ADDRESS		0xFFFFFFFF

/* in arch/ppc/kernel/setup.c -- Cort */
extern unsigned long DMA_MODE_WRITE, DMA_MODE_READ;
extern unsigned long ISA_DMA_THRESHOLD;

#ifdef HAVE_REALLY_SLOW_DMA_CONTROLLER
#define dma_outb	outb_p
#else
#define dma_outb	outb
#endif

#define dma_inb		inb

/*
 * NOTES about DMA transfers:
 *
 *  controller 1: channels 0-3, byte operations, ports 00-1F
 *  controller 2: channels 4-7, word operations, ports C0-DF
 *
 *  - ALL registers are 8 bits only, regardless of transfer size
 *  - channel 4 is not used - cascades 1 into 2.
 *  - channels 0-3 are byte - addresses/counts are for physical bytes
 *  - channels 5-7 are word - addresses/counts are for physical words
 *  - transfers must not cross physical 64K (0-3) or 128K (5-7) boundaries
 *  - transfer count loaded to registers is 1 less than actual count
 *  - controller 2 offsets are all even (2x offsets for controller 1)
 *  - page registers for 5-7 don't use data bit 0, represent 128K pages
 *  - page registers for 0-3 use bit 0, represent 64K pages
 *
 * On PReP, DMA transfers are limited to the lower 16MB of _physical_ memory.
 * On CHRP, the W83C553F (and VLSI Tollgate?) support full 32 bit addressing.
 * Note that addresses loaded into registers must be _physical_ addresses,
 * not logical addresses (which may differ if paging is active).
 *
 *  Address mapping for channels 0-3:
 *
 *   A23 ... A16 A15 ... A8  A7 ... A0    (Physical addresses)
 *    |  ...  |   |  ... |   |  ... |
 *    |  ...  |   |  ... |   |  ... |
 *    |  ...  |   |  ... |   |  ... |
 *   P7  ...  P0  A7 ... A0  A7 ... A0
 * |    Page    | Addr MSB | Addr LSB |   (DMA registers)
 *
 *  Address mapping for channels 5-7:
 *
 *   A23 ... A17 A16 A15 ... A9 A8 A7 ... A1 A0    (Physical addresses)
 *    |  ...  |   \   \   ... \  \  \  ... \  \
 *    |  ...  |    \   \   ... \  \  \  ... \  (not used)
 *    |  ...  |     \   \   ... \  \  \  ... \
 *   P7  ...  P1 (0) A7 A6  ... A0 A7 A6 ... A0
 * |      Page      |  Addr MSB   |  Addr LSB  |   (DMA registers)
 *
 * Again, channels 5-7 transfer _physical_ words (16 bits), so addresses
 * and counts _must_ be word-aligned (the lowest address bit is _ignored_ at
 * the hardware level, so odd-byte transfers aren't possible).
 *
 * Transfer count (_not # bytes_) is limited to 64K, represented as actual
 * count - 1 : 64K => 0xFFFF, 1 => 0x0000.  Thus, count is always 1 or more,
 * and up to 128K bytes may be transferred on channels 5-7 in one operation.
 *
 */

/* see prep_setup_arch() for detailed informations */
#if defined(CONFIG_SOUND_CS4232) && defined(CONFIG_PPC_PREP)
extern long ppc_cs4232_dma, ppc_cs4232_dma2;
#define SND_DMA1 ppc_cs4232_dma
#define SND_DMA2 ppc_cs4232_dma2
#else
#define SND_DMA1 -1
#define SND_DMA2 -1
#endif

/* 8237 DMA controllers */
#define IO_DMA1_BASE	0x00	/* 8 bit slave DMA, channels 0..3 */
#define IO_DMA2_BASE	0xC0	/* 16 bit master DMA, ch 4(=slave input)..7 */

/* DMA controller registers */
#define DMA1_CMD_REG		0x08	/* command register (w) */
#define DMA1_STAT_REG		0x08	/* status register (r) */
#define DMA1_REQ_REG		0x09	/* request register (w) */
#define DMA1_MASK_REG		0x0A	/* single-channel mask (w) */
#define DMA1_MODE_REG		0x0B	/* mode register (w) */
#define DMA1_CLEAR_FF_REG	0x0C	/* clear pointer flip-flop (w) */
#define DMA1_TEMP_REG		0x0D	/* Temporary Register (r) */
#define DMA1_RESET_REG		0x0D	/* Master Clear (w) */
#define DMA1_CLR_MASK_REG	0x0E	/* Clear Mask */
#define DMA1_MASK_ALL_REG	0x0F	/* all-channels mask (w) */

#define DMA2_CMD_REG		0xD0	/* command register (w) */
#define DMA2_STAT_REG		0xD0	/* status register (r) */
#define DMA2_REQ_REG		0xD2	/* request register (w) */
#define DMA2_MASK_REG		0xD4	/* single-channel mask (w) */
#define DMA2_MODE_REG		0xD6	/* mode register (w) */
#define DMA2_CLEAR_FF_REG	0xD8	/* clear pointer flip-flop (w) */
#define DMA2_TEMP_REG		0xDA	/* Temporary Register (r) */
#define DMA2_RESET_REG		0xDA	/* Master Clear (w) */
#define DMA2_CLR_MASK_REG	0xDC	/* Clear Mask */
#define DMA2_MASK_ALL_REG	0xDE	/* all-channels mask (w) */

#define DMA_ADDR_0		0x00	/* DMA address registers */
#define DMA_ADDR_1		0x02
#define DMA_ADDR_2		0x04
#define DMA_ADDR_3		0x06
#define DMA_ADDR_4		0xC0
#define DMA_ADDR_5		0xC4
#define DMA_ADDR_6		0xC8
#define DMA_ADDR_7		0xCC

#define DMA_CNT_0		0x01	/* DMA count registers */
#define DMA_CNT_1		0x03
#define DMA_CNT_2		0x05
#define DMA_CNT_3		0x07
#define DMA_CNT_4		0xC2
#define DMA_CNT_5		0xC6
#define DMA_CNT_6		0xCA
#define DMA_CNT_7		0xCE

#define DMA_LO_PAGE_0		0x87	/* DMA page registers */
#define DMA_LO_PAGE_1		0x83
#define DMA_LO_PAGE_2		0x81
#define DMA_LO_PAGE_3		0x82
#define DMA_LO_PAGE_5		0x8B
#define DMA_LO_PAGE_6		0x89
#define DMA_LO_PAGE_7		0x8A

#define DMA_HI_PAGE_0		0x487	/* DMA page registers */
#define DMA_HI_PAGE_1		0x483
#define DMA_HI_PAGE_2		0x481
#define DMA_HI_PAGE_3		0x482
#define DMA_HI_PAGE_5		0x48B
#define DMA_HI_PAGE_6		0x489
#define DMA_HI_PAGE_7		0x48A

#define DMA1_EXT_REG		0x40B
#define DMA2_EXT_REG		0x4D6

#define DMA_MODE_CASCADE	0xC0	/* pass thru DREQ->HRQ, DACK<-HLDA only */
#define DMA_AUTOINIT		0x10

extern spinlock_t dma_spin_lock;

static __inline__ unsigned long claim_dma_lock(void)
{
	unsigned long flags;
	spin_lock_irqsave(&dma_spin_lock, flags);
	return flags;
}

static __inline__ void release_dma_lock(unsigned long flags)
{
	spin_unlock_irqrestore(&dma_spin_lock, flags);
}

/* enable/disable a specific DMA channel */
static __inline__ void enable_dma(unsigned int dmanr)
{
	unsigned char ucDmaCmd = 0x00;

	if (dmanr != 4) {
		dma_outb(0, DMA2_MASK_REG);	/* This may not be enabled */
		dma_outb(ucDmaCmd, DMA2_CMD_REG);	/* Enable group */
	}
	if (dmanr <= 3) {
		dma_outb(dmanr, DMA1_MASK_REG);
		dma_outb(ucDmaCmd, DMA1_CMD_REG);	/* Enable group */
	} else
		dma_outb(dmanr & 3, DMA2_MASK_REG);
}

static __inline__ void disable_dma(unsigned int dmanr)
{
	if (dmanr <= 3)
		dma_outb(dmanr | 4, DMA1_MASK_REG);
	else
		dma_outb((dmanr & 3) | 4, DMA2_MASK_REG);
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while interrupts are disabled! ---
 */
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
	if (dmanr <= 3)
		dma_outb(0, DMA1_CLEAR_FF_REG);
	else
		dma_outb(0, DMA2_CLEAR_FF_REG);
}

/* set mode (above) for a specific DMA channel */
static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{
	if (dmanr <= 3)
		dma_outb(mode | dmanr, DMA1_MODE_REG);
	else
		dma_outb(mode | (dmanr & 3), DMA2_MODE_REG);
}

/* Set only the page register bits of the transfer address.
 * This is used for successive transfers when we know the contents of
 * the lower 16 bits of the DMA current address register, but a 64k boundary
 * may have been crossed.
 */
static __inline__ void set_dma_page(unsigned int dmanr, int pagenr)
{
	switch (dmanr) {
	case 0:
		dma_outb(pagenr, DMA_LO_PAGE_0);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_0);
		break;
	case 1:
		dma_outb(pagenr, DMA_LO_PAGE_1);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_1);
		break;
	case 2:
		dma_outb(pagenr, DMA_LO_PAGE_2);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_2);
		break;
	case 3:
		dma_outb(pagenr, DMA_LO_PAGE_3);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_3);
		break;
	case 5:
		if (SND_DMA1 == 5 || SND_DMA2 == 5)
			dma_outb(pagenr, DMA_LO_PAGE_5);
		else
			dma_outb(pagenr & 0xfe, DMA_LO_PAGE_5);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_5);
		break;
	case 6:
		if (SND_DMA1 == 6 || SND_DMA2 == 6)
			dma_outb(pagenr, DMA_LO_PAGE_6);
		else
			dma_outb(pagenr & 0xfe, DMA_LO_PAGE_6);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_6);
		break;
	case 7:
		if (SND_DMA1 == 7 || SND_DMA2 == 7)
			dma_outb(pagenr, DMA_LO_PAGE_7);
		else
			dma_outb(pagenr & 0xfe, DMA_LO_PAGE_7);
		dma_outb(pagenr >> 8, DMA_HI_PAGE_7);
		break;
	}
}

/* Set transfer address & page bits for specific DMA channel.
 * Assumes dma flipflop is clear.
 */
static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int phys)
{
	if (dmanr <= 3) {
		dma_outb(phys & 0xff, ((dmanr & 3) << 1) + IO_DMA1_BASE);
		dma_outb((phys >> 8) & 0xff, ((dmanr & 3) << 1) + IO_DMA1_BASE);
	} else if (dmanr == SND_DMA1 || dmanr == SND_DMA2) {
		dma_outb(phys & 0xff, ((dmanr & 3) << 2) + IO_DMA2_BASE);
		dma_outb((phys >> 8) & 0xff, ((dmanr & 3) << 2) + IO_DMA2_BASE);
		dma_outb((dmanr & 3), DMA2_EXT_REG);
	} else {
		dma_outb((phys >> 1) & 0xff, ((dmanr & 3) << 2) + IO_DMA2_BASE);
		dma_outb((phys >> 9) & 0xff, ((dmanr & 3) << 2) + IO_DMA2_BASE);
	}
	set_dma_page(dmanr, phys >> 16);
}

/* Set transfer size (max 64k for DMA1..3, 128k for DMA5..7) for
 * a specific DMA channel.
 * You must ensure the parameters are valid.
 * NOTE: from a manual: "the number of transfers is one more
 * than the initial word count"! This is taken into account.
 * Assumes dma flip-flop is clear.
 * NOTE 2: "count" represents _bytes_ and must be even for channels 5-7.
 */
static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
	count--;
	if (dmanr <= 3) {
		dma_outb(count & 0xff, ((dmanr & 3) << 1) + 1 + IO_DMA1_BASE);
		dma_outb((count >> 8) & 0xff, ((dmanr & 3) << 1) + 1 +
			 IO_DMA1_BASE);
	} else if (dmanr == SND_DMA1 || dmanr == SND_DMA2) {
		dma_outb(count & 0xff, ((dmanr & 3) << 2) + 2 + IO_DMA2_BASE);
		dma_outb((count >> 8) & 0xff, ((dmanr & 3) << 2) + 2 +
			 IO_DMA2_BASE);
	} else {
		dma_outb((count >> 1) & 0xff, ((dmanr & 3) << 2) + 2 +
			 IO_DMA2_BASE);
		dma_outb((count >> 9) & 0xff, ((dmanr & 3) << 2) + 2 +
			 IO_DMA2_BASE);
	}
}

/* Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * If called before the channel has been used, it may return 1.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 *
 * Assumes DMA flip-flop is clear.
 */
static __inline__ int get_dma_residue(unsigned int dmanr)
{
	unsigned int io_port = (dmanr <= 3) ?
	    ((dmanr & 3) << 1) + 1 + IO_DMA1_BASE
	    : ((dmanr & 3) << 2) + 2 + IO_DMA2_BASE;

	/* using short to get 16-bit wrap around */
	unsigned short count;

	count = 1 + dma_inb(io_port);
	count += dma_inb(io_port) << 8;

	return (dmanr <= 3 || dmanr == SND_DMA1 || dmanr == SND_DMA2)
	    ? count : (count << 1);

}

/* These are in kernel/dma.c: */

/* reserve a DMA channel */
extern int request_dma(unsigned int dmanr, const char *device_id);
/* release it again */
extern void free_dma(unsigned int dmanr);

#ifdef CONFIG_PCI
extern int isa_dma_bridge_buggy;
#else
#define isa_dma_bridge_buggy	(0)
#endif
#endif				/* _ASM_DMA_H */
#endif				/* __KERNEL__ */
