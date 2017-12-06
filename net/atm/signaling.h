/* SPDX-License-Identifier: GPL-2.0 */
/* net/atm/signaling.h - ATM signaling */

/* Written 1995-2000 by Werner Almesberger, EPFL LRC/ICA */


#ifndef NET_ATM_SIGNALING_H
#define NET_ATM_SIGNALING_H

#include <linux/atm.h>
#include <linux/atmdev.h>
#include <linux/atmsvc.h>


extern struct atm_vcc *sigd; /* needed in svc_release */


/*
 * sigd_enq is a wrapper for sigd_enq2, covering the more common cases, and
 * avoiding huge lists of null values.
 */

void sigd_enq2(struct atm_vcc *vcc,enum atmsvc_msg_type type,
    struct atm_vcc *listen_vcc,const struct sockaddr_atmpvc *pvc,
    const struct sockaddr_atmsvc *svc,const struct atm_qos *qos,int reply);
void sigd_enq(struct atm_vcc *vcc,enum atmsvc_msg_type type,
    struct atm_vcc *listen_vcc,const struct sockaddr_atmpvc *pvc,
    const struct sockaddr_atmsvc *svc);
int sigd_attach(struct atm_vcc *vcc);

#endif
