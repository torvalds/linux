/*	$Id: extern.h,v 1.22 2025/09/16 15:06:02 sthen Exp $ */
/*
 * Copyright (c) 2016 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef EXTERN_H
#define EXTERN_H

#include "parse.h"

#define MAX_SERVERS_DNS 8

#ifndef nitems
#define nitems(_a) (sizeof((_a)) / sizeof((_a)[0]))
#endif

/*
 * Requests to and from acctproc.
 */
enum	acctop {
	ACCT_STOP = 0,
	ACCT_READY,
	ACCT_SIGN,
	ACCT_KID_SIGN,
	ACCT_THUMBPRINT,
	ACCT__MAX
};

/*
 * Requests to and from chngproc.
 */
enum	chngop {
	CHNG_STOP = 0,
	CHNG_SYN,
	CHNG_ACK,
	CHNG__MAX
};

/*
 * Requests to keyproc.
 */
enum	keyop {
	KEY_STOP = 0,
	KEY_READY,
	KEY__MAX
};

/*
 * Requests to certproc.
 */
enum	certop {
	CERT_STOP = 0,
	CERT_REVOKE,
	CERT_UPDATE,
	CERT__MAX
};

/*
 * Requests to fileproc.
 */
enum	fileop {
	FILE_STOP = 0,
	FILE_REMOVE,
	FILE_CREATE,
	FILE__MAX
};

/*
 * Requests to dnsproc.
 */
enum	dnsop {
	DNS_STOP = 0,
	DNS_LOOKUP,
	DNS__MAX
};

enum	revokeop {
	REVOKE_STOP = 0,
	REVOKE_CHECK,
	REVOKE_EXP,
	REVOKE_OK,
	REVOKE__MAX
};

/*
 * Our components.
 * Each one of these is in a separated, isolated process.
 */
enum	comp {
	COMP_NET, /* network-facing (to ACME) */
	COMP_KEY, /* handles domain keys */
	COMP_CERT, /* handles domain certificates */
	COMP_ACCOUNT, /* handles account key */
	COMP_CHALLENGE, /* handles challenges */
	COMP_FILE, /* handles writing certs */
	COMP_DNS, /* handles DNS lookups */
	COMP_REVOKE, /* checks X509 expiration */
	COMP__MAX
};

/*
 * Inter-process communication labels.
 * This is purely for looking at debugging.
 */
enum	comm {
	COMM_REQ,
	COMM_THUMB,
	COMM_CERT,
	COMM_PAY,
	COMM_NONCE,
	COMM_KID,
	COMM_URL,
	COMM_TOK,
	COMM_CHNG_OP,
	COMM_CHNG_ACK,
	COMM_ACCT,
	COMM_ACCT_STAT,
	COMM_CSR,
	COMM_CSR_OP,
	COMM_ISSUER,
	COMM_CHAIN,
	COMM_CHAIN_OP,
	COMM_DNS,
	COMM_DNSQ,
	COMM_DNSA,
	COMM_DNSF,
	COMM_DNSLEN,
	COMM_KEY_STAT,
	COMM_REVOKE_OP,
	COMM_REVOKE_CHECK,
	COMM_REVOKE_RESP,
	COMM__MAX
};

/*
 * This contains the URI and token of an ACME-issued challenge.
 * A challenge consists of a token, which we must present on the
 * (presumably!) local machine to an ACME connection; and a URI, to
 * which we must connect to verify the token.
 */
enum	chngstatus {
	CHNG_INVALID = -1,
	CHNG_PENDING = 0,
	CHNG_PROCESSING = 1,
	CHNG_VALID = 2
};

struct	chng {
	char		*uri; /* uri on ACME server */
	char		*token; /* token we must offer */
	char		*error; /* "detail" field in case of error */
	size_t		 retry; /* how many times have we tried */
	enum chngstatus	 status; /* challenge accepted? */
};

