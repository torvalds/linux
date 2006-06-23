#ifndef __IP_CONNTRACK_SIP_H__
#define __IP_CONNTRACK_SIP_H__
#ifdef __KERNEL__

#define SIP_PORT	5060
#define SIP_TIMEOUT	3600

#define POS_VIA		0
#define POS_CONTACT	1
#define POS_CONTENT	2
#define POS_MEDIA	3
#define POS_OWNER	4
#define POS_CONNECTION	5
#define POS_REQ_HEADER	6
#define POS_SDP_HEADER	7

struct sip_header_nfo {
	const char	*lname;
	const char	*sname;
	const char	*ln_str;
	size_t		lnlen;
	size_t		snlen;
	size_t		ln_strlen;
	int		(*match_len)(const char *, const char *, int *);
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
			   struct sip_header_nfo *hnfo);
extern int ct_sip_lnlen(const char *line, const char *limit);
extern const char *ct_sip_search(const char *needle, const char *haystack,
                                 size_t needle_len, size_t haystack_len);
#endif /* __KERNEL__ */
#endif /* __IP_CONNTRACK_SIP_H__ */
