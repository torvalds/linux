/*	$OpenBSD: radish.c,v 1.8 2023/04/19 12:58:16 jsg Exp $ */
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
 * radish.c
 *
 * Version:	0.9
 * Created:	May     27, 1995
 * Modified:	January 28, 1997
 * Author:	Kazu YAMAMOTO
 * Email: 	kazu@is.aist-nara.ac.jp
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "radish.h"

#include <netinet/in.h>
#include <strings.h>
#include <stdio.h>

#define FATAL(x)			\
	do {				\
		fputs(x, stderr);	\
		abort();		\
	} while (0/* CONSTCOND */)

static u_char rd_bmask [] = {
	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfc, 0xfe,
};

static u_char rd_btest [] = {
	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01,
};

u_char rd_deleted_km[1024];

/*
 * return 1 if success
 * return 0 if error
 */
int
rd_inithead(void **headp, int family, int slen, int off, int alen,
    int (*match)(void *, void *))
{
	struct radish_head *head;
	struct radish *new;
	struct sockaddr *masks;
	u_char *m;
	int num = alen * 8 + 1, i, j, q, r;
	int len = sizeof(*head) + sizeof(*new) + slen * num;

	if (*headp) return (1);
	R_Malloc(head, struct radish_head *, len);
	if (head == NULL)
		return 0;
	Bzero(head, len);
	new = (struct radish *)(head + 1);
	masks = (struct sockaddr *)(new +1);
	*headp = head;

	/*
	 * prepare all continuous masks
	 */
	m = (u_char *)masks;
	for (i = 0; i < num; i++, m += slen) {
		*m = slen;
		*(m + 1) = family;
		q = i >> 3;
		r = i & 7;
		for(j = 0; j < q; j++)
			*(m + off + j) = 0xff;
		*(m + off + j) = rd_bmask[r];
	}

	head->rdh_slen = slen;
	head->rdh_offset = off;
	head->rdh_alen = alen;
	head->rdh_masks = masks;
	head->rdh_match = match;
	head->rdh_top = new;

	new->rd_route = masks;
	new->rd_mask = masks;
	new->rd_btest = rd_btest[0];
	/* other nembers are 0 */

	return(1);
}

struct sockaddr *
rd_mask(struct sockaddr *m_arg, struct radish_head *head, int *maskp)
{
	u_char *mp, *masks = (u_char *)head->rdh_masks;
	int off = head->rdh_offset;
	int slen = head->rdh_slen;
	int alen = head->rdh_alen;
	int i = 0, masklen = 0;

	if (m_arg == NULL) {
		masklen = alen * 8;
		*maskp = masklen;
		return((struct sockaddr *)(masks + slen * masklen));
	}
	mp = (u_char *)m_arg + off;
	while ((i < alen) && (mp[i] == 0xff)) {
		masklen += 8;
		i++;
	}
	if (i < alen)
		switch (mp[i]) {
		case 0xfe: masklen += 7; break;
		case 0xfc: masklen += 6; break;
		case 0xf8: masklen += 5; break;
		case 0xf0: masklen += 4; break;
		case 0xe0: masklen += 3; break;
		case 0xc0: masklen += 2; break;
		case 0x80: masklen += 1; break;
		case 0x00: break;
		}
	*maskp = masklen;
	return((struct sockaddr *)(masks + slen * masklen));
}

int
rd_insert(struct sockaddr *d_arg, struct sockaddr *m_arg,
	struct radish_head *head, void *rt)
{
	struct radish *cur = head->rdh_top, *parent, *new;
	int off = head->rdh_offset;
	int slen = head->rdh_slen;
	int alen = head->rdh_alen;
	int i, lim, q, r, masklen;
	u_char *dp, *np, *rp;
	struct sockaddr *mask;

	mask = rd_mask(m_arg, head, &masklen);
	q = masklen >> 3;
	r = masklen & 7;

