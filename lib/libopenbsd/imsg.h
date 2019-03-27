/*	$OpenBSD: imsg.h,v 1.3 2013/12/26 17:32:33 eric Exp $	*/

/*
 * Copyright (c) 2006, 2007 Pierre-Yves Ritschard <pyr@openbsd.org>
 * Copyright (c) 2006, 2007, 2008 Reyk Floeter <reyk@openbsd.org>
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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
 *
 * $FreeBSD$
 */

#ifndef _IMSG_H_
#define _IMSG_H_

#define IBUF_READ_SIZE		65535
#define IMSG_HEADER_SIZE	sizeof(struct imsg_hdr)
#define MAX_IMSGSIZE		16384

struct ibuf {
	TAILQ_ENTRY(ibuf)	 entry;
	u_char			*buf;
	size_t			 size;
	size_t			 max;
	size_t			 wpos;
	size_t			 rpos;
	int			 fd;
};

struct msgbuf {
	TAILQ_HEAD(, ibuf)	 bufs;
	u_int32_t		 queued;
	int			 fd;
};

struct ibuf_read {
	u_char			 buf[IBUF_READ_SIZE];
	u_char			*rptr;
	size_t			 wpos;
};

struct imsg_fd {
	TAILQ_ENTRY(imsg_fd)	entry;
	int			fd;
};

struct imsgbuf {
	TAILQ_HEAD(, imsg_fd)	 fds;
	struct ibuf_read	 r;
	struct msgbuf		 w;
	int			 fd;
	pid_t			 pid;
};

#define IMSGF_HASFD	1

struct imsg_hdr {
	u_int32_t	 type;
	u_int16_t	 len;
	u_int16_t	 flags;
	u_int32_t	 peerid;
	u_int32_t	 pid;
};

struct imsg {
	struct imsg_hdr	 hdr;
	int		 fd;
	void		*data;
};


/* buffer.c */
struct ibuf	*ibuf_open(size_t);
struct ibuf	*ibuf_dynamic(size_t, size_t);
int		 ibuf_add(struct ibuf *, const void *, size_t);
void		*ibuf_reserve(struct ibuf *, size_t);
void		*ibuf_seek(struct ibuf *, size_t, size_t);
size_t		 ibuf_size(struct ibuf *);
size_t		 ibuf_left(struct ibuf *);
void		 ibuf_close(struct msgbuf *, struct ibuf *);
int		 ibuf_write(struct msgbuf *);
void		 ibuf_free(struct ibuf *);
void		 msgbuf_init(struct msgbuf *);
void		 msgbuf_clear(struct msgbuf *);
int		 msgbuf_write(struct msgbuf *);
void		 msgbuf_drain(struct msgbuf *, size_t);

/* imsg.c */
void	 imsg_init(struct imsgbuf *, int);
ssize_t	 imsg_read(struct imsgbuf *);
ssize_t	 imsg_get(struct imsgbuf *, struct imsg *);
int	 imsg_compose(struct imsgbuf *, u_int32_t, u_int32_t, pid_t,
	    int, const void *, u_int16_t);
int	 imsg_composev(struct imsgbuf *, u_int32_t, u_int32_t,  pid_t,
	    int, const struct iovec *, int);
struct ibuf *imsg_create(struct imsgbuf *, u_int32_t, u_int32_t, pid_t,
	    u_int16_t);
int	 imsg_add(struct ibuf *, const void *, u_int16_t);
void	 imsg_close(struct imsgbuf *, struct ibuf *);
void	 imsg_free(struct imsg *);
int	 imsg_flush(struct imsgbuf *);
void	 imsg_clear(struct imsgbuf *);

#endif
