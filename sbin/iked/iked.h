/*	$OpenBSD: iked.h,v 1.233 2024/11/04 02:44:28 dlg Exp $	*/

/*
 * Copyright (c) 2019 Tobias Heider <tobias.heider@stusta.de>
 * Copyright (c) 2010-2013 Reyk Floeter <reyk@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/tree.h>
#include <sys/queue.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <imsg.h>

#include <openssl/evp.h>

#include "types.h"
#include "dh.h"

#define MAXIMUM(a,b) (((a)>(b))?(a):(b))
#define MINIMUM(a,b) (((a)<(b))?(a):(b))
#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))

#ifndef IKED_H
#define IKED_H

/*
 * Common IKEv1/IKEv2 header
 */

struct ike_header {
	uint64_t	 ike_ispi;		/* Initiator cookie */
	uint64_t	 ike_rspi;		/* Responder cookie */
	uint8_t		 ike_nextpayload;	/* Next payload type */
	uint8_t		 ike_version;		/* Major/Minor version number */
	uint8_t		 ike_exchange;		/* Exchange type */
	uint8_t		 ike_flags;		/* Message options */
	uint32_t	 ike_msgid;		/* Message identifier */
	uint32_t	 ike_length;		/* Total message length */
} __packed;

/*
 * Common daemon infrastructure, local imsg etc.
 */

struct imsgev {
	struct imsgbuf		 ibuf;
	void			(*handler)(int, short, void *);
	struct event		 ev;
	struct privsep_proc	*proc;
	void			*data;
	short			 events;
	const char		*name;
};

#define IMSG_SIZE_CHECK(imsg, p) do {				\
	if (IMSG_DATA_SIZE(imsg) < sizeof(*p))			\
		fatalx("bad length imsg received");		\
} while (0)
#define IMSG_DATA_SIZE(imsg)	((imsg)->hdr.len - IMSG_HEADER_SIZE)

#define IKED_ADDR_EQ(_a, _b)						\
	((_a)->addr_mask == (_b)->addr_mask &&				\
	sockaddr_cmp((struct sockaddr *)&(_a)->addr,			\
	(struct sockaddr *)&(_b)->addr, (_a)->addr_mask) == 0)

#define IKED_ADDR_NEQ(_a, _b)						\
	((_a)->addr_mask != (_b)->addr_mask ||				\
	sockaddr_cmp((struct sockaddr *)&(_a)->addr,			\
	(struct sockaddr *)&(_b)->addr, (_a)->addr_mask) != 0)

/* initially control.h */
struct control_sock {
	const char	*cs_name;
	struct event	 cs_ev;
	struct event	 cs_evt;
	int		 cs_fd;
	int		 cs_restricted;
	void		*cs_env;
};

struct ctl_conn {
	TAILQ_ENTRY(ctl_conn)	 entry;
	uint8_t			 flags;
#define CTL_CONN_NOTIFY		 0x01
	struct imsgev		 iev;
	uint32_t		 peerid;
};
TAILQ_HEAD(ctl_connlist, ctl_conn);

extern enum privsep_procid privsep_process;

/*
 * Runtime structures
 */

struct iked_timer {
	struct event	 tmr_ev;
	struct iked	*tmr_env;
	void		(*tmr_cb)(struct iked *, void *);
	void		*tmr_cbarg;
};

struct iked_spi {
	uint64_t	 spi;
	uint8_t		 spi_size;
	uint8_t		 spi_protoid;
};

struct iked_proposal {
	uint8_t				 prop_id;
	uint8_t				 prop_protoid;

	struct iked_spi			 prop_localspi;
	struct iked_spi			 prop_peerspi;

	struct iked_transform		*prop_xforms;
	unsigned int			 prop_nxforms;

	TAILQ_ENTRY(iked_proposal)	 prop_entry;
};
TAILQ_HEAD(iked_proposals, iked_proposal);

struct iked_addr {
	int				 addr_af;
	struct sockaddr_storage		 addr;
	uint8_t				 addr_mask;
	int				 addr_net;
	in_port_t			 addr_port;
};

struct iked_ts {
	struct iked_addr		 ts_addr;
	uint8_t				 ts_ipproto;
	TAILQ_ENTRY(iked_ts)		 ts_entry;
};
TAILQ_HEAD(iked_tss, iked_ts);

struct iked_flow {
	struct iked_addr		 flow_src;
	struct iked_addr		 flow_dst;
	unsigned int			 flow_dir;	/* in/out */
	int				 flow_rdomain;
	struct iked_addr		 flow_prenat;
	int				 flow_fixed;

	unsigned int			 flow_loaded;	/* pfkey done */

	uint8_t				 flow_saproto;
	uint8_t				 flow_ipproto;

	struct iked_addr		*flow_local;	/* outer source */
	struct iked_addr		*flow_peer;	/* outer dest */
	struct iked_sa			*flow_ikesa;	/* parent SA */

	RB_ENTRY(iked_flow)		 flow_node;
	TAILQ_ENTRY(iked_flow)		 flow_entry;
};
RB_HEAD(iked_flows, iked_flow);
TAILQ_HEAD(iked_saflows, iked_flow);

struct iked_childsa {
	uint8_t				 csa_saproto;	/* IPsec protocol */
	unsigned int			 csa_dir;	/* in/out */

	uint64_t			 csa_peerspi;	/* peer relation */
	uint8_t				 csa_loaded;	/* pfkey done */
	uint8_t				 csa_rekey;	/* will be deleted */
	uint8_t				 csa_allocated;	/* from the kernel */
	uint8_t				 csa_persistent;/* do not rekey */
	uint8_t				 csa_esn;	/* use ESN */
	uint8_t				 csa_transport;	/* transport mode */

	struct iked_spi			 csa_spi;

	struct ibuf			*csa_encrkey;	/* encryption key */
	uint16_t			 csa_encrid;	/* encryption xform id */

	struct ibuf			*csa_integrkey;	/* auth key */
	uint16_t			 csa_integrid;	/* auth xform id */

	struct iked_addr		*csa_local;	/* outer source */
	struct iked_addr		*csa_peer;	/* outer dest */
	struct iked_sa			*csa_ikesa;	/* parent SA */

	struct iked_childsa		*csa_peersa;	/* peer */

	struct iked_childsa		*csa_bundled;	/* IPCOMP */

	uint16_t			 csa_pfsgrpid;	/* pfs group id */

	RB_ENTRY(iked_childsa)		 csa_node;
	TAILQ_ENTRY(iked_childsa)	 csa_entry;
};
RB_HEAD(iked_activesas, iked_childsa);
TAILQ_HEAD(iked_childsas, iked_childsa);


struct iked_static_id {
	uint8_t		id_type;
	uint8_t		id_length;
	uint8_t		id_offset;
	uint8_t		id_data[IKED_ID_SIZE];
};

struct iked_auth {
	uint8_t		auth_method;
	uint8_t		auth_length;			/* zero if EAP */
	uint16_t	auth_eap;			/* optional EAP */
	uint8_t		auth_data[IKED_PSK_SIZE];
};

struct iked_cfg {
	uint8_t				 cfg_action;
	uint16_t			 cfg_type;
	union {
		struct iked_addr	 address;
	} cfg;
};

TAILQ_HEAD(iked_sapeers, iked_sa);

struct iked_lifetime {
	uint64_t			 lt_bytes;
	uint64_t			 lt_seconds;
};

struct iked_policy {
	unsigned int			 pol_id;
	char				 pol_name[IKED_ID_SIZE];
	unsigned int			 pol_iface;

#define IKED_SKIP_FLAGS			 0
#define IKED_SKIP_AF			 1
#define IKED_SKIP_SRC_ADDR		 2
#define IKED_SKIP_DST_ADDR		 3
#define IKED_SKIP_COUNT			 4
	struct iked_policy		*pol_skip[IKED_SKIP_COUNT];

