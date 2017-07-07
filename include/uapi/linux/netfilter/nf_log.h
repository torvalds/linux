#ifndef _NETFILTER_NF_LOG_H
#define _NETFILTER_NF_LOG_H

#define NF_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define NF_LOG_TCPOPT		0x02	/* Log TCP options */
#define NF_LOG_IPOPT		0x04	/* Log IP options */
#define NF_LOG_UID		0x08	/* Log UID owning local socket */
#define NF_LOG_NFLOG		0x10	/* Unsupported, don't reuse */
#define NF_LOG_MACDECODE	0x20	/* Decode MAC header */
#define NF_LOG_MASK		0x2f

#define NF_LOG_PREFIXLEN	128

#endif /* _NETFILTER_NF_LOG_H */
