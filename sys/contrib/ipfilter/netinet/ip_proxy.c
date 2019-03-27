/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if defined(KERNEL) || defined(_KERNEL)
# undef KERNEL
# undef _KERNEL
# define        KERNEL	1
# define        _KERNEL	1
#endif
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/file.h>
# include <sys/fcntl.h>
#if !defined(_KERNEL) && !defined(__KERNEL__)
# include <stdio.h>
# include <string.h>
# include <stdlib.h>
# include <ctype.h>
# define _KERNEL
# include <sys/uio.h>
# undef _KERNEL
#endif
# include <sys/protosw.h>
#include <sys/socket.h>
#if defined(_KERNEL)
#ifdef __FreeBSD_version
#  include <sys/ctype.h>
# endif
# include <sys/systm.h>
# if !defined(__SVR4)
#  include <sys/mbuf.h>
# endif
#endif
#if defined(_KERNEL) && defined(__FreeBSD_version)
# include <sys/filio.h>
# include <sys/fcntl.h>
#else
# include <sys/ioctl.h>
#endif
#if defined(__SVR4)
# include <sys/byteorder.h>
# ifdef _KERNEL
#  include <sys/dditypes.h>
# endif
# include <sys/stream.h>
# include <sys/kmem.h>
#endif
#if __FreeBSD_version >= 300000
# include <sys/queue.h>
#endif
#include <net/if.h>
#if defined(__FreeBSD_version) && defined(_KERNEL)
#include <net/vnet.h>
#else
#define CURVNET_SET(arg)
#define CURVNET_RESTORE()
#define	VNET_DEFINE(_t, _v)	_t _v
#define	VNET_DECLARE(_t, _v)	extern _t _v
#define	VNET(arg)	arg
#endif
#ifdef sun
# include <net/af.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
# include <netinet/ip_var.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include "netinet/ip_compat.h"
#include <netinet/tcpip.h>
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#if defined(__FreeBSD_version)
# include <sys/malloc.h>
#endif

/* END OF INCLUDES */

#include "netinet/ip_ftp_pxy.c"
#include "netinet/ip_tftp_pxy.c"
#include "netinet/ip_rcmd_pxy.c"
#include "netinet/ip_pptp_pxy.c"
#if defined(_KERNEL)
# include "netinet/ip_irc_pxy.c"
# include "netinet/ip_raudio_pxy.c"
# include "netinet/ip_netbios_pxy.c"
#endif
#include "netinet/ip_ipsec_pxy.c"
#include "netinet/ip_rpcb_pxy.c"

#if !defined(lint)
static const char rcsid[] = "@(#)$Id$";
#endif

#define	AP_SESS_SIZE	53

static int ipf_proxy_fixseqack __P((fr_info_t *, ip_t *, ap_session_t *, int ));
static aproxy_t *ipf_proxy_create_clone __P((ipf_main_softc_t *, aproxy_t *));

typedef struct ipf_proxy_softc_s {
	int		ips_proxy_debug;
	int		ips_proxy_session_size;
	ap_session_t	**ips_sess_tab;
	ap_session_t	*ips_sess_list;
	aproxy_t	*ips_proxies;
	int		ips_init_run;
	ipftuneable_t	*ipf_proxy_tune;
} ipf_proxy_softc_t;

static ipftuneable_t ipf_proxy_tuneables[] = {
	{ { (void *)offsetof(ipf_proxy_softc_t, ips_proxy_debug) },
		"proxy_debug",	0,	0x1f,
		stsizeof(ipf_proxy_softc_t, ips_proxy_debug),
		0,	NULL,	NULL },
	{ { NULL },		NULL,			0,	0,
		0,
		0,	NULL,	NULL}
};