	/* Allocate a new radish.
	 * This may be overhead in the case that
	 * 	masklen == cur->rd_masklen
	 * and
	 *	route == dest.
	 */
	R_Malloc(new, struct radish *, sizeof(*new) + slen);
	if (new == NULL)
		return ENOBUFS;
	Bzero(new, sizeof(*new) + slen);
	new->rd_route = (struct sockaddr *)(new + 1);
	new->rd_mask = mask;
	new->rd_masklen = masklen;
	new->rd_masklim = q;
	new->rd_bmask = rd_bmask[r];
	new->rd_btest = rd_btest[r];
	new->rd_rtent = rt;

	/* masked copy from dest to route */
	np = (u_char *)new->rd_route;
	dp = (u_char *)d_arg;
	*np = *dp; /* sa_len */
	np[1] = dp[1]; /* sa_family */
	dp += off;
	np += off;
	i = 0;
	while (i < q) {
		np[i] = dp[i];
		i++;
	}
	np[i] = dp[i] & rd_bmask[r]; /* just in case */

	while (cur) {
		if (masklen == cur->rd_masklen) {
			rp = (u_char *)cur->rd_route + off;
			for (i = 0; i < alen; i++)
				if (np[i] != rp[i]) {
					/*
					 * masklen == cur->rd_masklen
					 * dest != route
					 */
					return rd_glue(cur, new, i, head);
				}
			/*
			 * masklen == cur->rd_masklen
			 * dest == route
			 */
			Free(new);
			if (cur->rd_rtent != NULL)
				return EEXIST;
			cur->rd_rtent = rt;
			return 0;
		}
		/*
		 * masklen != cur->rd_masklen
		 */
		if (masklen > cur->rd_masklen) {
			/*
			 * See if dest matches with cur node.
			 * (dest & mask) == route
			 */
			rp = (u_char *)cur->rd_route + off;
			lim = cur->rd_masklim;

			/* mask is continuous, thus mask is 0xff here. */
			for (i = 0; i < lim; i++)
				if(np[i] != rp[i]) {
					/*
					 * masklen > cur->rd_masklen
					 * (dest & mask) != route
					 */
					return rd_glue(cur, new, i, head);
				}
			if (cur->rd_bmask)
				if ((np[lim] & cur->rd_bmask) != rp[lim]) {
					/*
					 * masklen > cur->rd_masklen
					 * (dest & mask) != route
					 */
					return rd_glue(cur, new, lim, head);
				}
			/*
			 * masklen > cur->rd_masklen
			 * (dest & mask) == route
			 */
			if (cur->rd_btest & np[cur->rd_masklim]) {
				if (cur->rd_r != NULL) {
					cur = cur->rd_r;
					continue;
				}
				cur->rd_r = new;
				new->rd_p = cur;
				return 0;
			} else {
				if (cur->rd_l != NULL) {
					cur = cur->rd_l;
					continue;
				}
				cur->rd_l = new;
				new->rd_p = cur;
				return 0;
			}
		}
		/*
		 * masklen < cur->rd_masklen
		 */

		/* See if route matches with dest, be careful!
		 * 	dest == (route & dest_mask)
		 */
		rp = (u_char *)cur->rd_route + off;
		lim = new->rd_masklim;

		/* mask is continuous, thus mask is 0xff here. */
		for (i = 0; i < lim; i++)
			if(np[i] != rp[i]) {
				/*
				 * masklen < cur->rd_masklen
				 * dest != (route & dest_mask)
				 */
				return rd_glue(cur, new, i, head);
			}
		if (new->rd_bmask)
			if (np[lim] != (rp[lim] & new->rd_bmask)) {
				/*
				 * masklen < cur->rd_masklen
				 * dest != (route & dest_mask)
				 */
				return rd_glue(cur, new, lim, head);
			}
		/*
		 * masklen < cur->rd_masklen
		 * dest == (route & dest_mask)
		 */

		/* put the new radish between cur and its parent */
		parent = cur->rd_p;
		new->rd_p = parent;
		if (parent->rd_l == cur)
			parent->rd_l = new;
		else if (parent->rd_r == cur)
			parent->rd_r = new;
		else
			FATAL("rd_insert");
		if (new->rd_btest & rp[new->rd_masklim])
			new->rd_r = cur;
		else
			new->rd_l = cur;

		cur->rd_p = new;
		return 0;
	}
	return 1;
}

