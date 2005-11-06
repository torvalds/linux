/*
 * include/asm-ppc/ppc4xx_dma.h
 *
 * IBM PPC4xx DMA engine library
 *
 * Copyright 2000-2004 MontaVista Software Inc.
 *
 * Cleaned up a bit more, Matt Porter <mporter@kernel.crashing.org>
 *
 * Original code by Armin Kuster <akuster@mvista.com>
 * and Pete Popov <ppopov@mvista.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __KERNEL__
#ifndef __ASMPPC_PPC4xx_DMA_H
#define __ASMPPC_PPC4xx_DMA_H

#include <linux/config.h>
#include <linux/types.h>
#include <asm/mmu.h>
#include <asm/ibm4xx.h>

#undef DEBUG_4xxDMA

#define MAX_PPC4xx_DMA_CHANNELS		4

/* in arch/ppc/kernel/setup.c -- Cort */
extern unsigned long DMA_MODE_WRITE, DMA_MODE_READ;

/*
 * Function return status codes
 * These values are used to indicate whether or not the function
 * call was successful, or a bad/invalid parameter was passed.
 */
#define DMA_STATUS_GOOD			0
#define DMA_STATUS_BAD_CHANNEL		1
#define DMA_STATUS_BAD_HANDLE		2
#define DMA_STATUS_BAD_MODE		3
#define DMA_STATUS_NULL_POINTER		4
#define DMA_STATUS_OUT_OF_MEMORY	5
#define DMA_STATUS_SGL_LIST_EMPTY	6
#define DMA_STATUS_GENERAL_ERROR	7
#define DMA_STATUS_CHANNEL_NOTFREE	8

#define DMA_CHANNEL_BUSY		0x80000000

/*
 * These indicate status as returned from the DMA Status Register.
 */
#define DMA_STATUS_NO_ERROR	0
#define DMA_STATUS_CS		1	/* Count Status        */
#define DMA_STATUS_TS		2	/* Transfer Status     */
#define DMA_STATUS_DMA_ERROR	3	/* DMA Error Occurred  */
#define DMA_STATUS_DMA_BUSY	4	/* The channel is busy */


/*
 * DMA Channel Control Registers
 */

#ifdef CONFIG_44x
#define	PPC4xx_DMA_64BIT
#define DMA_CR_OFFSET 1
#else
#define DMA_CR_OFFSET 0
#endif

#define DMA_CE_ENABLE        (1<<31)	/* DMA Channel Enable */
#define SET_DMA_CE_ENABLE(x) (((x)&0x1)<<31)
#define GET_DMA_CE_ENABLE(x) (((x)&DMA_CE_ENABLE)>>31)

#define DMA_CIE_ENABLE        (1<<30)	/* DMA Channel Interrupt Enable */
#define SET_DMA_CIE_ENABLE(x) (((x)&0x1)<<30)
#define GET_DMA_CIE_ENABLE(x) (((x)&DMA_CIE_ENABLE)>>30)

#define DMA_TD                (1<<29)
#define SET_DMA_TD(x)         (((x)&0x1)<<29)
#define GET_DMA_TD(x)         (((x)&DMA_TD)>>29)

#define DMA_PL                (1<<28)	/* Peripheral Location */
#define SET_DMA_PL(x)         (((x)&0x1)<<28)
#define GET_DMA_PL(x)         (((x)&DMA_PL)>>28)

#define EXTERNAL_PERIPHERAL    0
#define INTERNAL_PERIPHERAL    1

#define SET_DMA_PW(x)     (((x)&0x3)<<(26-DMA_CR_OFFSET))	/* Peripheral Width */
#define DMA_PW_MASK       SET_DMA_PW(3)
#define   PW_8                 0
#define   PW_16                1
#define   PW_32                2
#define   PW_64                3
/* FIXME: Add PW_128 support for 440GP DMA block */
#define GET_DMA_PW(x)     (((x)&DMA_PW_MASK)>>(26-DMA_CR_OFFSET))

#define DMA_DAI           (1<<(25-DMA_CR_OFFSET))	/* Destination Address Increment */
#define SET_DMA_DAI(x)    (((x)&0x1)<<(25-DMA_CR_OFFSET))

#define DMA_SAI           (1<<(24-DMA_CR_OFFSET))	/* Source Address Increment */
#define SET_DMA_SAI(x)    (((x)&0x1)<<(24-DMA_CR_OFFSET))

