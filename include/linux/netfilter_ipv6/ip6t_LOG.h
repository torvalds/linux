#ifndef _IP6T_LOG_H
#define _IP6T_LOG_H

#warning "Please update iptables, this file will be removed soon!"

/* make sure not to change this without changing netfilter.h:NF_LOG_* (!) */
#define IP6T_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define IP6T_LOG_TCPOPT		0x02	/* Log TCP options */
#define IP6T_LOG_IPOPT		0x04	/* Log IP options */
#define IP6T_LOG_UID		0x08	/* Log UID owning local socket */
#define IP6T_LOG_NFLOG		0x10	/* Unsupported, don't use */
#define IP6T_LOG_MACDECODE	0x20	/* Decode MAC header */
#define IP6T_LOG_MASK		0x2f

struct ip6t_log_info {
	unsigned char level;
	unsigned char logflags;
	char prefix[30];
};

#endif /*_IPT_LOG_H*/
