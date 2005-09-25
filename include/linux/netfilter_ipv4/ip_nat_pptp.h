/* PPTP constants and structs */
#ifndef _NAT_PPTP_H
#define _NAT_PPTP_H

/* conntrack private data */
struct ip_nat_pptp {
	u_int16_t pns_call_id;		/* NAT'ed PNS call id */
	u_int16_t pac_call_id;		/* NAT'ed PAC call id */
};

#endif /* _NAT_PPTP_H */