	unsigned int			 pol_flags;
#define IKED_POLICY_PASSIVE		 0x000
#define IKED_POLICY_DEFAULT		 0x001
#define IKED_POLICY_ACTIVE		 0x002
#define IKED_POLICY_REFCNT		 0x004
#define IKED_POLICY_QUICK		 0x008
#define IKED_POLICY_SKIP		 0x010
#define IKED_POLICY_IPCOMP		 0x020
#define IKED_POLICY_TRANSPORT		 0x040
#define IKED_POLICY_ROUTING		 0x080
#define IKED_POLICY_NATT_FORCE		 0x100

	int				 pol_refcnt;

	uint8_t				 pol_certreqtype;

	int				 pol_af;
	int				 pol_rdomain;
	uint8_t				 pol_saproto;
	unsigned int			 pol_ipproto[IKED_IPPROTO_MAX];
	unsigned int			 pol_nipproto;

	struct iked_addr		 pol_peer;
	struct iked_static_id		 pol_peerid;
	uint32_t			 pol_peerdh;

	struct iked_addr		 pol_local;
	struct iked_static_id		 pol_localid;

	struct iked_auth		 pol_auth;

	char				 pol_tag[IKED_TAG_SIZE];
	unsigned int			 pol_tap;

	struct iked_proposals		 pol_proposals;
	size_t				 pol_nproposals;

	struct iked_flows		 pol_flows;
	size_t				 pol_nflows;
	struct iked_tss			 pol_tssrc;	/* Traffic Selectors Initiator*/
	size_t				 pol_tssrc_count;
	struct iked_tss			 pol_tsdst;	/* Traffic Selectors Responder*/
	size_t				 pol_tsdst_count;

	struct iked_cfg			 pol_cfg[IKED_CFG_MAX];
	unsigned int			 pol_ncfg;

	uint32_t			 pol_rekey;	/* ike SA lifetime */
	struct iked_lifetime		 pol_lifetime;	/* child SA lifetime */

	struct iked_sapeers		 pol_sapeers;

	TAILQ_ENTRY(iked_policy)	 pol_entry;
};
TAILQ_HEAD(iked_policies, iked_policy);

struct iked_hash {
	uint8_t		 hash_type;	/* PRF or INTEGR */
	uint16_t	 hash_id;	/* IKE PRF/INTEGR hash id */
	const void	*hash_priv;	/* Identifying the hash alg */
	void		*hash_ctx;	/* Context of the current invocation */
	int		 hash_fixedkey;	/* Requires fixed key length */
	struct ibuf	*hash_key;	/* MAC key derived from key seed */
	size_t		 hash_length;	/* Output length */
	size_t		 hash_trunc;	/* Truncate the output length */
	struct iked_hash *hash_prf;	/* PRF pointer */
	int		 hash_isaead;
};

struct iked_cipher {
	uint8_t		 encr_type;	/* ENCR */
	uint16_t	 encr_id;	/* IKE ENCR hash id */
	const void	*encr_priv;	/* Identifying the hash alg */
	void		*encr_ctx;	/* Context of the current invocation */
	int		 encr_fixedkey;	/* Requires fixed key length */
	struct ibuf	*encr_key;	/* MAC key derived from key seed */
	struct ibuf	*encr_iv;	/* Initialization Vector */
	uint64_t	 encr_civ;	/* Counter IV for GCM */
	size_t		 encr_ivlength;	/* IV length */
	size_t		 encr_length;	/* Block length */
	size_t		 encr_saltlength;	/* IV salt length */
	uint16_t	 encr_authid;	/* ID of associated authentication */
};

struct iked_dsa {
	uint8_t		 dsa_method;	/* AUTH method */
	const void	*dsa_priv;	/* PRF or signature hash function */
	void		*dsa_ctx;	/* PRF or signature hash ctx */
	struct ibuf	*dsa_keydata;	/* public, private or shared key */
	void		*dsa_key;	/* parsed public or private key */
	int		 dsa_hmac;	/* HMAC or public/private key */
	int		 dsa_sign;	/* Sign or verify operation */
	uint32_t	 dsa_flags;	/* State flags */
};

struct iked_id {
	uint8_t		 id_type;
	uint8_t		 id_offset;
	struct ibuf	*id_buf;
};

#define IKED_REQ_CERT		0x0001	/* get local certificate (if required) */
#define IKED_REQ_CERTVALID	0x0002	/* validated the peer cert */
#define IKED_REQ_CERTREQ	0x0004	/* CERTREQ has been received */
#define IKED_REQ_AUTH		0x0008	/* AUTH payload */
#define IKED_REQ_AUTHVALID	0x0010	/* AUTH payload has been verified */
#define IKED_REQ_SA		0x0020	/* SA available */
#define IKED_REQ_EAPVALID	0x0040	/* EAP payload has been verified */
#define IKED_REQ_CHILDSA	0x0080	/* Child SA initiated */
#define IKED_REQ_INF		0x0100	/* Informational exchange initiated */

#define IKED_REQ_BITS	\
    "\20\01CERT\02CERTVALID\03CERTREQ\04AUTH\05AUTHVALID\06SA\07EAPVALID" \
    "\10CHILDSA\11INF"

TAILQ_HEAD(iked_msgqueue, iked_msg_retransmit);
TAILQ_HEAD(iked_msg_fragqueue, iked_message);

struct iked_sahdr {
	uint64_t			 sh_ispi;	/* Initiator SPI */
	uint64_t			 sh_rspi;	/* Responder SPI */
	unsigned int			 sh_initiator;	/* Is initiator? */
} __packed;

struct iked_kex {
	struct ibuf			*kex_inonce;	/* Ni */
	struct ibuf			*kex_rnonce;	/* Nr */

	struct dh_group			*kex_dhgroup;	/* DH group */
	struct ibuf			*kex_dhiexchange;
	struct ibuf			*kex_dhrexchange;
	struct ibuf			*kex_dhpeer;	/* pointer to i or r */
};

struct iked_frag_entry {
	uint8_t	*frag_data;
	size_t	 frag_size;
};

struct iked_frag {
	struct iked_frag_entry	**frag_arr;	/* list of fragment buffers */
	size_t			  frag_count;	/* number of fragments received */
#define IKED_FRAG_TOTAL_MAX	  111		/* upper limit (64kB / 576B) */
	size_t			  frag_total;	/* total numbe of fragments */
	size_t			  frag_total_size;
	uint8_t			  frag_nextpayload;

};

struct iked_ipcomp {
	uint16_t			 ic_cpi_out;	/* outgoing CPI */
	uint16_t			 ic_cpi_in;	/* incoming CPI */
	uint8_t				 ic_transform;	/* transform */
};

struct iked_sastats {
	uint64_t			 sas_ipackets;
	uint64_t			 sas_opackets;
	uint64_t			 sas_ibytes;
	uint64_t			 sas_obytes;
	uint64_t			 sas_idrops;
	uint64_t			 sas_odrops;
};

struct iked_sa {
	struct iked_sahdr		 sa_hdr;
	uint32_t			 sa_msgid;	/* Last request rcvd */
	int				 sa_msgid_set;	/* msgid initialized */
	uint32_t			 sa_msgid_current;	/* Current requested rcvd */
	uint32_t			 sa_reqid;	/* Next request sent */

	int				 sa_type;
#define IKED_SATYPE_LOOKUP		 0		/* Used for lookup */
#define IKED_SATYPE_LOCAL		 1		/* Local SA */

	struct iked_addr		 sa_peer;
	struct iked_addr		 sa_peer_loaded;/* MOBIKE */
	struct iked_addr		 sa_local;
	int				 sa_fd;

