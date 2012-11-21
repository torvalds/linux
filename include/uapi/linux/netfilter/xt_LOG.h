#ifndef _XT_LOG_H
#define _XT_LOG_H

/* make sure not to change this without changing nf_log.h:NF_LOG_* (!) */
#define XT_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define XT_LOG_TCPOPT		0x02	/* Log TCP options */
#define XT_LOG_IPOPT		0x04	/* Log IP options */
#define XT_LOG_UID		0x08	/* Log UID owning local socket */
#define XT_LOG_NFLOG		0x10	/* Unsupported, don't reuse */
#define XT_LOG_MACDECODE	0x20	/* Decode MAC header */
#define XT_LOG_MASK		0x2f

struct xt_log_info {
	unsigned char level;
	unsigned char logflags;
	char prefix[30];
};

#endif /* _XT_LOG_H */
