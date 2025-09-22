/*	$OpenBSD: if_wi_hostap.h,v 1.10 2014/08/24 18:01:27 zhuk Exp $	*/

/*
 * Copyright (c) 2002
 *	Thomas Skibo <skibo@pacbell.net>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Thomas Skibo.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Thomas Skibo AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Thomas Skibo OR HIS DRINKING PALS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: $
 */

#ifndef __WI_HOSTAP_H__
#define __WI_HOSTAP_H__

#define WIHAP_MAX_STATIONS	1800

struct hostap_sta {
	u_int8_t	addr[6];
	u_int16_t	asid;
	u_int16_t	flags;
	u_int16_t	sig_info;	/* 15:8 signal, 7:0 noise */
	u_int16_t	capinfo;
	u_int8_t	rates;
};

#define HOSTAP_FLAGS_AUTHEN	0x0001
#define HOSTAP_FLAGS_ASSOC	0x0002
#define HOSTAP_FLAGS_PERM	0x0004
#define	HOSTAP_FLAGS_BITS	"\20\01AUTH\02ASSOC\03PERM"

#define SIOCHOSTAP_GET		_IOWR('i', 200, struct ifreq)
#define SIOCHOSTAP_ADD		_IOWR('i', 201, struct ifreq)
#define SIOCHOSTAP_DEL		_IOWR('i', 202, struct ifreq)
#define SIOCHOSTAP_GETALL	_IOWR('i', 203, struct ifreq)
#define SIOCHOSTAP_GFLAGS	_IOWR('i', 204, struct ifreq)
#define SIOCHOSTAP_SFLAGS	_IOWR('i', 205, struct ifreq)

/* Flags for SIOCHOSTAP_GFLAGS/SFLAGS */
#define WIHAPFL_ACTIVE		0x0001
#define WIHAPFL_MAC_FILT	0x0002

/* Flags set internally only: */
#define WIHAPFL_CANTCHANGE	(WIHAPFL_ACTIVE)

struct hostap_getall {
	int		nstations;
	struct hostap_sta *addr;
	int		size;
};



#ifdef _KERNEL
struct wihap_sta_info {
	TAILQ_ENTRY(wihap_sta_info) list;
	LIST_ENTRY(wihap_sta_info) hash;

	struct wi_softc	*sc;
	u_int8_t	addr[6];
	u_short		flags;
	struct timeout	tmo;

	u_int16_t	asid;
	u_int16_t	capinfo;
	u_int16_t	sig_info;	/* 15:8 signal, 7:0 noise */
	u_int8_t	rates;
	u_int8_t	tx_curr_rate;
	u_int8_t	tx_max_rate;
	u_int32_t	*challenge;
};

#define WI_SIFLAGS_ASSOC	HOSTAP_FLAGS_ASSOC
#define WI_SIFLAGS_AUTHEN	HOSTAP_FLAGS_AUTHEN
#define WI_SIFLAGS_PERM		HOSTAP_FLAGS_PERM
#define WI_SIFLAGS_DEAD		0x1000

#define WI_STA_HASH_SIZE	113

#if WI_STA_HASH_SIZE*16 >= 2007 /* will generate ASID's too large. */
#error "WI_STA_HASH_SIZE too big"
#endif
#if WI_STA_HASH_SIZE*16 < WIHAP_MAX_STATIONS
#error "WI_STA_HASH_SIZE too small"
#endif

struct wihap_info {
	TAILQ_HEAD(sta_list, wihap_sta_info)	sta_list;
	LIST_HEAD(sta_hash, wihap_sta_info)	sta_hash[WI_STA_HASH_SIZE];

	u_int16_t		apflags;

	int			n_stations;
	u_int16_t		asid_inuse_mask[WI_STA_HASH_SIZE];

	int			inactivity_time;
	struct timeout		tmo;
};

#define WIHAP_DFLT_INACTIVITY_TIME	(120) /* 2 minutes */

struct wi_softc;
struct wi_frame;

int	wihap_check_tx(struct wihap_info *, u_int8_t [], u_int8_t *);
int	wihap_data_input(struct wi_softc *, struct wi_frame *, struct mbuf *);
int	wihap_ioctl(struct wi_softc *, u_long, caddr_t);
void	wihap_init(struct wi_softc *);
void	wihap_mgmt_input(struct wi_softc *, struct wi_frame *, struct mbuf *);
void	wihap_shutdown(struct wi_softc *);

#endif /* _KERNEL */
#endif /* __WI_HOSTAP_H__ */
