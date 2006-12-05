/****************************************************************************/

/*
 *      mcfmbus.h -- Coldfire MBUS support defines.
 *
 *      (C) Copyright 1999, Martin Floeer (mfloeer@axcent.de) 
 */

/****************************************************************************/


#ifndef mcfmbus_h
#define mcfmbus_h


#define MCFMBUS_BASE		0x280
#define MCFMBUS_IRQ_VECTOR	0x19
#define MCFMBUS_IRQ		0x1
#define MCFMBUS_CLK		0x3f
#define MCFMBUS_IRQ_LEVEL	0x07	/*IRQ Level 1*/
#define MCFMBUS_ADDRESS		0x01


/*
*	Define the 5307 MBUS register set addresses
*/

#define MCFMBUS_MADR	0x00
#define MCFMBUS_MFDR	0x04
#define MCFMBUS_MBCR	0x08
#define MCFMBUS_MBSR	0x0C
#define MCFMBUS_MBDR	0x10


#define MCFMBUS_MADR_ADDR(a)	(((a)&0x7F)<<0x01) /*Slave Address*/

#define MCFMBUS_MFDR_MBC(a)	((a)&0x3F)	   /*M-Bus Clock*/

/*
*	Define bit flags in Control Register
*/

#define MCFMBUS_MBCR_MEN           (0x80)  /* M-Bus Enable                 */
#define MCFMBUS_MBCR_MIEN          (0x40)  /* M-Bus Interrupt Enable       */
#define MCFMBUS_MBCR_MSTA          (0x20)  /* Master/Slave Mode Select Bit */
#define MCFMBUS_MBCR_MTX           (0x10)  /* Transmit/Rcv Mode Select Bit */
#define MCFMBUS_MBCR_TXAK          (0x08)  /* Transmit Acknowledge Enable  */
#define MCFMBUS_MBCR_RSTA          (0x04)  /* Repeat Start                 */

/*
*	Define bit flags in Status Register
*/

#define MCFMBUS_MBSR_MCF           (0x80)  /* Data Transfer Complete       */
#define MCFMBUS_MBSR_MAAS          (0x40)  /* Addressed as a Slave         */
#define MCFMBUS_MBSR_MBB           (0x20)  /* Bus Busy                     */
#define MCFMBUS_MBSR_MAL           (0x10)  /* Arbitration Lost             */
#define MCFMBUS_MBSR_SRW           (0x04)  /* Slave Transmit               */
#define MCFMBUS_MBSR_MIF           (0x02)  /* M-Bus Interrupt              */
#define MCFMBUS_MBSR_RXAK          (0x01)  /* No Acknowledge Received      */

/*
*	Define bit flags in DATA I/O Register
*/

#define MCFMBUS_MBDR_READ          (0x01)  /* 1=read 0=write MBUS */

#define MBUSIOCSCLOCK		1
#define MBUSIOCGCLOCK		2
#define MBUSIOCSADDR			3
#define MBUSIOCGADDR			4
#define MBUSIOCSSLADDR			5
#define MBUSIOCGSLADDR			6
#define MBUSIOCSSUBADDR			7
#define MBUSIOCGSUBADDR			8

#endif
