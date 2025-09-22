/*	$OpenBSD: i8237reg.h,v 1.3 1999/08/04 23:07:49 niklas Exp $	*/
/*	$NetBSD: i8237reg.h,v 1.5 1996/03/01 22:27:09 mycroft Exp $	*/

/*
 * Intel 8237 DMA Controller
 */

#define	DMA37MD_DEMAND	0x00	/* demand mode */
#define	DMA37MD_WRITE	0x04	/* read the device, write memory operation */
#define	DMA37MD_READ	0x08	/* write the device, read memory operation */
#define	DMA37MD_LOOP	0x10	/* auto-initialize mode */
#define	DMA37MD_SINGLE	0x40	/* single pass mode */
#define	DMA37MD_CASCADE	0xc0	/* cascade mode */
	
#define	DMA37SM_CLEAR	0x00	/* clear mask bit */
#define	DMA37SM_SET	0x04	/* set mask bit */
