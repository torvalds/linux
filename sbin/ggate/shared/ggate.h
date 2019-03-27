/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
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

#ifndef _GGATE_H_
#define	_GGATE_H_

#include <sys/endian.h>
#include <stdarg.h>

#define	G_GATE_PORT		3080

#define	G_GATE_RCVBUF		131072
#define	G_GATE_SNDBUF		131072
#define	G_GATE_QUEUE_SIZE	1024
#define	G_GATE_TIMEOUT		0

#define	GGATE_MAGIC		"GEOM_GATE       "
#define	GGATE_VERSION		0

#define	GGATE_FLAG_RDONLY	0x0001
#define	GGATE_FLAG_WRONLY	0x0002
/*
 * If GGATE_FLAG_SEND not GGATE_FLAG_RECV flag is set, this is initial
 * connection.
 * If GGATE_FLAG_SEND flag is set - this is socket to send data.
 * If GGATE_FLAG_RECV flag is set - this is socket to receive data.
 */
#define	GGATE_FLAG_SEND		0x0004
#define	GGATE_FLAG_RECV		0x0008

#define	GGATE_CMD_READ		0
#define	GGATE_CMD_WRITE		1

extern int g_gate_devfd;
extern int g_gate_verbose;

extern int nagle;
extern unsigned rcvbuf, sndbuf;

struct g_gate_version {
	char		gv_magic[16];
	uint16_t	gv_version;
	uint16_t	gv_error;
} __packed;

/* Client's initial packet. */
struct g_gate_cinit {
	char		gc_path[PATH_MAX + 1];
	uint64_t	gc_flags;
	uint16_t	gc_nconn;
	uint32_t	gc_token;
} __packed;

/* Server's initial packet. */
struct g_gate_sinit {
	uint8_t		gs_flags;
	uint64_t	gs_mediasize;
	uint32_t	gs_sectorsize;
	uint16_t	gs_error;
} __packed;

/* Control struct. */
struct g_gate_hdr {
	uint8_t		gh_cmd;		/* command */
	uint64_t	gh_offset;	/* device offset */
	uint32_t	gh_length;	/* size of block */
	uint64_t	gh_seq;		/* request number */
	uint16_t	gh_error;	/* error value (0 if ok) */
} __packed;

void	g_gate_vlog(int priority, const char *message, va_list ap);
void	g_gate_log(int priority, const char *message, ...);
void	g_gate_xvlog(const char *message, va_list ap) __dead2;
void	g_gate_xlog(const char *message, ...) __dead2;
off_t	g_gate_mediasize(int fd);
unsigned g_gate_sectorsize(int fd);
void	g_gate_open_device(void);
void	g_gate_close_device(void);
void	g_gate_ioctl(unsigned long req, void *data);
void	g_gate_destroy(int unit, int force);
void	g_gate_load_module(void);
ssize_t	g_gate_recv(int s, void *buf, size_t len, int flags);
ssize_t	g_gate_send(int s, const void *buf, size_t len, int flags);
void	g_gate_socket_settings(int sfd);
#ifdef LIBGEOM
void	g_gate_list(int unit, int verbose);
#endif
in_addr_t g_gate_str2ip(const char *str);

/*
 * g_gate_swap2h_* - functions swap bytes to host byte order (from big endian).
 * g_gate_swap2n_* - functions swap bytes to network byte order (actually
 *                   to big endian byte order).
 */

static __inline void
g_gate_swap2h_version(struct g_gate_version *ver)
{

	ver->gv_version = be16toh(ver->gv_version);
	ver->gv_error = be16toh(ver->gv_error);
}

static __inline void
g_gate_swap2n_version(struct g_gate_version *ver)
{

	ver->gv_version = htobe16(ver->gv_version);
	ver->gv_error = htobe16(ver->gv_error);
}

static __inline void
g_gate_swap2h_cinit(struct g_gate_cinit *cinit)
{

	cinit->gc_flags = be64toh(cinit->gc_flags);
	cinit->gc_nconn = be16toh(cinit->gc_nconn);
	cinit->gc_token = be32toh(cinit->gc_token);
}

static __inline void
g_gate_swap2n_cinit(struct g_gate_cinit *cinit)
{

	cinit->gc_flags = htobe64(cinit->gc_flags);
	cinit->gc_nconn = htobe16(cinit->gc_nconn);
	cinit->gc_token = htobe32(cinit->gc_token);
}

static __inline void
g_gate_swap2h_sinit(struct g_gate_sinit *sinit)
{

	/* Swap only used fields. */
	sinit->gs_mediasize = be64toh(sinit->gs_mediasize);
	sinit->gs_sectorsize = be32toh(sinit->gs_sectorsize);
	sinit->gs_error = be16toh(sinit->gs_error);
}

static __inline void
g_gate_swap2n_sinit(struct g_gate_sinit *sinit)
{

	/* Swap only used fields. */
	sinit->gs_mediasize = htobe64(sinit->gs_mediasize);
	sinit->gs_sectorsize = htobe32(sinit->gs_sectorsize);
	sinit->gs_error = htobe16(sinit->gs_error);
}

static __inline void
g_gate_swap2h_hdr(struct g_gate_hdr *hdr)
{

	/* Swap only used fields. */
	hdr->gh_offset = be64toh(hdr->gh_offset);
	hdr->gh_length = be32toh(hdr->gh_length);
	hdr->gh_seq = be64toh(hdr->gh_seq);
	hdr->gh_error = be16toh(hdr->gh_error);
}

static __inline void
g_gate_swap2n_hdr(struct g_gate_hdr *hdr)
{

	/* Swap only used fields. */
	hdr->gh_offset = htobe64(hdr->gh_offset);
	hdr->gh_length = htobe32(hdr->gh_length);
	hdr->gh_seq = htobe64(hdr->gh_seq);
	hdr->gh_error = htobe16(hdr->gh_error);
}
#endif	/* _GGATE_H_ */
