/* PPTP constants and structs */
#ifndef _NAT_PPTP_H
#define _NAT_PPTP_H

/* conntrack private data */
struct ip_nat_pptp {
	__be16 pns_call_id;		/* NAT'ed PNS call id */
	__be16 pac_call_id;		/* NAT'ed PAC call id */
};

#endif /* _NAT_PPTP_H */
