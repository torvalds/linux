/* Hop Limit modification module for ip6tables
 * Maciej Soltysiak <solt@dns.toxicfilms.tv>
 * Based on HW's TTL module */

#ifndef _IP6T_HL_H
#define _IP6T_HL_H

enum {
	IP6T_HL_SET = 0,
	IP6T_HL_INC,
	IP6T_HL_DEC
};

#define IP6T_HL_MAXMODE	IP6T_HL_DEC

struct ip6t_HL_info {
	u_int8_t	mode;
	u_int8_t	hop_limit;
};


#endif
