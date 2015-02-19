#ifndef _XT_IPCOMP_H
#define _XT_IPCOMP_H

#include <linux/types.h>

struct xt_ipcomp {
	__u32 spis[2];	/* Security Parameter Index */
	__u8 invflags;	/* Inverse flags */
	__u8 hdrres;	/* Test of the Reserved Filed */
};

/* Values for "invflags" field in struct xt_ipcomp. */
#define XT_IPCOMP_INV_SPI	0x01	/* Invert the sense of spi. */
#define XT_IPCOMP_INV_MASK	0x01	/* All possible flags. */

#endif /*_XT_IPCOMP_H*/
