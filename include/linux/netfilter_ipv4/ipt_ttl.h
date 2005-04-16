/* IP tables module for matching the value of the TTL
 * (C) 2000 by Harald Welte <laforge@gnumonks.org> */

#ifndef _IPT_TTL_H
#define _IPT_TTL_H

enum {
	IPT_TTL_EQ = 0,		/* equals */
	IPT_TTL_NE,		/* not equals */
	IPT_TTL_LT,		/* less than */
	IPT_TTL_GT,		/* greater than */
};


struct ipt_ttl_info {
	u_int8_t	mode;
	u_int8_t	ttl;
};


#endif
