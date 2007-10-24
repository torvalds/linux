#ifndef _M68K_DMA_H
#define _M68K_DMA_H 1
 
//#define	DMA_DEBUG	1


#ifdef CONFIG_COLDFIRE
/*
 * ColdFire DMA Model:
 *   ColdFire DMA supports two forms of DMA: Single and Dual address. Single
 * address mode emits a source address, and expects that the device will either
 * pick up the data (DMA READ) or source data (DMA WRITE). This implies that
 * the device will place data on the correct byte(s) of the data bus, as the
 * memory transactions are always 32 bits. This implies that only 32 bit
 * devices will find single mode transfers useful. Dual address DMA mode
 * performs two cycles: source read and destination write. ColdFire will
 * align the data so that the device will always get the correct bytes, thus
 * is useful for 8 and 16 bit devices. This is the mode that is supported
 * below.
 *
 * AUG/22/2000 : added support for 32-bit Dual-Address-Mode (K) 2000 
 *               Oliver Kamphenkel (O.Kamphenkel@tu-bs.de)
 *
 * AUG/25/2000 : addad support for 8, 16 and 32-bit Single-Address-Mode (K)2000
 *               Oliver Kamphenkel (O.Kamphenkel@tu-bs.de)
 *
 * APR/18/2002 : added proper support for MCF5272 DMA controller.
 *               Arthur Shipkowski (art@videon-central.com)
 */

#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfdma.h>

/*
 * Set number of channels of DMA on ColdFire for different implementations.
 */
#if defined(CONFIG_M5249) || defined(CONFIG_M5307) || defined(CONFIG_M5407)
#define MAX_M68K_DMA_CHANNELS 4
#elif defined(CONFIG_M5272)
#define MAX_M68K_DMA_CHANNELS 1
#elif defined(CONFIG_M532x)
#define MAX_M68K_DMA_CHANNELS 0
#else
#define MAX_M68K_DMA_CHANNELS 2
#endif

extern unsigned int dma_base_addr[MAX_M68K_DMA_CHANNELS];
extern unsigned int dma_device_address[MAX_M68K_DMA_CHANNELS];

#if !defined(CONFIG_M5272)
#define DMA_MODE_WRITE_BIT  0x01  /* Memory/IO to IO/Memory select */
#define DMA_MODE_WORD_BIT   0x02  /* 8 or 16 bit transfers */
#define DMA_MODE_LONG_BIT   0x04  /* or 32 bit transfers */
#define DMA_MODE_SINGLE_BIT 0x08  /* single-address-mode */

/* I/O to memory, 8 bits, mode */
#define DMA_MODE_READ	            0
/* memory to I/O, 8 bits, mode */
#define DMA_MODE_WRITE	            1
/* I/O to memory, 16 bits, mode */
#define DMA_MODE_READ_WORD          2
/* memory to I/O, 16 bits, mode */
#define DMA_MODE_WRITE_WORD         3
/* I/O to memory, 32 bits, mode */
#define DMA_MODE_READ_LONG          4
/* memory to I/O, 32 bits, mode */
#define DMA_MODE_WRITE_LONG         5
/* I/O to memory, 8 bits, single-address-mode */     
#define DMA_MODE_READ_SINGLE        8
/* memory to I/O, 8 bits, single-address-mode */
#define DMA_MODE_WRITE_SINGLE       9
/* I/O to memory, 16 bits, single-address-mode */
#define DMA_MODE_READ_WORD_SINGLE  10
/* memory to I/O, 16 bits, single-address-mode */
#define DMA_MODE_WRITE_WORD_SINGLE 11
/* I/O to memory, 32 bits, single-address-mode */
#define DMA_MODE_READ_LONG_SINGLE  12
/* memory to I/O, 32 bits, single-address-mode */
#define DMA_MODE_WRITE_LONG_SINGLE 13

#else /* CONFIG_M5272 is defined */

/* Source static-address mode */
#define DMA_MODE_SRC_SA_BIT 0x01  
/* Two bits to select between all four modes */
#define DMA_MODE_SSIZE_MASK 0x06 
/* Offset to shift bits in */
#define DMA_MODE_SSIZE_OFF  0x01  
/* Destination static-address mode */
#define DMA_MODE_DES_SA_BIT 0x10  
/* Two bits to select between all four modes */
#define DMA_MODE_DSIZE_MASK 0x60  
/* Offset to shift bits in */
#define DMA_MODE_DSIZE_OFF  0x05
/* Size modifiers */
#define DMA_MODE_SIZE_LONG  0x00
#define DMA_MODE_SIZE_BYTE  0x01
#define DMA_MODE_SIZE_WORD  0x02
#define DMA_MODE_SIZE_LINE  0x03

