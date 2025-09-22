/*	$OpenBSD: eap2mschap_local.h,v 1.3 2024/09/15 05:49:05 jsg Exp $	*/

/*
 * Copyright (c) 2024 Internet Initiative Japan Inc.
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

#define	EAP_CODE_REQUEST	1
#define	EAP_CODE_RESPONSE	2
#define	EAP_CODE_SUCCESS	3
#define	EAP_CODE_FAILURE	4

#define	EAP_TYPE_IDENTITY	1
#define	EAP_TYPE_NOTIFICATION	2
#define	EAP_TYPE_NAK		3
#define	EAP_TYPE_MSCHAPV2	0x1a	/* [MS-CHAP] MS-EAP-Authentication */

#define CHAP_CHALLENGE		1
#define CHAP_RESPONSE		2
#define CHAP_SUCCESS		3
#define CHAP_FAILURE		4

/* From [MS-CHAP] */
enum eap_chap_status {
	EAP_CHAP_NONE,
	EAP_CHAP_CHALLENGE_SENT,
	EAP_CHAP_SUCCESS_REQUEST_SENT,
	EAP_CHAP_FAILURE_REQUEST_SENT,
	EAP_CHAP_CHANGE_PASSWORD_SENT,
	EAP_CHAP_FAILED,
	EAP_CHAP_SUCCESS
};

struct eap {
	uint8_t		code;
	uint8_t		id;
	uint16_t	length;
	uint8_t		value[0];
} __packed;

struct chap {
	uint8_t		code;
	uint8_t		id;
	uint16_t	length;
	int8_t		value[0];
} __packed;

struct eap_chap {
	struct eap	eap;
	uint8_t		eap_type;
	struct chap	chap;
};

struct eap_mschap_challenge {
	struct eap	eap;
	uint8_t		eap_type;
	struct chap	chap;
	uint8_t		challsiz;
	uint8_t		chall[16];
	char		chap_name[0];
} __packed;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
static_assert(sizeof(struct eap_mschap_challenge) == 26, "");
static_assert(offsetof(struct eap_mschap_challenge, chap) == 5, "");
static_assert(offsetof(struct eap_mschap_challenge, chall) == 10, "");
#endif

struct eap_mschap_response {
	struct eap	eap;
	uint8_t		eap_type;
	struct chap	chap;
	uint8_t		challsiz;
	uint8_t		peerchall[16];
	uint8_t		reserved[8];
	uint8_t		ntresponse[24];
	uint8_t		flags;
	uint8_t		chap_name[0];
} __packed;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
static_assert(sizeof(struct eap_mschap_response) == 59, "");
static_assert(offsetof(struct eap_mschap_response, chap) == 5, "");
static_assert(offsetof(struct eap_mschap_response, peerchall) == 10, "");
#endif

struct radius_ms_chap2_response {
	uint8_t		ident;
	uint8_t		flags;
	uint8_t		peerchall[16];
	uint8_t		reserved[8];
	uint8_t		ntresponse[24];
} __packed;


struct eap2mschap;

struct access_req {
	struct eap2mschap	*eap2mschap;
	char			*username;
	u_int			 q_id;
	TAILQ_ENTRY(access_req)	 next;
	RB_ENTRY(access_req)	 tree;
	/* for EAP */
	enum eap_chap_status	 eap_chap_status;
	char			 state[16];
	unsigned char		 chap_id;
	unsigned char		 eap_id;
	time_t			 eap_time;
	char			 chall[16];
	RADIUS_PACKET		*pkt;

};
TAILQ_HEAD(access_reqq, access_req);
RB_HEAD(access_reqt, access_req);

#define CHAP_NAME_MAX			40

struct eap2mschap {
	struct module_base	*base;
	char			*secret;
	char			 chap_name[CHAP_NAME_MAX + 1];
	struct access_reqq	 reqq;
	struct access_reqt	 eapt;
	struct event		 ev_eapt;
};

/* Attributes copied from CHAP Access-Accept to EAP Access-Access-Accept */
struct preserve_attrs {
	uint8_t		type;
	uint32_t	vendor;
} preserve_attrs[] = {
	{ RADIUS_TYPE_FRAMED_PROTOCOL,		0},
	{ RADIUS_TYPE_FRAMED_IP_ADDRESS,	0},
	{ RADIUS_TYPE_FRAMED_IP_NETMASK,	0},
	{ RADIUS_TYPE_FRAMED_IPV6_ADDRESS,	0},
	{ RADIUS_TYPE_DNS_SERVER_IPV6_ADDRESS,	0},
	{ RADIUS_TYPE_FRAMED_ROUTING,		0},
	{ RADIUS_VTYPE_MS_PRIMARY_DNS_SERVER,	RADIUS_VENDOR_MICROSOFT },
	{ RADIUS_VTYPE_MS_SECONDARY_DNS_SERVER,	RADIUS_VENDOR_MICROSOFT },
	{ RADIUS_VTYPE_MS_PRIMARY_NBNS_SERVER,	RADIUS_VENDOR_MICROSOFT },
	{ RADIUS_VTYPE_MS_SECONDARY_NBNS_SERVER,RADIUS_VENDOR_MICROSOFT },
	{ RADIUS_VTYPE_MPPE_SEND_KEY,		RADIUS_VENDOR_MICROSOFT },
	{ RADIUS_VTYPE_MPPE_RECV_KEY, 		RADIUS_VENDOR_MICROSOFT }
};

#ifndef EAP2MSCHAP_DEBUG
#define	EAP2MSCHAP_DBG(...)
#define	EAP2MSCHAP_ASSERT(_cond)
#else
#define	EAP2MSCHAP_DBG(...)	logit(LOG_DEBUG, __VA_ARGS__)
#define	EAP2MSCHAP_ASSERT(_cond)				\
	do {							\
		if (!(_cond)) {					\
			log_warnx(				\
			    "ASSERT(%s) failed in %s() at %s:%d",\
			    #_cond, __func__, __FILE__, __LINE__);\
			abort();				\
		}						\
	} while (0/* CONSTCOND */);
#endif
#ifndef nitems
#define nitems(_x)    (sizeof((_x)) / sizeof((_x)[0]))
#endif

static void	 eap2mschap_init(struct eap2mschap *);
static void	 eap2mschap_start(void *);
static void	 eap2mschap_config_set(void *, const char *, int,
		    char * const *);
static void	 eap2mschap_stop(void *);
static void	 eap2mschap_access_request(void *, u_int, const u_char *,
		    size_t);
static void	 eap2mschap_next_response(void *, u_int, const u_char *,
		    size_t);

static void	 eap2mschap_on_eapt (int, short, void *);
static void	 eap2mschap_reset_eaptimer (struct eap2mschap *);

static struct access_req
		*access_request_new(struct eap2mschap *, u_int);
static void	 access_request_free(struct access_req *);
static int	 access_request_compar(struct access_req *,
		    struct access_req *);


static struct access_req
		*eap_recv(struct eap2mschap *, u_int, RADIUS_PACKET *);
static struct access_req
		*eap_recv_mschap(struct eap2mschap *, struct access_req *,
		    RADIUS_PACKET *, struct eap_chap *);
static void	 eap_resp_mschap(struct eap2mschap *, struct access_req *,
		    RADIUS_PACKET *);
static void	 eap_send_reject(struct access_req *, RADIUS_PACKET *, u_int);
static const char
		*eap_chap_status_string(enum eap_chap_status);
static const char
		*hex_string(const char *, size_t, char *, size_t);
static time_t	 monotime(void);

RB_PROTOTYPE_STATIC(access_reqt, access_req, tree, access_request_compar);