enum	orderstatus {
	ORDER_INVALID = -1,
	ORDER_PENDING = 0,
	ORDER_READY = 1,
	ORDER_PROCESSING = 2,
	ORDER_VALID = 3
};

struct	order {
	char			*uri;		/* uri of the order request */
	char			*finalize;	/* finalize uri */
	char			*certificate;	/* uri for issued certificate */
	enum orderstatus	 status;	/* status of order */
	char			**auths;	/* authorization uris */
	size_t			 authsz;
};

/*
 * This consists of the services offered by the CA.
 * They must all be filled in.
 */
struct	capaths {
	char		*newaccount;	/* new acme account */
	char		*newnonce;	/* new nonce */
	char		*neworder;	/* order new certificate */
	char		*revokecert; /* revoke certificate */
};

struct	jsmnn;

__BEGIN_DECLS

/*
 * Start with our components.
 * These are all isolated and talk to each other using sockets.
 */
int		 acctproc(int, const char *, enum keytype);
int		 certproc(int, int);
int		 chngproc(int, const char *);
int		 dnsproc(int);
int		 revokeproc(int, const char *, int, int, const char *const *,
			size_t);
int		 fileproc(int, const char *, const char *, const char *,
			const char *);
int		 keyproc(int, const char *, const char **, size_t,
			enum keytype);
int		 netproc(int, int, int, int, int, int, int,
			struct authority_c *, const char *const *,
			size_t, const char *);

/*
 * Debugging functions.
 * These just route to warnx according to the verbosity.
 */
void		 dodbg(const char *, ...)
			__attribute__((format(printf, 1, 2)));
void		 doddbg(const char *, ...)
			__attribute__((format(printf, 1, 2)));

/*
 * Read and write things from the wire.
 * The readers behave differently with respect to EOF.
 */
long		 readop(int, enum comm);
char		*readbuf(int, enum comm, size_t *);
char		*readstr(int, enum comm);
int		 writebuf(int, enum comm, const void *, size_t);
int		 writestr(int, enum comm, const char *);
int		 writeop(int, enum comm, long);

int		 checkexit(pid_t, enum comp);
int		 checkexit_ext(int *, pid_t, enum comp);

/*
 * Base64 and URL encoding.
 * Returns a buffer or NULL on allocation error.
 */
size_t		 base64len(size_t);
char		*base64buf_url(const char *, size_t);

/*
 * JSON parsing routines.
 * Keep this all in on place, though it's only used by one file.
 */
struct jsmnn	*json_parse(const char *, size_t);
void		 json_free(struct jsmnn *);
int		 json_parse_response(struct jsmnn *);
void		 json_free_challenge(struct chng *);
int		 json_parse_challenge(struct jsmnn *, struct chng *);
void		 json_free_order(struct order *);
int		 json_parse_order(struct jsmnn *, struct order *);
int		 json_parse_upd_order(struct jsmnn *, struct order *);
void		 json_free_capaths(struct capaths *);
int		 json_parse_capaths(struct jsmnn *, struct capaths *);
char		*json_getstr(struct jsmnn *, const char *);

char		*json_fmt_newcert(const char *);
char		*json_fmt_chkacc(void);
char		*json_fmt_newacc(const char *);
char		*json_fmt_neworder(const char *const *, size_t, const char *);
char		*json_fmt_protected_rsa(const char *,
			const char *, const char *, const char *);
char		*json_fmt_protected_ec(const char *, const char *, const char *,
			const char *);
char		*json_fmt_protected_kid(const char*, const char *, const char *,
			const char *);
char		*json_fmt_revokecert(const char *);
char		*json_fmt_thumb_rsa(const char *, const char *);
char		*json_fmt_thumb_ec(const char *, const char *);
char		*json_fmt_signed(const char *, const char *, const char *);

/*
 * Should we print debugging messages?
 */
extern int	 verbose;

/*
 * What component is the process within (COMP__MAX for none)?
 */
extern enum comp proccomp;

__END_DECLS

#endif /* ! EXTERN_H */