	struct iked_frag		 sa_fragments;

	int				 sa_natt;	/* for IKE messages */
	int				 sa_udpencap;	/* for pfkey */
	int				 sa_usekeepalive;/* NAT-T keepalive */

	int				 sa_state;
	unsigned int			 sa_stateflags;
	unsigned int			 sa_stateinit;	/* SA_INIT */
	unsigned int			 sa_statevalid;	/* IKE_AUTH */

	int				 sa_cp;		/* XXX */
	struct iked_addr		*sa_cp_addr;	/* requested address */
	struct iked_addr		*sa_cp_addr6;	/* requested address */
	struct iked_addr		*sa_cp_dns;	/* requested dns */

	struct iked_policy		*sa_policy;
	struct timeval			 sa_timecreated;
	struct timeval			 sa_timeused;

	char				*sa_tag;
	const char			*sa_reason;	/* reason for close */

	struct iked_kex			 sa_kex;
/* XXX compat defines until everything is converted */
#define sa_inonce		sa_kex.kex_inonce
#define sa_rnonce		sa_kex.kex_rnonce
#define sa_dhgroup		sa_kex.kex_dhgroup
#define sa_dhiexchange		sa_kex.kex_dhiexchange
#define sa_dhrexchange		sa_kex.kex_dhrexchange
#define sa_dhpeer		sa_kex.kex_dhpeer

	struct iked_hash		*sa_prf;	/* PRF alg */
	struct iked_hash		*sa_integr;	/* integrity alg */
	struct iked_cipher		*sa_encr;	/* encryption alg */

	struct ibuf			*sa_key_d;	/* SK_d */
	struct ibuf			*sa_key_iauth;	/* SK_ai */
	struct ibuf			*sa_key_rauth;	/* SK_ar */
	struct ibuf			*sa_key_iencr;	/* SK_ei */
	struct ibuf			*sa_key_rencr;	/* SK_er */
	struct ibuf			*sa_key_iprf;	/* SK_pi */
	struct ibuf			*sa_key_rprf;	/* SK_pr */

	struct ibuf			*sa_1stmsg;	/* for initiator AUTH */
	struct ibuf			*sa_2ndmsg;	/* for responder AUTH */
	struct iked_id			 sa_localauth;	/* local AUTH message */
	struct iked_id			 sa_peerauth;	/* peer AUTH message */
	int				 sa_sigsha2;	/* use SHA2 for signatures */
#define IKED_SCERT_MAX	3 /* max # of supplemental cert payloads */

	struct iked_id			 sa_iid;	/* initiator id */
	struct iked_id			 sa_rid;	/* responder id */
	struct iked_id			 sa_icert;	/* initiator cert */
	struct iked_id			 sa_rcert;	/* responder cert */
	struct iked_id			 sa_scert[IKED_SCERT_MAX]; /* supplemental certs */
#define IKESA_SRCID(x) ((x)->sa_hdr.sh_initiator ? &(x)->sa_iid : &(x)->sa_rid)
#define IKESA_DSTID(x) ((x)->sa_hdr.sh_initiator ? &(x)->sa_rid : &(x)->sa_iid)

	char				*sa_eapid;	/* EAP identity */
	struct iked_id			 sa_eap;	/* EAP challenge */
	struct ibuf			*sa_eapmsk;	/* EAK session key */
	struct ibuf			*sa_eapclass;	/* EAP/RADIUS class */

	struct iked_proposals		 sa_proposals;	/* SA proposals */
	struct iked_childsas		 sa_childsas;	/* IPsec Child SAs */
	struct iked_saflows		 sa_flows;	/* IPsec flows */
	struct iked_sastats		 sa_stats;

	struct iked_sa			*sa_nexti;	/* initiated IKE SA */
	struct iked_sa			*sa_previ;	/* matching back pointer */
	struct iked_sa			*sa_nextr;	/* simultaneous rekey */
	struct iked_sa			*sa_prevr;	/* matching back pointer */
	uint64_t			 sa_rekeyspi;	/* peerspi CSA rekey */
	struct ibuf			*sa_simult;	/* simultaneous rekey */

	struct iked_ipcomp		 sa_ipcompi;	/* IPcomp initator */
	struct iked_ipcomp		 sa_ipcompr;	/* IPcomp responder */

	int				 sa_mobike;	/* MOBIKE */
	int				 sa_frag;	/* fragmentation */

	int				 sa_use_transport_mode;	/* peer requested */
	int				 sa_used_transport_mode; /* we enabled */

	struct iked_timer		 sa_timer;	/* SA timeouts */
#define IKED_IKE_SA_EXCHANGE_TIMEOUT	 300		/* 5 minutes */
#define IKED_IKE_SA_REKEY_TIMEOUT	 120		/* 2 minutes */
#define IKED_IKE_SA_DELETE_TIMEOUT	 120		/* 2 minutes */
#define IKED_IKE_SA_ALIVE_TIMEOUT	 60		/* 1 minute */

	struct iked_timer		 sa_keepalive;	/* keepalive timer */
#define IKED_IKE_SA_KEEPALIVE_TIMEOUT	 20

	struct iked_timer		 sa_rekey;	/* rekey timeout */
	int				 sa_tmpfail;

	struct iked_msgqueue		 sa_requests;	/* request queue */
#define IKED_RETRANSMIT_TIMEOUT		 2		/* 2 seconds */

	struct iked_msgqueue		 sa_responses;	/* response queue */
#define IKED_RESPONSE_TIMEOUT		 120		/* 2 minutes */

	TAILQ_ENTRY(iked_sa)		 sa_peer_entry;
	RB_ENTRY(iked_sa)		 sa_entry;	/* all SAs */

	RB_ENTRY(iked_sa)		 sa_dstid_entry;	/* SAs by DSTID */
	int				 sa_dstid_entry_valid;		/* sa_dstid_entry valid */

	struct iked_addr		*sa_addrpool;	/* address from pool */
	RB_ENTRY(iked_sa)		 sa_addrpool_entry;	/* pool entries */

	struct iked_addr		*sa_addrpool6;	/* address from pool */
	RB_ENTRY(iked_sa)		 sa_addrpool6_entry;	/* pool entries */
	time_t				 sa_last_recvd;
#define IKED_IKE_SA_LAST_RECVD_TIMEOUT	 300		/* 5 minutes */
	struct timespec			 sa_starttime;

	struct iked_radserver_req	*sa_radreq;
	struct iked_addr		*sa_rad_addr;	/* requested address */
	struct iked_addr		*sa_rad_addr6;	/* requested address */
};
RB_HEAD(iked_sas, iked_sa);
RB_HEAD(iked_dstid_sas, iked_sa);
RB_HEAD(iked_addrpool, iked_sa);
RB_HEAD(iked_addrpool6, iked_sa);

/* stats */

struct iked_stats {
	uint64_t	ikes_sa_created;
	uint64_t	ikes_sa_established_total;
	uint64_t	ikes_sa_established_current;	/* gauge */
	uint64_t	ikes_sa_established_failures;
	uint64_t	ikes_sa_proposals_negotiate_failures;
	uint64_t	ikes_sa_rekeyed;
	uint64_t	ikes_sa_removed;
	uint64_t	ikes_csa_created;
	uint64_t	ikes_csa_removed;
	uint64_t	ikes_msg_sent;
	uint64_t	ikes_msg_send_failures;
	uint64_t	ikes_msg_rcvd;
	uint64_t	ikes_msg_rcvd_busy;
	uint64_t	ikes_msg_rcvd_dropped;
	uint64_t	ikes_retransmit_request;
	uint64_t	ikes_retransmit_response;
	uint64_t	ikes_retransmit_limit;
	uint64_t	ikes_frag_sent;
	uint64_t	ikes_frag_send_failures;
	uint64_t	ikes_frag_rcvd;
	uint64_t	ikes_frag_rcvd_drop;
	uint64_t	ikes_frag_reass_ok;
	uint64_t	ikes_frag_reass_drop;
	uint64_t	ikes_update_addresses_sent;
	uint64_t	ikes_dpd_sent;
	uint64_t	ikes_keepalive_sent;
};

