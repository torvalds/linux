/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $FreeBSD$
 * Id: ip_auth.h,v 2.16.2.2 2006/03/16 06:45:49 darrenr Exp $
 *
 */
#ifndef	__IP_AUTH_H__
#define	__IP_AUTH_H__

#define FR_NUMAUTH      32

typedef struct  frauth {
	int	fra_age;
	int	fra_len;
	int	fra_index;
	u_32_t	fra_pass;
	fr_info_t	fra_info;
	char	*fra_buf;
	u_32_t	fra_flx;
#ifdef	MENTAT
	queue_t	*fra_q;
	mb_t	*fra_m;
#endif
} frauth_t;

typedef	struct	frauthent  {
	struct	frentry	fae_fr;
	struct	frauthent	*fae_next;
	struct	frauthent	**fae_pnext;
	u_long	fae_age;
	int	fae_ref;
} frauthent_t;

typedef struct  ipf_authstat {
	U_QUAD_T	fas_hits;
	U_QUAD_T	fas_miss;
	u_long		fas_nospace;
	u_long		fas_added;
	u_long		fas_sendfail;
	u_long		fas_sendok;
	u_long		fas_queok;
	u_long		fas_quefail;
	u_long		fas_expire;
	frauthent_t	*fas_faelist;
} ipf_authstat_t;

typedef	struct ipf_auth_softc_s {
	ipfrwlock_t	ipf_authlk;
	ipfmutex_t	ipf_auth_mx;
	int		ipf_auth_size;
	int		ipf_auth_used;
	int		ipf_auth_replies;
	int		ipf_auth_defaultage;
	int		ipf_auth_lock;
	ipf_authstat_t	ipf_auth_stats;
	frauth_t	*ipf_auth;
	mb_t		**ipf_auth_pkts;
	int		ipf_auth_start;
	int		ipf_auth_end;
	int		ipf_auth_next;
	frauthent_t	*ipf_auth_entries;
	frentry_t	*ipf_auth_ip;
	frentry_t	*ipf_auth_rules;
} ipf_auth_softc_t;

extern	frentry_t *ipf_auth_check __P((fr_info_t *, u_32_t *));
extern	void	ipf_auth_expire __P((ipf_main_softc_t *));
extern	int	ipf_auth_ioctl __P((ipf_main_softc_t *, caddr_t, ioctlcmd_t,
				    int, int, void *));
extern	int	ipf_auth_init __P((void));
extern	int	ipf_auth_main_load __P((void));
extern	int	ipf_auth_main_unload __P((void));
extern	void	ipf_auth_soft_destroy __P((ipf_main_softc_t *, void *));
extern	void	*ipf_auth_soft_create __P((ipf_main_softc_t *));
extern	int	ipf_auth_new __P((mb_t *, fr_info_t *));
extern	int	ipf_auth_precmd __P((ipf_main_softc_t *, ioctlcmd_t,
				     frentry_t *, frentry_t **));
extern	void	ipf_auth_unload __P((ipf_main_softc_t *));
extern	int	ipf_auth_waiting __P((ipf_main_softc_t *));
extern	void	ipf_auth_setlock __P((void *, int));
extern	int	ipf_auth_soft_init __P((ipf_main_softc_t *, void *));
extern	int	ipf_auth_soft_fini __P((ipf_main_softc_t *, void *));
extern	u_32_t	ipf_auth_pre_scanlist __P((ipf_main_softc_t *, fr_info_t *,
					   u_32_t));
extern	frentry_t **ipf_auth_rulehead __P((ipf_main_softc_t *));

#endif	/* __IP_AUTH_H__ */
