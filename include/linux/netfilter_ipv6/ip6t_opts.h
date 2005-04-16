#ifndef _IP6T_OPTS_H
#define _IP6T_OPTS_H

#define IP6T_OPTS_OPTSNR 16

struct ip6t_opts
{
	u_int32_t hdrlen;			/* Header Length */
	u_int8_t flags;				/*  */
	u_int8_t invflags;			/* Inverse flags */
	u_int16_t opts[IP6T_OPTS_OPTSNR];	/* opts */
	u_int8_t optsnr;			/* Nr of OPts */
};

#define IP6T_OPTS_LEN 		0x01
#define IP6T_OPTS_OPTS 		0x02
#define IP6T_OPTS_NSTRICT	0x04

/* Values for "invflags" field in struct ip6t_rt. */
#define IP6T_OPTS_INV_LEN	0x01	/* Invert the sense of length. */
#define IP6T_OPTS_INV_MASK	0x01	/* All possible flags. */

#define MASK_HOPOPTS    128
#define MASK_DSTOPTS    64
#define MASK_ROUTING    32
#define MASK_FRAGMENT   16
#define MASK_AH         8
#define MASK_ESP        4
#define MASK_NONE       2
#define MASK_PROTO      1

#endif /*_IP6T_OPTS_H*/
