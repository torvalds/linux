/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* $Id: isdn.h,v 1.125.2.3 2004/02/10 01:07:14 keil Exp $
 *
 * Main header for the Linux ISDN subsystem (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
 * Copyright 1995,96    by Michael Hipp (Michael.Hipp@student.uni-tuebingen.de)
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef _UAPI__ISDN_H__
#define _UAPI__ISDN_H__

#include <linux/ioctl.h>
#include <linux/tty.h>

#define ISDN_MAX_DRIVERS    32
#define ISDN_MAX_CHANNELS   64

/* New ioctl-codes */
#define IIOCNETAIF  _IO('I',1)
#define IIOCNETDIF  _IO('I',2)
#define IIOCNETSCF  _IO('I',3)
#define IIOCNETGCF  _IO('I',4)
#define IIOCNETANM  _IO('I',5)
#define IIOCNETDNM  _IO('I',6)
#define IIOCNETGNM  _IO('I',7)
#define IIOCGETSET  _IO('I',8) /* no longer supported */
#define IIOCSETSET  _IO('I',9) /* no longer supported */
#define IIOCSETVER  _IO('I',10)
#define IIOCNETHUP  _IO('I',11)
#define IIOCSETGST  _IO('I',12)
#define IIOCSETBRJ  _IO('I',13)
#define IIOCSIGPRF  _IO('I',14)
#define IIOCGETPRF  _IO('I',15)
#define IIOCSETPRF  _IO('I',16)
#define IIOCGETMAP  _IO('I',17)
#define IIOCSETMAP  _IO('I',18)
#define IIOCNETASL  _IO('I',19)
#define IIOCNETDIL  _IO('I',20)
#define IIOCGETCPS  _IO('I',21)
#define IIOCGETDVR  _IO('I',22)
#define IIOCNETLCR  _IO('I',23) /* dwabc ioctl for LCR from isdnlog */
#define IIOCNETDWRSET  _IO('I',24) /* dwabc ioctl to reset abc-values to default on a net-interface */

#define IIOCNETALN  _IO('I',32)
#define IIOCNETDLN  _IO('I',33)

#define IIOCNETGPN  _IO('I',34)

#define IIOCDBGVAR  _IO('I',127)

#define IIOCDRVCTL  _IO('I',128)

/* cisco hdlck device private ioctls */
#define SIOCGKEEPPERIOD	(SIOCDEVPRIVATE + 0)
#define SIOCSKEEPPERIOD	(SIOCDEVPRIVATE + 1)
#define SIOCGDEBSERINT	(SIOCDEVPRIVATE + 2)
#define SIOCSDEBSERINT	(SIOCDEVPRIVATE + 3)

/* Packet encapsulations for net-interfaces */
#define ISDN_NET_ENCAP_ETHER      0
#define ISDN_NET_ENCAP_RAWIP      1
#define ISDN_NET_ENCAP_IPTYP      2
#define ISDN_NET_ENCAP_CISCOHDLC  3 /* Without SLARP and keepalive */
#define ISDN_NET_ENCAP_SYNCPPP    4
#define ISDN_NET_ENCAP_UIHDLC     5
#define ISDN_NET_ENCAP_CISCOHDLCK 6 /* With SLARP and keepalive    */
#define ISDN_NET_ENCAP_X25IFACE   7 /* Documentation/networking/x25-iface.txt */
#define ISDN_NET_ENCAP_MAX_ENCAP  ISDN_NET_ENCAP_X25IFACE

/* Facility which currently uses an ISDN-channel */
#define ISDN_USAGE_NONE       0
#define ISDN_USAGE_RAW        1
#define ISDN_USAGE_MODEM      2
#define ISDN_USAGE_NET        3
#define ISDN_USAGE_VOICE      4
#define ISDN_USAGE_FAX        5
#define ISDN_USAGE_MASK       7 /* Mask to get plain usage */
#define ISDN_USAGE_DISABLED  32 /* This bit is set, if channel is disabled */
#define ISDN_USAGE_EXCLUSIVE 64 /* This bit is set, if channel is exclusive */
#define ISDN_USAGE_OUTGOING 128 /* This bit is set, if channel is outgoing  */

#define ISDN_MODEM_NUMREG    24        /* Number of Modem-Registers        */
#define ISDN_LMSNLEN         255 /* Length of tty's Listen-MSN string */
#define ISDN_CMSGLEN	     50	 /* Length of CONNECT-Message to add for Modem */

#define ISDN_MSNLEN          32
#define NET_DV 0x06  /* Data version for isdn_net_ioctl_cfg   */
#define TTY_DV 0x06  /* Data version for iprofd etc.          */

#define INF_DV 0x01  /* Data version for /dev/isdninfo        */

typedef struct {
  char drvid[25];
  unsigned long arg;
} isdn_ioctl_struct;

typedef struct {
  char name[10];
  char phone[ISDN_MSNLEN];
  int  outgoing;
} isdn_net_ioctl_phone;

typedef struct {
  char name[10];     /* Name of interface                     */
  char master[10];   /* Name of Master for Bundling           */
  char slave[10];    /* Name of Slave for Bundling            */
  char eaz[256];     /* EAZ/MSN                               */
  char drvid[25];    /* DriverId for Bindings                 */
  int  onhtime;      /* Hangup-Timeout                        */
  int  charge;       /* Charge-Units                          */
  int  l2_proto;     /* Layer-2 protocol                      */
  int  l3_proto;     /* Layer-3 protocol                      */
  int  p_encap;      /* Encapsulation                         */
  int  exclusive;    /* Channel, if bound exclusive           */
  int  dialmax;      /* Dial Retry-Counter                    */
  int  slavedelay;   /* Delay until slave starts up           */
  int  cbdelay;      /* Delay before Callback                 */
  int  chargehup;    /* Flag: Charge-Hangup                   */
  int  ihup;         /* Flag: Hangup-Timeout on incoming line */
  int  secure;       /* Flag: Secure                          */
  int  callback;     /* Flag: Callback                        */
  int  cbhup;        /* Flag: Reject Call before Callback     */
  int  pppbind;      /* ippp device for bindings              */
  int  chargeint;    /* Use fixed charge interval length      */
  int  triggercps;   /* BogoCPS needed for triggering slave   */
  int  dialtimeout;  /* Dial-Timeout                          */
  int  dialwait;     /* Time to wait after failed dial        */
  int  dialmode;     /* Flag: off / on / auto                 */
} isdn_net_ioctl_cfg;

#define ISDN_NET_DIALMODE_MASK  0xC0    /* bits for status                */
#define ISDN_NET_DM_OFF	        0x00    /* this interface is stopped      */
#define ISDN_NET_DM_MANUAL	0x40    /* this interface is on (manual)  */
#define ISDN_NET_DM_AUTO	0x80    /* this interface is autodial     */
#define ISDN_NET_DIALMODE(x) ((&(x))->flags & ISDN_NET_DIALMODE_MASK)


#endif /* _UAPI__ISDN_H__ */
