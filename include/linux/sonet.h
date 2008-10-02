/* sonet.h - SONET/SHD physical layer control */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
 

#ifndef LINUX_SONET_H
#define LINUX_SONET_H

#define __SONET_ITEMS \
    __HANDLE_ITEM(section_bip); 	/* section parity errors (B1) */ \
    __HANDLE_ITEM(line_bip);		/* line parity errors (B2) */ \
    __HANDLE_ITEM(path_bip);		/* path parity errors (B3) */ \
    __HANDLE_ITEM(line_febe);		/* line parity errors at remote */ \
    __HANDLE_ITEM(path_febe);		/* path parity errors at remote */ \
    __HANDLE_ITEM(corr_hcs);		/* correctable header errors */ \
    __HANDLE_ITEM(uncorr_hcs);		/* uncorrectable header errors */ \
    __HANDLE_ITEM(tx_cells);		/* cells sent */ \
    __HANDLE_ITEM(rx_cells);		/* cells received */

struct sonet_stats {
#define __HANDLE_ITEM(i) int i
	__SONET_ITEMS
#undef __HANDLE_ITEM
} __attribute__ ((packed));


#define SONET_GETSTAT	_IOR('a',ATMIOC_PHYTYP,struct sonet_stats)
					/* get statistics */
#define SONET_GETSTATZ	_IOR('a',ATMIOC_PHYTYP+1,struct sonet_stats)
					/* ... and zero counters */
#define SONET_SETDIAG	_IOWR('a',ATMIOC_PHYTYP+2,int)
					/* set error insertion */
#define SONET_CLRDIAG	_IOWR('a',ATMIOC_PHYTYP+3,int)
					/* clear error insertion */
#define SONET_GETDIAG	_IOR('a',ATMIOC_PHYTYP+4,int)
					/* query error insertion */
#define SONET_SETFRAMING _IOW('a',ATMIOC_PHYTYP+5,int)
					/* set framing mode (SONET/SDH) */
#define SONET_GETFRAMING _IOR('a',ATMIOC_PHYTYP+6,int)
					/* get framing mode */
#define SONET_GETFRSENSE _IOR('a',ATMIOC_PHYTYP+7, \
  unsigned char[SONET_FRSENSE_SIZE])	/* get framing sense information */

#define SONET_INS_SBIP	  1		/* section BIP */
#define SONET_INS_LBIP	  2		/* line BIP */
#define SONET_INS_PBIP	  4		/* path BIP */
#define SONET_INS_FRAME	  8		/* out of frame */
#define SONET_INS_LOS	 16		/* set line to zero */
#define SONET_INS_LAIS	 32		/* line alarm indication signal */
#define SONET_INS_PAIS	 64		/* path alarm indication signal */
#define SONET_INS_HCS	128		/* insert HCS error */

#define SONET_FRAME_SONET 0		/* SONET STS-3 framing */
#define SONET_FRAME_SDH   1		/* SDH STM-1 framing */

#define SONET_FRSENSE_SIZE 6		/* C1[3],H1[3] (0xff for unknown) */


#ifdef __KERNEL__

#include <asm/atomic.h>

struct k_sonet_stats {
#define __HANDLE_ITEM(i) atomic_t i
	__SONET_ITEMS
#undef __HANDLE_ITEM
};

extern void sonet_copy_stats(struct k_sonet_stats *from,struct sonet_stats *to);
extern void sonet_subtract_stats(struct k_sonet_stats *from,
    struct sonet_stats *to);

#endif

#endif