#define DMA_BEN           (1<<(23-DMA_CR_OFFSET))	/* Buffer Enable */
#define SET_DMA_BEN(x)    (((x)&0x1)<<(23-DMA_CR_OFFSET))

#define SET_DMA_TM(x)     (((x)&0x3)<<(21-DMA_CR_OFFSET))	/* Transfer Mode */
#define DMA_TM_MASK       SET_DMA_TM(3)
#define   TM_PERIPHERAL        0	/* Peripheral */
#define   TM_RESERVED          1	/* Reserved */
#define   TM_S_MM              2	/* Memory to Memory */
#define   TM_D_MM              3	/* Device Paced Memory to Memory */
#define GET_DMA_TM(x)     (((x)&DMA_TM_MASK)>>(21-DMA_CR_OFFSET))

#define SET_DMA_PSC(x)    (((x)&0x3)<<(19-DMA_CR_OFFSET))	/* Peripheral Setup Cycles */
#define DMA_PSC_MASK      SET_DMA_PSC(3)
#define GET_DMA_PSC(x)    (((x)&DMA_PSC_MASK)>>(19-DMA_CR_OFFSET))

#define SET_DMA_PWC(x)    (((x)&0x3F)<<(13-DMA_CR_OFFSET))	/* Peripheral Wait Cycles */
#define DMA_PWC_MASK      SET_DMA_PWC(0x3F)
#define GET_DMA_PWC(x)    (((x)&DMA_PWC_MASK)>>(13-DMA_CR_OFFSET))

#define SET_DMA_PHC(x)    (((x)&0x7)<<(10-DMA_CR_OFFSET))	/* Peripheral Hold Cycles */
#define DMA_PHC_MASK      SET_DMA_PHC(0x7)
#define GET_DMA_PHC(x)    (((x)&DMA_PHC_MASK)>>(10-DMA_CR_OFFSET))

#define DMA_ETD_OUTPUT     (1<<(9-DMA_CR_OFFSET))	/* EOT pin is a TC output */
#define SET_DMA_ETD(x)     (((x)&0x1)<<(9-DMA_CR_OFFSET))

#define DMA_TCE_ENABLE     (1<<(8-DMA_CR_OFFSET))
#define SET_DMA_TCE(x)     (((x)&0x1)<<(8-DMA_CR_OFFSET))

#define DMA_DEC            (1<<(2))	/* Address Decrement */
#define SET_DMA_DEC(x)     (((x)&0x1)<<2)
#define GET_DMA_DEC(x)     (((x)&DMA_DEC)>>2)


/*
 * Transfer Modes
 * These modes are defined in a way that makes it possible to
 * simply "or" in the value in the control register.
 */

#define DMA_MODE_MM		(SET_DMA_TM(TM_S_MM))	/* memory to memory */

				/* Device-paced memory to memory, */
				/* device is at source address    */
#define DMA_MODE_MM_DEVATSRC	(DMA_TD | SET_DMA_TM(TM_D_MM))

				/* Device-paced memory to memory,      */
				/* device is at destination address    */
#define DMA_MODE_MM_DEVATDST	(SET_DMA_TM(TM_D_MM))

/* 405gp/440gp */
#define SET_DMA_PREFETCH(x)   (((x)&0x3)<<(4-DMA_CR_OFFSET))	/* Memory Read Prefetch */
#define DMA_PREFETCH_MASK      SET_DMA_PREFETCH(3)
#define   PREFETCH_1           0	/* Prefetch 1 Double Word */
#define   PREFETCH_2           1
#define   PREFETCH_4           2
#define GET_DMA_PREFETCH(x) (((x)&DMA_PREFETCH_MASK)>>(4-DMA_CR_OFFSET))

#define DMA_PCE            (1<<(3-DMA_CR_OFFSET))	/* Parity Check Enable */
#define SET_DMA_PCE(x)     (((x)&0x1)<<(3-DMA_CR_OFFSET))
#define GET_DMA_PCE(x)     (((x)&DMA_PCE)>>(3-DMA_CR_OFFSET))

/* stb3x */

#define DMA_ECE_ENABLE (1<<5)
#define SET_DMA_ECE(x) (((x)&0x1)<<5)
#define GET_DMA_ECE(x) (((x)&DMA_ECE_ENABLE)>>5)

#define DMA_TCD_DISABLE	(1<<4)
#define SET_DMA_TCD(x) (((x)&0x1)<<4)
#define GET_DMA_TCD(x) (((x)&DMA_TCD_DISABLE)>>4)

typedef uint32_t sgl_handle_t;

#ifdef CONFIG_PPC4xx_EDMA

