/*	$OpenBSD: isadmareg.h,v 1.5 1998/01/20 18:40:30 niklas Exp $	*/
/*	$NetBSD: isadmareg.h,v 1.4 1995/06/28 04:31:48 cgd Exp $	*/

#include <dev/ic/i8237reg.h>

/*
 * Register definitions for DMA controller 1 (channels 0..3):
 */
#define	DMA1_CHN(c)	(1*(2*(c)))	/* addr reg for channel c */
#define	DMA1_SR		(1*8)		/* status register */
#define	DMA1_SMSK	(1*10)	/* single mask register */
#define	DMA1_MODE	(1*11)	/* mode register */
#define	DMA1_FFC	(1*12)	/* clear first/last FF */

#define DMA1_IOSIZE	(1*12)

/*
 * Register definitions for DMA controller 2 (channels 4..7):
 */
#define	DMA2_CHN(c)	(2*(2*(c)))	/* addr reg for channel c */
#define	DMA2_SR		(2*8)		/* status register */
#define	DMA2_SMSK	(2*10)	/* single mask register */
#define	DMA2_MODE	(2*11)	/* mode register */
#define	DMA2_FFC	(2*12)	/* clear first/last FF */

#define DMA2_IOSIZE	(2*12)