/*
 * Insert a glue radish between the current and its parent.
 * Let the current radish one child of glue radish.
 * Let the new radish the other child of glue radish.
 */
int
rd_glue(struct radish *cur, struct radish *new, int misbyte,
    struct radish_head *head)
{
	struct radish *parent = cur->rd_p, *glue;
	u_char *cp = (u_char *)cur->rd_route;
	u_char *np = (u_char *)new->rd_route;
	u_char *gp;
	int off = head->rdh_offset, slen = head->rdh_slen;
	int maskb, xor, i;

	/*
	 * Glue radish
	 */
	R_Malloc(glue, struct radish *, sizeof(*glue) + slen);
	if (glue == NULL) {
		Free (new);
		return ENOBUFS;
	}
	Bzero(glue, sizeof(*glue) + slen);

	/* calculate a bit to test */
	xor = (*(cp + off + misbyte) ^ *(np + off + misbyte)) & 0xff;
	maskb = 8;
	while(xor) {
		xor >>= 1;
		maskb--;
	}

	glue->rd_route = (struct sockaddr *)(glue + 1);
	glue->rd_masklen = 8 * misbyte + maskb;
	glue->rd_masklim = misbyte;
	glue->rd_bmask = rd_bmask[maskb];
	glue->rd_btest = rd_btest[maskb];
	glue->rd_rtent = NULL;
	glue->rd_p = parent;
	glue->rd_mask = (struct sockaddr *)
		((u_char *)head->rdh_masks + slen * glue->rd_masklen);

	/* masked copy of route */
	gp = (u_char *)glue->rd_route;
	*gp = *cp; /* sa_len */
	*(gp + 1) = *(cp + 1); /* sa_family */
	cp += off;
	gp += off;
	for(i = 0; i < misbyte; i++)
		gp[i] = cp[i];
	gp[misbyte] = cp[misbyte] & glue->rd_bmask;

	if (glue->rd_btest & cp[misbyte]) {
		glue->rd_r = cur;
		glue->rd_l = new;
	} else {
		glue->rd_r = new;
		glue->rd_l = cur;
	}

	/*
	 * Children
	 */
	new->rd_p = cur->rd_p = glue;

	/*
	 * Parent
	 */
	if (parent->rd_l == cur)
		parent->rd_l = glue;
	else if (parent->rd_r == cur)
		parent->rd_r = glue;
	else
		FATAL("rd_insert");
	return 0;
}

/*
 * Find the longest-match radish with the destination.
 * Return 1 if success, otherwise return 0.
 */

int
rd_match(struct sockaddr *d_arg, struct radish_head *head, struct radish **rdp)
{
	return rd_match_next(d_arg, head, rdp, NULL);
}

int
rd_match_next(struct sockaddr *d_arg, struct radish_head *head,
    struct radish **rdp, struct radish *cur)
{
	struct radish *target = NULL;
	int off = head->rdh_offset, i, lim;
	u_char *dp = (u_char *)d_arg + off, *cp;

	if (cur == NULL) {
		cur = head->rdh_top;
		while (cur) {
			target = cur;
			if (cur->rd_btest & *(dp + cur->rd_masklim))
				cur = cur->rd_r;
			else
				cur = cur->rd_l;
		}
	} else {
		target = cur->rd_p;
		if (target == NULL) {
			*rdp = NULL;
			return 0;
		}
	}

