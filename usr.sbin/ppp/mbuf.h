/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996 - 2001 Brian Somers <brian@Awfulhak.org>
 *          based on work by Toshiharu OHNO <tony-o@iij.ad.jp>
 *                           Internet Initiative Japan, Inc (IIJ)
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
 *
 * $FreeBSD$
 */

struct mbuf {
  size_t m_size;		/* size allocated (excluding header) */
  u_short m_offset;		/* offset from header end to start position */
  size_t m_len;			/* available byte count in buffer */
  short m_type;			/* MB_* below */
  struct mbuf *m_next;		/* link to next mbuf */
  struct mbuf *m_nextpkt;	/* link to next packet */
  unsigned long priv;		/* private data - holds HDLC escape count */
  /* buffer space is malloc()d directly after the header */
};

struct mqueue {
  struct mbuf *top;
  struct mbuf *last;
  size_t len;
};

#define MBUF_CTOP(bp) \
	((bp) ? (u_char *)((bp)+1) + (bp)->m_offset : (u_char *)bp)

#define CONST_MBUF_CTOP(bp) \
	((bp) ? (const u_char *)((bp)+1) + (bp)->m_offset : (const u_char *)bp)

#define MB_IPIN		0
#define MB_IPOUT	1
#define MB_IPV6IN	2
#define MB_IPV6OUT	3
#define MB_NATIN	4
#define MB_NATOUT	5
#define MB_MPIN		6
#define MB_MPOUT	7
#define MB_VJIN		8
#define MB_VJOUT	9
#define MB_ICOMPDIN	10
#define MB_ICOMPDOUT	11
#define MB_COMPDIN	12
#define MB_COMPDOUT	13
#define MB_LQRIN	14
#define MB_LQROUT	15
#define MB_ECHOIN	16
#define MB_ECHOOUT	17
#define MB_PROTOIN	18
#define MB_PROTOOUT	19
#define MB_ACFIN	20
#define MB_ACFOUT	21
#define MB_SYNCIN	22
#define MB_SYNCOUT	23
#define MB_HDLCIN	24
#define MB_HDLCOUT	25
#define MB_ASYNCIN	26
#define MB_ASYNCOUT	27
#define MB_CBCPIN	28
#define MB_CBCPOUT	29
#define MB_CHAPIN	30
#define MB_CHAPOUT	31
#define MB_PAPIN	32
#define MB_PAPOUT	33
#define MB_CCPIN	34
#define MB_CCPOUT	35
#define MB_IPCPIN	36
#define MB_IPCPOUT	37
#define MB_IPV6CPIN	38
#define MB_IPV6CPOUT	39
#define MB_LCPIN	40
#define MB_LCPOUT	41
#define MB_UNKNOWN	42
#define MB_MAX		MB_UNKNOWN

#define M_MAXLEN	(4352 - sizeof(struct mbuf))	/* > HDLCSIZE */

struct cmdargs;

extern size_t m_length(struct mbuf *);
extern struct mbuf *m_get(size_t, int);
extern struct mbuf *m_free(struct mbuf *);
extern void m_freem(struct mbuf *);
extern void mbuf_Write(struct mbuf *, const void *, size_t);
extern struct mbuf *mbuf_Read(struct mbuf *, void *, size_t);
extern size_t mbuf_View(struct mbuf *, void *, size_t);
extern struct mbuf *m_prepend(struct mbuf *, const void *, size_t, u_short);
extern struct mbuf *m_adj(struct mbuf *, ssize_t);
extern struct mbuf *m_pullup(struct mbuf *);
extern void m_settype(struct mbuf *, int);
extern struct mbuf *m_append(struct mbuf *, const void *, size_t);

extern int mbuf_Show(struct cmdargs const *);

extern void m_enqueue(struct mqueue *, struct mbuf *);
extern struct mbuf *m_dequeue(struct mqueue *);