#define ikestat_add(env, c, n)	do { env->sc_stats.c += (n); } while(0)
#define ikestat_inc(env, c)	ikestat_add(env, c, 1)
#define ikestat_dec(env, c)	ikestat_add(env, c, -1)

struct iked_certreq {
	struct ibuf			*cr_data;
	uint8_t				 cr_type;
	SIMPLEQ_ENTRY(iked_certreq)	 cr_entry;
};
SIMPLEQ_HEAD(iked_certreqs, iked_certreq);

#define EAP_STATE_IDENTITY		(1)
#define EAP_STATE_MSCHAPV2_CHALLENGE	(2)
#define EAP_STATE_MSCHAPV2_SUCCESS	(3)
#define EAP_STATE_SUCCESS		(4)

struct eap_msg {
	char		*eam_identity;
	char		*eam_user;
	int		 eam_type;
	uint8_t		 eam_id;
	uint8_t		 eam_msrid;
	int		 eam_success;
	int		 eam_found;
	int		 eam_response;
	uint8_t		 eam_challenge[16];
	uint8_t		 eam_ntresponse[24];
	uint32_t	 eam_state;
};

struct iked_message {
	struct ibuf		*msg_data;
	size_t			 msg_offset;

	struct sockaddr_storage	 msg_local;
	socklen_t		 msg_locallen;

	struct sockaddr_storage	 msg_peer;
	socklen_t		 msg_peerlen;

	struct iked_socket	*msg_sock;

	int			 msg_fd;
	int			 msg_response;
	int			 msg_responded;
	int			 msg_valid;
	int			 msg_natt;
	int			 msg_natt_rcvd;
	int			 msg_nat_detected;
	int			 msg_error;
	int			 msg_e;
	struct iked_message	*msg_parent;

	/* Associated policy and SA */
	struct iked_policy	*msg_policy;
	struct iked_sa		*msg_sa;

	uint32_t		 msg_msgid;
	uint8_t			 msg_exchange;

	/* Parsed information */
	struct iked_proposals	 msg_proposals;
	struct iked_certreqs	 msg_certreqs;
	struct iked_spi		 msg_rekey;
	struct ibuf		*msg_nonce;	/* dh NONCE */
	uint16_t		 msg_dhgroup;	/* dh group */
	struct ibuf		*msg_ke;	/* dh key exchange */
	struct iked_id		 msg_auth;	/* AUTH payload */
	struct iked_id		 msg_peerid;
	struct iked_id		 msg_localid;
	struct iked_id		 msg_cert;
	struct iked_id		 msg_scert[IKED_SCERT_MAX]; /* supplemental certs */
	struct ibuf		*msg_cookie;
	uint16_t		 msg_group;
	uint16_t		 msg_cpi;
	uint8_t			 msg_transform;
	uint16_t		 msg_flags;
	struct eap_msg		 msg_eap;
	struct ibuf		*msg_eapmsg;
	size_t			 msg_del_spisize;
	size_t			 msg_del_cnt;
	struct ibuf		*msg_del_buf;
	int			 msg_del_protoid;
	int			 msg_cp;
	struct iked_addr	*msg_cp_addr;	/* requested address */
	struct iked_addr	*msg_cp_addr6;	/* requested address */
	struct iked_addr	*msg_cp_dns;	/* requested dns */
	uint16_t		 msg_frag_num;

	/* MOBIKE */
	int			 msg_update_sa_addresses;
	struct ibuf		*msg_cookie2;

	/* Parse stack */
	struct iked_proposal	*msg_prop;
	uint16_t		 msg_attrlength;

	/* Retransmit queue */
	TAILQ_ENTRY(iked_message)
				 msg_entry;
};

struct iked_msg_retransmit {
	struct iked_msg_fragqueue	      mrt_frags;
	TAILQ_ENTRY(iked_msg_retransmit)      mrt_entry;
	struct iked_timer		      mrt_timer;
	int				      mrt_tries;
#define IKED_RETRANSMIT_TRIES	 5		/* try 5 times */
};

#define IKED_MSG_NAT_SRC_IP				0x01
#define IKED_MSG_NAT_DST_IP				0x02

#define IKED_MSG_FLAGS_FRAGMENTATION			0x0001
#define IKED_MSG_FLAGS_MOBIKE				0x0002
#define IKED_MSG_FLAGS_SIGSHA2				0x0004
#define IKED_MSG_FLAGS_CHILD_SA_NOT_FOUND		0x0008
#define IKED_MSG_FLAGS_NO_ADDITIONAL_SAS		0x0010
#define IKED_MSG_FLAGS_AUTHENTICATION_FAILED		0x0020
#define IKED_MSG_FLAGS_INVALID_KE			0x0040
#define IKED_MSG_FLAGS_IPCOMP_SUPPORTED			0x0080
#define IKED_MSG_FLAGS_USE_TRANSPORT			0x0100
#define IKED_MSG_FLAGS_TEMPORARY_FAILURE		0x0200
#define IKED_MSG_FLAGS_NO_PROPOSAL_CHOSEN		0x0400


struct iked_user {
	char			 usr_name[LOGIN_NAME_MAX];
	char			 usr_pass[IKED_PASSWORD_SIZE];
	RB_ENTRY(iked_user)	 usr_entry;
};
RB_HEAD(iked_users, iked_user);

struct iked_radserver_req;

struct iked_radserver {
	int				 rs_sock;
	int				 rs_accounting;
	struct event			 rs_ev;
	struct iked			*rs_env;
	struct sockaddr_storage		 rs_sockaddr;
	TAILQ_ENTRY(iked_radserver)	 rs_entry;
	struct in_addr			 rs_nas_ipv4;
	struct in6_addr			 rs_nas_ipv6;
	unsigned int			 rs_reqseq;
	TAILQ_HEAD(, iked_radserver_req) rs_reqs;
	char				 rs_secret[];
};
TAILQ_HEAD(iked_radservers, iked_radserver);

struct iked_raddae {
	int				 rd_sock;
	struct event			 rd_ev;
	struct iked			*rd_env;
	struct sockaddr_storage		 rd_sockaddr;
	TAILQ_ENTRY(iked_raddae)	 rd_entry;
};
TAILQ_HEAD(iked_raddaes, iked_raddae);

struct iked_radclient {
	struct iked			*rc_env;
	struct sockaddr_storage		 rc_sockaddr;
	TAILQ_ENTRY(iked_radclient)	 rc_entry;
	char				 rc_secret[];
};
TAILQ_HEAD(iked_radclients , iked_radclient);

struct iked_radopts {
	int				 max_tries;
	int				 max_failovers;
};

struct iked_radcfgmap {
	uint16_t			 cfg_type;
	uint32_t			 vendor_id;
	uint8_t				 attr_type;
	TAILQ_ENTRY(iked_radcfgmap)	 entry;
};
TAILQ_HEAD(iked_radcfgmaps, iked_radcfgmap);

extern const struct iked_radcfgmap radius_cfgmaps[];

struct iked_radserver_req {
	struct iked_radserver		*rr_server;
	struct iked_sa			*rr_sa;
	struct iked_timer		 rr_timer;
	int				 rr_reqid;
	int				 rr_accounting;
	struct timespec			 rr_accttime;
	void				*rr_reqpkt;
	struct ibuf			*rr_state;
	char				*rr_user;
	int				 rr_ntry;
	int				 rr_nfailover;
	struct iked_cfg			 rr_cfg[IKED_CFG_MAX];
	unsigned int			 rr_ncfg;
	TAILQ_ENTRY(iked_radserver_req)	 rr_entry;
};

