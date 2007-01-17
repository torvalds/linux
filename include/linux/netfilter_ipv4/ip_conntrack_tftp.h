#ifndef _IP_CT_TFTP
#define _IP_CT_TFTP

#define TFTP_PORT 69

struct tftphdr {
	__be16 opcode;
};

#define TFTP_OPCODE_READ	1
#define TFTP_OPCODE_WRITE	2
#define TFTP_OPCODE_DATA	3
#define TFTP_OPCODE_ACK		4
#define TFTP_OPCODE_ERROR	5

extern unsigned int (*ip_nat_tftp_hook)(struct sk_buff **pskb,
				 enum ip_conntrack_info ctinfo,
				 struct ip_conntrack_expect *exp);

#endif /* _IP_CT_TFTP */
