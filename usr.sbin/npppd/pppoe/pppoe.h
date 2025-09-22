/*	$OpenBSD: pppoe.h,v 1.8 2021/03/29 03:54:39 yasuoka Exp $ */

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
#ifndef	PPPOE_H
#define	PPPOE_H 1

/*
 * Constant variables and types from PPPoE protocol (RFC 2516)
 */
#define PPPOE_RFC2516_TYPE	0x01
#define PPPOE_RFC2516_VER	0x01

/** The PPPoE Active Discovery Initiation (PADI) packet */
#define	PPPOE_CODE_PADI		0x09

/** The PPPoE Active Discovery Offer (PADO) packet */
#define	PPPOE_CODE_PADO		0x07

/** The PPPoE Active Discovery Request (PADR) packet */
#define	PPPOE_CODE_PADR		0x19

/** The PPPoE Active Discovery Session-confirmation (PADS) packet */
#define	PPPOE_CODE_PADS		0x65

/** The PPPoE Active Discovery Terminate (PADT) packet */
#define	PPPOE_CODE_PADT		0xa7

#define	PPPOE_TAG_END_OF_LIST		0x0000
#define	PPPOE_TAG_SERVICE_NAME		0x0101
#define	PPPOE_TAG_AC_NAME		0x0102
#define	PPPOE_TAG_HOST_UNIQ		0x0103
#define	PPPOE_TAG_AC_COOKIE		0x0104
#define	PPPOE_TAG_VENDOR_SPECIFIC	0x0105
#define	PPPOE_TAG_RELAY_SESSION_ID	0x0110
#define	PPPOE_TAG_SERVICE_NAME_ERROR	0x0201
#define	PPPOE_TAG_AC_SYSTEM_ERROR	0x0202
#define	PPPOE_TAG_GENERIC_ERROR		0x0203

/** PPPoE Protocol Header */
struct pppoe_header {
#if BYTE_ORDER == BIG_ENDIAN
    uint8_t ver:4, type:4;
#else
    uint8_t type:4, ver:4;
#endif
    uint8_t code;
    uint16_t session_id;
    uint16_t length;
} __attribute__((__packed__));

/** PPPoE TLV Header */
struct pppoe_tlv {
	uint16_t	type;
	uint16_t	length;
	uint8_t		value[0];
} __attribute__((__packed__));

/*
 * Constant variables and types for implementions
 */
#include "pppoe_conf.h"

/** Default data link layer  */
#define PPPOED_DEFAULT_LAYER2_LABEL	"PPPoE"

#define	PPPOED_CONFIG_BUFSIZ		65535
#define	PPPOED_HOSTUNIQ_LEN		64
#define PPPOED_PHY_LABEL_SIZE		16

/*
 * pppoed status
 */
#define	PPPOED_STATE_INIT 		0 /** Initial */
#define	PPPOED_STATE_RUNNING 		1 /** Running */
#define	PPPOED_STATE_STOPPED 		2 /** Stopped */

#define pppoed_is_stopped(pppoed)	\
	(((pppoed)->state == PPPOED_STATE_STOPPED)? 1 : 0)
#define pppoed_is_running(pppoed)	\
	(((pppoed)->state == PPPOED_STATE_RUNNING)? 1 : 0)

#define	PPPOED_LISTENER_INVALID_INDEX	UINT16_MAX

/** PPPoE listener type */
typedef struct _pppoed_listener {
	/** Descriptor of bpf(4) */
	int bpf;
	/** Context of event(3) for the descriptor of bpf(4) */
	struct event ev_bpf;
	/** Pointer to base PPPoE daemon */
	struct _pppoed *self;
	/** Ethernet address */
	u_char	ether_addr[ETHER_ADDR_LEN];
	/** Listener index numbered  by the base PPPoE daemon */
	uint16_t	index;
	/** Listening interface name */
	char	listen_ifname[IF_NAMESIZE];
	/** Label of physcal layer */
	char	tun_name[PPPOED_PHY_LABEL_SIZE];
	/** Configuration */
	struct pppoe_conf *conf;
} pppoed_listener;

/** PPPoE daemon type */
typedef struct _pppoed {
	/** PPPoE daemon Id */
	int id;
	/** List of {@link pppoed_listener} */
	slist listener;
	/** Status of this daemon */
	int state;

	/** Hashmap that maps from session number to {@link pppoe_session} */
	hash_table	*session_hash;
	/** List of free session numbers */
	slist	session_free_list;

	/** Hashmap that contains uniq cookie value */
	hash_table	*acookie_hash;
	/** Next cookie number */
	uint32_t	acookie_next;

	/** Flags */
	uint32_t
	    listen_incomplete:1,
	    reserved:31;
} pppoed;

/** PPPoE session type */
typedef struct _pppoe_session {
	/** State of this session */
	int 		state;
	/** Pointer to base {@link pppoed *} PPPoE daemon */
	pppoed		*pppoed;
	/** Pointer to the PPP context */
	void 		*ppp;
	/** Session id */
	uint16_t	session_id;
	/** Cookie number */
	int		acookie;
	/** Peer ethernet address */
	u_char 		ether_addr[ETHER_ADDR_LEN];
	/** Index of listener */
	uint16_t	listener_index;
	/** Cache for ethernet frame header */
	struct ether_header ehdr;
	int lcp_echo_interval;			/* XXX can remove */
	int lcp_echo_max_failure;		/* XXX can remove */
	/** Context of event(3) for disposing */
	struct event ev_disposing;
} pppoe_session;

#define	PPPOE_SESSION_STATE_INIT		0 /** Initial */
#define	PPPOE_SESSION_STATE_RUNNING		1 /** Running */
#define	PPPOE_SESSION_STATE_DISPOSING		2 /** Disposing */

#define	pppoed_need_polling(pppoed)	\
    (((pppoed)->listen_incomplete != 0)? 1 : 0)

#ifdef __cplusplus
extern "C" {
#endif

int         pppoe_session_init (pppoe_session *, pppoed *, int, int, u_char *);
void        pppoe_session_fini (pppoe_session *);
void        pppoe_session_stop (pppoe_session *);
int         pppoe_session_recv_PADR (pppoe_session *, slist *);
int         pppoe_session_recv_PADT (pppoe_session *, slist *);
void        pppoe_session_input (pppoe_session *, u_char *, int);
void        pppoe_session_disconnect (pppoe_session *);

int         pppoed_add_listener (pppoed *, int, const char *, const char *);
int         pppoed_reload_listeners(pppoed *);

int         pppoed_init (pppoed *);
int         pppoed_start (pppoed *);
void        pppoed_stop (pppoed *);
void        pppoed_uninit (pppoed *);
void        pppoed_pppoe_session_close_notify(pppoed *, pppoe_session *);
const char *pppoed_tlv_value_string(struct pppoe_tlv *);
int         pppoed_reload(pppoed *, struct pppoe_confs *);

#ifdef __cplusplus
}
#endif
#endif