struct privsep_pipes {
	int				*pp_pipes[PROC_MAX];
};

struct privsep {
	struct privsep_pipes		*ps_pipes[PROC_MAX];
	struct privsep_pipes		*ps_pp;

	struct imsgev			*ps_ievs[PROC_MAX];
	const char			*ps_title[PROC_MAX];
	pid_t				 ps_pid[PROC_MAX];
	struct passwd			*ps_pw;
	int				 ps_noaction;

	struct control_sock		 ps_csock;

	unsigned int			 ps_instances[PROC_MAX];
	unsigned int			 ps_ninstances;
	unsigned int			 ps_instance;

	/* Event and signal handlers */
	struct event			 ps_evsigint;
	struct event			 ps_evsigterm;
	struct event			 ps_evsigchld;
	struct event			 ps_evsighup;
	struct event			 ps_evsigpipe;
	struct event			 ps_evsigusr1;

	struct iked			*ps_env;
	unsigned int			 ps_connecting;
	void				(*ps_connected)(struct privsep *);
};

struct privsep_proc {
	const char		*p_title;
	enum privsep_procid	 p_id;
	int			(*p_cb)(int, struct privsep_proc *,
				    struct imsg *);
	void			(*p_init)(struct privsep *,
				    struct privsep_proc *);
	const char		*p_chroot;
	struct passwd		*p_pw;
	struct privsep		*p_ps;
	void			(*p_shutdown)(void);
};

struct privsep_fd {
	enum privsep_procid		 pf_procid;
	unsigned int			 pf_instance;
};

#define PROC_PARENT_SOCK_FILENO 3
#define PROC_MAX_INSTANCES      32

struct iked_ocsp_entry {
	TAILQ_ENTRY(iked_ocsp_entry) ioe_entry;	/* next request */
	void			*ioe_ocsp;	/* private ocsp request data */
};
TAILQ_HEAD(iked_ocsp_requests, iked_ocsp_entry);

/*
 * Daemon configuration
 */

enum natt_mode {
	NATT_DEFAULT,	/* send/recv with both :500 and NAT-T port */
	NATT_DISABLE,	/* send/recv with only :500 */
	NATT_FORCE,	/* send/recv with only NAT-T port */
};

struct iked_static {
	uint64_t		 st_alive_timeout;
	int			 st_cert_partial_chain;
	int			 st_enforcesingleikesa;
	uint8_t			 st_frag;	/* fragmentation */
	uint8_t			 st_mobike;	/* MOBIKE */
	in_port_t		 st_nattport;
	int			 st_stickyaddress; /* addr per DSTID  */
	int			 st_vendorid;
};

struct iked {
	char				 sc_conffile[PATH_MAX];

	uint32_t			 sc_opts;
	enum natt_mode			 sc_nattmode;
	uint8_t				 sc_passive;
	uint8_t				 sc_decoupled;

	struct iked_static		 sc_static;

#define sc_alive_timeout	sc_static.st_alive_timeout
#define sc_cert_partial_chain	sc_static.st_cert_partial_chain
#define sc_enforcesingleikesa	sc_static.st_enforcesingleikesa
#define sc_frag			sc_static.st_frag
#define sc_mobike		sc_static.st_mobike
#define sc_nattport		sc_static.st_nattport
#define sc_stickyaddress	sc_static.st_stickyaddress
#define sc_vendorid		sc_static.st_vendorid

	struct iked_policies		 sc_policies;
	struct iked_policy		*sc_defaultcon;

	struct iked_sas			 sc_sas;
	struct iked_dstid_sas		 sc_dstid_sas;
	struct iked_activesas		 sc_activesas;
	struct iked_flows		 sc_activeflows;
	struct iked_users		 sc_users;
	struct iked_radopts		 sc_radauth;
	struct iked_radopts		 sc_radacct;
	int				 sc_radaccton;
	struct iked_radservers		 sc_radauthservers;
	struct iked_radservers		 sc_radacctservers;
	struct iked_radcfgmaps		 sc_radcfgmaps;
	struct iked_raddaes		 sc_raddaes;
	struct iked_radclients		 sc_raddaeclients;

	struct iked_stats		 sc_stats;

	void				*sc_priv;	/* per-process */

	int				 sc_pfkey;	/* ike process */
	struct event			 sc_pfkeyev;
	struct event			 sc_routeev;
	uint8_t				 sc_certreqtype;
	struct ibuf			*sc_certreq;
	void				*sc_vroute;

	struct iked_socket		*sc_sock4[2];
	struct iked_socket		*sc_sock6[2];

	struct iked_timer		 sc_inittmr;
#define IKED_INITIATOR_INITIAL		 2
#define IKED_INITIATOR_INTERVAL		 60

	struct privsep			 sc_ps;

	struct iked_ocsp_requests	 sc_ocsp;
	char				*sc_ocsp_url;
	long				 sc_ocsp_tolerate;
	long				 sc_ocsp_maxage;

	struct iked_addrpool		 sc_addrpool;
	struct iked_addrpool6		 sc_addrpool6;
};

struct iked_socket {
	int			 sock_fd;
	struct event		 sock_ev;
	struct iked		*sock_env;
	struct sockaddr_storage	 sock_addr;
};

struct ipsec_xf {
	const char	*name;
	unsigned int	 id;
	unsigned int	 length;
	unsigned int	 keylength;
	unsigned int	 nonce;
	unsigned int	 noauth;
};

struct ipsec_transforms {
	const struct ipsec_xf	**authxf;
	unsigned int		  nauthxf;
	const struct ipsec_xf	**prfxf;
	unsigned int		  nprfxf;
	const struct ipsec_xf	**encxf;
	unsigned int		  nencxf;
	const struct ipsec_xf	**groupxf;
	unsigned int		  ngroupxf;
	const struct ipsec_xf	**esnxf;
	unsigned int		  nesnxf;
};

struct ipsec_mode {
	struct ipsec_transforms	**xfs;
	unsigned int		  nxfs;
};

/* iked.c */
void	 parent_reload(struct iked *, int, const char *);

extern struct iked	*iked_env;

/* control.c */
void	 control(struct privsep *, struct privsep_proc *);
int	 control_init(struct privsep *, struct control_sock *);
int	 control_listen(struct control_sock *);

/* config.c */
struct iked_policy *
	 config_new_policy(struct iked *);
void	 config_free_kex(struct iked_kex *);
void	 config_free_fragments(struct iked_frag *);
void	 config_free_sa(struct iked *, struct iked_sa *);
struct iked_sa *
	 config_new_sa(struct iked *, int);
struct iked_user *
	 config_new_user(struct iked *, struct iked_user *);
uint64_t
	 config_getspi(void);
struct iked_transform *
	 config_findtransform(struct iked_proposals *, uint8_t, unsigned int);
struct iked_transform *
	 config_findtransform_ext(struct iked_proposals *, uint8_t,int, unsigned int);
void	 config_free_policy(struct iked *, struct iked_policy *);
struct iked_proposal *
	 config_add_proposal(struct iked_proposals *, unsigned int,
	    unsigned int);
void	 config_free_proposal(struct iked_proposals *, struct iked_proposal *);
void	 config_free_proposals(struct iked_proposals *, unsigned int);
void	 config_free_flows(struct iked *, struct iked_flows *);
void	 config_free_childsas(struct iked *, struct iked_childsas *,
	    struct iked_spi *, struct iked_spi *);
int	 config_add_transform(struct iked_proposal *,
	    unsigned int, unsigned int, unsigned int, unsigned int);
