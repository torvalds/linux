/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1982, 1986, 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sockio.h	8.1 (Berkeley) 3/28/94
 * $FreeBSD$
 */

#ifndef _SYS_SOCKIO_H_
#define	_SYS_SOCKIO_H_

#include <sys/ioccom.h>

/* Socket ioctl's. */
#define	SIOCSHIWAT	 _IOW('s',  0, int)		/* set high watermark */
#define	SIOCGHIWAT	 _IOR('s',  1, int)		/* get high watermark */
#define	SIOCSLOWAT	 _IOW('s',  2, int)		/* set low watermark */
#define	SIOCGLOWAT	 _IOR('s',  3, int)		/* get low watermark */
#define	SIOCATMARK	 _IOR('s',  7, int)		/* at oob mark? */
#define	SIOCSPGRP	 _IOW('s',  8, int)		/* set process group */
#define	SIOCGPGRP	 _IOR('s',  9, int)		/* get process group */

/*	SIOCADDRT	 _IOW('r', 10, struct ortentry)	4.3BSD */
/*	SIOCDELRT	 _IOW('r', 11, struct ortentry)	4.3BSD */
#define	SIOCGETVIFCNT	_IOWR('r', 15, struct sioc_vif_req)/* get vif pkt cnt */
#define	SIOCGETSGCNT	_IOWR('r', 16, struct sioc_sg_req) /* get s,g pkt cnt */

#define	SIOCSIFADDR	 _IOW('i', 12, struct ifreq)	/* set ifnet address */
/*	OSIOCGIFADDR	_IOWR('i', 13, struct ifreq)	4.3BSD */
#define	SIOCGIFADDR	_IOWR('i', 33, struct ifreq)	/* get ifnet address */
#define	SIOCSIFDSTADDR	 _IOW('i', 14, struct ifreq)	/* set p-p address */
/*	OSIOCGIFDSTADDR	_IOWR('i', 15, struct ifreq)	4.3BSD */
#define	SIOCGIFDSTADDR	_IOWR('i', 34, struct ifreq)	/* get p-p address */
#define	SIOCSIFFLAGS	 _IOW('i', 16, struct ifreq)	/* set ifnet flags */
#define	SIOCGIFFLAGS	_IOWR('i', 17, struct ifreq)	/* get ifnet flags */
/*	OSIOCGIFBRDADDR	_IOWR('i', 18, struct ifreq)	4.3BSD */
#define	SIOCGIFBRDADDR	_IOWR('i', 35, struct ifreq)	/* get broadcast addr */
#define	SIOCSIFBRDADDR	 _IOW('i', 19, struct ifreq)	/* set broadcast addr */
/*	OSIOCGIFCONF	_IOWR('i', 20, struct ifconf)	4.3BSD */
#define	SIOCGIFCONF	_IOWR('i', 36, struct ifconf)	/* get ifnet list */
/*	OSIOCGIFNETMASK	_IOWR('i', 21, struct ifreq)	4.3BSD */
#define	SIOCGIFNETMASK	_IOWR('i', 37, struct ifreq)	/* get net addr mask */
#define	SIOCSIFNETMASK	 _IOW('i', 22, struct ifreq)	/* set net addr mask */
#define	SIOCGIFMETRIC	_IOWR('i', 23, struct ifreq)	/* get IF metric */
#define	SIOCSIFMETRIC	 _IOW('i', 24, struct ifreq)	/* set IF metric */
#define	SIOCDIFADDR	 _IOW('i', 25, struct ifreq)	/* delete IF addr */
#define	OSIOCAIFADDR	 _IOW('i', 26, struct oifaliasreq) /* FreeBSD 9.x */
/*	SIOCALIFADDR	 _IOW('i', 27, struct if_laddrreq) KAME */
/*	SIOCGLIFADDR	_IOWR('i', 28, struct if_laddrreq) KAME */
/*	SIOCDLIFADDR	 _IOW('i', 29, struct if_laddrreq) KAME */
#define	SIOCSIFCAP	 _IOW('i', 30, struct ifreq)	/* set IF features */
#define	SIOCGIFCAP	_IOWR('i', 31, struct ifreq)	/* get IF features */
#define	SIOCGIFINDEX	_IOWR('i', 32, struct ifreq)	/* get IF index */
#define	SIOCGIFMAC	_IOWR('i', 38, struct ifreq)	/* get IF MAC label */
#define	SIOCSIFMAC	 _IOW('i', 39, struct ifreq)	/* set IF MAC label */
#define	SIOCSIFNAME	 _IOW('i', 40, struct ifreq)	/* set IF name */
#define	SIOCSIFDESCR	 _IOW('i', 41, struct ifreq)	/* set ifnet descr */ 
#define	SIOCGIFDESCR	_IOWR('i', 42, struct ifreq)	/* get ifnet descr */ 
#define	SIOCAIFADDR	 _IOW('i', 43, struct ifaliasreq)/* add/chg IF alias */