#define SGL_LIST_SIZE 4096
#define DMA_PPC4xx_SIZE SGL_LIST_SIZE

#define SET_DMA_PRIORITY(x)   (((x)&0x3)<<(6-DMA_CR_OFFSET))	/* DMA Channel Priority */
#define DMA_PRIORITY_MASK SET_DMA_PRIORITY(3)
#define PRIORITY_LOW           0
#define PRIORITY_MID_LOW       1
#define PRIORITY_MID_HIGH      2
#define PRIORITY_HIGH          3
#define GET_DMA_PRIORITY(x) (((x)&DMA_PRIORITY_MASK)>>(6-DMA_CR_OFFSET))

/*
 * DMA Polarity Configuration Register
 */
#define DMAReq_ActiveLow(chan) (1<<(31-(chan*3)))
#define DMAAck_ActiveLow(chan) (1<<(30-(chan*3)))
#define EOT_ActiveLow(chan)    (1<<(29-(chan*3)))	/* End of Transfer */

/*
 * DMA Sleep Mode Register
 */
#define SLEEP_MODE_ENABLE (1<<21)

/*
 * DMA Status Register
 */
#define DMA_CS0           (1<<31)	/* Terminal Count has been reached */
#define DMA_CS1           (1<<30)
#define DMA_CS2           (1<<29)
#define DMA_CS3           (1<<28)

#define DMA_TS0           (1<<27)	/* End of Transfer has been requested */
#define DMA_TS1           (1<<26)
#define DMA_TS2           (1<<25)
#define DMA_TS3           (1<<24)

#define DMA_CH0_ERR       (1<<23)	/* DMA Chanel 0 Error */
#define DMA_CH1_ERR       (1<<22)
#define DMA_CH2_ERR       (1<<21)
#define DMA_CH3_ERR       (1<<20)

#define DMA_IN_DMA_REQ0   (1<<19)	/* Internal DMA Request is pending */
#define DMA_IN_DMA_REQ1   (1<<18)
#define DMA_IN_DMA_REQ2   (1<<17)
#define DMA_IN_DMA_REQ3   (1<<16)

#define DMA_EXT_DMA_REQ0  (1<<15)	/* External DMA Request is pending */
#define DMA_EXT_DMA_REQ1  (1<<14)
#define DMA_EXT_DMA_REQ2  (1<<13)
#define DMA_EXT_DMA_REQ3  (1<<12)

#define DMA_CH0_BUSY      (1<<11)	/* DMA Channel 0 Busy */
#define DMA_CH1_BUSY      (1<<10)
#define DMA_CH2_BUSY       (1<<9)
#define DMA_CH3_BUSY       (1<<8)

#define DMA_SG0            (1<<7)	/* DMA Channel 0 Scatter/Gather in progress */
#define DMA_SG1            (1<<6)
#define DMA_SG2            (1<<5)
#define DMA_SG3            (1<<4)

/* DMA Channel Count Register */
#define DMA_CTC_BTEN     (1<<23)    /* Burst Enable/Disable bit */
#define DMA_CTC_BSIZ_MSK (3<<21)    /* Mask of the Burst size bits */
#define DMA_CTC_BSIZ_2   (0)
#define DMA_CTC_BSIZ_4   (1<<21)
#define DMA_CTC_BSIZ_8   (2<<21)
#define DMA_CTC_BSIZ_16  (3<<21)

/*
 * DMA SG Command Register
 */
#define SSG_ENABLE(chan)   	(1<<(31-chan))	/* Start Scatter Gather */
#define SSG_MASK_ENABLE(chan)	(1<<(15-chan))	/* Enable writing to SSG0 bit */

/*
 * DMA Scatter/Gather Descriptor Bit fields
 */
#define SG_LINK            (1<<31)	/* Link */
#define SG_TCI_ENABLE      (1<<29)	/* Enable Terminal Count Interrupt */
#define SG_ETI_ENABLE      (1<<28)	/* Enable End of Transfer Interrupt */
#define SG_ERI_ENABLE      (1<<27)	/* Enable Error Interrupt */
#define SG_COUNT_MASK       0xFFFF	/* Count Field */

