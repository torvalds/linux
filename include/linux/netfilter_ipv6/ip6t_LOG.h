#ifndef _IP6T_LOG_H
#define _IP6T_LOG_H

#define IP6T_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define IP6T_LOG_TCPOPT		0x02	/* Log TCP options */
#define IP6T_LOG_IPOPT		0x04	/* Log IP options */
#define IP6T_LOG_UID		0x08	/* Log UID owning local socket */
#define IP6T_LOG_MASK		0x0f

struct ip6t_log_info {
	unsigned char level;
	unsigned char logflags;
	char prefix[30];
};

#endif /*_IPT_LOG_H*/