#define	SIOCADDMULTI	 _IOW('i', 49, struct ifreq)	/* add m'cast addr */
#define	SIOCDELMULTI	 _IOW('i', 50, struct ifreq)	/* del m'cast addr */
#define	SIOCGIFMTU	_IOWR('i', 51, struct ifreq)	/* get IF mtu */
#define	SIOCSIFMTU	 _IOW('i', 52, struct ifreq)	/* set IF mtu */
#define	SIOCGIFPHYS	_IOWR('i', 53, struct ifreq)	/* get IF wire */
#define	SIOCSIFPHYS	 _IOW('i', 54, struct ifreq)	/* set IF wire */
#define	SIOCSIFMEDIA	_IOWR('i', 55, struct ifreq)	/* set net media */
#define	SIOCGIFMEDIA	_IOWR('i', 56, struct ifmediareq) /* get net media */

#define	SIOCSIFGENERIC	 _IOW('i', 57, struct ifreq)	/* generic IF set op */
#define	SIOCGIFGENERIC	_IOWR('i', 58, struct ifreq)	/* generic IF get op */

#define	SIOCGIFSTATUS	_IOWR('i', 59, struct ifstat)	/* get IF status */
#define	SIOCSIFLLADDR	 _IOW('i', 60, struct ifreq)	/* set linklevel addr */
#define	SIOCGI2C	_IOWR('i', 61, struct ifreq)	/* get I2C data  */
#define	SIOCGHWADDR	_IOWR('i', 62, struct ifreq)	/* get hardware lladdr */

#define	SIOCSIFPHYADDR	 _IOW('i', 70, struct ifaliasreq) /* set gif address */
#define	SIOCGIFPSRCADDR	_IOWR('i', 71, struct ifreq)	/* get gif psrc addr */
#define	SIOCGIFPDSTADDR	_IOWR('i', 72, struct ifreq)	/* get gif pdst addr */
#define	SIOCDIFPHYADDR	 _IOW('i', 73, struct ifreq)	/* delete gif addrs */
/*	SIOCSLIFPHYADDR	 _IOW('i', 74, struct if_laddrreq) KAME */
/*	SIOCGLIFPHYADDR	_IOWR('i', 75, struct if_laddrreq) KAME */

#define	SIOCGPRIVATE_0	_IOWR('i', 80, struct ifreq)	/* device private 0 */
#define	SIOCGPRIVATE_1	_IOWR('i', 81, struct ifreq)	/* device private 1 */

#define	SIOCSIFVNET	_IOWR('i', 90, struct ifreq)	/* move IF jail/vnet */
#define	SIOCSIFRVNET	_IOWR('i', 91, struct ifreq)	/* reclaim vnet IF */

#define	SIOCGIFFIB	_IOWR('i', 92, struct ifreq)	/* get IF fib */
#define	SIOCSIFFIB	 _IOW('i', 93, struct ifreq)	/* set IF fib */

#define	SIOCGTUNFIB	_IOWR('i', 94, struct ifreq)	/* get tunnel fib */
#define	SIOCSTUNFIB	 _IOW('i', 95, struct ifreq)	/* set tunnel fib */

#define	SIOCSDRVSPEC	_IOW('i', 123, struct ifdrv)	/* set driver-specific
								  parameters */
#define	SIOCGDRVSPEC	_IOWR('i', 123, struct ifdrv)	/* get driver-specific
								  parameters */

#define	SIOCIFCREATE	_IOWR('i', 122, struct ifreq)	/* create clone if */
#define	SIOCIFCREATE2	_IOWR('i', 124, struct ifreq)	/* create clone if */
#define	SIOCIFDESTROY	 _IOW('i', 121, struct ifreq)	/* destroy clone if */
#define	SIOCIFGCLONERS	_IOWR('i', 120, struct if_clonereq) /* get cloners */

#define	SIOCAIFGROUP	 _IOW('i', 135, struct ifgroupreq) /* add an ifgroup */
#define	SIOCGIFGROUP	_IOWR('i', 136, struct ifgroupreq) /* get ifgroups */
#define	SIOCDIFGROUP	 _IOW('i', 137, struct ifgroupreq) /* delete ifgroup */
#define	SIOCGIFGMEMB	_IOWR('i', 138, struct ifgroupreq) /* get members */
#define	SIOCGIFXMEDIA	_IOWR('i', 139, struct ifmediareq) /* get net xmedia */

#define	SIOCGIFRSSKEY	_IOWR('i', 150, struct ifrsskey)/* get RSS key */
#define	SIOCGIFRSSHASH	_IOWR('i', 151, struct ifrsshash)/* get the current RSS
							type/func settings */

#define	SIOCGLANPCP	_IOWR('i', 152, struct ifreq)	/* Get (V)LAN PCP */
#define	SIOCSLANPCP	 _IOW('i', 153, struct ifreq)	/* Set (V)LAN PCP */

#endif /* !_SYS_SOCKIO_H_ */
