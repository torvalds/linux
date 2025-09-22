/*	$OpenBSD: if_elreg.h,v 1.3 2022/01/09 05:42:44 jsg Exp $	*/
/*	$NetBSD: if_elreg.h,v 1.4 1994/10/27 04:17:29 cgd Exp $	*/

/*
 * Copyright (c) 1994, Matthew E. Kimmel.  Permission is hereby granted
 * to use, copy, modify and distribute this software provided that both
 * the copyright notice and this permission notice appear in all copies
 * of the software, derivative works or modified versions, and any
 * portions thereof.
 */

/*
 * 3COM Etherlink 3C501 Register Definitions
 */

/*
 * I/O Ports
 */
#define	EL_RXS		0x6	/* Receive status register */
#define	EL_RXC		0x6	/* Receive command register */
#define	EL_TXS		0x7	/* Transmit status register */
#define	EL_TXC		0x7	/* Transmit command register */
#define	EL_GPBL		0x8	/* GP buffer ptr low byte */
#define	EL_GPBH		0x9	/* GP buffer ptr high byte */
#define	EL_RBL		0xa	/* Receive buffer ptr low byte */
#define	EL_RBC		0xa	/* Receive buffer clear */
#define	EL_RBH		0xb	/* Receive buffer ptr high byte */
#define	EL_EAW		0xc	/* Ethernet address window */
#define	EL_AS		0xe	/* Auxiliary status register */
#define	EL_AC		0xe	/* Auxiliary command register */
#define	EL_BUF		0xf	/* Data buffer */

/* Receive status register bits */
#define	EL_RXS_OFLOW	0x01	/* Overflow error */
#define	EL_RXS_FCS	0x02	/* FCS error */
#define	EL_RXS_DRIB	0x04	/* Dribble error */
#define	EL_RXS_SHORT	0x08	/* Short frame */
#define	EL_RXS_NOFLOW	0x10	/* No overflow */
#define	EL_RXS_GOOD	0x20	/* Received good frame */
#define	EL_RXS_STALE	0x80	/* Stale receive status */

/* Receive command register bits */
#define	EL_RXC_DISABLE	0x00	/* Receiver disabled */
#define	EL_RXC_DOFLOW	0x01	/* Detect overflow */
#define	EL_RXC_DFCS	0x02	/* Detect FCS errs */
#define	EL_RXC_DDRIB	0x04	/* Detect dribble errors */
#define	EL_RXC_DSHORT	0x08	/* Detect short frames */
#define	EL_RXC_DNOFLOW	0x10	/* Detect frames w/o overflow ??? */
#define	EL_RXC_AGF	0x20	/* Accept Good Frames */
#define	EL_RXC_PROMISC	0x40	/* Promiscuous mode */
#define	EL_RXC_ABROAD	0x80	/* Accept address, broadcast */
#define	EL_RXC_AMULTI	0xc0	/* Accept address, multicast */

/* Transmit status register bits */
#define	EL_TXS_UFLOW	0x01	/* Underflow */
#define	EL_TXS_COLL	0x02	/* Collision */
#define	EL_TXS_COLL16	0x04	/* Collision 16 */
#define	EL_TXS_READY	0x08	/* Ready for new frame */

/* Transmit command register bits */
#define	EL_TXC_DUFLOW	0x01	/* Detect underflow */
#define	EL_TXC_DCOLL	0x02	/* Detect collisions */
#define	EL_TXC_DCOLL16	0x04	/* Detect collision 16 */
#define	EL_TXC_DSUCCESS	0x08	/* Detect success */

/* Auxiliary status register bits */
#define	EL_AS_RXBUSY	0x01	/* Receive busy */
#define	EL_AS_DMADONE	0x10	/* DMA finished */
#define	EL_AS_TXBUSY	0x80	/* Transmit busy */

/* Auxiliary command register bits */
#define	EL_AC_HOST	0x00	/* System bus can access buffer */
#define	EL_AC_IRQE	0x01	/* IRQ enable */
#define	EL_AC_TXBAD	0x02	/* Transmit frames with bad FCS */
#define	EL_AC_TXFRX	0x04	/* Transmit followed by receive */
#define	EL_AC_RX	0x08	/* Receive */
#define	EL_AC_LB	0x0c	/* Loopback */
#define	EL_AC_DRQ	0x20	/* DMA request */
#define	EL_AC_RIDE	0x40	/* DRQ and IRQ enabled */
#define	EL_AC_RESET	0x80	/* Reset */

/* Packet buffer size */
#define	EL_BUFSIZ	2048
