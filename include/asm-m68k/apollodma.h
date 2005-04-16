/* $Id: dma.h,v 1.7 1992/12/14 00:29:34 root Exp root $
 * linux/include/asm/dma.h: Defines for using and allocating dma channels.
 * Written by Hennus Bergman, 1992.
 * High DMA channel support & info by Hannu Savolainen
 * and John Boyd, Nov. 1992.
 */

#ifndef _ASM_APOLLO_DMA_H
#define _ASM_APOLLO_DMA_H

#include <asm/apollohw.h>		/* need byte IO */
#include <linux/spinlock.h>		/* And spinlocks */
#include <linux/delay.h>


#define dma_outb(val,addr) (*((volatile unsigned char *)(addr+IO_BASE)) = (val))
#define dma_inb(addr)	   (*((volatile unsigned char *)(addr+IO_BASE)))

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
 * DMA transfers are limited to the lower 16MB of _physical_ memory.
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

#define MAX_DMA_CHANNELS	8

/* The maximum address that we can perform a DMA transfer to on this platform */#define MAX_DMA_ADDRESS      (PAGE_OFFSET+0x1000000)

/* 8237 DMA controllers */
#define IO_DMA1_BASE	0x10C00	/* 8 bit slave DMA, channels 0..3 */
#define IO_DMA2_BASE	0x10D00	/* 16 bit master DMA, ch 4(=slave input)..7 */

/* DMA controller registers */
#define DMA1_CMD_REG		(IO_DMA1_BASE+0x08) /* command register (w) */
#define DMA1_STAT_REG		(IO_DMA1_BASE+0x08) /* status register (r) */
#define DMA1_REQ_REG            (IO_DMA1_BASE+0x09) /* request register (w) */
#define DMA1_MASK_REG		(IO_DMA1_BASE+0x0A) /* single-channel mask (w) */
#define DMA1_MODE_REG		(IO_DMA1_BASE+0x0B) /* mode register (w) */
#define DMA1_CLEAR_FF_REG	(IO_DMA1_BASE+0x0C) /* clear pointer flip-flop (w) */
#define DMA1_TEMP_REG           (IO_DMA1_BASE+0x0D) /* Temporary Register (r) */
#define DMA1_RESET_REG		(IO_DMA1_BASE+0x0D) /* Master Clear (w) */
#define DMA1_CLR_MASK_REG       (IO_DMA1_BASE+0x0E) /* Clear Mask */
#define DMA1_MASK_ALL_REG       (IO_DMA1_BASE+0x0F) /* all-channels mask (w) */

#define DMA2_CMD_REG		(IO_DMA2_BASE+0x10) /* command register (w) */
#define DMA2_STAT_REG		(IO_DMA2_BASE+0x10) /* status register (r) */
#define DMA2_REQ_REG            (IO_DMA2_BASE+0x12) /* request register (w) */
#define DMA2_MASK_REG		(IO_DMA2_BASE+0x14) /* single-channel mask (w) */
#define DMA2_MODE_REG		(IO_DMA2_BASE+0x16) /* mode register (w) */
#define DMA2_CLEAR_FF_REG	(IO_DMA2_BASE+0x18) /* clear pointer flip-flop (w) */
#define DMA2_TEMP_REG           (IO_DMA2_BASE+0x1A) /* Temporary Register (r) */
#define DMA2_RESET_REG		(IO_DMA2_BASE+0x1A) /* Master Clear (w) */
#define DMA2_CLR_MASK_REG       (IO_DMA2_BASE+0x1C) /* Clear Mask */
#define DMA2_MASK_ALL_REG       (IO_DMA2_BASE+0x1E) /* all-channels mask (w) */

#define DMA_ADDR_0              (IO_DMA1_BASE+0x00) /* DMA address registers */
#define DMA_ADDR_1              (IO_DMA1_BASE+0x02)
#define DMA_ADDR_2              (IO_DMA1_BASE+0x04)
#define DMA_ADDR_3              (IO_DMA1_BASE+0x06)
#define DMA_ADDR_4              (IO_DMA2_BASE+0x00)
#define DMA_ADDR_5              (IO_DMA2_BASE+0x04)
#define DMA_ADDR_6              (IO_DMA2_BASE+0x08)
#define DMA_ADDR_7              (IO_DMA2_BASE+0x0C)

