#ifndef _RADIUS_MODULE_H
#define _RADIUS_MODULE_H

/*
 * Copyright (c) 2015 YASUOKA Masahiko <yasuoka@ysauoka.net>
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

#include "radiusd.h"

struct module_ctx;
struct imsg;

struct module_handlers {
	/* Should send IMSG_OK or IMSG_NG */
	void (*config_set)(void *ctx, const char *paramname, int paramvalc,
	    char * const * paramvalv);

	void (*start)(void *ctx);

	void (*stop)(void *ctx);

	void (*userpass)(void *ctx, u_int query_id, const char *user,
	    const char *pass);

	void (*access_request)(void *ctx, u_int query_id, const u_char *pkt,
	    size_t pktlen);
	/* User-Password Attribute is encrypted if the module has the secret */

	void (*next_response)(void *ctx, u_int query_id, const u_char *pkt,
	    size_t pktlen);

	void (*request_decoration)(void *ctx, u_int query_id, const u_char *pkt,
	    size_t pktlen);

	void (*response_decoration)(void *ctx, u_int query_id,
	    const u_char *req, size_t reqlen, const u_char *res, size_t reslen);

	void (*accounting_request)(void *ctx, u_int query_id, const u_char *pkt,
	    size_t pktlen);

	void (*dispatch_control)(void *ctx, struct imsg *);
};

#define SYNTAX_ASSERT(_cond, _msg)				\
	do {							\
		if (!(_cond)) {					\
			errmsg = (_msg);			\
			goto syntax_error;			\
		}						\
	} while (0 /* CONSTCOND */)

__BEGIN_DECLS

struct module_base	*module_create(int, void *, struct module_handlers *);
void			 module_start(struct module_base *);
void			 module_stop(struct module_base *);
int			 module_run(struct module_base *);
void			 module_destroy(struct module_base *);
void			 module_load(struct module_base *);
void			 module_drop_privilege(struct module_base *, int);
int			 module_notify_secret(struct module_base *,
			    const char *);
int			 module_send_message(struct module_base *, uint32_t,
			    const char *, ...)
			    __attribute__((__format__ (__printf__, 3, 4)));
int			 module_userpass_ok(struct module_base *, u_int,
			    const char *);
int			 module_userpass_fail(struct module_base *, u_int,
			    const char *);
int			 module_accsreq_answer(struct module_base *, u_int,
			    const u_char *, size_t);
int			 module_accsreq_next(struct module_base *, u_int,
			    const u_char *, size_t);
int			 module_accsreq_aborted(struct module_base *, u_int);
int			 module_reqdeco_done(struct module_base *, u_int,
			    const u_char *, size_t);
int			 module_resdeco_done(struct module_base *, u_int,
			    const u_char *, size_t);
int			 module_imsg_compose(struct module_base *, uint32_t,
			    uint32_t, pid_t, int, const void *, size_t);
int			 module_imsg_composev(struct module_base *, uint32_t,
			    uint32_t, pid_t, int, const struct iovec *, int);

__END_DECLS

#endif