	/* We are now on the leaf radish. Backtrace to find the radish
	   which contains route to match. */
	do {
		/* If this radish doesn't have route,
		   we skip it and chase the next parent. */
		if (target->rd_rtent != NULL) {
			cp = (u_char *)target->rd_route + off;
			lim = target->rd_masklim;

			/* Check the edge for slight speed up */
			if (target->rd_bmask)
				if ((*(dp + lim) & target->rd_bmask)
				    != *(cp + lim)){
				nextparent:
					continue;
				}

			/* mask is always 0xff */
			for (i = 0; i < lim; i++)
				if(dp[i] != cp[i])
					/* to break the for loop */
					goto nextparent;
			/* Matched to this radish! */
			*rdp = target;
			return 1;
		}
	} while ((target = target->rd_p));
	*rdp = NULL;
	return 0;
}

/*
 * Lookup the same radish according to a pair of destination and mask.
 * Return a pointer to rtentry if exists. Otherwise, return NULL.
 */

void *
rd_lookup(struct sockaddr *d_arg, struct sockaddr *m_arg,
    struct radish_head *head)
{
	struct radish *cur = head->rdh_top;
	int off = head->rdh_offset, i, lim, olim = 0, masklen;
	u_char *dp = (u_char *)d_arg + off, *cp;

	rd_mask(m_arg, head, &masklen);

	/* Skipping forward search */
	while (cur) {
		/* Skip a radish if it doesn't have a route */
		if (cur->rd_rtent != NULL) {
			cp = (u_char *)(cur->rd_route) + off;
			lim = cur->rd_masklim;
			/* check the edge to speed up a bit */
			if (cur->rd_bmask)
				if ((*(dp + lim) & cur->rd_bmask)
				    != *(cp + lim))
					return NULL;
			/* mask is always 0xff */
			for (i = olim; i < lim; i++)
				if(dp[i] != cp[i])
					return NULL;
			if (masklen == cur->rd_masklen)
				return cur->rd_rtent;
			olim = lim;
		}
		if (cur->rd_btest & *(dp + cur->rd_masklim))
			cur = cur->rd_r;
		else
			cur = cur->rd_l;
	}
	return NULL;
}

/*
 * Delete the radish for dest and mask.
 * Return 0 if success.
 * Return ENOENT if no such radish exists.
 * Return EFAULT if try to delete intermediate radish which doesn't have route.
 */

int
rd_delete(struct sockaddr *d_arg, struct sockaddr *m_arg,
    struct radish_head *head, void **item)
{
	struct radish *cur = head->rdh_top;
	int off = head->rdh_offset, i, lim, masklen;
	u_char *dp = (u_char *)d_arg + off, *cp;

	rd_mask(m_arg, head, &masklen);
	*item = NULL; /* just in case */

	while (cur) {
		/* exit loop if dest does not match with the current node
		 * 	(dest & mask) != route
		 */
		cp = (u_char *)cur->rd_route + off;
		/* check the edge to speed up */
		if (cur->rd_bmask)
			if ((*(dp + cur->rd_masklim) & cur->rd_bmask)
			    != *(cp + cur->rd_masklim))
				return ENOENT;
		/* mask is always 0xff */
		lim = cur->rd_masklim;
		for (i = 0; i < lim; i++)
			if(dp[i] != cp[i])
				return ENOENT;

		/* See if cur is exactly what we delete */

		/* Check mask to speed up */
		if (cur->rd_masklen != masklen)
			goto next;

		cp = (u_char *)cur->rd_route + off;
		lim = head->rdh_alen;
		for (i = 0; i < lim; i++)
			if (dp[i] != cp[i])
				goto next;
		/*
		 * Both route and mask are the same.
		 */
		if (cur->rd_rtent == NULL) {
			/* Leaf always has route, so this radish
			 * must be intermediate.
			 * Can't delete intermediate radish which
			 * doesn't have route.
			 */
			return EFAULT;
		}
		*item = cur->rd_rtent;
		{
			/* used to report the deleted entry back */
			u_char rl = cur->rd_route->sa_len;
			u_char ml = cur->rd_mask->sa_len;

			bcopy(cur->rd_route, rd_deleted_km, rl);
			bcopy(cur->rd_mask, rd_deleted_km + rl, ml);
		}
		if (cur->rd_l && cur->rd_r) {
			/* This radish has two children */
			cur->rd_rtent = NULL;
			return 0;
		}
		/* Unlink the radish that has 0 or 1 child
		 * and surely has a route.
		 */
		rd_unlink(cur, head->rdh_top);
		return 0;

	next:
		/* search corresponding subtree */
		if (cur->rd_btest & *(dp + cur->rd_masklim)) {
			if (cur->rd_r) {
				cur = cur->rd_r;
				continue;
			} else
				break;
		} else {
			if (cur->rd_l) {
				cur = cur->rd_l;
				continue;
			} else
				break;
		}
	}
	return ENOENT;
}