#define DMA_CNT_0               (IO_DMA1_BASE+0x01)   /* DMA count registers */
#define DMA_CNT_1               (IO_DMA1_BASE+0x03)
#define DMA_CNT_2               (IO_DMA1_BASE+0x05)
#define DMA_CNT_3               (IO_DMA1_BASE+0x07)
#define DMA_CNT_4               (IO_DMA2_BASE+0x02)
#define DMA_CNT_5               (IO_DMA2_BASE+0x06)
#define DMA_CNT_6               (IO_DMA2_BASE+0x0A)
#define DMA_CNT_7               (IO_DMA2_BASE+0x0E)

#define DMA_MODE_READ	0x44	/* I/O to memory, no autoinit, increment, single mode */
#define DMA_MODE_WRITE	0x48	/* memory to I/O, no autoinit, increment, single mode */
#define DMA_MODE_CASCADE 0xC0   /* pass thru DREQ->HRQ, DACK<-HLDA only */

#define DMA_AUTOINIT	0x10

#define DMA_8BIT 0
#define DMA_16BIT 1
#define DMA_BUSMASTER 2

extern spinlock_t  dma_spin_lock;

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
	if (dmanr<=3)
		dma_outb(dmanr,  DMA1_MASK_REG);
	else
		dma_outb(dmanr & 3,  DMA2_MASK_REG);
}

static __inline__ void disable_dma(unsigned int dmanr)
{
	if (dmanr<=3)
		dma_outb(dmanr | 4,  DMA1_MASK_REG);
	else
		dma_outb((dmanr & 3) | 4,  DMA2_MASK_REG);
}

/* Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while holding the DMA lock ! ---
 */
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
	if (dmanr<=3)
		dma_outb(0,  DMA1_CLEAR_FF_REG);
	else
		dma_outb(0,  DMA2_CLEAR_FF_REG);
}

/* set mode (above) for a specific DMA channel */
static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{
	if (dmanr<=3)
		dma_outb(mode | dmanr,  DMA1_MODE_REG);
	else
		dma_outb(mode | (dmanr&3),  DMA2_MODE_REG);
}

/* Set transfer address & page bits for specific DMA channel.
 * Assumes dma flipflop is clear.
 */
static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int a)
{
	if (dmanr <= 3)  {
	    dma_outb( a & 0xff, ((dmanr&3)<<1) + IO_DMA1_BASE );
            dma_outb( (a>>8) & 0xff, ((dmanr&3)<<1) + IO_DMA1_BASE );
	}  else  {
	    dma_outb( (a>>1) & 0xff, ((dmanr&3)<<2) + IO_DMA2_BASE );
	    dma_outb( (a>>9) & 0xff, ((dmanr&3)<<2) + IO_DMA2_BASE );
	}
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
	if (dmanr <= 3)  {
	    dma_outb( count & 0xff, ((dmanr&3)<<1) + 1 + IO_DMA1_BASE );
	    dma_outb( (count>>8) & 0xff, ((dmanr&3)<<1) + 1 + IO_DMA1_BASE );
        } else {
	    dma_outb( (count>>1) & 0xff, ((dmanr&3)<<2) + 2 + IO_DMA2_BASE );
	    dma_outb( (count>>9) & 0xff, ((dmanr&3)<<2) + 2 + IO_DMA2_BASE );
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
	unsigned int io_port = (dmanr<=3)? ((dmanr&3)<<1) + 1 + IO_DMA1_BASE
					 : ((dmanr&3)<<2) + 2 + IO_DMA2_BASE;

	/* using short to get 16-bit wrap around */
	unsigned short count;

	count = 1 + dma_inb(io_port);
	count += dma_inb(io_port) << 8;

	return (dmanr<=3)? count : (count<<1);
}


/* These are in kernel/dma.c: */
extern int request_dma(unsigned int dmanr, const char * device_id);	/* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	/* release it again */

/* These are in arch/m68k/apollo/dma.c: */
extern unsigned short dma_map_page(unsigned long phys_addr,int count,int type);
extern void dma_unmap_page(unsigned short dma_addr);

#endif /* _ASM_APOLLO_DMA_H */
