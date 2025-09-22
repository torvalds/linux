/*	$OpenBSD: lpd.h,v 1.2 2018/09/05 17:32:56 eric Exp $	*/

/*
 * Copyright (c) 2017 Eric Faurot <eric@openbsd.org>
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
#include <sys/queue.h>
#include <sys/tree.h>
#include <sys/uio.h>
#include <sys/socket.h>

#include <event.h>
#include <imsg.h>
#include <limits.h>
#include <netdb.h>

#define	PORT_LPR	515

#define	LPD_CONFIG	"/etc/lpd.conf"
#define	LPD_USER	"daemon"
#define	LPD_SOCKET	"/var/run/lpd.sock"

#define	LPR_DEFPRINTER	"lp"
#define	LPR_MAXCMDLEN	BUFSIZ
#define	LPR_MAXFILESIZE	104857600

#define	LOGLEVEL_CONN	2
#define	LOGLEVEL_IMSG	3
#define	LOGLEVEL_IO	4

enum {
	IMSG_NONE,

	IMSG_SOCK_ENGINE,
	IMSG_SOCK_FRONTEND,

	IMSG_CONF_START,
	IMSG_CONF_LISTENER,
	IMSG_CONF_END,

	IMSG_GETADDRINFO,
	IMSG_GETADDRINFO_END,
	IMSG_GETNAMEINFO,

	IMSG_LPR_ALLOWEDHOST,
	IMSG_LPR_DISPLAYQ,
	IMSG_LPR_PRINTJOB,
	IMSG_LPR_RECVJOB,
	IMSG_LPR_RECVJOB_CLEAR,
	IMSG_LPR_RECVJOB_CF,
	IMSG_LPR_RECVJOB_DF,
	IMSG_LPR_RECVJOB_COMMIT,
	IMSG_LPR_RECVJOB_ROLLBACK,
	IMSG_LPR_RMJOB
};

enum {
	PROC_CLIENT,
	PROC_CONTROL,
	PROC_ENGINE,
	PROC_FRONTEND,
	PROC_PRINTER,
	PROC_PRIV
};

enum {
	PROTO_NONE = 0,
	PROTO_LPR
};

struct listener {
	TAILQ_ENTRY(listener)	 entry;
	int			 sock;
	int			 proto;
	struct sockaddr_storage	 ss;
	struct timeval		 timeout;
	struct event		 ev;
	int			 pause;
};

struct lpd_conf {
	TAILQ_HEAD(, listener)	 listeners;
};

struct io;
struct imsgproc;

extern struct lpd_conf *env;
extern struct imsgproc *p_control;
extern struct imsgproc *p_engine;
extern struct imsgproc *p_frontend;
extern struct imsgproc *p_priv;

/* control.c */
void control(int, int);

/* engine.c */
void engine(int, int);

/* frontend.c */
void frontend(int, int);
void frontend_conn_closed(uint32_t);

/* logmsg.c */
const char *log_fmt_proto(int);
const char *log_fmt_imsgtype(int);
const char *log_fmt_proctype(int);
const char *log_fmt_sockaddr(const struct sockaddr *);
void log_imsg(struct imsgproc *, struct imsg *);
void log_io(const char *, struct io *, int);

/* engine_lpr.c */
void lpr_shutdown(void);
void lpr_dispatch_frontend(struct imsgproc *, struct imsg *);
void lpr_printjob(const char *);

/* frontend_lpr.c */
void lpr_init(void);
void lpr_dispatch_engine(struct imsgproc *, struct imsg *);
void lpr_conn(uint32_t, struct listener *, int, const struct sockaddr *);

/* parse.y */
struct lpd_conf *parse_config(const char *, int);
int cmdline_symset(char *);

/* printer.c */
void printer(int, int, const char *);

/* resolver.c */
void resolver_getaddrinfo(const char *, const char *, const struct addrinfo *,
    void(*)(void *, int, struct addrinfo*), void *);
void resolver_getnameinfo(const struct sockaddr *, int,
    void(*)(void *, int, const char *, const char *), void *);
void resolver_dispatch_request(struct imsgproc *, struct imsg *);
void resolver_dispatch_result(struct imsgproc *, struct imsg *);
