#ifndef _IP6T_AH_H
#define _IP6T_AH_H

struct ip6t_ah
{
	u_int32_t spis[2];			/* Security Parameter Index */
	u_int32_t hdrlen;			/* Header Length */
	u_int8_t  hdrres;			/* Test of the Reserved Filed */
	u_int8_t  invflags;			/* Inverse flags */
};

#define IP6T_AH_SPI 0x01
#define IP6T_AH_LEN 0x02
#define IP6T_AH_RES 0x04

/* Values for "invflags" field in struct ip6t_ah. */
#define IP6T_AH_INV_SPI		0x01	/* Invert the sense of spi. */
#define IP6T_AH_INV_LEN		0x02	/* Invert the sense of length. */
#define IP6T_AH_INV_MASK	0x03	/* All possible flags. */

#endif /*_IP6T_AH_H*/
