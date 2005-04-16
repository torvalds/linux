#ifndef _IPT_AH_H
#define _IPT_AH_H

struct ipt_ah
{
	u_int32_t spis[2];			/* Security Parameter Index */
	u_int8_t  invflags;			/* Inverse flags */
};



/* Values for "invflags" field in struct ipt_ah. */
#define IPT_AH_INV_SPI		0x01	/* Invert the sense of spi. */
#define IPT_AH_INV_MASK	0x01	/* All possible flags. */

#endif /*_IPT_AH_H*/
