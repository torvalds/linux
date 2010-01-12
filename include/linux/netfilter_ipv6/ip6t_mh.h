#ifndef _IP6T_MH_H
#define _IP6T_MH_H

/* MH matching stuff */
struct ip6t_mh {
	u_int8_t types[2];	/* MH type range */
	u_int8_t invflags;	/* Inverse flags */
};

/* Values for "invflags" field in struct ip6t_mh. */
#define IP6T_MH_INV_TYPE	0x01	/* Invert the sense of type. */
#define IP6T_MH_INV_MASK	0x01	/* All possible flags. */

#endif /*_IP6T_MH_H*/
