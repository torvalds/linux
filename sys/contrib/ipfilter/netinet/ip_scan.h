/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * @(#)ip_fil.h	1.35 6/5/96
 * $Id$
 */

#ifndef __IP_SCAN_H__
#define __IP_SCAN_H__ 1

#ifdef sun
# include <sys/ioccom.h>
#endif

#define	IPSCAN_NAME	"/dev/ipscan"
#define	IPL_SCAN	IPSCAN_NAME
#define	ISC_TLEN	16


struct fr_info;
struct frentry;
struct ip;
struct ipstate;


#if defined(__STDC__) || defined(__GNUC__) || defined(_AIX51)
# define	SIOCADSCA	_IOWR('r', 60, struct ipscan *)
# define	SIOCRMSCA	_IOWR('r', 61, struct ipscan *)
# define	SIOCGSCST	_IOWR('r', 62, struct ipscan *)
#else
# define	SIOCADSCA	_IOWR(r, 60, struct ipscan *)
# define	SIOCRMSCA	_IOWR(r, 61, struct ipscan *)
# define	SIOCGSCST	_IOWR(r, 62, struct ipscan *)
#endif

struct	action	{
	int		act_val;	/* what to do */
	struct	in_addr	act_ip;		/* redirect IP# */
	u_short		act_port;	/* redirect port number */
	int		act_else;	/* what to do */
	struct	in_addr	act_eip;	/* redirect IP# */
	u_short		act_eport;	/* redirect port number */
};


typedef	struct	sinfo {
	char	s_txt[ISC_TLEN];	/* text to match */
	char	s_msk[ISC_TLEN];	/* mask of the above to check */
	int	s_len;			/* length of server text */
} sinfo_t;


typedef	struct	ipscan	{
	struct	ipscan	*ipsc_next;
	struct	ipscan	**ipsc_pnext;
	char		ipsc_tag[ISC_TLEN];	/* table entry protocol tag */
	sinfo_t		ipsc_si[2];	/* client/server side information */
	int		ipsc_hits;	/* times this has been matched */
	int		ipsc_active;	/* # of active matches */
	int		ipsc_fref;	/* # of references from filter rules */
	int		ipsc_sref;	/* # of references from state entries */
	struct	action	ipsc_act;
} ipscan_t;


#define	ipsc_cl		ipsc_si[0]
#define	ipsc_sl		ipsc_si[1]
#define	ipsc_ctxt	ipsc_cl.s_txt
#define	ipsc_cmsk	ipsc_cl.s_msk
#define	ipsc_clen	ipsc_cl.s_len
#define	ipsc_stxt	ipsc_sl.s_txt
#define	ipsc_smsk	ipsc_sl.s_msk
#define	ipsc_slen	ipsc_sl.s_len
#define	ipsc_action	ipsc_act.act_val
#define	ipsc_ip		ipsc_act.act_ip
#define	ipsc_port	ipsc_act.act_port
#define	ipsc_else	ipsc_act.act_else
#define	ipsc_eip	ipsc_act.act_eip
#define	ipsc_eport	ipsc_act.act_eport

#define	ISC_A_NONE	0
#define	ISC_A_TRACK	1
#define	ISC_A_CLOSE	2
#define	ISC_A_REDIRECT	3


typedef	struct	ipscanstat	{
	struct	ipscan	*iscs_list;
	u_long		iscs_acted;
	u_long		iscs_else;
	int		iscs_entries;
} ipscanstat_t;


extern	int ipf_scan_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t, int, int, void *));
extern	int ipf_scan_init __P((void));
extern	int ipf_scan_attachis __P((struct ipstate *));
extern	int ipf_scan_attachfr __P((struct frentry *));
extern	int ipf_scan_detachis __P((struct ipstate *));
extern	int ipf_scan_detachfr __P((struct frentry *));
extern	int ipf_scan_packet __P((struct fr_info *, struct ipstate *));
extern	void ipf_scan_unload __P((ipf_main_softc_t *));

#endif /* __IP_SCAN_H__ */