/* 
 * Aliases to help speed quick ports; these may be suboptimal, however. They
 * do not include the SINGLE mode modifiers since the MCF5272 does not have a
 * mode where the device is in control of its addressing.
 */

/* I/O to memory, 8 bits, mode */
#define DMA_MODE_READ	              ((DMA_MODE_SIZE_BYTE << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_BYTE << DMA_MODE_SSIZE_OFF) | DMA_SRC_SA_BIT)
/* memory to I/O, 8 bits, mode */
#define DMA_MODE_WRITE	            ((DMA_MODE_SIZE_BYTE << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_BYTE << DMA_MODE_SSIZE_OFF) | DMA_DES_SA_BIT)
/* I/O to memory, 16 bits, mode */
#define DMA_MODE_READ_WORD	        ((DMA_MODE_SIZE_WORD << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_WORD << DMA_MODE_SSIZE_OFF) | DMA_SRC_SA_BIT)
/* memory to I/O, 16 bits, mode */
#define DMA_MODE_WRITE_WORD         ((DMA_MODE_SIZE_WORD << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_WORD << DMA_MODE_SSIZE_OFF) | DMA_DES_SA_BIT)
/* I/O to memory, 32 bits, mode */
#define DMA_MODE_READ_LONG	        ((DMA_MODE_SIZE_LONG << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_LONG << DMA_MODE_SSIZE_OFF) | DMA_SRC_SA_BIT)
/* memory to I/O, 32 bits, mode */
#define DMA_MODE_WRITE_LONG         ((DMA_MODE_SIZE_LONG << DMA_MODE_DSIZE_OFF) | (DMA_MODE_SIZE_LONG << DMA_MODE_SSIZE_OFF) | DMA_DES_SA_BIT)

#endif /* !defined(CONFIG_M5272) */

#if !defined(CONFIG_M5272)
/* enable/disable a specific DMA channel */
static __inline__ void enable_dma(unsigned int dmanr)
{
  volatile unsigned short *dmawp;

#ifdef DMA_DEBUG
  printk("enable_dma(dmanr=%d)\n", dmanr);
#endif

  dmawp = (unsigned short *) dma_base_addr[dmanr];
  dmawp[MCFDMA_DCR] |= MCFDMA_DCR_EEXT;
}

static __inline__ void disable_dma(unsigned int dmanr)
{
  volatile unsigned short *dmawp;
  volatile unsigned char  *dmapb;

#ifdef DMA_DEBUG
  printk("disable_dma(dmanr=%d)\n", dmanr);
#endif

  dmawp = (unsigned short *) dma_base_addr[dmanr];
  dmapb = (unsigned char *) dma_base_addr[dmanr];

  /* Turn off external requests, and stop any DMA in progress */
  dmawp[MCFDMA_DCR] &= ~MCFDMA_DCR_EEXT;
  dmapb[MCFDMA_DSR] = MCFDMA_DSR_DONE;
}

/*
 * Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while interrupts are disabled! ---
 *
 * This is a NOP for ColdFire. Provide a stub for compatibility.
 */
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
}

