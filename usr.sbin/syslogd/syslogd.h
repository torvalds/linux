/*	$OpenBSD: syslogd.h,v 1.37 2023/10/12 22:36:54 bluhm Exp $ */

/*
 * Copyright (c) 2014-2017 Alexander Bluhm <bluhm@genua.de>
 * Copyright (c) 2003 Anil Madhavapeddy <anil@recoil.org>
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
#include <sys/socket.h>
#include <sys/uio.h>

#include <stdarg.h>

extern int	ZuluTime;

/* Privilege separation */
void  priv_init(int, int, int, char **);
__dead void priv_exec(char *, int, int, int, char **);
int   priv_open_tty(const char *);
int   priv_open_log(const char *);
FILE *priv_open_utmp(void);
FILE *priv_open_config(void);
void  priv_config_parse_done(void);
int   priv_config_modified(void);
int   priv_getaddrinfo(const char *, const char *, const char *,
    struct sockaddr *, size_t);
int   priv_getnameinfo(struct sockaddr *, socklen_t, char *, size_t);

#define IOVCNT		7

/* Terminal message */
#define TTYMSGTIME	1		/* timeout used by ttymsg */
#define TTYMAXDELAY	256		/* max events in ttymsg */
void ttymsg(char *, struct iovec *);

/* File descriptor send/recv */
void send_fd(int, int);
int  receive_fd(int);

#define ERRBUFSIZE	256
void vlogmsg(int pri, const char *, const char *, va_list);
__dead void die(int);
extern int Debug;

struct ringbuf {
	char *buf;
	size_t len, start, end;
};

struct ringbuf *ringbuf_init(size_t);
void		ringbuf_free(struct ringbuf *);
void		ringbuf_clear(struct ringbuf *);
size_t		ringbuf_used(struct ringbuf *);
int		ringbuf_append_line(struct ringbuf *, char *);
ssize_t		ringbuf_to_string(char *, size_t, struct ringbuf *);
