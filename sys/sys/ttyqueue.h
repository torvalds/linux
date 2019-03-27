/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
 *
 * Portions of this software were developed under sponsorship from Snow
 * B.V., the Netherlands.
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
 *
 * $FreeBSD$
 */

#ifndef _SYS_TTYQUEUE_H_
#define	_SYS_TTYQUEUE_H_

#ifndef _SYS_TTY_H_
#error "can only be included through <sys/tty.h>"
#endif /* !_SYS_TTY_H_ */

struct tty;
struct ttyinq_block;
struct ttyoutq_block;
struct uio;

/* Data input queue. */
struct ttyinq {
	struct ttyinq_block	*ti_firstblock;
	struct ttyinq_block	*ti_startblock;
	struct ttyinq_block	*ti_reprintblock;
	struct ttyinq_block	*ti_lastblock;
	unsigned int		ti_begin;
	unsigned int		ti_linestart;
	unsigned int		ti_reprint;
	unsigned int		ti_end;
	unsigned int		ti_nblocks;
	unsigned int		ti_quota;
};
#define TTYINQ_DATASIZE 128

/* Data output queue. */
struct ttyoutq {
	struct ttyoutq_block	*to_firstblock;
	struct ttyoutq_block	*to_lastblock;
	unsigned int		to_begin;
	unsigned int		to_end;
	unsigned int		to_nblocks;
	unsigned int		to_quota;
};
#define TTYOUTQ_DATASIZE (256 - sizeof(struct ttyoutq_block *))

#ifdef _KERNEL
/* Input queue handling routines. */
int	ttyinq_setsize(struct ttyinq *ti, struct tty *tp, size_t len);
void	ttyinq_free(struct ttyinq *ti);
int	ttyinq_read_uio(struct ttyinq *ti, struct tty *tp, struct uio *uio,
    size_t readlen, size_t flushlen);
size_t	ttyinq_write(struct ttyinq *ti, const void *buf, size_t len,
    int quote);
int	ttyinq_write_nofrag(struct ttyinq *ti, const void *buf, size_t len,
    int quote);
void	ttyinq_canonicalize(struct ttyinq *ti);
size_t	ttyinq_findchar(struct ttyinq *ti, const char *breakc, size_t maxlen,
    char *lastc);
void	ttyinq_flush(struct ttyinq *ti);
int	ttyinq_peekchar(struct ttyinq *ti, char *c, int *quote);
void	ttyinq_unputchar(struct ttyinq *ti);
void	ttyinq_reprintpos_set(struct ttyinq *ti);
void	ttyinq_reprintpos_reset(struct ttyinq *ti);

static __inline size_t
ttyinq_getsize(struct ttyinq *ti)
{
	return (ti->ti_nblocks * TTYINQ_DATASIZE);
}

static __inline size_t
ttyinq_getallocatedsize(struct ttyinq *ti)
{

	return (ti->ti_quota * TTYINQ_DATASIZE);
}

static __inline size_t
ttyinq_bytesleft(struct ttyinq *ti)
{
	size_t len;

	/* Make sure the usage never exceeds the length. */
	len = ti->ti_nblocks * TTYINQ_DATASIZE;
	MPASS(len >= ti->ti_end);

	return (len - ti->ti_end);
}

static __inline size_t
ttyinq_bytescanonicalized(struct ttyinq *ti)
{
	MPASS(ti->ti_begin <= ti->ti_linestart);

	return (ti->ti_linestart - ti->ti_begin);
}

static __inline size_t
ttyinq_bytesline(struct ttyinq *ti)
{
	MPASS(ti->ti_linestart <= ti->ti_end);

	return (ti->ti_end - ti->ti_linestart);
}

/* Input buffer iteration. */
typedef void ttyinq_line_iterator_t(void *data, char c, int flags);
void	ttyinq_line_iterate_from_linestart(struct ttyinq *ti,
    ttyinq_line_iterator_t *iterator, void *data);
void	ttyinq_line_iterate_from_reprintpos(struct ttyinq *ti,
    ttyinq_line_iterator_t *iterator, void *data);

/* Output queue handling routines. */
void	ttyoutq_flush(struct ttyoutq *to);
int	ttyoutq_setsize(struct ttyoutq *to, struct tty *tp, size_t len);
void	ttyoutq_free(struct ttyoutq *to);
size_t	ttyoutq_read(struct ttyoutq *to, void *buf, size_t len);
int	ttyoutq_read_uio(struct ttyoutq *to, struct tty *tp, struct uio *uio);
size_t	ttyoutq_write(struct ttyoutq *to, const void *buf, size_t len);
int	ttyoutq_write_nofrag(struct ttyoutq *to, const void *buf, size_t len);

static __inline size_t
ttyoutq_getsize(struct ttyoutq *to)
{
	return (to->to_nblocks * TTYOUTQ_DATASIZE);
}

static __inline size_t
ttyoutq_getallocatedsize(struct ttyoutq *to)
{

	return (to->to_quota * TTYOUTQ_DATASIZE);
}

static __inline size_t
ttyoutq_bytesleft(struct ttyoutq *to)
{
	size_t len;

	/* Make sure the usage never exceeds the length. */
	len = to->to_nblocks * TTYOUTQ_DATASIZE;
	MPASS(len >= to->to_end);

	return (len - to->to_end);
}

static __inline size_t
ttyoutq_bytesused(struct ttyoutq *to)
{
	return (to->to_end - to->to_begin);
}
#endif /* _KERNEL */

#endif /* !_SYS_TTYQUEUE_H_ */
