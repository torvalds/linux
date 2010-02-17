#ifndef _NF_CONNTRACK_FTP_H
#define _NF_CONNTRACK_FTP_H
/* FTP tracking. */

/* This enum is exposed to userspace */
enum nf_ct_ftp_type {
	/* PORT command from client */
	NF_CT_FTP_PORT,
	/* PASV response from server */
	NF_CT_FTP_PASV,
	/* EPRT command from client */
	NF_CT_FTP_EPRT,
	/* EPSV response from server */
	NF_CT_FTP_EPSV,
};

#ifdef __KERNEL__

#define FTP_PORT	21

#define NUM_SEQ_TO_REMEMBER 2
/* This structure exists only once per master */
struct nf_ct_ftp_master {
	/* Valid seq positions for cmd matching after newline */
	u_int32_t seq_aft_nl[IP_CT_DIR_MAX][NUM_SEQ_TO_REMEMBER];
	/* 0 means seq_match_aft_nl not set */
	int seq_aft_nl_num[IP_CT_DIR_MAX];
};

struct nf_conntrack_expect;

/* For NAT to hook in when we find a packet which describes what other
 * connection we should expect. */
extern unsigned int (*nf_nat_ftp_hook)(struct sk_buff *skb,
				       enum ip_conntrack_info ctinfo,
				       enum nf_ct_ftp_type type,
				       unsigned int matchoff,
				       unsigned int matchlen,
				       struct nf_conntrack_expect *exp);
#endif /* __KERNEL__ */

#endif /* _NF_CONNTRACK_FTP_H */
