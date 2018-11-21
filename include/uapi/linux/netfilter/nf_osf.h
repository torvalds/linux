#ifndef _NF_OSF_H
#define _NF_OSF_H

#include <linux/types.h>

#define MAXGENRELEN	32

#define NF_OSF_GENRE	(1 << 0)
#define NF_OSF_TTL	(1 << 1)
#define NF_OSF_LOG	(1 << 2)
#define NF_OSF_INVERT	(1 << 3)

#define NF_OSF_LOGLEVEL_ALL		0	/* log all matched fingerprints */
#define NF_OSF_LOGLEVEL_FIRST		1	/* log only the first matced fingerprint */
#define NF_OSF_LOGLEVEL_ALL_KNOWN	2	/* do not log unknown packets */

#define NF_OSF_TTL_TRUE			0	/* True ip and fingerprint TTL comparison */

/* Do not compare ip and fingerprint TTL at all */
#define NF_OSF_TTL_NOCHECK		2

/* Wildcard MSS (kind of).
 * It is used to implement a state machine for the different wildcard values
 * of the MSS and window sizes.
 */
struct nf_osf_wc {
	__u32	wc;
	__u32	val;
};

/* This struct represents IANA options
 * http://www.iana.org/assignments/tcp-parameters
 */
struct nf_osf_opt {
	__u16			kind, length;
	struct nf_osf_wc	wc;
};

struct nf_osf_info {
	char	genre[MAXGENRELEN];
	__u32	len;
	__u32	flags;
	__u32	loglevel;
	__u32	ttl;
};

struct nf_osf_user_finger {
	struct nf_osf_wc	wss;

	__u8	ttl, df;
	__u16	ss, mss;
	__u16	opt_num;

	char	genre[MAXGENRELEN];
	char	version[MAXGENRELEN];
	char	subtype[MAXGENRELEN];

	/* MAX_IPOPTLEN is maximum if all options are NOPs or EOLs */
	struct nf_osf_opt	opt[MAX_IPOPTLEN];
};

struct nf_osf_nlmsg {
	struct nf_osf_user_finger	f;
	struct iphdr			ip;
	struct tcphdr			tcp;
};

/* Defines for IANA option kinds */
enum iana_options {
	OSFOPT_EOL = 0,		/* End of options */
	OSFOPT_NOP,		/* NOP */
	OSFOPT_MSS,		/* Maximum segment size */
	OSFOPT_WSO,		/* Window scale option */
	OSFOPT_SACKP,		/* SACK permitted */
	OSFOPT_SACK,		/* SACK */
	OSFOPT_ECHO,
	OSFOPT_ECHOREPLY,
	OSFOPT_TS,		/* Timestamp option */
	OSFOPT_POCP,		/* Partial Order Connection Permitted */
	OSFOPT_POSP,		/* Partial Order Service Profile */

	/* Others are not used in the current OSF */
	OSFOPT_EMPTY = 255,
};

#endif /* _NF_OSF_H */
