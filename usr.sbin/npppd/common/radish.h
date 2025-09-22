/*	$OpenBSD: radish.h,v 1.6 2017/05/30 17:52:05 yasuoka Exp $ */
/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
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
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * radish.h
 *
 * Version:	0.5
 * Created:	May     27, 1995
 * Modified:	January 11, 1997
 * Author:	Kazu YAMAMOTO
 * Email: 	kazu@is.aist-nara.ac.jp
 */

#ifndef _RADISH_H_
#define _RADISH_H_

struct radish {
	struct sockaddr	*rd_route;	/* destination route */
	struct sockaddr	*rd_mask;	/* destination mask */
	u_int rd_masklen;		/* length of mask */
	u_short rd_masklim;		/* length of mask / 8 : test point */
	u_char  rd_bmask;		/* byte mask */
	u_char	rd_btest;		/* bit to test */
	struct radish *rd_p;		/* parent */
	struct radish *rd_l;		/* left child */
	struct radish *rd_r;		/* right child */
	void *rd_rtent;			/* rtentry */
};

struct radish_head {
	int 	rdh_slen;	/* socket address length */
	int 	rdh_offset;	/* address start byte */
	int 	rdh_alen;	/* address length */
	void 	*rdh_masks;
	struct radish *rdh_top;
	int	(*rdh_match)(void *, void *);
};

#ifndef Bcmp
#define Bcmp(a, b, n) memcmp(((char *)(a)), ((char *)(b)), (size_t)(n))
#endif
#ifndef Bzero
#define Bzero(p, n) memset((char *)(p), 0, (size_t)(n))
#endif
#define R_Malloc(p, t, n) (p = (t) malloc((n)))
#define Free(p) free((char *)p);

/*
 * prototype for radish functions
 */

int rd_inithead(void **, int, int, int, int, int (*)(void *, void *));
struct sockaddr *rd_mask(struct sockaddr *, struct radish_head *, int *);

int rd_insert(struct sockaddr *, struct sockaddr *,
		   struct radish_head *, void *);
int rd_glue(struct radish *, struct radish *, int, struct radish_head *);
int rd_match(struct sockaddr *, struct radish_head *, struct radish **);
int rd_match_next(struct sockaddr *, struct radish_head *, struct radish **, struct radish *);
void *rd_lookup(struct sockaddr *,
			      struct sockaddr *, struct radish_head *);
int rd_delete(struct sockaddr *, struct sockaddr *,
		   struct radish_head *, void **);
void rd_unlink(struct radish *, struct radish *);
int  rd_walktree(struct radish_head *, int (*)(struct radish *, void *), void *);
int  rd_refines(void *, void *);
#endif
