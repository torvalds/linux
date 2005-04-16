#ifndef _IPT_ADDRTYPE_H
#define _IPT_ADDRTYPE_H

struct ipt_addrtype_info {
	u_int16_t	source;		/* source-type mask */
	u_int16_t	dest;		/* dest-type mask */
	u_int32_t	invert_source;
	u_int32_t	invert_dest;
};

#endif
