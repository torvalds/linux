#ifndef __NF_CONNTRACK_SIP_H__
#define __NF_CONNTRACK_SIP_H__
#ifdef __KERNEL__

#define SIP_PORT	5060
#define SIP_TIMEOUT	3600

enum sip_header_pos {
	POS_REG_REQ_URI,
	POS_REQ_URI,
	POS_FROM,
	POS_TO,
	POS_VIA,
	POS_CONTACT,
	POS_CONTENT,
	POS_MEDIA,
	POS_OWNER_IP4,
	POS_CONNECTION_IP4,
	POS_OWNER_IP6,
	POS_CONNECTION_IP6,
	POS_SDP_HEADER,
};

extern unsigned int (*nf_nat_sip_hook)(struct sk_buff **pskb,
				       enum ip_conntrack_info ctinfo,
				       struct nf_conn *ct,
				       const char **dptr);
extern unsigned int (*nf_nat_sdp_hook)(struct sk_buff **pskb,
				       enum ip_conntrack_info ctinfo,
				       struct nf_conntrack_expect *exp,
				       const char *dptr);

extern int ct_sip_get_info(struct nf_conn *ct, const char *dptr, size_t dlen,
			   unsigned int *matchoff, unsigned int *matchlen,
			   enum sip_header_pos pos);
extern int ct_sip_lnlen(const char *line, const char *limit);
extern const char *ct_sip_search(const char *needle, const char *haystack,
				 size_t needle_len, size_t haystack_len,
				 int case_sensitive);
#endif /* __KERNEL__ */
#endif /* __NF_CONNTRACK_SIP_H__ */