/*
 * Free radish and refine radish tree.
 * rd_unlink is called with radish which have 0 or 1 child and route.
 * Error causes panic, so return only when success.
 */

void
rd_unlink(struct radish *cur, struct radish *top)
{
	struct radish *parent, *child;

	if (cur == top) {
		cur->rd_rtent = NULL;
		return;
	}

	if ((cur->rd_l == 0) && (cur->rd_r == 0)) {
		/* No child, just delete it. */
		parent = cur->rd_p;
		if (parent->rd_l == cur) {
			parent->rd_l = NULL;
			Free(cur);
		} else if (parent->rd_r == cur) {
			parent->rd_r = NULL;
			Free(cur);
		} else
			FATAL("rd_unlink");
		if (parent->rd_rtent == NULL && parent != top)
			/* Parent is not necessary, refine radish. */
			rd_unlink(parent, top);
	} else {
		/*
		 * There is one child, never two.
		 * Make its child its parent's child.
		 */
		if (cur->rd_l)
			child = cur->rd_l;
		else
			child = cur->rd_r;
		parent = cur->rd_p;
		child->rd_p = parent;
		if (parent->rd_l == cur) {
			parent->rd_l = child;
			Free(cur);
		} else if (parent->rd_r == cur) {
			parent->rd_r = child;
			Free(cur);
		} else
			FATAL("rd_unlink");
	}
}

int
rd_walktree(struct radish_head *h, register int (*f)(struct radish *, void *),
    void *w)
{
	int error = 0;
	struct radish *par = NULL, *cur, *top = h->rdh_top;

	cur = top;
	for (;;) {
		while (cur) {
			if (cur->rd_rtent != NULL)
				if ((error = (*f)(cur, w)))
					return error;
			par = cur;
			cur = cur->rd_l;
		}
		cur = par;
		while (cur->rd_r == NULL || par == cur->rd_r) {
			par = cur;
			cur = cur->rd_p;
			if (cur == NULL) return 0;
		}
		par = cur;
		cur = cur->rd_r;
	}
}

/* This function can be mush easier in the context of radish.
 * For instance, using rd_mask. But we stay the original because
 * it works.
 */
int
rd_refines(void *m_arg, void *n_arg)
{
	register caddr_t m = m_arg, n = n_arg;
	register caddr_t lim, lim2 = lim = n + *(u_char *)n;
	int longer = (*(u_char *)n++) - (int)(*(u_char *)m++);
	int masks_are_equal = 1;

	if (longer > 0)
		lim -= longer;
	while (n < lim) {
		if (*n & ~(*m))
			return 0;
		if (*n++ != *m++)
			masks_are_equal = 0;

	}
	while (n < lim2)
		if (*n++)
			return 0;
	if (masks_are_equal && (longer < 0))
		for (lim2 = m - longer; m < lim2; )
			if (*m++)
				return 1;
	return (!masks_are_equal);
}