#define SET_DMA_CONTROL \
 		(SET_DMA_CIE_ENABLE(p_init->int_enable) | /* interrupt enable         */ \
 		SET_DMA_BEN(p_init->buffer_enable)     | /* buffer enable            */\
		SET_DMA_ETD(p_init->etd_output)        | /* end of transfer pin      */ \
	       	SET_DMA_TCE(p_init->tce_enable)        | /* terminal count enable    */ \
                SET_DMA_PL(p_init->pl)                 | /* peripheral location      */ \
                SET_DMA_DAI(p_init->dai)               | /* dest addr increment      */ \
                SET_DMA_SAI(p_init->sai)               | /* src addr increment       */ \
                SET_DMA_PRIORITY(p_init->cp)           |  /* channel priority        */ \
                SET_DMA_PW(p_init->pwidth)             |  /* peripheral/bus width    */ \
                SET_DMA_PSC(p_init->psc)               |  /* peripheral setup cycles */ \
                SET_DMA_PWC(p_init->pwc)               |  /* peripheral wait cycles  */ \
                SET_DMA_PHC(p_init->phc)               |  /* peripheral hold cycles  */ \
                SET_DMA_PREFETCH(p_init->pf)              /* read prefetch           */)

#define GET_DMA_POLARITY(chan) (DMAReq_ActiveLow(chan) | DMAAck_ActiveLow(chan) | EOT_ActiveLow(chan))

#elif defined(CONFIG_STB03xxx)		/* stb03xxx */

#define DMA_PPC4xx_SIZE	4096

/*
 * DMA Status Register
 */

#define SET_DMA_PRIORITY(x)   (((x)&0x00800001))	/* DMA Channel Priority */
#define DMA_PRIORITY_MASK	0x00800001
#define   PRIORITY_LOW         	0x00000000
#define   PRIORITY_MID_LOW     	0x00000001
#define   PRIORITY_MID_HIGH    	0x00800000
#define   PRIORITY_HIGH        	0x00800001
#define GET_DMA_PRIORITY(x) (((((x)&DMA_PRIORITY_MASK) &0x00800000) >> 22 ) | (((x)&DMA_PRIORITY_MASK) &0x00000001))

#define DMA_CS0           (1<<31)	/* Terminal Count has been reached */
#define DMA_CS1           (1<<30)
#define DMA_CS2           (1<<29)
#define DMA_CS3           (1<<28)

#define DMA_TS0           (1<<27)	/* End of Transfer has been requested */
#define DMA_TS1           (1<<26)
#define DMA_TS2           (1<<25)
#define DMA_TS3           (1<<24)

#define DMA_CH0_ERR       (1<<23)	/* DMA Chanel 0 Error */
#define DMA_CH1_ERR       (1<<22)
#define DMA_CH2_ERR       (1<<21)
#define DMA_CH3_ERR       (1<<20)

#define DMA_CT0		  (1<<19)	/* Chained transfere */

#define DMA_IN_DMA_REQ0   (1<<18)	/* Internal DMA Request is pending */
#define DMA_IN_DMA_REQ1   (1<<17)
#define DMA_IN_DMA_REQ2   (1<<16)
#define DMA_IN_DMA_REQ3   (1<<15)

#define DMA_EXT_DMA_REQ0  (1<<14)	/* External DMA Request is pending */
#define DMA_EXT_DMA_REQ1  (1<<13)
#define DMA_EXT_DMA_REQ2  (1<<12)
#define DMA_EXT_DMA_REQ3  (1<<11)

#define DMA_CH0_BUSY      (1<<10)	/* DMA Channel 0 Busy */
#define DMA_CH1_BUSY      (1<<9)
#define DMA_CH2_BUSY       (1<<8)
#define DMA_CH3_BUSY       (1<<7)

#define DMA_CT1            (1<<6)	/* Chained transfere */
#define DMA_CT2            (1<<5)
#define DMA_CT3            (1<<4)

#define DMA_CH_ENABLE (1<<7)
#define SET_DMA_CH(x) (((x)&0x1)<<7)
#define GET_DMA_CH(x) (((x)&DMA_CH_ENABLE)>>7)

/* STBx25xxx dma unique */
/* enable device port on a dma channel
 * example ext 0 on dma 1
 */

#define	SSP0_RECV	15
#define	SSP0_XMIT	14
#define EXT_DMA_0	12
#define	SC1_XMIT	11
#define SC1_RECV	10
#define EXT_DMA_2	9
#define	EXT_DMA_3	8
#define SERIAL2_XMIT	7
#define SERIAL2_RECV	6
#define SC0_XMIT 	5
#define	SC0_RECV	4
#define	SERIAL1_XMIT	3
#define SERIAL1_RECV	2
#define	SERIAL0_XMIT	1
#define SERIAL0_RECV	0

