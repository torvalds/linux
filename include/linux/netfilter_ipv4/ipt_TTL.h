/* TTL modification module for IP tables
 * (C) 2000 by Harald Welte <laforge@netfilter.org> */

#ifndef _IPT_TTL_H
#define _IPT_TTL_H

enum {
	IPT_TTL_SET = 0,
	IPT_TTL_INC,
	IPT_TTL_DEC
};

#define IPT_TTL_MAXMODE	IPT_TTL_DEC

struct ipt_TTL_info {
	u_int8_t	mode;
	u_int8_t	ttl;
};


#endif