int	 config_setcoupled(struct iked *, unsigned int);
int	 config_getcoupled(struct iked *, unsigned int);
int	 config_setmode(struct iked *, unsigned int);
int	 config_getmode(struct iked *, unsigned int);
int	 config_setreset(struct iked *, unsigned int, enum privsep_procid);
int	 config_getreset(struct iked *, struct imsg *);
int	 config_doreset(struct iked *, unsigned int);
int	 config_setpolicy(struct iked *, struct iked_policy *,
	    enum privsep_procid);
int	 config_getpolicy(struct iked *, struct imsg *);
int	 config_setflow(struct iked *, struct iked_policy *,
	    enum privsep_procid);
int	 config_getflow(struct iked *, struct imsg *);
int	 config_setsocket(struct iked *, struct sockaddr_storage *, in_port_t,
	    enum privsep_procid);
int	 config_getsocket(struct iked *env, struct imsg *,
	    void (*cb)(int, short, void *));
void	 config_enablesocket(struct iked *env);
int	 config_setpfkey(struct iked *);
int	 config_getpfkey(struct iked *, struct imsg *);
int	 config_setuser(struct iked *, struct iked_user *, enum privsep_procid);
int	 config_getuser(struct iked *, struct imsg *);
int	 config_setcompile(struct iked *, enum privsep_procid);
int	 config_getcompile(struct iked *);
int	 config_setocsp(struct iked *);
int	 config_getocsp(struct iked *, struct imsg *);
int	 config_setkeys(struct iked *);
int	 config_getkey(struct iked *, struct imsg *);
int	 config_setstatic(struct iked *);
int	 config_getstatic(struct iked *, struct imsg *);
int	 config_setradauth(struct iked *);
int	 config_getradauth(struct iked *, struct imsg *);
int	 config_setradacct(struct iked *);
int	 config_getradacct(struct iked *, struct imsg *);
int	 config_setradserver(struct iked *, struct sockaddr *, socklen_t,
	    char *, int);
int	 config_getradserver(struct iked *, struct imsg *);
int	 config_setradcfgmap(struct iked *, int, uint32_t, uint8_t);
int	 config_getradcfgmap(struct iked *, struct imsg *);
int	 config_setraddae(struct iked *, struct sockaddr *, socklen_t);
int	 config_getraddae(struct iked *, struct imsg *);
int	 config_setradclient(struct iked *, struct sockaddr *, socklen_t,
	    char *);
int	 config_getradclient(struct iked *, struct imsg *);

/* policy.c */
void	 policy_init(struct iked *);
int	 policy_lookup(struct iked *, struct iked_message *,
	    struct iked_proposals *, struct iked_flows *, int);
int	 policy_lookup_sa(struct iked *, struct iked_sa *);
struct iked_policy *
	 policy_test(struct iked *, struct iked_policy *);
int	 policy_generate_ts(struct iked_policy *);
void	 policy_calc_skip_steps(struct iked_policies *);
void	 policy_ref(struct iked *, struct iked_policy *);
void	 policy_unref(struct iked *, struct iked_policy *);
void	 sa_state(struct iked *, struct iked_sa *, int);
void	 sa_stateflags(struct iked_sa *, unsigned int);
int	 sa_stateok(const struct iked_sa *, int);
struct iked_sa *
	 sa_new(struct iked *, uint64_t, uint64_t, unsigned int,
	    struct iked_policy *);
void	 sa_free(struct iked *, struct iked_sa *);
void	 sa_free_flows(struct iked *, struct iked_saflows *);
int	 sa_configure_iface(struct iked *, struct iked_sa *, int);
int	 sa_address(struct iked_sa *, struct iked_addr *, struct sockaddr *);
void	 childsa_free(struct iked_childsa *);
struct iked_childsa *
	 childsa_lookup(struct iked_sa *, uint64_t, uint8_t);
void	 flow_free(struct iked_flow *);
int	 flow_equal(struct iked_flow *, struct iked_flow *);
struct iked_sa *
	 sa_lookup(struct iked *, uint64_t, uint64_t, unsigned int);
struct iked_user *
	 user_lookup(struct iked *, const char *);
struct iked_sa *
	 sa_dstid_lookup(struct iked *, struct iked_sa *);
struct iked_sa *
	 sa_dstid_insert(struct iked *, struct iked_sa *);
void	 sa_dstid_remove(struct iked *, struct iked_sa *);
int	 proposals_negotiate(struct iked_proposals *, struct iked_proposals *,
	    struct iked_proposals *, int, int);
RB_PROTOTYPE(iked_sas, iked_sa, sa_entry, sa_cmp);
RB_PROTOTYPE(iked_dstid_sas, iked_sa, sa_dstid_entry, sa_dstid_cmp);
RB_PROTOTYPE(iked_addrpool, iked_sa, sa_addrpool_entry, sa_addrpool_cmp);
RB_PROTOTYPE(iked_addrpool6, iked_sa, sa_addrpool6_entry, sa_addrpool6_cmp);
RB_PROTOTYPE(iked_users, iked_user, user_entry, user_cmp);
RB_PROTOTYPE(iked_activesas, iked_childsa, csa_node, childsa_cmp);
RB_PROTOTYPE(iked_flows, iked_flow, flow_node, flow_cmp);

/* crypto.c */
struct iked_hash *
	 hash_new(uint8_t, uint16_t);
struct ibuf *
	 hash_setkey(struct iked_hash *, void *, size_t);
void	 hash_free(struct iked_hash *);
void	 hash_init(struct iked_hash *);
void	 hash_update(struct iked_hash *, void *, size_t);
void	 hash_final(struct iked_hash *, void *, size_t *);
size_t	 hash_keylength(struct iked_hash *);
size_t	 hash_length(struct iked_hash *);

struct iked_cipher *
	 cipher_new(uint8_t, uint16_t, uint16_t);
struct ibuf *
	 cipher_setkey(struct iked_cipher *, const void *, size_t);
struct ibuf *
	 cipher_setiv(struct iked_cipher *, const void *, size_t);
int	 cipher_settag(struct iked_cipher *, uint8_t *, size_t);
int	 cipher_gettag(struct iked_cipher *, uint8_t *, size_t);
void	 cipher_free(struct iked_cipher *);
int	 cipher_init(struct iked_cipher *, int);
int	 cipher_init_encrypt(struct iked_cipher *);
int	 cipher_init_decrypt(struct iked_cipher *);
void	 cipher_aad(struct iked_cipher *, const void *, size_t, size_t *);
int	 cipher_update(struct iked_cipher *, const void *, size_t, void *, size_t *);
int	 cipher_final(struct iked_cipher *);
size_t	 cipher_length(struct iked_cipher *);
size_t	 cipher_keylength(struct iked_cipher *);
size_t	 cipher_ivlength(struct iked_cipher *);
size_t	 cipher_outlength(struct iked_cipher *, size_t);

struct iked_dsa *
	 dsa_new(uint8_t, struct iked_hash *, int);
struct iked_dsa *
	 dsa_sign_new(uint8_t, struct iked_hash *);
struct iked_dsa *
	 dsa_verify_new(uint8_t, struct iked_hash *);
struct ibuf *
	 dsa_setkey(struct iked_dsa *, void *, size_t, uint8_t);
void	 dsa_free(struct iked_dsa *);
int	 dsa_init(struct iked_dsa *, const void *, size_t);
size_t	 dsa_prefix(struct iked_dsa *);
size_t	 dsa_length(struct iked_dsa *);
int	 dsa_update(struct iked_dsa *, const void *, size_t);
ssize_t	 dsa_sign_final(struct iked_dsa *, void *, size_t);
ssize_t	 dsa_verify_final(struct iked_dsa *, void *, size_t);