/* set mode (above) for a specific DMA channel */
static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{

  volatile unsigned char  *dmabp;
  volatile unsigned short *dmawp;

#ifdef DMA_DEBUG
  printk("set_dma_mode(dmanr=%d,mode=%d)\n", dmanr, mode);
#endif

  dmabp = (unsigned char *) dma_base_addr[dmanr];
  dmawp = (unsigned short *) dma_base_addr[dmanr];

  // Clear config errors
  dmabp[MCFDMA_DSR] = MCFDMA_DSR_DONE; 

  // Set command register
  dmawp[MCFDMA_DCR] =
    MCFDMA_DCR_INT |         // Enable completion irq
    MCFDMA_DCR_CS |          // Force one xfer per request
    MCFDMA_DCR_AA |          // Enable auto alignment
    // single-address-mode
    ((mode & DMA_MODE_SINGLE_BIT) ? MCFDMA_DCR_SAA : 0) |
    // sets s_rw (-> r/w) high if Memory to I/0
    ((mode & DMA_MODE_WRITE_BIT) ? MCFDMA_DCR_S_RW : 0) |
    // Memory to I/O or I/O to Memory
    ((mode & DMA_MODE_WRITE_BIT) ? MCFDMA_DCR_SINC : MCFDMA_DCR_DINC) |
    // 32 bit, 16 bit or 8 bit transfers
    ((mode & DMA_MODE_WORD_BIT)  ? MCFDMA_DCR_SSIZE_WORD : 
     ((mode & DMA_MODE_LONG_BIT) ? MCFDMA_DCR_SSIZE_LONG :
                                   MCFDMA_DCR_SSIZE_BYTE)) |
    ((mode & DMA_MODE_WORD_BIT)  ? MCFDMA_DCR_DSIZE_WORD :
     ((mode & DMA_MODE_LONG_BIT) ? MCFDMA_DCR_DSIZE_LONG :
                                   MCFDMA_DCR_DSIZE_BYTE));

#ifdef DEBUG_DMA
  printk("%s(%d): dmanr=%d DSR[%x]=%x DCR[%x]=%x\n", __FILE__, __LINE__,
         dmanr, (int) &dmabp[MCFDMA_DSR], dmabp[MCFDMA_DSR],
	 (int) &dmawp[MCFDMA_DCR], dmawp[MCFDMA_DCR]);
#endif
}

/* Set transfer address for specific DMA channel */
static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int a)
{
  volatile unsigned short *dmawp;
  volatile unsigned int   *dmalp;

#ifdef DMA_DEBUG
  printk("set_dma_addr(dmanr=%d,a=%x)\n", dmanr, a);
#endif

  dmawp = (unsigned short *) dma_base_addr[dmanr];
  dmalp = (unsigned int *) dma_base_addr[dmanr];

  // Determine which address registers are used for memory/device accesses
  if (dmawp[MCFDMA_DCR] & MCFDMA_DCR_SINC) {
    // Source incrementing, must be memory
    dmalp[MCFDMA_SAR] = a;
    // Set dest address, must be device
    dmalp[MCFDMA_DAR] = dma_device_address[dmanr];
  } else {
    // Destination incrementing, must be memory
    dmalp[MCFDMA_DAR] = a;
    // Set source address, must be device
    dmalp[MCFDMA_SAR] = dma_device_address[dmanr];
  }

#ifdef DEBUG_DMA
  printk("%s(%d): dmanr=%d DCR[%x]=%x SAR[%x]=%08x DAR[%x]=%08x\n",
	__FILE__, __LINE__, dmanr, (int) &dmawp[MCFDMA_DCR], dmawp[MCFDMA_DCR],
	(int) &dmalp[MCFDMA_SAR], dmalp[MCFDMA_SAR],
	(int) &dmalp[MCFDMA_DAR], dmalp[MCFDMA_DAR]);
#endif
}

/*
 * Specific for Coldfire - sets device address.
 * Should be called after the mode set call, and before set DMA address.
 */
static __inline__ void set_dma_device_addr(unsigned int dmanr, unsigned int a)
{
#ifdef DMA_DEBUG
  printk("set_dma_device_addr(dmanr=%d,a=%x)\n", dmanr, a);
#endif

  dma_device_address[dmanr] = a;
}

/*
 * NOTE 2: "count" represents _bytes_.
 */
static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
  volatile unsigned short *dmawp;

#ifdef DMA_DEBUG
  printk("set_dma_count(dmanr=%d,count=%d)\n", dmanr, count);
#endif

  dmawp = (unsigned short *) dma_base_addr[dmanr];
  dmawp[MCFDMA_BCR] = (unsigned short)count;
}

/*
 * Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 */
static __inline__ int get_dma_residue(unsigned int dmanr)
{
  volatile unsigned short *dmawp;
  unsigned short count;

#ifdef DMA_DEBUG
  printk("get_dma_residue(dmanr=%d)\n", dmanr);
#endif

  dmawp = (unsigned short *) dma_base_addr[dmanr];
  count = dmawp[MCFDMA_BCR];
  return((int) count);
}
#else /* CONFIG_M5272 is defined */

