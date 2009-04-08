#ifndef _NF_LOG_H
#define _NF_LOG_H

#include <linux/netfilter.h>

/* those NF_LOG_* defines and struct nf_loginfo are legacy definitios that will
 * disappear once iptables is replaced with pkttables.  Please DO NOT use them
 * for any new code! */
#define NF_LOG_TCPSEQ		0x01	/* Log TCP sequence numbers */
#define NF_LOG_TCPOPT		0x02	/* Log TCP options */
#define NF_LOG_IPOPT		0x04	/* Log IP options */
#define NF_LOG_UID		0x08	/* Log UID owning local socket */
#define NF_LOG_MASK		0x0f

#define NF_LOG_TYPE_LOG		0x01
#define NF_LOG_TYPE_ULOG	0x02

struct nf_loginfo {
	u_int8_t type;
	union {
		struct {
			u_int32_t copy_len;
			u_int16_t group;
			u_int16_t qthreshold;
		} ulog;
		struct {
			u_int8_t level;
			u_int8_t logflags;
		} log;
	} u;
};

typedef void nf_logfn(u_int8_t pf,
		      unsigned int hooknum,
		      const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct nf_loginfo *li,
		      const char *prefix);

struct nf_logger {
	struct module	*me;
	nf_logfn 	*logfn;
	char		*name;
	struct list_head	list[NFPROTO_NUMPROTO];
};

/* Function to register/unregister log function. */
int nf_log_register(u_int8_t pf, struct nf_logger *logger);
void nf_log_unregister(struct nf_logger *logger);

int nf_log_bind_pf(u_int8_t pf, const struct nf_logger *logger);
void nf_log_unbind_pf(u_int8_t pf);

/* Calls the registered backend logging function */
void nf_log_packet(u_int8_t pf,
		   unsigned int hooknum,
		   const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   const struct nf_loginfo *li,
		   const char *fmt, ...) __attribute__ ((format(printf,7,8)));

#endif /* _NF_LOG_H */