/* vroute.c */
void vroute_init(struct iked *);
int vroute_setaddr(struct iked *, int, struct sockaddr *, int, unsigned int);
void vroute_cleanup(struct iked *);
int vroute_getaddr(struct iked *, struct imsg *);
int vroute_setdns(struct iked *, int, struct sockaddr *, unsigned int);
int vroute_getdns(struct iked *, struct imsg *);
int vroute_setaddroute(struct iked *, uint8_t, struct sockaddr *,
    uint8_t, struct sockaddr *);
int vroute_setcloneroute(struct iked *, uint8_t, struct sockaddr *,
    uint8_t, struct sockaddr *);
int vroute_setdelroute(struct iked *, uint8_t, struct sockaddr *,
    uint8_t, struct sockaddr *);
int vroute_getroute(struct iked *, struct imsg *);
int vroute_getcloneroute(struct iked *, struct imsg *);

/* ikev2.c */
void	 ikev2(struct privsep *, struct privsep_proc *);
void	 ikev2_recv(struct iked *, struct iked_message *);
void	 ikev2_init_ike_sa(struct iked *, void *);
int	 ikev2_policy2id(struct iked_static_id *, struct iked_id *, int);
int	 ikev2_childsa_enable(struct iked *, struct iked_sa *);
int	 ikev2_childsa_delete(struct iked *, struct iked_sa *,
	    uint8_t, uint64_t, uint64_t *, int);
void	 ikev2_ikesa_recv_delete(struct iked *, struct iked_sa *);
void	 ikev2_ike_sa_timeout(struct iked *env, void *);
void	 ikev2_ike_sa_setreason(struct iked_sa *, char *);
void	 ikev2_reset_alive_timer(struct iked *);
int	 ikev2_ike_sa_delete(struct iked *, struct iked_sa *);

struct ibuf *
	 ikev2_prfplus(struct iked_hash *, struct ibuf *, struct ibuf *,
	    size_t);
ssize_t	 ikev2_psk(struct iked_sa *, uint8_t *, size_t, uint8_t **);
ssize_t	 ikev2_nat_detection(struct iked *, struct iked_message *,
	    void *, size_t, unsigned int, int);
void	 ikev2_enable_natt(struct iked *, struct iked_sa *,
	    struct iked_message *, int);
int	 ikev2_send_informational(struct iked *, struct iked_message *);
int	 ikev2_send_ike_e(struct iked *, struct iked_sa *, struct ibuf *,
	    uint8_t, uint8_t, int);
struct ike_header *
	 ikev2_add_header(struct ibuf *, struct iked_sa *,
	    uint32_t, uint8_t, uint8_t, uint8_t);
int	 ikev2_set_header(struct ike_header *, size_t);
struct ikev2_payload *
	 ikev2_add_payload(struct ibuf *);
int	 ikev2_next_payload(struct ikev2_payload *, size_t,
	    uint8_t);
int	 ikev2_child_sa_acquire(struct iked *, struct iked_flow *);
int	 ikev2_child_sa_drop(struct iked *, struct iked_spi *);
int	 ikev2_child_sa_rekey(struct iked *, struct iked_spi *);
void	 ikev2_disable_rekeying(struct iked *, struct iked_sa *);
int	 ikev2_print_id(struct iked_id *, char *, size_t);
int	 ikev2_print_static_id(struct iked_static_id *, char *, size_t);

const char	*ikev2_ikesa_info(uint64_t, const char *msg);
#define SPI_IH(hdr)      ikev2_ikesa_info(betoh64((hdr)->ike_ispi), NULL)
#define SPI_SH(sh, f)    ikev2_ikesa_info((sh)->sh_ispi, (f))
#define SPI_SA(sa, f)    SPI_SH(&(sa)->sa_hdr, (f))

/* ikev2_msg.c */
void	 ikev2_msg_cb(int, short, void *);
struct ibuf *
	 ikev2_msg_init(struct iked *, struct iked_message *,
	    struct sockaddr_storage *, socklen_t,
	    struct sockaddr_storage *, socklen_t, int);
struct iked_message *
	 ikev2_msg_copy(struct iked *, struct iked_message *);
void	 ikev2_msg_cleanup(struct iked *, struct iked_message *);
uint32_t
	 ikev2_msg_id(struct iked *, struct iked_sa *);
struct ibuf
	*ikev2_msg_auth(struct iked *, struct iked_sa *, int);
int	 ikev2_msg_authsign(struct iked *, struct iked_sa *,
	    struct iked_auth *, struct ibuf *);
int	 ikev2_msg_authverify(struct iked *, struct iked_sa *,
	    struct iked_auth *, uint8_t *, size_t, struct ibuf *);
int	 ikev2_msg_valid_ike_sa(struct iked *, struct ike_header *,
	    struct iked_message *);
int	 ikev2_msg_send(struct iked *, struct iked_message *);
int	 ikev2_msg_send_encrypt(struct iked *, struct iked_sa *,
	    struct ibuf **, uint8_t, uint8_t, int);
struct ibuf
	*ikev2_msg_encrypt(struct iked *, struct iked_sa *, struct ibuf *,
	    struct ibuf *);
struct ibuf *
	 ikev2_msg_decrypt(struct iked *, struct iked_sa *,
	    struct ibuf *, struct ibuf *);
int	 ikev2_msg_integr(struct iked *, struct iked_sa *, struct ibuf *);
int	 ikev2_msg_frompeer(struct iked_message *);
struct iked_socket *
	 ikev2_msg_getsocket(struct iked *, int, int);
int	 ikev2_msg_enqueue(struct iked *, struct iked_msgqueue *,
	    struct iked_message *, int);
int	 ikev2_msg_retransmit_response(struct iked *, struct iked_sa *,
	    struct iked_message *, struct ike_header *);
void	 ikev2_msg_prevail(struct iked *, struct iked_msgqueue *,
	    struct iked_message *);
void	 ikev2_msg_dispose(struct iked *, struct iked_msgqueue *,
	    struct iked_msg_retransmit *);
void	 ikev2_msg_flushqueue(struct iked *, struct iked_msgqueue *);
struct iked_msg_retransmit *
	 ikev2_msg_lookup(struct iked *, struct iked_msgqueue *,
	    struct iked_message *, uint8_t);

/* ikev2_pld.c */
int	 ikev2_pld_parse(struct iked *, struct ike_header *,
	    struct iked_message *, size_t);
int	 ikev2_pld_parse_quick(struct iked *, struct ike_header *,
	    struct iked_message *, size_t);

/* eap.c */
int	 eap_parse(struct iked *, const struct iked_sa *, struct iked_message*,
	    void *, int);
int	 eap_success(struct iked *, struct iked_sa *, int);
int	 eap_identity_request(struct iked *, struct iked_sa *);
int	 eap_mschap_challenge(struct iked *, struct iked_sa *, int, int,
	    uint8_t *, size_t);
int	 eap_mschap_success(struct iked *, struct iked_sa *, int);
int	 eap_challenge_request(struct iked *, struct iked_sa *, int);

/* radius.c */
int	 iked_radius_request(struct iked *, struct iked_sa *,
	    struct iked_message *);
void	 iked_radius_request_free(struct iked *, struct iked_radserver_req *);
void	 iked_radius_on_event(int, short, void *);
void	 iked_radius_acct_on(struct iked *);
void	 iked_radius_acct_off(struct iked *);
void	 iked_radius_acct_start(struct iked *, struct iked_sa *);
void	 iked_radius_acct_stop(struct iked *, struct iked_sa *);
void	 iked_radius_dae_on_event(int, short, void *);

