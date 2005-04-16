/****************************************************************************/

/*
 *	anchor.h -- Anchor CO-MEM Lite PCI host bridge part.
 *
 *	(C) Copyright 2000, Moreton Bay (www.moreton.com.au)
 */

/****************************************************************************/
#ifndef	anchor_h
#define	anchor_h
/****************************************************************************/

/*
 *	Define basic addressing info.
 */
#if defined(CONFIG_MOTOROLA) && defined(CONFIG_M5407)
#define	COMEM_BASE	0xFFFF0000	/* Base of CO-MEM address space */
#define	COMEM_IRQ	25		/* IRQ of anchor part */
#else
#define	COMEM_BASE	0x80000000	/* Base of CO-MEM address space */
#define	COMEM_IRQ	25		/* IRQ of anchor part */
#endif

/****************************************************************************/

/*
 *	4-byte registers of CO-MEM, so adjust register addresses for
 *	easy access. Handy macro for word access too.
 */
#define	LREG(a)		((a) >> 2)
#define	WREG(a)		((a) >> 1)


/*
 *	Define base addresses within CO-MEM Lite register address space.
 */
#define	COMEM_I2O	0x0000		/* I2O registers */
#define	COMEM_OPREGS	0x0400		/* Operation registers */
#define	COMEM_PCIBUS	0x2000		/* Direct access to PCI bus */
#define	COMEM_SHMEM	0x4000		/* Shared memory region */

#define	COMEM_SHMEMSIZE	0x4000		/* Size of shared memory */


/*
 *	Define CO-MEM Registers.
 */
#define	COMEM_I2OHISR	0x0030		/* I2O host interrupt status */
#define	COMEM_I2OHIMR	0x0034		/* I2O host interrupt mask */
#define	COMEM_I2OLISR	0x0038		/* I2O local interrupt status */
#define	COMEM_I2OLIMR	0x003c		/* I2O local interrupt mask */
#define	COMEM_IBFPFIFO	0x0040		/* I2O inbound free/post FIFO */
#define	COMEM_OBPFFIFO	0x0044		/* I2O outbound post/free FIFO */
#define	COMEM_IBPFFIFO	0x0048		/* I2O inbound post/free FIFO */
#define	COMEM_OBFPFIFO	0x004c		/* I2O outbound free/post FIFO */

#define	COMEM_DAHBASE	0x0460		/* Direct access base address */

#define	COMEM_NVCMD	0x04a0		/* I2C serial command */
#define	COMEM_NVREAD	0x04a4		/* I2C serial read */
#define	COMEM_NVSTAT	0x04a8		/* I2C status */

#define	COMEM_DMALBASE	0x04b0		/* DMA local base address */
#define	COMEM_DMAHBASE	0x04b4		/* DMA host base address */
#define	COMEM_DMASIZE	0x04b8		/* DMA size */
#define	COMEM_DMACTL	0x04bc		/* DMA control */

#define	COMEM_HCTL	0x04e0		/* Host control */
#define	COMEM_HINT	0x04e4		/* Host interrupt control/status */
#define	COMEM_HLDATA	0x04e8		/* Host to local data mailbox */
#define	COMEM_LINT	0x04f4		/* Local interrupt contole status */
#define	COMEM_LHDATA	0x04f8		/* Local to host data mailbox */

#define	COMEM_LBUSCFG	0x04fc		/* Local bus configuration */


/*
 *	Commands and flags for use with Direct Access Register.
 */
#define	COMEM_DA_IACK	0x00000000	/* Interrupt acknowledge (read) */
#define	COMEM_DA_SPCL	0x00000010	/* Special cycle (write) */
#define	COMEM_DA_MEMRD	0x00000004	/* Memory read cycle */
#define	COMEM_DA_MEMWR	0x00000004	/* Memory write cycle */
#define	COMEM_DA_IORD	0x00000002	/* I/O read cycle */
#define	COMEM_DA_IOWR	0x00000002	/* I/O write cycle */
#define	COMEM_DA_CFGRD	0x00000006	/* Configuration read cycle */
#define	COMEM_DA_CFGWR	0x00000006	/* Configuration write cycle */

#define	COMEM_DA_ADDR(a)	((a) & 0xffffe000)

#define	COMEM_DA_OFFSET(a)	((a) & 0x00001fff)


/*
 *	The PCI bus will be limited in what slots will actually be used.
 *	Define valid device numbers for different boards.
 */
#if defined(CONFIG_MOTOROLA) && defined(CONFIG_M5407)
#define	COMEM_MINDEV	14		/* Minimum valid DEVICE */
#define	COMEM_MAXDEV	14		/* Maximum valid DEVICE */
#define	COMEM_BRIDGEDEV	15		/* Slot bridge is in */
#else
#define	COMEM_MINDEV	0		/* Minimum valid DEVICE */
#define	COMEM_MAXDEV	3		/* Maximum valid DEVICE */
#endif

#define	COMEM_MAXPCI	(COMEM_MAXDEV+1)	/* Maximum PCI devices */


/****************************************************************************/
#endif	/* anchor_h */
