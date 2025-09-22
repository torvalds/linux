/*	$OpenBSD: npppd_iface.h,v 1.8 2024/02/26 08:29:37 yasuoka Exp $ */

/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#ifndef NPPPD_IFACE_H
#define NPPPD_IFACE_H 1

typedef struct _npppd_iface {
	/** base of npppd structure */
	void	*npppd;
	/** interface name */
	char	ifname[IFNAMSIZ];
	/** file descriptor for device file */
	int	devf;

	/** assigned IPv4 address */
	struct in_addr	ip4addr;
	/** for event(3)  */
	struct event	ev;

	struct ipcpconf *ipcpconf;

	unsigned int	/**
		 * whether set IP address as npppd_iface's work or not.
		 * <p>if 0, npppd_iface only refers IP address already set.</p>
		 */
		set_ip4addr:1,
		/** set if using pppx(4) rather than tun(4) */
		using_pppx:1,
		/** is initialized */
		initialized:1,
		/** is started */
		started:1;
} npppd_iface;

/** whether interface IP address is usable or not */
#define npppd_iface_ip_is_ready(_iface)		\
    ((_iface)->initialized != 0 && (_iface)->ip4addr.s_addr != INADDR_ANY)

#ifdef __cplusplus
extern "C" {
#endif

void  npppd_iface_init (npppd *, npppd_iface *, struct iface *);
int   npppd_iface_reinit (npppd_iface *, struct iface *);
int   npppd_iface_start (npppd_iface *);
void  npppd_iface_stop (npppd_iface *);
void  npppd_iface_fini (npppd_iface *);
void  npppd_iface_write (npppd_iface *, npppd_ppp *, int proto, u_char *, int);

#ifdef __cplusplus
}
#endif
#endif
