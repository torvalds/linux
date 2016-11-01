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

/* This flag indicates that copy_len field in nf_loginfo is set */
#define NF_LOG_F_COPY_LEN	0x1

enum nf_log_type {
	NF_LOG_TYPE_LOG		= 0,
	NF_LOG_TYPE_ULOG,
	NF_LOG_TYPE_MAX
};

struct nf_loginfo {
	u_int8_t type;
	union {
		struct {
			/* copy_len will be used iff you set
			 * NF_LOG_F_COPY_LEN in flags
			 */
			u_int32_t copy_len;
			u_int16_t group;
			u_int16_t qthreshold;
			u_int16_t flags;
		} ulog;
		struct {
			u_int8_t level;
			u_int8_t logflags;
		} log;
	} u;
};

typedef void nf_logfn(struct net *net,
		      u_int8_t pf,
		      unsigned int hooknum,
		      const struct sk_buff *skb,
		      const struct net_device *in,
		      const struct net_device *out,
		      const struct nf_loginfo *li,
		      const char *prefix);

struct nf_logger {
	char			*name;
	enum nf_log_type	type;
	nf_logfn 		*logfn;
	struct module		*me;
};

/* Function to register/unregister log function. */
int nf_log_register(u_int8_t pf, struct nf_logger *logger);
void nf_log_unregister(struct nf_logger *logger);

void nf_log_set(struct net *net, u_int8_t pf,
		const struct nf_logger *logger);
void nf_log_unset(struct net *net, const struct nf_logger *logger);

int nf_log_bind_pf(struct net *net, u_int8_t pf,
		   const struct nf_logger *logger);
void nf_log_unbind_pf(struct net *net, u_int8_t pf);

int nf_logger_find_get(int pf, enum nf_log_type type);
void nf_logger_put(int pf, enum nf_log_type type);
void nf_logger_request_module(int pf, enum nf_log_type type);

#define MODULE_ALIAS_NF_LOGGER(family, type) \
	MODULE_ALIAS("nf-logger-" __stringify(family) "-" __stringify(type))

/* Calls the registered backend logging function */
__printf(8, 9)
void nf_log_packet(struct net *net,
		   u_int8_t pf,
		   unsigned int hooknum,
		   const struct sk_buff *skb,
		   const struct net_device *in,
		   const struct net_device *out,
		   const struct nf_loginfo *li,
		   const char *fmt, ...);

__printf(8, 9)
void nf_log_trace(struct net *net,
		  u_int8_t pf,
		  unsigned int hooknum,
		  const struct sk_buff *skb,
		  const struct net_device *in,
		  const struct net_device *out,
		  const struct nf_loginfo *li,
		  const char *fmt, ...);

struct nf_log_buf;

struct nf_log_buf *nf_log_buf_open(void);
__printf(2, 3) int nf_log_buf_add(struct nf_log_buf *m, const char *f, ...);
void nf_log_buf_close(struct nf_log_buf *m);

/* common logging functions */
int nf_log_dump_udp_header(struct nf_log_buf *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset);
int nf_log_dump_tcp_header(struct nf_log_buf *m, const struct sk_buff *skb,
			   u8 proto, int fragment, unsigned int offset,
			   unsigned int logflags);
void nf_log_dump_sk_uid_gid(struct nf_log_buf *m, struct sock *sk);
void nf_log_dump_packet_common(struct nf_log_buf *m, u_int8_t pf,
			       unsigned int hooknum, const struct sk_buff *skb,
			       const struct net_device *in,
			       const struct net_device *out,
			       const struct nf_loginfo *loginfo,
			       const char *prefix);

#endif /* _NF_LOG_H */