#define DMA_CHAN_0	1
#define DMA_CHAN_1	2
#define DMA_CHAN_2	3
#define DMA_CHAN_3	4

/* end STBx25xx */

/*
 * Bit 30 must be one for Redwoods, otherwise transfers may receive errors.
 */
#define DMA_CR_MB0 0x2

#define SET_DMA_CONTROL \
       		(SET_DMA_CIE_ENABLE(p_init->int_enable) |  /* interrupt enable         */ \
		SET_DMA_ETD(p_init->etd_output)        |  /* end of transfer pin      */ \
		SET_DMA_TCE(p_init->tce_enable)        |  /* terminal count enable    */ \
		SET_DMA_PL(p_init->pl)                 |  /* peripheral location      */ \
		SET_DMA_DAI(p_init->dai)               |  /* dest addr increment      */ \
		SET_DMA_SAI(p_init->sai)               |  /* src addr increment       */ \
		SET_DMA_PRIORITY(p_init->cp)           |  /* channel priority        */  \
		SET_DMA_PW(p_init->pwidth)             |  /* peripheral/bus width    */ \
		SET_DMA_PSC(p_init->psc)               |  /* peripheral setup cycles */ \
		SET_DMA_PWC(p_init->pwc)               |  /* peripheral wait cycles  */ \
		SET_DMA_PHC(p_init->phc)               |  /* peripheral hold cycles  */ \
		SET_DMA_TCD(p_init->tcd_disable)	  |  /* TC chain mode disable   */ \
		SET_DMA_ECE(p_init->ece_enable)	  |  /* ECE chanin mode enable  */ \
		SET_DMA_CH(p_init->ch_enable)	|    /* Chain enable 	        */ \
		DMA_CR_MB0				/* must be one */)

#define GET_DMA_POLARITY(chan) chan

#endif

typedef struct {
	unsigned short in_use;	/* set when channel is being used, clr when
				 * available.
				 */
	/*
	 * Valid polarity settings:
	 *   DMAReq_ActiveLow(n)
	 *   DMAAck_ActiveLow(n)
	 *   EOT_ActiveLow(n)
	 *
	 *   n is 0 to max dma chans
	 */
	unsigned int polarity;

	char buffer_enable;	/* Boolean: buffer enable            */
	char tce_enable;	/* Boolean: terminal count enable    */
	char etd_output;	/* Boolean: eot pin is a tc output   */
	char pce;		/* Boolean: parity check enable      */

	/*
	 * Peripheral location:
	 * INTERNAL_PERIPHERAL (UART0 on the 405GP)
	 * EXTERNAL_PERIPHERAL
	 */
	char pl;		/* internal/external peripheral      */

	/*
	 * Valid pwidth settings:
	 *   PW_8
	 *   PW_16
	 *   PW_32
	 *   PW_64
	 */
	unsigned int pwidth;

	char dai;		/* Boolean: dst address increment   */
	char sai;		/* Boolean: src address increment   */

	/*
	 * Valid psc settings: 0-3
	 */
	unsigned int psc;	/* Peripheral Setup Cycles         */

	/*
	 * Valid pwc settings:
	 * 0-63
	 */
	unsigned int pwc;	/* Peripheral Wait Cycles          */

	/*
	 * Valid phc settings:
	 * 0-7
	 */
	unsigned int phc;	/* Peripheral Hold Cycles          */

	/*
	 * Valid cp (channel priority) settings:
	 *   PRIORITY_LOW
	 *   PRIORITY_MID_LOW
	 *   PRIORITY_MID_HIGH
	 *   PRIORITY_HIGH
	 */
	unsigned int cp;	/* channel priority                */

	/*
	 * Valid pf (memory read prefetch) settings:
	 *
	 *   PREFETCH_1
	 *   PREFETCH_2
	 *   PREFETCH_4
	 */
	unsigned int pf;	/* memory read prefetch            */

	/*
	 * Boolean: channel interrupt enable
	 * NOTE: for sgl transfers, only the last descriptor will be setup to
	 * interrupt.
	 */
	char int_enable;

	char shift;		/* easy access to byte_count shift, based on */
	/* the width of the channel                  */

	uint32_t control;	/* channel control word                      */

	/* These variabled are used ONLY in single dma transfers              */
	unsigned int mode;	/* transfer mode                     */
	phys_addr_t addr;
	char ce;		/* channel enable */
#ifdef CONFIG_STB03xxx
	char ch_enable;
	char tcd_disable;
	char ece_enable;
	char td;		/* transfer direction */
#endif

	char int_on_final_sg;/* for scatter/gather - only interrupt on last sg */
} ppc_dma_ch_t;