/* pfkey.c */
int	 pfkey_couple(struct iked *, struct iked_sas *, int);
int	 pfkey_flow_add(struct iked *, struct iked_flow *);
int	 pfkey_flow_delete(struct iked *, struct iked_flow *);
int	 pfkey_sa_init(struct iked *, struct iked_childsa *, uint32_t *);
int	 pfkey_sa_add(struct iked *, struct iked_childsa *, struct iked_childsa *);
int	 pfkey_sa_update_addresses(struct iked *, struct iked_childsa *);
int	 pfkey_sa_delete(struct iked *, struct iked_childsa *);
int	 pfkey_sa_last_used(struct iked *, struct iked_childsa *, uint64_t *);
int	 pfkey_flush(struct iked *);
int	 pfkey_socket(struct iked *);
void	 pfkey_init(struct iked *, int fd);

/* ca.c */
void	 caproc(struct privsep *, struct privsep_proc *);
int	 ca_setreq(struct iked *, struct iked_sa *, struct iked_static_id *,
	    uint8_t, uint8_t, uint8_t *, size_t, enum privsep_procid);
int	 ca_setcert(struct iked *, struct iked_sahdr *, struct iked_id *,
	    uint8_t, uint8_t *, size_t, enum privsep_procid);
int	 ca_setauth(struct iked *, struct iked_sa *,
	    struct ibuf *, enum privsep_procid);
void	 ca_getkey(struct privsep *, struct iked_id *, enum imsg_type);
int	 ca_certbundle_add(struct ibuf *, struct iked_id *);
int	 ca_privkey_serialize(EVP_PKEY *, struct iked_id *);
int	 ca_pubkey_serialize(EVP_PKEY *, struct iked_id *);
void	 ca_sslerror(const char *);
char	*ca_asn1_name(uint8_t *, size_t);
void	*ca_x509_name_parse(char *);
void	 ca_cert_info(const char *, X509 *);

/* timer.c */
void	 timer_set(struct iked *, struct iked_timer *,
	    void (*)(struct iked *, void *), void *);
void	 timer_add(struct iked *, struct iked_timer *, int);
void	 timer_del(struct iked *, struct iked_timer *);

/* proc.c */
void	 proc_init(struct privsep *, struct privsep_proc *, unsigned int, int,
	    int, char **, enum privsep_procid);
void	 proc_kill(struct privsep *);
void	 proc_connect(struct privsep *, void (*)(struct privsep *));
void	 proc_dispatch(int, short event, void *);
void	 proc_run(struct privsep *, struct privsep_proc *,
	    struct privsep_proc *, unsigned int,
	    void (*)(struct privsep *, struct privsep_proc *, void *), void *);
void	 imsg_event_add(struct imsgev *);
int	 imsg_compose_event(struct imsgev *, uint16_t, uint32_t,
	    pid_t, int, void *, uint16_t);
int	 imsg_composev_event(struct imsgev *, uint16_t, uint32_t,
	    pid_t, int, const struct iovec *, int);
int	 proc_compose_imsg(struct privsep *, enum privsep_procid, int,
	    uint16_t, uint32_t, int, void *, uint16_t);
int	 proc_compose(struct privsep *, enum privsep_procid,
	    uint16_t, void *, uint16_t);
int	 proc_composev_imsg(struct privsep *, enum privsep_procid, int,
	    uint16_t, uint32_t, int, const struct iovec *, int);
int	 proc_composev(struct privsep *, enum privsep_procid,
	    uint16_t, const struct iovec *, int);
int	 proc_forward_imsg(struct privsep *, struct imsg *,
	    enum privsep_procid, int);
struct imsgbuf *
	 proc_ibuf(struct privsep *, enum privsep_procid, int);
struct imsgev *
	 proc_iev(struct privsep *, enum privsep_procid, int);
enum privsep_procid
	 proc_getid(struct privsep_proc *, unsigned int, const char *);
int	 proc_flush_imsg(struct privsep *, enum privsep_procid, int);

/* util.c */
int	 socket_af(struct sockaddr *, in_port_t);
in_port_t
	 socket_getport(struct sockaddr *);
int	 socket_setport(struct sockaddr *, in_port_t);
int	 socket_getaddr(int, struct sockaddr_storage *);
int	 socket_bypass(int, struct sockaddr *);
int	 udp_bind(struct sockaddr *, in_port_t);
ssize_t	 sendtofrom(int, void *, size_t, int, struct sockaddr *,
	    socklen_t, struct sockaddr *, socklen_t);
ssize_t	 recvfromto(int, void *, size_t, int, struct sockaddr *,
	    socklen_t *, struct sockaddr *, socklen_t *);
const char *
	 print_spi(uint64_t, int);
const char *
	 print_map(unsigned int, struct iked_constmap *);
void	 lc_idtype(char *);
void	 print_hex(const uint8_t *, off_t, size_t);
void	 print_hexval(const uint8_t *, off_t, size_t);
void	 print_hexbuf(struct ibuf *);
const char *
	 print_bits(unsigned short, unsigned char *);
int	 sockaddr_cmp(struct sockaddr *, struct sockaddr *, int);
uint8_t mask2prefixlen(struct sockaddr *);
uint8_t mask2prefixlen6(struct sockaddr *);
struct in6_addr *
	 prefixlen2mask6(uint8_t, uint32_t *);
uint32_t
	 prefixlen2mask(uint8_t);
const char *
	 print_addr(void *);
char	*get_string(uint8_t *, size_t);
const char *
	 print_proto(uint8_t);
int	 expand_string(char *, size_t, const char *, const char *);
uint8_t *string2unicode(const char *, size_t *);
void	 print_debug(const char *, ...)
	    __attribute__((format(printf, 1, 2)));
void	 print_verbose(const char *, ...)
	    __attribute__((format(printf, 1, 2)));

/* imsg_util.c */
struct ibuf *
	 ibuf_new(const void *, size_t);
struct ibuf *
	 ibuf_static(void);
size_t	 ibuf_length(struct ibuf *);
int	 ibuf_setsize(struct ibuf *, size_t);
struct ibuf *
	 ibuf_getdata(struct ibuf *, size_t);
struct ibuf *
	 ibuf_dup(struct ibuf *);
struct ibuf *
	 ibuf_random(size_t);

/* log.c */
void	log_init(int, int);
void	log_procinit(const char *);
void	log_setverbose(int);
int	log_getverbose(void);
void	log_warn(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_warnx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_info(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	log_debug(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
void	logit(int, const char *, ...)
	    __attribute__((__format__ (printf, 2, 3)));
void	vlog(int, const char *, va_list)
	    __attribute__((__format__ (printf, 2, 0)));
__dead void fatal(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));
__dead void fatalx(const char *, ...)
	    __attribute__((__format__ (printf, 1, 2)));

/* ocsp.c */
int	 ocsp_connect(struct iked *, struct imsg *);
int	 ocsp_receive_fd(struct iked *, struct imsg *);
int	 ocsp_validate_cert(struct iked *, void *, size_t, struct iked_sahdr,
    uint8_t, X509 *);

/* parse.y */
int	 parse_config(const char *, struct iked *);
int	 cmdline_symset(char *);
extern const struct ipsec_xf authxfs[];
extern const struct ipsec_xf prfxfs[];
extern const struct ipsec_xf *encxfs;
extern const struct ipsec_xf ikeencxfs[];
extern const struct ipsec_xf ipsecencxfs[];
extern const struct ipsec_xf groupxfs[];
extern const struct ipsec_xf esnxfs[];
extern const struct ipsec_xf methodxfs[];
extern const struct ipsec_xf saxfs[];
extern const struct ipsec_xf cpxfs[];
size_t	 keylength_xf(unsigned int, unsigned int, unsigned int);
size_t	 noncelength_xf(unsigned int, unsigned int);
int	 encxf_noauth(unsigned int);

/* print.c */
void	 print_user(struct iked_user *);
void	 print_policy(struct iked_policy *);
const char *print_xf(unsigned int, unsigned int, const struct ipsec_xf *);

#endif /* IKED_H */
