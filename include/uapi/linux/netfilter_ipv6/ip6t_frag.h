#ifndef _IP6T_FRAG_H
#define _IP6T_FRAG_H

#include <linux/types.h>

struct ip6t_frag {
	__u32 ids[2];			/* Identification range */
	__u32 hdrlen;			/* Header Length */
	__u8  flags;			/* Flags */
	__u8  invflags;			/* Inverse flags */
};

#define IP6T_FRAG_IDS 		0x01
#define IP6T_FRAG_LEN 		0x02
#define IP6T_FRAG_RES 		0x04
#define IP6T_FRAG_FST 		0x08
#define IP6T_FRAG_MF  		0x10
#define IP6T_FRAG_NMF  		0x20

/* Values for "invflags" field in struct ip6t_frag. */
#define IP6T_FRAG_INV_IDS	0x01	/* Invert the sense of ids. */
#define IP6T_FRAG_INV_LEN	0x02	/* Invert the sense of length. */
#define IP6T_FRAG_INV_MASK	0x03	/* All possible flags. */

#endif /*_IP6T_FRAG_H*/