/*
 * PPC44x DMA implementations have a slightly different
 * descriptor layout.  Probably moved about due to the
 * change to 64-bit addresses and link pointer. I don't
 * know why they didn't just leave control_count after
 * the dst_addr.
 */
#ifdef PPC4xx_DMA_64BIT
typedef struct {
	uint32_t control;
	uint32_t control_count;
	phys_addr_t src_addr;
	phys_addr_t dst_addr;
	phys_addr_t next;
} ppc_sgl_t;
#else
typedef struct {
	uint32_t control;
	phys_addr_t src_addr;
	phys_addr_t dst_addr;
	uint32_t control_count;
	uint32_t next;
} ppc_sgl_t;
#endif

typedef struct {
	unsigned int dmanr;
	uint32_t control;	/* channel ctrl word; loaded from each descrptr */
	uint32_t sgl_control;	/* LK, TCI, ETI, and ERI bits in sgl descriptor */
	dma_addr_t dma_addr;	/* dma (physical) address of this list          */
	ppc_sgl_t *phead;
	dma_addr_t phead_dma;
	ppc_sgl_t *ptail;
	dma_addr_t ptail_dma;
} sgl_list_info_t;

typedef struct {
	phys_addr_t *src_addr;
	phys_addr_t *dst_addr;
	phys_addr_t dma_src_addr;
	phys_addr_t dma_dst_addr;
} pci_alloc_desc_t;

extern ppc_dma_ch_t dma_channels[];

/*
 * The DMA API are in ppc4xx_dma.c and ppc4xx_sgdma.c
 */
extern int ppc4xx_init_dma_channel(unsigned int, ppc_dma_ch_t *);
extern int ppc4xx_get_channel_config(unsigned int, ppc_dma_ch_t *);
extern int ppc4xx_set_channel_priority(unsigned int, unsigned int);
extern unsigned int ppc4xx_get_peripheral_width(unsigned int);
extern void ppc4xx_set_sg_addr(int, phys_addr_t);
extern int ppc4xx_add_dma_sgl(sgl_handle_t, phys_addr_t, phys_addr_t, unsigned int);
extern void ppc4xx_enable_dma_sgl(sgl_handle_t);
extern void ppc4xx_disable_dma_sgl(sgl_handle_t);
extern int ppc4xx_get_dma_sgl_residue(sgl_handle_t, phys_addr_t *, phys_addr_t *);
extern int ppc4xx_delete_dma_sgl_element(sgl_handle_t, phys_addr_t *, phys_addr_t *);
extern int ppc4xx_alloc_dma_handle(sgl_handle_t *, unsigned int, unsigned int);
extern void ppc4xx_free_dma_handle(sgl_handle_t);
extern int ppc4xx_get_dma_status(void);
extern int ppc4xx_enable_burst(unsigned int);
extern int ppc4xx_disable_burst(unsigned int);
extern int ppc4xx_set_burst_size(unsigned int, unsigned int);
extern void ppc4xx_set_src_addr(int dmanr, phys_addr_t src_addr);
extern void ppc4xx_set_dst_addr(int dmanr, phys_addr_t dst_addr);
extern void ppc4xx_enable_dma(unsigned int dmanr);
extern void ppc4xx_disable_dma(unsigned int dmanr);
extern void ppc4xx_set_dma_count(unsigned int dmanr, unsigned int count);
extern int ppc4xx_get_dma_residue(unsigned int dmanr);
extern void ppc4xx_set_dma_addr2(unsigned int dmanr, phys_addr_t src_dma_addr,
				 phys_addr_t dst_dma_addr);
extern int ppc4xx_enable_dma_interrupt(unsigned int dmanr);
extern int ppc4xx_disable_dma_interrupt(unsigned int dmanr);
extern int ppc4xx_clr_dma_status(unsigned int dmanr);
extern int ppc4xx_map_dma_port(unsigned int dmanr, unsigned int ocp_dma,short dma_chan);
extern int ppc4xx_disable_dma_port(unsigned int dmanr, unsigned int ocp_dma,short dma_chan);
extern int ppc4xx_set_dma_mode(unsigned int dmanr, unsigned int mode);

/* These are in kernel/dma.c: */

/* reserve a DMA channel */
extern int request_dma(unsigned int dmanr, const char *device_id);
/* release it again */
extern void free_dma(unsigned int dmanr);
#endif
#endif				/* __KERNEL__ */
