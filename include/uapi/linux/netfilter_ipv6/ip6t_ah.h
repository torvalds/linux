/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _IP6T_AH_H
#define _IP6T_AH_H

#include <linux/types.h>

struct ip6t_ah {
	__u32 spis[2];			/* Security Parameter Index */
	__u32 hdrlen;			/* Header Length */
	__u8  hdrres;			/* Test of the Reserved Filed */
	__u8  invflags;			/* Inverse flags */
};

#define IP6T_AH_SPI 0x01
#define IP6T_AH_LEN 0x02
#define IP6T_AH_RES 0x04

/* Values for "invflags" field in struct ip6t_ah. */
#define IP6T_AH_INV_SPI		0x01	/* Invert the sense of spi. */
#define IP6T_AH_INV_LEN		0x02	/* Invert the sense of length. */
#define IP6T_AH_INV_MASK	0x03	/* All possible flags. */

#endif /*_IP6T_AH_H*/