/*
 * The MCF5272 DMA controller is very different than the controller defined above
 * in terms of register mapping.  For instance, with the exception of the 16-bit 
 * interrupt register (IRQ#85, for reference), all of the registers are 32-bit.
 *
 * The big difference, however, is the lack of device-requested DMA.  All modes
 * are dual address transfer, and there is no 'device' setup or direction bit.
 * You can DMA between a device and memory, between memory and memory, or even between
 * two devices directly, with any combination of incrementing and non-incrementing
 * addresses you choose.  This puts a crimp in distinguishing between the 'device 
 * address' set up by set_dma_device_addr.
 *
 * Therefore, there are two options.  One is to use set_dma_addr and set_dma_device_addr,
 * which will act exactly as above in -- it will look to see if the source is set to
 * autoincrement, and if so it will make the source use the set_dma_addr value and the
 * destination the set_dma_device_addr value.  Otherwise the source will be set to the
 * set_dma_device_addr value and the destination will get the set_dma_addr value.
 *
 * The other is to use the provided set_dma_src_addr and set_dma_dest_addr functions
 * and make it explicit.  Depending on what you're doing, one of these two should work
 * for you, but don't mix them in the same transfer setup.
 */

/* enable/disable a specific DMA channel */
static __inline__ void enable_dma(unsigned int dmanr)
{
  volatile unsigned int  *dmalp;

#ifdef DMA_DEBUG
  printk("enable_dma(dmanr=%d)\n", dmanr);
#endif

  dmalp = (unsigned int *) dma_base_addr[dmanr];
  dmalp[MCFDMA_DMR] |= MCFDMA_DMR_EN;
}

static __inline__ void disable_dma(unsigned int dmanr)
{
  volatile unsigned int   *dmalp;

#ifdef DMA_DEBUG
  printk("disable_dma(dmanr=%d)\n", dmanr);
#endif

  dmalp = (unsigned int *) dma_base_addr[dmanr];

  /* Turn off external requests, and stop any DMA in progress */
  dmalp[MCFDMA_DMR] &= ~MCFDMA_DMR_EN;
  dmalp[MCFDMA_DMR] |= MCFDMA_DMR_RESET;
}

/*
 * Clear the 'DMA Pointer Flip Flop'.
 * Write 0 for LSB/MSB, 1 for MSB/LSB access.
 * Use this once to initialize the FF to a known state.
 * After that, keep track of it. :-)
 * --- In order to do that, the DMA routines below should ---
 * --- only be used while interrupts are disabled! ---
 *
 * This is a NOP for ColdFire. Provide a stub for compatibility.
 */
static __inline__ void clear_dma_ff(unsigned int dmanr)
{
}

/* set mode (above) for a specific DMA channel */
static __inline__ void set_dma_mode(unsigned int dmanr, char mode)
{

  volatile unsigned int   *dmalp;
  volatile unsigned short *dmawp;

#ifdef DMA_DEBUG
  printk("set_dma_mode(dmanr=%d,mode=%d)\n", dmanr, mode);
#endif
  dmalp = (unsigned int *) dma_base_addr[dmanr];
  dmawp = (unsigned short *) dma_base_addr[dmanr];

  // Clear config errors
  dmalp[MCFDMA_DMR] |= MCFDMA_DMR_RESET; 

  // Set command register
  dmalp[MCFDMA_DMR] =
    MCFDMA_DMR_RQM_DUAL |         // Mandatory Request Mode setting
    MCFDMA_DMR_DSTT_SD  |         // Set up addressing types; set to supervisor-data.
    MCFDMA_DMR_SRCT_SD  |         // Set up addressing types; set to supervisor-data. 
    // source static-address-mode
    ((mode & DMA_MODE_SRC_SA_BIT) ? MCFDMA_DMR_SRCM_SA : MCFDMA_DMR_SRCM_IA) |
    // dest static-address-mode
    ((mode & DMA_MODE_DES_SA_BIT) ? MCFDMA_DMR_DSTM_SA : MCFDMA_DMR_DSTM_IA) |
    // burst, 32 bit, 16 bit or 8 bit transfers are separately configurable on the MCF5272
    (((mode & DMA_MODE_SSIZE_MASK) >> DMA_MODE_SSIZE_OFF) << MCFDMA_DMR_DSTS_OFF) |
    (((mode & DMA_MODE_SSIZE_MASK) >> DMA_MODE_SSIZE_OFF) << MCFDMA_DMR_SRCS_OFF);
    
  dmawp[MCFDMA_DIR] |= MCFDMA_DIR_ASCEN;   /* Enable completion interrupts */
  
#ifdef DEBUG_DMA
  printk("%s(%d): dmanr=%d DMR[%x]=%x DIR[%x]=%x\n", __FILE__, __LINE__,
         dmanr, (int) &dmalp[MCFDMA_DMR], dmabp[MCFDMA_DMR],
	 (int) &dmawp[MCFDMA_DIR], dmawp[MCFDMA_DIR]);
#endif
}

