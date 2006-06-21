/****************************************************************************/

/*
 *	mcfdma.h -- Coldfire internal DMA support defines.
 *
 *	(C) Copyright 1999, Rob Scott (rscott@mtrob.ml.org)
 */

/****************************************************************************/
#ifndef	mcfdma_h
#define	mcfdma_h
/****************************************************************************/


/*
 *	Get address specific defines for this Coldfire member.
 */
#if defined(CONFIG_M5206) || defined(CONFIG_M5206e)
#define	MCFDMA_BASE0		0x200		/* Base address of DMA 0 */
#define	MCFDMA_BASE1		0x240		/* Base address of DMA 1 */
#elif defined(CONFIG_M5272)
#define	MCFDMA_BASE0		0x0e0		/* Base address of DMA 0 */
#elif defined(CONFIG_M523x) || defined(CONFIG_M527x) || defined(CONFIG_M528x)
/* These are relative to the IPSBAR, not MBAR */
#define	MCFDMA_BASE0		0x100		/* Base address of DMA 0 */
#define	MCFDMA_BASE1		0x140		/* Base address of DMA 1 */
#define	MCFDMA_BASE2		0x180		/* Base address of DMA 2 */
#define	MCFDMA_BASE3		0x1C0		/* Base address of DMA 3 */
#elif defined(CONFIG_M5249) || defined(CONFIG_M5307) || defined(CONFIG_M5407)
#define	MCFDMA_BASE0		0x300		/* Base address of DMA 0 */
#define	MCFDMA_BASE1		0x340		/* Base address of DMA 1 */
#define	MCFDMA_BASE2		0x380		/* Base address of DMA 2 */
#define	MCFDMA_BASE3		0x3C0		/* Base address of DMA 3 */
#endif


#if !defined(CONFIG_M5272)

/*
 *	Define the DMA register set addresses.
 *      Note: these are longword registers, use unsigned long as data type
 */
#define	MCFDMA_SAR		0x00		/* DMA source address (r/w) */
#define	MCFDMA_DAR		0x01		/* DMA destination adr (r/w) */
/* these are word registers, use unsigned short data type */
#define	MCFDMA_DCR		0x04		/* DMA control reg (r/w) */
#define	MCFDMA_BCR		0x06		/* DMA byte count reg (r/w) */
/* these are byte registers, use unsiged char data type */
#define	MCFDMA_DSR		0x10		/* DMA status reg (r/w) */
#define	MCFDMA_DIVR		0x14		/* DMA interrupt vec (r/w) */

/*
 *	Bit definitions for the DMA Control Register (DCR).
 */
#define	MCFDMA_DCR_INT	        0x8000		/* Enable completion irq */
#define	MCFDMA_DCR_EEXT	        0x4000		/* Enable external DMA req */
#define	MCFDMA_DCR_CS 	        0x2000		/* Enable cycle steal */
#define	MCFDMA_DCR_AA   	0x1000		/* Enable auto alignment */
#define	MCFDMA_DCR_BWC_MASK  	0x0E00		/* Bandwidth ctl mask */
#define MCFDMA_DCR_BWC_512      0x0200          /* Bandwidth:   512 Bytes */
#define MCFDMA_DCR_BWC_1024     0x0400          /* Bandwidth:  1024 Bytes */
#define MCFDMA_DCR_BWC_2048     0x0600          /* Bandwidth:  2048 Bytes */
#define MCFDMA_DCR_BWC_4096     0x0800          /* Bandwidth:  4096 Bytes */
#define MCFDMA_DCR_BWC_8192     0x0a00          /* Bandwidth:  8192 Bytes */
#define MCFDMA_DCR_BWC_16384    0x0c00          /* Bandwidth: 16384 Bytes */
#define MCFDMA_DCR_BWC_32768    0x0e00          /* Bandwidth: 32768 Bytes */
#define	MCFDMA_DCR_SAA         	0x0100		/* Single Address Access */
#define	MCFDMA_DCR_S_RW        	0x0080		/* SAA read/write value */
#define	MCFDMA_DCR_SINC        	0x0040		/* Source addr inc enable */
#define	MCFDMA_DCR_SSIZE_MASK  	0x0030		/* Src xfer size */
#define	MCFDMA_DCR_SSIZE_LONG  	0x0000		/* Src xfer size, 00 = longw */
#define	MCFDMA_DCR_SSIZE_BYTE  	0x0010		/* Src xfer size, 01 = byte */
#define	MCFDMA_DCR_SSIZE_WORD  	0x0020		/* Src xfer size, 10 = word */
#define	MCFDMA_DCR_SSIZE_LINE  	0x0030		/* Src xfer size, 11 = line */
#define	MCFDMA_DCR_DINC        	0x0008		/* Dest addr inc enable */
#define	MCFDMA_DCR_DSIZE_MASK  	0x0006		/* Dest xfer size */
#define	MCFDMA_DCR_DSIZE_LONG  	0x0000		/* Dest xfer size, 00 = long */
#define	MCFDMA_DCR_DSIZE_BYTE  	0x0002		/* Dest xfer size, 01 = byte */
#define	MCFDMA_DCR_DSIZE_WORD  	0x0004		/* Dest xfer size, 10 = word */
#define	MCFDMA_DCR_DSIZE_LINE  	0x0006		/* Dest xfer size, 11 = line */
#define	MCFDMA_DCR_START       	0x0001		/* Start transfer */

