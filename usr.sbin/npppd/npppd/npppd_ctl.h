/*	$OpenBSD: npppd_ctl.h,v 1.7 2017/08/11 16:25:59 goda Exp $ */

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
#ifndef NPPPD_CTL_H
#define NPPPD_CTL_H 1

#include <sys/types.h>
#include <sys/socket.h>		/* for <netinet/in.h> */
#include <net/if.h>		/* for IF_NAMESIZE */
#include <net/if_dl.h>		/* for sockaddr_dl */
#include <netinet/in.h>		/* for sockaddr_in{,6} and in_addr */
#include <imsg.h>		/* for imsg */
#include <time.h>		/* for time_t */

#define	NPPPD_SOCKET			"/var/run/npppd.sock"
#define	NPPPD_CTL_USERNAME_SIZE		256

enum imsg_type {
	IMSG_NONE,
	IMSG_CTL_OK,			/* answers to npppctl requests */
	IMSG_CTL_FAIL,		
	IMSG_CTL_NOP,			/* npppctl requests */
	IMSG_CTL_WHO,
	IMSG_CTL_DISCONNECT,
	IMSG_CTL_MONITOR,
	IMSG_CTL_WHO_AND_MONITOR,
	IMSG_PPP_START,			/* notifies from npppd */
	IMSG_PPP_STOP
};

struct npppd_who {
	u_int             ppp_id;	/** Ppp Id */
	char           	  username[NPPPD_CTL_USERNAME_SIZE];
					/** Username */
	time_t            time;		/** Start time */
	uint32_t          duration_sec;	/** Elapsed time */
	char              ifname[IF_NAMESIZE];
					/** Concentrated interface */
	char              rlmname[32];	/** Authenticated realm name */
	char              tunnel_proto[16];
					/** Tunnel protocol name */
	union {
		struct sockaddr_in  peer_in4;
		struct sockaddr_in6 peer_in6;
		struct sockaddr_dl  peer_dl;
	}                 tunnel_peer; 	/** Tunnel peer address */
	struct in_addr    framed_ip_address;
					/** Framed IP Address */
	uint16_t          mru;		/** MRU */
	uint32_t          ipackets;	/** Numbers of input packets */
	uint32_t          opackets;	/** Numbers of output packets */
	uint32_t          ierrors;	/** Numbers of input error packets */
	uint32_t          oerrors;	/** Numbers of output error packets */
	uint64_t          ibytes;	/** Bytes of input packets */
	uint64_t          obytes;	/** Bytes of output packets */
};

struct npppd_who_list {
	int               more_data;	/** 1 if there is more data */
	int               entry_count;	/** count of the entry */
	struct npppd_who  entry[0];	/** entry arrays */
};

struct npppd_disconnect_request {
	int               count;
	u_int             ppp_id[0];
} ;

struct npppd_disconnect_response {
	int               count;
};
#endif