static	aproxy_t	*ap_proxylist = NULL;
static	aproxy_t	ips_proxies[] = {
#ifdef	IPF_FTP_PROXY
	{ NULL, NULL, "ftp", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_ftp_main_load, ipf_p_ftp_main_unload,
	  ipf_p_ftp_soft_create, ipf_p_ftp_soft_destroy,
	  NULL, NULL,
	  ipf_p_ftp_new, ipf_p_ftp_del, ipf_p_ftp_in, ipf_p_ftp_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_TFTP_PROXY
	{ NULL, NULL, "tftp", (char)IPPROTO_UDP, 0, 0, 0,
	  ipf_p_tftp_main_load, ipf_p_tftp_main_unload,
	  ipf_p_tftp_soft_create, ipf_p_tftp_soft_destroy,
	  NULL, NULL,
	  ipf_p_tftp_new, ipf_p_tftp_del,
	  ipf_p_tftp_in, ipf_p_tftp_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_IRC_PROXY
	{ NULL, NULL, "irc", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_irc_main_load, ipf_p_irc_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_irc_new, NULL, NULL, ipf_p_irc_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_RCMD_PROXY
	{ NULL, NULL, "rcmd", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_rcmd_main_load, ipf_p_rcmd_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_rcmd_new, ipf_p_rcmd_del,
	  ipf_p_rcmd_in, ipf_p_rcmd_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_RAUDIO_PROXY
	{ NULL, NULL, "raudio", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_raudio_main_load, ipf_p_raudio_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_raudio_new, NULL, ipf_p_raudio_in, ipf_p_raudio_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_MSNRPC_PROXY
	{ NULL, NULL, "msnrpc", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_msnrpc_init, ipf_p_msnrpc_fini,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_msnrpc_new, NULL, ipf_p_msnrpc_in, ipf_p_msnrpc_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_NETBIOS_PROXY
	{ NULL, NULL, "netbios", (char)IPPROTO_UDP, 0, 0, 0,
	  ipf_p_netbios_main_load, ipf_p_netbios_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  NULL, NULL, NULL, ipf_p_netbios_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_IPSEC_PROXY
	{ NULL, NULL, "ipsec", (char)IPPROTO_UDP, 0, 0, 0,
	  NULL, NULL,
	  ipf_p_ipsec_soft_create, ipf_p_ipsec_soft_destroy,
	  ipf_p_ipsec_soft_init, ipf_p_ipsec_soft_fini,
	  ipf_p_ipsec_new, ipf_p_ipsec_del,
	  ipf_p_ipsec_inout, ipf_p_ipsec_inout, ipf_p_ipsec_match,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_DNS_PROXY
	{ NULL, NULL, "dns", (char)IPPROTO_UDP, 0, 0, 0,
	  NULL, NULL,
	  ipf_p_dns_soft_create, ipf_p_dns_soft_destroy,
	  NULL, NULL,
	  ipf_p_dns_new, ipf_p_ipsec_del,
	  ipf_p_dns_inout, ipf_p_dns_inout, ipf_p_dns_match,
	  ipf_p_dns_ctl, NULL, NULL, NULL },
#endif
#ifdef	IPF_PPTP_PROXY
	{ NULL, NULL, "pptp", (char)IPPROTO_TCP, 0, 0, 0,
	  ipf_p_pptp_main_load, ipf_p_pptp_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_pptp_new, ipf_p_pptp_del,
	  ipf_p_pptp_inout, ipf_p_pptp_inout, NULL,
	  NULL, NULL, NULL, NULL },
#endif
#ifdef	IPF_RPCB_PROXY
# ifndef _KERNEL
	{ NULL, NULL, "rpcbt", (char)IPPROTO_TCP, 0, 0, 0,
	  NULL, NULL,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_rpcb_new, ipf_p_rpcb_del,
	  ipf_p_rpcb_in, ipf_p_rpcb_out, NULL,
	  NULL, NULL, NULL, NULL },
# endif
	{ NULL, NULL, "rpcbu", (char)IPPROTO_UDP, 0, 0, 0,
	  ipf_p_rpcb_main_load, ipf_p_rpcb_main_unload,
	  NULL, NULL,
	  NULL, NULL,
	  ipf_p_rpcb_new, ipf_p_rpcb_del,
	  ipf_p_rpcb_in, ipf_p_rpcb_out, NULL,
	  NULL, NULL, NULL, NULL },
#endif
	{ NULL, NULL, "", '\0', 0, 0, 0,
	  NULL, NULL,
	  NULL, NULL,
	  NULL, NULL,
	  NULL, NULL,
	  NULL, NULL, NULL,
	  NULL, NULL, NULL, NULL }
};


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_main_load                                         */
/* Returns:     int    - 0 == success, else failure.                        */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Initialise hook for kernel application proxies.                          */
/* Call the initialise routine for all the compiled in kernel proxies.      */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_main_load()
{
	aproxy_t *ap;

	for (ap = ips_proxies; ap->apr_p; ap++) {
		if (ap->apr_load != NULL)
			(*ap->apr_load)();
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_main_unload                                       */
/* Returns:     int - 0 == success, else failure.                           */
/* Parameters:  Nil                                                         */
/*                                                                          */
/* Unload hook for kernel application proxies.                              */
/* Call the finialise routine for all the compiled in kernel proxies.       */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_main_unload()
{
	aproxy_t *ap;

	for (ap = ips_proxies; ap->apr_p; ap++)
		if (ap->apr_unload != NULL)
			(*ap->apr_unload)();
	for (ap = ap_proxylist; ap; ap = ap->apr_next)
		if (ap->apr_unload != NULL)
			(*ap->apr_unload)();

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_soft_create                                       */
/* Returns:     void *   -                                                  */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*                                                                          */
/* Build the structure to hold all of the run time data to support proxies. */
/* ------------------------------------------------------------------------ */
void *
ipf_proxy_soft_create(softc)
	ipf_main_softc_t *softc;
{
	ipf_proxy_softc_t *softp;
	aproxy_t *last;
	aproxy_t *apn;
	aproxy_t *ap;

	KMALLOC(softp, ipf_proxy_softc_t *);
	if (softp == NULL)
		return softp;

	bzero((char *)softp, sizeof(*softp));

#if defined(_KERNEL)
	softp->ips_proxy_debug = 0;
#else
	softp->ips_proxy_debug = 2;
#endif
	softp->ips_proxy_session_size = AP_SESS_SIZE;

	softp->ipf_proxy_tune = ipf_tune_array_copy(softp,
						    sizeof(ipf_proxy_tuneables),
						    ipf_proxy_tuneables);
	if (softp->ipf_proxy_tune == NULL) {
		ipf_proxy_soft_destroy(softc, softp);
		return NULL;
	}
	if (ipf_tune_array_link(softc, softp->ipf_proxy_tune) == -1) {
		ipf_proxy_soft_destroy(softc, softp);
		return NULL;
	}

	last = NULL;
	for (ap = ips_proxies; ap->apr_p; ap++) {
		apn = ipf_proxy_create_clone(softc, ap);
		if (apn == NULL)
			goto failed;
		if (last != NULL)
			last->apr_next = apn;
		else
			softp->ips_proxies = apn;
		last = apn;
	}
	for (ap = ips_proxies; ap != NULL; ap = ap->apr_next) {
		apn = ipf_proxy_create_clone(softc, ap);
		if (apn == NULL)
			goto failed;
		if (last != NULL)
			last->apr_next = apn;
		else
			softp->ips_proxies = apn;
		last = apn;
	}

	return softp;
failed:
	ipf_proxy_soft_destroy(softc, softp);
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_soft_create                                       */
/* Returns:     void *   -                                                  */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              orig(I)  - pointer to proxy definition to copy              */
/*                                                                          */
/* This function clones a proxy definition given by orig and returns a      */
/* a pointer to that copy.                                                  */
/* ------------------------------------------------------------------------ */
static aproxy_t *
ipf_proxy_create_clone(softc, orig)
	ipf_main_softc_t *softc;
	aproxy_t *orig;
{
	aproxy_t *apn;

	KMALLOC(apn, aproxy_t *);
	if (apn == NULL)
		return NULL;

	bcopy((char *)orig, (char *)apn, sizeof(*apn));
	apn->apr_next = NULL;
	apn->apr_soft = NULL;

	if (apn->apr_create != NULL) {
		apn->apr_soft = (*apn->apr_create)(softc);
		if (apn->apr_soft == NULL) {
			KFREE(apn);
			return NULL;
		}
	}

	apn->apr_parent = orig;
	orig->apr_clones++;

	return apn;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_soft_create                                       */
/* Returns:     int      - 0 == success, else failure.                      */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to proxy contect data                    */
/*                                                                          */
/* Initialise the proxy context and walk through each of the proxies and    */
/* call its initialisation function. This allows for proxies to do any      */
/* local setup prior to actual use.                                         */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_soft_init(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_proxy_softc_t *softp;
	aproxy_t *ap;
	u_int size;
	int err;

	softp = arg;
	size = softp->ips_proxy_session_size * sizeof(ap_session_t *);

	KMALLOCS(softp->ips_sess_tab, ap_session_t **, size);

	if (softp->ips_sess_tab == NULL)
		return -1;

	bzero(softp->ips_sess_tab, size);

	for (ap = softp->ips_proxies; ap != NULL; ap = ap->apr_next) {
		if (ap->apr_init != NULL) {
			err = (*ap->apr_init)(softc, ap->apr_soft);
			if (err != 0)
				return -2;
		}
	}
	softp->ips_init_run = 1;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_soft_create                                       */
/* Returns:     int      - 0 == success, else failure.                      */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to proxy contect data                    */
/*                                                                          */
/* This function should always succeed. It is responsible for ensuring that */
/* the proxy context can be safely called when ipf_proxy_soft_destroy is    */
/* called and suring all of the proxies have similarly been instructed.     */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_soft_fini(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_proxy_softc_t *softp = arg;
	aproxy_t *ap;

	for (ap = softp->ips_proxies; ap != NULL; ap = ap->apr_next) {
		if (ap->apr_fini != NULL) {
			(*ap->apr_fini)(softc, ap->apr_soft);
		}
	}

	if (softp->ips_sess_tab != NULL) {
		KFREES(softp->ips_sess_tab,
		       softp->ips_proxy_session_size * sizeof(ap_session_t *));
		softp->ips_sess_tab = NULL;
	}
	softp->ips_init_run = 0;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_soft_destroy                                      */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to proxy contect data                    */
/*                                                                          */
/* Free up all of the local data structures allocated during creation.      */
/* ------------------------------------------------------------------------ */
void
ipf_proxy_soft_destroy(softc, arg)
	ipf_main_softc_t *softc;
	void *arg;
{
	ipf_proxy_softc_t *softp = arg;
	aproxy_t *ap;

	while ((ap = softp->ips_proxies) != NULL) {
		softp->ips_proxies = ap->apr_next;
		if (ap->apr_destroy != NULL)
			(*ap->apr_destroy)(softc, ap->apr_soft);
		ap->apr_parent->apr_clones--;
		KFREE(ap);
	}

	if (softp->ipf_proxy_tune != NULL) {
                ipf_tune_array_unlink(softc, softp->ipf_proxy_tune);
                KFREES(softp->ipf_proxy_tune, sizeof(ipf_proxy_tuneables));
                softp->ipf_proxy_tune = NULL;
	}

	KFREE(softp);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_flush                                             */
/* Returns:     Nil                                                         */
/* Parameters:  arg(I)   - pointer to proxy contect data                    */
/*              how(I)   - indicates the type of flush operation            */
/*                                                                          */
/* Walk through all of the proxies and pass on the flush command as either  */
/* a flush or a clear.                                                      */
/* ------------------------------------------------------------------------ */
void
ipf_proxy_flush(arg, how)
	void *arg;
	int how;
{
	ipf_proxy_softc_t *softp = arg;
	aproxy_t *ap;

	switch (how)
	{
	case 0 :
		for (ap = softp->ips_proxies; ap; ap = ap->apr_next)
			if (ap->apr_flush != NULL)
				(*ap->apr_flush)(ap, how);
		break;
	case 1 :
		for (ap = softp->ips_proxies; ap; ap = ap->apr_next)
			if (ap->apr_clear != NULL)
				(*ap->apr_clear)(ap);
		break;
	default :
		break;
	}
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_add                                               */
/* Returns:     int   - 0 == success, else failure.                         */
/* Parameters:  ap(I) - pointer to proxy structure                          */
/*                                                                          */
/* Dynamically add a new kernel proxy.  Ensure that it is unique in the     */
/* collection compiled in and dynamically added.                            */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_add(arg, ap)
	void *arg;
	aproxy_t *ap;
{
	ipf_proxy_softc_t *softp = arg;

	aproxy_t *a;

	for (a = ips_proxies; a->apr_p; a++)
		if ((a->apr_p == ap->apr_p) &&
		    !strncmp(a->apr_label, ap->apr_label,
			     sizeof(ap->apr_label))) {
			if (softp->ips_proxy_debug & 0x01)
				printf("ipf_proxy_add: %s/%d present (B)\n",
				       a->apr_label, a->apr_p);
			return -1;
		}

	for (a = ap_proxylist; (a != NULL); a = a->apr_next)
		if ((a->apr_p == ap->apr_p) &&
		    !strncmp(a->apr_label, ap->apr_label,
			     sizeof(ap->apr_label))) {
			if (softp->ips_proxy_debug & 0x01)
				printf("ipf_proxy_add: %s/%d present (D)\n",
				       a->apr_label, a->apr_p);
			return -1;
		}
	ap->apr_next = ap_proxylist;
	ap_proxylist = ap;
	if (ap->apr_load != NULL)
		(*ap->apr_load)();
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_ctl                                               */
/* Returns:     int    - 0 == success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              arg(I)   - pointer to proxy context                         */
/*              ctl(I)   - pointer to proxy control structure               */
/*                                                                          */
/* Check to see if the proxy this control request has come through for      */
/* exists, and if it does and it has a control function then invoke that    */
/* control function.                                                        */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_ctl(softc, arg, ctl)
	ipf_main_softc_t *softc;
	void *arg;
	ap_ctl_t *ctl;
{
	ipf_proxy_softc_t *softp = arg;
	aproxy_t *a;
	int error;

	a = ipf_proxy_lookup(arg, ctl->apc_p, ctl->apc_label);
	if (a == NULL) {
		if (softp->ips_proxy_debug & 0x01)
			printf("ipf_proxy_ctl: can't find %s/%d\n",
				ctl->apc_label, ctl->apc_p);
		IPFERROR(80001);
		error = ESRCH;
	} else if (a->apr_ctl == NULL) {
		if (softp->ips_proxy_debug & 0x01)
			printf("ipf_proxy_ctl: no ctl function for %s/%d\n",
				ctl->apc_label, ctl->apc_p);
		IPFERROR(80002);
		error = ENXIO;
	} else {
		error = (*a->apr_ctl)(softc, a->apr_soft, ctl);
		if ((error != 0) && (softp->ips_proxy_debug & 0x02))
			printf("ipf_proxy_ctl: %s/%d ctl error %d\n",
				a->apr_label, a->apr_p, error);
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_del                                               */
/* Returns:     int   - 0 == success, else failure.                         */
/* Parameters:  ap(I) - pointer to proxy structure                          */
/*                                                                          */
/* Delete a proxy that has been added dynamically from those available.     */
/* If it is in use, return 1 (do not destroy NOW), not in use 0 or -1       */
/* if it cannot be matched.                                                 */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_del(ap)
	aproxy_t *ap;
{
	aproxy_t *a, **app;

	for (app = &ap_proxylist; ((a = *app) != NULL); app = &a->apr_next) {
		if (a == ap) {
			a->apr_flags |= APR_DELETE;
			if (ap->apr_ref == 0 && ap->apr_clones == 0) {
				*app = a->apr_next;
				return 0;
			}
			return 1;
		}
	}

	return -1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_ok                                                */
/* Returns:     int    - 1 == good match else not.                          */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              tcp(I) - pointer to TCP/UDP header                          */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* This function extends the NAT matching to ensure that a packet that has  */
/* arrived matches the proxy information attached to the NAT rule. Notably, */
/* if the proxy is scheduled to be deleted then packets will not match the  */
/* rule even if the rule is still active.                                   */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_ok(fin, tcp, np)
	fr_info_t *fin;
	tcphdr_t *tcp;
	ipnat_t *np;
{
	aproxy_t *apr = np->in_apr;
	u_short dport = np->in_odport;

	if ((apr == NULL) || (apr->apr_flags & APR_DELETE) ||
	    (fin->fin_p != apr->apr_p))
		return 0;
	if ((tcp == NULL) && dport)
		return 0;
	return 1;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_ioctl                                             */
/* Returns:     int    - 0 == success, else error                           */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              data(I)  - pointer to ioctl data                            */
/*              cmd(I)   - ioctl command                                    */
/*              mode(I)  - mode bits for device                             */
/*              ctx(I)   - pointer to context information                   */
/*                                                                          */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_ioctl(softc, data, cmd, mode, ctx)
	ipf_main_softc_t *softc;
	caddr_t data;
	ioctlcmd_t cmd;
	int mode;
	void *ctx;
{
	ap_ctl_t ctl;
	caddr_t ptr;
	int error;

	mode = mode;	/* LINT */

	switch (cmd)
	{
	case SIOCPROXY :
		error = ipf_inobj(softc, data, NULL, &ctl, IPFOBJ_PROXYCTL);
		if (error != 0) {
			return error;
		}
		ptr = NULL;

		if (ctl.apc_dsize > 0) {
			KMALLOCS(ptr, caddr_t, ctl.apc_dsize);
			if (ptr == NULL) {
				IPFERROR(80003);
				error = ENOMEM;
			} else {
				error = copyinptr(softc, ctl.apc_data, ptr,
						  ctl.apc_dsize);
				if (error == 0)
					ctl.apc_data = ptr;
			}
		} else {
			ctl.apc_data = NULL;
			error = 0;
		}

		if (error == 0)
			error = ipf_proxy_ctl(softc, softc->ipf_proxy_soft,
					      &ctl);

		if ((error != 0) && (ptr != NULL)) {
			KFREES(ptr, ctl.apc_dsize);
		}
		break;

	default :
		IPFERROR(80004);
		error = EINVAL;
	}
	return error;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_match                                             */
/* Returns:     int    - 0 == success, else error                           */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* If a proxy has a match function, call that to do extended packet         */
/* matching. Whilst other parts of the NAT code are rather lenient when it  */
/* comes to the quality of the packet that it will transform, the proxy     */
/* matching is not because they need to work with data, not just headers.   */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_match(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_proxy_softc_t *softp = softc->ipf_proxy_soft;
	aproxy_t *apr;
	ipnat_t *ipn;
	int result;

	ipn = nat->nat_ptr;
	if (softp->ips_proxy_debug & 0x04)
		printf("ipf_proxy_match(%lx,%lx) aps %lx ptr %lx\n",
			(u_long)fin, (u_long)nat, (u_long)nat->nat_aps,
			(u_long)ipn);

	if ((fin->fin_flx & (FI_SHORT|FI_BAD)) != 0) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_match: flx 0x%x (BAD|SHORT)\n",
				fin->fin_flx);
		return -1;
	}

	apr = ipn->in_apr;
	if ((apr == NULL) || (apr->apr_flags & APR_DELETE)) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_match:apr %lx apr_flags 0x%x\n",
				(u_long)apr, apr ? apr->apr_flags : 0);
		return -1;
	}

	if (apr->apr_match != NULL) {
		result = (*apr->apr_match)(fin, nat->nat_aps, nat);
		if (result != 0) {
			if (softp->ips_proxy_debug & 0x08)
				printf("ipf_proxy_match: result %d\n", result);
			return -1;
		}
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_new                                               */
/* Returns:     int    - 0 == success, else error                           */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* Allocate a new application proxy structure and fill it in with the       */
/* relevant details.  call the init function once complete, prior to        */
/* returning.                                                               */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_new(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_proxy_softc_t *softp = softc->ipf_proxy_soft;
	register ap_session_t *aps;
	aproxy_t *apr;

	if (softp->ips_proxy_debug & 0x04)
		printf("ipf_proxy_new(%lx,%lx) \n", (u_long)fin, (u_long)nat);

	if ((nat->nat_ptr == NULL) || (nat->nat_aps != NULL)) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_new: nat_ptr %lx nat_aps %lx\n",
				(u_long)nat->nat_ptr, (u_long)nat->nat_aps);
		return -1;
	}

	apr = nat->nat_ptr->in_apr;

	if ((apr->apr_flags & APR_DELETE) ||
	    (fin->fin_p != apr->apr_p)) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_new: apr_flags 0x%x p %d/%d\n",
				apr->apr_flags, fin->fin_p, apr->apr_p);
		return -1;
	}

	KMALLOC(aps, ap_session_t *);
	if (!aps) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_new: malloc failed (%lu)\n",
				(u_long)sizeof(ap_session_t));
		return -1;
	}

	bzero((char *)aps, sizeof(*aps));
	aps->aps_data = NULL;
	aps->aps_apr = apr;
	aps->aps_psiz = 0;
	if (apr->apr_new != NULL)
		if ((*apr->apr_new)(apr->apr_soft, fin, aps, nat) == -1) {
			if ((aps->aps_data != NULL) && (aps->aps_psiz != 0)) {
				KFREES(aps->aps_data, aps->aps_psiz);
			}
			KFREE(aps);
			if (softp->ips_proxy_debug & 0x08)
				printf("ipf_proxy_new: new(%lx) failed\n",
					(u_long)apr->apr_new);
			return -1;
		}
	aps->aps_nat = nat;
	aps->aps_next = softp->ips_sess_list;
	softp->ips_sess_list = aps;
	nat->nat_aps = aps;

	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_check                                             */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              nat(I) - pointer to current NAT session                     */
/*                                                                          */
/* Check to see if a packet should be passed through an active proxy        */
/* routine if one has been setup for it.  We don't need to check the        */
/* checksum here if IPFILTER_CKSUM is defined because if it is, a failed    */
/* check causes FI_BAD to be set.                                           */
/* ------------------------------------------------------------------------ */
int
ipf_proxy_check(fin, nat)
	fr_info_t *fin;
	nat_t *nat;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_proxy_softc_t *softp = softc->ipf_proxy_soft;
#if SOLARIS && defined(_KERNEL) && defined(ICK_VALID)
	mb_t *m;
#endif
	tcphdr_t *tcp = NULL;
	udphdr_t *udp = NULL;
	ap_session_t *aps;
	aproxy_t *apr;
	short adjlen;
	int dosum;
	ip_t *ip;
	short rv;
	int err;
#if !defined(_KERNEL) || defined(MENTAT)
	u_32_t s1, s2, sd;
#endif

	if (fin->fin_flx & FI_BAD) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_check: flx 0x%x (BAD)\n",
			       fin->fin_flx);
		return -1;
	}

#ifndef IPFILTER_CKSUM
	if ((fin->fin_out == 0) && (ipf_checkl4sum(fin) == -1)) {
		if (softp->ips_proxy_debug & 0x08)
			printf("ipf_proxy_check: l4 checksum failure %d\n",
				fin->fin_p);
		if (fin->fin_p == IPPROTO_TCP)
			softc->ipf_stats[fin->fin_out].fr_tcpbad++;
		return -1;
	}
#endif

	aps = nat->nat_aps;
	if (aps != NULL) {
		/*
		 * If there is data in this packet to be proxied then try and
		 * get it all into the one buffer, else drop it.
		 */
#if defined(MENTAT) || defined(HAVE_M_PULLDOWN)
		if ((fin->fin_dlen > 0) && !(fin->fin_flx & FI_COALESCE))
			if (ipf_coalesce(fin) == -1) {
				if (softp->ips_proxy_debug & 0x08)
					printf("ipf_proxy_check: %s %x\n",
					       "coalesce failed", fin->fin_flx);
				return -1;
			}
#endif
		ip = fin->fin_ip;
		if (fin->fin_cksum > FI_CK_SUMOK)
			dosum = 0;
		else
			dosum = 1;

		switch (fin->fin_p)
		{
		case IPPROTO_TCP :
			tcp = (tcphdr_t *)fin->fin_dp;
#if SOLARIS && defined(_KERNEL) && defined(ICK_VALID)
			m = fin->fin_qfm;
			if (dohwcksum && (m->b_ick_flag == ICK_VALID))
				dosum = 0;
#endif
			break;
		case IPPROTO_UDP :
			udp = (udphdr_t *)fin->fin_dp;
			break;
		default :
			break;
		}

		apr = aps->aps_apr;
		err = 0;
		if (fin->fin_out != 0) {
			if (apr->apr_outpkt != NULL)
				err = (*apr->apr_outpkt)(apr->apr_soft, fin,
							 aps, nat);
		} else {
			if (apr->apr_inpkt != NULL)
				err = (*apr->apr_inpkt)(apr->apr_soft, fin,
							aps, nat);
		}

		rv = APR_EXIT(err);
		if (((softp->ips_proxy_debug & 0x08) && (rv != 0)) ||
		    (softp->ips_proxy_debug & 0x04))
			printf("ipf_proxy_check: out %d err %x rv %d\n",
				fin->fin_out, err, rv);
		if (rv == 1)
			return -1;

		if (rv == 2) {
			ipf_proxy_deref(apr);
			nat->nat_aps = NULL;
			return -1;
		}

		/*
		 * If err != 0 then the data size of the packet has changed
		 * so we need to recalculate the header checksums for the
		 * packet.
		 */
		adjlen = APR_INC(err);
#if !defined(_KERNEL) || defined(MENTAT)
		s1 = LONG_SUM(fin->fin_plen - adjlen);
		s2 = LONG_SUM(fin->fin_plen);
		CALC_SUMD(s1, s2, sd);
		if ((err != 0) && (fin->fin_cksum < FI_CK_L4PART) &&
		    fin->fin_v == 4)
			ipf_fix_outcksum(0, &ip->ip_sum, sd, 0);
#endif
		if (fin->fin_flx & FI_DOCKSUM)
			dosum = 1;

		/*
		 * For TCP packets, we may need to adjust the sequence and
		 * acknowledgement numbers to reflect changes in size of the
		 * data stream.
		 *
		 * For both TCP and UDP, recalculate the layer 4 checksum,
		 * regardless, as we can't tell (here) if data has been
		 * changed or not.
		 */
		if (tcp != NULL) {
			err = ipf_proxy_fixseqack(fin, ip, aps, adjlen);
			if (fin->fin_cksum == FI_CK_L4PART) {
				u_short sum = ntohs(tcp->th_sum);
				sum += adjlen;
				tcp->th_sum = htons(sum);
			} else if (fin->fin_cksum < FI_CK_L4PART) {
				tcp->th_sum = fr_cksum(fin, ip,
						       IPPROTO_TCP, tcp);
			}
		} else if ((udp != NULL) && (udp->uh_sum != 0)) {
			if (fin->fin_cksum == FI_CK_L4PART) {
				u_short sum = ntohs(udp->uh_sum);
				sum += adjlen;
				udp->uh_sum = htons(sum);
			} else if (dosum) {
				udp->uh_sum = fr_cksum(fin, ip,
						       IPPROTO_UDP, udp);
			}
		}
		aps->aps_bytes += fin->fin_plen;
		aps->aps_pkts++;
		return 1;
	}
	return 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_lookup                                            */
/* Returns:     int - -1 == error, 0 == success                             */
/* Parameters:  arg(I)  - pointer to proxy context information              */
/*              pr(I)   - protocol number for proxy                         */
/*              name(I) - proxy name                                        */
/*                                                                          */
/* Search for an proxy by the protocol it is being used with and its name.  */
/* ------------------------------------------------------------------------ */
aproxy_t *
ipf_proxy_lookup(arg, pr, name)
	void *arg;
	u_int pr;
	char *name;
{
	ipf_proxy_softc_t *softp = arg;
	aproxy_t *ap;

	if (softp->ips_proxy_debug & 0x04)
		printf("ipf_proxy_lookup(%d,%s)\n", pr, name);

	for (ap = softp->ips_proxies; ap != NULL; ap = ap->apr_next)
		if ((ap->apr_p == pr) &&
		    !strncmp(name, ap->apr_label, sizeof(ap->apr_label))) {
			ap->apr_ref++;
			return ap;
		}

	if (softp->ips_proxy_debug & 0x08)
		printf("ipf_proxy_lookup: failed for %d/%s\n", pr, name);
	return NULL;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_deref                                             */
/* Returns:     Nil                                                         */
/* Parameters:  ap(I) - pointer to proxy structure                          */
/*                                                                          */
/* Drop the reference counter associated with the proxy.                    */
/* ------------------------------------------------------------------------ */
void
ipf_proxy_deref(ap)
	aproxy_t *ap;
{
	ap->apr_ref--;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_free                                              */
/* Returns:     Nil                                                         */
/* Parameters:  softc(I) - pointer to soft context main structure           */
/*              aps(I)   - pointer to current proxy session                 */
/* Locks Held:  ipf_nat_new, ipf_nat(W)                                     */
/*                                                                          */
/* Free up proxy session information allocated to be used with a NAT        */
/* session.                                                                 */
/* ------------------------------------------------------------------------ */
void
ipf_proxy_free(softc, aps)
	ipf_main_softc_t *softc;
	ap_session_t *aps;
{
	ipf_proxy_softc_t *softp = softc->ipf_proxy_soft;
	ap_session_t *a, **ap;
	aproxy_t *apr;

	if (!aps)
		return;

	for (ap = &softp->ips_sess_list; ((a = *ap) != NULL); ap = &a->aps_next)
		if (a == aps) {
			*ap = a->aps_next;
			break;
		}

	apr = aps->aps_apr;
	if ((apr != NULL) && (apr->apr_del != NULL))
		(*apr->apr_del)(softc, aps);

	if ((aps->aps_data != NULL) && (aps->aps_psiz != 0))
		KFREES(aps->aps_data, aps->aps_psiz);
	KFREE(aps);
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_fixseqack                                         */
/* Returns:     int    - 2 if TCP ack/seq is changed, else 0                */
/* Parameters:  fin(I) - pointer to packet information                      */
/*              ip(I)  - pointer to IP header                               */
/*              nat(I) - pointer to current NAT session                     */
/*              inc(I) - delta to apply to TCP sequence numbering           */
/*                                                                          */
/* Adjust the TCP sequence/acknowledge numbers in the TCP header based on   */
/* whether or not the new header is past the point at which an adjustment   */
/* occurred. This might happen because of (say) an FTP string being changed */
/* and the new string being a different length to the old.                  */
/* ------------------------------------------------------------------------ */
static int
ipf_proxy_fixseqack(fin, ip, aps, inc)
	fr_info_t *fin;
	ip_t *ip;
	ap_session_t *aps;
	int inc;
{
	ipf_main_softc_t *softc = fin->fin_main_soft;
	ipf_proxy_softc_t *softp = softc->ipf_proxy_soft;
	int sel, ch = 0, out, nlen;
	u_32_t seq1, seq2;
	tcphdr_t *tcp;
	short inc2;

	tcp = (tcphdr_t *)fin->fin_dp;
	out = fin->fin_out;
	/*
	 * ip_len has already been adjusted by 'inc'.
	 */
	nlen = fin->fin_dlen;
	nlen -= (TCP_OFF(tcp) << 2);

	inc2 = inc;
	inc = (int)inc2;

	if (out != 0) {
		seq1 = (u_32_t)ntohl(tcp->th_seq);
		sel = aps->aps_sel[out];

		/* switch to other set ? */
		if ((aps->aps_seqmin[!sel] > aps->aps_seqmin[sel]) &&
		    (seq1 > aps->aps_seqmin[!sel])) {
			if (softp->ips_proxy_debug & 0x10)
				printf("proxy out switch set seq %d -> %d %x > %x\n",
					sel, !sel, seq1,
					aps->aps_seqmin[!sel]);
			sel = aps->aps_sel[out] = !sel;
		}

		if (aps->aps_seqoff[sel]) {
			seq2 = aps->aps_seqmin[sel] - aps->aps_seqoff[sel];
			if (seq1 > seq2) {
				seq2 = aps->aps_seqoff[sel];
				seq1 += seq2;
				tcp->th_seq = htonl(seq1);
				ch = 1;
			}
		}

		if (inc && (seq1 > aps->aps_seqmin[!sel])) {
			aps->aps_seqmin[sel] = seq1 + nlen - 1;
			aps->aps_seqoff[sel] = aps->aps_seqoff[sel] + inc;
			if (softp->ips_proxy_debug & 0x10)
				printf("proxy seq set %d at %x to %d + %d\n",
					sel, aps->aps_seqmin[sel],
					aps->aps_seqoff[sel], inc);
		}

		/***/

		seq1 = ntohl(tcp->th_ack);
		sel = aps->aps_sel[1 - out];

		/* switch to other set ? */
		if ((aps->aps_ackmin[!sel] > aps->aps_ackmin[sel]) &&
		    (seq1 > aps->aps_ackmin[!sel])) {
			if (softp->ips_proxy_debug & 0x10)
				printf("proxy out switch set ack %d -> %d %x > %x\n",
					sel, !sel, seq1,
					aps->aps_ackmin[!sel]);
			sel = aps->aps_sel[1 - out] = !sel;
		}

		if (aps->aps_ackoff[sel] && (seq1 > aps->aps_ackmin[sel])) {
			seq2 = aps->aps_ackoff[sel];
			tcp->th_ack = htonl(seq1 - seq2);
			ch = 1;
		}
	} else {
		seq1 = ntohl(tcp->th_seq);
		sel = aps->aps_sel[out];

		/* switch to other set ? */
		if ((aps->aps_ackmin[!sel] > aps->aps_ackmin[sel]) &&
		    (seq1 > aps->aps_ackmin[!sel])) {
			if (softp->ips_proxy_debug & 0x10)
				printf("proxy in switch set ack %d -> %d %x > %x\n",
					sel, !sel, seq1, aps->aps_ackmin[!sel]);
			sel = aps->aps_sel[out] = !sel;
		}

		if (aps->aps_ackoff[sel]) {
			seq2 = aps->aps_ackmin[sel] - aps->aps_ackoff[sel];
			if (seq1 > seq2) {
				seq2 = aps->aps_ackoff[sel];
				seq1 += seq2;
				tcp->th_seq = htonl(seq1);
				ch = 1;
			}
		}

		if (inc && (seq1 > aps->aps_ackmin[!sel])) {
			aps->aps_ackmin[!sel] = seq1 + nlen - 1;
			aps->aps_ackoff[!sel] = aps->aps_ackoff[sel] + inc;

			if (softp->ips_proxy_debug & 0x10)
				printf("proxy ack set %d at %x to %d + %d\n",
					!sel, aps->aps_seqmin[!sel],
					aps->aps_seqoff[sel], inc);
		}

		/***/

		seq1 = ntohl(tcp->th_ack);
		sel = aps->aps_sel[1 - out];

		/* switch to other set ? */
		if ((aps->aps_seqmin[!sel] > aps->aps_seqmin[sel]) &&
		    (seq1 > aps->aps_seqmin[!sel])) {
			if (softp->ips_proxy_debug & 0x10)
				printf("proxy in switch set seq %d -> %d %x > %x\n",
					sel, !sel, seq1, aps->aps_seqmin[!sel]);
			sel = aps->aps_sel[1 - out] = !sel;
		}

		if (aps->aps_seqoff[sel] != 0) {
			if (softp->ips_proxy_debug & 0x10)
				printf("sel %d seqoff %d seq1 %x seqmin %x\n",
					sel, aps->aps_seqoff[sel], seq1,
					aps->aps_seqmin[sel]);
			if (seq1 > aps->aps_seqmin[sel]) {
				seq2 = aps->aps_seqoff[sel];
				tcp->th_ack = htonl(seq1 - seq2);
				ch = 1;
			}
		}
	}

	if (softp->ips_proxy_debug & 0x10)
		printf("ipf_proxy_fixseqack: seq %u ack %u\n",
			(u_32_t)ntohl(tcp->th_seq), (u_32_t)ntohl(tcp->th_ack));
	return ch ? 2 : 0;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_rule_rev                                          */
/* Returns:     ipnat_t * - NULL = failure, else pointer to new rule        */
/* Parameters:  nat(I) - pointer to NAT session to create rule from         */
/*                                                                          */
/* This function creates a NAT rule that is based upon the reverse packet   */
/* flow associated with this NAT session. Thus if this NAT session was      */
/* created with a map rule then this function will create a rdr rule.       */ 
/* Only address fields and network interfaces are assigned in this function */
/* and the address fields are formed such that an exact is required. If the */
/* original rule had a netmask, that is not replicated here not is it       */
/* desired. The ultimate goal here is to create a NAT rule to support a NAT */
/* session being created that does not have a user configured rule. The     */
/* classic example is supporting the FTP proxy, where a data channel needs  */
/* to be setup, based on the addresses used for the control connection. In  */
/* that case, this function is used to handle creating NAT rules to support */
/* data connections with the PORT and EPRT commands.                        */
/* ------------------------------------------------------------------------ */ 
ipnat_t *
ipf_proxy_rule_rev(nat)
	nat_t *nat;
{
	ipnat_t *old;
	ipnat_t *ipn;
	int size;

	old = nat->nat_ptr;
	size = old->in_size;

	KMALLOCS(ipn, ipnat_t *, size);
	if (ipn == NULL)
		return NULL;

	bzero((char *)ipn, size);

	ipn->in_use = 1;
	ipn->in_hits = 1;
	ipn->in_ippip = 1;
	ipn->in_apr = NULL;
	ipn->in_size = size;
	ipn->in_pr[0] = old->in_pr[1];
	ipn->in_pr[1] = old->in_pr[0];
	ipn->in_v[0] = old->in_v[1];
	ipn->in_v[1] = old->in_v[0];
	ipn->in_ifps[0] = old->in_ifps[1];
	ipn->in_ifps[1] = old->in_ifps[0];
	ipn->in_flags = (old->in_flags | IPN_PROXYRULE);

	ipn->in_nsrcip6 = nat->nat_odst6;
	ipn->in_osrcip6 = nat->nat_ndst6;

	if ((old->in_redir & NAT_REDIRECT) != 0) {
		ipn->in_redir = NAT_MAP;
		if (ipn->in_v[0] == 4) {
			ipn->in_snip = ntohl(nat->nat_odstaddr);
			ipn->in_dnip = ntohl(nat->nat_nsrcaddr);
		} else {
#ifdef USE_INET6
			ipn->in_snip6 = nat->nat_odst6;
			ipn->in_dnip6 = nat->nat_nsrc6;
#endif
		}
		ipn->in_ndstip6 = nat->nat_nsrc6;
		ipn->in_odstip6 = nat->nat_osrc6;
	} else {
		ipn->in_redir = NAT_REDIRECT;
		if (ipn->in_v[0] == 4) {
			ipn->in_snip = ntohl(nat->nat_odstaddr);
			ipn->in_dnip = ntohl(nat->nat_osrcaddr);
		} else {
#ifdef USE_INET6
			ipn->in_snip6 = nat->nat_odst6;
			ipn->in_dnip6 = nat->nat_osrc6;
#endif
		}
		ipn->in_ndstip6 = nat->nat_osrc6;
		ipn->in_odstip6 = nat->nat_nsrc6;
	}

	IP6_SETONES(&ipn->in_osrcmsk6);
	IP6_SETONES(&ipn->in_nsrcmsk6);
	IP6_SETONES(&ipn->in_odstmsk6);
	IP6_SETONES(&ipn->in_ndstmsk6);

	ipn->in_namelen = old->in_namelen;
	ipn->in_ifnames[0] = old->in_ifnames[1];
	ipn->in_ifnames[1] = old->in_ifnames[0];
	bcopy(old->in_names, ipn->in_names, ipn->in_namelen);
	MUTEX_INIT(&ipn->in_lock, "ipnat rev rule lock");

	return ipn;
}


/* ------------------------------------------------------------------------ */
/* Function:    ipf_proxy_rule_fwd                                          */
/* Returns:     ipnat_t * - NULL = failure, else pointer to new rule        */
/* Parameters:  nat(I) - pointer to NAT session to create rule from         */
/*                                                                          */
/* The purpose and rationale of this function is much the same as the above */
/* function, ipf_proxy_rule_rev, except that a rule is created that matches */
/* the same direction as that of the existing NAT session. Thus if this NAT */ 
/* session was created with a map rule then this function will also create  */
/* a data structure to represent a map rule. Whereas ipf_proxy_rule_rev is  */
/* used to support PORT/EPRT, this function supports PASV/EPSV.             */
/* ------------------------------------------------------------------------ */ 
ipnat_t *
ipf_proxy_rule_fwd(nat)
	nat_t *nat;
{
	ipnat_t *old;
	ipnat_t *ipn;
	int size;

	old = nat->nat_ptr;
	size = old->in_size;

	KMALLOCS(ipn, ipnat_t *, size);
	if (ipn == NULL)
		return NULL;

	bzero((char *)ipn, size);

	ipn->in_use = 1;
	ipn->in_hits = 1;
	ipn->in_ippip = 1;
	ipn->in_apr = NULL;
	ipn->in_size = size;
	ipn->in_pr[0] = old->in_pr[0];
	ipn->in_pr[1] = old->in_pr[1];
	ipn->in_v[0] = old->in_v[0];
	ipn->in_v[1] = old->in_v[1];
	ipn->in_ifps[0] = nat->nat_ifps[0];
	ipn->in_ifps[1] = nat->nat_ifps[1];
	ipn->in_flags = (old->in_flags | IPN_PROXYRULE);

	ipn->in_nsrcip6 = nat->nat_nsrc6;
	ipn->in_osrcip6 = nat->nat_osrc6;
	ipn->in_ndstip6 = nat->nat_ndst6;
	ipn->in_odstip6 = nat->nat_odst6;
	ipn->in_redir = old->in_redir;

	if (ipn->in_v[0] == 4) {
		ipn->in_snip = ntohl(nat->nat_nsrcaddr);
		ipn->in_dnip = ntohl(nat->nat_ndstaddr);
	} else {
#ifdef USE_INET6
		ipn->in_snip6 = nat->nat_nsrc6;
		ipn->in_dnip6 = nat->nat_ndst6;
#endif
	}

	IP6_SETONES(&ipn->in_osrcmsk6);
	IP6_SETONES(&ipn->in_nsrcmsk6);
	IP6_SETONES(&ipn->in_odstmsk6);
	IP6_SETONES(&ipn->in_ndstmsk6);

	ipn->in_namelen = old->in_namelen;
	ipn->in_ifnames[0] = old->in_ifnames[0];
	ipn->in_ifnames[1] = old->in_ifnames[1];
	bcopy(old->in_names, ipn->in_names, ipn->in_namelen);
	MUTEX_INIT(&ipn->in_lock, "ipnat fwd rule lock");

	return ipn;
}