/*
 *	Bit definitions for the DMA Status Register (DSR).
 */
#define	MCFDMA_DSR_CE	        0x40		/* Config error */
#define	MCFDMA_DSR_BES	        0x20		/* Bus Error on source */
#define	MCFDMA_DSR_BED 	        0x10		/* Bus Error on dest */
#define	MCFDMA_DSR_REQ   	0x04		/* Requests remaining */
#define	MCFDMA_DSR_BSY  	0x02		/* Busy */
#define	MCFDMA_DSR_DONE        	0x01		/* DMA transfer complete */

#else /* This is an MCF5272 */

#define MCFDMA_DMR        0x00    /* Mode Register (r/w) */
#define MCFDMA_DIR        0x03    /* Interrupt trigger register (r/w) */
#define MCFDMA_DSAR       0x03    /* Source Address register (r/w) */
#define MCFDMA_DDAR       0x04    /* Destination Address register (r/w) */
#define MCFDMA_DBCR       0x02    /* Byte Count Register (r/w) */

/* Bit definitions for the DMA Mode Register (DMR) */
#define MCFDMA_DMR_RESET     0x80000000L /* Reset bit */
#define MCFDMA_DMR_EN        0x40000000L /* DMA enable */
#define MCFDMA_DMR_RQM       0x000C0000L /* Request Mode Mask */
#define MCFDMA_DMR_RQM_DUAL  0x000C0000L /* Dual address mode, the only valid mode */
#define MCFDMA_DMR_DSTM      0x00002000L /* Destination addressing mask */
#define MCFDMA_DMR_DSTM_SA   0x00000000L /* Destination uses static addressing */
#define MCFDMA_DMR_DSTM_IA   0x00002000L /* Destination uses incremental addressing */
#define MCFDMA_DMR_DSTT_UD   0x00000400L /* Destination is user data */
#define MCFDMA_DMR_DSTT_UC   0x00000800L /* Destination is user code */
#define MCFDMA_DMR_DSTT_SD   0x00001400L /* Destination is supervisor data */
#define MCFDMA_DMR_DSTT_SC   0x00001800L /* Destination is supervisor code */
#define MCFDMA_DMR_DSTS_OFF  0x8         /* offset to the destination size bits */
#define MCFDMA_DMR_DSTS_LONG 0x00000000L /* Long destination size */
#define MCFDMA_DMR_DSTS_BYTE 0x00000100L /* Byte destination size */
#define MCFDMA_DMR_DSTS_WORD 0x00000200L /* Word destination size */
#define MCFDMA_DMR_DSTS_LINE 0x00000300L /* Line destination size */
#define MCFDMA_DMR_SRCM      0x00000020L /* Source addressing mask */
#define MCFDMA_DMR_SRCM_SA   0x00000000L /* Source uses static addressing */
#define MCFDMA_DMR_SRCM_IA   0x00000020L /* Source uses incremental addressing */
#define MCFDMA_DMR_SRCT_UD   0x00000004L /* Source is user data */
#define MCFDMA_DMR_SRCT_UC   0x00000008L /* Source is user code */
#define MCFDMA_DMR_SRCT_SD   0x00000014L /* Source is supervisor data */
#define MCFDMA_DMR_SRCT_SC   0x00000018L /* Source is supervisor code */
#define MCFDMA_DMR_SRCS_OFF  0x0         /* Offset to the source size bits */
#define MCFDMA_DMR_SRCS_LONG 0x00000000L /* Long source size */
#define MCFDMA_DMR_SRCS_BYTE 0x00000001L /* Byte source size */
#define MCFDMA_DMR_SRCS_WORD 0x00000002L /* Word source size */
#define MCFDMA_DMR_SRCS_LINE 0x00000003L /* Line source size */

/* Bit definitions for the DMA interrupt register (DIR) */
#define MCFDMA_DIR_INVEN     0x1000 /* Invalid Combination interrupt enable */
#define MCFDMA_DIR_ASCEN     0x0800 /* Address Sequence Complete (Completion) interrupt enable */
#define MCFDMA_DIR_TEEN      0x0200 /* Transfer Error interrupt enable */
#define MCFDMA_DIR_TCEN      0x0100 /* Transfer Complete (a bus transfer, that is) interrupt enable */
#define MCFDMA_DIR_INV       0x1000 /* Invalid Combination */
#define MCFDMA_DIR_ASC       0x0008 /* Address Sequence Complete (DMA Completion) */
#define MCFDMA_DIR_TE        0x0002 /* Transfer Error */
#define MCFDMA_DIR_TC        0x0001 /* Transfer Complete */

#endif /* !defined(CONFIG_M5272) */ 

/****************************************************************************/
#endif	/* mcfdma_h */