/* Set transfer address for specific DMA channel */
static __inline__ void set_dma_addr(unsigned int dmanr, unsigned int a)
{
  volatile unsigned int   *dmalp;

#ifdef DMA_DEBUG
  printk("set_dma_addr(dmanr=%d,a=%x)\n", dmanr, a);
#endif

  dmalp = (unsigned int *) dma_base_addr[dmanr];

  // Determine which address registers are used for memory/device accesses
  if (dmalp[MCFDMA_DMR] & MCFDMA_DMR_SRCM) {
    // Source incrementing, must be memory
    dmalp[MCFDMA_DSAR] = a;
    // Set dest address, must be device
    dmalp[MCFDMA_DDAR] = dma_device_address[dmanr];
  } else {
    // Destination incrementing, must be memory
    dmalp[MCFDMA_DDAR] = a;
    // Set source address, must be device
    dmalp[MCFDMA_DSAR] = dma_device_address[dmanr];
  }

#ifdef DEBUG_DMA
  printk("%s(%d): dmanr=%d DMR[%x]=%x SAR[%x]=%08x DAR[%x]=%08x\n",
	__FILE__, __LINE__, dmanr, (int) &dmawp[MCFDMA_DMR], dmawp[MCFDMA_DMR],
	(int) &dmalp[MCFDMA_DSAR], dmalp[MCFDMA_DSAR],
	(int) &dmalp[MCFDMA_DDAR], dmalp[MCFDMA_DDAR]);
#endif
}

/*
 * Specific for Coldfire - sets device address.
 * Should be called after the mode set call, and before set DMA address.
 */
static __inline__ void set_dma_device_addr(unsigned int dmanr, unsigned int a)
{
#ifdef DMA_DEBUG
  printk("set_dma_device_addr(dmanr=%d,a=%x)\n", dmanr, a);
#endif

  dma_device_address[dmanr] = a;
}

/*
 * NOTE 2: "count" represents _bytes_.
 *
 * NOTE 3: While a 32-bit register, "count" is only a maximum 24-bit value.
 */
static __inline__ void set_dma_count(unsigned int dmanr, unsigned int count)
{
  volatile unsigned int *dmalp;
  
#ifdef DMA_DEBUG
  printk("set_dma_count(dmanr=%d,count=%d)\n", dmanr, count);
#endif

  dmalp = (unsigned int *) dma_base_addr[dmanr];
  dmalp[MCFDMA_DBCR] = count;
}

/*
 * Get DMA residue count. After a DMA transfer, this
 * should return zero. Reading this while a DMA transfer is
 * still in progress will return unpredictable results.
 * Otherwise, it returns the number of _bytes_ left to transfer.
 */
static __inline__ int get_dma_residue(unsigned int dmanr)
{
  volatile unsigned int *dmalp;
  unsigned int count;

#ifdef DMA_DEBUG
  printk("get_dma_residue(dmanr=%d)\n", dmanr);
#endif

  dmalp = (unsigned int *) dma_base_addr[dmanr];
  count = dmalp[MCFDMA_DBCR];
  return(count);
}

#endif /* !defined(CONFIG_M5272) */
#endif /* CONFIG_COLDFIRE */
 
#define MAX_DMA_CHANNELS 8

/* Don't define MAX_DMA_ADDRESS; it's useless on the m68k/coldfire and any
   occurrence should be flagged as an error.  */
/* under 2.4 it is actually needed by the new bootmem allocator */
#define MAX_DMA_ADDRESS PAGE_OFFSET

/* These are in kernel/dma.c: */
extern int request_dma(unsigned int dmanr, const char *device_id);	/* reserve a DMA channel */
extern void free_dma(unsigned int dmanr);	/* release it again */
 
#endif /* _M68K_DMA_H */
