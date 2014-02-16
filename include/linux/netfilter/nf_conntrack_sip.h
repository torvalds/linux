#ifndef __NF_CONNTRACK_SIP_H__
#define __NF_CONNTRACK_SIP_H__
#ifdef __KERNEL__

#include <net/netfilter/nf_conntrack_expect.h>

#include <linux/types.h>

#define SIP_PORT	5060
#define SIP_TIMEOUT	3600

struct nf_ct_sip_master {
	unsigned int	register_cseq;
	unsigned int	invite_cseq;
	__be16		forced_dport;
};

enum sip_expectation_classes {
	SIP_EXPECT_SIGNALLING,
	SIP_EXPECT_AUDIO,
	SIP_EXPECT_VIDEO,
	SIP_EXPECT_IMAGE,
	__SIP_EXPECT_MAX
};
#define SIP_EXPECT_MAX	(__SIP_EXPECT_MAX - 1)

struct sdp_media_type {
	const char			*name;
	unsigned int			len;
	enum sip_expectation_classes	class;
};

#define SDP_MEDIA_TYPE(__name, __class)					\
{									\
	.name	= (__name),						\
	.len	= sizeof(__name) - 1,					\
	.class	= (__class),						\
}

struct sip_handler {
	const char	*method;
	unsigned int	len;
	int		(*request)(struct sk_buff *skb, unsigned int protoff,
				   unsigned int dataoff,
				   const char **dptr, unsigned int *datalen,
				   unsigned int cseq);
	int		(*response)(struct sk_buff *skb, unsigned int protoff,
				    unsigned int dataoff,
				    const char **dptr, unsigned int *datalen,
				    unsigned int cseq, unsigned int code);
};

#define SIP_HANDLER(__method, __request, __response)			\
{									\
	.method		= (__method),					\
	.len		= sizeof(__method) - 1,				\
	.request	= (__request),					\
	.response	= (__response),					\
}

struct sip_header {
	const char	*name;
	const char	*cname;
	const char	*search;
	unsigned int	len;
	unsigned int	clen;
	unsigned int	slen;
	int		(*match_len)(const struct nf_conn *ct,
				     const char *dptr, const char *limit,
				     int *shift);
};

#define __SIP_HDR(__name, __cname, __search, __match)			\
{									\
	.name		= (__name),					\
	.len		= sizeof(__name) - 1,				\
	.cname		= (__cname),					\
	.clen		= (__cname) ? sizeof(__cname) - 1 : 0,		\
	.search		= (__search),					\
	.slen		= (__search) ? sizeof(__search) - 1 : 0,	\
	.match_len	= (__match),					\
}

#define SIP_HDR(__name, __cname, __search, __match) \
	__SIP_HDR(__name, __cname, __search, __match)

#define SDP_HDR(__name, __search, __match) \
	__SIP_HDR(__name, NULL, __search, __match)

enum sip_header_types {
	SIP_HDR_CSEQ,
	SIP_HDR_FROM,
	SIP_HDR_TO,
	SIP_HDR_CONTACT,
	SIP_HDR_VIA_UDP,
	SIP_HDR_VIA_TCP,
	SIP_HDR_EXPIRES,
	SIP_HDR_CONTENT_LENGTH,
	SIP_HDR_CALL_ID,
};

enum sdp_header_types {
	SDP_HDR_UNSPEC,
	SDP_HDR_VERSION,
	SDP_HDR_OWNER,
	SDP_HDR_CONNECTION,
	SDP_HDR_MEDIA,
};

struct nf_nat_sip_hooks {
	unsigned int (*msg)(struct sk_buff *skb,
			    unsigned int protoff,
			    unsigned int dataoff,
			    const char **dptr,
			    unsigned int *datalen);

	void (*seq_adjust)(struct sk_buff *skb,
			   unsigned int protoff, s16 off);

	unsigned int (*expect)(struct sk_buff *skb,
			       unsigned int protoff,
			       unsigned int dataoff,
			       const char **dptr,
			       unsigned int *datalen,
			       struct nf_conntrack_expect *exp,
			       unsigned int matchoff,
			       unsigned int matchlen);

	unsigned int (*sdp_addr)(struct sk_buff *skb,
				 unsigned int protoff,
				 unsigned int dataoff,
				 const char **dptr,
				 unsigned int *datalen,
				 unsigned int sdpoff,
				 enum sdp_header_types type,
				 enum sdp_header_types term,
				 const union nf_inet_addr *addr);

	unsigned int (*sdp_port)(struct sk_buff *skb,
				 unsigned int protoff,
				 unsigned int dataoff,
				 const char **dptr,
				 unsigned int *datalen,
				 unsigned int matchoff,
				 unsigned int matchlen,
				 u_int16_t port);

	unsigned int (*sdp_session)(struct sk_buff *skb,
				    unsigned int protoff,
				    unsigned int dataoff,
				    const char **dptr,
				    unsigned int *datalen,
				    unsigned int sdpoff,
				    const union nf_inet_addr *addr);

	unsigned int (*sdp_media)(struct sk_buff *skb,
				  unsigned int protoff,
				  unsigned int dataoff,
				  const char **dptr,
				  unsigned int *datalen,
				  struct nf_conntrack_expect *rtp_exp,
				  struct nf_conntrack_expect *rtcp_exp,
				  unsigned int mediaoff,
				  unsigned int medialen,
				  union nf_inet_addr *rtp_addr);
};
extern const struct nf_nat_sip_hooks *nf_nat_sip_hooks;

int ct_sip_parse_request(const struct nf_conn *ct, const char *dptr,
			 unsigned int datalen, unsigned int *matchoff,
			 unsigned int *matchlen, union nf_inet_addr *addr,
			 __be16 *port);
int ct_sip_get_header(const struct nf_conn *ct, const char *dptr,
		      unsigned int dataoff, unsigned int datalen,
		      enum sip_header_types type, unsigned int *matchoff,
		      unsigned int *matchlen);
int ct_sip_parse_header_uri(const struct nf_conn *ct, const char *dptr,
			    unsigned int *dataoff, unsigned int datalen,
			    enum sip_header_types type, int *in_header,
			    unsigned int *matchoff, unsigned int *matchlen,
			    union nf_inet_addr *addr, __be16 *port);
int ct_sip_parse_address_param(const struct nf_conn *ct, const char *dptr,
			       unsigned int dataoff, unsigned int datalen,
			       const char *name, unsigned int *matchoff,
			       unsigned int *matchlen, union nf_inet_addr *addr,
			       bool delim);
int ct_sip_parse_numerical_param(const struct nf_conn *ct, const char *dptr,
				 unsigned int off, unsigned int datalen,
				 const char *name, unsigned int *matchoff,
				 unsigned int *matchen, unsigned int *val);

int ct_sip_get_sdp_header(const struct nf_conn *ct, const char *dptr,
			  unsigned int dataoff, unsigned int datalen,
			  enum sdp_header_types type,
			  enum sdp_header_types term,
			  unsigned int *matchoff, unsigned int *matchlen);

#endif /* __KERNEL__ */
#endif /* __NF_CONNTRACK_SIP_H__ */
