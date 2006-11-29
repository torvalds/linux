#ifndef __IP_CONNTRACK_SIP_H__
#define __IP_CONNTRACK_SIP_H__
#ifdef __KERNEL__

#define SIP_PORT	5060
#define SIP_TIMEOUT	3600

enum sip_header_pos {
	POS_REQ_HEADER,
	POS_VIA,
	POS_CONTACT,
	POS_CONTENT,
	POS_MEDIA,
	POS_OWNER,
	POS_CONNECTION,
	POS_SDP_HEADER,
};

extern unsigned int (*ip_nat_sip_hook)(struct sk_buff **pskb,
				       enum ip_conntrack_info ctinfo,
				       struct ip_conntrack *ct,
				       const char **dptr);
extern unsigned int (*ip_nat_sdp_hook)(struct sk_buff **pskb,
				       enum ip_conntrack_info ctinfo,
				       struct ip_conntrack_expect *exp,
				       const char *dptr);

extern int ct_sip_get_info(const char *dptr, size_t dlen,
			   unsigned int *matchoff,
			   unsigned int *matchlen,
			   enum sip_header_pos pos);
extern int ct_sip_lnlen(const char *line, const char *limit);
extern const char *ct_sip_search(const char *needle, const char *haystack,
				 size_t needle_len, size_t haystack_len,
				 int case_sensitive);
#endif /* __KERNEL__ */
#endif /* __IP_CONNTRACK_SIP_H__ */
