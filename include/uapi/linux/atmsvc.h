/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* atmsvc.h - ATM signaling kernel-demon interface definitions */
 
/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */
 

#ifndef _LINUX_ATMSVC_H
#define _LINUX_ATMSVC_H

#include <linux/atmapi.h>
#include <linux/atm.h>
#include <linux/atmioc.h>


#define ATMSIGD_CTRL _IO('a',ATMIOC_SPECIAL)
				/* become ATM signaling demon control socket */

enum atmsvc_msg_type { as_catch_null, as_bind, as_connect, as_accept, as_reject,
		       as_listen, as_okay, as_error, as_indicate, as_close,
		       as_itf_notify, as_modify, as_identify, as_terminate,
		       as_addparty, as_dropparty };

struct atmsvc_msg {
	enum atmsvc_msg_type type;
	atm_kptr_t vcc;
	atm_kptr_t listen_vcc;		/* indicate */
	int reply;			/* for okay and close:		   */
					/*   < 0: error before active	   */
					/*        (sigd has discarded ctx) */
					/*   ==0: success		   */
				        /*   > 0: error when active (still */
					/*        need to close)	   */
	struct sockaddr_atmpvc pvc;	/* indicate, okay (connect) */
	struct sockaddr_atmsvc local;	/* local SVC address */
	struct atm_qos qos;		/* QOS parameters */
	struct atm_sap sap;		/* SAP */
	unsigned int session;		/* for p2pm */
	struct sockaddr_atmsvc svc;	/* SVC address */
} __ATM_API_ALIGN;

/*
 * Message contents: see ftp://icaftp.epfl.ch/pub/linux/atm/docs/isp-*.tar.gz
 */

/*
 * Some policy stuff for atmsigd and for net/atm/svc.c. Both have to agree on
 * what PCR is used to request bandwidth from the device driver. net/atm/svc.c
 * tries to do better than that, but only if there's no routing decision (i.e.
 * if signaling only uses one ATM interface).
 */

#define SELECT_TOP_PCR(tp) ((tp).pcr ? (tp).pcr : \
  (tp).max_pcr && (tp).max_pcr != ATM_MAX_PCR ? (tp).max_pcr : \
  (tp).min_pcr ? (tp).min_pcr : ATM_MAX_PCR)

#endif
